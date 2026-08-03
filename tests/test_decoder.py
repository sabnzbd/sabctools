import io
import os
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
    _, _, _, _, _, crc = sabctools_yenc_wrapper(data_plain)
    assert crc is None


def test_no_filename():
    data_plain = read_plain_yenc_file("test_no_name.yenc")
    _, filename, _, _, _, _ = sabctools_yenc_wrapper(data_plain)
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
    _, _, _, _, _, crc = sabctools_yenc_wrapper(data_plain)
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
    line = bytes(f"{code} 0 <message-id>\r\n", encoding="utf-8")
    input = BytesIO(line)
    decoder = sabctools.Decoder(len(line))
    n = input.readinto(decoder)
    decoder.process(n)
    response = next(decoder, None)
    assert response
    assert response.data is None
    assert response.status_code == code


def test_head():
    data_plain = read_plain_yenc_file("test_head.yenc")
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)

    response = next(decoder, None)
    assert response
    assert response.data is None
    assert response.status_code == 221
    assert response.lines is not None
    assert len(response.lines) == 13
    assert "X-Received-Bytes: 740059" in response.lines


def test_capabilities():
    data_plain = read_plain_yenc_file("capabilities.yenc")
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)

    response = next(decoder, None)
    assert response
    assert response.data is None
    assert len(response.lines) == 2
    assert "VERSION 1" in response.lines
    assert "AUTHINFO USER PASS" in response.lines


def test_article():
    data_plain = read_plain_yenc_file("test_article.yenc")
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)

    response = next(decoder, None)
    assert response
    assert response.data
    assert response.status_code == 220
    assert response.lines is not None
    assert len(response.lines) == 13
    assert "X-Received-Bytes: 740059" in response.lines
    assert len(response.data) == 716800


def test_streaming():
    BUFFER_SIZE = 1024
    yenc_files = ["test_regular_2.yenc"] * 5 + ["test_special_utf8_chars.yenc"]
    responses = []

    # Read in chunks like a network
    input = io.BytesIO()
    for filename in yenc_files:
        input.write(read_plain_yenc_file(filename))
    input.seek(0)

    decoder = sabctools.Decoder(BUFFER_SIZE)
    while (n := input.readinto(decoder)) != 0:
        decoder.process(n)

    for response in decoder:
        responses.append(response)

    assert len(responses) == len(yenc_files)

    for i, dec in enumerate(responses):
        assert dec.status_code in (220, 222)
        assert python_yenc(read_plain_yenc_file(yenc_files[i])) == (
            dec.data,
            correct_unknown_encoding(dec.file_name),
            dec.file_size,
            dec.part_begin,
            dec.part_size,
            dec.crc,
        )


def test_uu():
    data_plain = read_uu_file("logo_full.nntp")
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)
    response = next(decoder, None)
    assert response
    assert response.data
    assert response.lines is None
    assert response.file_name == "logo-full.svg"
    assert response.file_size == 2184
    assert response.crc == 0x6BC2917D


def test_uu_no_filename():
    data_plain = read_uu_file("logo_full.nntp")
    data_plain = data_plain.replace(b"begin 644 logo-full.svg", b"begin 644")
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)
    response = next(decoder, None)
    assert response
    assert response.file_name is None


@pytest.mark.parametrize(
    "length",
    range(1, 46),
)
def test_uu_length(length: int):
    expected = os.urandom(length)
    parts = [b"222 0 <foo@bar>\r\n", uu(expected), b"\r\n" b".\r\n"]
    data_plain = b"".join(parts)
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)
    response = next(decoder, None)
    assert response
    assert response.format is sabctools.EncodingFormat.UU
    assert response.bytes_decoded
    assert response.data == expected


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
        "test_huge_size_1TiB.yenc",  # Extremely large size (1 TB)
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
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)

    # Basic check: decoder should not crash
    assert decoder is not None


def test_exceeds_size_limit():
    size = 20 * 1024 * 1024
    output, crc = sabctools.yenc_encode(b"\x00" * size)
    parts = [
        b"222 0 <foo@bar>\r\n" b"=ybegin part=1 total=1 line=128 size=%d name=helloworld\r\n" % size,
        b"=ypart begin=1 end=%d\r\n" % size,
        output,
        b"\r\n=yend size=%d pcrc32=%s\r\n" % (size, hex(crc).encode()[2:]),
        b".\r\n",
    ]
    data_plain = b"".join(parts)
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(256 * 1024)
    with pytest.raises(BufferError, match="Maximum data buffer size exceeded"):
        while n := input.readinto(decoder):
            decoder.process(n)


def test_invalid_crc_chars():
    """Test with non-hex characters in CRC field - crc_expected should be None."""
    data_plain = read_plain_yenc_file("test_invalid_crc_chars.yenc")
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)
    response = next(decoder, None)
    assert response
    assert response.crc_expected is None


