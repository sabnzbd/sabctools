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
static int encode_buffer(Byte *, Byte *, uInt, Crc32 *, uInt *);
static int decode_buffer(Byte *, Byte *, uInt, Crc32 *, Bool *);
static int decode_buffer_usenet(Byte *, Byte *, Byte **, Crc32 *, uInt *,  Bool *);

/* Python API requirements */
static char encode_string_doc[] = "encode_string(string, crc32, column)";
static char decode_string_doc[] = "decode_string(string, crc32, escape)";
static char decode_string_usenet_doc[] = "decode_string_usenet(string)";

static PyMethodDef funcs[] = {
        {"encode_string", (PyCFunction) encode_string, METH_KEYWORDS | METH_VARARGS, encode_string_doc},
        {"decode_string", (PyCFunction) decode_string, METH_KEYWORDS | METH_VARARGS, decode_string_doc},
        {"decode_string_usenet", (PyCFunction) decode_string_usenet, METH_KEYWORDS | METH_VARARGS, decode_string_usenet_doc},
        {NULL, NULL, 0, NULL}
};

/* Function definitions */
static void crc_init(Crc32 *crc, uInt value)
{
	crc->crc = value;
	crc->bytes = 0UL;
}

static void crc_update(Crc32 *crc, uInt c)
{
	crc->crc=crc_tab[(crc->crc^c)&0xff]^((crc->crc>>8)&0xffffff);
	crc->bytes++;
}


static int encode_buffer(
		Byte *input_buffer, 
		Byte *output_buffer, 
		uInt bytes, 
		Crc32 *crc, 
		uInt *col
		)
{
	uInt in_ind;
	uInt out_ind;
	Byte byte;
		
	out_ind = 0;
	for(in_ind=0; in_ind < bytes; in_ind++) {
		byte = (Byte)(input_buffer[in_ind] + 42);
		crc_update(crc, input_buffer[in_ind]);
		switch(byte){
			case ZERO:
			case LF:
			case CR:
			case ESC:
				goto escape_string;
			case TAB:
			case SPACE:
				if(*col == 0 || *col == LINESIZE-1) {
					goto escape_string;
				}
                        case DOT:
                                if(*col == 0) {
                                        goto escape_string;
                                }
			default:
				goto plain_string;
		}
		escape_string:
		byte = (Byte)(byte + 64);
		output_buffer[out_ind++] = ESC;
		(*col)++;
		plain_string:
		output_buffer[out_ind++] = byte;
		(*col)++;
		if(*col >= LINESIZE) {
			output_buffer[out_ind++] = CR;
			output_buffer[out_ind++] = LF;
			*col = 0;
		}
	}
	return out_ind;
}


static int decode_buffer(
		Byte *input_buffer, 
		Byte *output_buffer, 
		uInt bytes, 
		Crc32 *crc, 
		Bool *escape
		)
{
	uInt read_ind;
	uInt decoded_bytes;
	Byte byte;
	
	decoded_bytes = 0;
	for(read_ind = 0; read_ind < bytes; read_ind++) {
		byte = input_buffer[read_ind];

		if(*escape) {
			byte = (Byte)(byte - 106);
			*escape = 0;
		} else if(byte == ESC) {
			*escape = 1;
			continue;
		} else if(byte == LF || byte == CR) {
			continue;
		} else {
			byte = (Byte)(byte - 42);
		}
		output_buffer[decoded_bytes] = byte;
		decoded_bytes++;
		crc_update(crc, byte);
	}
	return decoded_bytes;
}


