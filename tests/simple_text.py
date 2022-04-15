import sabyenc3

chunk_size = 14

# Read some data (can pick any of the files from the yencfiles folder)
with open("tests/yencfiles/test_regular.txt", "rb") as yencfile:
    data_raw = yencfile.read()
    data_raw_bytes = bytearray(data_raw)
    data_bytes = len(data_raw)
    n = 2 ** chunk_size
    data_chunks = [data_raw[i: i + n] for i in range(0, len(data_raw), n)]

#data_raw=bytearray(b"=ybegin part=41 line=128 size=49152000 name=90E2Sdvsmds0801dvsmds90E.part06.rar\r\nbla")



decoded_data, output_filename, crc_correct = sabyenc3.decode_usenet_chunks(data_chunks)
size, output_filename_new = sabyenc3.decode(data_raw_bytes, data_bytes)

assert len(decoded_data) == size
assert decoded_data[:100] == data_raw_bytes[:100]
assert decoded_data[-100:] == data_raw_bytes[-100:]