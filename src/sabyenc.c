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

#include "sabyenc.h"

/* Typedefs */
typedef struct {
    uInt crc;
    uLong bytes;
} Crc32;

/* Declarations */
static uInt crc_tab[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};
static char* argnames[] = {"infile", "outfile", "bytez", NULL};

/* Function declarations */
static void crc_init(Crc32 *, uInt);
static void crc_update(Crc32 *, uInt);
void initsabyenc(void);
static int decode_buffer_usenet(PyObject *, Byte *, int, Byte **, Crc32 *, uInt *,  Bool *);
static char * find_text_in_pylist(PyObject *, char *, char **, int *);

/* Python API requirements */
static char decode_usenet_chunks_doc[] = "decode_usenet_chunks(string)";

static PyMethodDef funcs[] = {
        {"decode_usenet_chunks", (PyCFunction) decode_usenet_chunks, METH_KEYWORDS | METH_VARARGS, decode_usenet_chunks_doc},
        {NULL, NULL, 0, NULL}
};

/* Function definitions */
static void crc_init(Crc32 *crc, uInt value) {
    crc->crc = value;
    crc->bytes = 0UL;
}

static void crc_update(Crc32 *crc, uInt c) {
    crc->crc=crc_tab[(crc->crc^c)&0xff]^((crc->crc>>8)&0xffffff);
    crc->bytes++;
}

