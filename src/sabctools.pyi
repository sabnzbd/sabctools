from enum import IntEnum
from typing import Tuple, Optional, IO, List
from ssl import SSLSocket

__version__: str
openssl_linked: bool
simd: str

def yenc_encode(input_string: bytes) -> Tuple[bytes, int]: ...
def unlocked_ssl_recv_into(ssl_socket: SSLSocket, buffer: memoryview) -> int: ...
def crc32_combine(crc1: int, crc2: int, length: int) -> int: ...
def crc32_multiply(crc1: int, crc2: int) -> int: ...
def crc32_xpow8n(n: int) -> int: ...
def crc32_xpown(n: int) -> int: ...
def crc32_zero_unpad(crc1: int, length: int) -> int: ...
def sparse(file: IO, length: int) -> None: ...
def bytearray_malloc(size: int) -> bytearray: ...

class EncodingFormat(IntEnum):
    UNKNOWN = 0
    YENC = 1
    UU = 2

class Decoder:
    eof: bool
    """End of response reached"""
    status_code: int
    """Code extracted from the first 3 characters of the response"""
    message: Optional[str]
    """The first line of the response"""
    bytes_read: int
    """Bytes consumed, including status line and yEnc headers"""
    bytes_decoded: int
    """Bytes produced"""
    file_name: Optional[str]
    file_size: int
    part_begin: int
    part_size: int
    data: Optional[bytearray]
    """Decoded data"""
    crc: Optional[int]
    """CRC of decoded data, None if does not match crc_expected"""
    crc_expected: Optional[int]
    """CRC is yEnc headers, None if not found"""
    lines: Optional[List[str]]
    """NNTP lines from multi-line responses which are not yEnc headers/data e.g. ARTICLE/HEAD/CAPABILITIES"""
    format: EncodingFormat
    """Decoding process used"""
    def decode(self, data: memoryview) -> Tuple[bool, Optional[memoryview]]:
        """Decode a buffer of NNTP response.

        The decoder maintains an internal bytearray that is grown only as needed and
        re-used across calls. Incoming data is consumed in fixed-size chunks to
        avoid repeatedly allocating large temporary buffers. The return value is a
        pair ``(eof, remaining)`` where:

        - ``eof`` is a ``bool`` indicating if the end of the response was reached.
        - ``remaining`` is a ``memoryview`` of any unprocessed bytes from ``data``,
          or ``None`` if the entire buffer was consumed.

        Callers are expected to feed data from sockets or files incrementally,
        passing any ``remaining`` bytes back into subsequent calls. This pattern
        minimizes copying and wasted allocations while allowing streaming decode
        of multiple NNTP responses.
        """