static int decode_buffer_usenet(Byte *input_buffer, Byte *output_buffer, Byte **filename_out, 
	                            Crc32 *crc, uInt *crc_yenc, Bool *crc_correct)
{
	// Search variables
    char *cur_char; // Pointer to search result
    char *start_loc; // Pointer to current char
    char *end_loc;
    
    // Other vars
    Byte byte;
    uInt part_begin = 0;
    uInt part_size = 0;
    uInt decoded_bytes = 0;
    uInt safe_nr_bytes = 0;
    
    /*
     ANALYZE HEADER
     Always in the same format, e.g.:
     
     =ybegin part=41 line=128 size=49152000 name=90E2Sdvsmds0801dvsmds90E.part06.rar
	 =ypart begin=15360001 end=15744000
	 
	 But we only care about the filename and the size
	*/

    // Start of header
    start_loc = strstr(input_buffer, "=ybegin");

    if(start_loc) {
    	// Move forward to start of header
    	cur_char = start_loc+8;

		// Find name
    	start_loc = strstr(cur_char, "name=");
    	if(start_loc) {
    		start_loc += 5;
    		// Skip over everything untill end of line
    		for(end_loc = start_loc; *end_loc != CR && *end_loc != LF && *end_loc != '\0'; end_loc++);

    		// Now copy this part to the output
    		*filename_out = (Byte *)calloc(end_loc - start_loc + 1, sizeof(Byte));
            strncpy(*filename_out, start_loc, end_loc - start_loc);
            (*filename_out)[strlen(*filename_out)] = '\0';
            
            // Move pointer
            cur_char = end_loc;
    	}

    	// Is there a part-indicator?
    	start_loc = strstr(cur_char, "=ypart");
    	if(start_loc) {
    		// Mover over a bit
    		cur_char = start_loc+6;
    		
    		// Find part-begin
	    	start_loc = strstr(cur_char, "begin=");
	    	if(start_loc) {
	    		cur_char = start_loc+6;
	    		part_begin = strtoul(cur_char, NULL, 0);
	    	}

	    	// Find part-begin
	    	start_loc = strstr(cur_char, "end=");
	    	if(start_loc) {
	    		cur_char = start_loc+4;
	    		part_size = strtoul(cur_char, NULL, 0) - part_begin;
	    	}
	    	
    		// Skip over everything untill end of line
    		for(end_loc = start_loc; *end_loc != '\n' && *end_loc != '\r' && *end_loc != '\0'; end_loc++);
    		// Move pointer
            cur_char = end_loc;
    	}

    	// How many bytes can be checked safely?
    	safe_nr_bytes = part_size ? part_size - 200 : 0;

    	// Let's loop over all the data
    	while(1) {
    		// Saftey net
    		if(*cur_char == '\0') {
    			break;
    		}

    		// Get current char and increment pointer
    		cur_char++;

    		// Special charaters
    		if(*cur_char == ESC) {
    			// strncmp is expensive, only perform near the end
    			if(decoded_bytes > safe_nr_bytes) {
    				// Looking for the end, format:
    				// =yend size=384000 part=41 pcrc32=084e170f
	    			if (!strncmp(cur_char, "=yend ", 6)) {
	    				// Find part-begin
				    	start_loc = strstr(cur_char, "crc32=");
				    	if(start_loc) {
				    		cur_char = start_loc+6;
				    		*crc_yenc = strtoul(cur_char, NULL, 16);

				    		// Change format to CRC-style (don't ask me why..)
				    		*crc_yenc = -1*(*crc_yenc)-1;

				    		// Check if CRC is correct
				    		if(crc->crc == *crc_yenc) {
				    			*crc_correct = 1;
				    		}
				    	}
		    			break;
		    		}
		    	}

	    		// Escape character
	    		cur_char++;
				byte = (Byte)(*cur_char - 106);
    		} else if(*cur_char == LF || *cur_char == CR) {
				continue;
			} else if(*cur_char == DOT && *(cur_char-1) == DOT && *(cur_char-2) == LF) {
				continue;
			} else {
				byte = (Byte)(*cur_char - 42);
			}
			output_buffer[decoded_bytes] = byte;
			decoded_bytes++;

			crc_update(crc, byte);
    	}
    }
	return decoded_bytes;
}

PyObject* encode_string(
		PyObject* self, 
		PyObject* args, 
		PyObject* kwds
		)
{
	PyObject *Py_input_string;
	PyObject *Py_output_string;
	PyObject *retval = NULL;
	
	Byte *input_buffer = NULL;
	Byte *output_buffer = NULL;
	long long crc_value = 0xffffffffll;
	uInt input_len = 0;
	uInt output_len = 0;
	uInt col = 0;
	Crc32 crc;
	
	static char *kwlist[] = { "string", "crc32", "column", NULL };
	if(!PyArg_ParseTupleAndKeywords(args, 
				kwds,
				"O!|Li", 
				kwlist,
				&PyString_Type,
				&Py_input_string, 
				&crc_value,
				&col
				)) 
		return NULL;

	crc_init(&crc, (uInt)crc_value);
	input_len = PyString_Size(Py_input_string);
	input_buffer = (Byte *) PyString_AsString(Py_input_string);
	output_buffer = (Byte *) malloc((2 * input_len / LINESIZE + 1) * (LINESIZE + 2));
	if(!output_buffer)
		return PyErr_NoMemory();
	output_len = encode_buffer(input_buffer, output_buffer, input_len, &crc, &col);
	Py_output_string = PyString_FromStringAndSize((char *)output_buffer, output_len);
	if(!Py_output_string)
		goto out;

	retval = Py_BuildValue("(S,L,i)", Py_output_string, (long long)crc.crc, col);
	Py_DECREF(Py_output_string);
	
out:
	free(output_buffer);
	return retval;
}


