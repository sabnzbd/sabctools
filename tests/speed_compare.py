import os

import _yenc
import sabyenc
import re
import time
import binascii
import sys
from testsupport import *

###################
# Real test
###################

nr_runs = 500

# Read some data (can pick any of the files from the yencfiles folder)
data_plain, data_chunks, data_bytes = read_pickle('tests/yencfiles/crc_5nCweVA4aKnlBTG1s0K8_4o22@JBinUp.local')

###################
# SUPPORT FUNCTIONS
###################

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


###################
# YENC C - NEW
###################

# Time it!
time1_new = time.clock()
timet_new = 0.0
for i in xrange(nr_runs):
    time2 = time.clock()

    timet_new += time.clock()-time2

    # Different from homemade
    decoded_data_new, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, data_bytes)


print "---"
time1_new_disp = 1000*(time.clock()-time1_new)
timet_new_disp = 1000*timet_new
print "%15s  took  %4d ms" % ("yEnc C New", time1_new_disp)
print "%15s  took  %4d ms = %d%%" % ("Base Python", timet_new_disp, 100*timet_new_disp/time1_new_disp)
print "---"


###################
# YENC C - Old
###################

# Time it!
time1 = time.clock()
timet = 0.0



for i in xrange(nr_runs):
    time2=time.clock()
    data = []
    for chunk in data_chunks:
        new_lines = chunk.split('\r\n')
        for i in xrange(len(new_lines)):
            if new_lines[i][:2] == '..':
                new_lines[i] = new_lines[i][1:]
        if new_lines[-1] == '.':
            new_lines = new_lines[1:-1]
        data.extend(new_lines)


    # Filter out empty ones
    data = filter(None, data)
    yenc, data = yCheck(data)
    ybegin, ypart, yend = yenc

    timet += time.clock()-time2

    # Different from homemade
    decoded_data, crc = _yenc.decode_string(''.join(data))[:2]
    partcrc = '%08X' % ((crc ^ -1) & 2 ** 32L - 1)

    if ypart:
        crcname = 'pcrc32'
    else:
        crcname = 'crc32'

    if crcname in yend:

        _partcrc = '0' * (8 - len(yend[crcname])) + yend[crcname].upper()
        #print _partcrc
    else:
        _partcrc = None
        print "Corrupt header detected => yend: %s" % yend

# print "Results equal:", decoded_data_new == decoded_data
# print "Size new:", len(decoded_data_new)
# print "Size old:", len(decoded_data)
# print "---"

# for u in xrange(0,len(decoded_data_new)):
#     if decoded_data_new[u] != decoded_data[u]:
#         print u
#         print u/2**12
#         print len(data_chunks)
#         print data_chunks[u/2**12][0]
#         print decoded_data_new[u]
#         print decoded_data_new[u+1]
#         print decoded_data[u]
#         break

time1_disp = 1000*(time.clock()-time1)
timet_disp = 1000*timet
print "%15s  took  %4d ms   Diff %4d ms (%d%%)" % ("yEnc C Old", time1_disp, time1_disp-time1_new_disp, 100*(time1_disp-time1_new_disp)/time1_disp)
print "%15s  took  %4d ms   Diff %4d ms" % ("Base Python", timet_disp, timet_disp-timet_new_disp)
print "---"





###################
# YENC PYTHON
###################

# Time it!
time1 = time.clock()

YDEC_TRANS = ''.join([chr((i + 256 - 42) % 256) for i in xrange(256)])
for i in xrange(nr_runs):
    data = []
    for chunk in data_chunks:
        new_lines = chunk.split('\r\n')
        for i in xrange(len(new_lines)):
            if new_lines[i][:2] == '..':
                new_lines[i] = new_lines[i][1:]
        if new_lines[-1] == '.':
            new_lines = new_lines[1:-1]
        data.extend(new_lines)

    # Filter out empty ones
    data = filter(None, data)
    yenc, data = yCheck(data)
    ybegin, ypart, yend = yenc

    # Different from homemade
    data = ''.join(data)
    for i in (0, 9, 10, 13, 27, 32, 46, 61):
        j = '=%c' % (i + 64)
        data = data.replace(j, chr(i))
    decoded_data = data.translate(YDEC_TRANS)
    crc = binascii.crc32(decoded_data)
    partcrc = '%08X' % (crc & 2 ** 32L - 1)

    if ypart:
        crcname = 'pcrc32'
    else:
        crcname = 'crc32'

    if crcname in yend:
        _partcrc = '0' * (8 - len(yend[crcname])) + yend[crcname].upper()
    else:
        _partcrc = None
        print "Corrupt header detected => yend: %s" % yend

print "%15s  took  %4d ms" % ("yEnc Python", 1000*(time.clock()-time1))
print "---"

sys.exit()


###################
# YENC PYTHON 2
###################

# Time it!
time1 = time.clock()


for i in xrange(nr_runs*10):
    data = data_raw
    print data[-3:] == '.\r\n'

print "%15s took %d ms" % ("Python1", 1000*(time.clock()-time1))

for i in xrange(nr_runs*10):
    data = data_raw
    l = 0
    while 1:
        if data[l:l+2] == '\r\n':
            break
        l += 1

print "%15s took %d ms" % ("Python2", 1000*(time.clock()-time1))
print "---"

