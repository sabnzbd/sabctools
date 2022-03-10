 /*=============================================================================
 *
 * Copyright (C) 2003, 2011 Alessandro Duca <alessandro.duca@gmail.com>
 * Modified in 2016 by Safihre <safihre@sabnzbd.org> for use within SABnzbd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *=============================================================================
 */

#include "sabyenc3.h"

#include "yencode/common.h"
#include "yencode/encoder.h"
#include "yencode/decoder.h"
#include "yencode/crc.h"

/* Declarations */

/* Function declarations */
PyMODINIT_FUNC PyInit_sabyenc3(void);
static size_t decode_buffer_usenet(PyObject *, char *, int, char **, Bool *);
static char * find_text_in_pylist(PyObject *, const char *, char **, int *);
int extract_filename_from_pylist(PyObject *, int *, char **, char **, char **);
uLong extract_int_from_pylist(PyObject *, int *, char **, char **);


/* Python API requirements */
static PyMethodDef sabyenc3_methods[] = {
    {
        "decode_usenet_chunks",
        decode_usenet_chunks,
        METH_VARARGS,
        "decode_usenet_chunks(list_of_chunks)"
    },
    {
        "encode",
        encode,
        METH_VARARGS,
        "encode(input_string)"
    },
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef sabyenc3_definition = {
    PyModuleDef_HEAD_INIT,
    "sabyenc3",
    "Processing of raw NNTP-yEnc streams for SABnzbd.",
    -1,
    sabyenc3_methods
};

PyMODINIT_FUNC PyInit_sabyenc3(void) {
    // Initialize and add version / SIMD information
    Py_Initialize();
    encoder_init();
    decoder_init();
    crc_init();
    PyObject* module = PyModule_Create(&sabyenc3_definition);
    PyModule_AddStringConstant(module, "__version__", SABYENC_VERSION);
    PyModule_AddStringConstant(module, "simd", simd_detected());
    return module;
}


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

static size_t decode_buffer_usenet(PyObject *Py_input_list, char *output_buffer, int num_bytes_reserved,
                                char **filename_out, Bool *crc_correct) {
    // For the list
    Py_ssize_t num_lines;
    int list_index = 0;

    // Search variables
    char *cur_char; // Pointer to search result
    char *start_loc; // Pointer to current char

    // Other vars
    uLong part_begin = 0;
    int part_size = 0;
    size_t decoded_bytes = 0;

    /*
     ANALYZE HEADER
     Always in the same format, e.g.:

     =ybegin part=41 line=128 size=49152000 name=90E2Sdvsmds0801dvsmds90E.part06.rar
     =ypart begin=15360001 end=15744000

     But we only care about the filename and the size
     For single-part yEnc we need to get size from the first line, for
     multi-part we need to substract end-begin from second line
    */

    // Get number of lines
    num_lines = PyList_Size(Py_input_list);

    // Get first chunk
    cur_char = PyBytes_AsString(PyList_GetItem(Py_input_list, 0));

    // Start of header (which doesn't have to be part of first chunk)
    start_loc = find_text_in_pylist(Py_input_list, "=ybegin", &cur_char, &list_index);
    if(!start_loc)
        return 0;

    // First we find the size (for single-part files)
    start_loc = find_text_in_pylist(Py_input_list, "size=", &cur_char, &list_index);
    if(start_loc) {
        // Move over a bit
        part_size = (int)extract_int_from_pylist(Py_input_list, &list_index, &start_loc, &cur_char);
    }

    // Find name
    start_loc = find_text_in_pylist(Py_input_list, "name=", &cur_char, &list_index);
    if(start_loc) {
        extract_filename_from_pylist(Py_input_list, &list_index, &start_loc, &cur_char, filename_out);
    } else {
        // Don't go on without a name
        return 0;
    }

    // Is there a multi-part indicator?
    start_loc = find_text_in_pylist(Py_input_list, "=ypart", &cur_char, &list_index);
    if(start_loc) {
        // Reset size, so we for sure don't use the previously found "size=" value
        part_size = 0;

        // Find part-begin
        start_loc = find_text_in_pylist(Py_input_list, "begin=", &cur_char, &list_index);
        if(start_loc) {
            // Get begin
            part_begin = extract_int_from_pylist(Py_input_list, &list_index, &start_loc, &cur_char);

            // Find part-end
            start_loc = find_text_in_pylist(Py_input_list, "end=", &cur_char, &list_index);
            if(start_loc) {
                // Move over a bit
                part_size = (int)(extract_int_from_pylist(Py_input_list, &list_index, &start_loc, &cur_char) - part_begin + 1);
            }
        }

        // We want to make sure it's a valid value
        if(part_size <= 0  || part_size > num_bytes_reserved) {
            // Set safe value
            part_size = (int)(num_bytes_reserved*0.75);
        }

        // Skip over everything untill end of line, where the content starts
        for( ; *cur_char != LF && *cur_char != CR && *cur_char != ZERO; cur_char++);
    }

    /*
        Looking for the end, format:
        =yend size=384000 part=41 pcrc32=084e170f
    */
    // first, join chunks to form a flat buffer
    char tail_buffer[MAX_TAIL_BYTES];
    int tail_buffer_space = MAX_TAIL_BYTES;
    int end_line = (int)num_lines - 1;
    Py_ssize_t end_line_len;
    const char* tail_buffer_pos = NULL;
    for(; end_line >= list_index; end_line--) {
        char* str;
        Py_ssize_t _len;
        PyBytes_AsStringAndSize(PyList_GetItem(Py_input_list, end_line), &str, &_len);
        int len = (int)_len;
        size_t strOffset = 0;
        if(len >= tail_buffer_space) {
            strOffset = len - tail_buffer_space;
            memcpy(tail_buffer, str + strOffset, tail_buffer_space);
            tail_buffer_space = 0;
        } else {
            tail_buffer_space -= len;
            memcpy(tail_buffer + tail_buffer_space, str, len);
        }
        // TODO: if looking to optimize, can restrict the search range
        // TODO: finding the first instance isn't ideal - it should search in reverse
        // TODO: should we attempt to handle "=\r\n=yend" sequences? Although invalid for an encoder, a decoder still needs to unescape the first \r, which means it wouldn't be a valid end sequence
        tail_buffer_pos = my_memstr(tail_buffer + tail_buffer_space, MAX_TAIL_BYTES - tail_buffer_space, "\r\n=yend ", 0);
        if(tail_buffer_pos) { // end point found
            if(tail_buffer_space) {
                // this chunk was small
                end_line_len = tail_buffer_pos - tail_buffer - tail_buffer_space;
            } else {
                end_line_len = tail_buffer_pos - tail_buffer + strOffset;
            }
            break;
        }
        if(tail_buffer_space == 0)
            break;
    }
    if(!tail_buffer_pos) {
        // =yend not found - invalid article
        return 0;
    }
#if CRC_CHECK
    uint32_t crc = 0;
    uInt crc_yenc = 0;
    tail_buffer_pos += 7; // skip "\r\n=yend"
    int tail_buffer_len = tail_buffer + MAX_TAIL_BYTES - tail_buffer_pos;

    // Process CRC
    const char* crc_pos = my_memstr(tail_buffer_pos, tail_buffer_len, " pcrc32=", 1);
    if(crc_pos && (tail_buffer + MAX_TAIL_BYTES - crc_pos) >= 8) {
        char* end;
        crc_yenc = strtoul(crc_pos, &end, 16); // TODO: consider stricter parsing
    } else {
        // CRC32 not found - article is invalid
        return 0;
    }
#endif



    size_t input_offset = cur_char - PyBytes_AsString(PyList_GetItem(Py_input_list, list_index));;
    YencDecoderState state = YDEC_STATE_CRLF;

    // loop through chunks and decode
    while(list_index <= end_line) {
        char* str;
        Py_ssize_t len;
        PyBytes_AsStringAndSize(PyList_GetItem(Py_input_list, list_index), &str, &len);

        if(list_index == end_line) {
            len = end_line_len;
        }
        list_index++;
        if((size_t)len <= input_offset) continue;
        // send to decoder
        size_t output_len = do_decode(1, (unsigned char*)str + input_offset, (unsigned char*)output_buffer, len - input_offset, &state);
        decoded_bytes += output_len;
        input_offset = 0;
#if CRC_CHECK
        crc = do_crc32(output_buffer, output_len, crc);
#endif
        output_buffer += output_len;
    }

#if CRC_CHECK
    *crc_correct = (crc == crc_yenc);
#else
    // Do a simple check based on size, faster than CRC
    if(part_size != (int)decoded_bytes) {
        *crc_correct = 0;
    } else {
        *crc_correct = 1;
    }
#endif

    return decoded_bytes;
}


/*
    We need a special function to find the keywords
    because they can be split over multiple chunks.
*/
static char * find_text_in_pylist(PyObject *Py_input_list, const char *search_term, char **cur_char, int *cur_index) {
    // String holders
    char *next_string = NULL;
    char *start_loc = NULL;
    char *search_placeholder;
    // Size holders
    size_t cur_len;
    int start_index;
    int init_index = *cur_index;
    Py_ssize_t max_extra_lines = PyList_Size(Py_input_list) - 1;

    // First we try to do a fast location
    start_loc = strstr(*cur_char, search_term);

    // We didn't find it..
    if(!start_loc) {
        // We do maximum of 3 times extra lines, otherwise to slow
        max_extra_lines = (*cur_index+3 >= max_extra_lines) ?  max_extra_lines : *cur_index+3;

        // Start by adding the current string to the placeholder
        cur_len = strlen(*cur_char)+1;
        search_placeholder = (char *) calloc(cur_len, sizeof(char *));
        strcpy(search_placeholder, *cur_char);

        // Add the next item and try again
        while(!start_loc && *cur_index < max_extra_lines) {
            // Need to get the next one
            *cur_index = *cur_index+1;
            next_string = PyBytes_AsString(PyList_GetItem(Py_input_list, *cur_index));

            // Reserve the next bit
            cur_len = cur_len + strlen(next_string);
            search_placeholder = (char *) realloc(search_placeholder, cur_len);
            strcat(search_placeholder, next_string);

            // Try to find it again
            start_loc = strstr(search_placeholder, search_term);
        }

        /*
            Problem: If we return start_loc now, we will have a memory leak
            because search_placeholder is never free'd. So we need to get
            the correct location in the current string from the list.
        */
        if(start_loc) {
            // How much in the new string are we?
            start_index = (int)(start_loc - search_placeholder) - (int)(strlen(search_placeholder) - strlen(next_string));
            // Just make sure it's valid
            if(start_index < 0 || start_index > (int)strlen(next_string)) {
                start_loc = NULL;
            }
            // Point to the location in the item from the list
            start_loc = next_string + start_index;
        } else {
            // Decrease the index to where we begun
            *cur_index = init_index;
        }

        // Cleanup
        free(search_placeholder);
    }

    // Did we find it now?
    if(start_loc) {
        start_loc += strlen(search_term);
        *cur_char = start_loc;
    }

    // Found it directly
    return start_loc;
}


/*
    Integer values like "begin=1234" or "pcrc=ABCDE" can also
    be split over multiple lines. And thus we need to really
    check that we did not reach the end of a line every time.
*/
uLong extract_int_from_pylist(PyObject *Py_input_list, int *cur_index, char **start_loc, char **cur_char) {
    char *enc_loc;
    char *item_holder;
    char *combi_holder;
    uLong part_value = 0;
    Py_ssize_t max_lines = PyList_Size(Py_input_list);

    part_value = strtoll(*start_loc, &enc_loc, 0);

    // Did we reach the end of a line?
    if(*enc_loc == ZERO) {
        // Do we even have another item?
        if(*cur_index+1 >= max_lines) return part_value;

        // We need to fix things by combining the 2 lines
        combi_holder = (char *) calloc(strlen(*start_loc)+1, sizeof(char *));
        strcpy(combi_holder, *start_loc);
        *cur_index = *cur_index+1;
        item_holder = PyBytes_AsString(PyList_GetItem(Py_input_list, *cur_index));
        combi_holder = (char *) realloc(combi_holder, strlen(*start_loc)+strlen(item_holder)+1);
        strcat(combi_holder, item_holder);

        // Now we do it again
        part_value = strtol(combi_holder, &enc_loc, 0);

        // Free the space
        free(combi_holder);
        // Set the current position
        *cur_char = item_holder;
    } else {
        // Move pointer
        *cur_char = enc_loc;
    }

    return part_value;
}


/*
    Filename can also be split over multiple lines
    and thus needs saftey checks!
*/
int extract_filename_from_pylist(PyObject *Py_input_list, int *cur_index, char **start_loc, char **cur_char, char **filename_ptr) {
    // Temporary holders
    char *end_loc;
    Py_ssize_t max_lines = PyList_Size(Py_input_list);

    // Start at current setting
    end_loc = *start_loc;
    while(1) {
        // Did we reach end of the line but not newline?
        if(*(end_loc+1) == CR || *(end_loc+1) == LF || *(end_loc+1) == ZERO) {
            // Did we allocate yet?
            if(!*filename_ptr) {
                // Reserve space (plus current char and terminator)
                *filename_ptr = (char *)calloc(end_loc - *start_loc + 2, sizeof(char));
                // Allocation check
                if(!filename_ptr) return 0;
                // Copy the text, including the current char
                strncpy(*filename_ptr, *start_loc, end_loc - *start_loc + 1);
                // Add termininator
                (*filename_ptr)[strlen(*filename_ptr)] = ZERO;
                // Was this the end?
                if(*(end_loc+1) == CR || *(end_loc+1) == LF) {
                    // Move the pointer and return
                    *cur_char = end_loc+1;
                    return 1;
                } else {
                    // Do we even have another item?
                    if(*cur_index+1 >= max_lines) return 0;
                    // Need to get the next one
                    *cur_index = *cur_index+1;
                    *start_loc = end_loc = PyBytes_AsString(PyList_GetItem(Py_input_list, *cur_index));
                }
            } else {
                // Expand the result to hold this new bit (plus current char and terminator)
                *filename_ptr = (char *)realloc(*filename_ptr, strlen(*filename_ptr) + end_loc - *start_loc + 2);
                // Allocation check
                if(!filename_ptr) return 0;
                // Copy result at the end
                strncat(*filename_ptr, *start_loc, end_loc - *start_loc + 1);
                // Add termininator
                (*filename_ptr)[strlen(*filename_ptr)] = ZERO;
                // Move the pointer and return
                *cur_char = end_loc+1;
                return 1;
            }
        } else {
            // Move 1 char forward, not if we just fetched new chunk
            end_loc++;
        }
    }
}


PyObject* decode_usenet_chunks(PyObject* self, PyObject* args) {
    // The input/output PyObjects
    (void)self;
    PyObject *Py_input_list;
    PyObject *Py_output_buffer;
    PyObject *Py_output_filename;
    PyObject *retval = NULL;

    // CRC
    Bool crc_correct = 0;

    // Buffers
    char *filename_out = NULL;
    size_t output_len = 0;
    int num_bytes_reserved;
    int lp_max;
    int lp;

    // Parse input
    if (!PyArg_ParseTuple(args, "O:decode_usenet_chunks", &Py_input_list)) {
        return NULL;
    }

    // Verify it's a list
    if(!PyList_Check(Py_input_list)) {
        PyErr_SetString(PyExc_TypeError, "Expected list");
        return NULL;
    }

    // yEnc data can never be larger than the source data, so use that as a start
    num_bytes_reserved = 0;
    lp_max = (int)PyList_Size(Py_input_list);
    for(lp = 0; lp < lp_max; lp++) {
        num_bytes_reserved += (int)PyBytes_Size(PyList_GetItem(Py_input_list, lp));
    }

    // Create empty bytes object for direct access to char-pointer
    // Only on Windows this is faster
    Py_output_buffer = PyBytes_FromStringAndSize(NULL, num_bytes_reserved);
    if(Py_output_buffer == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    PyBytesObject *sv = (PyBytesObject *)Py_output_buffer;

    // Lift the GIL
    Py_BEGIN_ALLOW_THREADS;

    // Calculate
    output_len = decode_buffer_usenet(Py_input_list, sv->ob_sval, num_bytes_reserved, &filename_out, &crc_correct);

    // Aaah there you are again GIL..
    Py_END_ALLOW_THREADS;

    // Catch if there's nothing
    if(!output_len || !filename_out) {
        PyErr_SetString(PyExc_ValueError, "Could not get filename");
        // Safety free's
        if(filename_out) free(filename_out);
        if(Py_output_buffer) Py_XDECREF(Py_output_buffer);
        return NULL;
    }

    // Use special Python function to go from Latin-1 to Unicode
    Py_output_filename = PyUnicode_DecodeLatin1((char *)filename_out, strlen((char *)filename_out), NULL);

    // Resize data to actual value
    // We use this instead of "_PyBytes_Resize", as it seems to cause a drop in performance
    Py_SIZE(sv) = output_len;
    sv->ob_sval[output_len] = '\0';
    sv->ob_shash = -1;

    // Build output
    retval = Py_BuildValue("(S,S,O)", Py_output_buffer, Py_output_filename, crc_correct ? Py_True: Py_False);

    // Make sure we free all the buffers
    Py_XDECREF(Py_output_buffer);
    Py_XDECREF(Py_output_filename);
    free(filename_out);
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

PyObject* encode(PyObject* self, PyObject* args)
{
    (void)self;
	PyObject *Py_input_string;
	PyObject *Py_output_string;
	PyObject *retval = NULL;

	char *input_buffer = NULL;
	char *output_buffer = NULL;
	size_t input_len = 0;
	size_t output_len = 0;
    uint32_t crc;

	// Parse input
    if (!PyArg_ParseTuple(args, "O:encode", &Py_input_string)) {
        return NULL;
    }

    // Initialze buffers and CRC's
	input_len = PyBytes_Size(Py_input_string);
	input_buffer = (char *)PyBytes_AsString(Py_input_string);
	output_buffer = (char *)malloc(YENC_MAX_SIZE(input_len, LINESIZE));
	if(!output_buffer)
		return PyErr_NoMemory();

	// Free GIL, in case it helps
    Py_BEGIN_ALLOW_THREADS;

    // Encode result
    int column = 0;
	output_len = do_encode(LINESIZE, &column, (unsigned char*)input_buffer, (unsigned char*)output_buffer, input_len, 1);
    crc = do_crc32(input_buffer, input_len, 0);

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