PyObject* decode_string(
		PyObject* self, 
		PyObject* args, 
		PyObject* kwds
		)
{
	PyObject *Py_input_string;
	PyObject *Py_output_string;
	PyObject *retval = NULL;
	
	Byte *input_buffer = NULL;
	Byte *output_buffer = NULL;
	long long crc_value = 0xffffffffll;
	uInt input_len = 0;
	uInt output_len = 0;
	int escape = 0;
	Crc32 crc;
	
	static char *kwlist[] = { "string", "crc32", "escape", NULL };
	if(!PyArg_ParseTupleAndKeywords(args, 
				kwds,
				"O!|Li", 
				kwlist,
				&PyString_Type,
				&Py_input_string, 
				&crc_value,
				&escape
				)) 
		return NULL;
	crc_init(&crc, (uInt)crc_value);
	input_len = PyString_Size(Py_input_string);
	input_buffer = (Byte *)PyString_AsString(Py_input_string);
	output_buffer = (Byte *)malloc( input_len );
	if(!output_buffer)
		return PyErr_NoMemory();
	output_len = decode_buffer(input_buffer, output_buffer, input_len, &crc, &escape);
	Py_output_string = PyString_FromStringAndSize((char *)output_buffer, output_len);
	if(!Py_output_string)
		goto out;

	retval = Py_BuildValue("(S,L,i)", Py_output_string, (long long)crc.crc, escape);
	Py_DECREF(Py_output_string);
	
out:
	free(output_buffer);
	return retval;
}

PyObject* decode_string_usenet(PyObject* self, PyObject* args, PyObject* kwds)
{
	// The output PyObjects
	PyObject *Py_input_string;
	PyObject *Py_output_buffer;
	PyObject *Py_output_filename;
	PyObject *retval = NULL;
	
	Byte *input_buffer = NULL;
	Byte *output_buffer = NULL;
	Crc32 crc;
	uInt crc_yenc;
	Bool crc_correct = 0;
	Byte *filename_out = NULL;
	long long crc_value = 0xffffffffll;
	uInt input_len = 0;
	uInt output_len = 0;

	
	static char *kwlist[] = { "string" };
	if(!PyArg_ParseTupleAndKeywords(args, kwds, "O!|Li", kwlist, &PyString_Type, &Py_input_string)) 
		return NULL;
	
	crc_init(&crc, (uInt)crc_value);
	input_len = PyString_Size(Py_input_string);
	input_buffer = (Byte *)PyString_AsString(Py_input_string);
	output_buffer = (Byte *)malloc(input_len);
	
	if(!output_buffer)
		return PyErr_NoMemory();
	
	// Calculate
	output_len = decode_buffer_usenet(input_buffer, output_buffer, &filename_out, &crc, &crc_yenc, &crc_correct);
	
	// Prepare output
	Py_output_buffer = PyString_FromStringAndSize((char *)output_buffer, output_len);

	// Catch if there's nothing
	if(!Py_output_buffer || !filename_out) {
		goto out;
	}
	
	// Use special Python function to go from Latin-1 to Unicode
	Py_output_filename = PyUnicode_DecodeLatin1(filename_out, strlen(filename_out), NULL);

	// Build output
	retval = Py_BuildValue("(S,S,L,L,O)", Py_output_buffer, Py_output_filename, (long long)crc.crc, (long long)crc_yenc, crc_correct ? Py_True: Py_False);
	Py_DECREF(Py_output_buffer);
	Py_DECREF(Py_output_filename);
	
out:
	free(output_buffer);
	return retval;
}


void initsabyenc()
{
    PyObject *module;
    module = Py_InitModule3("sabyenc", funcs, "Raw yenc operations");
    PyModule_AddStringConstant(module, "__version__", SABYENC_VERSION);
}

