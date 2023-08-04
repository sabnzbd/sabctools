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

#endif //SABCTOOLS_YENC_H
