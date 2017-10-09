import pytest
import glob
from tests.testsupport import *


def test_regular():
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt")
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)
    data_plain, data_chunks, data_bytes = read_and_split("test_regular_2.txt")
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_regular_small_chunks():
    # Chunks of 512 chars
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt", 9)
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)
    data_plain, data_chunks, data_bytes = read_and_split("test_regular_2.txt", 9)
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)
    # Chunks of 256 chars
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt", 8)
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_single_part():
    data_plain, data_chunks, data_bytes = read_and_split("test_single_part.txt")
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_chunks, data_bytes)
    assert filename == "logo.gif"
    assert crc_correct == True
    assert len(decoded_data) == 16335


def test_partial():
    data_plain, data_chunks, data_bytes = read_and_split("test_partial.txt")
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_chunks, data_bytes)
    assert filename == "90E2Sdvsmds0801dvsmds90E.part06.rar"
    assert crc_correct == False
    assert len(decoded_data) == 587


def test_special_chars():
    data_plain, data_chunks, data_bytes = read_and_split("test_special_chars.txt")
    # We only compare the data and the filename
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_bad_crc():
    data_plain, data_chunks, data_bytes = read_and_split("test_badcrc.txt")
    # We only compare the data and the filename
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_false_bytes():
    # What if a false number of bytes is passed?
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt")
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_chunks, 10)
    # No error should be thrown
    assert filename == "90E2Sdvsmds0801dvsmds90E.part06.rar"
    assert crc_correct == False


def test_yend_bad_1():
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt")
    # Test if "=" and "yend" is split over multiple lines
    last_bits = data_chunks[-1].split(b"yend")
    last_bits[1] = b"yend" + last_bits[1]
    del data_chunks[-1]
    data_chunks.extend(last_bits)
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_yend_bad_2():
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt")
    # Test if "=ye" and "nd" is split over multiple lines
    last_bits = data_chunks[-1].split(b"nd ")
    last_bits[1] = b"nd " + last_bits[1]
    del data_chunks[-1]
    data_chunks.extend(last_bits)
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_crc_split():
    data_plain, data_chunks, data_bytes = read_and_split("test_regular.txt")
    # Test if the CRC-code is split over multiple lines
    last_bits = data_chunks[-1].split(b"4e1")
    last_bits[1] = b"4e1" + last_bits[1]
    del data_chunks[-1]
    data_chunks.extend(last_bits)
    assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_no_filename():
    data_plain, data_chunks, data_bytes = read_and_split("test_no_name.txt")
    with pytest.raises(ValueError) as excinfo:
        sabyenc3_wrapper(data_chunks, data_bytes)
    assert "Could not get filename" in str(excinfo.value)


def test_bad_filename_pickle():
    # This one fails in the old yEnc in different way
    data_plain, data_chunks, data_bytes = read_pickle("tests/yencfiles/split_filename")
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_chunks, data_bytes)
    assert filename == "Low.Winter.Sun.US.S01E01.720p.BluRay.x264-DEMAND.part04.rar"
    assert crc_correct == False
    assert len(decoded_data) == 384000


def test_crc_pickles():
    all_crc_fails = glob.glob("tests/yencfiles/crc_*")
    for fname in all_crc_fails:
        data_plain, data_chunks, data_bytes = read_pickle(fname)
        assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_empty_size_pickles():
    # When article size is left empty, it should not result in segfaults!
    data_plain, data_chunks, data_bytes = read_pickle(
        "tests/yencfiles/emptysize_67caae212"
    )
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_chunks, 0)
    assert filename == "Jake.and.the.Never.Land.Pirates.S02E38.480p.hdtv.x264.r05"
    assert crc_correct == True
    assert len(decoded_data) == 384000

    # Or when it's an invalid number
    decoded_data, filename, crc_correct = sabyenc3_wrapper(data_chunks, -1)
    assert filename == "Jake.and.the.Never.Land.Pirates.S02E38.480p.hdtv.x264.r05"
    assert crc_correct == True
    assert len(decoded_data) == 384000


def test_huge_file_pickles():
    all_crc_fails = glob.glob("tests/yencfiles/huge_file*")
    for fname in all_crc_fails:
        data_plain, data_chunks, data_bytes = read_pickle(fname)
        assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)


def test_small_file_pickles():
    all_crc_fails = glob.glob("tests/yencfiles/small_file*")
    for fname in all_crc_fails:
        data_plain, data_chunks, data_bytes = read_pickle(fname)
        assert old_yenc(data_plain) == sabyenc3_wrapper(data_chunks, data_bytes)
