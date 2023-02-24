/*
 * Copyright 2007-2023 The SABnzbd-Team <team@sabnzbd.org>
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

#include "crc32.h"
#include "crcutil-1.0/examples/interface.h"

extern crcutil_interface::CRC *crc;

PyObject* crc32_combine(PyObject *self, PyObject *args) {
    crcutil_interface::UINT64 crc1, crc2;
    unsigned long long length;

    if(!PyArg_ParseTuple(args, "KKK:crc32_combine", &crc1, &crc2, &length)) {
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
    unsigned long long length;

    if(!PyArg_ParseTuple(args, "KK:crc32_zero_unpad", &crc1, &length)) {
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
