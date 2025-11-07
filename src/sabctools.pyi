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

class DecodingStatus(IntEnum):
    NOT_FINISHED = 0
    """End of response not reached, need more data"""
    SUCCESS = 1
    """
    Successful command response
    For article requests size, crc, and filename are valid
    """
    NO_DATA = 2
    """Response has no binary data"""
    INVALID_SIZE = 3
    """Decoded data length does not match yEnc headers"""
    INVALID_CRC = 4
    """CRC does not match yEnc headers"""
    INVALID_FILENAME = 5
    """Filename not found in yEnc/UU headers"""
    NOT_FOUND = 6
    """Article not found, server responded 410 to 439"""
    FAILED = 7
    """Command failed: check status_code"""
    AUTH = 8
    """Authentication related response"""
    UNKNOWN = 99
    """Check status_code and handle appropriately"""

class Decoder:
    status_code: int
    bytes_read: int
    """Bytes consumed, including status line and yEnc headers"""
    file_name: Optional[str]
    file_size: int
    part_begin: int
    part_size: int
    data: memoryview
    """Decoded data an alternative to memoryview(decoder)"""
    crc: Optional[int]
    """CRC of decoded data, None if does not match crc_expected"""
    crc_expected: Optional[int]
    """CRC is yEnc headers, None if not found"""
    lines: Optional[List[str]]
    """NNTP lines from multi-line responses which are not yEnc headers/data e.g. ARTICLE/HEAD/CAPABILITIES"""
    format: EncodingFormat
    """Decoding process used"""
    status: DecodingStatus
    """Completed decoding result """
    def decode(self, data: memoryview) -> Tuple[DecodingStatus, memoryview]: ...
    def __buffer__(self, __flags: int) -> memoryview: ...
    def __release_buffer__(self, __buffer: memoryview) -> None: ...
