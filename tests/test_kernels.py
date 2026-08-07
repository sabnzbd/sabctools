import os
import pathlib
import re

import pytest
import sabctools

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDORED_HEADER = ROOT / "src" / "rapidyenc" / "rapidyenc.h"
MODULE_SOURCE = ROOT / "src" / "sabctools.cc"

ENCODE_DECODE_KERNELS = ("", "SSE2", "SSSE3", "AVX", "AVX2", "AVX512VL+VBMI2", "NEON", "RVV")
CRC_KERNELS = ("", "PCLMULQDQ", "VPCLMULQDQ", "ARMv8-CRC", "ARMv8-CRC+PMULL", "Zbc")

# An empty kernel name is RYKERN_GENERIC, which is a legitimate answer on hardware with
# nothing to offer - RISC-V without the vector or Zbc extensions, say. Our runners are not
# that: every one is x86_64 or arm64, where SSE2 and NEON are baseline and the CRC kernels
# have been available for well over a decade. So empty there means detection regressed,
# and is worth failing on. GITHUB_ACTIONS rather than CI, because it is those runners we
# know the capabilities of.
ON_GITHUB_ACTIONS = os.environ.get("GITHUB_ACTIONS") == "true"


def test_simd_reports_an_encode_decode_kernel():
    # One mapping names both families and their values interleave, so check the
    # kernel stays in its own family rather than merely being a name we know.
    assert sabctools.simd in ENCODE_DECODE_KERNELS
    if ON_GITHUB_ACTIONS:
        assert sabctools.simd, "no yEnc SIMD kernel detected on a runner that has one"


def test_crc_simd_reports_a_crc_kernel():
    assert sabctools.crc_simd in CRC_KERNELS
    if ON_GITHUB_ACTIONS:
        assert sabctools.crc_simd, "no CRC32 SIMD kernel detected on a runner that has one"


def test_every_vendored_kernel_is_named():
    """Fail when re-vendoring rapidyenc introduces a kernel we do not name.

    kernel_name() matches exactly and answers "unknown" for anything else, so a new
    RYKERN_* would otherwise go unnoticed until someone wondered why their CPU reports
    nothing useful. The opposite case - a constant upstream drops - needs no test: the
    switch stops compiling.
    """
    defined = set(re.findall(r"#define\s+(RYKERN_\w+)", VENDORED_HEADER.read_text()))
    named = set(re.findall(r"case\s+(RYKERN_\w+)\s*:", MODULE_SOURCE.read_text()))

    assert defined, "no RYKERN_* defines found; has the header moved?"
    assert defined <= named, "kernel_name() in src/sabctools.cc does not name: %s" % ", ".join(sorted(defined - named))
