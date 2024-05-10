/*
 * Copyright 2007-2023 The SABnzbd-Team (sabnzbd.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "yenc.h"

#include "yencode/common.h"
#include "yencode/encoder.h"
#include "yencode/decoder.h"
#include "yencode/crc.h"

/* Function definitions */

// memmem clone (as function isn't in C standard library)
static void* my_memmem(const void* haystack, size_t haystackLen, const void* needle, size_t needleLen) {
    if(needleLen > haystackLen || haystack == NULL || needle == NULL)
        return NULL;
    if(needleLen == 0 || haystack == needle)
        return (void*)haystack;

    size_t checkBytes = haystackLen - needleLen +1;
    char* p;
    const char* haystackPos = (const char*)haystack;
    const char* needleStr = (const char*)needle;
    while((p = (char*)memchr(haystackPos, needleStr[0], checkBytes))) {
        if(!memcmp(p+1, needleStr+1, needleLen-1))
            break;
        size_t skip = p - haystackPos +1;
        checkBytes -= skip;
        haystackPos += skip;
    }
    return p;
}

static inline char* my_memstr(const void* haystack, size_t haystackLen, const char* str, int pointToEnd) {
    size_t len = strlen(str);
    char* p = (char*)my_memmem(haystack, haystackLen, str, len);
    if(p && pointToEnd)
        return p + len;
    return p;
}

PyObject* yenc_decode(PyObject* self, PyObject* Py_memoryview_obj) {
    // The input/output PyObjects
    (void)self;
    PyObject *retval = NULL;
    Py_buffer *Py_buffer_obj;
    PyObject *Py_output_bytearray = NULL;
    PyObject *Py_output_filename = NULL;
    PyObject *Py_output_crc = NULL;

    // Used buffers
    char *cur_char = NULL;
    char *start_loc = NULL;
    char *end_loc = NULL;
    char *dest_loc = NULL;
    uint32_t crc = 0;
    uint32_t crc_yenc = 0;
    size_t yenc_data_length;
    size_t output_len;
    unsigned long long file_size = 0;
    unsigned long long part_begin = 0;
    unsigned long long part_end = 0;
    unsigned long long part_size = 0;
    const char* crc_pos;

    // Verify it's a bytearray
    if (!PyMemoryView_Check(Py_memoryview_obj)) {
        PyErr_SetString(PyExc_TypeError, "Expected memoryview");
        return NULL;
    }

    // Get buffer and check it is a valid size and type
    Py_buffer_obj = PyMemoryView_GET_BUFFER(Py_memoryview_obj);
    if (!PyBuffer_IsContiguous(Py_buffer_obj, 'C') || Py_buffer_obj->len <= 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid data length or order");
        retval = NULL;
        goto finish;
    }

    cur_char = (char*)Py_buffer_obj->buf;
    end_loc = cur_char + Py_buffer_obj->len;
    output_len = Py_buffer_obj->len;

    /*
     ANALYZE HEADER
     Always in the same format, e.g.:

     =ybegin part=41 line=128 size=49152000 name=90E2Sdvsmds0801dvsmds90E.part06.rar
     =ypart begin=15360001 end=15744000

     But we only care about the filename
    */
    // Start of header
    start_loc = my_memstr(cur_char, end_loc - cur_char, "=ybegin", 1);
    if (!start_loc) {
        PyErr_SetString(PyExc_ValueError, "Invalid yEnc header");
        retval = NULL;
        goto finish;
    }

    // Get the size of the reconstructed file
    start_loc = my_memstr(start_loc, end_loc - start_loc, "size=", 1);
    if (start_loc) {
        file_size = atoll(start_loc);
    }

    // Find start of the filename
    start_loc = my_memstr(start_loc, end_loc - start_loc, " name=", 1);
    if (!start_loc) {
        PyErr_SetString(PyExc_ValueError, "Could not find yEnc filename");
        retval = NULL;
        goto finish;
    }

    // Extract filename
    cur_char = start_loc;
    for (; *cur_char != YENC_LF && *cur_char != YENC_CR && *cur_char != YENC_ZERO && cur_char < end_loc; cur_char++);
    Py_output_filename = PyUnicode_DecodeUTF8(start_loc, cur_char - start_loc, NULL);

    // In case it's invalid UTF8, we try the latin1 fallback
    if (!Py_output_filename) {
        PyErr_Clear();
        Py_output_filename = PyUnicode_DecodeLatin1(start_loc, cur_char - start_loc, NULL);
    }

    // Check for =ypart in order to get begin/end
    start_loc = my_memstr(cur_char, end_loc - cur_char, "=ypart ", 1);
    if (start_loc) {
        // Should be right after the "=part"
        start_loc = my_memstr(start_loc, end_loc - start_loc, "begin=", 1);
        if (start_loc) {
            part_begin = atoll(start_loc);
        }
        start_loc = my_memstr(start_loc, end_loc - start_loc, "end=", 1);
        if (start_loc) {
            part_end = atoll(start_loc);
        }

        // Get the size and sanity check the values
        part_size = part_end - part_begin + 1;
        if(part_end > part_begin && part_size > 0 && part_size <= YENC_MAX_PART_SIZE) {
            part_begin = part_begin - 1;
        } else {
            part_size = part_end = part_begin = 0;
        }

        // Move to end of this line
        cur_char = start_loc;
        for (; *cur_char != YENC_LF && *cur_char != YENC_CR && *cur_char != YENC_ZERO && cur_char < end_loc; cur_char++);
    }
    start_loc = cur_char;

    /*
        Looking for the end, format:
        =yend size=384000 part=41 pcrc32=084e170f
    */
    // Make sure we don't go past the end of the buffer
    if (end_loc - YENC_MAX_TAIL_BYTES > cur_char) {
        cur_char = end_loc - YENC_MAX_TAIL_BYTES;
    }
    cur_char = my_memstr(cur_char, end_loc - cur_char, "\r\n=yend", 0);
    if (!cur_char) {
        PyErr_SetString(PyExc_ValueError, "Invalid yEnc footer");
        retval = NULL;
        goto finish;
    }
    yenc_data_length = cur_char - start_loc;

    // Try to find the crc32 of the part (skip "\r\n=yend")
    cur_char += 7;
    crc_pos = my_memstr(cur_char, end_loc - cur_char, " pcrc32=", 1);

    // Sometimes only crc32 is used
    if (!crc_pos) {
        crc_pos = my_memstr(cur_char, end_loc - cur_char, " crc32=", 1);
    }

    // Parse CRC32
    if (crc_pos && (end_loc - crc_pos) >= 8) {
        // Parse up to 64 bit representations of a CRC32 hash, discarding the upper 32 bits
        // This is necessary become some posts have malformed hashes
        crc_yenc = strtoull(crc_pos, NULL, 16);
    } else {
        // CRC32 not found - article is invalid
        PyErr_SetString(PyExc_ValueError, "Invalid CRC in footer");
        retval = NULL;
        goto finish;
    }

    // Create our destination bytearray
    Py_output_bytearray = PyByteArray_FromStringAndSize(NULL, yenc_data_length);
    if(!Py_output_bytearray) {
        PyErr_SetNone(PyExc_MemoryError);
        retval = NULL;
        goto finish;
    }
    dest_loc = PyByteArray_AsString(Py_output_bytearray);

    // Lift the GIL
    Py_BEGIN_ALLOW_THREADS;

    // send to decoder
    RapidYenc::YencDecoderState state = RapidYenc::YDEC_STATE_CRLF;
    output_len = RapidYenc::decode(1, start_loc, dest_loc, yenc_data_length, &state);
    crc = RapidYenc::crc32(dest_loc, output_len, crc);

    // Return GIL to perform Python modifications
    Py_END_ALLOW_THREADS;

    // Is there a valid CRC?
    if (crc != crc_yenc) {
        Py_output_crc = Py_None;
        Py_INCREF(Py_output_crc);
    } else {
        Py_output_crc = PyLong_FromUnsignedLong(crc);
    }

    // Adjust the Python-size of the bytesarray-object
    // This will only do a real resize if the data shrunk by half, so never in our case!
    // Resizing a bytes object always does a real resize, so more costly
    PyByteArray_Resize(Py_output_bytearray, output_len);

    // Build output
    retval = Py_BuildValue("(O, O, K, K, K, N)", Py_output_bytearray, Py_output_filename, file_size, part_begin, part_size, Py_output_crc);

finish:
    Py_XDECREF(Py_output_bytearray);
    Py_XDECREF(Py_output_filename);
    return retval;
}

