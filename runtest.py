import os 
os.system('python setup.py install')

import sabyenc
print
print

data_raw = open("test_noheader.txt", "rb").read()
#sabyenc.decode_string_usenet('=ybegin part=41 line=128 size=49152000 name=90E2Sdvsmds0801dvsmds90E.part06.rar')
sabyenc.decode_string_usenet(data_raw)

print
print