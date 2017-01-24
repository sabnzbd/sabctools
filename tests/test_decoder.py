import pytest
import sabyenc
import glob
from testsupport import *

def test_regular():
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt')
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)
    data_plain, data_chunks, data_bytes = read_and_split('test_regular_2.txt')
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_regular_small_chunks():
    # Chunks of 512 chars
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt', 9)
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)
    data_plain, data_chunks, data_bytes = read_and_split('test_regular_2.txt', 9)
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)
    # Chunks of 256 chars
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt', 8)
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_single_part():
    data_plain, data_chunks, data_bytes = read_and_split('test_single_part.txt')
    decoded_data, filename, crc_correct = sabyenc_wrapper(data_chunks, data_bytes)
    assert filename == 'logo.gif'
    assert crc_correct == True
    assert len(decoded_data) == 16335

def test_partial():
    data_plain, data_chunks, data_bytes = read_and_split('test_partial.txt')
    decoded_data, filename, crc_correct = sabyenc_wrapper(data_chunks, data_bytes)
    assert filename == '90E2Sdvsmds0801dvsmds90E.part06.rar'
    assert crc_correct == False
    assert len(decoded_data) == 587

def test_special_chars():
    data_plain, data_chunks, data_bytes = read_and_split('test_special_chars.txt')
    # We only compare the data and the filename
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_bad_crc():
    data_plain, data_chunks, data_bytes = read_and_split('test_badcrc.txt')
    # We only compare the data and the filename
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_false_bytes():
    # What if a false number of bytes is passed?
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt')
    decoded_data, filename, crc_correct = sabyenc_wrapper(data_chunks, 10)
    # No error should be thrown
    assert filename == '90E2Sdvsmds0801dvsmds90E.part06.rar'
    assert crc_correct == False

def test_yend_bad_1():
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt')
    # Test if "=" and "yend" is split over multiple lines
    last_bits = data_chunks[-1].split('yend')
    last_bits[1] = 'yend' + last_bits[1]
    del data_chunks[-1]
    data_chunks.extend(last_bits)
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_yend_bad_2():
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt')
    # Test if "=ye" and "nd" is split over multiple lines
    last_bits = data_chunks[-1].split('nd ')
    last_bits[1] = 'nd ' + last_bits[1]
    del data_chunks[-1]
    data_chunks.extend(last_bits)
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_crc_split():
    data_plain, data_chunks, data_bytes = read_and_split('test_regular.txt')
    # Test if the CRC-code is split over multiple lines
    last_bits = data_chunks[-1].split('4e1')
    last_bits[1] = '4e1' + last_bits[1]
    del data_chunks[-1]
    data_chunks.extend(last_bits)
    assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)

def test_no_filename():
    data_plain, data_chunks, data_bytes = read_and_split('test_no_name.txt')
    with pytest.raises(ValueError) as excinfo:
        sabyenc_wrapper(data_chunks, data_bytes)
    assert 'Could not get filename' in str(excinfo.value)

def test_crc_picles():
    all_crc_fails = glob.glob('tests/yencfiles/crc_*')
    for fname in all_crc_fails:
        data_plain, data_chunks, data_bytes = read_pickle(fname)
        assert old_yenc(data_plain) == sabyenc_wrapper(data_chunks, data_bytes)




