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

// Names any kernel rapidyenc can report, whether an encode/decode one or a CRC32
// one. No platform ifdefs are needed: the RYKERN_* values are disjoint between
// architectures, so a given build only ever reports from its own family.
//
// Matched exactly rather than by range. Ranges would look tidier but the families
// interleave on one number line - PCLMUL (0x340) sits below AVX (0x381), VPCLMUL
// (0x440) between AVX2 and VBMI2 - so a ladder can hand a decode kernel a CRC name.
// An exact switch also means a kernel upstream adds shows up as unknown rather than
// silently borrowing its neighbour's label.
static const char* kernel_name(int level) {
    switch(level) {
        // Encode/decode
        case RYKERN_SSE2:     return "SSE2";
        case RYKERN_SSSE3:    return "SSSE3";
        case RYKERN_AVX:      return "AVX";
        case RYKERN_AVX2:     return "AVX2";
        case RYKERN_VBMI2:    return "AVX512VL+VBMI2";
        case RYKERN_NEON:     return "NEON";
        case RYKERN_RVV:      return "RVV";
        // CRC32
        case RYKERN_PCLMUL:   return "PCLMULQDQ";
        case RYKERN_VPCLMUL:  return "VPCLMULQDQ";
        case RYKERN_ARMCRC:   return "ARMv8-CRC";
        case RYKERN_ARMPMULL: return "ARMv8-CRC+PMULL";
        case RYKERN_ZBC:      return "Zbc";
        // For CRC this means crcutil's assembly on x86 and a slice-by-4 table
        // elsewhere; for encode/decode, plain scalar code.
        case RYKERN_GENERIC:  return "";
    }
    // Only reachable from a BUILD_NATIVE build, which targets the host CPU and
    // reports levels of its own (ISA_LEVEL_AVX3, or a level OR'd with POPCNT and
    // LZCNT) that upstream deliberately does not publish a RYKERN_* value for.
    return "unknown";
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

    if (PyModule_AddStringConstant(m, "version", SABCTOOLS_VERSION) < 0 ||
        PyModule_AddStringConstant(m, "simd", kernel_name(rapidyenc_decode_kernel())) < 0 ||
        PyModule_AddStringConstant(m, "crc_simd", kernel_name(rapidyenc_crc_kernel())) < 0 ||
        // AddObjectRef does not steal a reference, so no manual INCREF/leak handling
        PyModule_AddObjectRef(m, "openssl_linked", openssl_linked() ? Py_True : Py_False) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    return m;
}


