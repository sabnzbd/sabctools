#include "unlocked_ssl.h"

int (SABYENC_DLL_CALL *SSL_read_ex)(void*, void*, size_t, size_t*);
int (SABYENC_DLL_CALL *SSL_get_error)(void*, int);
int (SABYENC_DLL_CALL *SSL_get_shutdown)(void*);

/* Redefined below for Windows debug builds after important #includes */
#define _PySSL_FIX_ERRNO

#define PySSL_BEGIN_ALLOW_THREADS_S(save) \
    do { (save) = PyEval_SaveThread(); } while(0)
#define PySSL_END_ALLOW_THREADS_S(save) \
    do { PyEval_RestoreThread(save); _PySSL_FIX_ERRNO; } while(0)
#define PySSL_BEGIN_ALLOW_THREADS { \
            PyThreadState *_save = NULL;  \
            PySSL_BEGIN_ALLOW_THREADS_S(_save);
#define PySSL_END_ALLOW_THREADS PySSL_END_ALLOW_THREADS_S(_save); }

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
    void *ctx; /* weakref to SSL context */
    char shutdown_seen_zero;
    int socket_type;
    PyObject *owner; /* Python level "owner" passed to servername callback */
    PyObject *server_hostname;
    _PySSLError err; /* last seen error from various sources */
    /* Some SSL callbacks don't have error reporting. Callback wrappers
     * store exception information on the socket. The handshake, read, write,
     * and shutdown methods check for chained exceptions.
     */
    PyObject *exc_type;
    PyObject *exc_value;
    PyObject *exc_tb;
} PySSLSocket;

static inline _PySSLError _PySSL_errno(int failed, void *ssl, int retcode)
{
    _PySSLError err = { 0 };
    if (failed) {
#ifdef MS_WINDOWS
        err.ws = WSAGetLastError();
        _PySSL_FIX_ERRNO;
#endif
        err.c = errno;
        err.ssl = SSL_get_error(ssl, retcode);
    }
    return err;
}

/* The object holding a socket.  It holds some extra information,
   like the address family, which is used to decode socket address
   arguments properly. */

typedef struct {
    PyObject_HEAD
            SOCKET_T sock_fd;           /* Socket file descriptor */
    int sock_family;            /* Address family, e.g., AF_INET */
    int sock_type;              /* Socket type, e.g., SOCK_STREAM */
    int sock_proto;             /* Protocol type, usually 0 */
    PyObject *(*errorhandler)(void); /* Error handler; checks
                                        errno, returns NULL and
                                        sets a Python exception */
    _PyTime_t sock_timeout;     /* Operation timeout in seconds;
                                        0.0 means non-blocking */
} PySocketSockObject;

typedef enum {
    SOCKET_IS_NONBLOCKING,
    SOCKET_IS_BLOCKING,
    SOCKET_HAS_TIMED_OUT,
    SOCKET_HAS_BEEN_CLOSED,
    SOCKET_TOO_LARGE_FOR_SELECT,
    SOCKET_OPERATION_OK
} timeout_state;

/* Get the socket from a PySSLSocket, if it has one */
#define GET_SOCKET(obj) ((obj)->Socket ? \
    (PySocketSockObject *) PyWeakref_GetObject((obj)->Socket) : NULL)

/* Linking to OpenSSL function used by Python */
void openssl_init() {
    // TODO: consider adding an extra version check to avoid possible future changes to SSL_read_ex

    PyObject *ssl_module = PyImport_ImportModule("_ssl");
    if(!ssl_module) return;

#if defined(_WIN32) || defined(__CYGWIN__)
    HMODULE openssl_handle = GetModuleHandle(TEXT("libssl-1_1.dll"));

    // TODO: more DLL names?
    if(!openssl_handle) return;
    *(void**)&SSL_read_ex = GetProcAddress(openssl_handle, "SSL_read_ex");
    *(void**)&SSL_get_error = GetProcAddress(openssl_handle, "SSL_get_error");
    *(void**)&SSL_get_shutdown = GetProcAddress(openssl_handle, "SSL_get_shutdown");
#else
    // Find library at "import ssl; print(ssl._ssl.__file__)"
    PyObject *ssl_module_dict = PyModule_GetDict(ssl_module);
    if(!ssl_module_dict) return;

    PyObject *ssl_module_path = PyDict_GetItemString(ssl_module_dict, "__file__");
    if(!ssl_module_path) return;

    void* openssl_handle = dlopen(PyUnicode_AsUTF8(ssl_module_path), RTLD_LAZY | RTLD_NOLOAD);

    if(!openssl_handle) return;

    *(void**)&SSL_read_ex = dlsym(openssl_handle, "SSL_read_ex");
    *(void**)&SSL_get_error = dlsym(openssl_handle, "SSL_get_error");
    *(void**)&SSL_get_shutdown = dlsym(openssl_handle, "SSL_get_shutdown");

    if (!openssl_linked()) dlclose(openssl_handle);
#endif
}

