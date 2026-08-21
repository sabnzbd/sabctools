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

#ifndef SABCTOOLS_FILEWRITER_H
#define SABCTOOLS_FILEWRITER_H

#include <Python.h>
// shared_mutex and shared_lock come from <shared_mutex>, unique_lock from <mutex>, and
// placement new from <new>. libc++ happens to pull the latter two in transitively;
// libstdc++ does not, so all three are named rather than relied on.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <new>
#include <shared_mutex>

#if defined(_WIN32) || defined(__CYGWIN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Otherwise Windows.h defines min/max macros that break std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winioctl.h>
typedef HANDLE FileHandle;
#define SABCTOOLS_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
#include <unistd.h>
typedef int FileHandle;
#define SABCTOOLS_INVALID_HANDLE (-1)
#endif

/*
 * A file opened for positional writing.
 *
 * The point of owning the descriptor rather than borrowing one is that nothing
 * outside can close it while a write is in flight. Python cannot hand back a
 * descriptor that has since been reused for something else, which is the failure
 * mode that matters here: a stale descriptor does not error, it writes an article
 * into whatever file now holds that number.
 *
 * Concurrency: writes take the lock in shared mode and run at the same time as one
 * another, which is what both platforms allow. pwrite() carries its own offset and
 * touches no shared file position. WriteFile() with an OVERLAPPED offset is
 * positional too, even on a handle that was not opened for overlapped I/O; it
 * disturbs the file pointer, but not the data. Only close() takes the lock
 * exclusively, so it waits for writes to drain rather than pulling the handle out
 * from under them.
 */
typedef struct {
    PyObject_HEAD

    FileHandle handle;
    PyObject *path;
    // Guards handle against close(), not the writes against each other
    std::shared_mutex lock;
} FileWriter;

bool filewriter_init(PyObject *);

extern PyTypeObject FileWriterType;

/*
 * Write a whole buffer at an absolute offset, without touching the Python API.
 *
 * Safe to call with the GIL released, which is the point: the decoder writes from
 * inside its own GIL-free section. Failure is reported through the out-parameters and
 * raised by the caller once the GIL is back.
 *
 * ``error_code`` receives errno on POSIX and the Windows error code on Windows.
 */
Py_ssize_t filewriter_write_raw(FileWriter *writer, const char *buffer, Py_ssize_t length, long long offset,
                                bool *was_closed, unsigned long *error_code);

/* Raise the error reported by filewriter_write_raw. Requires the GIL. */
void filewriter_raise(FileWriter *writer, bool was_closed, unsigned long error_code);

/* Totals for every write through every FileWriter. Requires the GIL. */
PyObject *filewriter_write_stats(PyObject *, PyObject *);

#endif // SABCTOOLS_FILEWRITER_H
