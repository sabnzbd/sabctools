from enum import IntEnum
from typing import Tuple, Optional, IO, List, Iterator, Union
from ssl import SSLSocket
from _typeshed import WriteableBuffer

__version__: str
openssl_linked: bool
simd: str

def yenc_encode(input_string: bytes) -> Tuple[bytes, int]: ...
def unlocked_ssl_recv_into(ssl_socket: SSLSocket, buffer: WriteableBuffer) -> int: ...
def crc32_combine(crc1: int, crc2: int, length: int) -> int: ...
def crc32_multiply(crc1: int, crc2: int) -> int: ...
def crc32_xpow8n(n: int) -> int: ...
def crc32_xpown(n: int) -> int: ...
def crc32_zero_unpad(crc1: int, length: int) -> int: ...
def sparse(file: Union[IO, int], length: int) -> None: ...
def bytearray_malloc(size: int) -> bytearray: ...

class EncodingFormat(IntEnum):
    YENC = 1
    UU = 2

class NNTPResponse:
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
    part_end: int
    part_size: int
    end_size: int
    data: Optional[bytearray]
    """Decoded data"""
    crc: Optional[int]
    """CRC of decoded data, None if does not match crc_expected"""
    crc_expected: Optional[int]
    """CRC is yEnc headers, None if not found"""
    lines: Optional[List[str]]
    """NNTP lines from multi-line responses which are not yEnc headers/data e.g. ARTICLE/HEAD/CAPABILITIES"""
    format: Optional[EncodingFormat]
    """Decoding process used"""
    baddata: bool
    """Invalid UU lines were encountered, some data was lost"""

class Decoder:
    def __init__(self, size: int):
        """Initialise a decoder with the given internal buffer size."""

    def __iter__(self) -> Iterator[NNTPResponse]: ...
    def __next__(self) -> NNTPResponse: ...
    def __buffer__(self, __flags: int) -> memoryview: ...
    def __release_buffer__(self, __buffer: memoryview) -> None: ...
    def process(self, length: int) -> None:
        """Process `length` additional bytes of the internal buffer.

        The decoder maintains an internal buffer that is re-used across calls.
        Incoming data is consumed in fixed-size chunks to avoid repeatedly
        allocating large temporary buffers.

        Callers are expected to feed data from sockets or files incrementally.
        This pattern minimizes copying and wasted allocations while allowing
        streaming decode of multiple NNTP responses.
        """
