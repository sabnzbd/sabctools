 /*=============================================================================
 *
 * Copyright (C) 2003, 2011 Alessandro Duca <alessandro.duca@gmail.com>
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

#include <Python.h>
#include <stdio.h>
#include <fcntl.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* Constants			*/
#define	LINESIZE	128

/* BLOCK defines the size of the input buffer used while encoding data.
 */
#define BLOCK		65536

/* LONGBUFF: ((2 * BLOCK) / 128 + 2 ) * 128
 * In the worst case we will escape every byte thus doubling output's size,
 * since we write out lines of LINESIZE characters, we must add 2 more bytes for
 * CRLF sequence at the end of each line.
 * This is the maximum encoded size of BLOCK bytes.
 */
#define LONGBUFF	( 2 * BLOCK / LINESIZE + 1) * ( LINESIZE + 2 )

#define SMALLBUFF 	512

#define ZERO		0x00
#define CR		0x0d
#define	LF		0x0a
#define	ESC		0x3d
#define TAB		0x09
#define SPACE		0x20
#define DOT             0x2e

#define E_MODE		1
#define E_EOF 		2
#define E_IO		3

#define E_MODE_MSG	"Invalide mode for '*file' arguments"
#define E_IO_MSG	"I/O Error"

#define _DDEBUG_


/* Customized types		*/
typedef unsigned long uLong;
typedef unsigned int uInt;
typedef unsigned char Byte;
typedef int Bool;

/* Functions */
PyObject* encode_file(PyObject*, PyObject*, PyObject*);
PyObject* decode_file(PyObject*, PyObject*, PyObject*);
PyObject* encode_string(PyObject* ,PyObject* ,PyObject*);
PyObject* decode_string(PyObject* ,PyObject* , PyObject*);
void init_yenc(void);