static int decode_buffer_usenet(PyObject *Py_input_list, Byte *output_buffer, int num_bytes_reserved,
                                Byte **filename_out,  Crc32 *crc, uInt *crc_yenc, Bool *crc_correct) {
    // For the list
    int num_lines;
    int list_index = 0;

    // Search variables
    char *cur_char; // Pointer to search result
    char *start_loc; // Pointer to current char
    char *end_loc;
    char *crc_holder;

    // Other vars
    Byte byte;
    uInt part_begin = 0;
    uInt part_size = 0;
    int decoded_bytes = 0;
    int safe_nr_bytes = 0;
    Bool escape_char = 0;
    int double_point_escape = 0;

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
    cur_char = PyString_AsString(PyList_GetItem(Py_input_list, 0));

    // Start of header (which doesn't have to be part of first chunk)
    start_loc = find_text_in_pylist(Py_input_list, "=ybegin", &cur_char, &list_index);

    if(start_loc) {
        // First we find the size (for single-part files)
        start_loc = find_text_in_pylist(Py_input_list, "size=", &cur_char, &list_index);
        if(start_loc) {
            // Move over a bit
            part_size = strtol(cur_char, NULL, 0);
        }

        // Find name
        start_loc = find_text_in_pylist(Py_input_list, "name=", &cur_char, &list_index);
        if(start_loc) {
            // Skip over everything untill end of line
            for(end_loc = start_loc; *end_loc != CR && *end_loc != LF && *end_loc != '\0'; end_loc++);

            // Now copy this part to the output
            *filename_out = (Byte *)calloc(end_loc - start_loc + 1, sizeof(Byte));

            // Saftey check, required in case the allocation fails
            if(!*filename_out) {
                return 0;
            }

            // Copy the text and add terminator
            strncpy(*filename_out, start_loc, end_loc - start_loc);
            (*filename_out)[strlen(*filename_out)] = '\0';

            // Move pointer
            cur_char = end_loc;
        } else {
            // Don't go on without a name
            return 0;
        }

        // Is there a multi-part indicator?
        start_loc = find_text_in_pylist(Py_input_list, "=ypart", &cur_char, &list_index);
        if(start_loc) {
            // Find part-begin
            start_loc = find_text_in_pylist(Py_input_list, "begin=", &cur_char, &list_index);
            if(start_loc) {
                part_begin = strtol(cur_char, NULL, 0);
            }

            // Find part-begin
            start_loc = find_text_in_pylist(Py_input_list, "end=", &cur_char, &list_index);
            if(start_loc) {
                part_size = strtol(cur_char, NULL, 0) - part_begin + 1;
            }

            // Skip over everything untill end of line
            for(end_loc = start_loc; *end_loc != LF && *end_loc != CR && *end_loc != '\0'; end_loc++);
            // Move pointer
            cur_char = end_loc;
        }

        // How many bytes can be checked safely?
        safe_nr_bytes = part_size ? part_size - 200 : 0;

        /*
            During the loop we need to take care of special cases.
            The escape "=" and whatever it escapes might be on the
            next Python-list-item. Also the sequence "\n.." should
            onlyconvert one dot, but this sequence might also be
            split across list items.
        */
        while(1) {
            // Get current char and increment pointer
            cur_char++;

            // End of the line of list-item
            if(*cur_char == '\0') {
                // Are we outside the list?
                list_index++;
                if(list_index == num_lines) {
                    break;
                }

                // Get reference to the new line
                cur_char = PyString_AsString(PyList_GetItem(Py_input_list, list_index));
            }

            // Special charaters
            if(escape_char) {
                byte = (*cur_char - 106);
                escape_char = 0;
                double_point_escape = 0;
            } else if(*cur_char == ESC) {
                // strncmp is expensive, only perform near the end
                if(decoded_bytes > safe_nr_bytes) {
                    // Looking for the end, format:
                    // =yend size=384000 part=41 pcrc32=084e170f
                    if (!strncmp(cur_char, "=y", 2)) {
                        // Find CRC
                        start_loc = find_text_in_pylist(Py_input_list, "crc32=", &cur_char, &list_index);

                        /*
                            Especially with the CRC, the code is
                            often split between the 2 last chunks..
                        */
                        if(list_index+1 < num_lines) {
                            // Let's add the extra line to be sure
                            crc_holder = (Byte *) calloc(strlen(cur_char)+1, sizeof(Byte *));
                            strcpy(crc_holder, cur_char);
                            cur_char = PyString_AsString(PyList_GetItem(Py_input_list, list_index+1));
                            crc_holder = (Byte *) realloc(crc_holder, strlen(cur_char)+strlen(crc_holder)+1);
                            strcat(crc_holder, cur_char);
                            cur_char = crc_holder;
                        }

                        // Process CRC
                        if(start_loc) {
                            *crc_yenc = strtoul(cur_char, NULL, 16);

                            // Change format to CRC-style (don't ask me why..)
                            *crc_yenc = -1*(*crc_yenc)-1;

                            // Check if CRC is correct
                            if(crc->crc == *crc_yenc) {
                                *crc_correct = 1;
                            }
                        }

                        // Cleanup
                        if(list_index+1 < num_lines) {
                            // This also cleans linked cur_char
                            free(crc_holder);
                        }

                        break;
                    }
                }

                // Becaus the escape might be at the end of the chunk
                // we need to do it in the next loop
                escape_char = 1;
                continue;
            } else if(*cur_char == CR) {
                continue;
            } else if(*cur_char == LF) {
                double_point_escape = 1;
                continue;
            } else if(double_point_escape == 2 && *cur_char == DOT) {
                // We found "\n.."! Ignore that second dot.
                double_point_escape = 0;
                continue;
            } else if(*cur_char == DOT) {
                // Special case for "\n.." that can be split between list items
                if(double_point_escape == 1) {
                    double_point_escape = 2;
                }
                // We do include this dot
                byte = (*cur_char - 42);
            } else {
                byte = (*cur_char - 42);
                // Reset exception
                double_point_escape = 0;
            }

            // Place the byte and go to the next
            output_buffer[decoded_bytes] = byte;
            decoded_bytes++;
            crc_update(crc, byte);

            // Saftey check
            if(decoded_bytes > num_bytes_reserved - 1) {
                break;
            }
        }
    }
    return decoded_bytes;
}


