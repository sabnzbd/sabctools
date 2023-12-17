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
