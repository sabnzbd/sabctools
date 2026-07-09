import pytest

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


@pytest.mark.parametrize(
    "password, salt, expected_key, expected_iv",
    [
        (
            "75f8c9f91969b42eaaadc389739df9ed65e8970f9ad333a146e4f73e3875b69a",
            "46a04bdd01522d30",
            "e611048aa47fd7dea7413b8a6875c122",
            "9491a5165c6894cfc5942b665e96f6bd",
        ),
        (
            "p" * 200,
            "1122334455667788",
            "b1bc223609af7d4f3b70e5a254ac2501",
            "302c97945530d7ffa7c551eb2dd21a90",
        ),
    ],
)
def test_rarfile_rar3_s2k_known_key_iv(password, salt, expected_key, expected_iv):
    key_le, iv = sabctools.rarfile_rar3_s2k(password, bytes.fromhex(salt))
    assert key_le.hex() == expected_key
    assert iv.hex() == expected_iv