@pytest.mark.parametrize(
    "hex,expected",
    [
        ["ffffffffa95d3e50", 0xA95D3E50],
        ["fffffffa95d3e50", 0xA95D3E50],
        ["ffffffa95d3e50", 0xA95D3E50],
        ["fffffa95d3e50", 0xA95D3E50],
        ["ffffa95d3e50", 0xA95D3E50],
        ["fffa95d3e50", 0xA95D3E50],
        ["ffa95d3e50", 0xA95D3E50],
        ["fa95d3e50", 0xA95D3E50],
        ["a95d3e50", 0xA95D3E50],
        ["a95d3e5", 0xA95D3E5],
        ["a95d3e", 0xA95D3E],
        ["a95d3", 0xA95D3],
        ["a95d", 0xA95D],
        ["a95", 0xA95],
        ["a9", 0xA9],
        ["a", 0xA],
        ["", 0],
        ["12345678 ", 0x12345678],  # space at end
    ],
)
def test_parsing_crc(hex: str, expected: int):
    parts = [
        b"222 0 <foo@bar>\r\n"
        b"=ybegin part=1 total=1 line=128 size=12 name=helloworld\r\n"
        b"=ypart begin=1 end=12\r\n"
        b"r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK\r\n"
        b"=yend size=12 pcrc32=%s\r\n" % hex.encode(),
        b".\r\n",
    ]
    data_plain = b"".join(parts)
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)
    response = next(decoder, None)
    assert response
    assert response.crc_expected == expected


def test_no_reinitialization():
    decoder = sabctools.Decoder(0)
    with pytest.raises(RuntimeError, match="Decoder cannot be reinitialized"):
        decoder.__init__(100)


def sizeof_allocated_once(size: int) -> int:
    """sys.getsizeof of a bytearray that went through the same allocation sequence the
    decoder performs for a well-formed article, and nothing else: allocated at
    part_size + 64 by decode_yenc, then resized to bytes_decoded by Decoder_process.

    Reproducing the sequence rather than computing a capacity from the object header
    keeps this independent of how a given CPython lays out or over-allocates bytearrays.
    A buffer that was grown along the way ends up a different size."""
    reference = bytearray(size + 64)
    del reference[size:]
    return sys.getsizeof(reference)


@pytest.mark.parametrize(
    "filename",
    [
        "test_regular.yenc",
        "test_regular_2.yenc",
        "test_bad_crc.yenc",
        "test_padded_crc.yenc",
        "test_article.yenc",
    ],
)
def test_no_excess_allocation(filename: str):
    """The decoded bytearray is handed straight to the caller and kept for the lifetime
    of the article, so it must not hold on to a large over-allocation. It can never be
    given back later: PyByteArray_Resize only reallocates on a downsize below half the
    allocation, so anything allocated beyond bytes_decoded is retained permanently."""
    data_plain = read_plain_yenc_file(filename)
    input = BytesIO(data_plain)
    decoder = sabctools.Decoder(len(data_plain))
    n = input.readinto(decoder)
    decoder.process(n)

    response = next(decoder, None)
    assert response
    assert response.data
    assert sys.getsizeof(response.data) == sizeof_allocated_once(len(response.data)), filename


@pytest.mark.parametrize("buffer_size", [1024, 64 * 1024, 512 * 1024, None])
def test_no_excess_allocation_multiple_responses(buffer_size):
    """Input regularly spans several responses, so the tail of one response sits in the
    same buffer as the whole of the next. That trailing data must not drive the size of
    this response's buffer: each response is allocated once, at part_size + 64, and
    never grown."""
    filenames = ["test_regular.yenc", "test_regular_2.yenc", "test_bad_crc.yenc", "test_padded_crc.yenc"]
    payload = b"".join(read_plain_yenc_file(f) for f in filenames)

    input = BytesIO(payload)
    decoder = sabctools.Decoder(buffer_size or len(payload))
    responses = []
    while (n := input.readinto(decoder)) != 0:
        decoder.process(n)
        responses.extend(decoder)

    assert len(responses) == len(filenames)
    for response, filename in zip(responses, filenames):
        assert response.data
        assert sys.getsizeof(response.data) == sizeof_allocated_once(len(response.data)), f"{filename} was reallocated"


@pytest.mark.parametrize("split_at", range(1, 8))
def test_article_terminator_split(split_at: int):
    """The yEnc decoder consumes the ".\\r\\n" terminator itself when the body runs to the
    end of the article without a =yend line. A network read may split the input part-way
    through that terminator, in which case the leading bytes are no longer in the buffer
    and cannot be backed up over. The response must still be completed."""
    data_plain = read_plain_yenc_file("test_missing_yend.yenc")

    # Reference: the whole article in a single read
    decoder = sabctools.Decoder(len(data_plain))
    decoder.process(BytesIO(data_plain).readinto(decoder))
    expected = next(decoder, None)
    assert expected
    assert expected.data

    decoder = sabctools.Decoder(len(data_plain))
    responses = []
    for part in (data_plain[:-split_at], data_plain[-split_at:]):
        input = BytesIO(part)
        n = input.readinto(decoder)
        decoder.process(n)
        responses.extend(decoder)

    # Splitting the read must not change the outcome
    assert len(responses) == 1
    assert responses[0].status_code == expected.status_code
    assert responses[0].data == expected.data
    assert responses[0].bytes_read == expected.bytes_read == len(data_plain)
