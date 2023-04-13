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

@pytest.mark.parametrize(
    "length,position",
    [
        (1024, 0),
        (1024, 512),
        (1024, 4096),
    ],
)
def test_sparse_position_expected(length, position):
    with tempfile.TemporaryFile() as file:
        file.seek(position)
        sabctools.sparse(file, length)
        assert file.tell() == position

def is_sparse(file: IO) -> bool:
    """Is the file sparse?
    On Windows this closes the file"""
    if sys.platform == "win32":
        file.close()
        return b"This file is set as sparse" in subprocess.run(
            ["fsutil", "sparse", "queryflag", file.name],
            capture_output=True
        ).stdout

    return os.stat(file.name).st_blocks * 512 < os.path.getsize(file.name)
