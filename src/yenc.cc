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

static PyObject* ENCODING_FORMAT_UNKNOWN = nullptr;
static PyObject* ENCODING_FORMAT_YENC = nullptr;
static PyObject* ENCODING_FORMAT_UU = nullptr;

static PyObject* DECODING_STATUS_NOT_FINISHED = nullptr;
static PyObject* DECODING_STATUS_SUCCESS = nullptr;
static PyObject* DECODING_STATUS_NO_DATA = nullptr;
static PyObject* DECODING_STATUS_INVALID_SIZE = nullptr;
static PyObject* DECODING_STATUS_INVALID_CRC = nullptr;
static PyObject* DECODING_STATUS_INVALID_FILENAME = nullptr;
static PyObject* DECODING_STATUS_NOT_FOUND = nullptr;
static PyObject* DECODING_STATUS_FAILED = nullptr;
static PyObject* DECODING_STATUS_AUTH = nullptr;
static PyObject* DECODING_STATUS_UNKNOWN = nullptr;

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
 * Detect the encoding format of the article by examining a line.
 * Detects yEnc format (lines starting with "=ybegin ") and UUEncode format
 * (60/61 character lines starting with 'M', or "begin " header with octal permissions).
 * 
 * @param instance The Decoder instance to update
 * @param line The line to examine for format detection
 */
