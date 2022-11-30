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
# Input
###################

# Use a number of files from the tests/yencfiles folder
input_data = []
all_pickles = glob.glob("tests/yencfiles/*_file*")
for fname in all_pickles:
    _, data_chunks = read_pickle(fname)
    input_data.append(data_chunks)

###################
# Real test
###################

nr_runs = 200

# Time it!
time1_new = time.process_time()
for i in range(nr_runs):
    for data_chunks in input_data:
        decoded_data_new, output_filename, crc_correct = sabyenc3.decode_usenet_chunks(data_chunks)
time1_new_disp = 1000 * (time.process_time() - time1_new)
print("%15s  took  %4d ms" % ("yEnc C New", time1_new_disp))
print()
