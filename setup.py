#!/usr/bin/env python
# -*- coding: utf-8 -*-
##=============================================================================
 #
 # Copyright (C) 2003, 2011 Alessandro Duca <alessandro.duca@gmail.com>
 # Modified in 2016 by Safihre <safihre@sabnzbd.org> for use within SABnzbd
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

from setuptools import setup, Extension

setup(
    name            = "sabyenc",
    version         = "3.3.3",
    author          = "Safihre",
    author_email    = "safihre@sabnzbd.org",
    url             = "https://github.com/sabnzbd/sabnzbd-yenc",
    license         = "LGPLv3",
    package_dir     = {'sabyenc': 'src'},
    ext_modules     = [Extension("sabyenc", ["src/sabyenc.c"])],
    classifiers     = [
        "Programming Language :: Python",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: C",
        "License :: OSI Approved :: GNU Lesser General Public License v3 (LGPLv3)",
        "Operating System :: Unix",
        "Development Status :: 5 - Production/Stable",
        "Environment :: Plugins",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: Communications :: Usenet News"
    ],
    description     = "yEnc Module for Python modified for SABnzbd",
    long_description = """
yEnc Decoding for Python
---------------------------------

Mofied the original yenc module by Alessandro Duca for use within SABnzbd.

The module was extended to do header parsing and full yEnc decoding from a Python
list of chunks, the way in which data is retrieved from usenet.

Currently CRC-checking of decoded data is disabled to allow for increased performance.
It can only be re-enabled by locally altering 'sabyenc.h' and setting 'CRC_CHECK 1'.
"""
)

