/*
 * Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
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

#include "filewriter.h"

#include <errno.h>
// memset, for zeroing the OVERLAPPED on Windows
#include <string.h>

#if !defined(_WIN32) && !defined(__CYGWIN__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

/*
 * Largest single write handed to the OS. WriteFile takes a DWORD, and on POSIX some
 * systems refuse writes above SSIZE_MAX, so a long buffer is written in pieces. The
 * loop that does so is also what absorbs a short write, which raw descriptors are
 * allowed to return at any time.
 */
#define FILEWRITER_MAX_CHUNK ((Py_ssize_t)0x3FFFF000)

static PyObject *FileWriter_new(PyTypeObject *type, PyObject *Py_UNUSED(args), PyObject *Py_UNUSED(kwargs)) {
    FileWriter *self = (FileWriter *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    self->handle = SABCTOOLS_INVALID_HANDLE;
    self->path = NULL;
    // The mutex is a real C++ object inside a C struct, so it has to be constructed
    // and destroyed by hand
    new (&self->lock) std::shared_mutex();
    return (PyObject *)self;
}

/* Close without touching the Python API, so it is safe with the GIL released */
static void filewriter_close_handle(FileWriter *self) {
    if (self->handle != SABCTOOLS_INVALID_HANDLE) {
#if defined(_WIN32) || defined(__CYGWIN__)
        CloseHandle(self->handle);
#else
        close(self->handle);
#endif
        self->handle = SABCTOOLS_INVALID_HANDLE;
    }
}

static int FileWriter_init(FileWriter *self, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {(char *)"path", NULL};
    PyObject *path_obj = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:FileWriter", keywords, &path_obj))
        return -1;

    if (self->handle != SABCTOOLS_INVALID_HANDLE) {
        PyErr_SetString(PyExc_RuntimeError, "FileWriter is already open");
        return -1;
    }

    // Accept str, bytes or anything implementing os.PathLike
    PyObject *fspath = PyOS_FSPath(path_obj);
    if (!fspath) return -1;

#if defined(_WIN32) || defined(__CYGWIN__)
    PyObject *as_str = NULL;
    if (PyBytes_Check(fspath)) {
        as_str = PyUnicode_DecodeFSDefaultAndSize(PyBytes_AS_STRING(fspath), PyBytes_GET_SIZE(fspath));
        if (!as_str) {
            Py_DECREF(fspath);
            return -1;
        }
        Py_SETREF(fspath, as_str);
    }

    wchar_t *wide = PyUnicode_AsWideCharString(fspath, NULL);
    if (!wide) {
        Py_DECREF(fspath);
        return -1;
    }

    // Opened directly rather than through msvcrt, so there is a real HANDLE for both
    // the positional writes and the sparse ioctl without a descriptor in between.
    // OPEN_ALWAYS matches O_CREAT without O_TRUNC: create it, or open what is there.
    HANDLE handle = CreateFileW(
        wide,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    PyMem_Free(wide);

    if (handle == INVALID_HANDLE_VALUE) {
        PyErr_SetExcFromWindowsErrWithFilenameObject(PyExc_OSError, 0, fspath);
        Py_DECREF(fspath);
        return -1;
    }
#else
    PyObject *encoded = NULL;
    if (!PyUnicode_FSConverter(fspath, &encoded)) {
        Py_DECREF(fspath);
        return -1;
    }

    int handle;
    const char *filename = PyBytes_AS_STRING(encoded);
    Py_BEGIN_ALLOW_THREADS
    do {
        handle = open(filename, O_CREAT | O_WRONLY, 0666);
    } while (handle < 0 && errno == EINTR);
    Py_END_ALLOW_THREADS
    Py_DECREF(encoded);

    if (handle < 0) {
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, fspath);
        Py_DECREF(fspath);
        return -1;
    }
#endif

    self->handle = handle;
    Py_XSETREF(self->path, fspath);
    return 0;
}

static void FileWriter_dealloc(FileWriter *self) {
    filewriter_close_handle(self);
    Py_CLEAR(self->path);
    self->lock.~shared_mutex();
    Py_TYPE(self)->tp_free((PyObject *)self);
}

/*
 * Write the whole buffer at an absolute offset.
 *
 * The GIL is dropped for the duration, so nothing here may touch the Python API; the
 * buffer is pinned by the caller beforehand and any failure is carried out as an
 * error number and raised once the GIL is back.
 */
static PyObject *FileWriter_write(FileWriter *self, PyObject *args) {
    Py_buffer data;
    long long offset;

    if (!PyArg_ParseTuple(args, "y*L:write", &data, &offset))
        return NULL;

    if (offset < 0) {
        PyBuffer_Release(&data);
        PyErr_SetString(PyExc_ValueError, "offset must not be negative");
        return NULL;
    }

    const char *buffer = (const char *)data.buf;
    const Py_ssize_t length = data.len;
    Py_ssize_t written_total = 0;
    bool was_closed = false;
#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD error_code = 0;
#else
    int error_code = 0;
#endif

    Py_BEGIN_ALLOW_THREADS
    {
        // Shared: concurrent writes are allowed and are the whole point. Only close()
        // takes this exclusively, so the handle cannot be pulled away mid-write.
        std::shared_lock<std::shared_mutex> guard(self->lock);

        if (self->handle == SABCTOOLS_INVALID_HANDLE) {
            was_closed = true;
        } else {
            while (written_total < length) {
                Py_ssize_t remaining = length - written_total;
                if (remaining > FILEWRITER_MAX_CHUNK) remaining = FILEWRITER_MAX_CHUNK;

#if defined(_WIN32) || defined(__CYGWIN__)
                // WriteFile with an OVERLAPPED offset writes positionally even on a
                // handle not opened for overlapped I/O. It shifts the file pointer,
                // which nothing here reads, so no lock is needed to keep writes apart.
                OVERLAPPED overlapped;
                memset(&overlapped, 0, sizeof(overlapped));
                ULARGE_INTEGER position;
                position.QuadPart = (ULONGLONG)(offset + written_total);
                overlapped.Offset = position.LowPart;
                overlapped.OffsetHigh = position.HighPart;

                DWORD written = 0;
                if (!WriteFile(self->handle, buffer + written_total, (DWORD)remaining, &written, &overlapped)) {
                    error_code = GetLastError();
                    break;
                }
                if (written == 0) {
                    error_code = ERROR_DISK_FULL;
                    break;
                }
                written_total += (Py_ssize_t)written;
#else
                ssize_t written = pwrite(self->handle, buffer + written_total, (size_t)remaining,
                                         (off_t)(offset + written_total));
                if (written < 0) {
                    if (errno == EINTR) continue;
                    error_code = errno;
                    break;
                }
                if (written == 0) {
                    // Not documented to happen for a regular file, but looping on it
                    // forever would be worse than reporting a full disk
                    error_code = ENOSPC;
                    break;
                }
                written_total += (Py_ssize_t)written;
#endif
            }
        }
    }
    Py_END_ALLOW_THREADS

    PyBuffer_Release(&data);

    if (was_closed) {
        PyErr_SetString(PyExc_ValueError, "write on closed FileWriter");
        return NULL;
    }
    if (error_code) {
#if defined(_WIN32) || defined(__CYGWIN__)
        PyErr_SetExcFromWindowsErrWithFilenameObject(PyExc_OSError, (int)error_code, self->path);
#else
        errno = error_code;
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, self->path);
#endif
        return NULL;
    }
    return PyLong_FromSsize_t(written_total);
}

