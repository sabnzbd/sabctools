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

#ifndef SABCTOOLS_YENC_H
#define SABCTOOLS_YENC_H

#include <Python.h>
#include "structmember.h"

#include <stdio.h>
#include <string.h>

#include <string_view>
#include <iostream>
#include <charconv>
#include <optional>
#include <algorithm>
#include <iomanip>
#include <string>

#include "yencode/common.h"
#include "yencode/encoder.h"
#include "yencode/decoder.h"
#include "yencode/crc.h"

/* Constants */
#define YENC_LINESIZE    128
#define YENC_ZERO        0x00
#define YENC_CR          0x0d
#define YENC_LF          0x0a

/* The =yend line cannot be crazy long */
#define YENC_MAX_TAIL_BYTES 256

/* Prevent strange yEnc sizes */
#define YENC_MAX_PART_SIZE 10*1024*1024

/* Functions */
PyObject* yenc_decode(PyObject *, PyObject*);
PyObject* yenc_encode(PyObject *, PyObject*);
PyObject* Decoder_Decode(PyObject *, PyObject*);

template <typename T>
static inline bool extract_int(std::string_view line, const char* needle, T& dest) {
    std::string::size_type pos = 0;
    std::string::size_type epos = 0;
    if ((pos = line.find(needle)) != std::string::npos) {
        if ((epos = std::string_view(line.data() + strlen(needle) + pos).find_last_not_of("0123456789")) != std::string::npos) {
            auto [ptr, ec] = std::from_chars(line.data() + strlen(needle) + pos, line.data() + strlen(needle) + pos + epos, dest);
            return ec == std::errc();
        }
    }
    return false;
}

enum EncodingFormat {
    UNKNOWN,
    YENC,
    UU
};

typedef struct {
    PyObject_HEAD
    PyObject* data; // decoded data
    Py_ssize_t data_position; // number of bytes decoded
    EncodingFormat format;
    RapidYenc::YencDecoderState state;
    PyObject* file_name;
    Py_ssize_t file_size;
    Py_ssize_t part;
    Py_ssize_t part_begin;
    Py_ssize_t part_size;
    Py_ssize_t total;
    uint32_t crc;
    std::optional<uint32_t> crc_expected;

	bool done; // seen \r\n.\r\n
	bool body; // in yenc data
} Decoder;

static inline void detect_format(Decoder* instance, std::string_view line) {    
	if (line.rfind("=ybegin ", 0) == 0)
	{
		instance->format = YENC;
        return;
	}

	instance->format = UNKNOWN;
}

static inline void process_yenc_header(Decoder* instance, std::string_view line) {
	if (line.rfind("=ybegin ", 0) != std::string::npos)
	{
        std::string_view remaining = std::string_view(line.data() + 7, line.length() - 7);
        extract_int(remaining, " size=", instance->file_size);
        if (!extract_int(remaining, " part=", instance->part)) {
            // Not multi-part
            instance->body = true;
        }
        extract_int(remaining, " total=", instance->total);

        std::string::size_type pos = 0;
	    if ((pos = remaining.find(" name=")) != std::string::npos) {
            std::string_view name = std::string_view(remaining.data() + 6 + pos, remaining.length() - 6 - pos);
            // Not sure \r\n is necessary lines already have them stripped
            if ((pos = name.find_last_not_of("\r\n\0")) != std::string::npos) {
                Py_DECREF(instance->file_name);
                instance->file_name = PyUnicode_DecodeUTF8(name.data(), pos + 1, NULL);
                if (!instance->file_name) {
                    PyErr_Clear();
                    instance->file_name = PyUnicode_DecodeLatin1(name.data(), pos + 1, NULL);
                }
            }
	    }
	} else if (line.rfind("=ypart ", 0) != std::string::npos) {
        instance->body = true;

        std::string_view remaining = std::string_view(line.data() + 6, line.length() - 6);
        if (extract_int(remaining, " begin=", instance->part_begin) && instance->part_begin > 0) {
            instance->part_begin--;
        }
        if (extract_int(remaining, " end=", instance->part_size) && instance->part_size >= instance->part_begin) {
            instance->part_size -= instance->part_begin;
        }
	} else if (line.rfind("=yend ", 0) != std::string::npos) {
        std::string::size_type pos = 0;
        std::string_view crc32;
	    if ((pos = line.find(" pcrc32=", 5)) != std::string::npos) {
	        crc32 = std::string_view(line.data() + 8 + pos, line.length() - 8 - pos);
	    } else if ((pos = line.find(" crc32=", 5)) != std::string::npos) {
	        crc32 = std::string_view(line.data() + 7 + pos, line.length() - 7 - pos);
	    }
        if (crc32.length() >= 8) {
            // Parse up to 64 bit representations of a CRC32 hash, discarding the upper 32 bits
            // This is necessary become some posts have malformed hashes
            instance->crc_expected = static_cast<uint32_t>(strtoull(crc32.data(), NULL, 16)); // TODO: could use from_chars
        }
	}
}

