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
#define NNTP_ARTICLE     220
#define NNTP_BODY        222
#define NNTP_STAT        223

/* The =yend line cannot be crazy long */
#define YENC_MAX_TAIL_BYTES 256

/* Prevent strange yEnc sizes */
#define YENC_MAX_PART_SIZE 10*1024*1024

/* Functions */
PyObject* yenc_decode(PyObject *, PyObject*);
PyObject* yenc_encode(PyObject *, PyObject*);

/* Decoder Class */
extern PyTypeObject DecoderType;

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
    int status_code;
    unsigned long long bytes_read;

	bool done; // seen \r\n.\r\n
	bool body; // in yenc data
} Decoder;

#endif //SABCTOOLS_YENC_H
