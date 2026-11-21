/*
 * This code was largely copied from cpython's ssl.c:
 * Copyright 2001-2022 Python Software Foundation
 * Licensed under the PSF LICENSE AGREEMENT FOR PYTHON 3.11.1,
 * see https://docs.python.org/3/license.html
 *
 * With modifications:
 * Copyright 2023 The SABnzbd-Team (sabnzbd.org)
 * Licensed under the GNU GPL version 2 or later.
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

#include "unlocked_ssl.h"

#include <errno.h>

static int (*SSL_read_ex)(void*, void*, size_t, size_t*) = NULL;
static int (*SSL_get_error)(void*, int) = NULL;
static int (*SSL_get_shutdown)(void*) = NULL;
static int (*SSL_pending)(const void*) = NULL;
static PyObject *SSLWantReadError = NULL;
static PyObject *SSLWantWriteError = NULL;
static PyTypeObject *SSLSocketType = NULL;

typedef struct {
    int ssl; /* last seen error from SSL */
    int c; /* last seen error from libc */
#ifdef MS_WINDOWS
    int ws; /* last seen error from winsock */
#endif
} _PySSLError;

typedef struct {
    PyObject_HEAD
    PyObject *Socket; /* weakref to socket on which we're layered */
    void *ssl;
    PyObject *ctx; /* weakref to SSL context */
    char shutdown_seen_zero;
    int socket_type;
    PyObject *owner; /* Python level "owner" passed to servername callback */
    PyObject *server_hostname;
#if PY_VERSION_HEX >= SABCTOOLS_PY_HEX(3, 15)
    int got_eof_error;
#else
    _PySSLError err; /* last seen error from various sources */
#endif
} PySSLSocket;

#ifndef INVALID_SOCKET /* MS defines this */
#define INVALID_SOCKET (-1)
#endif

#ifdef MS_WINDOWS
typedef SOCKET SOCKET_T;
#else
typedef int SOCKET_T;
#endif

typedef struct {
    PyObject_HEAD
    SOCKET_T sock_fd;           /* Socket file descriptor */
} PySocketSockObject;

static inline _PySSLError _PySSL_errno(int failed, void *ssl, int retcode)
{
    _PySSLError err = { 0 };
    if (failed) {
#ifdef MS_WINDOWS
        err.ws = WSAGetLastError();
#endif
        err.c = errno;
        err.ssl = SSL_get_error(ssl, retcode);
    }
    return err;
}

/*
 * A closed or vanished socket raises what socket.recv_into raises for one, an OSError
 * with EBADF, so a caller handles both read paths with the same except clause
 */
static void raise_socket_closed(const char *message)
{
    PyObject *args = Py_BuildValue("(is)", EBADF, message);
    if (args) {
        PyErr_SetObject(PyExc_OSError, args);
        Py_DECREF(args);
    }
}

static int get_socket(PySSLSocket *obj, PySocketSockObject **out_sock)
{
    if (!obj->Socket) {
        *out_sock = NULL;
        return 0;
    }

#if PY_VERSION_HEX >= SABCTOOLS_PY_HEX(3, 13)
    PySocketSockObject *sock;
    int res = PyWeakref_GetRef(obj->Socket, (PyObject **)&sock);
    if (res < 0) return -1;
    if (res == 0 || sock->sock_fd == INVALID_SOCKET) {
        Py_XDECREF(sock);
        raise_socket_closed("Underlying socket connection gone");
        return -1;
    }
    // sock is a now a NEW reference
    *out_sock = sock;
    return res;
#else
    PyObject *tmp = PyWeakref_GetObject(obj->Socket);
    if (!tmp || Py_IsNone(tmp)) {
        raise_socket_closed("Underlying socket connection gone");
        return -1;
    }

    PySocketSockObject *sock = (PySocketSockObject *)tmp;
    if (sock->sock_fd == INVALID_SOCKET) {
        raise_socket_closed("Underlying socket connection gone");
        return -1;
    }

    // Promote to an owned reference
    Py_INCREF(sock);

    *out_sock = sock;
    return 1;
#endif
}

