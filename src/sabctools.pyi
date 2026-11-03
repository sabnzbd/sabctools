from enum import IntEnum
from os import PathLike
from types import TracebackType
from typing import Optional, IO, Iterator, Sequence, TypedDict, Callable, Mapping
from ssl import SSLSocket
from _typeshed import ReadableBuffer, WriteableBuffer

__version__: str
openssl_linked: bool
simd: str
crc_simd: str

def yenc_encode(input_string: bytes) -> tuple[bytes, int]: ...
def unlocked_ssl_recv_into(ssl_socket: SSLSocket, buffer: WriteableBuffer) -> int: ...
def crc32_combine(crc1: int, crc2: int, length: int) -> int: ...
def crc32_multiply(crc1: int, crc2: int) -> int: ...
def crc32_xpow8n(n: int) -> int: ...
def crc32_xpown(n: int) -> int: ...
def crc32_zero_unpad(crc1: int, length: int) -> int: ...
def sparse(file: IO | int, length: int) -> None:
    """Deprecated in favour of FileWriter.preallocate, kept for existing callers."""

class SparseUnsupported(OSError):
    """The filesystem cannot store the file with holes in it."""

class WriteStats(TypedDict):
    count: int
    bytes: int
    nanos: int
    """Nanoseconds spent inside the write itself"""
    max_nanos: int
    """Nanoseconds the slowest single write took"""

def write_stats(reset: bool = False) -> WriteStats:
    """Totals for every write through every FileWriter.

    Only write() is counted, and closing a file does not subtract what it wrote. With
    reset, each total is taken and zeroed in one step, so consecutive calls carve the
    writes into intervals with none lost or counted twice.
    """

def bytearray_malloc(size: int) -> bytearray: ...
def rarfile_rar3_s2k(pwd, salt) -> tuple[bytes, bytes]: ...

class EncodingFormat(IntEnum):
    YENC = 1
    UU = 2

class Par2Result(IntEnum):
    """Return codes from Par2Repairer's methods, mirroring par2cmdline's own."""

    SUCCESS = 0
    REPAIR_POSSIBLE = 1
    REPAIR_NOT_POSSIBLE = 2
    INVALID_COMMAND_LINE_ARGUMENTS = 3
    INSUFFICIENT_CRITICAL_DATA = 4
    REPAIR_FAILED = 5
    FILE_IO_ERROR = 6
    LOGIC_ERROR = 7
    MEMORY_ERROR = 8
    CANCELLED = 9

class Par2ErrorCode(IntEnum):
    """Why an operation failed, refining the Par2Result it returned."""

    NONE = 0
    NOT_VERIFIED = 1
    INVALID_SETTING = 2
    PAR2_FILE_MISSING = 3
    MAIN_PACKET_MISSING = 4
    FILE_DESCRIPTION_MISSING = 5
    DUPLICATE_SOURCE_FILE = 6
    TOO_MANY_SOURCE_BLOCKS = 7
    FILE_OPEN_FAILED = 8
    FILE_CREATE_FAILED = 9
    FILE_RENAME_FAILED = 10
    FILE_READ_FAILED = 11
    FILE_WRITE_FAILED = 12
    OUT_OF_MEMORY = 13
    PROCESSOR_FAILED = 14
    INTERNAL_ERROR = 15
    RECOVERY_UNSOLVABLE = 16

class Par2WarningCode(IntEnum):
    """Something worth knowing which did not stop the work."""

    NONE = 0
    FILENAME_UNSAFE = 1
    """A name the set records which this system may not accept as it stands"""
    FILENAME_CHANGED = 2
    """The name a file will be written under is not the name the set records"""
    INCOMPLETE_WRITE = 3
    INCOMPLETE_READ = 4
    PACKET_DISCARDED = 5
    """A packet which disagrees with the rest of the set was left out of it"""
    RENAME_FAILED = 6
    """A complete copy under another name could not be renamed into place, so its blocks were copied there"""
    NOT_REPAIRED_IN_PLACE = 7
    """A damaged file is written as a new copy instead of in place; the message says why"""
    IN_PLACE_INCOMPLETE = 8
    """A stopped repair left a file it was repairing in place as it was"""