static inline void decoder_detect_format(Decoder* instance, std::string_view line) {
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
static inline void decoder_process_yenc_header(Decoder* instance, std::string_view line) {
    if (starts_with(line, "=ybegin ")) {
        line.remove_prefix(7);
        extract_int(line, " size=", instance->file_size);
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
        // Convert from 1-based to 0-based indexing
        if (extract_int(line, " begin=", instance->part_begin) && instance->part_begin > 0) {
            instance->part_begin--;
        }
        // Calculate part size as (end - begin)
        if (extract_int(line, " end=", instance->part_size) && instance->part_size >= instance->part_begin) {
            instance->part_size -= instance->part_begin;
        }
    } else if (starts_with(line, "=yend ")) {
        instance->has_end = true;
        line.remove_prefix(5);
        std::string_view crc32;
        // Multi-part files use pcrc32 (part CRC), single files use crc32
        constexpr std::string_view prefixes[] = { " pcrc32=", " crc32=" };

        for (const auto& prefix : prefixes) {
            auto pos = line.find(prefix, 5);
            if (pos != std::string::npos) {
                crc32 = line.substr(pos + prefix.size());
                break;
            }
        }

        if (crc32.size() >= 8) {
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
static void decoder_append_line(Decoder* instance, std::string_view line) {
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
static void decoder_dealloc(Decoder* self)
{
    Py_XDECREF(self->data);
    Py_XDECREF(self->file_name);
    Py_XDECREF(self->lines);
    Py_XDECREF(self->format);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

/**
 * Implements the buffer protocol for Decoder objects, allowing zero-copy access to decoded data.
 * This enables using Decoder instances with memoryview() for efficient data access.
 * 
 * @param obj The Decoder object
 * @param view Output buffer view to populate
 * @param flags Buffer protocol flags (writable flag is ignored/removed)
 * @return 0 on success, -1 on error
 */
static int decoder_getbuffer(PyObject *obj, Py_buffer *view, int flags)
{
    Decoder *self = reinterpret_cast<Decoder *>(obj);

    if (self->data == nullptr) {
        PyErr_SetString(PyExc_BufferError, "No data available");
        return -1;
    }

    const Py_ssize_t length = self->data_position;

    if (length > PyByteArray_Size(self->data)) {
        PyErr_SetString(PyExc_ValueError, "slice out of bounds");
        return -1;
    }

    // Populate buffer with a slice of decoded data, always read-only
    if (PyBuffer_FillInfo(view, self->data, PyByteArray_AsString(self->data), length, 1, flags) < 0) {
        return -1;
    }

    return 0;
}

/**
 * Release a buffer obtained via decoder_getbuffer.
 * Required by Python's buffer protocol.
 * 
 * @param obj The Decoder object
 * @param view The buffer view to release
 */
static void decoder_releasebuffer(PyObject *obj, Py_buffer *view)
{
    PyBuffer_Release(view);
}

static PyBufferProcs decoder_as_buffer = {
    decoder_getbuffer,
    decoder_releasebuffer
};

/**
 * Constructor for Decoder objects. Initializes a new streaming decoder instance.
 * 
 * @param type The type object
 * @param args Positional arguments (unused)
 * @param kwds Keyword arguments (unused)
 * @return New Decoder instance or NULL on allocation failure
 */
static PyObject* decoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    Decoder* self = (Decoder*)type->tp_alloc(type, 0);
    if (!self) return NULL;

    // Initialize to starting state (tp_alloc zeros memory, but explicit for clarity)
    self->format = ENCODING_FORMAT_UNKNOWN; Py_INCREF(ENCODING_FORMAT_UNKNOWN);
    self->state = RapidYenc::YDEC_STATE_CRLF;

    return (PyObject*)self;
}

/**
 * Property getter for the 'data' attribute. Returns a memoryview of the decoded data.
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return memoryview object providing access to decoded data
 */
static PyObject* decoder_get_data(Decoder* self, void* closure)
{
    return PyMemoryView_FromObject((PyObject*)self);
}

/**
 * Property getter for the 'file_name' attribute. Returns the filename from yEnc headers.
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return Unicode string with filename, or None if not found
 */
static PyObject* decoder_get_file_name(Decoder* self, void *closure)
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
static PyObject* decoder_get_crc(Decoder* self, void *closure)
{
    if (!self->crc_expected.has_value() || self->crc != self->crc_expected.value()) {
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
static PyObject* decoder_get_crc_expected(Decoder* self, void *closure)
{
    if (!self->crc_expected.has_value()) {
        Py_RETURN_NONE;
    }
    return PyLong_FromUnsignedLong(self->crc_expected.value());
}

/**
 * Validate a completed yEnc-encoded article and return its decoding status.
 * 
 * Performs a series of validation checks on the decoded yEnc data:
 * 1. Ensures data was actually decoded (non-zero size)
 * 2. Verifies the decoded size matches the expected size from yEnc headers
 * 3. Validates the calculated CRC32 matches the expected CRC from =yend footer
 * 4. Confirms a filename was extracted from the =ybegin header
 * 
 * @param self The Decoder instance with completed yEnc decoding
 * @return PyObject* representing the status:
 *         - DECODING_STATUS_NO_DATA if no data was decoded
 *         - DECODING_STATUS_INVALID_SIZE if decoded size doesn't match expected
 *         - DECODING_STATUS_INVALID_CRC if CRC validation fails
 *         - DECODING_STATUS_INVALID_FILENAME if no filename was found
 *         - DECODING_STATUS_SUCCESS if all validations pass
 * 
 * Note: This function increments the reference count of the returned status object.
 */
inline PyObject* decoder_get_status_yenc(Decoder* self) {
    if (self->data_position == 0) {
        Py_INCREF(DECODING_STATUS_NO_DATA);
        return DECODING_STATUS_NO_DATA;
    }

    if ((!self->has_part && self->data_position != self->end_size) || self->data_position != self->end_size) {
        Py_INCREF(DECODING_STATUS_INVALID_SIZE);
        return DECODING_STATUS_INVALID_SIZE;
    }

    if (!self->crc_expected.has_value() || self->crc != self->crc_expected.value()) {
        Py_INCREF(DECODING_STATUS_INVALID_CRC);
        return DECODING_STATUS_INVALID_CRC;
    }

    if (self->file_name == nullptr) {
        Py_INCREF(DECODING_STATUS_INVALID_FILENAME);
        return DECODING_STATUS_INVALID_FILENAME;
    }

    Py_INCREF(DECODING_STATUS_SUCCESS);
    return DECODING_STATUS_SUCCESS;
}

/**
 * Validate a completed UUEncode-encoded article and return its decoding status.
 * 
 * Performs basic validation checks on the decoded UUEncode data:
 * 1. Ensures data was actually decoded (non-zero size)
 * 2. Confirms a filename was extracted from the UUEncode header
 * 
 * Unlike yEnc validation, UUEncode does not include CRC or size information
 * in its headers, so only minimal validation is performed.
 * 
 * @param self The Decoder instance with completed UUEncode decoding
 * @return PyObject* representing the status:
 *         - DECODING_STATUS_NO_DATA if no data was decoded
 *         - DECODING_STATUS_INVALID_FILENAME if no filename was found
 *         - DECODING_STATUS_SUCCESS if all validations pass
 * 
 * Note: This function increments the reference count of the returned status object.
 */
inline PyObject* decoder_get_status_uu(Decoder* self)
{
    if (self->data_position == 0) {
        Py_INCREF(DECODING_STATUS_NO_DATA);
        return DECODING_STATUS_NO_DATA;
    }

    if (self->file_name == nullptr) {
        Py_INCREF(DECODING_STATUS_INVALID_FILENAME);
        return DECODING_STATUS_INVALID_FILENAME;
    }

    Py_INCREF(DECODING_STATUS_SUCCESS);
    return DECODING_STATUS_SUCCESS;
}

/**
 * Property getter for the 'status' attribute. Returns the overall decoding status.
 * 
 * This is the main status property that dispatches to format-specific validators
 * or interprets NNTP response codes when no encoding format was detected.
 * 
 * Status determination flow:
 * 1. If decoding is incomplete (!done), returns NOT_FINISHED
 * 2. If format is yEnc, delegates to decoder_get_status_yenc for validation
 * 3. If format is UUEncode, delegates to decoder_get_status_uu for validation
 * 4. Otherwise interprets NNTP status codes:
 *    - STAT/MULTILINE responses: SUCCESS (non-article responses)
 *    - AUTH codes: DECODING_STATUS_AUTH (authentication required)
 *    - COMMAND_FAILED: DECODING_STATUS_FAILED (connection may need to close)
 *    - 410-439: DECODING_STATUS_NOT_FOUND (article/group selection failures)
 *    - 440-499: DECODING_STATUS_FAILED (other command failures)
 *    - Other codes: DECODING_STATUS_UNKNOWN (caller must inspect status_code)
 * 
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return PyObject* representing the decoding status (reference count incremented)
 */
static PyObject* decoder_get_status(Decoder* self, void *closure)
{
    // EOF not reached, need more data
    if (!self->done) {
        Py_INCREF(DECODING_STATUS_NOT_FINISHED);
        return DECODING_STATUS_NOT_FINISHED;
    }

    if (self->format == ENCODING_FORMAT_YENC) {
        return decoder_get_status_yenc(self);
    }

    if (self->format == ENCODING_FORMAT_UU) {
        return decoder_get_status_uu(self);
    }

    // Include special cases for non-article responses
    if (one_of(self->status_code, NNTP_STAT, NNTP_MULTILINE)) {
        Py_INCREF(DECODING_STATUS_SUCCESS);
        return DECODING_STATUS_SUCCESS;
    }

    // Need to or are in the process of authenticating
    if (one_of(self->status_code, NNTP_AUTH)) {
        Py_INCREF(DECODING_STATUS_AUTH);
        return DECODING_STATUS_AUTH;
    }

    // Connection needs closing
    if (one_of(self->status_code, NNTP_COMMAND_FAILED)) {
        Py_INCREF(DECODING_STATUS_FAILED);
        return DECODING_STATUS_FAILED;
    }

    // 4xx - Command was syntactically correct but failed for some reason
    // x1x - Newsgroup selection
    // x2x - Article selection
    // x3x - Distribution functions
    // x4x - Posting
    // x8x - Reserved for authentication and privacy extensions
    // x9x - Reserved for private use (non-standard extensions)
    if (self->status_code >= 410 && self->status_code <= 499) {
        if (self->status_code <= 439) {
            Py_INCREF(DECODING_STATUS_NOT_FOUND);
            return DECODING_STATUS_NOT_FOUND;
        }
        Py_INCREF(DECODING_STATUS_FAILED);
        return DECODING_STATUS_FAILED;
    }

    // Case not handled, caller needs to look at status_code
    Py_INCREF(DECODING_STATUS_UNKNOWN);
    return DECODING_STATUS_UNKNOWN;
}

/**
 * Property getter for the 'lines' attribute. Returns NNTP/header lines captured
 * before the encoding format was determined.
 *
 * @param self The Decoder instance
 * @param closure Unused closure parameter
 * @return List of Unicode strings, without \r\n at the end
 */
static PyObject* decoder_get_lines(Decoder* self, void *closure)
{
    if (self->lines == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->lines);
    return self->lines;
}

/**
 * Decode yEnc-encoded body data in streaming fashion.
 * This is the core decoding function that processes encoded data incrementally.
 * 
 * @param instance The Decoder instance with state to maintain across calls
 * @param buf Input buffer containing encoded data
 * @param buf_len Total length of input buffer
 * @param read Input/output parameter tracking position in buffer
 * @return true on success, false on error
 * 
 * Key behaviors:
 * - Allocates output buffer on first call based on expected size from headers
 * - Resizes output buffer if needed (for malformed posts)
 * - Uses RapidYenc SIMD decoder with state machine to handle partial data
 * - Updates CRC incrementally as data is decoded
 * - Detects end conditions (control line, article terminator) and adjusts read position
 * - Releases GIL during decoding for parallel processing
 */
static bool decoder_decode_yenc(Decoder *instance, const char *buf, const Py_ssize_t buf_len, Py_ssize_t &read) {
    if (read >= buf_len) return false;

    constexpr Py_ssize_t CHUNK = YENC_CHUNK_SIZE;

    // Remaining input
    Py_ssize_t remaining = buf_len - read;
    if (remaining <= 0) return false;

    if (instance->data == nullptr) {
        // Allocate output buffer on first decode call
        // Use size from headers, capped at YENC_MAX_PART_SIZE for safety
        Py_ssize_t base = instance->part_size > 0 ? instance->part_size : instance->file_size;
        Py_ssize_t expected = base + 64;  // small margin to see the end of yEnc data
        // Round up to next multiple of CHUNK
        expected = ((expected + YENC_CHUNK_SIZE - 1) / CHUNK) * CHUNK;
        // Add an extra CHUNK so we should never need to resize
        expected += CHUNK;

        if (expected < YENC_MIN_BUFFER_SIZE)
            expected = YENC_MIN_BUFFER_SIZE;
        if (expected > YENC_MAX_PART_SIZE)
            expected = YENC_MAX_PART_SIZE;

        instance->data = PyByteArray_FromStringAndSize(nullptr, expected);
        if (!instance->data) {
            PyErr_SetNone(PyExc_MemoryError);
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
        Py_ssize_t needed = instance->data_position + chunk_in;
        Py_ssize_t current = PyByteArray_GET_SIZE(instance->data);

        if (needed > current) {
            if (needed > YENC_MAX_PART_SIZE) {
                PyBuffer_Release(&dst_buf);
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
        char *dst = data_ptr + instance->data_position;
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
        instance->data_position += produced;

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
            instance->body = false; // Back up to include ".\r\n" for terminator detection
            read -= 3;
            break;
    }

    return true;
}

/**
 * Decode a single UUEncoded character to its 6-bit value.
 *
 * @param c The UUEncoded character (typically in range ' ' to '_', or '`').
 * @return The decoded 6-bit value (0–63), masked to ensure it stays within range.
 */
constexpr unsigned char uu_decode_char(const char c) noexcept {
    return (c == '`') ? 0 : ((c - ' ') & 0x3F);
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
static bool decoder_decode_uu(Decoder* instance, std::string_view line)
{
    // Allocate or resize bytearray
    if (!instance->data) {
        instance->data = PyByteArray_FromStringAndSize(nullptr, line.size());
        if (!instance->data) {
            PyErr_SetNone(PyExc_MemoryError);
            return false;
        }
    } else if (PyByteArray_Resize(instance->data, instance->data_position + line.size()) == -1) {
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
    if (instance->body && (starts_with(line, "end ") || starts_with(line, "`"))) {
        instance->body = false;
        instance->file_size = instance->data_position;
        return true;
    }

    // Decode body lines
    if (instance->body && !line.empty()) {
        // Remove dot stuffing
        if (starts_with(line, "..")) {
            line.remove_prefix(1);
        }

        // Workaround for broken uuencoders by Fredrik Lundh
        std::size_t effLen = (((static_cast<unsigned char>(line.front()) - 32) & 63) * 4 + 5) / 3;
        if (effLen > line.size()) return true; // ignore invalid lines

        line.remove_prefix(1); // skip length byte
        char* out_ptr = PyByteArray_AsString(instance->data) + instance->data_position;
        auto it = line.begin();
        const auto end = line.end();

        while (effLen > 0 && std::distance(it, end) >= 4) {
            const auto chunk = std::min(effLen, static_cast<std::size_t>(3));
            const unsigned char c0 = uu_decode_char(*it++);
            const unsigned char c1 = uu_decode_char(*it++);
            unsigned char c2 = 0;

            *out_ptr++ = static_cast<char>(c0 << 2 | c1 >> 4);

            if (chunk > 1) {
                c2 = uu_decode_char(*it++);
                *out_ptr++ = static_cast<char>(c1 << 4 | c2 >> 2);
            }

            if (chunk > 2) {
                const unsigned char c3 = uu_decode_char(*it++);
                *out_ptr++ = static_cast<char>(c2 << 6 | c3);
            }

            effLen -= 3;
        }

        instance->data_position = out_ptr - PyByteArray_AsString(instance->data);
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
 * @param instance The Decoder instance maintaining state across calls
 * @param input_buffer The buffer to process
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
static Py_ssize_t decoder_decode_buffer(Decoder *instance, const Py_buffer *input_buffer) {
    const char* buf = static_cast<char*>(input_buffer->buf);
    const Py_ssize_t buf_len = input_buffer->len;
    Py_ssize_t read = 0;

    // Resume body decoding if we were in the middle of it
    if (instance->body && instance->format == ENCODING_FORMAT_YENC) {
        if (!decoder_decode_yenc(instance, buf, buf_len, read)) return -1;
        if (instance->body) return read;  // Still in body, need more data
    }

    // Parse headers and footers line-by-line
    std::string_view line;
    while (next_crlf_line(buf, buf_len, read, line)) {
        if (line == ".") {
            // NNTP article terminator
            instance->done = true;
            return read;
        }

        if (instance->format == ENCODING_FORMAT_UNKNOWN) {
            if (!instance->status_code && line.length() >= 3) {
                // Store the full command response line
                instance->message = decode_utf8_with_fallback(line);
                // First line should be NNTP status code (220, 222, 223, etc.)
                if (!extract_int(line, "", instance->status_code)
                    || !one_of(instance->status_code, NNTP_MULTILINE)) {
                    // Single-line response (not ARTICLE/BODY), we're done
                    instance->done = true;
                    break;
                }
                continue;
            }

            decoder_detect_format(instance, line);
        }

        if (instance->format == ENCODING_FORMAT_UNKNOWN) {
            // Format is still unknown so record lines
            decoder_append_line(instance, line);
        } else if (instance->format == ENCODING_FORMAT_YENC) {
            decoder_process_yenc_header(instance, line);
            if (instance->body) {
                // =ypart was encountered, switch to body decoding
                if (!decoder_decode_yenc(instance, buf, buf_len, read)) return -1;
                if (instance->body) return read;  // Still decoding, need more data
            }
        } else if (instance->format == ENCODING_FORMAT_UU) {
            if (!decoder_decode_uu(instance, line)) return -1;
        }
    }

    return read;
}

/**
 * Main decode method called from Python: decoder.decode(data)
 * Processes a chunk of data in streaming fashion, maintaining state between calls.
 * 
 * @param self The Decoder instance
 * @param Py_memoryview_obj memoryview containing data to decode
 * @return Tuple of (done: bool, remaining: memoryview | None)
 * 
 * Returns:
 * - done: True if decoding is complete (saw ".\r\n" or single-line response)
 * - remaining: memoryview of unprocessed data if buffer contained multiple articles,
 *              or None if all data was consumed
 * 
 * This enables efficient streaming from network sockets:
 * ```python
 * decoder = sabctools.Decoder()
 * while data := socket.recv(8192):
 *     done, remaining = decoder.decode(memoryview(data))
 *     if done:
 *         break
 * ```
 */
PyObject* decoder_decode(PyObject* self, PyObject* Py_memoryview_obj) {
    Decoder* instance = reinterpret_cast<Decoder*>(self);

    PyObject* unprocessed_memoryview = Py_None;

    if (instance->done) {
        PyErr_SetString(PyExc_ValueError, "Already finished decoding");
        return NULL;
    }

    // Verify input type
    if (!PyMemoryView_Check(Py_memoryview_obj)) {
        PyErr_SetString(PyExc_TypeError, "Expected memoryview");
        return NULL;
    }

    // Get buffer and validate
    Py_buffer *input_buffer = PyMemoryView_GET_BUFFER(Py_memoryview_obj);
    if (!PyBuffer_IsContiguous(input_buffer, 'C') || input_buffer->len <= 0) {
        PyErr_SetString(PyExc_ValueError, "Invalid data length or order");
        return NULL;
    }

    const auto read = decoder_decode_buffer(instance, input_buffer);
    if (read == -1) return NULL;
    instance->bytes_read += read;

    // Create memoryview for unprocessed data if any remains
    const Py_ssize_t unprocessed_length = input_buffer->len - read;
    if (unprocessed_length > 0) {
        Py_buffer subbuf = *input_buffer; // shallow copy
        subbuf.buf = static_cast<char *>(input_buffer->buf) + read;
        subbuf.len = unprocessed_length;

        // Adjust shape for 1D array
        if (subbuf.ndim == 1 && subbuf.shape) {
            subbuf.shape[0] = unprocessed_length;
        }

        unprocessed_memoryview = PyMemoryView_FromBuffer(&subbuf);
    } else {
        Py_INCREF(unprocessed_memoryview);
    }

    return Py_BuildValue("(N, O)", decoder_get_status(instance, nullptr), unprocessed_memoryview);
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
 * String representation of Decoder for debugging.
 * 
 * @param self The Decoder instance
 * @return Unicode string with decoder state summary
 */
static PyObject* decoder_repr(Decoder* self)
{
    const auto status = decoder_get_status(self, nullptr);
    const auto repr = PyUnicode_FromFormat(
        "<Decoder: status=%R, status_code=%d, message=%R, file_name=%R, length=%zd>",
        status,
        self->status_code,
        self->message ? self->message : Py_None,
        self->file_name ? self->file_name : Py_None,
        self->data_position);
    Py_DECREF(status);
    return repr;
}

static PyMethodDef decoder_methods[] = {
    {"decode", decoder_decode, METH_O, ""},
    {nullptr, nullptr, 0, nullptr}
};

static PyMemberDef decoder_members[] = {
    {"file_size", T_PYSSIZET, offsetof(Decoder, file_size), READONLY, ""},
    {"part_begin", T_PYSSIZET, offsetof(Decoder, part_begin), READONLY, ""},
    {"part_size", T_PYSSIZET, offsetof(Decoder, part_size), READONLY, ""},
    {"status_code", T_INT, offsetof(Decoder, status_code), READONLY, ""},
    {"message", T_OBJECT_EX, offsetof(Decoder, message), READONLY, ""},
    {"bytes_read", T_ULONGLONG, offsetof(Decoder, bytes_read), READONLY, ""},
    {"format", T_OBJECT_EX, offsetof(Decoder, format), READONLY, ""},
    {nullptr, 0, 0, 0, nullptr}
};

static PyGetSetDef decoder_gets_sets[] = {
    {"data", (getter)decoder_get_data, NULL, NULL, NULL},
    {"file_name", (getter)decoder_get_file_name, NULL, NULL, NULL},
    {"lines", (getter)decoder_get_lines, NULL, NULL, NULL},
    {"crc", (getter)decoder_get_crc, NULL, NULL, NULL},
    {"crc_expected", (getter)decoder_get_crc_expected, NULL, NULL, NULL},
    {"status", (getter)decoder_get_status, NULL, NULL, NULL},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

PyTypeObject DecoderType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "sabctools.Decoder",            // tp_name
    sizeof(Decoder),                // tp_basicsize
    0,                              // tp_itemsize
    (destructor)decoder_dealloc,    // tp_dealloc
    0,                              // tp_vectorcall_offset
    nullptr,                        // tp_getattr
    nullptr,                        // tp_setattr
    nullptr,                        // tp_as_async
    (reprfunc)decoder_repr,         // tp_repr
    nullptr,                        // tp_as_number
    nullptr,                        // tp_as_sequence
    nullptr,                        // tp_as_mapping
    nullptr,                        // tp_hash
    nullptr,                        // tp_call
    nullptr,                        // tp_str
    nullptr,                        // tp_getattro
    nullptr,                        // tp_setattro
    &decoder_as_buffer,             // tp_as_buffer
    Py_TPFLAGS_DEFAULT,             // tp_flags
    PyDoc_STR("Decoder"),           // tp_doc
    nullptr,                        // tp_traverse
    nullptr,                        // tp_clear
    nullptr,                        // tp_richcompare
    0,                              // tp_weaklistoffset
    nullptr,                        // tp_iter
    nullptr,                        // tp_iternext
    decoder_methods,                // tp_methods
    decoder_members,                // tp_members
    decoder_gets_sets,              // tp_getset
    nullptr,                        // tp_base
    nullptr,                        // tp_dict
    nullptr,                        // tp_descr_get
    nullptr,                        // tp_descr_set
    0,                              // tp_dictoffset
    nullptr,                        // tp_init
    PyType_GenericAlloc,            // tp_alloc
    (newfunc)decoder_new,           // tp_new
};

static PyObject* create_encoding_format_enum() {
    PyObject* enum_module = PyImport_ImportModule("enum");

    PyObject* members = PyDict_New();
    PyDict_SetItemString(members, "UNKNOWN",  PyLong_FromLong(0));
    PyDict_SetItemString(members, "YENC",  PyLong_FromLong(1));
    PyDict_SetItemString(members, "UU",  PyLong_FromLong(2));

    PyObject* encoding_format_enum = PyObject_CallMethod(enum_module, "IntEnum", "(sO)", "EncodingFormat", members);

    Py_DECREF(enum_module);
    Py_DECREF(members);

    return encoding_format_enum;
}

static PyObject* create_decoding_status_enum() {
    PyObject* enum_module = PyImport_ImportModule("enum");

    PyObject* members = PyDict_New();
    PyDict_SetItemString(members, "NOT_FINISHED", PyLong_FromLong(0));
    PyDict_SetItemString(members, "SUCCESS", PyLong_FromLong(1));
    PyDict_SetItemString(members, "NO_DATA", PyLong_FromLong(2));
    PyDict_SetItemString(members, "INVALID_SIZE", PyLong_FromLong(3));
    PyDict_SetItemString(members, "INVALID_CRC", PyLong_FromLong(4));
    PyDict_SetItemString(members, "INVALID_FILENAME", PyLong_FromLong(5));
    PyDict_SetItemString(members, "NOT_FOUND", PyLong_FromLong(6));
    PyDict_SetItemString(members, "FAILED", PyLong_FromLong(7));
    PyDict_SetItemString(members, "AUTH", PyLong_FromLong(8));
    PyDict_SetItemString(members, "UNKNOWN",  PyLong_FromLong(99));

    PyObject* encoding_format_enum = PyObject_CallMethod(enum_module, "IntEnum", "(sO)", "DecodingStatus", members);

    Py_DECREF(enum_module);
    Py_DECREF(members);

    return encoding_format_enum;
}

bool yenc_init(PyObject *m) {
    RapidYenc::encoder_init();
    RapidYenc::decoder_init();
    RapidYenc::crc32_init();

    PyObject* encoding_format_enum = create_encoding_format_enum();
    if (!encoding_format_enum) return false;

    ENCODING_FORMAT_UNKNOWN = PyObject_GetAttrString(encoding_format_enum, "UNKNOWN"); Py_INCREF(ENCODING_FORMAT_UNKNOWN);
    ENCODING_FORMAT_YENC = PyObject_GetAttrString(encoding_format_enum, "YENC"); Py_INCREF(ENCODING_FORMAT_YENC);
    ENCODING_FORMAT_UU = PyObject_GetAttrString(encoding_format_enum, "UU"); Py_INCREF(ENCODING_FORMAT_UU);

    PyObject* decoding_status_enum = create_decoding_status_enum();
    if (!decoding_status_enum) {
        Py_DECREF(encoding_format_enum);
        Py_DECREF(ENCODING_FORMAT_UNKNOWN);
        Py_DECREF(ENCODING_FORMAT_YENC);
        Py_DECREF(ENCODING_FORMAT_UU);
        return false;
    }

    DECODING_STATUS_UNKNOWN = PyObject_GetAttrString(decoding_status_enum, "UNKNOWN"); Py_INCREF(DECODING_STATUS_UNKNOWN);
    DECODING_STATUS_SUCCESS = PyObject_GetAttrString(decoding_status_enum, "SUCCESS"); Py_INCREF(DECODING_STATUS_SUCCESS);
    DECODING_STATUS_NOT_FINISHED = PyObject_GetAttrString(decoding_status_enum, "NOT_FINISHED"); Py_INCREF(DECODING_STATUS_NOT_FINISHED);
    DECODING_STATUS_NO_DATA = PyObject_GetAttrString(decoding_status_enum, "NO_DATA"); Py_INCREF(DECODING_STATUS_NO_DATA);
    DECODING_STATUS_INVALID_SIZE = PyObject_GetAttrString(decoding_status_enum, "INVALID_SIZE"); Py_INCREF(DECODING_STATUS_INVALID_SIZE);
    DECODING_STATUS_INVALID_CRC = PyObject_GetAttrString(decoding_status_enum, "INVALID_CRC"); Py_INCREF(DECODING_STATUS_INVALID_CRC);
    DECODING_STATUS_INVALID_FILENAME = PyObject_GetAttrString(decoding_status_enum, "INVALID_FILENAME"); Py_INCREF(DECODING_STATUS_INVALID_FILENAME);
    DECODING_STATUS_NOT_FOUND = PyObject_GetAttrString(decoding_status_enum, "NOT_FOUND"); Py_INCREF(DECODING_STATUS_NOT_FOUND);
    DECODING_STATUS_FAILED = PyObject_GetAttrString(decoding_status_enum, "FAILED"); Py_INCREF(DECODING_STATUS_FAILED);
    DECODING_STATUS_AUTH = PyObject_GetAttrString(decoding_status_enum, "AUTH"); Py_INCREF(DECODING_STATUS_AUTH);

    Py_INCREF(&DecoderType);
    if (PyModule_AddObject(m, "Decoder", reinterpret_cast<PyObject *>(&DecoderType)) < 0) goto fail;
    if (PyModule_AddObject(m, "EncodingFormat", encoding_format_enum) < 0) goto fail;
    if (PyModule_AddObject(m, "DecodingStatus", decoding_status_enum) < 0) goto fail;

    return true;

fail:
    Py_DECREF(&DecoderType);
    Py_DECREF(encoding_format_enum);
    Py_DECREF(decoding_status_enum);
    Py_DECREF(ENCODING_FORMAT_UNKNOWN);
    Py_DECREF(ENCODING_FORMAT_YENC);
    Py_DECREF(ENCODING_FORMAT_UU);
    Py_DECREF(DECODING_STATUS_UNKNOWN);
    Py_DECREF(DECODING_STATUS_SUCCESS);
    Py_DECREF(DECODING_STATUS_NOT_FINISHED);
    Py_DECREF(DECODING_STATUS_NO_DATA);
    Py_DECREF(DECODING_STATUS_INVALID_SIZE);
    Py_DECREF(DECODING_STATUS_INVALID_CRC);
    Py_DECREF(DECODING_STATUS_INVALID_FILENAME);
    Py_DECREF(DECODING_STATUS_NOT_FOUND);
    Py_DECREF(DECODING_STATUS_FAILED);
    Py_DECREF(DECODING_STATUS_AUTH);

    return false;
}
