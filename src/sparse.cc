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

    // Accept either a file object (with fileno()) or an integer file descriptor directly
    if (!PyArg_ParseTuple(args, "OL:sparse", &Py_file, &length))
    {
        return NULL;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    // Get the windows file handle and set file attributes to sparse

    HANDLE handle = NULL;

    if (Py_msvcrt_module == NULL)
    {
        PyErr_SetString(PyExc_SystemError, "msvcrt module not loaded.");
        goto error;
    }

    if (PyLong_Check(Py_file))
    {
        Py_file_fileno = Py_file;
        Py_INCREF(Py_file_fileno);
    }
    else if (!(Py_file_fileno = PyObject_CallMethod(Py_file, "fileno", NULL)))
    {
        PyErr_SetString(PyExc_SystemError, "Error calling fileno function.");
        goto error;
    }

    if (!(Py_file_handle = PyObject_CallMethodObjArgs(Py_msvcrt_module, get_osfhandle_string, Py_file_fileno, NULL)))
    {
        PyErr_SetString(PyExc_SystemError, "Failed calling get_osfhandle function.");
        goto error;
    }

    handle = reinterpret_cast<HANDLE>(PyLong_AsLongLong(Py_file_handle));

    // Creating a sparse file may fail; only change the file size if it succeeds
    DWORD bytesReturned;
    if (DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr))
    {
        // Set the file length to `length` and restore the original position
        LARGE_INTEGER li_size;
        li_size.QuadPart = length;
        LARGE_INTEGER li_start;
        if (!SetFilePointerEx(handle, {}, &li_start, FILE_CURRENT) ||
            !SetFilePointerEx(handle, li_size, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(handle) ||
            !SetFilePointerEx(handle, li_start, nullptr, FILE_BEGIN))
        {
            PyErr_SetFromWindowsErr(0);
            goto error;
        }
    }
#else
    int fd;
    if (PyLong_Check(Py_file))
    {
        fd = (int)PyLong_AsLong(Py_file);
        if (fd == -1 && PyErr_Occurred())
        {
            goto error;
        }
    }
    else
    {
        if (!(Py_file_fileno = PyObject_CallMethod(Py_file, "fileno", NULL)))
        {
            PyErr_SetString(PyExc_SystemError, "Error calling fileno function.");
            goto error;
        }

        fd = (int)PyLong_AsLong(Py_file_fileno);
        if (fd == -1 && PyErr_Occurred())
        {
            goto error;
        }
    }

    if (ftruncate(fd, (off_t)length) == -1)
    {
        PyErr_SetFromErrno(PyExc_OSError);
        goto error;
    }
#endif

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
