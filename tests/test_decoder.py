import io
import sys
from zlib import crc32

import pytest
import glob
from tests.testsupport import *
from sabctools import DecodingStatus


@pytest.mark.parametrize(
    "filename",
    ["test_regular.yenc", "test_regular_2.yenc"],
)
def test_regular(filename: str):
    data_plain = read_plain_yenc_file(filename)
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_partial():
    data_plain = read_plain_yenc_file("test_partial.yenc")
    decoded_data, filename, filesize, begin, size, crc_correct = sabctools_yenc_wrapper(data_plain, expected_status=DecodingStatus.INVALID_SIZE)
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
    (_, _, _, _, _, crc) = sabctools_yenc_wrapper(data_plain, expected_status=DecodingStatus.INVALID_CRC)
    assert crc is None


def test_no_filename():
    data_plain = read_plain_yenc_file("test_no_name.yenc")
    (_, filename, _, _, _, _) = sabctools_yenc_wrapper(data_plain, expected_status=DecodingStatus.INVALID_FILENAME)
    assert filename is None


def test_padded_crc():
    data_plain = read_plain_yenc_file("test_padded_crc.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_end_after_filename():
    data_plain = read_plain_yenc_file("test_end_after_filename.yenc")
    with pytest.raises(BufferError) as excinfo:
        sabctools_yenc_wrapper(data_plain, expected_status=DecodingStatus.NO_DATA)
    assert "No data available" in str(excinfo.value)


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
    (_, _, _, _, _, crc) = sabctools_yenc_wrapper(data_plain, expected_status=DecodingStatus.INVALID_CRC)
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
    "code,expected_status",
    [
        # Article
        (223, DecodingStatus.SUCCESS),  # stat
        (412, DecodingStatus.NOT_FOUND),  # No newsgroup selected
        (423, DecodingStatus.NOT_FOUND),  # No article with that number
        (420, DecodingStatus.NOT_FOUND),  # No newsgroup selected
        (430, DecodingStatus.NOT_FOUND),  # Article not found
        # Auth
        (281, DecodingStatus.AUTH),  # Authentication accepted
        (381, DecodingStatus.AUTH),  # Password required
        (481, DecodingStatus.AUTH),  # Authentication failed/rejected
        (482, DecodingStatus.AUTH),  # Authentication commands issued out of sequence
        # Generic
        (500, DecodingStatus.FAILED),  # Unknown command
        (501, DecodingStatus.FAILED),  # Syntax error
        (502, DecodingStatus.FAILED),  # Command unavailable
        (503, DecodingStatus.FAILED),  # Not supported
    ],
)
def test_nntp_not_multiline(code: int, expected_status: DecodingStatus):
    decoder = sabctools.Decoder()
    status, remaining_view = decoder.decode(memoryview(bytes(f"{code} 0 <message-id>\r\n", encoding="utf-8")))
    assert status is expected_status
    assert remaining_view is None
    assert decoder.status_code == code


def test_head():
    data_plain = read_plain_yenc_file("test_head.yenc")
    decoder = sabctools.Decoder()
    status, remaining_view = decoder.decode(memoryview(data_plain))
    assert status is DecodingStatus.SUCCESS
    assert remaining_view is None
    assert decoder.status_code == 221
    assert decoder.lines is not None
    assert len(decoder.lines) == 13
    assert "X-Received-Bytes: 740059" in decoder.lines
    with pytest.raises(BufferError):
        memoryview(decoder)

def test_capabilities():
    data_plain = read_plain_yenc_file("capabilities.yenc")
    decoder = sabctools.Decoder()
    status, remaining_view = decoder.decode(memoryview(data_plain))
    assert status is DecodingStatus.SUCCESS
    assert len(decoder.lines) == 2
    assert "VERSION 1" in decoder.lines
    assert "AUTHINFO USER PASS" in decoder.lines

def test_article():
    data_plain = read_plain_yenc_file("test_article.yenc")
    decoder = sabctools.Decoder()
    status, remaining_view = decoder.decode(memoryview(data_plain))
    assert status is DecodingStatus.SUCCESS
    assert remaining_view is None
    assert decoder.status_code == 220
    assert decoder.lines is not None
    assert len(decoder.lines) == 13
    assert "X-Received-Bytes: 740059" in decoder.lines
    assert len((memoryview(decoder))) == 716800


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
            done, remaining_view = decoder.decode(remaining_view)
            if done:
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

        done, remaining_view = decoder.decode(buffer_view[:read_bytes+buffer_remaining])
        if done:
            buffer_remaining = 0
            responses.append(decoder)
            decoder = sabctools.Decoder()

    assert len(responses) == len(yenc_files)
    assert has_seen_unprocessable is True

    for i, dec in enumerate(responses):
        assert dec.status_code in (220, 222)
        assert python_yenc(read_plain_yenc_file(yenc_files[i])) == (memoryview(dec), correct_unknown_encoding(dec.file_name), dec.file_size, dec.part_begin, dec.part_size, dec.crc)


def test_uu():
    data_plain = read_uu_file("logo_full.nntp")
    decoder = sabctools.Decoder()
    status, unprocessed = decoder.decode(memoryview(data_plain))
    assert status is DecodingStatus.SUCCESS
    assert unprocessed is None
    assert decoder.lines is None
    assert decoder.file_name == "logo-full.svg"
    assert decoder.file_size == 2184
    assert crc32(decoder) == 0x6BC2917D