/*
 * Set the file length, marking it sparse first where the filesystem needs telling.
 *
 * Behaviour matches the older sparse() deliberately, so this stage swaps one for the
 * other without changing what SABnzbd sees. That includes the Windows wart: if the
 * sparse ioctl fails the length is left alone and no error is raised, because a
 * filesystem that cannot do sparse files would otherwise have the full length
 * physically allocated here.
 */
static PyObject *FileWriter_preallocate(FileWriter *self, PyObject *arg) {
    long long length = PyLong_AsLongLong(arg);
    if (length == -1 && PyErr_Occurred()) return NULL;

    if (length < 0) {
        PyErr_SetString(PyExc_ValueError, "length must not be negative");
        return NULL;
    }

    bool was_closed = false;
#if defined(_WIN32) || defined(__CYGWIN__)
    DWORD error_code = 0;
#else
    int error_code = 0;
#endif

    Py_BEGIN_ALLOW_THREADS
    {
        std::shared_lock<std::shared_mutex> guard(self->lock);

        if (self->handle == SABCTOOLS_INVALID_HANDLE) {
            was_closed = true;
        } else {
#if defined(_WIN32) || defined(__CYGWIN__)
            DWORD bytes_returned;
            if (DeviceIoControl(self->handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytes_returned, NULL)) {
                LARGE_INTEGER size;
                size.QuadPart = length;
                if (!SetFilePointerEx(self->handle, size, NULL, FILE_BEGIN) || !SetEndOfFile(self->handle)) {
                    error_code = GetLastError();
                }
            }
#else
            int result;
            do {
                result = ftruncate(self->handle, (off_t)length);
            } while (result < 0 && errno == EINTR);
            if (result < 0) error_code = errno;
#endif
        }
    }
    Py_END_ALLOW_THREADS

    if (was_closed) {
        PyErr_SetString(PyExc_ValueError, "preallocate on closed FileWriter");
        return NULL;
    }
    if (error_code) {
#if defined(_WIN32) || defined(__CYGWIN__)
        PyErr_SetExcFromWindowsErrWithFilenameObject(PyExc_OSError, (int)error_code, self->path);
#else
        errno = error_code;
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, self->path);
#endif
        return NULL;
    }
    Py_RETURN_NONE;
}

