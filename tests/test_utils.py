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
