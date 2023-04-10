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

PyObject *sparse(PyObject *self, PyObject *args)
{
    PyObject *Py_file;
    PyObject *Py_file_fileno_function = NULL;
    PyObject *Py_file_fileno = NULL;
    PyObject *Py_msvcrt_module = NULL;
    PyObject *Py_get_osfhandle_function = NULL;
    PyObject *Py_get_osfhandle_args = NULL;
    PyObject *Py_file_handle = NULL;
    PyObject *Py_file_truncate_function = NULL;
    PyObject *Py_file_truncate = NULL;
    long long length;

    if (!PyArg_ParseTuple(args, "OL:sparse", &Py_file, &length))
    {
        return NULL;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    // Get the windows file handle and set file attributes to sparse

    if (!(Py_file_fileno_function = PyObject_GetAttrString(Py_file, "fileno")))
    {
        PyErr_SetString(PyExc_TypeError, "Object does not have a fileno function.");
        goto error;
    }

    if (!(Py_file_fileno = PyObject_CallObject(Py_file_fileno_function, NULL)))
    {
        PyErr_SetString(PyExc_SystemError, "Error calling fileno function.");
        goto error;
    }

    if (!(Py_msvcrt_module = PyImport_ImportModule("msvcrt")))
    {
        goto error;
    }

    if (!(Py_get_osfhandle_function = PyObject_GetAttrString(Py_msvcrt_module, "get_osfhandle")))
    {
        PyErr_SetString(PyExc_SystemError, "Failed to get get_osfhandle function.");
        goto error;
    }

    Py_get_osfhandle_args = Py_BuildValue("(O)", Py_file_fileno);
    if (!(Py_file_handle = PyObject_CallObject(Py_get_osfhandle_function, Py_get_osfhandle_args)))
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

    if (!(Py_file_truncate_function = PyObject_GetAttrString(Py_file, "truncate")))
    {
        PyErr_SetString(PyExc_TypeError, "Object does not have a truncate function.");
        goto error;
    }

    if (!(Py_file_truncate = PyObject_CallObject(Py_file_truncate_function, Py_BuildValue("(L)", length))))
    {
        PyErr_SetString(PyExc_SystemError, "Error calling truncate function.");
        goto error;
    }
#endif

done:
    Py_XDECREF(Py_file_fileno_function);
    Py_XDECREF(Py_file_fileno);
    Py_XDECREF(Py_msvcrt_module);
    Py_XDECREF(Py_get_osfhandle_function);
    Py_XDECREF(Py_get_osfhandle_args);
    Py_XDECREF(Py_file_handle);
    Py_XDECREF(Py_file_truncate_function);
    Py_XDECREF(Py_file_truncate);
    Py_RETURN_NONE;

error:
    Py_XDECREF(Py_file_fileno_function);
    Py_XDECREF(Py_file_fileno);
    Py_XDECREF(Py_msvcrt_module);
    Py_XDECREF(Py_get_osfhandle_function);
    Py_XDECREF(Py_get_osfhandle_args);
    Py_XDECREF(Py_file_handle);
    Py_XDECREF(Py_file_truncate_function);
    Py_XDECREF(Py_file_truncate);
    return NULL;
}
