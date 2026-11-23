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

#ifndef SABCTOOLS_UNLOCKED_SSL_H
#define SABCTOOLS_UNLOCKED_SSL_H

#define SABCTOOLS_PY_HEX(major, minor) (((major) << 24) | ((minor) << 16))

/* Must precede Python.h, in every header that includes it */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

/* OpenSSL link */
#if defined(_WIN32) || defined(__CYGWIN__)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <Windows.h>
# include <winsock2.h>
#else
# include <dlfcn.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Have to manually define this OpenSSL constant and hope it never changes */
# define SSL_RECEIVED_SHUTDOWN 2
# define SSL_ERROR_WANT_READ 2
# define SSL_ERROR_WANT_WRITE 3
# define SSL_ERROR_ZERO_RETURN 6

bool socket_init();
void openssl_init();
extern bool openssl_linked;
int require_nonblocking(PyObject *sock);
bool is_socket(PyObject *obj);
Py_ssize_t socket_read(PyObject *sock, char *buf, Py_ssize_t len);
Py_ssize_t unlocked_ssl_read(PyObject *ssl_socket, char *buf, Py_ssize_t len, int *pending);
bool is_unlocked_ssl_socket(PyObject *obj);
PyObject *unlocked_ssl_recv_into(PyObject *, PyObject*);

#ifdef __cplusplus
}
#endif
#endif