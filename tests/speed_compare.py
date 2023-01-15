#!/usr/bin/python3 -OO
# Copyright 2007-2019 The SABnzbd-Team <team@sabnzbd.org>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

import time
import glob
import sabyenc3
from testsupport import read_pickle

###################
# Real test
###################

nr_runs = 5000
chunk_size = 14

# Read some data (can pick any of the files from the yencfiles folder)
with open("tests/yencfiles/test_regular.txt", "rb") as yencfile:
    data_raw = yencfile.read()

# Split for old method
data_length = len(data_raw)
n = 2**chunk_size
data_chunks = [data_raw[i : i + n] for i in range(0, data_length, n)]

for _ in range(5):
    # Add each to their own list
    list_chunks = []
    list_bytes = []
    for _ in range(nr_runs):
        list_chunks.append(list(map(lambda x: bytes(bytearray(x)), data_chunks)))
        list_bytes.append(bytearray(data_raw))

    # Buffer version
    time2_new = time.process_time()
    for i in range(nr_runs):
        output_filename = sabyenc3.decode_buffer(list_bytes[i], data_length)
    time2_new_disp = 1000 * (time.process_time() - time2_new)

    # Current version
    time1_new = time.process_time()
    for i in range(nr_runs):
        decoded_data_new, output_filename, crc_correct = sabyenc3.decode_usenet_chunks(list_chunks[i])
    time1_new_disp = 1000 * (time.process_time() - time1_new)




    print("%15s  took  %4d ms" % ("yEnc C Current", time1_new_disp))
    print("%15s  took  %4d ms" % ("yEnc C Buffer", time2_new_disp))
    print("---")
