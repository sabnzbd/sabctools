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
#define SABYENC_VERSION "5.4.4"

/* Do we CRC check? */
#define SABYENC_CRC_CHECK   1

/* Constants */
#define SABYENC_LINESIZE    128
#define SABYENC_ZERO        0x00
#define SABYENC_CR          0x0d
#define SABYENC_LF          0x0a

/* The =yend line shouldn't exceed 1KB */
#define MAX_TAIL_BYTES 1024

/* Customized types */
typedef unsigned long long uLong;
typedef unsigned int uInt;
typedef int Bool;

/* Windows doesn't have strtoll */
#if defined(_MSC_VER)
#define strtoll _strtoi64
#endif

/* Limited sub-struct of Python struct */
typedef struct {
	PyObject_HEAD
	PyObject *Socket; /* We do not use it, but needs to match Python */
	void *ssl;
} PySSLSocket;

/* OpenSSL link */
#if defined(_WIN32) || defined(__CYGWIN__)
# define SABYENC_DLL_CALL __stdcall
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#else
# define SABYENC_DLL_CALL
# include <dlfcn.h>
#endif
int (SABYENC_DLL_CALL *SSL_read_ex)(void*, void*, size_t, size_t*) = NULL;
int (SABYENC_DLL_CALL *SSL_get_error)(void*, int) = NULL;

/* Have to manually define this OpenSSL constant and hope it never changes */
# define SSL_ERROR_WANT_READ 2

/* Functions */
PyObject* decode_usenet_chunks(PyObject *, PyObject*);
PyObject* encode(PyObject *, PyObject*);
PyObject* unlocked_ssl_recv(PyObject *, PyObject*);
PyMODINIT_FUNC PyInit_sabyenc3(void);