bool openssl_linked() {
    return SSL_read_ex && SSL_get_error && SSL_get_shutdown;
}

static int PySSL_ChainExceptions(PySSLSocket *sslsock) {
    if (sslsock->exc_type == NULL)
        return 0;

    _PyErr_ChainExceptions(sslsock->exc_type, sslsock->exc_value, sslsock->exc_tb);
    sslsock->exc_type = NULL;
    sslsock->exc_value = NULL;
    sslsock->exc_tb = NULL;
    return -1;
}

static PyObject* unlocked_ssl_recv_into_impl(PySSLSocket *self, Py_ssize_t len, Py_buffer *buffer) {
    char *mem;
    size_t count = 0;
    size_t readbytes = 0;
    int retval;
    int sockstate;
    _PySSLError err;
    PySocketSockObject *sock = GET_SOCKET(self);

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

    if (sock != NULL) {
        if (((PyObject*)sock) == Py_None) {
            // TODO: likely need this exception
//            _setSSLError(get_state_sock(self),
//                         "Underlying socket connection gone",
//                         PY_SSL_ERROR_NO_SOCKET, __FILE__, __LINE__);  // TODO:
            PyErr_SetString(PyExc_ValueError, "Underlying socket connection gone");
            return NULL;
        }
        Py_INCREF(sock);
    }

    do {
        PySSL_BEGIN_ALLOW_THREADS;
        do {
            retval = SSL_read_ex(self->ssl, mem + count, len, &readbytes);
            if (retval <= 0) {
                break;
            }
            count += readbytes;
            len -= readbytes;
        } while (len > 0);
        err = _PySSL_errno(retval == 0, self->ssl, retval);
        PySSL_END_ALLOW_THREADS;
        self->err = err;

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
//        PySSL_SetError(self, retval, __FILE__, __LINE__); // TODO:
        if (err.ssl == SSL_ERROR_WANT_READ) {
            PyErr_SetString(SSLWantReadError, "Need more data");
        } else {
            // Raise general error, since we don't care
            PyErr_SetString(PyExc_ValueError, "Failed to read data");
        }
        goto error;
    }
    if (self->exc_type != NULL)
        goto error;

    done:
    Py_XDECREF(sock);
    return PyLong_FromSize_t(count);

    error:
    PySSL_ChainExceptions(self);
    Py_XDECREF(sock);
    return NULL;
}

PyObject* unlocked_ssl_recv_into(PyObject* self, PyObject* args) {
    PySSLSocket *Py_ssl_socket;
    Py_ssize_t len;
    Py_buffer Py_buffer;
    PyObject *retval;

    if(!openssl_linked()) {
        PyErr_SetString(PyExc_OSError, "Failed to link with OpenSSL");
        return NULL;
    }

    // Parse input
    if (!PyArg_ParseTuple(args, "Ow*:unlocked_ssl_recv_into", &Py_ssl_socket, &Py_buffer)) {
        return NULL;
    }

    // Basic sanity check
    len = (Py_ssize_t)Py_buffer.len;
    if (len <= 0) {
        PyErr_SetString(PyExc_ValueError, "No space left in buffer");
        goto error;
    }

    retval = unlocked_ssl_recv_into_impl(Py_ssl_socket, len, &Py_buffer);
    if (!retval) {
        goto error;
    }

    done:
    PyBuffer_Release(&Py_buffer);
    return retval;

    error:
    PyBuffer_Release(&Py_buffer);
    return NULL;
}
