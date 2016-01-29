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

    def testDecode(self):
        d, c, x = _yenc.decode_string('r\x8f\x96\x96\x99J\xa1\x99\x9c\x96\x8eK')
        self.assertEquals(d, 'Hello world!')
        self.assertEquals(c, 3833259626L)



if __name__ == "__main__":
        logging.basicConfig(level=logging.INFO)
	unittest.main()
