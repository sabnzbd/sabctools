import errno
import os
import subprocess
import sys
import tempfile
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


def is_sparse(file: IO) -> bool:
    """Is the file sparse?
    On Windows this closes the file"""
    if sys.platform == "win32":
        file.close()
        return b"This file is set as sparse" in subprocess.run(
            ["fsutil", "sparse", "queryflag", file.name],
            capture_output=True
        ).stdout

    # Starts with a hole
    if file.seek(0, os.SEEK_HOLE) != 0:
        return False

    # An empty sparse file should have no data
    try:
        file.seek(0, os.SEEK_DATA)
    except OSError as e:
        if e.errno == errno.ENXIO:
            return True

    return False
