#!/usr/bin/env python
# -*- coding: utf-8 -*-
##=============================================================================
 #
 # Copyright (C) 2003, 2011 Alessandro Duca <alessandro.duca@gmail.com>
 #
 # This library is free software; you can redistribute it and/or
 #modify it under the terms of the GNU Lesser General Public
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

from distutils.core import setup, Extension

setup(	
	name		 = "yenc",
	version		 = "0.4.0",
	author		 = "Alessandro Duca",
	author_email	 = "alessandro.duca@gmail.com",
        url		 = "https://bitbucket.org/dual75/yenc",
	license		 = "LGPL",
        platforms        = ["Unix"],
	package_dir	 = { '': 'lib' },
	py_modules	 = ["yenc"],
	ext_modules	 = [Extension("_yenc",["src/_yenc.c"],extra_compile_args=["-O2","-g"])],
        classifiers      = [
            "Programming Language :: Python",
            "Programming Language :: Python :: 2.5",
            "Programming Language :: Python :: 2.6",
            "Programming Language :: Python :: 2.7",
            "Programming Language :: C",
            "License :: OSI Approved :: GNU Library or Lesser General Public License (LGPL)",
            "Operating System :: Unix",
            "Development Status :: 4 - Beta",
            "Environment :: Other Environment",
            "Intended Audience :: Developers",
            "Topic :: Software Development :: Libraries :: Python Modules",
            "Topic :: Communications :: Usenet News"
            ],
	description	 = "yEnc Module for Python",
        long_description = """
yEnc Encoding/Decoding for Python
---------------------------------

This a fairly simple module, it provide only raw yEnc encoding/decoding with
builitin crc32 calculation. Header parsing, checkings and yenc formatting are 
left to you (see examples directory for possible implementations). 

Supports encoding and decoding directly to files or to memory buffers
with helper classes Encoder and Decoder.
"""

	)

