import os
import sys
import tempfile
import pytest

from tests.testsupport import *


def test_allocate_file():
    file = tempfile.NamedTemporaryFile(delete=False)
    try:
        sabctools.allocate_file(file, 100)
        assert os.path.getsize(file.name) == 100
        assert sabctools.is_sparse(file.name) is True
    finally:
        file.close()
        os.unlink(file.name)


def test_allocate_file_fd():
    file = tempfile.NamedTemporaryFile(delete=False)
    try:
        sabctools.allocate_file(file.fileno(), 100)
        assert os.path.getsize(file.name) == 100
        assert sabctools.is_sparse(file.name) is True
    finally:
        file.close()
        os.unlink(file.name)


@pytest.mark.skipif(not sys.platform.startswith("win"), reason="Windows tests")
def test_allocate_file_disable_sparse_flag_windows():
    """When sparse=False on Windows, file should not be marked sparse."""
    file = tempfile.NamedTemporaryFile(delete=False)
    try:
        sabctools.allocate_file(file.fileno(), 100, sparse=False)
        assert os.path.getsize(file.name) == 100
        assert sabctools.is_sparse(file.name) is False
    finally:
        file.close()
        os.unlink(file.name)


@pytest.mark.parametrize(
    "length,position",
    [
        (1024, 0),
        (1024, 512),
        (1024, 4096),
    ],
)
def test_allocate_file_position_expected(length: int, position: int):
    with tempfile.TemporaryFile() as file:
        file.write(b"Hello World!")
        file.seek(position)
        sabctools.allocate_file(file, length)
        assert os.stat(file.name).st_size == length
        assert file.tell() == position


@pytest.mark.parametrize(
    "length",
    [
        1024,
    ],
)
def test_allocate_file_preallocate(length: int):
    with tempfile.TemporaryFile() as file:
        file.write(b"Hello World!")
        sabctools.allocate_file(file, length, preallocate=True)
        assert os.stat(file.name).st_size == length
