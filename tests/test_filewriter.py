import os
import sys
import threading
import pytest

from tests.testsupport import *


def is_sparse(path: str) -> bool:
    """Check if a path is a sparse file"""
    stat = os.stat(path)
    if "win32" in sys.platform:
        return bool(stat.st_file_attributes & 0x200)

    # Linux and macOS
    if stat.st_blocks * 512 < stat.st_size:
        return True

    # Filesystem with SEEK_HOLE (ZFS)
    try:
        with open(path, "rb") as f:
            return f.seek(0, os.SEEK_HOLE) < stat.st_size
    except (AttributeError, OSError):
        pass

    return False


@pytest.fixture
def target(tmp_path):
    return str(tmp_path / "target.bin")


class TestOpen:
    def test_creates_a_missing_file(self, target):
        assert not os.path.exists(target)
        with sabctools.FileWriter(target) as writer:
            assert writer.path == target
            assert writer.closed is False
        assert os.path.exists(target)

    def test_does_not_truncate_an_existing_file(self, target):
        """Articles arrive across restarts and retries, so opening must never discard
        what has already been written"""
        with open(target, "wb") as existing:
            existing.write(b"already here")

        with sabctools.FileWriter(target) as writer:
            writer.write(b"X", 0)

        assert open(target, "rb").read() == b"Xlready here"

    def test_accepts_pathlike(self, tmp_path):
        writer = sabctools.FileWriter(tmp_path / "pathlike.bin")
        writer.close()
        assert os.path.exists(tmp_path / "pathlike.bin")

    def test_accepts_bytes(self, target):
        writer = sabctools.FileWriter(os.fsencode(target))
        writer.close()
        assert os.path.exists(target)

    def test_missing_directory_raises_oserror(self, tmp_path):
        with pytest.raises(OSError) as error:
            sabctools.FileWriter(str(tmp_path / "no_such_dir" / "file.bin"))
        # The path belongs in the error or it is useless for diagnosis
        assert "file.bin" in str(error.value)

    def test_reinitialising_is_refused(self, target, tmp_path):
        """Re-running __init__ would strand the first descriptor with no way to close it"""
        writer = sabctools.FileWriter(target)
        try:
            with pytest.raises(RuntimeError):
                writer.__init__(str(tmp_path / "other.bin"))
        finally:
            writer.close()


