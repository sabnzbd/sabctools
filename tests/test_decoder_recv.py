import os
import select
import socket
import ssl
import threading
import time

import pytest

import sabctools
from tests.test_decoder_sink import build_article
from tests.test_unlocked_ssl import client, server  # noqa: F401 (fixtures)

BUFFER = 256 * 1024


def receive(decoder, sock, count, timeout=10):
    """Call recv until `count` responses are complete, returning them and every recv() result"""
    responses, results = [], []
    deadline = time.monotonic() + timeout
    while len(responses) < count:
        assert time.monotonic() < deadline, "responses did not arrive"
        select.select([sock], [], [], 1)
        try:
            results.append(decoder.recv(sock))
        except (BlockingIOError, ssl.SSLWantReadError):
            continue
        assert results[-1], "peer closed early"
        responses.extend(decoder)
    return responses, results


@pytest.fixture
def pair():
    reader, writer = socket.socketpair()
    reader.setblocking(False)
    yield reader, writer
    reader.close()
    writer.close()


class TestPlain:
    def test_articles_are_decoded(self, pair):
        reader, writer = pair
        payloads = [os.urandom(300_000), os.urandom(5000)]
        decoder = sabctools.Decoder(BUFFER)
        for i, payload in enumerate(payloads):
            decoder.expect(i)
        sender = threading.Thread(target=writer.sendall, args=(b"".join(build_article(p) for p in payloads),))
        sender.start()
        responses, _ = receive(decoder, reader, len(payloads))
        sender.join()

        assert [r.context for r in responses] == [0, 1]
        for response, payload in zip(responses, payloads):
            assert response.crc is not None
            assert response.data == payload

    def test_a_socket_subclass_is_read(self, pair):
        class Subclass(socket.socket):
            pass

        reader, writer = pair
        sub = Subclass(fileno=reader.detach())
        sub.setblocking(False)
        payload = os.urandom(5000)
        decoder = sabctools.Decoder(BUFFER)
        decoder.expect("article")
        writer.sendall(build_article(payload))
        responses, _ = receive(decoder, sub, 1)
        sub.close()
        assert responses[0].data == payload

    def test_one_call_reads_at_most_the_free_space(self, pair):
        reader, writer = pair
        reader.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
        writer.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 * 1024 * 1024)
        payloads = [os.urandom(1024 * 1024) for _ in range(3)]
        decoder = sabctools.Decoder(BUFFER)
        for i in range(len(payloads)):
            decoder.expect(i)
        data = b"".join(build_article(p) for p in payloads)

        # A small read first, so the next read starts part-way into the buffer
        writer.sendall(data[:1000])
        select.select([reader], [], [], 5)
        start = decoder.recv(reader)
        writer.sendall(data[1000:])
        time.sleep(0.2)

        select.select([reader], [], [], 5)
        first = decoder.recv(reader)

        # Never more than the free space, even when more is ready
        assert BUFFER // 2 < first <= BUFFER - start
        responses = list(decoder)
        more, _ = receive(decoder, reader, len(payloads) - len(responses))
        assert [r.data for r in responses + more] == payloads

    def test_closed_peer_returns_zero(self, pair):
        reader, writer = pair
        writer.close()
        decoder = sabctools.Decoder(BUFFER)
        decoder.expect(None)
        select.select([reader], [], [], 5)
        assert decoder.recv(reader) == 0

    def test_nothing_ready_raises_blocking_io_error(self, pair):
        reader, _ = pair
        decoder = sabctools.Decoder(BUFFER)
        decoder.expect(None)
        with pytest.raises(BlockingIOError):
            decoder.recv(reader)

    def test_a_blocking_socket_is_refused(self, pair):
        reader, writer = pair
        reader.setblocking(True)
        writer.sendall(b"200 Welcome\r\n")
        with pytest.raises(ValueError, match="Only non-blocking sockets are supported"):
            sabctools.Decoder(BUFFER).recv(reader)

    def test_a_closed_socket_raises_oserror(self, pair):
        reader, _ = pair
        reader.close()
        decoder = sabctools.Decoder(BUFFER)
        with pytest.raises(OSError):
            decoder.recv(reader)


@pytest.mark.parametrize("sock", ["not a socket", object()])
def test_anything_else_is_refused(sock):
    with pytest.raises(TypeError, match=r"recv\(\) needs a socket\.socket"):
        sabctools.Decoder(BUFFER).recv(sock)


def send_all(sock, data):
    """sendall for a non-blocking SSLSocket"""
    view = memoryview(data)
    while view:
        try:
            view = view[sock.send(view) :]
        except (ssl.SSLWantWriteError, ssl.SSLWantReadError):
            select.select([sock], [sock], [], 1)


@pytest.mark.skipif(not sabctools.openssl_linked, reason="needs the linked OpenSSL")
class TestTLS:
    def test_articles_are_decoded(self, client):  # noqa: F811
        # The echo server returns what it is sent, so the article comes back over TLS
        payload = os.urandom(100_000)
        decoder = sabctools.Decoder(BUFFER)
        decoder.expect("tls")
        send_all(client, build_article(payload))
        responses, _ = receive(decoder, client, 1)
        assert responses[0].context == "tls"
        assert responses[0].data == payload

    def test_one_call_leaves_nothing_in_openssl(self, client):  # noqa: F811
        # A buffer that is no multiple of a TLS record, so reads stop mid-record
        payload = os.urandom(200_000)
        decoder = sabctools.Decoder(20_000)
        decoder.expect("tls")
        send_all(client, build_article(payload))
        responses = []
        deadline = time.monotonic() + 10
        while not responses:
            assert time.monotonic() < deadline, "response did not arrive"
            select.select([client], [], [], 1)
            try:
                assert decoder.recv(client)
            except ssl.SSLWantReadError:
                continue
            # select() would not report bytes OpenSSL still holds
            assert client.pending() == 0
            responses.extend(decoder)
        assert responses[0].data == payload

    def test_nothing_ready_raises_want_read(self, client):  # noqa: F811
        decoder = sabctools.Decoder(BUFFER)
        decoder.expect(None)
        with pytest.raises(ssl.SSLWantReadError):
            decoder.recv(client)

    def test_a_blocking_socket_is_refused(self, client):  # noqa: F811
        client.setblocking(True)
        with pytest.raises(ValueError, match="Only non-blocking sockets are supported"):
            sabctools.Decoder(BUFFER).recv(client)

    def test_a_closed_socket_raises_oserror(self, client):  # noqa: F811
        client.close()
        with pytest.raises(OSError, match="Could not find _sslobj attribute"):
            sabctools.Decoder(BUFFER).recv(client)
