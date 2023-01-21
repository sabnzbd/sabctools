#include "crc32.h"
#include "crcutil-1.0/examples/interface.h"

extern crcutil_interface::CRC *crc;

PyObject* crc32_combine(PyObject *self, PyObject *args) {
    crcutil_interface::UINT64 crc1, crc2;
    Py_ssize_t length;

    if(!PyArg_ParseTuple(args, "KKn:crc32_combine", &crc1, &crc2, &length)) {
        return NULL;
    }

    crc->Concatenate(crc2, 0, length, &crc1);

    return PyLong_FromUnsignedLong((uint32_t) crc1);
}

PyObject* crc32_multiply(PyObject *self, PyObject *args) {
    crcutil_interface::UINT64 crc1, crc2;

    if(!PyArg_ParseTuple(args, "KK:crc32_multiply", &crc1, &crc2)) {
        return NULL;
    }

    crc->Multiply(crc2, &crc1);

    return PyLong_FromUnsignedLong((uint32_t)crc1);
}

PyObject* crc32_zero_unpad(PyObject *self, PyObject *args) {
    crcutil_interface::UINT64 crc1;
    Py_ssize_t length;

    if(!PyArg_ParseTuple(args, "Kn:crc32_zero_unpad", &crc1, &length)) {
        return NULL;
    }

    crc->ZeroUnpad(length, &crc1);

    return PyLong_FromUnsignedLong((uint32_t) crc1);
}

PyObject* crc32_xpown(PyObject* self, PyObject* arg) {
    crcutil_interface::UINT64 n = PyLong_AsUnsignedLongLong(arg) % 0xffffffff;

    if (PyErr_Occurred()) {
        return NULL;
    }

    crc->XpowN(&n);

    return PyLong_FromUnsignedLong(n);
}

PyObject* crc32_xpow8n(PyObject* self, PyObject* arg) {
    crcutil_interface::UINT64 n = PyLong_AsUnsignedLongLong(arg) % 0xffffffff;

    if (PyErr_Occurred()) {
        return NULL;
    }

    crc->Xpow8N(&n);

    return PyLong_FromUnsignedLong(n);
}
