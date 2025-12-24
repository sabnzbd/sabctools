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

#include "yenc.h"

#include "yencode/common.h"
#include "yencode/encoder.h"
#include "yencode/decoder.h"
#include "yencode/crc.h"

/* Global objects */

static PyObject* ENCODING_FORMAT_YENC = nullptr;
static PyObject* ENCODING_FORMAT_UU = nullptr;

/* Function definitions */

/**
 * Check if a value matches any of the provided candidates.
 * 
 * Uses C++17 fold expressions to efficiently test equality against
 * a variadic list of values.
 * 
 * @param v Value to test
 * @param ts Variadic list of candidate values to compare against
 * @return true if v equals any of ts; false otherwise
 * 
 * Example: one_of(status, STATUS_SUCCESS, STATUS_OK, STATUS_COMPLETE)
 */
template<typename T, typename... Ts>
constexpr bool one_of(T v, Ts... ts) {
    return ((v == ts) || ...);
}

/**
 * Check whether all characters in a string view fall within a given ASCII range.
 *
 * Primarily used as a fast validation helper for UUEncoded data where the
 * payload section must consist only of printable UU characters.
 *
 * @param sv Input string view to validate.
 * @param lo Lowest allowed ASCII value (inclusive).
 * @param hi Highest allowed ASCII value (inclusive).
 * @return true if every character in sv is between lo and hi (or sv is empty),
 *         false otherwise.
 */
bool all_in_ascii_range(const std::string_view& sv, char lo, char hi) {
    return std::all_of(sv.begin(), sv.end(), [=](unsigned char c) {
        return c >= lo && c <= hi;
    });
}

/**
 * Check whether a string view contains only padding characters used in UUEncode.
 *
 * In UUEncoded lines, any characters after the data payload should be either
 * spaces or backticks. This helper is used when heuristically recognising
 * multipart UU lines by validating the padding section.
 *
 * @param sv Input string view to validate.
 * @return true if sv is empty or consists only of space (' ') and backtick ('`')
 *         characters, false otherwise.
 */
bool only_space_or_backtick(const std::string_view& sv) {
    return std::all_of(sv.begin(), sv.end(), [](unsigned char c) {
        return c == ' ' || c == '`';
    });
}

/**
 * Lightweight prefix check helper used when parsing protocol and yEnc header lines.
 *
 * Behavior:
 * - C++20 and newer: delegates to std::string_view::starts_with for efficiency and clarity.
 * - Pre-C++20: provides a constexpr fallback that compares characters until the
 *   NUL terminator of the C-string prefix or a mismatch is found.
 *
 * Notes:
 * - The prefix parameter must be a NUL-terminated C string.
 * - This function does not allocate and operates only on the views provided.
 *
 * @param sv     Input string view to test.
 * @param prefix NUL-terminated C-string prefix to match at the start of sv.
 * @return true if sv begins with prefix; false otherwise.
 */
#if defined(__cplusplus) && __cplusplus >= 202002L
constexpr bool starts_with(std::string_view sv, const char* prefix) {
    return sv.starts_with(prefix);
}
#else
constexpr bool starts_with(std::string_view sv, const char* prefix) {
    size_t i = 0;
    for (; prefix[i] != '\0'; ++i) {
        if (i >= sv.size() || sv[i] != prefix[i]) return false;
    }
    return true;
}
#endif

/**
 * Extract an integer from a yEnc header line after a specified needle pattern.
 * 
 * @param line The string view to search within
 * @param needle The pattern to search for (e.g., " size="). If empty, starts from beginning
 * @param dest Output parameter to store the extracted integer
 * @return true if extraction succeeded, false otherwise
 * 
 * Example: extract_int("line=123 size=456", " size=", dest) extracts 456
 */
template <typename T>
static inline bool extract_int(std::string_view line, const char* needle, T& dest) {
    std::size_t start = 0;

    // find needle, or start from beginning if empty
    if (needle && *needle) {
        std::size_t pos = line.find(needle);
        if (pos == std::string_view::npos) return false;
        start = pos + strlen(needle);
    }

    // slice the line from start
    line.remove_prefix(start);

    if (line.empty() || line.front() < '0' || line.front() > '9') return false;

    // std::from_chars will automatically stop at first non-digit
    auto [ptr, ec] = std::from_chars(line.data(), line.data() + line.size(), dest);

    return ec == std::errc();
}

/**
 * Parse up to 64 bit representations of a CRC32 hash, discarding the upper 32 bits.
 * This is necessary because some posts have malformed hashes that exceed 32 bits.
 * 
 * @param crc32 String view containing hexadecimal CRC value
 * @return Optional uint32_t with the parsed CRC, or nullopt if parsing fails
 */
std::optional<uint32_t> parse_crc32(std::string_view crc32) {
    if (crc32.empty()) return 0;

    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(crc32.data(), crc32.data() + crc32.size(), value, 16);

    // Fail if conversion failed
    if (ec != std::errc{}) {
        return std::nullopt;
    }

    return static_cast<uint32_t>(value); // Discard upper 32 bits
}

/**
 * Decode a string to Python Unicode with encoding fallback.
 * 
 * Try UTF-8 first, fall back to Latin-1 for legacy encodings.
 * 
 * This handles filenames and other metadata in yEnc headers that may use
 * non-UTF-8 encodings from older Usenet posts. Latin-1 is used as fallback
 * because it can decode any byte sequence without error.
 * 
 * @param line String view containing the text to decode
 * @return PyObject* Unicode string, or nullptr if line is empty or all decoding attempts fail
 * 
 * Note: Clears Python errors internally when decoding fails.
 */
static PyObject* decode_utf8_with_fallback(std::string_view line) {
    if (line.empty()) return nullptr; // Ignore empty lines

    auto try_decode = [&](auto decoder, const char* errors = nullptr) -> PyObject* {
        PyObject* result = decoder(line.data(), line.size(), errors);
        if (!result) PyErr_Clear();
        return result;
    };

    PyObject* py_str = try_decode(PyUnicode_DecodeUTF8);
    if (!py_str) {
        py_str = try_decode(PyUnicode_DecodeLatin1, "replace");
    }

    return py_str;
}

