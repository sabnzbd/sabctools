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
from setuptools.command.build_ext import build_ext
import sysconfig
import platform

with open("README.md", "r") as file_long_description:
    long_description = file_long_description.read()

BUILD_DIR = "build"
machine = platform.machine().lower()  # TODO: this is unreliable, need something better to detect target architecture
is_armv7 = machine.startswith("armv7")  # TODO: can armv8_ still be AArch32?
is_arm = is_armv7 or machine.startswith("arm") or machine.startswith("aarch64")
is_x86 = machine in ["i386", "i686", "x86", "x86_64", "x64", "amd64"]


class SAByEncBuild(build_ext):
    def build_extension(self, ext):
        # determine compiler flags
        compiler = self.compiler
        if compiler.compiler_type == "msvc":
            # LTCG not enabled due to issues seen with code generation where different ISA extensions are selected for specific files
            base_ldflags = ["/OPT:REF", "/OPT:ICF"]
            cflags = ["/O2", "/GS-", "/Gy", "/sdl-", "/Oy", "/Oi"]
        else:
            # TODO: consider -flto - may require some extra testing
            base_ldflags = sysconfig.get_config_var("LDFLAGS").split()
            cflags = [
                "-Wall",
                "-Wextra",
                "-Wno-unused-function",
                "-fomit-frame-pointer",
                "-fno-rtti",
                "-fno-exceptions",
                "-O3",
                "-fPIC",
            ] + sysconfig.get_config_var("CFLAGS").split()

        srcdeps_crc_common = ["yencode/common.h", "yencode/crc_common.h", "yencode/crc.h"]
        srcdeps_dec_common = ["yencode/common.h", "yencode/decoder_common.h", "yencode/decoder.h"]
        srcdeps_enc_common = ["yencode/common.h", "yencode/encoder_common.h", "yencode/encoder.h"]
        # build yencode/crcutil
        # TODO: consider multi-threading + only compile modified sources
        objects = []
        for obj in [
            {
                "sources": ["yencode/encoder_sse2.cc"],
                "depends": srcdeps_enc_common + ["encoder_sse_base.h"],
                "gcc_x86_flags": ["-msse2"],
            },
            {
                "sources": ["yencode/decoder_sse2.cc"],
                "depends": srcdeps_dec_common + ["decoder_sse_base.h"],
                "gcc_x86_flags": ["-msse2"],
            },
            {
                "sources": ["yencode/encoder_ssse3.cc"],
                "depends": srcdeps_enc_common + ["encoder_sse_base.h"],
                "gcc_x86_flags": ["-mssse3"],
            },
            {
                "sources": ["yencode/decoder_ssse3.cc"],
                "depends": srcdeps_dec_common + ["decoder_sse_base.h"],
                "gcc_x86_flags": ["-mssse3"],
            },
            {
                "sources": ["yencode/crc_folding.cc"],
                "depends": srcdeps_crc_common,
                "gcc_x86_flags": ["-mssse3", "-msse4.1", "-mpclmul"],
            },
            {
                "sources": ["yencode/encoder_avx.cc"],
                "depends": srcdeps_enc_common + ["encoder_sse_base.h"],
                "gcc_x86_flags": ["-mavx", "-mpopcnt"],
                "msvc_x86_flags": ["/arch:AVX"],
            },
            {
                "sources": ["yencode/decoder_avx.cc"],
                "depends": srcdeps_dec_common + ["decoder_sse_base.h"],
                "gcc_x86_flags": ["-mavx", "-mpopcnt"],
                "msvc_x86_flags": ["/arch:AVX"],
            },
            {
                "sources": ["yencode/encoder_avx2.cc"],
                "depends": srcdeps_enc_common + ["encoder_avx_base.h"],
                "gcc_x86_flags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
                "msvc_x86_flags": ["/arch:AVX2"],
            },
            {
                "sources": ["yencode/decoder_avx2.cc"],
                "depends": srcdeps_dec_common + ["decoder_avx2_base.h"],
                "gcc_x86_flags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
                "msvc_x86_flags": ["/arch:AVX2"],
            },
            {
                "sources": ["yencode/encoder_neon.cc"],
                "depends": srcdeps_enc_common,
                "gcc_arm_flags": (["-mfpu=neon"] if is_armv7 else []),
            },
            {
                "sources": ["yencode/decoder_neon.cc" if is_armv7 else "yencode/decoder_neon64.cc"],
                "depends": srcdeps_dec_common,
                "gcc_arm_flags": (["-mfpu=neon"] if is_armv7 else []),
            },
            {"sources": ["yencode/crc_arm.cc"], "depends": srcdeps_crc_common, "gcc_arm_flags": ["-march=armv8-a+crc"]},
            {
                "sources": [
                    "crcutil-1.0/code/crc32c_sse4.cc",
                    "crcutil-1.0/code/multiword_64_64_cl_i386_mmx.cc",
                    "crcutil-1.0/code/multiword_64_64_gcc_amd64_asm.cc",
                    "crcutil-1.0/code/multiword_64_64_gcc_i386_mmx.cc",
                    "crcutil-1.0/code/multiword_64_64_intrinsic_i386_mmx.cc",
                    "crcutil-1.0/code/multiword_128_64_gcc_amd64_sse2.cc",
                    "crcutil-1.0/examples/interface.cc",
                ],
                "gcc_flags": ["-Wno-expansion-to-defined"],
                "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"],
                "macros": [("CRCUTIL_USE_MM_CRC32", "0")],
            },
        ]:
            args = {"sources": obj["sources"], "output_dir": BUILD_DIR}
            args["extra_postargs"] = cflags[:]
            if compiler.compiler_type == "msvc":
                if is_x86 and "msvc_x86_flags" in obj:
                    args["extra_postargs"] += obj["msvc_x86_flags"]
            else:
                if "gcc_flags" in obj:
                    args["extra_postargs"] += obj["gcc_flags"]
                if is_x86 and "gcc_x86_flags" in obj:
                    args["extra_postargs"] += obj["gcc_x86_flags"]
                if is_arm and "gcc_arm_flags" in obj:
                    args["extra_postargs"] += obj["gcc_arm_flags"]

            if "include_dirs" in obj:
                args["include_dirs"] = obj["include_dirs"]
            if "macros" in obj:
                args["macros"] = obj["macros"]
            compiler.compile(**args)
            objects += compiler.object_filenames(obj["sources"], output_dir=BUILD_DIR)

        # attach to Extension
        ext.extra_link_args = base_ldflags + objects
        ext.depends = ["src/sabyenc3.h"] + objects

        # proceed with regular Extension build
        super(SAByEncBuild, self).build_extension(ext)


setup(
    name="sabyenc3",
    version="5.0.0",
    author="Safihre",
    author_email="safihre@sabnzbd.org",
    url="https://github.com/sabnzbd/sabyenc/",
    license="LGPLv3",
    package_dir={"sabyenc3": "src"},
    ext_modules=[
        Extension(
            "sabyenc3",
            ["src/sabyenc3.c", "yencode/platform.cc", "yencode/encoder.cc", "yencode/decoder.cc", "yencode/crc.cc"],
            include_dirs=["crcutil-1.0/code", "crcutil-1.0/examples"],
        )
    ],
    cmdclass={"build_ext": SAByEncBuild},
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
