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

#include <Python.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

/* OpenSSL link */
#if defined(_WIN32) || defined(__CYGWIN__)
# define WIN32_LEAN_AND_MEAN
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

void openssl_init();
bool openssl_linked();
PyObject *unlocked_ssl_recv_into(PyObject *, PyObject*);

#ifdef __cplusplus
}
#endif
#endif