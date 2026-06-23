import pytest
import rarfile

from tests.testsupport import *


def test_bytearray_malloc():
    assert len(sabctools.bytearray_malloc(10)) == 10


def test_bytearray_malloc_bad_inputs():
    with pytest.raises(TypeError):
        sabctools.bytearray_malloc(10.0)
    with pytest.raises(SystemError):
        sabctools.bytearray_malloc(-1)
    with pytest.raises(TypeError):
        sabctools.bytearray_malloc("foo")


def test_rarfile_rar3sha1_corrupt_inputs():
    with pytest.raises(TypeError):
        sabctools.rarfile_rar3sha1_corrupt()
    with pytest.raises(ValueError):
        sabctools.rarfile_rar3sha1_corrupt(b"", 0)


def test_rarfile_rar3sha1_corrupt_check_hash(monkeypatch):
    monkeypatch.setattr(
        rarfile.Rar3Sha1, "_corrupt", lambda self, data, dpos: sabctools.rarfile_rar3sha1_corrupt(data, dpos)
    )
    key_le, iv = rarfile.rar3_s2k(
        "75f8c9f91969b42eaaadc389739df9ed65e8970f9ad333a146e4f73e3875b69a", b"F\xa0K\xdd\x01R-0"
    )
    assert key_le == b'\xe6\x11\x04\x8a\xa4\x7f\xd7\xde\xa7A;\x8ahu\xc1"'
    assert iv == b"\x94\x91\xa5\x16\\h\x94\xcf\xc5\x94+f^\x96\xf6\xbd"
