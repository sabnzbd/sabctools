import sabyenc3
import time


###################
# Real test
###################

nr_runs = 5000
chunk_size = 14

# Read some data (can pick any of the files from the yencfiles folder)
with open("tests/yencfiles/test_regular.txt", "rb") as yencfile:
    data_raw = yencfile.read()

# Split for old method
data_length = len(data_raw)
n = 2**chunk_size
data_chunks = [data_raw[i : i + n] for i in range(0, data_length, n)]

for _ in range(5):
    # Add each to their own list
    list_chunks = []
    list_bytes = []
    for _ in range(nr_runs):
        list_chunks.append(data_chunks)
        list_bytes.append(bytearray(data_raw))

    # Buffer version
    time2_new = time.process_time()
    for i in range(nr_runs):
        output_filename = sabyenc3.decode(list_bytes[i], data_length)
    time2_new_disp = 1000 * (time.process_time() - time2_new)

    # Current version
    time1_new = time.process_time()
    for i in range(nr_runs):
        decoded_data_new, output_filename, crc_correct = sabyenc3.decode_usenet_chunks(list_chunks[i])
    time1_new_disp = 1000 * (time.process_time() - time1_new)




    print("%15s  took  %4d ms" % ("yEnc C Current", time1_new_disp))
    print("%15s  took  %4d ms" % ("yEnc C Buffer", time2_new_disp))
    print("---")
