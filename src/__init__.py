import hashlib
from struct import pack, unpack

# C-extension is placed as submodule to allow typing
from sabctools.sabctools import (
    Decoder,
    EncodingFormat,
    FileWriter,
    NNTPResponse,
    SparseUnsupported,
    bytearray_malloc,
    crc32_combine,
    crc32_multiply,
    crc32_xpow8n,
    crc32_xpown,
    crc32_zero_unpad,
    crc_simd,
    openssl_linked,
    rarfile_rar3_loop,
    simd,
    sparse,
    unlocked_ssl_recv_into,
    version,
    write_stats,
    yenc_encode,
)

__version__ = version

__all__ = [
    "Decoder",
    "EncodingFormat",
    "FileWriter",
    "NNTPResponse",
    "SparseUnsupported",
    "bytearray_malloc",
    "crc32_combine",
    "crc32_multiply",
    "crc32_xpow8n",
    "crc32_xpown",
    "crc32_zero_unpad",
    "crc_simd",
    "openssl_linked",
    "rarfile_rar3_s2k",
    "simd",
    "sparse",
    "unlocked_ssl_recv_into",
    "write_stats",
    "yenc_encode",
    "__version__",
]


def rarfile_rar3_s2k(pwd, salt):
    """String-to-key hash for RAR3."""
    rar_max_password = 127
    if not isinstance(pwd, str):
        pwd = pwd.decode("utf8")
    wstr = pwd.encode("utf-16le")[: rar_max_password * 2]
    seed = bytearray(wstr + salt)
    h = hashlib.sha1()
    iv = bytearray(16)
    for i in range(16):
        iv[i] = rarfile_rar3_loop(h, seed, i << 14)
    key_be = h.digest()[:16]
    key_le = pack("<LLLL", *unpack(">LLLL", key_be))
    return key_le, bytes(iv)