/*
 * Close the file. Idempotent, so it is safe from a finally block or twice over.
 *
 * The GIL is dropped before the exclusive lock is taken. Not for deadlock reasons -
 * a writer releases the shared lock before it reaches for the GIL again, so holding
 * the GIL here would still make progress - but because waiting for a multi-megabyte
 * write to drain while holding the GIL would stall every other Python thread in the
 * process for the duration.
 */
static PyObject *FileWriter_close(FileWriter *self, PyObject *Py_UNUSED(ignored)) {
    Py_BEGIN_ALLOW_THREADS
    {
        std::unique_lock<std::shared_mutex> guard(self->lock);
        filewriter_close_handle(self);
    }
    Py_END_ALLOW_THREADS
    Py_RETURN_NONE;
}

static PyObject *FileWriter_enter(FileWriter *self, PyObject *Py_UNUSED(ignored)) {
    if (self->handle == SABCTOOLS_INVALID_HANDLE) {
        PyErr_SetString(PyExc_ValueError, "FileWriter is closed");
        return NULL;
    }
    Py_INCREF(self);
    return (PyObject *)self;
}

static PyObject *FileWriter_exit(FileWriter *self, PyObject *Py_UNUSED(args)) {
    return FileWriter_close(self, NULL);
}

/*
 * Current length of the file.
 *
 * Taken from the handle this object owns rather than from the path, so it cannot
 * disagree with the file actually being written. Callers use it to tell an existing
 * file from one they have just created, which decides whether to preallocate.
 */
static PyObject *FileWriter_get_size(FileWriter *self, void *Py_UNUSED(closure)) {
    if (self->handle == SABCTOOLS_INVALID_HANDLE) {
        PyErr_SetString(PyExc_ValueError, "size of closed FileWriter");
        return NULL;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    LARGE_INTEGER size;
    if (!GetFileSizeEx(self->handle, &size)) {
        PyErr_SetExcFromWindowsErrWithFilenameObject(PyExc_OSError, 0, self->path);
        return NULL;
    }
    return PyLong_FromLongLong((long long)size.QuadPart);
#else
    struct stat info;
    if (fstat(self->handle, &info) < 0) {
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, self->path);
        return NULL;
    }
    return PyLong_FromLongLong((long long)info.st_size);
#endif
}

