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

template <typename T>
static inline bool extract_int(std::string_view line, const char* needle, T& dest) {
    std::size_t start = 0;

    // find needle, or start from beginning if empty
    if (needle && *needle) {
        std::size_t pos = line.find(needle);
        if (pos == std::string_view::npos) return false;
        start = pos + std::strlen(needle);
    }

    // slice the line from start
    line.remove_prefix(start);

    if (line.empty() || (line.front() < '0' || line.front() > '9')) return false;

    // std::from_chars will automatically stop at first non-digit
    auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), dest);

    return ec == std::errc();
}

/**
* Parse up to 64 bit representations of a CRC32 hash, discarding the upper 32 bits
* This is necessary become some posts have malformed hashes
 */
std::optional<uint32_t> parse_crc32(std::string_view crc32) {
    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(crc32.data(), crc32.data() + crc32.size(), value, 16);

    // Fail if conversion failed
    if (ec != std::errc{}) {
        return std::nullopt;
    }

    return static_cast<uint32_t>(value); // Discard upper 32 bits
}

static inline void decoder_detect_format(Decoder* instance, std::string_view line) {
	if (line.rfind("=ybegin ", 0) == 0)
	{
		instance->format = YENC;
        return;
	}

	instance->format = UNKNOWN;
}

static inline void decoder_process_yenc_header(Decoder* instance, std::string_view line) {
	if (line.rfind("=ybegin ", 0) != std::string::npos)
	{
        std::string_view remaining = line.substr(7);
        extract_int(remaining, " size=", instance->file_size);
        if (!extract_int(remaining, " part=", instance->part)) {
            // Not multi-part
            instance->body = true;
        }
        extract_int(remaining, " total=", instance->total);

        std::string::size_type pos = 0;
	    if ((pos = remaining.find(" name=")) != std::string::npos) {
            std::string_view name = remaining.substr(pos + 6);
            // Not sure \r\n is necessary lines already have them stripped
            if ((pos = name.find_last_not_of("\r\n\0")) != std::string::npos) {
                instance->file_name = PyUnicode_DecodeUTF8(name.data(), pos + 1, NULL);
                if (!instance->file_name) {
                    PyErr_Clear();
                    instance->file_name = PyUnicode_DecodeLatin1(name.data(), pos + 1, NULL);
                }
            }
	    }
	} else if (line.rfind("=ypart ", 0) != std::string::npos) {
        instance->body = true;

        std::string_view remaining = line.substr(6);
        if (extract_int(remaining, " begin=", instance->part_begin) && instance->part_begin > 0) {
            instance->part_begin--;
        }
        if (extract_int(remaining, " end=", instance->part_size) && instance->part_size >= instance->part_begin) {
            instance->part_size -= instance->part_begin;
        }
	} else if (line.rfind("=yend ", 0) != std::string::npos) {
        std::string::size_type pos = 0;
        std::string_view crc32;
	    if ((pos = line.find(" pcrc32=", 5)) != std::string::npos) {
	        crc32 = line.substr(pos + 8);
	    } else if ((pos = line.find(" crc32=", 5)) != std::string::npos) {
	        crc32 = line.substr(pos + 7);
	    }
        if (crc32.length() >= 8) {
            instance->crc_expected = parse_crc32(crc32);
        }
	}
}

static void decoder_dealloc(Decoder* self)
{
    Py_XDECREF(self->data);
    Py_XDECREF(self->file_name);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int decoder_getbuffer(PyObject *obj, Py_buffer *view, int flags)
{
    Decoder *self = (Decoder *)obj;

    if (self->data == NULL) {
        PyErr_SetString(PyExc_BufferError, "No data available");
        return -1;
    }

    // Remove writeable request if present
    flags &= ~PyBUF_WRITABLE;

    // Ensure the underlying object supports the buffer protocol
    if (PyObject_GetBuffer(self->data, view, flags) < 0) {
        PyErr_SetString(PyExc_BufferError, "Underlying data does not support buffer protocol");
        return -1;
    }

    // Explicitly mark buffer as read-only
    view->readonly = 1;

    return 0;
}

static void decoder_releasebuffer(PyObject *obj, Py_buffer *view)
{
    PyBuffer_Release(view);
}

static PyBufferProcs decoder_as_buffer = {
    decoder_getbuffer,
    decoder_releasebuffer
};

static PyObject* decoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    Decoder* self = (Decoder*)type->tp_alloc(type, 0);
    if (!self) return NULL;

    // Not necessary because they are the zero values
    self->format = UNKNOWN;
    self->state = RapidYenc::YDEC_STATE_CRLF;

    return (PyObject*)self;
}

static PyObject* decoder_get_data(Decoder* self, void* closure)
{
    return PyMemoryView_FromObject((PyObject*)self);
}

static PyObject* decoder_get_file_name(Decoder* self, void *closure)
{
    if (self->file_name == NULL) {
        return PyUnicode_New(0, 0);
    }
    Py_INCREF(self->file_name);
    return self->file_name;
}


