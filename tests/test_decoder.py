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
    # Test regular case
    data_plain = read_plain_yenc_file("test_regular.yenc")
    data_out, filename, filesize, begin, end, crc_correct = sabctools_yenc_wrapper(data_plain)

    assert sys.getrefcount(data_plain) == 2
    assert sys.getrefcount(data_out) == 2
    assert sys.getrefcount(filename) == 2
    assert sys.getrefcount(begin) == 2
    assert sys.getrefcount(end) == 2
    assert sys.getrefcount(crc_correct) == 2

    # Test simple error case
    fake_inp = memoryview(bytearray(b"1234"))
    assert sys.getrefcount(fake_inp) == 2
    with pytest.raises(ValueError):
        sabctools.yenc_decode(fake_inp)
    assert sys.getrefcount(fake_inp) == 2

    # Test further processing
    data_plain = read_plain_yenc_file("test_bad_crc_end.yenc")
    with pytest.raises(ValueError):
        sabctools_yenc_wrapper(data_plain)
    assert sys.getrefcount(data_plain) == 2


def test_crc_pickles():
    all_crc_fails = glob.glob("tests/yencfiles/crc_*")
    for fname in all_crc_fails:
        data_plain = read_pickle(fname)
        assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_small_file_pickles():
    all_pickles = glob.glob("tests/yencfiles/small_file*")
    for fname in all_pickles:
        data_plain = read_pickle(fname)
        assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)
