import gc
import glob
import os
import sys
from io import BytesIO

import pytest

from tests.testsupport import *


def build_article(
    payload: bytes, begin: int = 0, total: int = None, name: str = "test.bin", part: int = 1, crc: int = None
) -> bytes:
    """One yEnc part as it arrives on the wire, so a body of any size can be built"""
    total = len(payload) if total is None else total
    encoded, real_crc = sabctools.yenc_encode(payload)
    crc = real_crc if crc is None else crc
    header = (
        f"222 0 <{name}-{part}>\r\n"
        f"=ybegin part={part} line=128 size={total} name={name}\r\n"
        f"=ypart begin={begin + 1} end={begin + len(payload)}\r\n"
    ).encode()
    trailer = f"\r\n=yend size={len(payload)} part={part} pcrc32={crc:08x}\r\n.\r\n".encode()
    return header + encoded + trailer


def feed(decoder, wire: bytes, chunk: int = 0):
    """Push bytes through the decoder's buffer, returning the responses produced"""
    responses = []
    view_of = memoryview(wire)
    position = 0
    while position < len(view_of):
        buffer = memoryview(decoder)
        count = min(len(buffer), len(view_of) - position)
        if chunk:
            count = min(count, chunk)
        buffer[:count] = view_of[position : position + count]
        buffer.release()
        decoder.process(count)
        position += count
        responses.extend(decoder)
    return responses


@pytest.fixture
def writer(tmp_path):
    target = sabctools.FileWriter(str(tmp_path / "target.bin"))
    yield target
    target.close()


class TestPairing:
    """The request queue lives in the decoder so a response cannot be matched with the
    wrong request. Kept in Python alongside this one, the two could drift, and a drift
    writes one article's bytes into another article's file at a plausible offset."""

    def test_context_comes_back_on_the_response(self):
        data = read_plain_yenc_file("test_regular.yenc")
        decoder = sabctools.Decoder(len(data) * 2)
        decoder.expect("first")
        decoder.expect("second")
        assert decoder.expected == 2

        responses = feed(decoder, bytes(data) * 2)
        assert [response.context for response in responses] == ["first", "second"]
        assert decoder.expected == 0

    def test_without_expect_the_context_is_none(self):
        """Existing callers that never pair keep working exactly as before"""
        data = read_plain_yenc_file("test_regular.yenc")
        decoder = sabctools.Decoder(len(data))
        response = feed(decoder, bytes(data))[0]
        assert response.context is None
        assert response.data is not None
        assert response.bytes_decoded == 384000

    def test_clear_expected_drops_everything(self):
        decoder = sabctools.Decoder(4096)
        decoder.expect("one")
        decoder.expect("two")
        decoder.clear_expected()
        assert decoder.expected == 0

    def test_sink_must_be_a_filewriter(self):
        """Duck typing is not enough: the write happens from C with the GIL released"""
        decoder = sabctools.Decoder(4096)
        with pytest.raises(TypeError):
            decoder.expect("ctx", object())
        with pytest.raises(TypeError):
            decoder.expect("ctx", "not a writer")

    def test_none_sink_is_accepted(self):
        decoder = sabctools.Decoder(4096)
        decoder.expect("ctx", None)
        assert decoder.expected == 1