class TestWrite:
    def test_writes_at_an_absolute_offset(self, target):
        with sabctools.FileWriter(target) as writer:
            assert writer.write(b"world", 6) == 5
            assert writer.write(b"hello ", 0) == 6
        assert open(target, "rb").read() == b"hello world"

    def test_returns_the_full_length(self, target):
        payload = os.urandom(1024 * 1024)
        with sabctools.FileWriter(target) as writer:
            assert writer.write(payload, 0) == len(payload)
        assert open(target, "rb").read() == payload

    def test_accepts_any_buffer(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.write(bytearray(b"aaaa"), 0)
            writer.write(memoryview(bytearray(b"0123456789"))[4:8], 4)
        assert open(target, "rb").read() == b"aaaa4567"

    def test_empty_write_is_allowed(self, target):
        with sabctools.FileWriter(target) as writer:
            assert writer.write(b"", 0) == 0
        assert os.path.getsize(target) == 0

    def test_writing_past_the_end_leaves_a_hole(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.write(b"end", 4096)
        assert os.path.getsize(target) == 4099
        assert open(target, "rb").read()[:4096] == b"\0" * 4096

    def test_negative_offset_is_refused(self, target):
        with sabctools.FileWriter(target) as writer:
            with pytest.raises(ValueError):
                writer.write(b"x", -1)

    def test_rejects_a_text_argument(self, target):
        with sabctools.FileWriter(target) as writer:
            with pytest.raises(TypeError):
                writer.write("not bytes", 0)


class TestPreallocate:
    def test_sets_the_length(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.preallocate(1024 * 1024)
        assert os.path.getsize(target) == 1024 * 1024

    def test_the_file_is_sparse(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.preallocate(64 * 1024 * 1024)
            assert is_sparse(target) is True

    def test_writes_land_inside_a_preallocated_file(self, target):
        """Direct write preallocates, then fills the holes as articles arrive"""
        with sabctools.FileWriter(target) as writer:
            writer.preallocate(8192)
            writer.write(b"tail", 8188)
            writer.write(b"head", 0)
        assert os.path.getsize(target) == 8192
        contents = open(target, "rb").read()
        assert contents[:4] == b"head"
        assert contents[8188:] == b"tail"

    def test_what_was_already_written_survives(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.write(b"head", 0)
            writer.preallocate(64 * 1024 * 1024)
            assert writer.size == 64 * 1024 * 1024
        assert open(target, "rb").read(4) == b"head"
        assert is_sparse(target) is True

    def test_it_can_shrink(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.preallocate(1024 * 1024)
            writer.preallocate(4096)
            assert writer.size == 4096

    def test_negative_length_is_refused(self, target):
        with sabctools.FileWriter(target) as writer:
            with pytest.raises(ValueError):
                writer.preallocate(-1)

    def test_rejects_a_non_integer(self, target):
        with sabctools.FileWriter(target) as writer:
            with pytest.raises(TypeError):
                writer.preallocate("big")


class TestClose:
    def test_close_is_idempotent(self, target):
        writer = sabctools.FileWriter(target)
        writer.close()
        writer.close()
        assert writer.closed is True

    def test_write_after_close_raises(self, target):
        writer = sabctools.FileWriter(target)
        writer.close()
        with pytest.raises(ValueError):
            writer.write(b"x", 0)

    def test_preallocate_after_close_raises(self, target):
        writer = sabctools.FileWriter(target)
        writer.close()
        with pytest.raises(ValueError):
            writer.preallocate(1024)

    def test_entering_a_closed_writer_raises(self, target):
        writer = sabctools.FileWriter(target)
        writer.close()
        with pytest.raises(ValueError):
            with writer:
                pass

    def test_exit_closes_after_an_exception(self, target):
        writer = sabctools.FileWriter(target)
        with pytest.raises(ZeroDivisionError):
            with writer:
                1 / 0
        assert writer.closed is True

    def test_dropping_the_last_reference_closes(self, target):
        """Descriptors are per open file, so leaking one per job would exhaust them"""
        before = descriptor_count()
        for _ in range(50):
            sabctools.FileWriter(target).write(b"x", 0)
        assert descriptor_count() <= before + 5


def descriptor_count() -> int:
    """Open descriptors for this process, or 0 where that cannot be counted"""
    try:
        return len(os.listdir("/dev/fd"))
    except OSError:
        return 0


class TestConcurrency:
    """Concurrent positional writes are the reason this exists. os.pwrite gives them on
    Unix but does not exist on Windows, so SABnzbd holds a lock there instead."""

    def test_threads_write_to_one_file_without_interleaving(self, target):
        chunk = 64 * 1024
        threads_count = 8
        payloads = [bytes([index + 1]) * chunk for index in range(threads_count)]

        with sabctools.FileWriter(target) as writer:
            writer.preallocate(chunk * threads_count)
            barrier = threading.Barrier(threads_count)

            def write(index):
                barrier.wait()
                for _ in range(20):
                    writer.write(payloads[index], index * chunk)

            threads = [threading.Thread(target=write, args=(index,)) for index in range(threads_count)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()

        contents = open(target, "rb").read()
        assert len(contents) == chunk * threads_count
        for index in range(threads_count):
            assert contents[index * chunk : (index + 1) * chunk] == payloads[index]

    def test_close_waits_for_writes_in_flight(self, target):
        """close() takes the lock exclusively, so a write already inside the C call
        finishes rather than having the descriptor pulled out from under it.

        The writer loops until told to stop rather than for a fixed count, so close()
        is guaranteed to land while a write is actually in progress. A fixed count
        races the close and usually finishes first, testing nothing.
        """
        payload = os.urandom(4 * 1024 * 1024)
        writer = sabctools.FileWriter(target)
        errors = []
        started = threading.Event()
        stop = threading.Event()
        writes = []

        def write_repeatedly():
            try:
                while not stop.is_set():
                    writer.write(payload, 0)
                    writes.append(1)
                    started.set()
            except ValueError:
                # Closed underneath us: allowed, and the point is that it arrives as a
                # clean error rather than a write into a reused descriptor
                pass
            except Exception as err:
                errors.append(err)

        thread = threading.Thread(target=write_repeatedly)
        thread.start()
        assert started.wait(10), "writer thread never got going"
        writer.close()
        stop.set()
        thread.join(30)

        assert not thread.is_alive(), "close() deadlocked against a write in flight"
        assert not errors
        assert writes, "no writes were actually issued"
        assert writer.closed is True

    def test_closing_twice_from_threads_is_safe(self, target):
        writer = sabctools.FileWriter(target)
        threads = [threading.Thread(target=writer.close) for _ in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        assert writer.closed is True


class TestReferenceCounting:
    def test_write_does_not_leak_the_buffer(self, target):
        payload = bytearray(b"x" * 1024)
        with sabctools.FileWriter(target) as writer:
            before = sys.getrefcount(payload)
            for _ in range(100):
                writer.write(payload, 0)
            assert sys.getrefcount(payload) == before

    def test_path_does_not_leak(self, target):
        """The getter hands out a new reference each time, so a missing decref would
        show up as growth proportional to the number of accesses.

        Deliberately not asserting the count is unchanged. The path string is also held
        by the fixture, by tmp_path's own bookkeeping and by whatever else the
        environment keeps, and how many of those exist differs between platforms and
        pytest versions; a one-off difference there says nothing about this getter. A
        leak would add a reference per access, so only growth that scales matters.
        """
        accesses = 1000
        writer = sabctools.FileWriter(target)
        try:
            path = writer.path
            before = sys.getrefcount(path)
            for _ in range(accesses):
                writer.path
            assert sys.getrefcount(path) - before < accesses // 10
        finally:
            writer.close()


class TestAccessors:
    def test_size_reports_the_current_length(self, target):
        with sabctools.FileWriter(target) as writer:
            assert writer.size == 0
            writer.write(b"x" * 1000, 0)
            assert writer.size == 1000
            writer.preallocate(8192)
            assert writer.size == 8192

    def test_size_after_close_raises(self, target):
        writer = sabctools.FileWriter(target)
        writer.close()
        with pytest.raises(ValueError):
            writer.size

    def test_repr_says_whether_it_is_open(self, target):
        writer = sabctools.FileWriter(target)
        assert "closed=False" in repr(writer)
        writer.close()
        assert "closed=True" in repr(writer)

    def test_accessors_are_safe_against_a_concurrent_close(self, target):
        """close() mutates the handle under the exclusive lock with the GIL released,
        so the accessors have to take the shared lock rather than reading it bare.
        Unlocked, size would also be free to stat a descriptor that close() has already
        released - and that the OS may have handed to an entirely different file."""
        errors = []

        for _ in range(50):
            writer = sabctools.FileWriter(target)
            writer.write(b"payload", 0)
            stop = threading.Event()

            def poke():
                while not stop.is_set():
                    try:
                        writer.closed
                        repr(writer)
                        writer.size
                    except ValueError:
                        pass  # closed underneath us, which is the expected race
                    except Exception as err:  # anything else is a real failure
                        errors.append(err)
                        return

            readers = [threading.Thread(target=poke) for _ in range(4)]
            for reader in readers:
                reader.start()
            writer.close()
            stop.set()
            for reader in readers:
                reader.join()

        assert not errors, errors[:3]


class TestWriteStats:
    @pytest.fixture
    def before(self):
        return sabctools.write_stats()

    @staticmethod
    def since(before: dict) -> dict:
        now = sabctools.write_stats()
        return {key: now[key] - before[key] for key in ("count", "bytes", "nanos")}

    def test_it_reports_four_counters(self):
        stats = sabctools.write_stats()
        assert set(stats) == {"count", "bytes", "nanos", "max_nanos"}
        assert all(isinstance(value, int) and value >= 0 for value in stats.values())

    def test_every_write_is_counted(self, target, before):
        with sabctools.FileWriter(target) as writer:
            for index in range(4):
                writer.write(b"x" * 1000, index * 1000)
        assert self.since(before)["count"] == 4
        assert self.since(before)["bytes"] == 4000
        assert self.since(before)["nanos"] > 0

    def test_writes_to_several_files_add_up(self, tmp_path, before):
        for name in ("a.bin", "b.bin", "c.bin"):
            with sabctools.FileWriter(str(tmp_path / name)) as writer:
                writer.write(b"y" * 100, 0)
        assert self.since(before)["count"] == 3
        assert self.since(before)["bytes"] == 300

    def test_a_closed_file_keeps_its_writes_in_the_total(self, target, before):
        writer = sabctools.FileWriter(target)
        writer.write(b"x" * 500, 0)
        writer.close()
        del writer
        assert self.since(before)["count"] == 1
        assert self.since(before)["bytes"] == 500

    def test_preallocate_is_not_a_write(self, target, before):
        with sabctools.FileWriter(target) as writer:
            writer.preallocate(1 << 20)
        assert self.since(before)["count"] == 0

    def test_a_write_to_a_closed_writer_is_not_counted(self, target, before):
        writer = sabctools.FileWriter(target)
        writer.close()
        with pytest.raises(ValueError):
            writer.write(b"payload", 0)
        assert self.since(before)["count"] == 0

    def test_the_slowest_write_never_falls(self, target):
        with sabctools.FileWriter(target) as writer:
            writer.write(b"x" * 4096, 0)
            worst = sabctools.write_stats()["max_nanos"]
            writer.write(b"x", 0)
            assert sabctools.write_stats()["max_nanos"] >= worst

    def test_concurrent_writes_are_all_counted(self, target, before):
        with sabctools.FileWriter(target) as writer:
            threads = [threading.Thread(target=writer.write, args=(b"y" * 4096, index * 4096)) for index in range(8)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join()
        assert self.since(before)["count"] == 8
        assert self.since(before)["bytes"] == 8 * 4096
