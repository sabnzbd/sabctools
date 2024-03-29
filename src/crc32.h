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

#ifndef SABCTOOLS_CRC32_H
#define SABCTOOLS_CRC32_H

#include <Python.h>

PyObject* crc32_combine(PyObject *, PyObject*);
PyObject* crc32_multiply(PyObject *, PyObject*);
PyObject* crc32_zero_unpad(PyObject *, PyObject*);
PyObject* crc32_xpown(PyObject *, PyObject*);
PyObject* crc32_xpow8n(PyObject *, PyObject*);

#endif //SABCTOOLS_CRC32_H
