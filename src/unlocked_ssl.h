#ifndef SABYENC_UNLOCKED_SSL_H
#define SABYENC_UNLOCKED_SSL_H

#include <Python.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>

/* OpenSSL link */
#if defined(_WIN32) || defined(__CYGWIN__)
# define SABYENC_DLL_CALL __stdcall
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
# include <winsock2.h>
#else
# define SABYENC_DLL_CALL
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