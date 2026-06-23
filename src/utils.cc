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

#include "utils.h"

PyObject* bytearray_malloc(PyObject* self, PyObject* Py_input_size) {
    if(!PyLong_Check(Py_input_size)) {
        PyErr_SetString(PyExc_TypeError, "Expected type 'int'.");
        return NULL;
    }
    return PyByteArray_FromStringAndSize(NULL, PyLong_AsSsize_t(Py_input_size));
}

/**
 * Native implementation of rarfile.Rar3Sha1._corrupt for faster bitwise operations
 *
 * @param self
 * @param args data and dpos
 * @return None
 */
PyObject* rarfile_rar3sha1_corrupt(PyObject *self, PyObject *args)
{
    Py_buffer buf;
    Py_ssize_t dpos;
    uint32_t w[16];
    uint32_t x;
    int i;

    if (!PyArg_ParseTuple(args, "y*n", &buf, &dpos))
        return NULL;

    if (dpos < 0 || dpos + 64 > buf.len) {
        PyBuffer_Release(&buf);
        PyErr_SetString(PyExc_ValueError, "invalid dpos");
        return NULL;
    }

    unsigned char *p = static_cast<unsigned char *>(buf.buf) + dpos;

    /* load big-endian */
    for (i = 0; i < 16; i++) {
        w[i] =
            static_cast<uint32_t>(p[i * 4 + 0]) << 24 |
            static_cast<uint32_t>(p[i * 4 + 1]) << 16 |
            static_cast<uint32_t>(p[i * 4 + 2]) << 8  |
            static_cast<uint32_t>(p[i * 4 + 3]);
    }

    for (i = 16; i < 80; i++) {
        x =
            w[(i - 3) & 15] ^
            w[(i - 8) & 15] ^
            w[(i - 14) & 15] ^
            w[(i - 16) & 15];

        w[i & 15] = (x << 1) | (x >> 31);
    }

    /* store little-endian */
    for (i = 0; i < 16; i++) {
        x = w[i];

        p[i*4+0] = x & 0xff;
        p[i*4+1] = (x >> 8) & 0xff;
        p[i*4+2] = (x >> 16) & 0xff;
        p[i*4+3] = (x >> 24) & 0xff;
    }

    PyBuffer_Release(&buf);
    Py_RETURN_NONE;
}
