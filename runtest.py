import os 
os.system('python setup.py install')

import sabyenc
print
print



data_raw = open("test_noheader.txt", "rb").read()


n = 2**12
data_chunks = [data_raw[i:i+n] for i in range(0, len(data_raw), n)] 


output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, len(data_raw))

print 'Filename:', output_filename
print 'CRC Calc:', crc
print 'CRC Yenc:', crc_yenc
print 'CRC Bool:', crc_correct