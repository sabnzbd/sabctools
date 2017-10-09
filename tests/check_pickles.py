"""

This file can be used to check pickle files with raw NNTP data

"""

import os
import time
import sabyenc3
import pickle
import glob
import _yenc
import re
import binascii
import sys
import tests.testsupport
import json

all_crc_fails = glob.glob("./tests/yencfiles/crc_*")

failed_checks = 0

for fname in all_crc_fails:
    # Open file
    print("\n\n ======================================= \n\n")
    print(fname)
    data_p = open(fname, "r")
    try:
        data_chunks, data_size, lines = pickle.load(data_p)
    except:
        data_p.seek(0)
        data_chunks, data_size = pickle.load(data_p)
    data_p.close()

    """
    First we check SABYenc
    """
    output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc3.decode_usenet_chunks(
        data_chunks, data_size
    )
    print("\n---- SABYenc output ----\n")
    print("Filename:", output_filename)
    print("Size:", len(output_buffer))
    print("NZB size:", data_size)
    print("CRC Calc:", crc)
    print("CRC Yenc:", crc_yenc)
    print("CRC Bool:", crc_correct)

    """
    Validate using _yenc
    """
    data = []
    new_lines = "".join(data_chunks).split("\r\n")
    for i in range(len(new_lines)):
        if new_lines[i][:2] == "..":
            new_lines[i] = new_lines[i][1:]
    if new_lines[-1] == ".":
        new_lines = new_lines[1:-1]
    data.extend(new_lines)

    # Filter out empty ones
    data = [_f for _f in data if _f]
    yenc, data = tests.testsupport.parse_yenc_data(data)
    ybegin, ypart, yend = yenc

    # Different from homemade
    flat_data = "".join(data)
    decoded_data, crc = _yenc.decode_string(flat_data)[:2]
    partcrc = "%08X" % ((crc ^ -1) & 2 ** 32 - 1)

    if ypart:
        crcname = "pcrc32"
    else:
        crcname = "crc32"

    if crcname in yend:
        _partcrc = "0" * (8 - len(yend[crcname])) + yend[crcname].upper()
    else:
        _partcrc = None

    if output_buffer != decoded_data:
        # Discrepancy between _yenc and sabyenc3
        failed_checks += 1
        print("\n---- Old-method output ----\n")
        print("Filename:", ybegin["name"])
        print("Size:", len(decoded_data))
        print("CRC Calc:", _partcrc)
        print("CRC Yenc:", partcrc)

        # Where is the difference?
        diff_size = 1
        for n in range(0, len(min(decoded_data, output_buffer)), diff_size):
            if decoded_data[n : n + diff_size] != output_buffer[n : n + diff_size]:
                print("\nDiff location: ", n)
                print(output_buffer[n])
                print(decoded_data[n])
                break

print("")
print("FAILS: ", failed_checks)