static void Decoder_dealloc(Decoder* self)
{
    Py_XDECREF(self->data);
    Py_XDECREF(self->file_name);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static int Decoder_getbuffer(PyObject *obj, Py_buffer *view, int flags)
{
    Decoder *self = (Decoder *)obj;

    if (self->data == Py_None) {
        PyErr_SetString(PyExc_BufferError, "No data available");
        return -1;
    }

    // Remove writeable request if present
    flags &= ~PyBUF_WRITABLE;

    // Ensure the underlying object supports the buffer protocol
    if (PyObject_GetBuffer(self->data, view, flags) < 0) {
        PyErr_SetString(PyExc_BufferError, "Underlying data does not support buffer protocol");
        return -1;
    }

    // Explicitly mark buffer as read-only
    view->readonly = 1;

    return 0;
}

static void Decoder_releasebuffer(PyObject *obj, Py_buffer *view)
{
    PyBuffer_Release(view);
}

static PyBufferProcs Decoder_as_buffer = {
    Decoder_getbuffer,
    Decoder_releasebuffer
};

static PyObject* Decoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    Decoder* self = (Decoder*)type->tp_alloc(type, 0);
    if (!self) return NULL;

    self->data = Py_None; Py_INCREF(Py_None);
    self->file_name = Py_None; Py_INCREF(Py_None);

    // Not necessary because the are the zero values
    self->format = UNKNOWN;
    self->state = RapidYenc::YDEC_STATE_CRLF;

    return (PyObject*)self;
}

static PyObject* Decoder_repr(Decoder* self)
{
    return PyUnicode_FromFormat(
        "<Decoder done=%s, file_name=%R, length=%zd>",
        self->done ? "True" : "False",
        self->file_name ? self->file_name : Py_None,
        self->data_position);
}

static PyObject* Decoder_get_data(Decoder* self, void* closure)
{
    return PyMemoryView_FromObject((PyObject*)self);
}

static PyObject* Decoder_get_crc(Decoder* self, void *closure)
{
    if (!self->crc_expected.has_value() || self->crc != self->crc_expected.value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromUnsignedLong(self->crc);
}

static PyObject* Decoder_get_crc_expected(Decoder* self, void *closure)
{
    if (!self->crc_expected.has_value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromUnsignedLong(self->crc_expected.value());
}

static PyMethodDef DecoderMethods[] = {
    {"decode", Decoder_Decode, METH_O, ""},
    {nullptr}
};

static PyMemberDef DecoderMembers[] = {
    {"file_name", T_OBJECT_EX, offsetof(Decoder, file_name), READONLY, ""},
    {"file_size", T_PYSSIZET, offsetof(Decoder, file_size), READONLY, ""},
    {"part_begin", T_PYSSIZET, offsetof(Decoder, part_begin), READONLY, ""},
    {"part_size", T_PYSSIZET, offsetof(Decoder, part_size), READONLY, ""},
    {nullptr}
};

static PyGetSetDef DecoderGetsSets[] = {
    {"data", (getter)Decoder_get_data, NULL, NULL, NULL},
    {"crc", (getter)Decoder_get_crc, NULL, NULL, NULL},
    {"crc_expected", (getter)Decoder_get_crc_expected, NULL, NULL, NULL},
    {NULL}
};

static PyTypeObject DecoderType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "sabctools.Decoder",            // tp_name
    sizeof(Decoder),                // tp_basicsize
    0,                              // tp_itemsize
    (destructor)Decoder_dealloc,    // tp_dealloc
    0,                              // tp_vectorcall_offset
    0,                              // tp_getattr
    0,                              // tp_setattr
    0,                              // tp_as_async
    (reprfunc)Decoder_repr,         // tp_repr
    0,                              // tp_as_number
    0,                              // tp_as_sequence
    0,                              // tp_as_mapping
    0,                              // tp_hash
    0,                              // tp_call
    0,                              // tp_str
    0,                              // tp_getattro
    0,                              // tp_setattro
    &Decoder_as_buffer,             // tp_as_buffer
    Py_TPFLAGS_DEFAULT,             // tp_flags
    PyDoc_STR("Decoder"),           // tp_doc
    0,                              // tp_traverse
    0,                              // tp_clear
    0,                              // tp_richcompare
    0,                              // tp_weaklistoffset
    0,                              // tp_iter
    0,                              // tp_iternext
    DecoderMethods,                 // tp_methods
    DecoderMembers,                 // tp_members
    DecoderGetsSets,                // tp_getset
    0,                              // tp_base
    0,                              // tp_dict
    0,                              // tp_descr_get
    0,                              // tp_descr_set
    0,                              // tp_dictoffset
    0,                              // tp_init
    PyType_GenericAlloc,            // tp_alloc
    (newfunc)Decoder_new,           // tp_new
};

#endif //SABCTOOLS_YENC_H
