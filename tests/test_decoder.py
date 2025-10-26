from pathlib import Path
import sys
import pytest
import glob
from tests.testsupport import *


def test_regular():
    data_plain = read_plain_yenc_file("test_regular.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)
    data_plain = read_plain_yenc_file("test_regular_2.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_bytes_compat():
    data_plain = read_plain_yenc_file("test_regular.yenc")
    assert python_yenc(data_plain) == sabctools.yenc_decode(memoryview(bytes(data_plain)))


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
    with pytest.raises(ValueError) as excinfo:
        sabctools_yenc_wrapper(data_plain)
    assert "Invalid CRC in footer" in str(excinfo.value)


def test_no_filename():
    data_plain = read_plain_yenc_file("test_no_name.yenc")
    with pytest.raises(ValueError) as excinfo:
        sabctools_yenc_wrapper(data_plain)
    assert "Could not find yEnc filename" in str(excinfo.value)


def test_padded_crc():
    data_plain = read_plain_yenc_file("test_padded_crc.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_end_after_filename():
    data_plain = read_plain_yenc_file("test_end_after_filename.yenc")
    with pytest.raises(ValueError):
        sabctools_yenc_wrapper(data_plain)


def test_empty():
    with pytest.raises(ValueError) as excinfo:
        sabctools.yenc_decode(memoryview(bytearray(b"")))
    assert "Invalid data length" in str(excinfo.value)


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

    # Test simple error case
    fake_inp = memoryview(bytearray(b"1234"))
    assert sys.getrefcount(fake_inp) == expected_refcount
    with pytest.raises(ValueError):
        sabctools.yenc_decode(fake_inp)
    assert sys.getrefcount(fake_inp) == expected_refcount

    # Test further processing
    data_plain = read_plain_yenc_file("test_bad_crc_end.yenc")
    with pytest.raises(ValueError):
        sabctools_yenc_wrapper(data_plain)
    assert sys.getrefcount(data_plain) == expected_refcount


def test_crc_yenc_files():
    all_crc_fails = glob.glob("tests/yencfiles/crc_*.yenc")
    for fname in all_crc_fails:
        data_plain = read_plain_yenc_file(Path(fname).name)
        assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_small_file_yenc_files():
    all_small_files = glob.glob("tests/yencfiles/small_file*.yenc")
    for fname in all_small_files:
        data_plain = read_plain_yenc_file(Path(fname).name)
        assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)