/* Linking to OpenSSL function used by Python */
void openssl_init() {
    PyObject *ssl_module = NULL;
    PyObject *_ssl_module = NULL;
    PyObject *_ssl_module_path = NULL;
    #if defined(_WIN32) || defined(__CYGWIN__)
    HMODULE openssl_handle = NULL;
    #else
    void* openssl_handle = NULL;
    #endif

    ssl_module = PyImport_ImportModule("ssl");
    if(!ssl_module) goto cleanup;

    _ssl_module = PyImport_ImportModule("_ssl");
    if(!_ssl_module) goto cleanup;

    SSLSocketType = reinterpret_cast<PyTypeObject *>(PyObject_GetAttrString(ssl_module, "SSLSocket"));
    if(!SSLSocketType || !PyType_Check(SSLSocketType)) goto cleanup;

    SSLWantReadError = PyObject_GetAttrString(_ssl_module, "SSLWantReadError");
    if(!SSLWantReadError) goto cleanup;

    SSLWantWriteError = PyObject_GetAttrString(_ssl_module, "SSLWantWriteError");
    if(!SSLWantWriteError) goto cleanup;

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef _M_ARM64
    openssl_handle = GetModuleHandle(TEXT("libssl-3-arm64.dll"));
#else
    openssl_handle = GetModuleHandle(TEXT("libssl-3.dll"));
    if(!openssl_handle) openssl_handle = GetModuleHandle(TEXT("libssl-1_1.dll"));
#endif
    if(!openssl_handle) goto cleanup;

    SSL_read_ex = reinterpret_cast<decltype(SSL_read_ex)>(GetProcAddress(openssl_handle, "SSL_read_ex"));
    SSL_get_error = reinterpret_cast<decltype(SSL_get_error)>(GetProcAddress(openssl_handle, "SSL_get_error"));
    SSL_get_shutdown = reinterpret_cast<decltype(SSL_get_shutdown)>(GetProcAddress(openssl_handle, "SSL_get_shutdown"));
    SSL_pending = reinterpret_cast<decltype(SSL_pending)>(GetProcAddress(openssl_handle, "SSL_pending"));
#else
    // Find library at "import ssl; print(ssl._ssl.__file__)"

    if (PyObject_HasAttrString(_ssl_module, "__file__")) {
        _ssl_module_path = PyObject_GetAttrString(_ssl_module, "__file__");
        if(!_ssl_module_path) goto cleanup;
        const char *path = PyUnicode_AsUTF8(_ssl_module_path);
        if(!path) goto cleanup;
        openssl_handle = dlopen(path, RTLD_LAZY | RTLD_NOLOAD);
    }

    if(!openssl_handle) goto cleanup;

    *(void**)&SSL_read_ex = dlsym(openssl_handle, "SSL_read_ex");
    *(void**)&SSL_get_error = dlsym(openssl_handle, "SSL_get_error");
    *(void**)&SSL_get_shutdown = dlsym(openssl_handle, "SSL_get_shutdown");
    *(void**)&SSL_pending = dlsym(openssl_handle, "SSL_pending");
#endif

cleanup:
    Py_CLEAR(ssl_module);
    Py_CLEAR(_ssl_module);
    Py_CLEAR(_ssl_module_path);
    if (!openssl_linked()) {
        Py_CLEAR(SSLWantReadError);
        Py_CLEAR(SSLWantWriteError);
        Py_CLEAR(SSLSocketType);
        SSL_read_ex = NULL;
        SSL_get_error = NULL;
        SSL_get_shutdown = NULL;
        SSL_pending = NULL;
        PyErr_Clear(); // linking is optional; any failure results in openssl_linked=False
    }
}

bool openssl_linked() {
    return SSL_read_ex &&
        SSL_get_error &&
        SSL_get_shutdown &&
        SSL_pending &&
        SSLWantReadError &&
        SSLWantWriteError &&
        SSLSocketType;
}

/* 0 for a non-blocking socket, else -1 with an exception set */
int require_nonblocking(PyObject *sock) {
    PyObject *blocking = PyObject_CallMethod(sock, "getblocking", NULL);
    const int is_blocking = blocking ? PyObject_IsTrue(blocking) : -1;
    Py_XDECREF(blocking);
    if (is_blocking > 0) PyErr_SetString(PyExc_ValueError, "Only non-blocking sockets are supported");
    return is_blocking == 0 ? 0 : -1;
}

