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
 * Emulate one block of the buggy RAR3 SHA1 corruption.
 *
 * Reads a 64-byte block as 16 big-endian words, runs the SHA1 message
 * schedule expansion (rounds 16..79) over a rolling 16-word window, then
 * writes the resulting words back little-endian, mutating the block in place.
 *
 * @param p pointer to a 64-byte block
 */
static void rar3_corrupt_block(unsigned char *p)
{
    uint32_t w[16];
    uint32_t x;
    int i;

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
}

/**
 * Process one RAR3 string-to-key outer loop (0x4000 inner iterations).
 *
 * Replicates the inner body of rarfile.rar3_s2k together with the buggy
 * rarfile.Rar3Sha1.update behaviour: each iteration feeds `seed` and a 3-byte
 * little-endian counter into the SHA1 `update`, then corrupts `seed` in place
 * for every full 64-byte block (the RAR3 bug), and captures the IV byte
 * (digest()[19]) from the first iteration.
 *
 * `sha1` is a hashlib.sha1() object. `base` is `i << 14` for outer index i, which
 * also lets us recompute the running byte count so the corruption offset stays
 * aligned across the 16 separate invocations that make up a full key derivation.
 *
 * @param self
 * @param args (sha1, seed, base)
 * @return IV byte for this outer loop as a Python int
 */
PyObject* rarfile_rar3_loop(PyObject *self, PyObject *args)
{
    PyObject *sha1;
    PyObject *seed;
    PyObject *update = NULL;
    PyObject *digest = NULL;

    unsigned int base;

    if (!PyArg_ParseTuple(args, "OOI", &sha1, &seed, &base))
        return NULL;

    /* writable view of the seed so we can emulate the in-place corruption */
    Py_buffer seed_view;
    if (PyObject_GetBuffer(seed, &seed_view, PyBUF_WRITABLE) < 0)
        return NULL;

    unsigned char *seed_ptr = static_cast<unsigned char *>(seed_view.buf);
    Py_ssize_t seed_len = seed_view.len;

    /* reusable 3-byte object */
    PyObject *cnt = PyByteArray_FromStringAndSize(NULL, 3);
    if (!cnt) {
        PyBuffer_Release(&seed_view);
        return NULL;
    }

    char *cnt_ptr = PyByteArray_AS_STRING(cnt);
    unsigned char iv = 0;
    PyObject *result;
    PyObject *d;

    /*
     * Total bytes hashed so far. Each inner iteration hashes the seed plus a
     * 3-byte counter, and `base` counts the iterations completed by previous
     * outer loops, so this reproduces Rar3Sha1._nbytes continuously across the
     * 16 separate calls.
     */
    unsigned long long nbytes = static_cast<unsigned long long>(base) * (static_cast<unsigned long long>(seed_len) + 3);

    update = PyObject_GetAttrString(sha1, "update");
    if (!update)
        goto error;
    digest = PyObject_GetAttrString(sha1, "digest");
    if (!digest)
        goto error;

    for (unsigned int j = 0; j < 0x4000; ++j) {
        result = PyObject_CallOneArg(update, seed);
        if (!result)
            goto error;
        Py_DECREF(result);

        /*
         * Mirror Rar3Sha1.update: after feeding the data, corrupt each full
         * 64-byte block that lands inside it (only when len(data) > 64).
         */
        unsigned long bufpos = nbytes & 63;
        nbytes += static_cast<unsigned long long>(seed_len);
        if (seed_len > 64) {
            Py_BEGIN_ALLOW_THREADS
            Py_ssize_t dpos = 64 - static_cast<Py_ssize_t>(bufpos);
            while (dpos + 64 <= seed_len) {
                rar3_corrupt_block(seed_ptr + dpos);
                dpos += 64;
            }
            Py_END_ALLOW_THREADS
        }

        const unsigned int x = base + j;

        cnt_ptr[0] = x & 0xff;
        cnt_ptr[1] = (x >> 8) & 0xff;
        cnt_ptr[2] = (x >> 16) & 0xff;

        result = PyObject_CallOneArg(update, cnt);
        if (!result)
            goto error;
        Py_DECREF(result);

        /* counter is only 3 bytes, so it never triggers the corruption */
        nbytes += 3;

        if (j == 0) {
            d = PyObject_CallNoArgs(digest);
            if (!d)
                goto error;

            if (!PyBytes_Check(d) || PyBytes_GET_SIZE(d) != 20)
            {
                Py_DECREF(d);
                PyErr_SetString(PyExc_RuntimeError, "digest() did not return SHA1 bytes");
                goto error;
            }

            iv = static_cast<unsigned char>(PyBytes_AS_STRING(d)[19]);
            Py_DECREF(d);
        }
    }

    Py_DECREF(cnt);
    Py_DECREF(update);
    Py_DECREF(digest);
    PyBuffer_Release(&seed_view);

    return PyLong_FromUnsignedLong(iv);

    error:
        Py_DECREF(cnt);
        Py_XDECREF(update);
        Py_XDECREF(digest);
        PyBuffer_Release(&seed_view);
    return NULL;
}
