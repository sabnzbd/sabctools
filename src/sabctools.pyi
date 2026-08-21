from enum import IntEnum
from os import PathLike
from types import TracebackType
from typing import Tuple, Optional, IO, List, Iterator, TypedDict, Union, Type
from ssl import SSLSocket
from _typeshed import ReadableBuffer, WriteableBuffer

__version__: str
openssl_linked: bool
simd: str
crc_simd: str

def yenc_encode(input_string: bytes) -> Tuple[bytes, int]: ...
def unlocked_ssl_recv_into(ssl_socket: SSLSocket, buffer: WriteableBuffer) -> int: ...
def crc32_combine(crc1: int, crc2: int, length: int) -> int: ...
def crc32_multiply(crc1: int, crc2: int) -> int: ...
def crc32_xpow8n(n: int) -> int: ...
def crc32_xpown(n: int) -> int: ...
def crc32_zero_unpad(crc1: int, length: int) -> int: ...
def sparse(file: Union[IO, int], length: int) -> None:
    """Deprecated in favour of FileWriter.preallocate, kept for existing callers."""

class WriteStats(TypedDict):
    count: int
    bytes: int
    nanos: int
    """Nanoseconds spent inside the write itself"""
    max_nanos: int
    """Nanoseconds the slowest single write took"""

def write_stats() -> WriteStats:
    """Totals for every write through every FileWriter since sabctools was imported.

    Only write() is counted, and closing a file does not subtract what it wrote.
    """

def bytearray_malloc(size: int) -> bytearray: ...
def rarfile_rar3_s2k(pwd, salt) -> tuple[bytes, bytes]: ...

class EncodingFormat(IntEnum):
    YENC = 1
    UU = 2

class NNTPResponse:
    context: Optional[object]
    """Object handed to Decoder.expect() for the request this answers"""
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
    """Decoded data, or None when it was streamed to a sink instead"""
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
    sink_failed: bool
    """A write to the sink failed, so the decoded body was discarded.

    The response is still completed and the connection is left usable - abandoning it
    mid-stream would desynchronise the byte stream - but nothing was kept, so the
    article has to be fetched again."""
    sink_error: Optional[BaseException]
    """The exception that failed write produced, held rather than raised.

    An OSError for a real disk error, carrying its errno and the file it was writing -
    a full disk arrives as ENOSPC. A ValueError when the file had simply been closed,
    which is what a deleted job looks like. None when no write failed."""

class Decoder:
    def __init__(self, size: int):
        """Initialise a decoder with the given internal buffer size."""

    def __bool__(self) -> bool: ...
    def __len__(self) -> int: ...
    def __iter__(self) -> Iterator[NNTPResponse]: ...
    def __next__(self) -> NNTPResponse: ...
    def __buffer__(self, __flags: int) -> memoryview: ...
    def __release_buffer__(self, __buffer: memoryview) -> None: ...
    expected: int
    """Requests recorded with expect() whose responses have not been decoded yet"""
    pending: Tuple[object, ...]
    """Contexts of the requests still awaiting a response, oldest first"""

    def expect(self, context: object, sink: Optional["FileWriter"] = None) -> None:
        """Record that a request has been sent, so its response can be paired with it.

        `context` is returned untouched as NNTPResponse.context. Calls must be in the
        order the requests were sent.

        When `sink` is given, the decoded body is written into it at the offset the
        yEnc headers declare rather than collected into a bytearray, and the response's
        `data` is left as None. Bodies larger than the internal staging buffer are
        written in pieces. uu-encoded articles carry no offsets, so a sink is ignored
        for those and `data` is populated as usual.
        """

    def clear_expected(self) -> None:
        """Forget every pending request, for a connection being reset."""

    def process(self, length: int) -> None:
        """Process `length` additional bytes of the internal buffer.

        The decoder maintains an internal buffer that is re-used across calls.
        Incoming data is consumed in fixed-size chunks to avoid repeatedly
        allocating large temporary buffers.

        Callers are expected to feed data from sockets or files incrementally.
        This pattern minimizes copying and wasted allocations while allowing
        streaming decode of multiple NNTP responses.
        """

class FileWriter:
    """A file opened for positional writes.

    Owns its descriptor so nothing outside can close it while a write is in flight,
    and writes at absolute offsets so several threads may write to one file at once
    without a lock. On Windows this uses WriteFile with an OVERLAPPED offset, which
    Python itself has no equivalent for: os.pwrite is Unix only.
    """

    def __init__(self, path: Union[str, bytes, PathLike]) -> None:
        """Open path for writing, creating it if it does not exist."""
    closed: bool
    path: Optional[str]
    size: int
    """Current length of the file, read from the owned handle"""

    def write(self, data: ReadableBuffer, offset: int) -> int:
        """Write all of data at an absolute offset, returning the bytes written.

        Short writes are retried internally, so the return value always equals
        len(data) unless an error was raised.
        """

    def preallocate(self, length: int) -> None:
        """Set the file length, marking it sparse first where the filesystem requires it."""

    def close(self) -> None:
        """Close the file. Idempotent, and waits for any writes still in flight."""

    def __enter__(self) -> "FileWriter": ...
    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc: Optional[BaseException],
        tb: Optional[TracebackType],
    ) -> None: ...
