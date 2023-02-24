import os
import select
import socket
import ssl
import sys
import tempfile
import threading
import time

import pytest
import portend

from tests.testsupport import *

HOST = "127.0.0.1"

cert = """-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIUXYYp1atAnSypLyKprYna65kFn9owDQYJKoZIhvcNAQEL
BQAwRTELMAkGA1UEBhMCQVUxEzARBgNVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoM
GEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDAeFw0yMzAxMTAwODE5NDVaFw0zMzAx
MDcwODE5NDVaMEUxCzAJBgNVBAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEw
HwYDVQQKDBhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQwggIiMA0GCSqGSIb3DQEB
AQUAA4ICDwAwggIKAoICAQDWHsTQOLJ0O/WdJ2pr/IjwXEcrFr6xiDuxQGwJQxkI
beY4w/jFxz9TmRwkIm+wrqw6o/4BxRJIJrot0byaJzSYlYJS+ChH1rRa3E72Jb0L
Kfk1peNcTInujHStXv93aLbxhamswg2M7gTUPelZeIlTHf/YsLzx4KEfF/PSs6DJ
eOUHmGcFE01Ypef0tM+72DncrRbDWKbAC5IQm2g4yCC/L6RYNbpOn5Cm9/M1JPnx
+Gn6ylrWQhquVrLnxEfupNgCWiRTZXSvy6jNPgGEi92OiwT5M3MMn/FZVxDKILHg
B9esr1b3s6qTW3sclcLXKIC1eG1/ikUmVfbVonmnlLuHao6SfHWeV7kuBdqnYQFN
UgZ11xtakWC/dwjIt101MjFe3PlgLfgxeSMXZFYpBc0RwBfmBavEHbbRTgebloIO
xrPMIvbFiNg9osoC8nTKwtrEMo0MlCYyElXs18hLzo3GvL1bCUCQiqj4sRBFKG4B
SgyhV/i72kuf9FhdcM4IJFoCc9B2Ll9KCflhhwGe+f/1cpd6nlKXgI2+UfgHY0Nj
UqDqq7gLg0WMCpSKasH68e4UDzceUUB1d7OwJwljxJxGuZErKIQbGi0wKVy0evRO
lcb5IL9l8rPfxE1aqlQ6VbDJ91rOrYdB6MugWNK3KfYPcAXHkPsWcMPtO4R2nEeR
FwIDAQABo1MwUTAdBgNVHQ4EFgQUp9LcqyRllH3ZN/WpSPjxVybPSZowHwYDVR0j
BBgwFoAUp9LcqyRllH3ZN/WpSPjxVybPSZowDwYDVR0TAQH/BAUwAwEB/zANBgkq
hkiG9w0BAQsFAAOCAgEAaeyercYN95kMCdLCjX9jmAGhG6WB3eFvEwmTtcSoxo3Q
6/LBk8EdikfirIHuJKZVxTRuvRA/KniDI/pJC16qcRUI0OmQFtCirqD7kiR/fkYO
IXqh3wzu4bBGewf+MLGtJjYxn83w/KP7kj3ZTaoOLbmizBDWCMlYdYwbqTVfuOCL
hQwvRpiqeOBNCphvrreo6HqLaiWaRPb60NZqEu+yI7ltGN+QDKQJ04CJT1gLqkAB
MsmvLLmQDPtnhE16zqZc5iZ3FHFm+sSSQq5jImjmCHT8aNGk4ng0VuQQsMZClgmx
U6SvMo2u5akK9h3d9L2QT9vc8Zn/gUdQW4N1kLBU2HoHod/KLKkPrWGR4oIowYmk
C/nQjh/qxJFk6zEwo7Woo2+mQop9vKnA5jH24qbuCm0k4GfJegB7FCaW7TcRTwxy
wExvMNVgaZ3+Fuf/dbGTG3EHSfQO+hPMFCvo7WQ+ndQcqEmLlyNnV9owfHOYYo1l
TgLUi49uVqHLfN3BZPIOVW3FP26OLxdVRQf1OdwR5+eDp6CXceO5U7kZJN5L7ZMi
fDIh4KerzRZrtdt6Y/CnxI6R1qDHfpK2BA8DJpyhhDUap7OrGhktqb1nTT0hrLAq
acN1EC6NLepwbQL0RR3Xhmu3Jq59bXRVpjCMHC4rQHQvbG01vT0SjENM38RGUbk=
-----END CERTIFICATE-----
"""

key = """-----BEGIN PRIVATE KEY-----
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
ZNWqBl74DpmsArY6Tz4P6sbK+jVl
-----END PRIVATE KEY-----
"""


