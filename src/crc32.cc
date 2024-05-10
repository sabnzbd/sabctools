/*
 * Copyright 2007-2023 The SABnzbd-Team (sabnzbd.org)
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
#include "yencode/crc.h"

PyObject* crc32_combine(PyObject *self, PyObject *args) {
    unsigned long crc1, crc2;
    unsigned long long length;

    if(!PyArg_ParseTuple(args, "kkK:crc32_combine", &crc1, &crc2, &length)) {
        return NULL;
    }

    crc1 = RapidYenc::crc32_combine(crc1, crc2, length);

    return PyLong_FromUnsignedLong(crc1);
}

PyObject* crc32_multiply(PyObject *self, PyObject *args) {
    unsigned long crc1, crc2;

    if(!PyArg_ParseTuple(args, "kk:crc32_multiply", &crc1, &crc2)) {
        return NULL;
    }

    crc1 = RapidYenc::crc32_multiply(crc1, crc2);

    return PyLong_FromUnsignedLong(crc1);
}

PyObject* crc32_zero_unpad(PyObject *self, PyObject *args) {
    unsigned long crc1;
    unsigned long long length;

    if(!PyArg_ParseTuple(args, "kK:crc32_zero_unpad", &crc1, &length)) {
        return NULL;
    }

    crc1 = RapidYenc::crc32_unzero(crc1, length);

    return PyLong_FromUnsignedLong(crc1);
}

PyObject* crc32_xpown(PyObject* self, PyObject* arg) {
    long long n = PyLong_AsLongLong(arg);

    if (PyErr_Occurred()) {
        return NULL;
    }

    unsigned long result = RapidYenc::crc32_2pow(n);

    return PyLong_FromUnsignedLong(result);
}

PyObject* crc32_xpow8n(PyObject* self, PyObject* arg) {
    unsigned long long n = PyLong_AsUnsignedLongLong(arg);

    if (PyErr_Occurred()) {
        return NULL;
    }

    unsigned long result = RapidYenc::crc32_256pow(n);

    return PyLong_FromUnsignedLong(result);
}
