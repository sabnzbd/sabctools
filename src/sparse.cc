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

#include "sparse.h"

PyObject *Py_msvcrt_module = NULL;
PyObject *get_osfhandle_string = NULL;

void sparse_init()
{
#if defined(_WIN32) || defined(__CYGWIN__)
    Py_msvcrt_module = PyImport_ImportModule("msvcrt");
    get_osfhandle_string = PyUnicode_FromString("get_osfhandle");
#endif
}

PyObject *sparse(PyObject *self, PyObject *args)
{
    PyObject *Py_file;
    long long length;

    PyObject *Py_file_fileno = NULL;
    PyObject *Py_file_handle = NULL;
    PyObject *Py_file_truncate = NULL;

    if (!PyArg_ParseTuple(args, "OL:sparse", &Py_file, &length))
    {
        return NULL;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    // Get the windows file handle and set file attributes to sparse

    if (Py_msvcrt_module == NULL)
    {
        PyErr_SetString(PyExc_SystemError, "msvcrt module not loaded.");
        goto error;
    }

    if (!(Py_file_fileno = PyObject_CallMethod(Py_file, "fileno", NULL)))
    {
        PyErr_SetString(PyExc_SystemError, "Error calling fileno function.");
        goto error;
    }

    if (!(Py_file_handle = PyObject_CallMethodObjArgs(Py_msvcrt_module, get_osfhandle_string, Py_file_fileno, NULL)))
    {
        PyErr_SetString(PyExc_SystemError, "Failed calling get_osfhandle function.");
        goto error;
    }

    HANDLE handle = reinterpret_cast<HANDLE>(PyLong_AsLongLong(Py_file_handle));

    DWORD bytesReturned;
    DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);

    LARGE_INTEGER size64;
    size64.QuadPart = length;
    SetFilePointerEx(handle, size64, nullptr, FILE_END);
    SetEndOfFile(handle);
#else
    // Call file.truncate(length)

    if (!(Py_file_truncate = PyObject_CallMethod(Py_file, "truncate", "(L)", length)))
    {
        PyErr_SetString(PyExc_SystemError, "Error calling truncate function.");
        goto error;
    }
#endif

done:
    Py_XDECREF(Py_file_fileno);
    Py_XDECREF(Py_file_handle);
    Py_XDECREF(Py_file_truncate);
    Py_RETURN_NONE;

error:
    Py_XDECREF(Py_file_fileno);
    Py_XDECREF(Py_file_handle);
    Py_XDECREF(Py_file_truncate);
    return NULL;
}