class EchoServer(threading.Thread):
    """An SSL echo server"""

    class ConnectionHandler(threading.Thread):
        def __init__(self, server, connsock, addr, buffer_size):
            self.server = server
            self.sock = connsock
            self.addr = addr
            self.buffer_size = buffer_size
            self.running = False
            self.sslconn = None
            super().__init__()

        def wrap_conn(self):
            try:
                self.sslconn = self.server.context.wrap_socket(self.sock, server_side=True)
            except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError) as e:
                self.running = False
                self.close()
                return False
            except (ssl.SSLError, OSError):
                return False
            return True

        def read(self):
            return self.sslconn.read(self.buffer_size)

        def write(self, bytes: bytes):
            self.sslconn.write(bytes)

        def close(self):
            if self.sslconn:
                self.sslconn.close()

        def run(self) -> None:
            self.running = True
            if not self.wrap_conn():
                return
            while self.running:
                try:
                    data = self.read()
                    if not data.strip():
                        self.running = False
                        try:
                            self.sock = self.sslconn.unwrap()
                        except OSError:
                            # Incorrect shutdown of SSL session
                            pass
                        else:
                            self.sslconn = None
                        self.close()
                    else:
                        self.write(data)
                except OSError:
                    self.close()
                    self.running = False
                    self.server.stop()

    def __init__(self):
        self.host = HOST
        self.port = portend.find_available_local_port()
        self.flag = None
        self.active = False
        super().__init__()

        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            self.certfile = tmp.name
            with open(self.certfile, "w") as f:
                f.write(cert)
                f.close()

        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            self.keyfile = tmp.name
            with open(self.keyfile, "w") as f:
                f.write(key)
                f.close()

        self.context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        self.context.load_cert_chain(certfile=self.certfile, keyfile=self.keyfile)

        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((self.host, self.port))

    def __enter__(self):
        self.start(threading.Event())
        self.flag.wait()
        return self

    def __exit__(self, *args):
        self.stop()
        self.join()

    def start(self, flag=None):
        self.flag = flag
        threading.Thread.start(self)

    def run(self) -> None:
        self.sock.settimeout(2)
        self.sock.listen(5)
        self.active = True
        if self.flag:
            self.flag.set()
        while self.active:
            try:
                conn, addr = self.sock.accept()
                handler = self.ConnectionHandler(self, conn, addr, 131072)
                handler.start()
                handler.join()
            except TimeoutError:
                pass
            except KeyboardInterrupt:
                self.stop()

        self.close()

    def close(self):
        if self.sock is not None:
            self.sock.close()
            self.sock = None
        os.unlink(self.certfile)
        os.unlink(self.keyfile)

    def stop(self):
        self.active = False


@pytest.fixture()
def server():
    server = EchoServer()
    with server:
        yield server


@pytest.fixture()
def client(server):
    with socket.create_connection((server.host, server.port)) as sock:
        context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
        context.check_hostname = False
        context.verify_mode = ssl.VerifyMode.CERT_NONE
        with context.wrap_socket(sock, server_hostname=server.host) as ssock:
            ssock.setblocking(False)
            yield ssock


def unlocked_ssl_recv_into_wrapper(sock: ssl.SSLSocket, data: bytes, data_view: memoryview) -> int:
    """Send data to the socket and writes the response to a buffer"""
    sock.sendall(data)
    data_position = 0
    while True:
        try:
            data_position += sabctools.unlocked_ssl_recv_into(sock, data_view[data_position:])
        except ssl.SSLWantReadError:
            pass
        if data_position > 0:
            break
    return data_position


@pytest.fixture()
def buffer():
    yield memoryview(bytearray(1024))


def test_openssl_linked():
    assert sabctools.openssl_linked is True


def test_unlocked_ssl_recv_into_not_a_socket_fails(buffer):
    with pytest.raises(TypeError, match=r"argument 1 must be SSLSocket"):
        sabctools.unlocked_ssl_recv_into("this is not a socket", buffer)


def test_unlocked_ssl_recv_into_not_a_buffer_fails(client):
    with pytest.raises(TypeError, match=r"argument 2 must be read-write bytes-like object"):
        unlocked_ssl_recv_into_wrapper(client, b"TEST", "...")


def test_unlocked_ssl_recv_into_response(client, buffer):
    bytes_received = unlocked_ssl_recv_into_wrapper(client, b"TEST", buffer)
    assert buffer[:bytes_received].tobytes() == b"TEST"


def test_unlocked_ssl_recv_into_bulk_response(client):
    # 131072 bytes divide up into 8 TLS records (16 KB each)
    # In nonblocking mode, we should be able to read all eight in a single
    # drop of the GIL.
    size = 131072
    buffer = bytearray(size)

    client.sendall(b"\xFF" * size)

    select.select([client], [], [])

    while size > 0:
        try:
            count = sabctools.unlocked_ssl_recv_into(client, buffer)
        except ssl.SSLWantReadError:
            select.select([client], [], [])
            # Give the sender some more time to complete sending.
            time.sleep(0.1)
        else:
            if count > 16384:
                return
            size -= count

    pytest.fail("All TLS reads were smaller than 16KB")


#
def test_unlocked_ssl_recv_into_blocking_socket_fails(client, buffer):
    with pytest.raises(ValueError, match="Only non-blocking sockets are supported"):
        client.setblocking(True)
        unlocked_ssl_recv_into_wrapper(client, b"TEST", buffer)


def test_unlocked_ssl_recv_into_full_buffer_fails(client, buffer):
    with pytest.raises(ValueError, match="No space left in buffer"):
        unlocked_ssl_recv_into_wrapper(client, b"TEST", buffer[len(buffer) :])


def test_unlocked_ssl_recv_into_ref_counts_unchanged(client, buffer):
    objects = [
        client,
        client._sslobj,
        buffer,
    ]

    before = [sys.getrefcount(x) for x in objects]

    bytes_received = unlocked_ssl_recv_into_wrapper(client, b"Hello World", buffer)
    assert buffer[:bytes_received].tobytes() == b"Hello World"

    after = [sys.getrefcount(x) for x in objects]

    assert after == before
