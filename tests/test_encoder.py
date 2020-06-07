from tests.testsupport import *


def test_encoder():
    output, crc = sabyenc3.encode(b"Hello world!")
    assert output == b"r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK"
    assert crc == 3833259626
