"""

This file can be used to check pickle files with raw NNTP data

"""

import os
import time
import pickle
import glob
import _yenc
import re
import binascii
import sys
import json

all_crc_fails = glob.glob('./tests/yencfiles/special_*')


def yCheck(data):
    ybegin = None
    ypart = None
    yend = None

    # Check head
    for i in xrange(min(40, len(data))):
        try:
            if data[i].startswith('=ybegin '):
                splits = 3
                if data[i].find(' part=') > 0:
                    splits += 1
                if data[i].find(' total=') > 0:
                    splits += 1

                ybegin = ySplit(data[i], splits)

                if data[i + 1].startswith('=ypart '):
                    ypart = ySplit(data[i + 1])
                    data = data[i + 2:]
                    break
                else:
                    data = data[i + 1:]
                    break
        except IndexError:
            break

    # Check tail
    for i in xrange(-1, -11, -1):
        try:
            if data[i].startswith('=yend '):
                yend = ySplit(data[i])
                data = data[:i]
                break
        except IndexError:
            break

    return ((ybegin, ypart, yend), data)

# Example: =ybegin part=1 line=128 size=123 name=-=DUMMY=- abc.par
YSPLIT_RE = re.compile(r'([a-zA-Z0-9]+)=')
def ySplit(line, splits=None):
    fields = {}

    if splits:
        parts = YSPLIT_RE.split(line, splits)[1:]
    else:
        parts = YSPLIT_RE.split(line)[1:]

    if len(parts) % 2:
        return fields

    for i in range(0, len(parts), 2):
        key, value = parts[i], parts[i + 1]
        fields[key] = value.strip()

    return fields

for fname in all_crc_fails:
    # Open file
    print '\n\n ======================================= \n\n'
    print fname
    data_p = open(fname, "r")
    data_chunks, data_size, lines = pickle.load(data_p)
    data_p.close()

    data_p = open(fname.replace('special', 'crc'), "r")
    data_chunks2, data_size2, lines2 = pickle.load(data_p)
    data_p.close()


    import pdb; pdb.set_trace()  # breakpoint fe42284c //
