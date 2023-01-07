#include "unlocked_ssl.h"

int (SABYENC_DLL_CALL *SSL_read_ex)(void*, void*, size_t, size_t*);
int (SABYENC_DLL_CALL *SSL_get_error)(void*, int);

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
    if(!SSL_read_ex || !SSL_get_error) dlclose(openssl_handle);
#endif
}

bool openssl_linked() {
    return SSL_read_ex && SSL_get_error;
}

PyObject* unlocked_ssl_recv_into(PyObject* self, PyObject* args) {
    PySSLSocket *Py_ssl_socket;
    Py_buffer Py_buffer;
    size_t nbytes;
    char *mem;
    size_t count = 0;
    size_t readbytes = 0;
    int retval;
    int err;
    PyObject *Py_retval = NULL;

    if(!openssl_linked()) {
        PyErr_SetString(PyExc_OSError, "Failed to link with OpenSSL");
        return NULL;
    }

    // Parse input
    if (!PyArg_ParseTuple(args, "Ow*:unlocked_ssl_recv", &Py_ssl_socket, &Py_buffer)) {
        return NULL;
    }

    // Basic sanity check
    nbytes = (size_t)Py_buffer.len;
    if (nbytes <= 0) {
        PyErr_SetString(PyExc_ValueError, "No space left in buffer");
        goto finish;
    }
    mem = (char *)Py_buffer.buf;

    Py_BEGIN_ALLOW_THREADS;
    do {
        retval = SSL_read_ex(Py_ssl_socket->ssl, mem + count, nbytes, &readbytes);
        if (retval <= 0) {
            break;
        }
        count += readbytes;
        nbytes -= readbytes;
    } while (nbytes > 0);
    Py_END_ALLOW_THREADS;

    // Check for errors if no data was recieved
    if (count == 0 && retval == 0) {
        err = SSL_get_error(Py_ssl_socket->ssl, retval);
        if (err == SSL_ERROR_WANT_READ) {
            PyErr_SetString(SSLWantReadError, "Need more data");
        } else {
            // Raise general error, since we don't care
            PyErr_SetString(PyExc_ValueError, "Failed to read data");
        }
        goto finish;
    }

    // All good, return number of bytes fetched
    Py_retval = PyLong_FromSize_t(count);

    finish:
    // Release buffer
    PyBuffer_Release(&Py_buffer);
    return Py_retval;
}
