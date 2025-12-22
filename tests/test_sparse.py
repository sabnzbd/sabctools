import os
import subprocess
import sys
import tempfile
import pytest
from typing import IO

from tests.testsupport import *


def test_sparse():
    file = tempfile.NamedTemporaryFile(delete=False)
    try:
        sabctools.sparse(file, 100)
        assert os.path.getsize(file.name) == 100
        assert is_sparse(file) is True
    finally:
        file.close()
        os.unlink(file.name)


def test_sparse_fd():
    file = tempfile.NamedTemporaryFile(delete=False)
    try:
        sabctools.sparse(file.fileno(), 100)
        assert os.path.getsize(file.name) == 100
        assert is_sparse(file) is True
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
def test_sparse_position_expected(length: int, position: int):
    with tempfile.TemporaryFile() as file:
        file.write(b"Hello World!")
        file.seek(position)
        sabctools.sparse(file, length)
        assert os.stat(file.name).st_size == length
        assert file.tell() == position


def is_sparse(file: IO) -> bool:
    """Check if a path is a sparse file"""
    stat = os.stat(file.name)
    if "win32" in sys.platform:
        return bool(stat.st_file_attributes & 0x200)

    # Linux and macOS
    if stat.st_blocks * 512 < stat.st_size:
        return True

    # Filesystem with SEEK_HOLE (ZFS)
    try:
        with open(file.name, "rb") as f:
            pos = f.seek(0, os.SEEK_HOLE)
            return pos < stat.st_size
    except (AttributeError, OSError):
        pass

    return False
