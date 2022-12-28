import time
start=time.time()
import ssl
import sabyenc3
import socket
print(time.time()-start)

import time


hostname = 'eunews.frugalusenet.com'
context = ssl.create_default_context()
print(time.time() - start)
with socket.create_connection((hostname, 563)) as sock:
    with context.wrap_socket(sock, server_hostname=hostname) as ssock:
        ssock.setblocking(False)
        time.sleep(1)
        print(ssock.version())
        print(time.time() - start)
        print(sabyenc3.unlocked_ssl_recv(ssock._sslobj, 100))
        print(sabyenc3.unlocked_ssl_recv(ssock._sslobj, 100))
        print(sabyenc3.openssl_linked)
        print(time.time() - start)