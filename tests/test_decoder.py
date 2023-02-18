import sys
import pytest
import glob
from tests.testsupport import *


def test_regular():
    data_plain = read_plain_yenc_file("test_regular.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)
    data_plain = read_plain_yenc_file("test_regular_2.yenc")
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)


def test_partial():
    data_plain = read_plain_yenc_file("test_partial.yenc")
    decoded_data, filename, crc_correct = sabctools_yenc_wrapper(data_plain)
    assert filename == "90E2Sdvsmds0801dvsmds90E.part06.rar"
    assert crc_correct is None
    assert len(decoded_data) == 549


def test_special_chars():
    data_plain = read_plain_yenc_file("test_special_chars.yenc")
    # We only compare the data and the filename
    assert python_yenc(data_plain) == sabctools_yenc_wrapper(data_plain)

    data_plain = bytearray(
        b"=ybegin part=1 total=1 line=128 size=6 name=Hi Kingdom \xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c.yenc\r\n=ypart begin=1 end=6\r\nr\x8f\x96\x96\x994\r\n=yend size=6 part=1 pcrc32=31963516 crc32=31963516\r\n"
    )
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


def test_end_after_filename():
    data_plain = read_plain_yenc_file("test_end_after_filename.yenc")
    with pytest.raises(ValueError):
        sabctools_yenc_wrapper(data_plain)


def test_bad_size():
    with pytest.raises(ValueError) as excinfo:
        sabctools.yenc_decode(bytearray())
    assert "Invalid data length" in str(excinfo.value)


def test_ref_counts():
    """Note that sys.getrefcount itself adds another reference!"""
    # Test regular case
    data_plain = read_plain_yenc_file("test_regular.yenc")
    data_out, filename, crc_correct = sabctools_yenc_wrapper(data_plain)
    # data_plain and data_out point to the same data!
    assert sys.getrefcount(data_plain) == 3
    assert sys.getrefcount(data_out) == 3
    assert sys.getrefcount(filename) == 2
    assert sys.getrefcount(crc_correct) == 2

    # Test simple error case
    fake_inp = bytearray(b"1234")
    assert sys.getrefcount(fake_inp) == 2
    with pytest.raises(ValueError):
        sabctools.yenc_decode(fake_inp)
    assert sys.getrefcount(fake_inp) == 2

    # Test further processing
    data_plain = read_plain_yenc_file("test_bad_crc_end.yenc")
    with pytest.raises(ValueError):
        sabctools_yenc_wrapper(data_plain)
    assert sys.getrefcount(data_plain) == 2


def test_bad_filename_pickle():
    # This one fails in the old yEnc in different way
    data_plain = read_pickle("tests/yencfiles/split_filename.pickle")
    decoded_data, filename, crc_correct = sabctools_yenc_wrapper(data_plain)
    assert filename == "Low.Winter.Sun.US.S01E01.720p.BluRay.x264-DEMAND.part04.rar"
    assert crc_correct is None
    assert len(decoded_data) == 384126


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
