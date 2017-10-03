#!/usr/bin/python -OO
# Copyright 2008-2017 The SABnzbd-Team <team@sabnzbd.org>
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

from setuptools import setup, Extension

setup(
    name            = "subprocessww",
    version         = "0.1",
    author          = "Safihre",
    author_email    = "safihre@sabnzbd.org",
    url             = "https://github.com/sabnzbd/sabbuild/tree/master/builder/win/subprocessww",
    license         = "LGPLv3",
    py_modules       = ['subprocessww'],
    ext_modules     = [Extension("_subprocessww", ["src/_subprocessww.c"])],
    classifiers     = [
        "Programming Language :: Python",
        "Programming Language :: Python :: 2.7",
        "Programming Language :: C",
        "License :: OSI Approved :: GNU Lesser General Public License v3 (LGPLv3)",
        "Operating System :: Microsoft :: Windows",
        "Development Status :: 5 - Production/Stable",
        "Environment :: Plugins",
        "Environment :: Win32 (MS Windows)",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    description     = "Version of subprocess with patches to allow unicode arguments",
    long_description = """
Applied the C-patch proposed in https://bugs.python.org/issue1759845 to the most recent (3 October 2017) version of _subprocess.c.

This allows Unicode parameters to be passed to POpen on Python 2.7.

Usage:
------------

.. code:: python


    # Use patched version of subprocess module for Unicode on Windows
    import subprocessww

    # Load the regular POpen, which is now patched
    from subprocess import Popen


2017 The SABnzbd Team <team@sabnzbd.org>
"""
)

