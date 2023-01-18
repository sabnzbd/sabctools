#include "crc32.h"
#include "crcutil-1.0/examples/interface.h"

extern crcutil_interface::CRC *crc;

PyObject* crc32_combine(PyObject *self, PyObject *args) {
    uint32_t crc1, crc2;
    size_t length;

    if(!PyArg_ParseTuple(args, "IIn:crc32_combine", &crc1, &crc2, &length)) {
        return NULL;
    }

    crcutil_interface::UINT64 crc1_ = crc1, crc2_ = crc2;
    crc->Concatenate(crc2_, 0, length, &crc1_);

    return PyLong_FromUnsignedLong((uint32_t) crc1_);
}

PyObject* crc32_multiply(PyObject *self, PyObject *args) {
    uint32_t crc1, crc2;

    if(!PyArg_ParseTuple(args, "II:crc32_multiply", &crc1, &crc2)) {
        return NULL;
    }

    crcutil_interface::UINT64 crc1_ = crc1, crc2_ = crc2;
    crc->Multiply(crc2_, &crc1_);

    return PyLong_FromUnsignedLong((uint32_t) crc1_);
}

PyObject* crc32_zero_unpad(PyObject *self, PyObject *args) {
    uint32_t crc1;
    size_t length;

    if(!PyArg_ParseTuple(args, "In:crc32_zero_unpad", &crc1, &length)) {
        return NULL;
    }

    crcutil_interface::UINT64 crc_ = crc1;
    crc->ZeroUnpad(length, &crc_);

    return PyLong_FromUnsignedLong((uint32_t) crc_);
}

PyObject* crc32_XpowN(PyObject* self, PyObject* arg) {
    crcutil_interface::UINT64 n = PyLong_AsUnsignedLongLong(arg);

    if (PyErr_Occurred()) {
        return NULL;
    }

    crc->XpowN(&n);

    return PyLong_FromUnsignedLongLong(n);
}

PyObject* crc32_Xpow8N(PyObject* self, PyObject* arg) {
    crcutil_interface::UINT64 n = PyLong_AsUnsignedLongLong(arg);

    if (PyErr_Occurred()) {
        return NULL;
    }

    crc->Xpow8N(&n);

    return PyLong_FromUnsignedLongLong(n);
}
