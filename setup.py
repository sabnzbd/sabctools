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
import os
import sys
import platform
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils import log


with open("README.md", "r") as file_long_description:
    long_description = file_long_description.read()


class SAByEncBuild(build_ext):
    def build_extension(self, ext):
        # Try to determine the architecture to build for
        machine = platform.machine().lower()
        IS_ARM = machine.startswith("arm") or machine.startswith("aarch64")
        IS_ARM7 = machine.startswith("armv7")
        IS_X86 = machine in ["i386", "i686", "x86", "x86_64", "x64", "amd64"]
        IS_MACOS = sys.platform == "darwin"
        log.info("Detected: ARM=%s, ARM7=%s, x86=%s, macOS=%s", IS_ARM, IS_ARM7, IS_X86, IS_MACOS)

        # Determine compiler flags
        if self.compiler.compiler_type == "msvc":
            # LTCG not enabled due to issues seen with code generation where
            # different ISA extensions are selected for specific files
            ldflags = ["/OPT:REF", "/OPT:ICF"]
            cflags = ["/O2", "/GS-", "/GL-", "/Gy", "/sdl-", "/Oy", "/Oi"]
        else:
            # TODO: consider -flto - may require some extra testing
            ldflags = []
            cflags = [
                "-Wall",
                "-Wextra",
                "-Wno-unused-function",
                "-fomit-frame-pointer",
                "-fno-rtti",
                "-fno-exceptions",
                "-O3",
                "-fPIC",
                "-fwrapv",
            ]

        srcdeps_crc_common = ["yencode/common.h", "yencode/crc_common.h", "yencode/crc.h"]
        srcdeps_dec_common = ["yencode/common.h", "yencode/decoder_common.h", "yencode/decoder.h"]
        srcdeps_enc_common = ["yencode/common.h", "yencode/encoder_common.h", "yencode/encoder.h"]

        # build yencode/crcutil
        output_dir = os.path.dirname(self.build_lib)
        compiled_objects = []
        for source_files in [
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
                "gcc_arm_flags": (["-mfpu=neon"] if IS_ARM7 else []),
            },
            {
                "sources": ["yencode/decoder_neon.cc" if IS_ARM7 else "yencode/decoder_neon64.cc"],
                "depends": srcdeps_dec_common,
                "gcc_arm_flags": (["-mfpu=neon"] if IS_ARM7 else []),
            },
            {
                "sources": ["yencode/crc_arm.cc"],
                "depends": srcdeps_crc_common,
                "gcc_arm_flags": (["-march=armv8-a+crc"] if not IS_MACOS else []),
            },
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
            args = {"sources": source_files["sources"], "output_dir": output_dir, "extra_postargs": cflags[:]}
            if self.compiler.compiler_type == "msvc":
                if IS_X86 and "msvc_x86_flags" in source_files:
                    args["extra_postargs"] += source_files["msvc_x86_flags"]
            else:
                if "gcc_flags" in source_files:
                    args["extra_postargs"] += source_files["gcc_flags"]
                if IS_X86 and "gcc_x86_flags" in source_files:
                    args["extra_postargs"] += source_files["gcc_x86_flags"]
                if IS_ARM and "gcc_arm_flags" in source_files:
                    args["extra_postargs"] += source_files["gcc_arm_flags"]

            if "include_dirs" in source_files:
                args["include_dirs"] = source_files["include_dirs"]
            if "macros" in source_files:
                args["macros"] = source_files["macros"]

            self.compiler.compile(**args)
            compiled_objects += self.compiler.object_filenames(source_files["sources"], output_dir=output_dir)

        # attach to Extension
        ext.extra_link_args = ldflags + compiled_objects
        ext.depends = ["src/sabyenc3.h"] + compiled_objects

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
