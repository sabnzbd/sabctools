import os
import time
#os.system('python setup.py install')
#time.sleep(3)
import sabyenc

import pickle

import glob
all_crc_fails = glob.glob('./tests/crc_*')

for fname in all_crc_fails:
    #data_raw = open(".\\tests\\test_single_part.txt", "rb").read()
    #data_raw = open(".\\tests\\test_partial.txt", "rb").read()
    #data_raw = open(".\\tests\\test_yenc_new.txt", "rb").read()
    #data_raw = open(".\\tests\\6a15ae2034444c0ea2c64f0e3d8f2851@reader.usenetbucket.com.txt", "rb").read()

    #data_size =  len(data_raw)

    #n = 2**12
    #data_chunks = [data_raw[i:i+n] for i in range(0, len(data_raw), n)]


    print
    print fname
    data_p = open(fname, "rb")
    data_chunks, data_size = pickle.load(data_p)
    for i in range(100000):
        output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, data_size)
    import pdb; pdb.set_trace()  # breakpoint a5c02678 //

    print 'Filename:', output_filename
    print 'Size', len(output_buffer)
    print 'CRC Calc:', crc
    print 'CRC Yenc:', crc_yenc
    print 'CRC Bool:', crc_correct

    #\if not crc_correct:
    #    print data_chunks[-1]

