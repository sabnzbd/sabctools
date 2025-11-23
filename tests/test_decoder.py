import io
import sys

import pytest
import glob
from tests.testsupport import *


@pytest.mark.parametrize(
    "filename",
    ["test_regular.yenc", "test_regular_2.yenc"],
)
def test_regular(filename: str):
    data_plain = read_plain_yenc_file(filename)
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_partial():
    data_plain = read_plain_yenc_file("test_partial.yenc")
    decoded_data, filename, filesize, begin, size, crc_correct = sabctools_yenc_wrapper(data_plain)
    assert filename == "90E2Sdvsmds0801dvsmds90E.part06.rar"
    assert filesize == 49152000
    assert begin == 15360000
    assert size == 384000
    assert crc_correct is None
    assert len(decoded_data) == 549


def test_special_chars():
    data_plain = read_plain_yenc_file("test_special_chars.yenc")
    # We only compare the data and the filename
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)

    data_plain = read_plain_yenc_file("test_special_utf8_chars.yenc")
    # We only compare the data and the filename
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_bad_crc():
    data_plain = read_plain_yenc_file("test_bad_crc.yenc")
    # We only compare the data and the filename
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_bad_crc_end():
    data_plain = read_plain_yenc_file("test_bad_crc_end.yenc")
    (_, _, _, _, _, crc) = sabctools_yenc_wrapper(data_plain)
    assert crc is None


def test_no_filename():
    data_plain = read_plain_yenc_file("test_no_name.yenc")
    (_, filename, _, _, _, _) = sabctools_yenc_wrapper(data_plain)
    assert filename is None


def test_padded_crc():
    data_plain = read_plain_yenc_file("test_padded_crc.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


@pytest.mark.parametrize(
    "filename",
    [
        "test_end_after_filename.yenc",
        "test_end_after_ypart.yenc",
    ],
)
def test_end_after(filename: str):
    data_plain = read_plain_yenc_file(filename)
    decoded_data, _, _, _, _, _ = sabctools_yenc_wrapper(data_plain)
    assert decoded_data is None

def test_ref_counts():
    """Note that sys.getrefcount itself adds another reference!"""
    # In Python 3.14+, getrefcount returns 1, in earlier versions it returns 2
    expected_refcount = 1 if sys.version_info >= (3, 14) else 2

    # Test regular case
    data_plain = read_plain_yenc_file("test_regular.yenc")
    data_out, filename, filesize, begin, end, crc_correct = sabctools_yenc_wrapper(data_plain)

    assert sys.getrefcount(data_plain) == expected_refcount
    assert sys.getrefcount(data_out) == expected_refcount
    assert sys.getrefcount(filename) == expected_refcount
    assert sys.getrefcount(begin) == expected_refcount
    assert sys.getrefcount(end) == expected_refcount
    assert sys.getrefcount(crc_correct) == expected_refcount

    # Test further processing
    data_plain = read_plain_yenc_file("test_bad_crc_end.yenc")
    (_, _, _, _, _, crc) = sabctools_yenc_wrapper(data_plain)
    assert crc is None
    assert sys.getrefcount(data_plain) == expected_refcount


@pytest.mark.parametrize(
    "filename",
    sorted(glob.glob("tests/yencfiles/crc_*")),
)
def test_crc_pickles(filename: str):
    data_plain = read_pickle(filename)
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


@pytest.mark.parametrize(
    "filename",
    sorted(glob.glob("tests/yencfiles/small_file*")),
)
def test_small_file_pickles(filename: str):
    data_plain = read_pickle(filename)
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


@pytest.mark.parametrize(
    "code",
    [
        # Article
        223,  # stat
        412,  # No newsgroup selected
        423,  # No article with that number
        420,  # No newsgroup selected
        430,  # Article not found
        # Auth
        281,  # Authentication accepted
        381,  # Password required
        481,  # Authentication failed/rejected
        482,  # Authentication commands issued out of sequence
        # Generic
        500,  # Unknown command
        501,  # Syntax error
        502,  # Command unavailable
        503,  # Not supported
    ],
)
def test_nntp_not_multiline(code: int):
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(bytes(f"{code} 0 <message-id>\r\n", encoding="utf-8")))
    assert eof
    assert decoder.data is None
    assert remaining_view is None
    assert decoder.status_code == code


def test_head():
    data_plain = read_plain_yenc_file("test_head.yenc")
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(data_plain))
    assert eof
    assert decoder.data is None
    assert remaining_view is None
    assert decoder.status_code == 221
    assert decoder.lines is not None
    assert len(decoder.lines) == 13
    assert "X-Received-Bytes: 740059" in decoder.lines