/**
 * Decode a single UUEncoded character to its 6-bit value.
 *
 * @param c The UUEncoded character (typically in range ' ' to '_', or '`').
 * @return The decoded 6-bit value (0–63), masked to ensure it stays within range.
 */
constexpr unsigned char NNTPResponse_decode_uu_char(const unsigned char c) noexcept {
    return (c == '`') ? 0 : ((c - ' ') & 0x3F);
}

/**
 * Decode UU line length for broken uuencoders by Fredrik Lundh

 * @param c The UUEncoded character (typically in range ' ' to '_', or '`').
 * @return The decoded 6-bit value (0–63), masked to ensure it stays within range.
 */
constexpr unsigned char NNTPResponse_decode_uu_char_workaround(const unsigned char c) noexcept {
    return (((static_cast<unsigned char>(c) - 32) & 63) * 4 + 5) / 3;
}

/**
 * Detect the encoding format of the article by examining a line.
 * Detects yEnc format (lines starting with "=ybegin ") and UUEncode format
 * (60/61 character lines starting with 'M', or "begin " header with octal permissions).
 * 
 * @param instance The Decoder instance to update
 * @param line The line to examine for format detection
 */
static inline void NNTPResponse_detect_format(NNTPResponse* instance, std::string_view line) {
    if (!one_of(instance->status_code, NNTP_BODY, NNTP_ARTICLE)) {
        return;
    }

    if (line.empty()) {
        instance->has_emptyline = true;
        return;
    }

    // YEnc detection
    if (starts_with(line, "=ybegin "))
    {
        Py_XDECREF(instance->format);
        instance->format = ENCODING_FORMAT_YENC;
        Py_INCREF(ENCODING_FORMAT_YENC);
        return;
    }

    // UUEncode detection: 60 or 61 chars, starts with 'M'
    if ((line.size() == 60 || line.size() == 61) && line.front() == 'M') {
        Py_XDECREF(instance->format);
        instance->format = ENCODING_FORMAT_UU;
        Py_INCREF(ENCODING_FORMAT_UU);
        return;
    }

    // UUEncode alternative header form: "begin "
    if (starts_with(line, "begin ")) {
        line.remove_prefix(6);

        // Skip leading spaces
        while (!line.empty() && isspace(static_cast<unsigned char>(line.front())))
            line.remove_prefix(1);

        // Extract the next token (permission part)
        size_t perm_len = 0;
        while (perm_len < line.size() && !isspace(static_cast<unsigned char>(line[perm_len])))
            ++perm_len;

        if (perm_len == 0)
            return; // No permission digits found

        const std::string_view perms = line.substr(0, perm_len);

        // Check all characters are between '0' and '7'
        bool all_valid = true;
        for (const char c : perms) {
            if (c < '0' || c > '7') {
                all_valid = false;
                break;
            }
        }

        if (all_valid) {
            Py_XDECREF(instance->format);
            instance->format = ENCODING_FORMAT_UU;
            Py_INCREF(ENCODING_FORMAT_UU);
        }
        return;
    }

    // Remove dot stuffing
    if (starts_with(line, "..")) {
        line.remove_prefix(1);
    }

    // Multipart UU with a short final part
    if (line.size() <= 1)
        return;

    // For Article responses only consider after the headers
    if (!(instance->status_code == NNTP_BODY || (instance->status_code == NNTP_ARTICLE && instance->has_emptyline)))
        return;

    const unsigned char first = line.front();
    const size_t n = line.size();

    for (const size_t len : {
            static_cast<size_t>(NNTPResponse_decode_uu_char_workaround(first)),
            static_cast<size_t>(NNTPResponse_decode_uu_char(first))
        })
    {
        if (n < len)
            continue;

        std::string_view body = line.substr(1, len - 1);
        std::string_view padding = line.substr(len);

        if (!all_in_ascii_range(body, 32, 96)) continue;
        if (!only_space_or_backtick(padding)) continue;

        // Probably UU
        Py_XDECREF(instance->format);
        instance->format = ENCODING_FORMAT_UU;
        Py_INCREF(ENCODING_FORMAT_UU);
        instance->body = true;
        return;
    }
}

/**
 * Process yEnc header lines (=ybegin, =ypart, =yend) and extract metadata.
 * 
 * @param instance The Decoder instance to update with extracted metadata
 * @param line The header line to process
 * 
 * Handles three types of yEnc headers:
 * - =ybegin: Extracts file size, part number, total parts, and filename
 * - =ypart: Marks start of body and extracts part begin/end positions (converts to 0-based)
 * - =yend: Extracts CRC32 (pcrc32 for multi-part, crc32 for single file)
 */
static inline void NNTPResponse_process_yenc_header(NNTPResponse* instance, std::string_view line) {
    if (starts_with(line, "=ybegin ")) {
        line.remove_prefix(7);
        extract_int(line, " size=", instance->file_size);
        if (instance->file_size > YENC_MAX_FILE_SIZE) {
            instance->file_size = 0;
        }
        if (!extract_int(line, " part=", instance->part)) {
            // Not multi-part, so body starts immediately after =ybegin
            instance->body = true;
        }
        extract_int(line, " total=", instance->total);

        std::string::size_type pos;
	    if ((pos = line.find(" name=")) != std::string::npos) {
	        line.remove_prefix(pos + 6);
            // Strip trailing whitespace/null from filename
            if ((pos = line.find_last_not_of('\0')) != std::string::npos) {
                instance->file_name = decode_utf8_with_fallback(line.substr(0, pos + 1));
            }
	    }
    } else if (starts_with(line, "=ypart ")) {
        // =ypart signals start of body data in multi-part files
        instance->has_part = true;
        instance->body = true;
        line.remove_prefix(6);
        extract_int(line, " begin=", instance->part_begin);
        extract_int(line, " end=", instance->part_end);
        // Get the size and sanity check the values
        instance->part_size = instance->part_end - instance->part_begin + 1;
        if (instance->part_size > 0 &&
            instance->part_size <= YENC_MAX_PART_SIZE &&
            instance->part_end <= instance->file_size ) {
            // Convert from 1-based to 0-based indexing
            instance->part_begin--;
        } else {
            // Reset values; invalid metadata
            instance->part_begin = 0;
            instance->part_end = 0;
            instance->part_size = 0;
        }
    } else if (starts_with(line, "=yend ")) {
        instance->has_end = true;
        line.remove_prefix(5);
        std::string_view crc32;
        bool crc32_found = false;
        // Multi-part files use pcrc32 (part CRC), single files use crc32
        constexpr std::string_view prefixes[] = { " pcrc32=", " crc32=" };

        for (const auto& prefix : prefixes) {
            auto pos = line.find(prefix, 5);
            if (pos != std::string::npos) {
                crc32 = line.substr(pos + prefix.size());
                crc32_found = true;
                break;
            }
        }

        if (crc32_found) {
            instance->crc_expected = parse_crc32(crc32);
        }

        extract_int(line, " size=", instance->end_size);
	}
}

