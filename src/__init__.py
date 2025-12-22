# C-extension is placed as submodule to allow typing
from sabctools.sabctools import *
from typing import Union
import os
import stat
import sys

__version__ = version


def is_sparse(path: Union[int, str, bytes, os.PathLike[str], os.PathLike[bytes]]) -> bool:
    """Check if a path is a sparse file"""
    info = os.stat(path)
    if sys.platform == "win32":
        return bool(info.st_file_attributes & stat.FILE_ATTRIBUTE_SPARSE_FILE)

    # Linux and macOS
    if info.st_blocks * 512 < info.st_size:
        return True

    # Filesystem with SEEK_HOLE (ZFS)
    try:
        with open(path, "rb") as f:
            pos = f.seek(0, os.SEEK_HOLE)
            return pos < info.st_size
    except (AttributeError, OSError):
        pass

    return False