static inline size_t YENC_MAX_SIZE(size_t len, size_t line_size) {
    size_t ret = len * 2    /* all characters escaped */
        + 2 /* allocation for offset and that a newline may occur early */
#if !defined(YENC_DISABLE_AVX256)
        + 64 /* allocation for YMM overflowing */
#else
        + 32 /* allocation for XMM overflowing */
#endif
    ;
    /* add newlines, considering the possibility of all chars escaped */
    if(line_size == 128) // optimize common case
        return ret + 2 * (len >> 6);
    return ret + 2 * ((len*2) / line_size);
}

PyObject* yenc_encode(PyObject* self, PyObject* Py_input_string)
{
    (void)self;
    PyObject *Py_output_string;
    PyObject *retval = NULL;

    char *input_buffer = NULL;
    char *output_buffer = NULL;
    size_t input_len = 0;
    size_t output_len = 0;
    uint32_t crc;

    // Verify the input is a bytes string
    if(!PyBytes_Check(Py_input_string)) {
        PyErr_SetString(PyExc_TypeError, "Expected bytes");
        return NULL;
    }

    // Initialize buffers and CRC's
    input_len = PyBytes_Size(Py_input_string);
    input_buffer = (char *)PyBytes_AsString(Py_input_string);
    output_buffer = (char *)malloc(YENC_MAX_SIZE(input_len, YENC_LINESIZE));
    if(!output_buffer)
        return PyErr_NoMemory();

    // Free GIL, in case it helps
    Py_BEGIN_ALLOW_THREADS;

    // Encode result
    int column = 0;
    output_len = RapidYenc::encode(YENC_LINESIZE, &column, input_buffer, output_buffer, input_len, 1);
    crc = RapidYenc::crc32(input_buffer, input_len, 0);

    // Restore GIL so we can build Python strings
    Py_END_ALLOW_THREADS;

    // Build output string
    Py_output_string = PyBytes_FromStringAndSize((char *)output_buffer, output_len);
    if(Py_output_string)
        retval = Py_BuildValue("(S,L)", Py_output_string, (long long)crc);

    Py_XDECREF(Py_output_string);
    free(output_buffer);
    return retval;
}
