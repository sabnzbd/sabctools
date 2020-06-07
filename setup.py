#!/usr/bin/python3 -OO
# -*- coding: utf-8 -*-
# ============================================================================
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
# =============================================================================
#

from setuptools import setup, Extension

with open("README.md", "r") as file_long_description:
    long_description = file_long_description.read()

setup(
    name="sabyenc3",
    version="4.0.2",
    author="Safihre",
    author_email="safihre@sabnzbd.org",
    url="https://github.com/sabnzbd/sabyenc/",
    license="LGPLv3",
    package_dir={"sabyenc3": "src"},
    ext_modules=[Extension("sabyenc3", ["src/sabyenc3.c"])],
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 3 :: Only",
        "Programming Language :: C",
        "License :: OSI Approved :: GNU Lesser General Public License v3 (LGPLv3)",
        "Operating System :: Unix",
        "Development Status :: 5 - Production/Stable",
        "Environment :: Plugins",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: Communications :: Usenet News",
    ],
    description="yEnc Module for Python modified for SABnzbd",
    long_description=long_description,
    long_description_content_type="text/markdown",
)
