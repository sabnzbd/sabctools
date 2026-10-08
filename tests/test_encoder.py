import os
import zlib

import sabctools
from tests.testsupport import sabctools_yenc_wrapper


def test_encoder():
    output, crc = sabctools.yenc_encode(b"Hello world!")
    assert output == b"r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK"
    assert crc == 0x1B851995


def test_encoder_round_trip():
    """Random data over many lines, so lines start with bytes the encoder has to wrap past 255"""
    payload = os.urandom(100_000)
    output, crc = sabctools.yenc_encode(payload)
    assert crc == zlib.crc32(payload)

    data_plain = b"".join(
        [
            b"222 0 <foo@bar>\r\n" b"=ybegin part=1 total=1 line=128 size=%d name=helloworld\r\n" % len(payload),
            b"=ypart begin=1 end=%d\r\n" % len(payload),
            output,
            b"\r\n=yend size=%d pcrc32=%08x\r\n" % (len(payload), crc),
            b".\r\n",
        ]
    )
    decoded_data, _, _, _, _, decoded_crc = sabctools_yenc_wrapper(bytearray(data_plain))
    assert bytes(decoded_data) == payload
    assert decoded_crc == crc