class TestSinkOutput:
    def test_matches_the_bytearray_path_exactly(self, writer):
        """The sink is only worth having if it produces the same bytes"""
        data = bytes(read_plain_yenc_file("test_regular.yenc"))

        reference = feed(sabctools.Decoder(len(data)), data)[0]

        decoder = sabctools.Decoder(len(data))
        decoder.expect("article", writer)
        streamed = feed(decoder, data)[0]
        writer.close()

        assert streamed.data is None, "a streamed response must not also build a bytearray"
        assert streamed.bytes_decoded == reference.bytes_decoded
        assert streamed.crc == reference.crc
        assert streamed.part_begin == reference.part_begin

        on_disk = open(writer.path, "rb").read()
        assert on_disk[streamed.part_begin :] == bytes(reference.data)

    def test_lands_at_the_offset_from_the_headers(self, writer):
        payload = b"payload at an offset"
        decoder = sabctools.Decoder(65536)
        decoder.expect("article", writer)
        response = feed(decoder, build_article(payload, begin=4096, total=8192))[0]
        writer.close()

        assert response.part_begin == 4096
        contents = open(writer.path, "rb").read()
        assert contents[4096 : 4096 + len(payload)] == payload
        assert contents[:4096] == b"\0" * 4096

    def test_parts_arriving_out_of_order(self, writer):
        """Articles do not arrive in order, which is the whole reason for writing at an
        offset rather than appending"""
        parts = [os.urandom(1000) for _ in range(4)]
        total = sum(len(part) for part in parts)

        decoder = sabctools.Decoder(65536)
        for index in (2, 0, 3, 1):
            decoder.expect(index, writer)
            feed(decoder, build_article(parts[index], begin=index * 1000, total=total, part=index + 1))
        writer.close()

        assert open(writer.path, "rb").read() == b"".join(parts)

    def test_body_larger_than_the_staging_buffer(self, writer):
        """Anything over YENC_STAGING_SIZE is written in pieces, and the pieces have to
        land contiguously and still checksum"""
        payload = os.urandom(3 * 1024 * 1024)
        decoder = sabctools.Decoder(256 * 1024)
        decoder.expect("big", writer)
        response = feed(decoder, build_article(payload))[0]
        writer.close()

        assert response.bytes_decoded == len(payload)
        assert response.crc is not None, "the CRC has to survive being folded across flushes"
        assert open(writer.path, "rb").read() == payload

    def test_response_split_across_many_reads(self, writer):
        """A response normally spans several socket reads, so the staging buffer has to
        carry over between process() calls"""
        payload = os.urandom(400_000)
        decoder = sabctools.Decoder(64 * 1024)
        decoder.expect("split", writer)
        response = feed(decoder, build_article(payload), chunk=1024)[0]
        writer.close()

        assert response.bytes_decoded == len(payload)
        assert open(writer.path, "rb").read() == payload

    def test_the_staging_buffer_is_reused(self, writer):
        """Streaming exists to remove the per-article allocation, so many articles must
        not grow the decoder's memory"""
        decoder = sabctools.Decoder(256 * 1024)
        payload = os.urandom(200_000)
        for index in range(20):
            decoder.expect(index, writer)
            feed(decoder, build_article(payload, begin=index * len(payload), total=20 * len(payload)))
        writer.close()

        assert os.path.getsize(writer.path) == 20 * len(payload)


