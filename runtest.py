import os 
os.system('python setup.py install')

import sabyenc
print
print

test_list = ['abc', 'defg'] 

data_raw = open("logo.ync", "rb").read()
#data_raw = open("logo.ync", "rb").read()


output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_raw, test_list)

print output_filename
print crc
print crc_yenc
print crc_correct