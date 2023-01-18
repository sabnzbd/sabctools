#ifndef SABYENC_CRC32_H
#define SABYENC_CRC32_H

#include <Python.h>

PyObject* crc32_combine(PyObject *, PyObject*);
PyObject* crc32_multiply(PyObject *, PyObject*);
PyObject* crc32_zero_unpad(PyObject *, PyObject*);
PyObject* crc32_XpowN(PyObject *, PyObject*);
PyObject* crc32_Xpow8N(PyObject *, PyObject*);

#endif //SABYENC_CRC32_H