/*
 * Read what a non-blocking ssl.SSLSocket has ready into buf, without the GIL.
 * Returns the bytes read, 0 when the peer closed, -1 with an exception set.
 * *more is set when OpenSSL still holds decrypted bytes.
 */
Py_ssize_t unlocked_ssl_read(PyObject *ssl_socket, char *buf, Py_ssize_t len, bool *more) {
    *more = false;

    PyObject *sslobj = PyObject_GetAttrString(ssl_socket, "_sslobj");
    if (!sslobj || Py_IsNone(sslobj)) {
        // None once the socket is closed
        Py_XDECREF(sslobj);
        PyErr_Clear();
        raise_socket_closed("Could not find _sslobj attribute");
        return -1;
    }

    if (require_nonblocking(ssl_socket) < 0) {
        Py_DECREF(sslobj);
        return -1;
    }

    if (len <= 0) {
        PyErr_SetString(PyExc_ValueError, "No space left in buffer");
        Py_DECREF(sslobj);
        return -1;
    }

    PySSLSocket *self = (PySSLSocket *)sslobj;
    PySocketSockObject *sock = NULL;
    if (get_socket(self, &sock) < 0) {
        Py_DECREF(sslobj);
        return -1;
    }

    size_t count = 0, readbytes = 0;
    int retval = 1, pending = 0;
    _PySSLError err;
    Py_BEGIN_ALLOW_THREADS;
    while (len > 0) {
        retval = SSL_read_ex(self->ssl, buf + count, (size_t)len, &readbytes);
        if (retval <= 0) break;
        count += readbytes;
        len -= (Py_ssize_t)readbytes;
    }
    err = _PySSL_errno(retval <= 0, self->ssl, retval);
    pending = SSL_pending(self->ssl);
    Py_END_ALLOW_THREADS;
#if PY_VERSION_HEX < SABCTOOLS_PY_HEX(3, 15)
    self->err = err;
#endif

    Py_ssize_t result = (Py_ssize_t)count;
    *more = pending > 0;
    if (count == 0) {
        if (PyErr_CheckSignals()) {
            result = -1;
        } else if (err.ssl == SSL_ERROR_WANT_READ) {
            PyErr_SetString(SSLWantReadError, "The operation did not complete (read)");
            result = -1;
        } else if (err.ssl == SSL_ERROR_WANT_WRITE) {
            PyErr_SetString(SSLWantWriteError, "The operation did not complete (write)");
            result = -1;
        } else if (!(err.ssl == SSL_ERROR_ZERO_RETURN && SSL_get_shutdown(self->ssl) == SSL_RECEIVED_SHUTDOWN)) {
            // Fatal, needs a new connection
            PyErr_SetString(PyExc_ConnectionAbortedError, "Failed to read data");
            result = -1;
        }
    }
    Py_XDECREF(sock);
    Py_DECREF(sslobj);
    return result;
}

/* Whether unlocked_ssl_read() can read obj */
bool is_unlocked_ssl_socket(PyObject *obj) {
    return openssl_linked() && PyObject_TypeCheck(obj, SSLSocketType);
}

/* Clear the error if it only says nothing was ready yet */
bool unlocked_ssl_clear_want() {
    if (PyErr_ExceptionMatches(SSLWantReadError) || PyErr_ExceptionMatches(SSLWantWriteError)) {
        PyErr_Clear();
        return true;
    }
    return false;
}

PyObject* unlocked_ssl_recv_into(PyObject* self, PyObject* args) {
    PyObject *ssl_socket;
    Py_buffer buffer;

    if (!openssl_linked()) {
        PyErr_SetString(PyExc_OSError, "Failed to link with OpenSSL");
        return NULL;
    }

    if (!PyArg_ParseTuple(args, "O!w*:unlocked_ssl_recv_into", SSLSocketType, &ssl_socket, &buffer)) {
        return NULL;
    }

    bool more;
    const Py_ssize_t count = unlocked_ssl_read(ssl_socket, (char *)buffer.buf, buffer.len, &more);
    PyBuffer_Release(&buffer);
    return count < 0 ? NULL : PyLong_FromSsize_t(count);
}
