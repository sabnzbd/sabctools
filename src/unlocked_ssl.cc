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

static int (*SSL_read_ex)(void*, void*, size_t, size_t*) = NULL;
static int (*SSL_get_error)(void*, int) = NULL;
static int (*SSL_get_shutdown)(void*) = NULL;
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

typedef enum {
    SOCKET_IS_NONBLOCKING,
    SOCKET_IS_BLOCKING,
    SOCKET_HAS_TIMED_OUT,
    SOCKET_HAS_BEEN_CLOSED,
    SOCKET_TOO_LARGE_FOR_SELECT,
    SOCKET_OPERATION_OK
} timeout_state;

static int get_socket(PySSLSocket *obj, PySocketSockObject **out_sock)
{
    *out_sock = NULL;

    if (!obj->Socket) {
        return 0;
    }

#if PY_VERSION_HEX >= SABCTOOLS_PY_HEX(3, 13)
    PySocketSockObject *sock = NULL;

    int res = PyWeakref_GetRef(obj->Socket, (PyObject **)&sock);
    if (res < 0) {
        return -1; // exception already set
    }
    if (res == 0) {
        return -1; // dead weakref
    }

    // sock is a now a NEW reference

    if (sock->sock_fd == INVALID_SOCKET) {
        Py_DECREF(sock);
        PyErr_SetString(PyExc_ValueError, "Socket already closed");
        return -1;
    }

    *out_sock = sock;
    return 1;
#else
    PyObject *tmp = PyWeakref_GetObject(obj->Socket);

    if (tmp == Py_None) {
        return -1;
    }

    PySocketSockObject *sock = (PySocketSockObject *)tmp;

    // Promote to an owned reference
    Py_INCREF(sock);

    if (sock->sock_fd == INVALID_SOCKET) {
        Py_DECREF(sock);
        PyErr_SetString(PyExc_ValueError, "Socket already closed");
        return -1;
    }

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

    *(void**)&SSL_read_ex = GetProcAddress(openssl_handle, "SSL_read_ex");
    *(void**)&SSL_get_error = GetProcAddress(openssl_handle, "SSL_get_error");
    *(void**)&SSL_get_shutdown = GetProcAddress(openssl_handle, "SSL_get_shutdown");
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
        PyErr_Clear(); // linking is optional; any failure results in openssl_linked=False
    }
}

bool openssl_linked() {
    return SSL_read_ex &&
        SSL_get_error &&
        SSL_get_shutdown &&
        SSLWantReadError &&
        SSLWantWriteError &&
        SSLSocketType;
}

static PyObject* unlocked_ssl_recv_into_impl(PySSLSocket *self, Py_ssize_t len, Py_buffer *buffer) {
    char *mem;
    size_t count = 0;
    size_t readbytes = 0;
    int retval;
    int sockstate;
    _PySSLError err;

    PySocketSockObject *sock = NULL;
    if (get_socket(self, &sock) < 0) {
        PyErr_SetString(PyExc_ValueError, "Underlying socket connection gone");
        return NULL;
    }

    mem = (char *)buffer->buf;
    if (len <= 0 || len > buffer->len) {
        len = (int) buffer->len;
        if (buffer->len != len) {
            PyErr_SetString(PyExc_OverflowError,
                            "maximum length can't fit in a C 'int'");
            goto error;
        }
        if (len == 0) {
            count = 0;
            goto done;
        }
    }

    do {
        Py_BEGIN_ALLOW_THREADS;
        do {
            retval = SSL_read_ex(self->ssl, mem + count, len, &readbytes);
            if (retval <= 0) {
                break;
            }
            count += readbytes;
            len -= readbytes;
        } while (len > 0);
        err = _PySSL_errno(retval == 0, self->ssl, retval);
        Py_END_ALLOW_THREADS;
#if PY_VERSION_HEX < SABCTOOLS_PY_HEX(3, 15)
        self->err = err;
#endif

        if (count > 0) {
            break;
        }

        if (PyErr_CheckSignals())
            goto error;

        if (err.ssl == SSL_ERROR_WANT_READ) {
            sockstate = SOCKET_IS_NONBLOCKING;
        } else if (err.ssl == SSL_ERROR_WANT_WRITE) {
            sockstate = SOCKET_IS_NONBLOCKING;
        } else if (err.ssl == SSL_ERROR_ZERO_RETURN &&
                SSL_get_shutdown(self->ssl) == SSL_RECEIVED_SHUTDOWN)
        {
            count = 0;
            goto done;
        }
        else
            sockstate = SOCKET_OPERATION_OK;

        if (sockstate == SOCKET_IS_NONBLOCKING) {
            break;
        }
    } while (err.ssl == SSL_ERROR_WANT_READ ||
             err.ssl == SSL_ERROR_WANT_WRITE);

    if (count == 0) {
        if (err.ssl == SSL_ERROR_WANT_READ) {
            PyErr_SetString(SSLWantReadError, "The operation did not complete (read)");
        }
        else if (err.ssl == SSL_ERROR_WANT_WRITE) {
            PyErr_SetString(SSLWantWriteError, "The operation did not complete (write)");
        } else {
            // Raise general error, as all errors that are left indicate fatal errors
            // The calling code will have to establish a new connection
            PyErr_SetString(PyExc_ConnectionAbortedError, "Failed to read data");
        }
        goto error;
    }

done:
    Py_XDECREF(sock);
    return PyLong_FromSize_t(count);

error:
    Py_XDECREF(sock);
    return NULL;
}

PyObject* unlocked_ssl_recv_into(PyObject* self, PyObject* args) {
    PyObject *ssl_socket;
    PyObject *Py_ssl_socket;
    Py_ssize_t len;
    Py_buffer Py_buffer;
    PyObject *retval = NULL;
    PyObject *blocking = NULL;
    int is_blocking;

    if(!openssl_linked()) {
        PyErr_SetString(PyExc_OSError, "Failed to link with OpenSSL");
        return NULL;
    }

    // Parse input
    if (!PyArg_ParseTuple(args, "O!w*:unlocked_ssl_recv_into", SSLSocketType, &ssl_socket, &Py_buffer)) {
        return NULL;
    }

    if (!PyObject_TypeCheck(ssl_socket, SSLSocketType)) {
        PyErr_Format(PyExc_TypeError, "argument 1 must be %s", SSLSocketType->tp_name);
        return NULL;
    }

    Py_ssl_socket = PyObject_GetAttrString(ssl_socket, "_sslobj");
    if (!Py_ssl_socket) {
        PyErr_SetString(PyExc_ValueError, "Could not find _sslobj attribute");
        goto error;
    }

    blocking = PyObject_CallMethod(ssl_socket, "getblocking", NULL);
    if (!blocking)
        goto error; // call failed
    is_blocking = PyObject_IsTrue(blocking);
    if (is_blocking < 0)
        goto error;
    if (is_blocking) {
        PyErr_SetString(PyExc_ValueError, "Only non-blocking sockets are supported");
        goto error;
    }

    // Basic sanity check
    len = (Py_ssize_t)Py_buffer.len;
    if (len <= 0) {
        PyErr_SetString(PyExc_ValueError, "No space left in buffer");
        goto error;
    }

    retval = unlocked_ssl_recv_into_impl((PySSLSocket*)Py_ssl_socket, len, &Py_buffer);

error:
    PyBuffer_Release(&Py_buffer);
    Py_XDECREF(Py_ssl_socket);
    Py_XDECREF(blocking);
    return retval;
}