/**
 * Append a parsed line to the Decoder's collected header/response lines.
 *
 * Used while the encoding format is still UNKNOWN to retain NNTP response
 * lines for diagnostics or higher-level consumers. Lazily allocates the
 * Python list on first use and ignores empty lines.
 *
 * @param instance Decoder instance whose lines list will be appended to.
 * @param line     Line contents without the trailing CRLF.
 */
static void NNTPResponse_append_line(NNTPResponse* instance, std::string_view line) {
    auto py_str = decode_utf8_with_fallback(line);
    if (!py_str) return;

    if (instance->lines == nullptr) {
        instance->lines = PyList_New(0);
        Py_INCREF(instance->lines);
    }

    PyList_Append(instance->lines, py_str);
    Py_DECREF(py_str);
}

/**
 * Destructor for Decoder objects. Releases Python object references and frees memory.
 * 
 * @param self The Decoder instance to deallocate
 */
static void NNTPResponse_dealloc(NNTPResponse* self)
{
    Py_XDECREF(self->decoder);
    Py_XDECREF(self->data);
    Py_XDECREF(self->file_name);
    Py_XDECREF(self->lines);
    Py_XDECREF(self->format);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

/**
 * Property getter for the 'data' attribute. Returns a memoryview of the decoded data.
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return memoryview object providing access to decoded data
 */
static PyObject* NNTPResponse_get_data(NNTPResponse* self, void* closure)
{
    if (!self->eof || !self->bytes_decoded || self->data == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->data);
    return self->data;
}

/**
 * Property getter for the 'file_name' attribute. Returns the filename from yEnc headers.
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return Unicode string with filename, or None if not found
 */
static PyObject* NNTPResponse_get_file_name(NNTPResponse* self, void *closure)
{
    if (self->file_name == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->file_name);
    return self->file_name;
}

/**
 * Property getter for the 'crc' attribute. Returns calculated CRC only if it matches expected.
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return CRC32 value if valid and matches expected, otherwise None
 */
static PyObject* NNTPResponse_get_crc(NNTPResponse* self, void *closure)
{
    if (self->format == nullptr) {
        Py_RETURN_NONE;
    }

    if (self->format == ENCODING_FORMAT_YENC && (!self->crc_expected.has_value() || self->crc != self->crc_expected.value())) {
        Py_RETURN_NONE;
    }

    return PyLong_FromUnsignedLong(self->crc);
}

/**
 * Property getter for the 'crc_expected' attribute. Returns the CRC from yEnc footer.
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return Expected CRC32 value from =yend line, or None if not found
 */
static PyObject* NNTPResponse_get_crc_expected(NNTPResponse* self, void *closure)
{
    if (!self->crc_expected.has_value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromUnsignedLong(self->crc_expected.value());
}

/**
 * Property getter for the 'lines' attribute. Returns NNTP/header lines captured
 * before the encoding format was determined.
 *
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return List of Unicode strings, without \r\n at the end
 */
static PyObject* NNTPResponse_get_lines(NNTPResponse* self, void *closure)
{
    if (self->lines == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->lines);
    return self->lines;
}

/**
 * Property getter for the 'format' attribute. Indicates which encoding format
 * was detected for this NNTP response.
 *
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return EncodingFormat IntEnum value for the detected format, or None if
 *         the format has not yet been determined
 */
static PyObject* NNTPResponse_get_format(NNTPResponse* self, void *closure)
{
    if (self->format == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->format);
    return self->format;
}

/**
 * Decode yEnc-encoded body data in streaming fashion.
 * This is the core decoding function that processes encoded data incrementally.
 *
 * Data from ``buf`` is consumed in fixed-size chunks of ``YENC_CHUNK_SIZE`` bytes.
 * The internal output bytearray is allocated once on first use and then grown
 * only when required, which keeps reallocations to a minimum and avoids large
 * temporary buffers.
 * 
 * @param instance The Decoder instance with state to maintain across calls
 * @param buf Input buffer containing encoded data
 * @param buf_len Total length of input buffer
 * @param read Input/output parameter tracking position in buffer
 * @return true on success, false on error
 * 
 * Key behaviors:
 * - Allocates output buffer on first call based on expected size from headers
 *   and processes input in ``YENC_CHUNK_SIZE`` blocks to limit working set
 * - Uses RapidYenc SIMD decoder with state machine to handle partial data
 * - Updates CRC incrementally as data is decoded
 * - Detects end conditions (control line, article terminator) and adjusts read position
 * - Releases GIL during decoding for parallel processing
 */
static bool NNTPResponse_decode_yenc(NNTPResponse *instance, const char *buf, const Py_ssize_t buf_len, Py_ssize_t &read) {
    // Already at the end of input
    if (read >= buf_len) return true;

    constexpr Py_ssize_t CHUNK = YENC_CHUNK_SIZE;

    if (instance->data == nullptr) {
        // Allocate output buffer on first decode call
        // Use size from headers, capped at YENC_MAX_PART_SIZE for safety
        Py_ssize_t base = instance->part_size > 0 ? instance->part_size : instance->file_size;
        Py_ssize_t expected = base + 64;  // small margin to see the end of yEnc data
        // Round up to next multiple of CHUNK
        expected = ((expected + CHUNK - 1) / CHUNK) * CHUNK;
        // Add an extra CHUNK so we should never need to resize
        expected += CHUNK;

        if (expected < YENC_MIN_BUFFER_SIZE)
            expected = YENC_MIN_BUFFER_SIZE;
        if (expected > YENC_MAX_PART_SIZE)
            expected = YENC_MAX_PART_SIZE;

        instance->data = PyByteArray_FromStringAndSize(nullptr, expected);
        if (!instance->data) {
            return false;
        }
    }

    // Pin buffer once per call
    Py_buffer dst_buf;
    if (PyObject_GetBuffer(instance->data, &dst_buf, PyBUF_WRITABLE) < 0)
        return false;

    char *data_ptr = static_cast<char*>(dst_buf.buf);

    RapidYenc::YencDecoderEnd end = RapidYenc::YDEC_END_NONE;

    // Main decode loop
    while (read < buf_len) {
        Py_ssize_t chunk_in = std::min(CHUNK, buf_len - read);

        // Ensure buffer has enough space
        Py_ssize_t needed = instance->bytes_decoded + chunk_in;
        Py_ssize_t current = PyByteArray_GET_SIZE(instance->data);

        if (needed > current) {
            if (needed > YENC_MAX_PART_SIZE) {
                PyBuffer_Release(&dst_buf);
                PyErr_SetString(PyExc_BufferError, "Maximum data buffer size exceeded");
                return false;
            }

            // Release buffer to resize
            PyBuffer_Release(&dst_buf);
            if (PyByteArray_Resize(instance->data, needed) == -1) {
                PyBuffer_Release(&dst_buf);
                return false;
            }

            // Re-pin buffer after resize
            if (PyObject_GetBuffer(instance->data, &dst_buf, PyBUF_WRITABLE) < 0)
                return false;
            data_ptr = static_cast<char*>(dst_buf.buf);
        }

        const char *src = buf + read;
        char *dst = data_ptr + instance->bytes_decoded;
        char *dst_start = dst;

        Py_ssize_t consumed = 0, produced = 0;

        // Release GIL during CPU-intensive decode operation for better parallelism
        Py_BEGIN_ALLOW_THREADS;

        end = RapidYenc::decode_end(
            reinterpret_cast<const void **>(&src),
            reinterpret_cast<void **>(&dst),
            chunk_in,
            &instance->state
        );

        consumed = src - (buf + read);
        produced = dst - dst_start;

        if (produced > 0) {
            instance->crc = RapidYenc::crc32(dst_start, produced, instance->crc);
        }

        Py_END_ALLOW_THREADS;

        read += consumed;
        instance->bytes_decoded += produced;

        if (end != RapidYenc::YDEC_END_NONE || (consumed == 0 && produced == 0))
            break;
    }

    PyBuffer_Release(&dst_buf);

    // Handle different end conditions from the decoder
    switch (end) {
        case RapidYenc::YDEC_END_NONE:
            if (instance->state == RapidYenc::YDEC_STATE_CRLFEQ) {
                // Special case: found "\r\n=" but no more data - might be start of =yend
                instance->state = RapidYenc::YDEC_STATE_CRLF;
                read -= 1; // Back up to allow =yend detection
            }
            break;
        case RapidYenc::YDEC_END_CONTROL:
            // Found "\r\n=y" - likely =yend line, exit body mode
            instance->body = false;
            read -= 2; // Back up to include "=y" for header processing
            break;
        case RapidYenc::YDEC_END_ARTICLE:
            // Found ".\r\n" - NNTP article terminator, exit body mode
            instance->body = false;
            read -= 3; // Back up to include ".\r\n" for terminator detection
            break;
    }

    return true;
}

/**
 * Decode a single UUEncoded line and update the Decoder's state and buffer.
 *
 * Adapted (with modifications assisted by AI) from:
 *   UUDECODE - a Win32 utility to uudecode single files
 *   Copyright (C) 1998 Clem Dye
 *   Source: http://www.bastet.com/uue.zip
 *
 * Behavior:
 * - Header detection: on lines starting with "begin ", extracts filename and enters body mode.
 * - Heuristic body detection: if no header yet, treats 60/61-char lines starting with 'M' as body.
 * - End detection: lines starting with "end " or "`" terminate UU decoding.
 * - Body decoding: uses the leading length character followed by groups of 4 printable
 *   characters to reconstruct up to 3 bytes per group, appending to the bytearray.
 * - Storage: lazily allocates/resizes the output bytearray and advances data_position.
 *
 * Notes:
 * - CRC/permissions from UU are not validated here.
 * - Expects a single logical line without trailing CRLF.
 *
 * @param instance The Decoder instance to update.
 * @param line     The current input line (without CRLF).
 * @return true on success, false on allocation/resize failure.
 */
static bool NNTPResponse_decode_uu(NNTPResponse* instance, std::string_view line)
{
    // Allocate or resize bytearray
    if (!instance->data) {
        instance->data = PyByteArray_FromStringAndSize(nullptr, line.size());
        if (!instance->data) {
            return false;
        }
    } else if (PyByteArray_Resize(instance->data, instance->bytes_decoded + line.size()) == -1) {
        return false;
    }

    // Detect 'begin' line and extract filename
    if (!instance->body) {
        if (starts_with(line, "begin ")) {
            line.remove_prefix(6);

            auto trim_while = [](std::string_view& sv, auto pred) {
                while (!sv.empty() && pred(sv.front())) sv.remove_prefix(1);
            };

            trim_while(line, [](const unsigned char c){ return std::isspace(c); }); // skip leading spaces
            trim_while(line, [](const unsigned char c){ return std::isdigit(c); }); // skip permissions
            trim_while(line, [](const unsigned char c){ return std::isspace(c); }); // skip spaces after permissions

            // The rest of the line is the filename
            instance->file_name = decode_utf8_with_fallback(line);

            instance->body = true;
            return true;
        }

        // Begin missing but looks like UUEncode: 60 or 61 chars, starts with 'M'
        if ((line.size() == 60 || line.size() == 61) && line.front() == 'M') {
            instance->body = true;
        }
    }

    // Detect 'end' line
    if (instance->body && one_of(line, "`", "end")) {
        instance->body = false;
        instance->file_size = instance->bytes_decoded;
        return true;
    }

    // Decode body lines
    if (instance->body) {
        // Ignore junk
        if (line.empty() || line == "-- " || starts_with(line, "Posted via ")) {
            return true;
        }

        // Remove dot stuffing
        if (starts_with(line, "..")) {
            line.remove_prefix(1);
        }

        std::size_t effLen = NNTPResponse_decode_uu_char(line.front());
        if (effLen > line.size() - 1) {
            // Workaround for broken uuencoders by Fredrik Lundh
            effLen = NNTPResponse_decode_uu_char_workaround(line.front());
            if (effLen > line.size() - 1) {
                // Bad line
                instance->has_baddata = true;
                return true;
            }
        }

        line.remove_prefix(1); // skip length byte
        char* dst = PyByteArray_AsString(instance->data) + instance->bytes_decoded;
        char* dst_start = dst;
        auto it = line.begin();
        const auto end = line.end();

        while (effLen > 0 && std::distance(it, end) >= 4) {
            const auto chunk = std::min(effLen, static_cast<std::size_t>(3));
            const unsigned char c0 = NNTPResponse_decode_uu_char(*it++);
            const unsigned char c1 = NNTPResponse_decode_uu_char(*it++);
            unsigned char c2 = 0;

            *dst++ = static_cast<char>(c0 << 2 | c1 >> 4);

            if (chunk > 1) {
                c2 = NNTPResponse_decode_uu_char(*it++);
                *dst++ = static_cast<char>(c1 << 4 | c2 >> 2);
            }

            if (chunk > 2) {
                const unsigned char c3 = NNTPResponse_decode_uu_char(*it++);
                *dst++ = static_cast<char>(c2 << 6 | c3);
            }

            effLen -= 3;
        }

        Py_ssize_t produced = dst - dst_start;
        instance->bytes_decoded += produced;
        if (produced > 0) {
            instance->crc = RapidYenc::crc32(dst_start, produced, instance->crc);
        }
    }

    return true;
}

/**
 * Extract the next complete line ending with \r\n from the buffer.
 * Used for parsing NNTP and yEnc header/footer lines.
 * 
 * @param buf Input buffer
 * @param buf_len Length of input buffer
 * @param read Input/output parameter tracking current position in buffer
 * @param line Output parameter receiving the line content (without \r\n)
 * @return true if a complete line was found, false if incomplete
 */
bool next_crlf_line(const char* buf, std::size_t buf_len, Py_ssize_t &read, std::string_view &line) {
    if (read + 1 >= static_cast<Py_ssize_t>(buf_len)) return false; // Not enough room for "\r\n"

    const char* start = buf + read;
    const char* end = buf + buf_len;
    const char* line_end = start;

    // Scan for "\r\n"
    while (line_end + 1 < end && !(line_end[0] == '\r' && line_end[1] == '\n')) {
        ++line_end;
    }

    if (line_end + 1 >= end) return false; // No complete "\r\n"

    line = std::string_view(start, line_end - start);
    read = line_end - buf + 2; // Total bytes consumed including \r\n
    return true;
}

/**
 * Main buffer processing function for the streaming decoder.
 * Handles state transitions between line-based parsing and body decoding.
 *
 * @param instance The NNTPResponse instance maintaining state across calls
 * @param buf The buffer to process
 * @param buf_len The length of data in buf to process
 * @return Number of bytes consumed from buffer, or -1 on error
 *
 * Processing flow:
 * 1. If already in body mode, decode yEnc data immediately
 * 2. Otherwise, parse line-by-line:
 *    - Detect NNTP article terminator (".\r\n")
 *    - Parse NNTP status code from first line
 *    - Detect encoding format (yEnc)
 *    - Process yEnc headers (=ybegin, =ypart, =yend)
 *    - Switch to body decoding when =ypart is encountered
 * 3. Return number of bytes processed (may be less than buffer length)
 */
static Py_ssize_t NNTPResponse_decode_buffer(NNTPResponse *instance, const char* buf, const Py_ssize_t buf_len) {
    Py_ssize_t read = 0;

    // Resume body decoding if we were in the middle of it
    if (instance->body && instance->format == ENCODING_FORMAT_YENC) {
        if (!NNTPResponse_decode_yenc(instance, buf, buf_len, read)) return -1;
        if (instance->body) return read;  // Still in body, need more data
    }

    // Parse headers and footers line-by-line
    std::string_view line;
    while (next_crlf_line(buf, buf_len, read, line)) {
        if (line == ".") {
            // NNTP article terminator
            instance->eof = true;
            return read;
        }

        if (instance->format == nullptr) {
            if (!instance->status_code && line.length() >= 3) {
                // Store the full command response line
                instance->message = decode_utf8_with_fallback(line);
                // First line should be NNTP status code (220, 222, 223, etc.)
                if (!extract_int(line, "", instance->status_code)
                    || !one_of(instance->status_code, NNTP_MULTILINE)) {
                    // Single-line response (not ARTICLE/BODY), we're done
                    instance->eof = true;
                    break;
                }
                continue;
            }

            NNTPResponse_detect_format(instance, line);
        }

        if (instance->format == nullptr) {
            // Format is still unknown so record lines
            NNTPResponse_append_line(instance, line);
        } else if (instance->format == ENCODING_FORMAT_YENC) {
            NNTPResponse_process_yenc_header(instance, line);
            if (instance->body) {
                // =ypart was encountered, switch to body decoding
                if (!NNTPResponse_decode_yenc(instance, buf, buf_len, read)) return -1;
                if (instance->body) return read;  // Still decoding, need more data
            }
        } else if (instance->format == ENCODING_FORMAT_UU) {
            if (!NNTPResponse_decode_uu(instance, line)) return -1;
        }
    }

    return read;
}

static PyObject* NNTPResponse_iternext(NNTPResponse *instance)
{
    const auto deque_obj = reinterpret_cast<Decoder*>(instance->decoder);
    if (!instance->decoder || deque_obj->deque.empty())
        return NULL;  // StopIteration

    NNTPResponse* item = deque_obj->deque.front();
    deque_obj->deque.pop_front();
    Py_INCREF(item);  // Return a new reference

    return reinterpret_cast<PyObject*>(item);
}

static inline size_t YENC_MAX_SIZE(size_t len, size_t line_size) {
    size_t ret = len * 2    /* all characters escaped */
        + 2 /* allocation for offset and that a newline may occur early */
#if !defined(YENC_DISABLE_AVX256)
        + 64 /* allocation for YMM overflowing */
#else
        + 32 /* allocation for XMM overflowing */
#endif
    ;
    /* add newlines, considering the possibility of all chars escaped */
    if(line_size == 128) // optimize common case
        return ret + 2 * (len >> 6);
    return ret + 2 * ((len*2) / line_size);
}

PyObject* yenc_encode(PyObject* self, PyObject* Py_input_string)
{
    (void)self;
    PyObject *Py_output_string;
    PyObject *retval = NULL;

    char *input_buffer = NULL;
    char *output_buffer = NULL;
    size_t input_len = 0;
    size_t output_len = 0;
    uint32_t crc;

    // Verify the input is a bytes string
    if(!PyBytes_Check(Py_input_string)) {
        PyErr_SetString(PyExc_TypeError, "Expected bytes");
        return NULL;
    }

    // Initialize buffers and CRC's
    input_len = PyBytes_Size(Py_input_string);
    input_buffer = (char *)PyBytes_AsString(Py_input_string);
    output_buffer = (char *)malloc(YENC_MAX_SIZE(input_len, YENC_LINESIZE));
    if(!output_buffer)
        return PyErr_NoMemory();

    // Free GIL, in case it helps
    Py_BEGIN_ALLOW_THREADS;

    // Encode result
    int column = 0;
    output_len = RapidYenc::encode(YENC_LINESIZE, &column, input_buffer, output_buffer, input_len, 1);
    crc = RapidYenc::crc32(input_buffer, input_len, 0);

    // Restore GIL so we can build Python strings
    Py_END_ALLOW_THREADS;

    // Build output string
    Py_output_string = PyBytes_FromStringAndSize((char *)output_buffer, output_len);
    if(Py_output_string)
        retval = Py_BuildValue("(S,L)", Py_output_string, (long long)crc);

    Py_XDECREF(Py_output_string);
    free(output_buffer);
    return retval;
}

/**
 * Initializer for NNTPResponse instances used by Decoder.
 *
 * Binds the NNTPResponse to its owning Decoder and resets all parsing
 * and decoding fields to a consistent default state.
 *
 * @param instance Newly allocated NNTPResponse object to initialize
 * @param parent   Decoder object that owns this response and will be
 *                 referenced for iteration
 */
static void NNTPResponse_init(NNTPResponse* instance, PyObject* parent) {
    // Iterator requires a reference back
    instance->decoder = parent;
    Py_INCREF(parent);

    // Initialise all members
    instance->data = nullptr;
    instance->lines = nullptr;
    instance->format = nullptr;
    instance->file_name = nullptr;
    instance->message = nullptr;
    instance->bytes_decoded = 0;
    instance->bytes_read = 0;
    instance->file_size = 0;
    instance->part = 0;
    instance->part_begin = 0;
    instance->part_end = 0;
    instance->part_size = 0;
    instance->end_size = 0;
    instance->total = 0;
    instance->crc = 0;
    instance->status_code = 0;
    instance->crc_expected = std::nullopt;
    instance->state = RapidYenc::YDEC_STATE_CRLF;
    instance->eof = false;
    instance->body = false;
    instance->has_part = false;
    instance->has_end = false;
    instance->has_emptyline = false;
    instance->has_baddata = false;
}

/**
 * String representation of Decoder for debugging.
 * 
 * @param self The Decoder instance
 * @return Unicode string with decoder state summary
 */
static PyObject* NNTPResponse_repr(NNTPResponse* self)
{
    return PyUnicode_FromFormat(
        "<NNTPResponse: status_code=%d, message=%R, file_name=%R, length=%zd>",
        self->status_code,
        self->message ? self->message : Py_None,
        self->file_name ? self->file_name : Py_None,
        self->bytes_decoded);
}

static PyMemberDef NNTPResponse_members[] = {
    {"status_code", T_INT, offsetof(NNTPResponse, status_code), READONLY, ""},
    {"message", T_OBJECT_EX, offsetof(NNTPResponse, message), READONLY, ""},
    {"file_size", T_PYSSIZET, offsetof(NNTPResponse, file_size), READONLY, ""},
    {"part_begin", T_PYSSIZET, offsetof(NNTPResponse, part_begin), READONLY, ""},
    {"part_end", T_PYSSIZET, offsetof(NNTPResponse, part_end), READONLY, ""},
    {"part_size", T_PYSSIZET, offsetof(NNTPResponse, part_size), READONLY, ""},
    {"end_size", T_PYSSIZET, offsetof(NNTPResponse, end_size), READONLY, ""},
    {"bytes_read", T_PYSSIZET, offsetof(NNTPResponse, bytes_read), READONLY, ""},
    {"bytes_decoded", T_PYSSIZET, offsetof(NNTPResponse, bytes_decoded), READONLY, ""},
    {"baddata", T_BOOL, offsetof(NNTPResponse, has_baddata), READONLY, ""},
    {nullptr, 0, 0, 0, nullptr}
};

static PyGetSetDef NNTPResponse_gets_sets[] = {
    {"data", (getter)NNTPResponse_get_data, NULL, NULL, NULL},
    {"file_name", (getter)NNTPResponse_get_file_name, NULL, NULL, NULL},
    {"lines", (getter)NNTPResponse_get_lines, NULL, NULL, NULL},
    {"crc", (getter)NNTPResponse_get_crc, NULL, NULL, NULL},
    {"crc_expected", (getter)NNTPResponse_get_crc_expected, NULL, NULL, NULL},
    {"format", (getter)NNTPResponse_get_format, NULL, NULL, NULL},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

PyTypeObject NNTPResponseType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "sabctools.NNTPResponse",            // tp_name
    sizeof(NNTPResponse),                // tp_basicsize
    0,                                   // tp_itemsize
    (destructor)NNTPResponse_dealloc,    // tp_dealloc
    0,                                   // tp_vectorcall_offset
    nullptr,                             // tp_getattr
    nullptr,                             // tp_setattr
    nullptr,                             // tp_as_async
    (reprfunc)NNTPResponse_repr,         // tp_repr
    nullptr,                             // tp_as_number
    nullptr,                             // tp_as_sequence
    nullptr,                             // tp_as_mapping
    nullptr,                             // tp_hash
    nullptr,                             // tp_call
    nullptr,                             // tp_str
    nullptr,                             // tp_getattro
    nullptr,                             // tp_setattro
    nullptr,                             // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                  // tp_flags
    PyDoc_STR("NNTPResponse"),           // tp_doc
    nullptr,                             // tp_traverse
    nullptr,                             // tp_clear
    nullptr,                             // tp_richcompare
    0,                                   // tp_weaklistoffset
    nullptr,                             // tp_iter
    (iternextfunc)NNTPResponse_iternext, // tp_iternext
    nullptr,                             // tp_methods
    NNTPResponse_members,                // tp_members
    NNTPResponse_gets_sets,              // tp_getset
};

/**
 * Buffer protocol getbuffer implementation for Decoder.
 *
 * Exposes the Decoder's internal storage as a writable, contiguous
 * bytes buffer so callers can fill it directly (e.g. via memoryviews
 * or I/O APIs). The buffer always starts at the beginning of the
 * internal array and spans ``self->size`` bytes.
 *
 * @param self Decoder instance whose internal buffer is being exported
 * @param view Filled-in Py_buffer describing the exported memory
 * @param flags Standard buffer protocol request flags
 * @return 0 on success, or -1 with an exception set on error
 */
static int Decoder_getbuffer(Decoder* self, Py_buffer *view, int flags)
{
    return PyBuffer_FillInfo(
        view,
        reinterpret_cast<PyObject *>(self),
        self->data + self->position,
        self->size - self->position,
        0,
        flags);
}

/**
 * Buffer protocol releasebuffer implementation for Decoder.
 *
 * The Decoder does not allocate per-view resources when exporting its
 * buffer, so there is nothing to clean up when a view is released.
 * This function exists only to satisfy the PyBufferProcs interface.
 *
 * @param self Decoder instance that previously exported a buffer view
 * @param view Py_buffer being released (unused)
 */
static void Decoder_releasebuffer(Decoder* self, Py_buffer *view)
{
    // nothing to do
}

static PyBufferProcs Decoder_bufferprocs = {
    (getbufferproc)Decoder_getbuffer,
    (releasebufferproc)Decoder_releasebuffer
};

static PyObject* Decoder_iter(Decoder *self)
{
    Py_INCREF(self);
    return (PyObject*)self;
}

static PyObject* Decoder_iternext(Decoder *self)
{
    if (self->deque.empty())
        return NULL;  // StopIteration

    NNTPResponse* item = self->deque.front();
    self->deque.pop_front();

    return reinterpret_cast<PyObject*>(item);
}

static int Decoder_init(Decoder *self, PyObject *args, PyObject *kwds)
{
    Py_ssize_t size;
    if (!PyArg_ParseTuple(args, "n", &size))
        return -1;

    if (size < YENC_MIN_BUFFER_SIZE)
        size = YENC_MIN_BUFFER_SIZE;
    if (size > YENC_MAX_PART_SIZE)
        size = YENC_MAX_PART_SIZE;

    self->response = nullptr;
    self->data = static_cast<char *>(malloc(size));
    self->size = size;
    self->consumed = 0;
    self->position = 0;
    if (!self->data) {
        PyErr_NoMemory();
        return -1;
    }
    new (&self->deque) std::deque<NNTPResponse*>();

    return 0;
}

static PyObject* Decoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    auto* self = reinterpret_cast<Decoder *>(type->tp_alloc(type, 0));
    if (!self) return NULL;

    return reinterpret_cast<PyObject *>(self);
}

static void Decoder_dealloc(Decoder *self)
{
    // DECREF all remaining items
    for (NNTPResponse* item : self->deque)
        Py_XDECREF(item);
    self->deque.~deque();
    free(self->data);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

Py_ssize_t Decoder_decode(Decoder *self, const char* data, const Py_ssize_t size) {
    auto instance = self->response;
    if (!instance) {
        instance = PyObject_New(NNTPResponse, &NNTPResponseType);
        if (!instance) return -1;
        self->response = instance;
        NNTPResponse_init(instance, reinterpret_cast<PyObject*>(self));
    }

    return NNTPResponse_decode_buffer(instance, data, size);;
}

/*
 * Advance decoding for data previously written via the buffer protocol.
 *
 * Decoder instances expose the Python buffer protocol; external code (for
 * example, a socket reader) writes raw NNTP article bytes directly into the
 * decoder's internal buffer. ``Decoder_process`` is then called with a
 * ``length`` indicating how many newly written bytes should be processed.
 *
 * The method consumes up to ``length`` bytes starting at the current
 * ``self->position`` from the internal buffer, feeding them into the NNTP
 * state machine and yEnc/UU decoders. Completed NNTPResponse objects are
 * queued on the decoder and can be retrieved by iterating the Decoder.
 *
 * @param self Decoder instance whose internal buffer has been filled via the
 *             buffer protocol
 * @param arg  Python integer specifying how many bytes of newly available
 *             data to process from the internal buffer
 * @return None on success, or NULL with an exception set on error
 */
static PyObject* Decoder_process(Decoder *self, PyObject *arg)
{
    Py_ssize_t length = PyLong_AsSsize_t(arg);
    if (length == -1 && PyErr_Occurred()) {
        return NULL;
    }

    if (length <= 0) {
        PyErr_SetString(PyExc_ValueError, "length is <= 0");
        return NULL;
    }

    if (self->position + length > self->size) {
        PyErr_SetString(PyExc_ValueError, "length exceeds buffer size");
        return NULL;
    }

    self->position += length;

    while (self->position > self->consumed) {
        auto read = Decoder_decode(
            self,
            self->data + self->consumed,
            self->position - self->consumed
        );
        if (read == -1) return NULL;

        self->consumed += read;
        self->response->bytes_read += read;

        const Py_ssize_t unprocessed = self->position - self->consumed;

        // Case 1: EOF for the current decoder
        if (self->response->eof) {
            if (self->response->bytes_decoded && self->response->data) {
                // Adjust the Python-size of the bytearray-object
                // This will only do a real resize if the data shrunk by half, so never in our case!
                // Resizing a bytes object always does a real resize, so more costly
                PyByteArray_Resize(self->response->data, self->response->bytes_decoded);
            }

            // Push completed decoder
            self->deque.push_back(self->response);
            self->response = nullptr;

            // More data to consume which may result in another EOF
            if (unprocessed > 0) {
                continue;
            }

            self->position = 0;
            self->consumed = 0;
            break;
        }

        // Case 2: not EOF
        if (unprocessed > 0) {
            memmove(self->data, self->data + self->consumed, unprocessed);
            self->position = unprocessed;
            self->consumed = 0;
        } else {
            self->position = 0;
            self->consumed = 0;
        }
        break;
    }

    Py_RETURN_NONE;
}

static PyMethodDef Decoder_methods[] = {
    {"process", (PyCFunction)Decoder_process, METH_O, ""},
    {NULL}
};

PyTypeObject DecoderType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "sabctols.Decoder",                   // tp_name
    sizeof(Decoder),                      // tp_basicsize
    0,                                    // tp_itemsize
    (destructor)Decoder_dealloc,          // tp_dealloc
    0,                                    // tp_print
    nullptr,                              // tp_getattr
    nullptr,                              // tp_setattr
    nullptr,                              // tp_compare / tp_reserved
    nullptr,                              // tp_repr
    nullptr,                              // tp_as_number
    nullptr,                              // tp_as_sequence
    nullptr,                              // tp_as_mapping
    nullptr,                              // tp_hash
    nullptr,                              // tp_call
    nullptr,                              // tp_str
    nullptr,                              // tp_getattro
    nullptr,                              // tp_setattro
    &Decoder_bufferprocs,                 // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                   // tp_flags
    PyDoc_STR("Decoder"),                 // tp_doc
    nullptr,                              // tp_traverse
    nullptr,                              // tp_clear
    nullptr,                              // tp_richcompare
    0,                                    // tp_weaklistoffset
    (getiterfunc)Decoder_iter,            // tp_iter
    (iternextfunc)Decoder_iternext,       // tp_iternext
    Decoder_methods,                      // tp_methods
    nullptr,                              // tp_members
    nullptr,                              // tp_getset
    nullptr,                              // tp_base
    nullptr,                              // tp_dict
    nullptr,                              // tp_descr_get
    nullptr,                              // tp_descr_set
    0,                                    // tp_dictoffset
    (initproc)Decoder_init,               // tp_init
    PyType_GenericAlloc,                  // tp_alloc
    Decoder_new,                          // tp_new
};

struct EnumEntry {
    const char* name;
    long value;
};

// Add a key, value to a dictionary
static bool add_member(PyObject* dict, const char* name, PyObject* value) {
    if (!dict || !name || !value) return false;

    Py_INCREF(value);
    if (PyDict_SetItemString(dict, name, value) < 0) {
        Py_DECREF(value);
        return false;
    }
    Py_DECREF(value);

    return true;
}

// Create an int enum for a list of entries
static PyObject* create_int_enum(const char* enum_name, const EnumEntry* entries, std::size_t count) {
    if (!enum_name || !entries) return nullptr;

    PyObject* members = PyDict_New();
    if (!members) return nullptr;

    // Range over the entries
    for (std::size_t i = 0; i < count; ++i) {
        const auto& e = entries[i];
        if (!add_member(members, e.name, PyLong_FromLong(e.value))) {
            Py_DECREF(members);
            return nullptr;
        }
    }

    PyObject* enum_module = PyImport_ImportModule("enum");
    if (!enum_module) {
        Py_DECREF(members);
        return nullptr;
    }

    PyObject* enum_obj = PyObject_CallMethod(enum_module, "IntEnum", "(sO)", enum_name, members);

    Py_DECREF(enum_module);
    Py_DECREF(members);

    return enum_obj;
}

bool yenc_init(PyObject *m) {
    if (PyType_Ready(&DecoderType) < 0 ||  PyType_Ready(&NNTPResponseType) < 0) return false;

    RapidYenc::encoder_init();
    RapidYenc::decoder_init();
    RapidYenc::crc32_init();

    // Create EncodingFormat enum
    static EnumEntry encoding_entries[] = {
        {"YENC", 0},
        {"UU", 1}
    };
    PyObject* encoding_enum = create_int_enum("EncodingFormat", encoding_entries, std::size(encoding_entries));
    if (!encoding_enum) return false;

    ENCODING_FORMAT_YENC = PyObject_GetAttrString(encoding_enum, "YENC");
    ENCODING_FORMAT_UU = PyObject_GetAttrString(encoding_enum, "UU");
    if (!ENCODING_FORMAT_YENC || !ENCODING_FORMAT_UU) {
        Py_XDECREF(encoding_enum);
        return false;
    }

    // Add objects to module
    Py_INCREF(&DecoderType);
    Py_INCREF(&NNTPResponseType);
    if (PyModule_AddObject(m, "Decoder", reinterpret_cast<PyObject *>(&DecoderType)) < 0 ||
        PyModule_AddObject(m, "NNTPResponse", reinterpret_cast<PyObject *>(&NNTPResponseType)) < 0 ||
        PyModule_AddObject(m, "EncodingFormat", encoding_enum) < 0) {
        Py_XDECREF(&DecoderType);
        Py_XDECREF(&NNTPResponseType);
        Py_XDECREF(encoding_enum);
        return false;
    }

    return true;
}
