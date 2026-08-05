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

#include "sabctools.h"
#include "yenc.h"
#include "unlocked_ssl.h"
#include "crc32.h"
#include "sparse.h"
#include "utils.h"

/* Function and exception declarations */
PyMODINIT_FUNC PyInit_sabctools(void);

/* Python API requirements */
static PyMethodDef sabctools_methods[] = {
    {
        "yenc_encode",
        yenc_encode,
        METH_O,
        "yenc_encode(input_string)"
    },
    {
        "unlocked_ssl_recv_into",
        unlocked_ssl_recv_into,
        METH_VARARGS,
        "unlocked_ssl_recv_into(ssl_socket, buffer)"
    },
    {
        "crc32_combine",
        crc32_combine,
        METH_VARARGS,
        "crc32_combine(crc1, crc2, length)"
    },
    {
        "crc32_multiply",
        crc32_multiply,
        METH_VARARGS,
        "crc32_multiply(crc1, crc2)"
    },
    {
        "crc32_zero_unpad",
        crc32_zero_unpad,
        METH_VARARGS,
        "crc32_zero_unpad(crc1, length)"
    },
    {
        "crc32_xpown",
        crc32_xpown,
        METH_O,
        "crc32_xpown(n)"
    },
    {
        "crc32_xpow8n",
        crc32_xpow8n,
        METH_O,
        "crc32_xpow8n(n)"
    },
    {
        "sparse",
        sparse,
        METH_VARARGS,
        "sparse(handle, length)"
    },
    {
        "bytearray_malloc",
        bytearray_malloc,
        METH_O,
        "bytearray_malloc(size)"
    },
    {
        "rarfile_rar3_loop",
        rarfile_rar3_loop,
        METH_VARARGS,
        "rarfile_rar3_loop(sha1, seed, base)"
    },
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef sabctools_definition = {
    PyModuleDef_HEAD_INIT,
    "sabctools",
    "Utils written in C for use within SABnzbd.",
    -1,
    sabctools_methods
};

static const char* simd_detected(void) {
    // Which decoder kernel rapidyenc picked for this CPU. The RYKERN_* values are
    // ordered within an architecture and disjoint between architectures, so no
    // platform ifdefs are needed - a given build only ever selects from one family.
    int level = rapidyenc_decode_kernel();

    if(level >= RYKERN_RVV)
        return "RVV";
    if(level >= RYKERN_NEON)
        return "NEON";
    if(level >= RYKERN_VBMI2)
        return "AVX512VL+VBMI2";
    if(level >= RYKERN_AVX2)
        return "AVX2";
    if(level >= RYKERN_AVX)
        return "AVX";
    if(level >= RYKERN_SSSE3)
        return "SSSE3";
    if(level >= RYKERN_SSE2)
        return "SSE2";
    return "";
}

PyMODINIT_FUNC PyInit_sabctools(void) {
    PyObject* m = PyModule_Create(&sabctools_definition);
    if (m == NULL) return NULL;

    // Initialize and add version / SIMD information
    if (!yenc_init(m)) {
        Py_DECREF(m);
        return NULL;
    }
    openssl_init();
    sparse_init();

    PyModule_AddStringConstant(m, "version", SABCTOOLS_VERSION);
    PyModule_AddStringConstant(m, "simd", simd_detected());

    // Add status of linking OpenSSL function
    PyObject *openssl_linked_object = openssl_linked() ? Py_True : Py_False;
    Py_INCREF(openssl_linked_object);
    PyModule_AddObject(m, "openssl_linked", openssl_linked_object);

    return m;
}


