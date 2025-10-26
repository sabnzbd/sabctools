import io
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

def test_streaming():
    decoder = sabctools.Decoder()

    BUFFER_SIZE = 4096
    buffer = bytearray(BUFFER_SIZE)
    buffer_view = memoryview(buffer)
    buffer_remaining = 0  # unprocessed contents of the buffer

    expected = [
        { "crc": "e83e50e7", "file_name": "Applideck Revenue 980788779079648.z12" },
        { "crc": "31963516", "file_name": "Hi Kingdom 你好世界.txt" }  # doesn't end with .\r\n
    ]
    responses = 0

    # Read in chunks like a network
    f = io.BytesIO(read_plain_yenc_file("test_regular_2.yenc") + read_plain_yenc_file("test_special_utf8_chars.yenc") + b".\r\n")
    while True:
        buffer_slice = buffer_view
        read_bytes = f.readinto(buffer_slice[buffer_remaining:])
        if read_bytes == 0 and buffer_remaining == 0:
            print("Not done but no more data...")
            break

        # Need to expose a way to see the status code, can do buffer_view[:3] if decode hasn't been called yet
        # Because we need to handle non 2xx responses the decoder could handle that itself
        
        buffer_slice = buffer_slice[:read_bytes+buffer_remaining]
        done, buffer_remaining = decoder.decode(buffer_slice)

        if done:
            assert int(expected[responses]["crc"], 16) == decoder.crc_expected
            assert int(expected[responses]["crc"], 16) == decoder.crc
            assert expected[responses]["file_name"] == decoder.file_name
            responses += 1
            if responses == len(expected):
                break
            decoder = sabctools.Decoder()

    assert responses == 2
                
