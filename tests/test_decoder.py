#import pytest
import re
import sabyenc
import _yenc


def test_regular():
    data_chunks, data_bytes = read_and_split('test_regular.txt')
    assert old_yenc(data_chunks) == sabyenc_wrapper(data_chunks, data_bytes)
    data_chunks, data_bytes = read_and_split('test_regular_2.txt')
    assert old_yenc(data_chunks) == sabyenc_wrapper(data_chunks, data_bytes)

def test_regular_small_chunks():
    # Chunks of 512 chars
    data_chunks, data_bytes = read_and_split('test_regular.txt', 9)
    assert old_yenc(data_chunks) == sabyenc_wrapper(data_chunks, data_bytes)

def test_single_part():
    data_chunks, data_bytes = read_and_split('test_single_part.txt')
    decoded_data, filename, crc_correct = sabyenc_wrapper(data_chunks, data_bytes)
    assert filename == 'logo.gif'
    assert crc_correct == True
    assert len(decoded_data) == 16335

def test_partial():
    data_chunks, data_bytes = read_and_split('test_partial.txt')
    decoded_data, filename, crc_correct = sabyenc_wrapper(data_chunks, data_bytes)
    assert filename == '90E2Sdvsmds0801dvsmds90E.part06.rar'
    assert crc_correct == False
    assert len(decoded_data) == 587

def test_special_chars():
    data_chunks, data_bytes = read_and_split('test_special_chars.txt')
    # We only compare the data and the filename
    assert old_yenc(data_chunks) == sabyenc_wrapper(data_chunks, data_bytes)

def test_bad_crc():
    data_chunks, data_bytes = read_and_split('test_badcrc.txt')
    # We only compare the data and the filename
    assert old_yenc(data_chunks) == sabyenc_wrapper(data_chunks, data_bytes)

###################
# SUPPORT FUNCTIONS
###################
def read_and_split(filename, chunk_size=14):
    # Default to chunks of 16K, as used in SSL
    with open('yencfiles/%s' % filename, 'rb') as yencfile:
        data_raw = yencfile.read()
        data_bytes = len(data_raw)
        n = 2**chunk_size
        data_chunks = [data_raw[i:i+n] for i in range(0, len(data_raw), n)]
    return data_chunks, data_bytes

def sabyenc_wrapper(data_chunks, data_bytes):
    """ CRC's are """
    decoded_data, filename, crc_calc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, data_bytes)
    return decoded_data, filename, crc_correct

def old_yenc(data_chunks):
    """ Use the older decoder to verify the new one """
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
    decoded_data, partcrc = _yenc.decode_string(''.join(data))[:2]
    partcrc = '%08X' % ((partcrc ^ -1) & 2 ** 32L - 1)

    if ypart:
        crcname = 'pcrc32'
    else:
        crcname = 'crc32'

    if crcname in yend:
        _partcrc = '0' * (8 - len(yend[crcname])) + yend[crcname].upper()
    else:
        _partcrc = None
    return decoded_data, yenc_name_fixer(ybegin['name']), _partcrc == partcrc

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


def yenc_name_fixer(p):
    """ Return Unicode name of 8bit ASCII string, first try utf-8, then cp1252 """
    try:
        return p.decode('utf-8')
    except:
        return p.decode('cp1252', errors='replace').replace('?', '!')
