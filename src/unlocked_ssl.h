#ifndef SABYENC_UNLOCKED_SSL_RECV_INTO_H
#define SABYENC_UNLOCKED_SSL_RECV_INTO_H

#include <Python.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

/* OpenSSL link */
#if defined(_WIN32) || defined(__CYGWIN__)
# define SABYENC_DLL_CALL __stdcall
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#else
# define SABYENC_DLL_CALL
# include <dlfcn.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

//typedef int (SABYENC_DLL_CALL *ssl_read_ex_func)(const void *, size_t, uint32_t);
//int (SABYENC_DLL_CALL *SSL_read_ex)(void*, void*, size_t, size_t*);

/* Limited sub-struct of Python struct */
typedef struct {
    PyObject_HEAD
            PyObject *Socket; /* We do not use it, but needs to match Python */
    void *ssl;
} PySSLSocket;

/* Have to manually define this OpenSSL constant and hope it never changes */
# define SSL_ERROR_WANT_READ 2

extern PyObject *SSLWantReadError;

void openssl_init();
bool openssl_linked();
PyObject *unlocked_ssl_recv_into(PyObject *, PyObject*);

#ifdef __cplusplus
}
#endif
#endif