class TestSinkErrors:
    def test_a_closed_sink_fails_the_article_not_the_connection(self, tmp_path):
        """A job deleted mid-download closes the file under an article still arriving.

        Raising here would abandon the decoder inside a response, leaving the rest of
        that article in the buffer to be parsed as the next one - so one closed file
        would cost the whole connection. The response is consumed to its end instead
        and the failure reported on the response.
        """
        target = sabctools.FileWriter(str(tmp_path / "closed.bin"))
        target.close()

        decoder = sabctools.Decoder(65536)
        decoder.expect("article", target)
        responses = feed(decoder, build_article(b"x" * 5000))

        assert len(responses) == 1
        assert responses[0].sink_failed is True
        assert responses[0].data is None, "nothing was kept, so there is nothing to save"
        assert responses[0].context == "article"

    def test_the_stream_stays_in_sync_after_a_sink_failure(self, tmp_path):
        """The point of not raising: the next article on the same connection still
        decodes. This is what a mid-response abort would destroy."""
        closed = sabctools.FileWriter(str(tmp_path / "closed.bin"))
        closed.close()
        good = sabctools.FileWriter(str(tmp_path / "good.bin"))

        decoder = sabctools.Decoder(1 << 20)
        decoder.expect("doomed", closed)
        decoder.expect("fine", good)

        payload = b"y" * 4000
        wire = build_article(b"x" * 5000, name="a.bin") + build_article(payload, name="b.bin")
        responses = feed(decoder, wire)

        assert [r.context for r in responses] == ["doomed", "fine"]
        assert responses[0].sink_failed is True
        assert responses[1].sink_failed is False, "the second article was collateral damage"
        assert responses[1].bytes_decoded == len(payload)
        good.close()
        assert open(str(tmp_path / "good.bin"), "rb").read() == payload

    def test_a_sink_closed_partway_through_is_reported(self, tmp_path):
        """The realistic shape: the file is open when the article starts and closed
        while its body is still arriving"""
        target = sabctools.FileWriter(str(tmp_path / "midway.bin"))
        decoder = sabctools.Decoder(64 * 1024)
        decoder.expect("article", target)

        # Larger than the staging buffer, so flushes happen while the body arrives
        wire = memoryview(build_article(b"z" * (2 * 1024 * 1024)))
        responses = []
        position = 0
        closed = False
        while position < len(wire):
            buffer = memoryview(decoder)
            count = min(len(buffer), len(wire) - position)
            buffer[:count] = wire[position : position + count]
            buffer.release()
            decoder.process(count)
            responses.extend(decoder)
            position += count
            if not closed and position > len(wire) // 2:
                target.close()  # the job is deleted right here
                closed = True

        assert closed, "the sink was never closed, so nothing was tested"
        assert len(responses) == 1, "the response still has to complete"
        assert responses[0].sink_failed is True
        assert responses[0].data is None

    def test_a_bad_crc_is_still_reported(self, writer):
        """The bytes are already on disk by the time the CRC is known, which matches
        what the cache path does: the data is kept so par2 can repair it"""
        payload = b"z" * 5000
        wire = build_article(payload, crc=0xDEADBEEF)
        decoder = sabctools.Decoder(65536)
        decoder.expect("article", writer)
        response = feed(decoder, wire)[0]
        writer.close()

        assert response.crc is None, "a mismatch has to be reported"
        assert response.bytes_decoded == len(payload)
        assert open(writer.path, "rb").read() == payload, "the data is still written, for par2"

    def test_uu_falls_back_to_a_bytearray(self, writer):
        """uu carries no offsets, so there is nowhere to stream it to. The sink is
        ignored and the caller gets data as usual."""
        data = read_uu_file("logo_full.nntp")
        decoder = sabctools.Decoder(len(data))
        decoder.expect("article", writer)
        response = feed(decoder, bytes(data))[0]
        writer.close()

        assert response.format is sabctools.EncodingFormat.UU
        assert response.data is not None
        assert os.path.getsize(writer.path) == 0


class TestGarbageCollection:
    """Both types hold arbitrary Python objects now, so cycles through them are
    reachable and have to be collectable"""

    @staticmethod
    def live(cls) -> int:
        """How many instances of a type are still reachable.

        Counting instances rather than checking whether a remembered id() is gone:
        an address identifies an object only while it is alive, so a freed one is
        immediately available to the next allocation and an identity check reports
        whatever happens to land there. Which allocation wins the block is down to
        the platform allocator, so that spuriously fails on some platforms and not
        others while the collector is behaving identically on all of them.
        """
        return sum(1 for obj in gc.get_objects() if type(obj) is cls)

    def test_a_decoder_cycle_is_collected(self):
        gc.collect()
        before = self.live(sabctools.Decoder)

        decoder = sabctools.Decoder(4096)
        decoder.expect(decoder)  # decoder -> pending -> decoder
        assert self.live(sabctools.Decoder) == before + 1, "a live instance is not being counted"

        del decoder
        gc.collect()
        assert self.live(sabctools.Decoder) == before

    def test_a_response_cycle_is_collected(self):
        gc.collect()
        before = self.live(sabctools.NNTPResponse)

        data = bytes(read_plain_yenc_file("test_regular.yenc"))
        decoder = sabctools.Decoder(len(data))
        cycle = []
        decoder.expect(cycle)
        response = feed(decoder, data)[0]
        cycle.append(response)  # response -> context list -> response
        assert self.live(sabctools.NNTPResponse) == before + 1, "a live instance is not being counted"

        del response, cycle, decoder
        gc.collect()
        assert self.live(sabctools.NNTPResponse) == before

    def test_both_types_are_tracked(self):
        decoder = sabctools.Decoder(4096)
        assert gc.is_tracked(decoder)

    def test_the_sink_is_released_with_the_response(self, tmp_path):
        """A held sink reference would keep the file open past the article"""
        target = sabctools.FileWriter(str(tmp_path / "released.bin"))
        decoder = sabctools.Decoder(65536)
        before = sys.getrefcount(target)
        decoder.expect("article", target)
        response = feed(decoder, build_article(b"y" * 2000))[0]
        del response
        gc.collect()
        assert sys.getrefcount(target) == before
        target.close()


