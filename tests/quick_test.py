import os
os.system('python setup.py install')
import sabyenc
print
print

import pickle


#data_raw = open(".\\tests\\test_single_part.txt", "rb").read()
#data_raw = open(".\\tests\\test_partial.txt", "rb").read()
#data_raw = open(".\\tests\\test_yenc_new.txt", "rb").read()
data_raw = open(".\\tests\\6a15ae2034444c0ea2c64f0e3d8f2851@reader.usenetbucket.com.txt", "rb").read()

data_size =  len(data_raw)

n = 2**12
data_chunks = [data_raw[i:i+n] for i in range(0, len(data_raw), n)]


#data_p = open(".\\tests\\test2.p", "rb")
#data_chunks, data_size = pickle.load(data_p)



output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, data_size)

print 'Filename:', output_filename
print 'Size', len(output_buffer)
print 'CRC Calc:', crc
print 'CRC Yenc:', crc_yenc
print 'CRC Bool:', crc_correct