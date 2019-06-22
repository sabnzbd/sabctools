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

#include <Python.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

/* Version information */
#define SABYENC_VERSION "3.3.5"

/* Do we CRC check? */
#define CRC_CHECK   0

/* Byte-check limit */
#define END_CHECK_BYTES 50

/* Constants */
#define LINESIZE    128
#define ZERO        0x00
#define CR          0x0d
#define LF          0x0a
#define ESC         0x3d
#define TAB         0x09
#define SPACE       0x20
#define DOT         0x2e

/* 10MB should be enough for any article,
   as 1MB is the common maximum */
#define MAX_RESERVED_BYTES     10111000

/* Customized types */
typedef unsigned long long uLong;
typedef unsigned int uInt;
typedef int Bool;

/* Windows doesn't have strtoll */
#if defined(_MSC_VER)
#define strtoll _strtoi64
#endif

/* Functions */
PyObject* decode_usenet_chunks(PyObject* ,PyObject* , PyObject*);
void initsabyenc(void);
