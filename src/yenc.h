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


#include <string_view>
#include <iostream>
#include <charconv>
#include <optional>
#include <deque>
#include <algorithm>

#include "yencode/common.h"
#include "yencode/encoder.h"
#include "yencode/decoder.h"
#include "yencode/crc.h"

/* Constants */
#define YENC_LINESIZE    128
#define YENC_ZERO        0x00
#define YENC_CR          0x0d
#define YENC_LF          0x0a

#define NNTP_CAPABILITIES             101
#define NNTP_ARTICLE                  220
#define NNTP_HEAD                     221
#define NNTP_BODY                     222
#define NNTP_STAT                     223
#define NNTP_MULTILINE                NNTP_BODY, NNTP_ARTICLE, NNTP_HEAD, NNTP_CAPABILITIES

/* The =yend line cannot be crazy long */
#define YENC_MAX_TAIL_BYTES 256

/* Prevent strange yEnc sizes */
#define YENC_MAX_PART_SIZE (UINT64_C(10) * UINT64_C(1024) * UINT64_C(1024))
#define YENC_MAX_FILE_SIZE (UINT64_C(500) * UINT64_C(1024) * UINT64_C(1024) * UINT64_C(1024))

/* Minimum decoder internal buffer size */
#define YENC_MIN_BUFFER_SIZE 1024
/* How much raw data to process each loop */
#define YENC_CHUNK_SIZE (64*1024)

/* Functions */
bool yenc_init(PyObject *);
PyObject* yenc_encode(PyObject *, PyObject*);

typedef struct {
    PyObject_HEAD

	PyObject *decoder; // reference to parent decoder
	PyObject* data;
	Py_ssize_t bytes_decoded;
	Py_ssize_t bytes_read;
	PyObject* lines;
	PyObject* format;
	PyObject* file_name;
	Py_ssize_t file_size;
	Py_ssize_t part;
	Py_ssize_t part_begin;
	Py_ssize_t part_end;
	Py_ssize_t part_size;
	Py_ssize_t end_size;
	Py_ssize_t total;
	std::optional<uint32_t> crc_expected;
	PyObject* message;
	RapidYenc::YencDecoderState state;
	int status_code;
	uint32_t crc;

	bool eof;
	bool body;
	bool has_part;
	bool has_end;
	bool has_emptyline; // for article requests has the empty line separating headers and body been seen
	bool has_baddata; // invalid line lengths for uu decoding; some data lost
} NNTPResponse;

typedef struct {
	PyObject_HEAD

	std::deque<NNTPResponse*> deque; // completed responses
	NNTPResponse* response; // current response being worked on
	char* data; // raw input
	Py_ssize_t size; // size of data
	Py_ssize_t consumed; // left position
	Py_ssize_t position; // right position
} Decoder;

#endif //SABCTOOLS_YENC_H
