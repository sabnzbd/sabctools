import pytest
import sabyenc3
from tests.testsupport import *

# ----- Data list part -----
def test_list_none():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(None, 1)
    assert "Expected list" in str(excinfo.value)


def test_list_str():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks("baddata", 1)
    assert "Expected list" in str(excinfo.value)


def test_list_int():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(1, 1)
    assert "Expected list" in str(excinfo.value)


def test_list_dict():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks({1: "yenc"}, 1)
    assert "Expected list" in str(excinfo.value)


# ----- Data size part -----
def test_size_none():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(["yenc1", "yenc2"], None)
    assert "integer" in str(excinfo.value)


def test_size_str():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(["yenc1", "yenc2"], "yenc")
    assert "integer" in str(excinfo.value)


def test_size_list():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(["yenc1", "yenc2"], ["yenc"])
    assert "integer" in str(excinfo.value)


def test_size_dict():
    with pytest.raises(TypeError) as excinfo:
        sabyenc3.decode_usenet_chunks(["yenc1", "yenc2"], {1: "yenc"})
    assert "integer" in str(excinfo.value)