static PyObject* decoder_get_crc(Decoder* self, void *closure)
{
    if (!self->crc_expected.has_value() || self->crc != self->crc_expected.value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromUnsignedLong(self->crc);
}

static PyObject* decoder_get_crc_expected(Decoder* self, void *closure)
{
    if (!self->crc_expected.has_value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromUnsignedLong(self->crc_expected.value());
}

static PyObject* decoder_get_success(Decoder* self, void *closure)
{
    // Special case for STAT responses
    if (self->status_code == NNTP_STAT) {
        Py_RETURN_TRUE;
    }

    if (self->status_code != NNTP_BODY && self->status_code != NNTP_ARTICLE) {
        Py_RETURN_FALSE;
    }

    if (self->file_name == NULL)
    {
        Py_RETURN_FALSE;
    }

    if (!self->crc_expected.has_value()) {
        // CRC32 not found - article is invalid
        Py_RETURN_FALSE;
    }

    if (self->crc != self->crc_expected.value()) {
        Py_RETURN_FALSE;
    }

    Py_RETURN_TRUE;
}

static Py_ssize_t decoder_decode_yenc(Decoder *instance, char *buf, Py_ssize_t buf_len) {
    if (buf_len < 0) {
        return 0;
    }

    if (instance->data == nullptr) {
        // Get the size and sanity check the values
        const Py_ssize_t expected_size = std::min(
            static_cast<Py_ssize_t>(YENC_MAX_PART_SIZE),
            std::max(
                instance->part_size > 0 ? instance->part_size : instance->file_size, // only multi-part have a part size
                buf_len // for safety ensure we allocate at least enough to process the whole buffer
            )
        );
        instance->data = PyByteArray_FromStringAndSize(nullptr, expected_size);
        if (!instance->data) {
            PyErr_SetNone(PyExc_MemoryError);
            return -1;
        }
    } else if (instance->data_position + buf_len > PyBytes_GET_SIZE(instance->data)) {
        // For safety resize to size of buffer
        if (PyByteArray_Resize(instance->data, instance->data_position + buf_len) == -1) {
            return -1;
        }
    }

    char *src_ptr = buf;
    char *dst_ptr = PyByteArray_AsString(instance->data) + instance->data_position;
    char *dest_start = dst_ptr;

    RapidYenc::YencDecoderEnd end;
    Py_ssize_t nsrc, ndst;
    uint32_t crc = instance->crc;

    // Decode the data
    Py_BEGIN_ALLOW_THREADS;

    end = RapidYenc::decode_end((const void**)&src_ptr, reinterpret_cast<void **>(&dst_ptr), buf_len, &instance->state);
    nsrc = src_ptr - buf;
    ndst = dst_ptr - dest_start;
    crc = RapidYenc::crc32(dest_start, ndst, crc);

    Py_END_ALLOW_THREADS;

    instance->data_position += ndst;
    instance->crc = crc;

    switch (end) {
        case RapidYenc::YDEC_END_NONE: {
            if (instance->state == RapidYenc::YDEC_STATE_CRLFEQ) {
                instance->state = RapidYenc::YDEC_STATE_CRLF;
                return nsrc - 1;
            }
            return nsrc;
        }
        case RapidYenc::YDEC_END_CONTROL:
            instance->body = false;
            // step back to include =y
            return nsrc - 2;
        case RapidYenc::YDEC_END_ARTICLE:
            instance->body = false;
            // step back to include .\r\n
            return nsrc - 3;
    }

    return nsrc;
}

static Py_ssize_t decoder_decode_buffer(Decoder *instance, const Py_buffer *input_buffer) {
    auto buf = static_cast<char*>(input_buffer->buf);
    size_t buf_len = input_buffer->len;
    Py_ssize_t read = 0;

    if (instance->body && instance->format == YENC) {
        read = decoder_decode_yenc(instance, buf, buf_len);
        if (read == -1) return -1;
        if (instance->body) {
            return read;
        }
    }

    std::string_view s = std::string_view(buf, buf_len);
    std::size_t pos = 0;
    while ((pos = s.find("\r\n", read)) != std::string::npos) {
        std::string_view line = s.substr(read, pos - read); // does not include \r\n
        read = pos + 2;

        if (line == ".") {
            instance->done = true;
            return read;
        }

        if (instance->format == UNKNOWN) {
            if (!instance->status_code && line.length() >= 3) {
                // First line should start with a 3 character response code
                if (!extract_int(line, "", instance->status_code)
                    || (instance->status_code != NNTP_BODY && instance->status_code != NNTP_ARTICLE)) {
                    // Not a multi-line response... we are done
                    instance->done = true;
                    break;
                }
            }

            decoder_detect_format(instance, line);
        }

        if (instance->format == YENC) {
            decoder_process_yenc_header(instance, line);
            if (instance->body) {
                const auto n = decoder_decode_yenc(instance, buf + read, buf_len - read);
                if (n == -1) return -1;
                read += n;
                if (instance->body) {
                    return read;
                }
            }
        } else if (instance->format == UU) {
            // Not implemented
        }
    }

    return read;
}

PyObject* decoder_decode(PyObject* self, PyObject* Py_memoryview_obj) {
    Decoder* instance = reinterpret_cast<Decoder*>(self);

    PyObject* unprocessed_memoryview = Py_None;

    if (instance->done) {
        PyErr_SetString(PyExc_ValueError, "Already finished decoding");
        return NULL;
    }

    // Verify it's a bytearray
    if (!PyMemoryView_Check(Py_memoryview_obj)) {
        PyErr_SetString(PyExc_TypeError, "Expected memoryview");
        return NULL;
    }

    // Get buffer and check it is a valid size and type
    Py_buffer *input_buffer = PyMemoryView_GET_BUFFER(Py_memoryview_obj);
    if (!PyBuffer_IsContiguous(input_buffer, 'C') || input_buffer->len <= 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid data length or order");
        return NULL;
    }

    const auto read = decoder_decode_buffer(instance, input_buffer);
    if (read == -1) return NULL;
    instance->bytes_read += read;

    if (instance->done && instance->data != NULL && instance->data_position != PyBytes_GET_SIZE(instance->data)) {
        // Adjust the Python-size of the bytesarray-object
        // This will only do a real resize if the data shrunk by half, so never in our case!
        // Resizing a bytes object always does a real resize, so more costly
        PyByteArray_Resize(instance->data, instance->data_position);
    }

    const Py_ssize_t unprocessed_length = input_buffer->len - read;
    if (unprocessed_length > 0) {
        Py_buffer subbuf = *input_buffer; // shallow copy
        subbuf.buf = static_cast<char *>(input_buffer->buf) + read;
        subbuf.len = unprocessed_length;

        // Adjust shape - should always be true
        if (subbuf.ndim == 1 && subbuf.shape) {
            subbuf.shape[0] = unprocessed_length;
        }

        unprocessed_memoryview = PyMemoryView_FromBuffer(&subbuf);
    } else {
        Py_INCREF(unprocessed_memoryview);
    }

    return Py_BuildValue("(O, O)", instance->done ? Py_True : Py_False, unprocessed_memoryview);
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

static PyObject* decoder_repr(Decoder* self)
{
    return PyUnicode_FromFormat(
        "<Decoder: done=%s, status_code=%d, file_name=%R, length=%zd>",
        self->done ? "True" : "False",
        self->status_code,
        decoder_get_file_name(self, NULL),
        self->data_position);
}

static PyMethodDef decoder_methods[] = {
    {"decode", decoder_decode, METH_O, ""},
    {nullptr}
};

static PyMemberDef decoder_members[] = {
    {"file_size", T_PYSSIZET, offsetof(Decoder, file_size), READONLY, ""},
    {"part_begin", T_PYSSIZET, offsetof(Decoder, part_begin), READONLY, ""},
    {"part_size", T_PYSSIZET, offsetof(Decoder, part_size), READONLY, ""},
    {"status_code", T_INT, offsetof(Decoder, status_code), READONLY, ""},
    {"bytes_read", T_ULONGLONG, offsetof(Decoder, bytes_read), READONLY, ""},
    {nullptr}
};

static PyGetSetDef decoder_gets_sets[] = {
    {"data", (getter)decoder_get_data, NULL, NULL, NULL},
    {"file_name", (getter)decoder_get_file_name, NULL, NULL, NULL},
    {"crc", (getter)decoder_get_crc, NULL, NULL, NULL},
    {"crc_expected", (getter)decoder_get_crc_expected, NULL, NULL, NULL},
    {"success", (getter)decoder_get_success, NULL, NULL, NULL},
    {NULL}
};

PyTypeObject DecoderType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "sabctools.Decoder",            // tp_name
    sizeof(Decoder),                // tp_basicsize
    0,                              // tp_itemsize
    (destructor)decoder_dealloc,    // tp_dealloc
    0,                              // tp_vectorcall_offset
    0,                              // tp_getattr
    0,                              // tp_setattr
    0,                              // tp_as_async
    (reprfunc)decoder_repr,         // tp_repr
    0,                              // tp_as_number
    0,                              // tp_as_sequence
    0,                              // tp_as_mapping
    0,                              // tp_hash
    0,                              // tp_call
    0,                              // tp_str
    0,                              // tp_getattro
    0,                              // tp_setattro
    &decoder_as_buffer,             // tp_as_buffer
    Py_TPFLAGS_DEFAULT,             // tp_flags
    PyDoc_STR("Decoder"),           // tp_doc
    0,                              // tp_traverse
    0,                              // tp_clear
    0,                              // tp_richcompare
    0,                              // tp_weaklistoffset
    0,                              // tp_iter
    0,                              // tp_iternext
    decoder_methods,                // tp_methods
    decoder_members,                // tp_members
    decoder_gets_sets,              // tp_getset
    0,                              // tp_base
    0,                              // tp_dict
    0,                              // tp_descr_get
    0,                              // tp_descr_set
    0,                              // tp_dictoffset
    0,                              // tp_init
    PyType_GenericAlloc,            // tp_alloc
    (newfunc)decoder_new,           // tp_new
};
