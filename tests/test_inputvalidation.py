import pytest
import sys
import sabyenc3
from tests.testsupport import *


def test_simd_version():
    # Windows and macOS systems always have some form of SIMD
    if sys.platform == "darwin" or sys.platform == "win32":
        assert sabyenc3.simd


def test_list_none():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(None)
    assert "Expected list" in str(excinfo.value)


def test_list_str():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks("baddata")
    assert "Expected list" in str(excinfo.value)


def test_list_int():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(1)
    assert "Expected list" in str(excinfo.value)


def test_list_dict():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks({1: "yenc"})
    assert "Expected list" in str(excinfo.value)
