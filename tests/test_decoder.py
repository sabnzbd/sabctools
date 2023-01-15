import pytest
import glob
from tests.testsupport import *


def test_regular():
    data_plain = read_plain_yenc_file("test_regular.txt")
    assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)
    data_plain = read_plain_yenc_file("test_regular_2.txt")
    assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)


def test_partial():
    data_plain = read_plain_yenc_file("test_partial.txt")
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_plain)
    assert filename == "90E2Sdvsmds0801dvsmds90E.part06.rar"
    assert crc_correct is None
    assert len(decoded_data) == 549


def test_special_chars():
    data_plain = read_plain_yenc_file("test_special_chars.txt")
    # We only compare the data and the filename
    assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)


def test_bad_crc():
    data_plain = read_plain_yenc_file("test_badcrc.txt")
    # We only compare the data and the filename
    assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)


def test_bad_crc_end():
    data_plain = read_plain_yenc_file("test_bad_crc_end.txt")
    with pytest.raises(ValueError) as excinfo:
        sabyenc3_wrapper(data_plain)
    assert "Invalid CRC in footer" in str(excinfo.value)


def test_no_filename():
    data_plain = read_plain_yenc_file("test_no_name.txt")
    with pytest.raises(ValueError) as excinfo:
        sabyenc3_wrapper(data_plain)
    assert "Could not find yEnc filename" in str(excinfo.value)


def test_bad_size():
    with pytest.raises(ValueError) as excinfo:
        sabyenc3.decode_buffer(b"1234", 10)
    assert "Invalid data length" in str(excinfo.value)


def test_bad_filename_pickle():
    # This one fails in the old yEnc in different way
    data_plain = read_pickle("tests/yencfiles/split_filename")
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_plain)
    assert filename == "Low.Winter.Sun.US.S01E01.720p.BluRay.x264-DEMAND.part04.rar"
    assert crc_correct is None
    assert len(decoded_data) == 384126


def test_crc_pickles():
    all_crc_fails = glob.glob("tests/yencfiles/crc_*")
    for fname in all_crc_fails:
        data_plain = read_pickle(fname)
        assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)


def test_huge_file_pickles():
    all_pickles = glob.glob("tests/yencfiles/huge_file*")
    for fname in all_pickles:
        data_plain = read_pickle(fname)
        assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)


def test_small_file_pickles():
    all_pickles = glob.glob("tests/yencfiles/small_file*")
    for fname in all_pickles:
        data_plain = read_pickle(fname)
        assert python_yenc(data_plain) == sabyenc3_wrapper(data_plain)
