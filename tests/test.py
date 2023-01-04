import time
import ssl
import sabyenc3
import socket
import time


hostname = 'eunews.frugalusenet.com'
context = ssl.create_default_context()

print(sabyenc3.openssl_linked)

buffer = bytearray(100)
bufferview = memoryview(buffer)

with socket.create_connection((hostname, 563)) as sock:
    with context.wrap_socket(sock, server_hostname=hostname) as ssock:
        ssock.setblocking(False)
        time.sleep(1)
        print(ssock.version())
        print(sabyenc3.unlocked_ssl_recv_into(ssock._sslobj, bufferview[99:]))
        print(buffer)
