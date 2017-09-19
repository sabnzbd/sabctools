#!/usr/bin/env python
# -*- coding: utf-8 -*-
##=============================================================================
 #
 # Copyright (C) 2003, 2011 Alessandro Duca <alessandro.duca@gmail.com>
 #
 # This library is free software; you can redistribute it and/or
 # modify it under the terms of the GNU Lesser General Public
 # License as published by the Free Software Foundation; either
 # version 2.1 of the License, or (at your option) any later version.
 #
 # This library is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 # Lesser General Public License for more details.
 #
 # You should have received a copy of the GNU Lesser General Public
 # License along with this library; if not, write to the Free Software
 # Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 #=============================================================================
 #
##=============================================================================

import os
import os.path
import sys
import time
import logging
import tempfile
import unittest
from binascii import crc32
from stat import *

import yenc
import _yenc

BLOCK_SIZE = 4096


class BaseTest(object):
    CMD_DATA = "dd if=/dev/urandom of=%s bs=1b count=%d"

    def setUp(self):
        self.open_file_e = tempfile.NamedTemporaryFile()
        self.open_file_o = tempfile.NamedTemporaryFile()

        self.FILE_E = self.open_file_e.name
        self.FILE_O = self.open_file_o.name

        os.system(self.CMD_DATA % (self.FILE_E, 128))
        os.system(self.CMD_DATA % (self.FILE_O, 129))

    def tearDown(self):
        self.open_file_e.close()
        self.open_file_o.close()

        for basename in (self.FILE_E, self.FILE_O):
            for x in ('.out', '.dec'):
                if os.path.exists(basename + x):
                    os.unlink(basename + x)

    def _readFile(self, filename):
        file_in = open(filename, 'rb')

        data = file_in.read()
        file_in.close()
        return data, "%08x" % (crc32(data) & 0xffffffffL)


class TestLowLevel(unittest.TestCase):

    def testEncode(self):
        e, c, z = _yenc.encode_string('Hello world!')
        self.assertEquals(e, 'r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK')
        self.assertEquals(c, 3833259626L)

    def testDecode(self):
        d, c, x = _yenc.decode_string('r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK')
        self.assertEquals(d, 'Hello world!')
        self.assertEquals(c, 3833259626L)


class TestFileFunctions(BaseTest, unittest.TestCase):

    def _testEncodeFile(self, filename):
        data, crc =  self._readFile(filename)
        file_in = open(filename, 'rb')
        file_out = open(filename + ".out", 'wb')

        bytes_out, crc_out = yenc.encode(file_in, file_out, len(data))

        self.assertEquals(crc, crc_out)


    def testEncodeE(self):
        self._testEncodeFile(self.FILE_E)


    def testEncodeO(self):
        self._testEncodeFile(self.FILE_O)


    def _testDecodeFile(self, filename):
        data, crc =  self._readFile(filename)
        file_in = open(filename, 'rb')
        file_out = open(filename + ".out", 'wb')

        bytes_out, crc_out = yenc.encode(file_in, file_out, len(data))

        file_in = open(filename + ".out", 'rb')
        file_out = open(filename + ".dec", 'wb')
        bytes_dec, crc_dec = yenc.decode(file_in, file_out)

        self.assertEquals(crc, crc_dec)

    def testDecodeE(self):
        self._testDecodeFile(self.FILE_E)

    def testDecodeO(self):
        self._testDecodeFile(self.FILE_O)


class TestEncoderDecoderOnFile(BaseTest, unittest.TestCase):

    def _testEncoderDecoder(self, filename):
        file_data, crc =  self._readFile(filename)

        file_out = open(filename + '.out', 'wb')
        file_in = open(filename, 'rb')

        encoder = yenc.Encoder(file_out)
        data = file_in.read(BLOCK_SIZE)
        while(len(data)):
            encoder.feed(data)
            data = file_in.read(BLOCK_SIZE)
        file_in.close()
        encoder.terminate()
        logging.info("orig: %s enc: %s" %(crc, encoder.getCrc32()))
        self.assertEquals(crc, encoder.getCrc32())

        # deleting forces files to be flushed
        del encoder

        file_in = open(filename + '.out', 'rb')
        file_out = open(filename + '.dec', 'wb')

        decoder = yenc.Decoder(file_out)
        data = file_in.read(BLOCK_SIZE)
        while(len(data) > 0):
            decoder.feed(data)
            data = file_in.read(BLOCK_SIZE)
        file_in.close()
        decoder.flush()
        logging.info("orig: %s dec: %s" %(crc, decoder.getCrc32()))
        self.assertEquals(crc, decoder.getCrc32())

        # deleting forces files to be flushed
        # if __del__ is not called further tests are going to fail
        del decoder

        data_dec, crc_dec = self._readFile(filename + '.dec')
        self.assertEquals(file_data, data_dec)
        self.assertEquals(crc, crc_dec)

    def testEncoderDecoderE(self):
        self._testEncoderDecoder(self.FILE_E)

    def testEncoderDecoderO(self):
        self._testEncoderDecoder(self.FILE_O)

    def testEncoderClosed(self):
        encoder = yenc.Encoder(open('afile.out', 'wb'))
        encoder.feed('some data')
        encoder.close()
        self.assertFalse(encoder._output_file)
        self.assertRaises(IOError, encoder.feed, ('some data'))

    def testEncoderTerminated(self):
        encoder = yenc.Encoder(open('afile.out', 'wb'))
        encoder.terminate()
        self.assertRaises(IOError, encoder.feed, ('some data'))

    def testDecoderClose(self):
        decoder = yenc.Decoder(open('afile.out', 'wb'))
        decoder.feed('some data')
        decoder.close()
        self.assertFalse(decoder._output_file)


class TestEncoderDecoderInMemory(unittest.TestCase):

    def testEncodeInMemory(self):
        """ Checks simple encoding in memory
        """
        encoder = yenc.Encoder()
        encoder.feed('Hello world!')
        self.assertEquals('r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK', encoder.getEncoded())
        self.assertEquals(encoder.getCrc32(), "%08x" % (crc32("Hello world!") & 0xffffffff))

    def testEncodeAndWriteInMemory(self):
        pass

    def testDecodeInMemory(self):
        decoder = yenc.Decoder()
        decoder.feed('r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK')
        self.assertEquals("Hello world!", decoder.getDecoded())
        self.assertEquals(decoder.getCrc32(), "%08x" % (crc32("Hello world!") & 0xffffffff))

    def testDecodeAndWriteInMemory(self):
        pass

    def testEncoderCloseInMemory(self):
        encoder = yenc.Encoder()
        self.assertRaises(ValueError, encoder.close)

    def testDecoderCloseInMemory(self):
        decoder = yenc.Decoder()
        self.assertRaises(ValueError, decoder.close)

    def testEncoderFlushInMemory(self):
        encoder = yenc.Encoder()
        self.assertRaises(ValueError, encoder.flush)

    def testDecodeFlushInMemory(self):
        decoder = yenc.Decoder()
        self.assertRaises(ValueError, decoder.flush)



if __name__ == "__main__":
        logging.basicConfig(level=logging.INFO)
	unittest.main()
