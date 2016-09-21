import os 
os.system('python setup.py install')

import sabyenc
print
print

data_raw = open("logo.ync", "rb").read()
#data_raw = open("logo.ync", "rb").read()
#sabyenc.decode_string_usenet('=ybegin part=41 line=128 size=49152000 name=90E2Sdvsmds0801dvsmds90E.part06.rar')
output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_string_usenet(data_raw)

print output_filename
print crc
print crc_yenc
print crc_correct