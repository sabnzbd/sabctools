import re
import binascii
import sabyenc

###################
# SUPPORT FUNCTIONS
###################

def read_and_split(filename, chunk_size=14):
    # Default to chunks of 16K, as used in SSL
    with open('tests/yencfiles/%s' % filename, 'rb') as yencfile:
        data_raw = yencfile.read()
        data_bytes = len(data_raw)
        n = 2**chunk_size
        data_chunks = [data_raw[i:i+n] for i in range(0, len(data_raw), n)]
    return data_raw, data_chunks, data_bytes

def sabyenc_wrapper(data_chunks, data_bytes):
    """ CRC's are """
    decoded_data, filename, crc_calc, crc_yenc, crc_correct = sabyenc.decode_usenet_chunks(data_chunks, data_bytes)
    return decoded_data, filename, crc_correct


def old_yenc(data_plain):
    """ Use the older decoder to verify the new one """
    YDEC_TRANS = ''.join([chr((i + 256 - 42) % 256) for i in xrange(256)])
    data = []
    new_lines = data_plain.split('\r\n')
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
    data = ''.join(data)
    for i in (0, 9, 10, 13, 27, 32, 46, 61):
        j = '=%c' % (i + 64)
        data = data.replace(j, chr(i))
    decoded_data = data.translate(YDEC_TRANS)
    crc = binascii.crc32(decoded_data)
    partcrc = '%08X' % (crc & 2 ** 32L - 1)

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
