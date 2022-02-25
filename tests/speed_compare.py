#!/usr/bin/python3

import os
import pickle
import sys
import platform
print("Python", sys.version.split("\n")[0], platform.processor())
import re
import time
import binascii

import sabyenc3
print("sabyenc3", sabyenc3.__version__, sabyenc3.__file__)


###################
# Real test
###################

nr_runs = 500
chunk_size = 14

# Read some data (can pick any of the files from the yencfiles folder)
with open("tests/yencfiles/test_regular.txt", "rb") as yencfile:
    data_raw = yencfile.read()
    data_bytes = len(data_raw)
    n = 2 ** chunk_size
    data_chunks = [data_raw[i : i + n] for i in range(0, len(data_raw), n)]

try:
    # Python 2
    get_time = time.clock
except AttributeError:
    # Python 3
    get_time = time.process_time

###################
# YENC C - NEW
###################

# Time it!
time1_new = get_time()

for i in range(nr_runs):
    decoded_data_new, output_filename, crc, crc_yenc, crc_correct = sabyenc3.decode_usenet_chunks(
        data_chunks, data_bytes
    )

print("---")
time1_new_disp = 1000 * (get_time() - time1_new)
print("%15s  took  %4d ms" % ("yEnc C New", time1_new_disp))
print("---")