class TestInFlightAccounting:
    """A request stays in flight until its response reaches the caller. Counting only
    untouched requests reports a pipelined connection as idle while it is still
    receiving, and the caller then stops reading from the socket."""

    def test_a_partially_received_response_still_counts(self):
        data = bytes(read_plain_yenc_file("test_regular.yenc"))
        decoder = sabctools.Decoder(len(data) * 2)
        decoder.expect("first")
        decoder.expect("second")

        # Enough of the first response to start it, nowhere near enough to finish
        buffer = memoryview(decoder)
        buffer[:512] = data[:512]
        buffer.release()
        decoder.process(512)

        assert decoder.expected == 2, "the response being received is still in flight"
        assert decoder.pending == ("first", "second")

    def test_completed_but_uncollected_responses_count(self):
        data = bytes(read_plain_yenc_file("test_regular.yenc"))
        decoder = sabctools.Decoder(len(data) * 2)
        decoder.expect("first")
        decoder.expect("second")
        feed_without_collecting = memoryview(data + data)

        buffer = memoryview(decoder)
        buffer[: len(feed_without_collecting)] = feed_without_collecting
        buffer.release()
        decoder.process(len(feed_without_collecting))

        # Both finished, neither collected
        assert decoder.expected == 2
        assert decoder.pending == ("first", "second")

        collected = list(decoder)
        assert [response.context for response in collected] == ["first", "second"]
        assert decoder.expected == 0
        assert decoder.pending == ()

    def test_the_article_being_received_is_reported_first(self):
        """The caller names the article a connection is fetching from this"""
        data = bytes(read_plain_yenc_file("test_regular.yenc"))
        decoder = sabctools.Decoder(len(data) * 2)
        decoder.expect("being received")
        decoder.expect("still queued")

        buffer = memoryview(decoder)
        buffer[:512] = data[:512]
        buffer.release()
        decoder.process(512)

        assert decoder.pending[0] == "being received"


@pytest.mark.parametrize("test_data", sorted(glob.glob("tests/yencfiles/*.yenc")), ids=lambda p: os.path.basename(p))
def test_sink_and_bytearray_agree(test_data, tmp_path):
    """Every yenc file decoded both ways, must agree"""
    data = open(test_data, "rb").read()
    size = max(1024, len(data) * 2)

    reference = feed(sabctools.Decoder(size), data)

    target = str(tmp_path / "sink.bin")
    writer = sabctools.FileWriter(target)

    # Preallocate as Assembler.open does, which marks the file sparse. Without it a
    # write past the end has to be materialised: NTFS zero-fills from the file's valid
    # data length to the write offset, so test_huge_size_1TiB_ypart tries to write a
    # terabyte and fails. APFS and ext4 make the hole implicitly, which is why skipping
    # this only shows up on Windows.
    needed = max((r.part_begin + len(r.data) for r in reference if r.data), default=0)
    if needed:
        writer.preallocate(needed)

    decoder = sabctools.Decoder(size)
    for _ in range(max(1, len(reference))):
        decoder.expect("article", writer)
    streamed = feed(decoder, data)
    writer.close()

    assert len(streamed) == len(reference), "different number of responses"

    for expected, actual in zip(reference, streamed):
        for attribute in (
            "bytes_decoded",
            "bytes_read",
            "crc",
            "crc_expected",
            "part_begin",
            "part_end",
            "part_size",
            "end_size",
            "file_size",
            "file_name",
            "status_code",
            "format",
            "baddata",
        ):
            assert getattr(actual, attribute) == getattr(expected, attribute), attribute
        assert actual.sink_failed is False

        if expected.data is None:
            # Nothing was decoded either way, so there is nothing on disk to check
            assert actual.data is None
            continue

        if actual.data is not None:
            # uu carries no offsets, so a sink is ignored and both build a bytearray
            assert actual.format is sabctools.EncodingFormat.UU
            assert actual.data == expected.data
            continue

        # Streamed: the same bytes have to be at the offset the headers gave
        with open(target, "rb") as written:
            written.seek(expected.part_begin)
            assert written.read(len(expected.data)) == bytes(expected.data)
