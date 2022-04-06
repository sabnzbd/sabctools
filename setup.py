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
import re
import tempfile
from distutils.ccompiler import CCompiler
from distutils.errors import CompileError
from typing import Type

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils import log


def autoconf_check(
    compiler: Type[CCompiler], include_check: str = None, define_check: str = None, flag_check: str = None
):
    """A makeshift Python version of the autoconf checks"""
    with tempfile.NamedTemporaryFile("w", suffix=".cc") as f:
        if include_check:
            log.info("==> Checking support for include: %s", include_check)
            f.write(f"#include <{include_check}>\n")

        if define_check:
            log.info("==> Checking support for define: %s", define_check)
            # Just let it crash
            f.write(f"#ifndef {define_check}\n")
            f.write(f"#error {define_check} not available!\n")
            f.write(f"#endif\n")

        extra_postargs = []
        if flag_check:
            log.info("==> Checking support for flag: %s", flag_check)
            extra_postargs.append(flag_check)

        f.write("int main (int argc, char **argv) { return 0; }")

        # Make sure contents are on disk
        f.flush()

        try:
            result_files = compiler.compile([f.name], extra_postargs=extra_postargs)
            log.info("==> Success!")
        except CompileError:
            log.info("==> Not available!")
            return False

        # Remove output file(s)
        for result_file in result_files:
            os.unlink(result_file)
    return True


