import hashlib
from struct import pack, unpack

# C-extension is placed as submodule to allow typing
from sabctools.sabctools import *

__version__ = version


def rarfile_rar3_s2k(pwd, salt):
    """String-to-key hash for RAR3."""
    if not isinstance(pwd, str):
        pwd = pwd.decode("utf8")
    wstr = pwd.encode("utf-16le")[: 127 * 2]
    seed = bytearray(wstr + salt)
    h = hashlib.sha1()
    iv = bytearray(16)
    for i in range(16):
        iv[i] = rarfile_rar3_loop(h, seed, i << 14)
    key_be = h.digest()[:16]
    key_le = pack("<LLLL", *unpack(">LLLL", key_be))
    return key_le, bytes(iv)
