#!/usr/bin/python3 -OO
# Copyright 2007-2019 The SABnzbd-Team <team@sabnzbd.org>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

###################
# SUPPORT FUNCTIONS
###################
import binascii
import re
import pickle
import chardet
import sabyenc3


def correct_unknown_encoding(str_or_bytes_in):
    """ Files created on Windows but unpacked/repaired on
        linux can result in invalid filenames. Try to fix this
        encoding by going to bytes and then back to unicode again.
        Last resort we use chardet package
    """
    # If already string, back to bytes
    if not isinstance(str_or_bytes_in, bytes):
        str_or_bytes_in = str_or_bytes_in.encode('utf-8', 'surrogateescape')

    # Try simple bytes-to-string
    try:
        return str_or_bytes_in.decode("utf-8")
    except UnicodeDecodeError:
        try:
            # Try using 8-bit ASCII, if came from Windows
            return str_or_bytes_in.decode('ISO-8859-1')
        except ValueError:
            # Last resort we use the slow chardet package
            return str_or_bytes_in.decode(chardet.detect(str_or_bytes_in)['encoding'])


def read_and_split(filename, chunk_size=14):
    # Default to chunks of 16K, as used in SSL
    with open("tests/yencfiles/%s" % filename, "rb") as yencfile:
        data_raw = yencfile.read()
        data_bytes = len(data_raw)
        n = 2 ** chunk_size
        data_chunks = [data_raw[i : i + n] for i in range(0, len(data_raw), n)]
    return data_raw, data_chunks, data_bytes


def read_pickle(filename):
    with open(filename, "rb") as yencfile:
        try:
            data_chunks, data_bytes = pickle.load(yencfile, encoding="bytes")
        except:
            # Reset the pointer and try again
            yencfile.seek(0)
            data_chunks, data_bytes, lines = pickle.load(yencfile, encoding="bytes")
    return b"".join(data_chunks), data_chunks, data_bytes


def sabyenc3_wrapper(data_chunks, data_bytes):
    """ CRC's are """
    decoded_data, filename, crc_calc, crc_yenc, crc_correct = sabyenc3.decode_usenet_chunks(
        data_chunks, data_bytes
    )
    return decoded_data, correct_unknown_encoding(filename), crc_correct


def old_yenc(data_plain):
    """ Use the older decoder to verify the new one """
    data = []

    # Remove the NNTP-double-dot style
    new_lines = data_plain.split(b"\r\n")
    for i in range(len(new_lines)):
        if new_lines[i][:2] == b"..":
            new_lines[i] = new_lines[i][1:]
    if new_lines[-1] == b".":
        new_lines = new_lines[1:-1]
    data.extend(new_lines)

    # Parse the yEnc headers
    yenc, data = parse_yenc_data(data)
    ybegin, ypart, yend = yenc

    # Now we get the true flat data
    flat_yenc_data = b"".join(data)

    # Remove the escaped-chars
    for i in (0, 9, 10, 13, 27, 32, 46, 61):
        j = b"=%c" % (i + 64)
        flat_yenc_data = flat_yenc_data.replace(j, b"%c" % i)

    # Use the much faster translate function to do fast-subtract of 42
    from_bytes = b"".join([b"%c" % i for i in range(256)])
    to_bytes = b"".join([b"%c" % ((i + 256 - 42) % 256) for i in range(256)])
    translate_table = bytes.maketrans(from_bytes, to_bytes)
    decoded_data = flat_yenc_data.translate(translate_table)

    # Let's get the CRC
    crc = binascii.crc32(decoded_data)
    partcrc = "%08X" % (crc & 2 ** 32 - 1)

    if ypart:
        crcname = "pcrc32"
    else:
        crcname = "crc32"

    if crcname in yend:
        _partcrc = "0" * (8 - len(yend[crcname])) + yend[crcname].upper()
    else:
        _partcrc = None

    return decoded_data, ybegin["name"], _partcrc == partcrc


def parse_yenc_data(data):
    ybegin = None
    ypart = None
    yend = None

    # Check head
    for i in range(min(40, len(data))):
        try:
            if data[i].startswith(b"=ybegin "):
                splits = 3
                if data[i].find(b" part=") > 0:
                    splits += 1
                if data[i].find(b" total=") > 0:
                    splits += 1

                ybegin = get_yenc_data(data[i], splits)

                if data[i + 1].startswith(b"=ypart "):
                    ypart = get_yenc_data(data[i + 1])
                    data = data[i + 2 :]
                    break
                else:
                    data = data[i + 1 :]
                    break
        except IndexError:
            break

    # Check tail
    for i in range(-1, -11, -1):
        try:
            if data[i].startswith(b"=yend "):
                yend = get_yenc_data(data[i])
                data = data[:i]
                break
        except IndexError:
            break

    return ((ybegin, ypart, yend), data)


def get_yenc_data(line, splits=None):
    # Example: =ybegin part=1 line=128 size=123 name=-=DUMMY=- abc.par
    YSPLIT_RE = re.compile(b"([a-zA-Z0-9]+)=")

    fields = {}

    if splits:
        parts = YSPLIT_RE.split(line, splits)[1:]
    else:
        parts = YSPLIT_RE.split(line)[1:]

    if len(parts) % 2:
        return fields

    for i in range(0, len(parts), 2):
        key, value = parts[i], parts[i + 1]
        fields[correct_unknown_encoding(key)] = correct_unknown_encoding(value.strip())

    return fields


def yenc_subtract(char, subtract):
    """ Wrap-around for below 0 """
    char_diff = char - subtract
    if char_diff < 0:
        return 256 + char_diff
    return char_diff