class SAByEncBuild(build_ext):
    def build_extension(self, ext: Extension):
        # Try to determine the architecture to build for
        machine = platform.machine().lower()
        IS_X86 = machine in ["i386", "i686", "x86", "x86_64", "x64", "amd64"]
        IS_MACOS = sys.platform == "darwin"
        IS_ARM = machine.startswith("arm") or machine.startswith("aarch64")
        IS_AARCH64 = True

        log.info("==> Baseline detection: ARM=%s, x86=%s, macOS=%s", IS_ARM, IS_X86, IS_MACOS)

        # Determine compiler flags
        gcc_arm_neon_flags = []
        gcc_arm_crc_flags = []
        gcc_macros = []
        if self.compiler.compiler_type == "msvc":
            # LTCG not enabled due to issues seen with code generation where
            # different ISA extensions are selected for specific files
            ldflags = ["/OPT:REF", "/OPT:ICF"]
            cflags = ["/O2", "/GS-", "/Gy", "/sdl-", "/Oy", "/Oi"]
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
            # gcc before 4.3 did not support the "-std=c++11" flag
            # gcc before 4.7 called it "-std=c++0x"
            if autoconf_check(self.compiler, flag_check="-std=c++11"):
                cflags.append("-std=c++11")
                ext.extra_compile_args.append("-std=c++11")
            elif autoconf_check(self.compiler, flag_check="-std=c++0x"):
                cflags.append("-std=c++0x")
                ext.extra_compile_args.append("-std=c++0x")
            else:
                log.info("==> C++11 flag not available")

            # Verify specific flags for ARM chips
            # macOS M1 do not need any flags, they support everything
            if IS_ARM and not IS_MACOS:
                if not autoconf_check(self.compiler, include_check="sys/auxv.h"):
                    # We only tested this for GCC, might still be valid on MS-ARM compiler (see #38)
                    if autoconf_check(self.compiler, define_check="__GNUC__"):
                        log.info("==> On GGC and sys/auxv.h not available, setting UNSUPPORTED_PLATFORM_ARM=1")
                        ext.define_macros.append(("UNSUPPORTED_PLATFORM_ARM", "1"))
                        gcc_macros.append(("UNSUPPORTED_PLATFORM_ARM", "1"))
                        IS_ARM = False
                if not autoconf_check(self.compiler, define_check="__aarch64__"):
                    log.info("==> __aarch64__ not available, disabling 64bit extensions")
                    IS_AARCH64 = False
                if autoconf_check(self.compiler, flag_check="-march=armv8-a+crc"):
                    gcc_arm_crc_flags.append("-march=armv8-a+crc")
                if autoconf_check(self.compiler, flag_check="-mfpu=neon"):
                    gcc_arm_neon_flags.append("-mfpu=neon")

            # Check for special x32 case
            if (
                IS_X86 
                and not IS_MACOS 
                and autoconf_check(self.compiler, define_check="__ILP32__")
                and autoconf_check(self.compiler, define_check="__x86_64__")
            ):
                log.info("==> Detected x32 platform, setting CRCUTIL_USE_ASM=0")
                ext.define_macros.append(("CRCUTIL_USE_ASM", "0"))
                gcc_macros.append(("CRCUTIL_USE_ASM", "0"))

        srcdeps_crc_common = ["src/yencode/common.h", "src/yencode/crc_common.h", "src/yencode/crc.h"]
        srcdeps_dec_common = ["src/yencode/common.h", "src/yencode/decoder_common.h", "src/yencode/decoder.h"]
        srcdeps_enc_common = ["src/yencode/common.h", "src/yencode/encoder_common.h", "src/yencode/encoder.h"]

        # build yencode/crcutil
        output_dir = os.path.dirname(self.build_lib)
        compiled_objects = []
        for source_files in [
            {
                "sources": [
                    "src/yencode/platform.cc",
                    "src/yencode/encoder.cc",
                    "src/yencode/decoder.cc",
                    "src/yencode/crc.cc",
                ],
                "include_dirs": ["src/crcutil-1.0/code", "src/crcutil-1.0/examples"],
            },
            {
                "sources": ["src/yencode/encoder_sse2.cc"],
                "depends": srcdeps_enc_common + ["encoder_sse_base.h"],
                "gcc_x86_flags": ["-msse2"],
            },
            {
                "sources": ["src/yencode/decoder_sse2.cc"],
                "depends": srcdeps_dec_common + ["decoder_sse_base.h"],
                "gcc_x86_flags": ["-msse2"],
            },
            {
                "sources": ["src/yencode/encoder_ssse3.cc"],
                "depends": srcdeps_enc_common + ["encoder_sse_base.h"],
                "gcc_x86_flags": ["-mssse3"],
            },
            {
                "sources": ["src/yencode/decoder_ssse3.cc"],
                "depends": srcdeps_dec_common + ["decoder_sse_base.h"],
                "gcc_x86_flags": ["-mssse3"],
            },
            {
                "sources": ["src/yencode/crc_folding.cc"],
                "depends": srcdeps_crc_common,
                "gcc_x86_flags": ["-mssse3", "-msse4.1", "-mpclmul"],
            },
            {
                "sources": ["src/yencode/encoder_avx.cc"],
                "depends": srcdeps_enc_common + ["encoder_sse_base.h"],
                "gcc_x86_flags": ["-mavx", "-mpopcnt"],
                "msvc_x86_flags": ["/arch:AVX"],
            },
            {
                "sources": ["src/yencode/decoder_avx.cc"],
                "depends": srcdeps_dec_common + ["decoder_sse_base.h"],
                "gcc_x86_flags": ["-mavx", "-mpopcnt"],
                "msvc_x86_flags": ["/arch:AVX"],
            },
            {
                "sources": ["src/yencode/encoder_avx2.cc"],
                "depends": srcdeps_enc_common + ["encoder_avx_base.h"],
                "gcc_x86_flags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
                "msvc_x86_flags": ["/arch:AVX2"],
            },
            {
                "sources": ["src/yencode/decoder_avx2.cc"],
                "depends": srcdeps_dec_common + ["decoder_avx2_base.h"],
                "gcc_x86_flags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
                "msvc_x86_flags": ["/arch:AVX2"],
            },
            {
                "sources": ["src/yencode/encoder_neon.cc"],
                "depends": srcdeps_enc_common,
                "gcc_arm_flags": gcc_arm_neon_flags,
            },
            {
                "sources": ["src/yencode/decoder_neon64.cc" if IS_AARCH64 else "src/yencode/decoder_neon.cc"],
                "depends": srcdeps_dec_common,
                "gcc_arm_flags": gcc_arm_neon_flags,
            },
            {
                "sources": ["src/yencode/crc_arm.cc"],
                "depends": srcdeps_crc_common,
                "gcc_arm_flags": gcc_arm_crc_flags,
            },
            {
                "sources": [
                    "src/crcutil-1.0/code/crc32c_sse4.cc",
                    "src/crcutil-1.0/code/multiword_64_64_cl_i386_mmx.cc",
                    "src/crcutil-1.0/code/multiword_64_64_gcc_amd64_asm.cc",
                    "src/crcutil-1.0/code/multiword_64_64_gcc_i386_mmx.cc",
                    "src/crcutil-1.0/code/multiword_64_64_intrinsic_i386_mmx.cc",
                    "src/crcutil-1.0/code/multiword_128_64_gcc_amd64_sse2.cc",
                    "src/crcutil-1.0/examples/interface.cc",
                ],
                "gcc_flags": ["-Wno-expansion-to-defined", "-Wno-unused-parameter"],
                "include_dirs": ["src/crcutil-1.0/code", "src/crcutil-1.0/tests"],
                "macros": [("CRCUTIL_USE_MM_CRC32", "0")],
            },
        ]:
            args = {
                "sources": source_files["sources"],
                "output_dir": output_dir,
                "extra_postargs": cflags[:],
                "macros": gcc_macros[:],
            }
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
                args["macros"].extend(source_files["macros"])

            self.compiler.compile(**args)
            compiled_objects += self.compiler.object_filenames(source_files["sources"], output_dir=output_dir)

        # attach to Extension
        ext.extra_link_args = ldflags + compiled_objects
        ext.depends = ["src/sabyenc3.h"] + compiled_objects

        # proceed with regular Extension build
        super(SAByEncBuild, self).build_extension(ext)


# Load description
with open("README.md", "r") as file_long_description:
    long_description = file_long_description.read()

# Parse the version from the C sources
with open("src/sabyenc3.h", "r") as sabyenc_h:
    version = re.findall('#define SABYENC_VERSION +"([0-9xA-Z_.]+)"?', sabyenc_h.read())[0]

setup(
    name="sabyenc3",
    version=version,
    author="Safihre",
    author_email="safihre@sabnzbd.org",
    url="https://github.com/sabnzbd/sabyenc/",
    license="LGPLv3",
    package_dir={"sabyenc3": "src"},
    ext_modules=[
        Extension(
            "sabyenc3",
            ["src/sabyenc3.cc"],
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
    description="yEnc decoding of usenet data using SIMD routines",
    long_description=long_description,
    long_description_content_type="text/markdown",
)