static PyObject *FileWriter_get_closed(FileWriter *self, void *Py_UNUSED(closure)) {
    return PyBool_FromLong(self->handle == SABCTOOLS_INVALID_HANDLE);
}

static PyObject *FileWriter_get_path(FileWriter *self, void *Py_UNUSED(closure)) {
    if (!self->path) Py_RETURN_NONE;
    Py_INCREF(self->path);
    return self->path;
}

static PyObject *FileWriter_repr(FileWriter *self) {
    return PyUnicode_FromFormat("<sabctools.FileWriter path=%R closed=%s>", self->path ? self->path : Py_None,
                                self->handle == SABCTOOLS_INVALID_HANDLE ? "True" : "False");
}

static PyMethodDef FileWriter_methods[] = {
    {"write", (PyCFunction)FileWriter_write, METH_VARARGS,
     PyDoc_STR("write(data, offset) -> int\n\nWrite all of data at an absolute offset, returning the bytes written.")},
    {"preallocate", (PyCFunction)FileWriter_preallocate, METH_O,
     PyDoc_STR("preallocate(length)\n\nSet the file length, marking it sparse first where required.")},
    {"close", (PyCFunction)FileWriter_close, METH_NOARGS,
     PyDoc_STR("close()\n\nClose the file. Idempotent, and waits for writes in flight.")},
    {"__enter__", (PyCFunction)FileWriter_enter, METH_NOARGS, NULL},
    {"__exit__", (PyCFunction)FileWriter_exit, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};

static PyGetSetDef FileWriter_getset[] = {
    {"closed", (getter)FileWriter_get_closed, NULL, PyDoc_STR("Has the file been closed"), NULL},
    {"path", (getter)FileWriter_get_path, NULL, PyDoc_STR("Path the file was opened with"), NULL},
    {"size", (getter)FileWriter_get_size, NULL, PyDoc_STR("Current length of the file in bytes"), NULL},
    {NULL, NULL, NULL, NULL, NULL}
};

PyTypeObject FileWriterType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "sabctools.FileWriter",                 // tp_name
    sizeof(FileWriter),                     // tp_basicsize
    0,                                      // tp_itemsize
    (destructor)FileWriter_dealloc,         // tp_dealloc
    0,                                      // tp_vectorcall_offset
    nullptr,                                // tp_getattr
    nullptr,                                // tp_setattr
    nullptr,                                // tp_as_async
    (reprfunc)FileWriter_repr,              // tp_repr
    nullptr,                                // tp_as_number
    nullptr,                                // tp_as_sequence
    nullptr,                                // tp_as_mapping
    nullptr,                                // tp_hash
    nullptr,                                // tp_call
    nullptr,                                // tp_str
    nullptr,                                // tp_getattro
    nullptr,                                // tp_setattro
    nullptr,                                // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                     // tp_flags
    PyDoc_STR("FileWriter(path)"),          // tp_doc
    nullptr,                                // tp_traverse
    nullptr,                                // tp_clear
    nullptr,                                // tp_richcompare
    0,                                      // tp_weaklistoffset
    nullptr,                                // tp_iter
    nullptr,                                // tp_iternext
    FileWriter_methods,                     // tp_methods
    nullptr,                                // tp_members
    FileWriter_getset,                      // tp_getset
    nullptr,                                // tp_base
    nullptr,                                // tp_dict
    nullptr,                                // tp_descr_get
    nullptr,                                // tp_descr_set
    0,                                      // tp_dictoffset
    (initproc)FileWriter_init,              // tp_init
    PyType_GenericAlloc,                    // tp_alloc
    FileWriter_new,                         // tp_new
};

bool filewriter_init(PyObject *m) {
    if (PyType_Ready(&FileWriterType) < 0) return false;
    if (PyModule_AddType(m, &FileWriterType) < 0) return false;
    return true;
}