class Par2Error(Exception):
    """Raised when par2 fails in a way it has no return code for."""

class Par2LastError(TypedDict):
    """Why the last call failed, as reported by Par2Repairer.last_error."""

    code: Par2ErrorCode
    message: str
    """One line, without a trailing newline. May be empty."""
    filename: str
    """The absolute path of the file it concerns, empty where it concerns none"""

class Par2File(TypedDict):
    """One file of a par2 set, as reported by Par2Repairer.files."""

    name: str
    """Name recorded in the par2 set.

    The set's own bytes, which need not be UTF-8: bytes which are not come through
    as lone surrogates, as os.fsdecode() gives them, and the calls which take a name
    accept it back as it is.
    """
    target: str
    """Absolute path the file belongs at on this system"""
    size: int
    """Size of the file in bytes"""
    blocks: int
    """Number of par2 blocks this file spans, 0 if it cannot be recovered"""
    hash16k: bytes
    """MD5 of the file's first 16k, as hashlib.md5().digest() orders it.

    What identifies a file of the set whose name is not known.
    """

class Par2Repairer:
    def __init__(
        self,
        parfile: str,
        extrafiles: Sequence[str] = (),
        basepath: str = "",
        memory_limit: int = 0,
        threads: int = 0,
        file_threads: int = 0,
        skip_data: bool = False,
        skip_leaway: int = 0,
        skip_repaired_verification: bool = True,
        in_place_repair: bool = True,
    ) -> None:
        """Verify and repair a par2 set in-process.

        Nothing is read here, so a `parfile` that is not there surfaces as a load()
        result. `memory_limit` of 0 lets par2 derive one from physical memory, and an
        empty `basepath` is taken from the first par2 file loaded.

        `skip_repaired_verification` drops the pass par2 makes over the files it just
        rebuilt, but only for a set where set_known_blocks() actually shortcut the
        source scan - trusting the repair on the way out follows from having trusted
        the caller's checksums on the way in, so a set that got a real scan still gets
        its repair verified. Saves in proportion to how much was repaired, not to the
        size of the set, and gives up the only check on what reached disk; par2 still
        checksums its own GF16 computation. The reported counts are unaffected.

        `in_place_repair` writes the rebuilt blocks of a damaged file back into it rather
        than writing a new copy, and makes no backup of it. A file which cannot be repaired
        that way is written as a new copy, with NOT_REPAIRED_IN_PLACE saying why, as is
        every file while `skip_data` is on.
        """

    def load(self, volumes: bool = True) -> Par2Result:
        """Read the par2 packets and work out the file set.

        The volume files beside the named one are read too, and they carry the critical
        packets, so naming a set whose index file is missing still describes it.
        FILE_IO_ERROR means the named file is absent *and* nothing new was read.

        With `volumes` False only the named file is read, for a caller that knows which
        of the files beside it are complete and adds those with load_more().
        """

    def load_more(self, parfiles: Sequence[str]) -> int:
        """Add recovery blocks from further par2 files; returns recovery_block_count.

        Does not re-scan the data files. Once verify() has run, repair_possible says
        whether there are now enough blocks to repair, which is what makes "fetch more
        blocks and retry" cheap. Reads only the files named, not the volumes beside
        them. Requires load() first, and raises Par2Error if a named file does not
        exist.
        """

    def set_known_blocks(self, mapping: Mapping[str, Sequence[object]]) -> None:
        """Take {filename: per-block values} as what is already known of each block.

        A true value is a block intact at its own offset, taken without being read. A
        false value is a block known to be missing, neither read nor searched for, so a
        repair rebuilds it. None is a block nothing is known of, which is looked for in
        the file as usual. Files not listed are scanned normally, and an empty mapping
        restores the default behaviour. Filenames are as recorded in the par2 set, and
        each sequence runs from block 0.

        A file with every block true is treated as intact, and one with every block
        false as holding nothing usable; in both cases it is never read. A block said to
        be missing which is in fact intact costs a recovery block to rebuild.

        Call after load(), which is when block_size becomes known, and before verify().
        A repair() which goes ahead forgets them, since it may rewrite the files they
        describe.
        """

    def block_checksums(self, filename: str) -> Optional[list[int]]:
        """The CRC32 the set records for each block of that file, from block 0.

        What the par2 packets say the data should be, rather than anything read off
        disk, so it is available as soon as the packets are. par2 checksums the final
        block of a file padded out to block_size with zeroes.

        None when the set does not describe that file, which includes a file it holds
        no verification packet for and so cannot recover. Filenames are as recorded in
        the par2 set. Call after load().
        """

    def verify_file(self, filename: str) -> Par2Result:
        """Scan one file as it becomes available, reading nothing else.

        Call it again for the same file once it has finished downloading: what an
        earlier scan found for it is discarded first, so a file sitting on disk at its
        final size with data still missing can be scanned again rather than trusted.

        May be called before load(), which returns INSUFFICIENT_CRITICAL_DATA and scans
        the file once the par2 packets arrive. Returns what verify() would return for
        the set as it stands, so files not yet scanned still count as missing.
        """

    def verify(self) -> Par2Result:
        """Scan the source files. Requires load() first.

        May be called more than once; each call is a fresh pass.
        """

    def repair(self) -> Par2Result:
        """Rebuild whatever the preceding verify() found missing or damaged.

        Runs the verification pass itself when verify() was not called.
        """

    def cancel(self) -> None:
        """Ask an in-progress verify() or repair() to stop.

        Safe from another thread, including from progress_callback. The interrupted
        call returns CANCELLED, having removed any partly written files.
        """
    progress_callback: Optional[Callable[[str, str, int], None]]
    """Called as (stage, filename, percent).

    `stage` is one of "loading", "verifying", "repairing", "verifying_repair", each of
    which counts from 0 again. `filename` is set when a new
    file is opened and empty on percentage updates; `percent` is 0-100. Invoked from
    par2's worker threads, so it must be quick and must not call back into the repairer
    other than cancel() and the properties describing the set: recoverable_file_count,
    source_block_count, recovery_block_count, block_size, data_size, setid and creator.
    Exceptions raised here cannot unwind through par2 and are
    reported via sys.unraisablehook.
    """

    file_done_callback: Optional[Callable[[str, int, int], None]]
    """Called as (filename, blocks_found, blocks_total) once per file.

    blocks_found > 0 means that file contributed data to the repair, which is the only
    way to tell that joinable .001/.002 parts were consumed - par2 never reports those
    as source files. Both counts are 0 for a par2 file, which has no blocks of its own.
    Same threading rules as progress_callback.
    """

    error_callback: Optional[Callable[[Par2ErrorCode, str, str], None]]
    """Called as (code, message, filename) once per error, as par2 finds it.

    The operation may carry on and may still succeed - a file par2 could not open does
    not fail a verify - so the Par2Result says whether it mattered. Use this rather than
    last_error to see every error a parallel scan turns up. Same threading rules as
    progress_callback.
    """

    warning_callback: Optional[Callable[[Par2WarningCode, str, str], None]]
    """Called as (code, message, filename) once per warning, as par2 finds it.

    Most often a name the set records which this system will not take as it stands.
    Nothing else reports these: no outcome depends on them, so there is no property to
    read them back from afterwards. Same threading rules as progress_callback.
    """

    last_error: Optional[Par2LastError]
    """Why the last call failed, or None where it did not.

    Describes the call that returned last and the first thing that went wrong during it.
    """

    missing_block_count: int
    """Blocks that need reconstructing"""
    available_block_count: int
    """Undamaged source blocks found"""
    source_block_count: int
    """Blocks in the complete set"""
    recovery_block_count: int
    """Recovery blocks loaded from the par2 files"""
    recoverable_file_count: int
    """Files the set can recover"""
    complete_file_count: int
    """Files verified intact"""
    damaged_file_count: int
    """Files present but damaged"""
    missing_file_count: int
    """Files not found"""
    renamed_file_count: int
    """Files found under a different name"""
    block_size: int
    """Block size of the set, in bytes"""
    data_size: int
    """Total size of the recoverable files, in bytes"""
    setid: bytes
    """The par2 set id, 16 bytes, in the order the packets store it"""
    creator: str
    """The client that created the set, as its creator packet records it.

    Empty until load(), and where the set carries no creator packet. The text is
    whatever the client chose to write; bytes that are not UTF-8 are replaced.
    """
    repair_possible: bool
    """Whether enough recovery blocks are available to repair"""
    cancelled: bool
    """Whether cancel() was called"""
    quick_verified_files: int
    """How many files verify() will take from set_known_blocks() instead of reading"""
    in_place_files: list[str]
    """The files the last repair() repaired in place, by the names the set records"""
    renames: dict[str, str]
    """{path_on_disk: path_it_belongs_under} for files matched under another name.

    Both paths are absolute. Reads the same before and after repair(), and is emptied
    by the next verify().
    """
    files: list[Par2File]
    """Per-file state, in the order par2 records them"""
    backup_files: list[str]
    """Damaged files repair() renamed out of the way.

    What par2's own purge option deletes; a caller tidying up can delete or keep them.
    Files passed as `extrafiles` are never listed, even where their blocks were used.
    """

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
    """CRC of decoded data, None if does not match crc_expected, or when the body was
    streamed to a sink but its =ypart offsets were invalid and nothing was written"""
    crc_expected: Optional[int]
    """CRC is yEnc headers, None if not found"""
    block_size: int
    """The block_size given to Decoder.expect(), 0 for none"""
    block_crcs: Optional[list[int]]
    """With a block_size given to Decoder.expect(), the CRC of each piece of the yEnc body
    between multiples of it, counted from the start of the file, in order. Reported even
    when the article's CRC does not match. None without a block size, for uu, without
    valid =ypart offsets, and where the pieces could not be kept."""
    lines: Optional[list[str]]
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
    pending: tuple[object, ...]
    """Contexts of the requests still awaiting a response, oldest first"""

    def expect(self, context: object, sink: Optional[FileWriter] = None, block_size: int = 0) -> None:
        """Record that a request has been sent, so its response can be paired with it.

        `context` is returned untouched as NNTPResponse.context. Calls must be in the
        order the requests were sent.

        When `sink` is given, the decoded body is written into it at the offset the
        yEnc headers declare rather than collected into a bytearray, and the response's
        `data` is left as None. Bodies larger than the internal staging buffer are
        written in pieces. uu-encoded articles carry no offsets, so a sink is ignored
        for those and `data` is populated as usual.

        When `block_size` is given, the response's `block_crcs` holds the CRC of each
        piece of the body between multiples of it.
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

    def __init__(self, path: str | bytes | PathLike) -> None:
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
        """Set the file length, marking it sparse first where the filesystem requires it.

        Raises SparseUnsupported if the filesystem cannot. Windows knows before it acts
        and changes nothing; elsewhere it is only visible afterwards, so the length is
        left set and allocated in full.
        """

    def close(self) -> None:
        """Close the file. Idempotent, and waits for any writes still in flight."""

    def __enter__(self) -> FileWriter: ...
    def __exit__(
        self,
        exc_type: Optional[type[BaseException]],
        exc: Optional[BaseException],
        tb: Optional[TracebackType],
    ) -> None: ...
