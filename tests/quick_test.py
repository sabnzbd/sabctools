import os  
os.system('python setup.py install') 
import sabyenc 
print 
print 
 
 
#data_raw = open(".\\tests\\test_single_part.txt", "rb").read()
#data_raw = open(".\\tests\\test_partial.txt", "rb").read()
#data_raw = open(".\\tests\\test_yenc_new.txt", "rb").read()
data_raw = open(".\\tests\\test_nocontent.txt", "rb").read()

data_size =  len(data_raw)
print data_size
#data_raw = open(".\\tests\\test_nocontent.txt", "rb").read() 
data_size = 100000
 
n = 2**12 
data_chunks = [data_raw[i:i+n] for i in range(0, len(data_raw), n)]  
 
 
output_buffer, output_filename, crc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, data_size) 
 
print 'Filename:', output_filename 
print 'CRC Calc:', crc 
print 'CRC Yenc:', crc_yenc 
print 'CRC Bool:', crc_correct