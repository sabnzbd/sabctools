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
from distutils import ccompiler
import sysconfig
import platform

with open("README.md", "r") as file_long_description:
    long_description = file_long_description.read()

BUILD_DIR = "build"
compiler = ccompiler.new_compiler()
if compiler.compiler_type == "msvc":
    base_ldflags = []
    cflags = ["/O2"]
else:
    base_ldflags = sysconfig.get_config_var('LDFLAGS').split()
    cflags = ["-Wno-unused-function", "-fomit-frame-pointer", "-O3", "-fPIC"] + sysconfig.get_config_var('CFLAGS').split()

machine = platform.machine().lower()  # TODO: this is unreliable, need something better to detect target architecture
is_armv7 = (machine.startswith("armv7"))  # TODO: can armv8_ still be AArch32?
is_arm = (is_armv7 or machine.startswith("arm") or machine.startswith("aarch64"))
is_x86 = (machine in ["i386", "i686", "x86", "x86_64", "x64", "amd64"])
objects = []
# TODO: move this to compile step so setup.py --help doesn't trigger compilation
# TODO: add depends
# TODO: consider other opts, like LTO
for obj in [
    {"sources": ["yencode/encoder_sse2.cc", "yencode/decoder_sse2.cc"], "gcc_x86_flags": ["-msse2"]},
    {"sources": ["yencode/encoder_ssse3.cc", "yencode/decoder_ssse3.cc"], "gcc_x86_flags": ["-mssse3"]},
    {"sources": ["yencode/crc_folding.cc"], "gcc_x86_flags": ["-mssse3", "-msse4.1", "-mpclmul"]},
    {"sources": ["yencode/encoder_avx.cc", "yencode/decoder_avx.cc"], "gcc_x86_flags": ["-mavx", "-mpopcnt"], "msvc_x86_flags": ["/arch:AVX"]},
    {"sources": ["yencode/encoder_avx2.cc", "yencode/decoder_avx2.cc"], "gcc_x86_flags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"], "msvc_x86_flags": ["/arch:AVX2"]},
    {"sources": ["yencode/encoder_neon.cc", ("yencode/decoder_neon.cc" if is_armv7 else "yencode/decoder_neon64.cc")], "gcc_arm_flags": (["-mfpu=neon"] if is_armv7 else [])},
    {"sources": ["yencode/crc_arm.cc"], "gcc_arm_flags": ["-march=armv8-a+crc"]},
    {"sources": [
        "crcutil-1.0/code/crc32c_sse4.cc",
        "crcutil-1.0/code/multiword_64_64_cl_i386_mmx.cc",
        "crcutil-1.0/code/multiword_64_64_gcc_amd64_asm.cc",
        "crcutil-1.0/code/multiword_64_64_gcc_i386_mmx.cc",
        "crcutil-1.0/code/multiword_64_64_intrinsic_i386_mmx.cc",
        "crcutil-1.0/code/multiword_128_64_gcc_amd64_sse2.cc",
        "crcutil-1.0/examples/interface.cc"
    ], "gcc_flags": ["-Wno-expansion-to-defined"], "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"], "macros": [("CRCUTIL_USE_MM_CRC32", "0")]}
]:
    args = {
        "sources": obj["sources"],
        "output_dir": BUILD_DIR
    }
    args["extra_preargs"] = cflags
    if compiler.compiler_type == "msvc":
        if is_x86 and "msvc_x86_flags" in obj:
            args["extra_preargs"] += obj["msvc_x86_flags"]
    else:
        if "gcc_flags" in obj:
            args["extra_preargs"] += obj["gcc_flags"]
        if is_x86 and "gcc_x86_flags" in obj:
            args["extra_preargs"] += obj["gcc_x86_flags"]
        if is_arm and "gcc_arm_flags" in obj:
            args["extra_preargs"] += obj["gcc_arm_flags"]
    
    if "include_dirs" in obj:
        args["include_dirs"] = obj["include_dirs"]
    if "macros" in obj:
        args["macros"] = obj["macros"]
    compiler.compile(**args)
    objects += compiler.object_filenames(obj["sources"], output_dir=BUILD_DIR)

setup(
    name="sabyenc3",
    version="4.0.2",
    author="Safihre",
    author_email="safihre@sabnzbd.org",
    url="https://github.com/sabnzbd/sabyenc/",
    license="LGPLv3",
    package_dir={"sabyenc3": "src"},
    ext_modules=[Extension(
        "sabyenc3",
        ["src/sabyenc3.c", "yencode/platform.cc", "yencode/encoder.cc", "yencode/decoder.cc", "yencode/crc.cc"],
        extra_link_args=objects,
        depends=["src/sabyenc3.h"] + objects,
        include_dirs=["crcutil-1.0/code","crcutil-1.0/examples"]
    )],
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
