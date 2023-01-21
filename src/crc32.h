#ifndef SABYENC_CRC32_H
#define SABYENC_CRC32_H

#include <Python.h>

PyObject* crc32_combine(PyObject *, PyObject*);
PyObject* crc32_multiply(PyObject *, PyObject*);
PyObject* crc32_zero_unpad(PyObject *, PyObject*);
PyObject* crc32_xpown(PyObject *, PyObject*);
PyObject* crc32_xpow8n(PyObject *, PyObject*);

#endif //SABYENC_CRC32_H
