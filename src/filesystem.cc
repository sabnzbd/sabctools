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

#include "filesystem.h"

static PyObject *Py_msvcrt_module = NULL;
static PyObject *fileno_string = NULL;

void filesystem_init()
{
    fileno_string = PyUnicode_FromString("fileno");
#if defined(_WIN32) || defined(__CYGWIN__)
    Py_msvcrt_module = PyImport_ImportModule("msvcrt");
#endif
}

#if defined(_WIN32) || defined(__CYGWIN__)
static int allocate_file_windows(PyObject *Py_file, long long length, int sparse)
{
    PyObject *Py_file_fileno = NULL;
    PyObject *Py_file_handle = NULL;
    HANDLE handle = NULL;

    if (Py_msvcrt_module == NULL)
    {
        PyErr_SetString(PyExc_SystemError, "msvcrt module not loaded.");
        return -1;
    }

    if (PyLong_Check(Py_file))
    {
        Py_file_fileno = Py_file;
        Py_INCREF(Py_file_fileno);
    }
    else
    {
        Py_file_fileno = PyObject_CallMethodNoArgs(Py_file, fileno_string);
        if (!Py_file_fileno)
        {
            PyErr_SetString(PyExc_SystemError, "Error calling fileno function.");
            return -1;
        }
    }

    Py_file_handle = PyObject_CallMethod(Py_msvcrt_module, "get_osfhandle", "O", Py_file_fileno);
    if (!Py_file_handle)
    {
        Py_XDECREF(Py_file_fileno);
        PyErr_SetString(PyExc_SystemError, "Failed calling get_osfhandle function.");
        return -1;
    }

    handle = PyLong_AsVoidPtr(Py_file_handle);

    if (sparse)
    {
        // Creating a sparse file may fail; ignore the error
        DWORD bytesReturned;
        Py_BEGIN_ALLOW_THREADS
        DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytesReturned, nullptr);
        Py_END_ALLOW_THREADS
    }

    // Set the file length to `length` and restore the original position
    LARGE_INTEGER li_size;
    li_size.QuadPart = length;
    LARGE_INTEGER li_start;
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    BOOL ok;
    DWORD last_error = 0;

    Py_BEGIN_ALLOW_THREADS
    if (!SetFilePointerEx(handle, zero, &li_start, FILE_CURRENT) ||
        !SetFilePointerEx(handle, li_size, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(handle) ||
        !SetFilePointerEx(handle, li_start, nullptr, FILE_BEGIN))
    {
        ok = FALSE;
        last_error = GetLastError();
    }
    else
    {
        ok = TRUE;
    }
    Py_END_ALLOW_THREADS

    Py_XDECREF(Py_file_handle);
    Py_XDECREF(Py_file_fileno);
    if (!ok)
    {
        PyErr_SetFromWindowsErr(last_error);
        return -1;
    }

    return 0;
}
#else
static int allocate_file_posix(PyObject *Py_file, long long length, int preallocate)
{
    PyObject *Py_file_fileno = NULL;
    int fd;

    if (PyLong_Check(Py_file))
    {
        fd = (int)PyLong_AsLong(Py_file);
        if (fd == -1 && PyErr_Occurred())
        {
            return -1;
        }
    }
    else
    {
        Py_file_fileno = PyObject_CallMethodNoArgs(Py_file, fileno_string);
        if (!Py_file_fileno)
        {
            PyErr_SetString(PyExc_SystemError, "Error calling fileno function.");
            return -1;
        }

        fd = (int)PyLong_AsLong(Py_file_fileno);
        if (fd == -1 && PyErr_Occurred())
        {
            Py_XDECREF(Py_file_fileno);
            return -1;
        }
    }

#ifdef HAVE_POSIX_FALLOCATE
    if (preallocate)
    {
        int r;
        Py_BEGIN_ALLOW_THREADS
        r = posix_fallocate(fd, 0, length);
        Py_END_ALLOW_THREADS

        if (r == 0)
        {
            Py_XDECREF(Py_file_fileno);
            return 0;
        }
        if (r != EOPNOTSUPP && r != ENOSYS && r != EINVAL)
        {
            errno = r;
            PyErr_SetFromErrno(PyExc_OSError);
            Py_XDECREF(Py_file_fileno);
            return -1;
        }
    }
#endif

    int trunc_res;
    int trunc_errno = 0;

    Py_BEGIN_ALLOW_THREADS
    trunc_res = ftruncate(fd, length);
    trunc_errno = (trunc_res == -1) ? errno : 0;
    Py_END_ALLOW_THREADS

    if (trunc_res == -1)
    {
        // If ftruncate failed because it's unsupported, optionally expand the file by writing (slow)
        if (preallocate && (trunc_errno == EOPNOTSUPP || trunc_errno == ENOTSUP))
        {
            off_t cur = 0;
            off_t old_size = 0;
            int write_error = 0;
            int write_errno = 0;

            Py_BEGIN_ALLOW_THREADS
            cur = lseek(fd, 0, SEEK_CUR);
            if (cur == -1)
            {
                write_error = 1;
                write_errno = errno;
            }
            else
            {
                old_size = lseek(fd, 0, SEEK_END);
                if (old_size == -1)
                {
                    write_error = 1;
                    write_errno = errno;
                }
                else
                {
                    // Sparse not supported; fill manually in chunks
                    char buffer[ZERO_CHUNK_SIZE];
                    memset(buffer, 0, ZERO_CHUNK_SIZE);
                    off_t remaining = length - old_size;
                    while (remaining > 0)
                    {
                        ssize_t to_write = (remaining > ZERO_CHUNK_SIZE) ? ZERO_CHUNK_SIZE : remaining;
                        ssize_t written = write(fd, buffer, to_write);
                        if (written <= 0)
                        {
                            write_error = 1;
                            write_errno = errno;
                            break;
                        }
                        remaining -= written;
                    }

                    // Restore original position
                    if (!write_error && lseek(fd, cur, SEEK_SET) == -1)
                    {
                        write_error = 1;
                        write_errno = errno;
                    }
                }
            }
            Py_END_ALLOW_THREADS

            if (write_error)
            {
                errno = write_errno;
                PyErr_SetFromErrno(PyExc_OSError);
                Py_XDECREF(Py_file_fileno);
                return -1;
            }
        }
        else
        {
            errno = trunc_errno;
            PyErr_SetFromErrno(PyExc_OSError);

            Py_XDECREF(Py_file_fileno);
            return -1;
        }
    }

    Py_XDECREF(Py_file_fileno);
    return 0;
}
#endif

PyObject *allocate_file(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *Py_file;
    long long length;
    int sparse = 1;
    int preallocate = 1;

    // Accept either a file object (with fileno()) or an integer file descriptor directly
    static const char *kwlist[] = {"file", "length", "sparse", "preallocate", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OL|pp:sparse", const_cast<char **>(kwlist),
                                     &Py_file, &length, &sparse, &preallocate))
    {
        return NULL;
    }

    if (length < 0)
    {
        PyErr_SetString(PyExc_ValueError, "length must be non-negative");
        return NULL;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    if (allocate_file_windows(Py_file, length, sparse) < 0)
    {
        return NULL;
    }
#else
    if (allocate_file_posix(Py_file, length, preallocate) < 0)
    {
        return NULL;
    }
#endif

    Py_RETURN_NONE;
}