static char * find_text_in_pylist(PyObject *Py_input_list, char *search_term, char **cur_char, int *cur_index) {
    // Temp variables
    char *next_string;
    char *start_loc = NULL;
    char *search_placeholder;
    int cur_len;
    int start_index;
    int num_lines = PyList_Size(Py_input_list);

    // First we try to do a fast location
    start_loc = strstr(*cur_char, search_term);

    // We didn't find it..
    if(!start_loc) {
        // We do maximum of 5 times extra lines, otherwise to slow
        num_lines = (*cur_index+5 > num_lines-1) ?  num_lines-1 : *cur_index+5;

        // Start by adding the current string to the placeholder
        cur_len = strlen(*cur_char)+1;
        search_placeholder = (Byte *) calloc(cur_len, sizeof(Byte *));
        strcpy(search_placeholder, *cur_char);

        // Add the next item and try again
        while(!start_loc && *cur_index < num_lines) {
            // Need to get the next one
            *cur_index = *cur_index+1;
            next_string = PyString_AsString(PyList_GetItem(Py_input_list, *cur_index));

            // Reserve the next bit
            cur_len = cur_len + strlen(next_string);
            search_placeholder = (Byte *) realloc(search_placeholder, cur_len);
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
            start_index = (start_loc - search_placeholder) - (strlen(search_placeholder) - strlen(next_string));
            // Just make sure it's valid
            if(start_index < 0 || start_index > (int)strlen(next_string)) {
                start_loc = NULL;
            }
            // Point to the location in the item from the list
            start_loc = next_string + start_index;
        } else {
            // Decrease the index again just to be sure
            *cur_index -= num_lines;
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


PyObject* decode_usenet_chunks(PyObject* self, PyObject* args, PyObject* kwds) {
    // The input/output PyObjects
    PyObject *Py_input_list;
    PyObject *Py_num_bytes;
    PyObject *Py_output_buffer;
    PyObject *Py_output_filename;
    PyObject *retval = NULL;

    // CRC
    Crc32 crc;
    uInt crc_yenc;
    Bool crc_correct = 0;
    uInt crc_value = 0xffffffffll;

    // Buffers
    Byte *output_buffer = NULL;
    Byte *filename_out = NULL;
    uInt output_len = 0;
    int num_bytes_reserved;

    if(!PyArg_UnpackTuple(args, "decode_usenet_chunks", 2, 2, &Py_input_list, &Py_num_bytes))
        return NULL;

    // Did we get anything?
    if(!PyList_Size(Py_input_list)) {
        PyErr_SetString(PyExc_ValueError, "No valid list recieved");
        goto out;
    }

    // We reserve 10% extra, just to be sure
    num_bytes_reserved = (int)(PyInt_AsLong(Py_num_bytes) * 1.10);
    output_buffer = (Byte *)malloc(num_bytes_reserved);

    if(!output_buffer) {
        retval = PyErr_NoMemory();
        goto out;
    }

    // Byeeeeeeee GIL!
    Py_BEGIN_ALLOW_THREADS;

    // Initial CRC
    crc_init(&crc, crc_value);

    // Calculate
    output_len = decode_buffer_usenet(Py_input_list, output_buffer, num_bytes_reserved, &filename_out, &crc, &crc_yenc, &crc_correct);

    // Aaah there you are again GIL..
    Py_END_ALLOW_THREADS;

    // Catch if there's nothing
    if(!output_len || !filename_out) {
        PyErr_SetString(PyExc_ValueError, "Could not get filename");
        retval = (PyObject *) NULL;
        goto out;
    }

    // Prepare output
    Py_output_buffer = PyString_FromStringAndSize((char *)output_buffer, output_len);

    // Use special Python function to go from Latin-1 to Unicode
    Py_output_filename = PyUnicode_DecodeLatin1((char *)filename_out, strlen((char *)filename_out), NULL);

    // Build output
    retval = Py_BuildValue("(S,S,L,L,O)", Py_output_buffer, Py_output_filename, (long long)crc.crc, (long long)crc_yenc, crc_correct ? Py_True: Py_False);

out:
    // Make sure we free all the buffers!
    Py_XDECREF(Py_output_buffer);
    Py_XDECREF(Py_output_filename);
    free(output_buffer);
    free(filename_out);
    return retval;
}


void initsabyenc(void) {
    // Add module
    PyObject *module;
    module = Py_InitModule3("sabyenc", funcs, "Raw yenc operations");

    // Add version
    PyModule_AddStringConstant(module, "__version__", SABYENC_VERSION);
}