def test_capabilities():
    data_plain = read_plain_yenc_file("capabilities.yenc")
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(data_plain))
    assert eof
    assert decoder.data is None
    assert len(decoder.lines) == 2
    assert "VERSION 1" in decoder.lines
    assert "AUTHINFO USER PASS" in decoder.lines

def test_article():
    data_plain = read_plain_yenc_file("test_article.yenc")
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(data_plain))
    assert eof
    assert decoder.data
    assert remaining_view is None
    assert decoder.status_code == 220
    assert decoder.lines is not None
    assert len(decoder.lines) == 13
    assert "X-Received-Bytes: 740059" in decoder.lines
    assert len(decoder.data) == 716800


def test_streaming():
    BUFFER_SIZE = 1024
    buffer = bytearray(BUFFER_SIZE)
    buffer_view = memoryview(buffer)
    remaining_view = None  # unprocessed contents of the buffer
    buffer_remaining = 0
    has_seen_unprocessable = False

    yenc_files = ["test_regular_2.yenc"] * 5 + ["test_special_utf8_chars.yenc"]
    responses = []

    # Read in chunks like a network
    f = io.BytesIO()
    for filename in yenc_files:
        f.write(read_plain_yenc_file(filename))
    f.seek(0)

    decoder = sabctools.Decoder()

    while len(responses) != len(yenc_files):
        if remaining_view is not None:
            # Are there unprocessed bytes that we need to try first?
            eof, remaining_view = decoder.decode(remaining_view)
            if eof:
                responses.append(decoder)
                decoder = sabctools.Decoder()
                continue
            elif remaining_view is not None:
                # Unprocessable; copy to start of buffer and read more
                # Rare if the buffer is large enough to hold then end of a response and the next yenc headers
                buffer_view[:len(remaining_view)] = remaining_view
                buffer_remaining = len(remaining_view)
                has_seen_unprocessable = True

        if (read_bytes := f.readinto(buffer_view[buffer_remaining:])) == 0:
            break

        eof, remaining_view = decoder.decode(buffer_view[:read_bytes+buffer_remaining])
        if eof:
            buffer_remaining = 0
            responses.append(decoder)
            decoder = sabctools.Decoder()

    assert len(responses) == len(yenc_files)
    assert has_seen_unprocessable is True

    for i, dec in enumerate(responses):
        assert dec.status_code in (220, 222)
        assert python_yenc(read_plain_yenc_file(yenc_files[i])) == (dec.data, correct_unknown_encoding(dec.file_name), dec.file_size, dec.part_begin, dec.part_size, dec.crc)


def test_uu():
    data_plain = read_uu_file("logo_full.nntp")
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(data_plain))
    assert eof
    assert decoder.data
    assert remaining_view is None
    assert decoder.lines is None
    assert decoder.file_name == "logo-full.svg"
    assert decoder.file_size == 2184
    assert crc32(decoder.data) == 0x6BC2917D


# Tests for super-invalid inputs to ensure decoder doesn't crash

@pytest.mark.parametrize(
    "filename",
    [
        # Protocol/Status edge cases
        "test_invalid_status_code.yenc",  # Non-numeric status code
        "test_truncated_status.yenc",  # Incomplete status line
        "test_empty_file.yenc",  # Empty file
        "test_only_newlines.yenc",  # Only newlines
        # Malformed yEnc headers
        "test_malformed_ybegin.yenc",  # ybegin missing required fields
        "test_negative_size.yenc",  # Negative size value
        "test_huge_size.yenc",  # Extremely large size
        "test_double_ybegin.yenc",  # Two ybegin lines
        # Structure violations
        "test_missing_yend.yenc",  # ybegin without yend
        "test_ypart_without_ybegin.yenc",  # ypart before ybegin
        "test_ypart_invalid_range.yenc",  # ypart begin > end
        "test_part_exceeds_limit.yenc",  # Part size > 10MB limit
        # Special characters & encoding
        "test_non_ascii_everywhere.yenc",  # UTF-8/Chinese characters
        "test_only_dots.yenc",  # Dot-stuffing edge case
        "test_invalid_escape.yenc",  # Invalid escape sequences
        # CRC edge cases (tested separately due to additional assertions)
        "test_extremely_long_crc.yenc",  # CRC exceeding 64-bit
    ],
)
def test_invalid_inputs_no_crash(filename: str):
    """Test that decoder handles super-invalid inputs gracefully without crashing."""
    data_plain = read_plain_yenc_file(filename)
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(data_plain))
    
    # Basic check: decoder should not crash
    assert decoder is not None


def test_invalid_crc_chars():
    """Test with non-hex characters in CRC field - crc_expected should be None."""
    data_plain = read_plain_yenc_file("test_invalid_crc_chars.yenc")
    decoder = sabctools.Decoder()
    eof, remaining_view = decoder.decode(memoryview(data_plain))
    assert decoder is not None
    assert decoder.crc_expected is None
