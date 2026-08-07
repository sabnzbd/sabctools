#!/usr/bin/env python3
# Copyright 2007-2026 The SABnzbd-Team (sabnzbd.org)
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

"""
Re-vendor src/rapidyenc/ from rapidyenc.

    python tools/vendor_rapidyenc.py                # the pinned ref below
    python tools/vendor_rapidyenc.py --ref v1.1.1   # a tag or branch
    python tools/vendor_rapidyenc.py --ref 27f435a  # or a commit

Leaves the result in the working tree for review; does not commit.
"""

import datetime
import os
import sys
import tempfile

import vendor_common

REPO = "https://github.com/animetosho/rapidyenc.git"
REF = "27f435afa0a3a15995001219beca08de29f87a0f"

DEST = os.path.join(vendor_common.ROOT, "src", "rapidyenc")

# The per-ISA kernels, the crcutil CRC32 backend they build on, and the public C API
# that wraps both. Upstream's CMakeLists.txt is what our own CMakeLists.txt drives.
#
# Nothing needs pruning afterwards: upstream's crcutil-1.0/ is already stripped to the
# sources its CMake compiles, and tool/ is simply not copied.
COPY_TREES = ("src", "crcutil-1.0")
COPY_FILES = ("CMakeLists.txt", "rapidyenc.h", "rapidyenc.cc", "README.md")


def apply_patches():
    """No local patches.

    Upstream leaves CMAKE_MSVC_RUNTIME_LIBRARY alone, so its default of /MD already suits
    a CPython extension and there is nothing to patch for that. Kept as the place to add
    one, and vendor_common.patch() fails loudly once a patch stops matching.
    """


def write_vendor_notes(commit: str, ref: str):
    with open(os.path.join(DEST, "VENDOR.md"), "w", encoding="utf-8") as handle:
        handle.write(
            """# Vendored rapidyenc

| | |
|---|---|
| Upstream | {repo} |
| Ref | `{ref}` |
| Commit | `{commit}` |
| Vendored | {date} |

[animetosho/rapidyenc](https://github.com/animetosho/rapidyenc) is the library form of
node-yencode: SIMD yEnc encoding and decoding, and a CRC32 implementation built on
[crcutil](https://code.google.com/p/crcutil/) and zlib-ng's folding approach.

## Licensing

rapidyenc is Public Domain / CC0; the bundled `crcutil-1.0/` is Apache-2.0, and the folding
CRC32 came from zlib-ng under the zlib licence. All are compatible with sabctools' GPL-2.0-or-later.

## Updating

```bash
python tools/vendor_rapidyenc.py --ref <tag, branch or commit>
```

Then review the diff and rebuild. Update the `REF` default in `tools/vendor_rapidyenc.py` to
match, so a plain re-run reproduces the same tree.

## How it is built

Our top-level `CMakeLists.txt` runs upstream's own CMake as a nested project to produce the
`rapidyenc` static library, and links it into the extension. Upstream owns the per-ISA flag
matrix - which files get `-mavx2`, which get `-march=armv8-a+crc`, and which ISA probes have
to pass first - so re-vendoring picks up new kernels without any change here.

sabctools calls the public C API in `rapidyenc.h` (`rapidyenc_encode_ex`, `rapidyenc_decode_incremental`,
`rapidyenc_crc*`), not the `RapidYenc::` headers under `src/`. Those are upstream's internals
and are not part of its compatibility promise.

### Local patches

None. The vendoring script fails loudly if a patch it carries stops matching, so this
section is the one to check when adding one.
""".format(repo=REPO, ref=ref, commit=commit, date=datetime.date.today().isoformat())
        )


def main():
    arguments = vendor_common.parse_args(__doc__, REF, REPO)

    with tempfile.TemporaryDirectory() as temporary:
        checkout = os.path.join(temporary, "rapidyenc")
        print("==> Fetching %s at %s" % (arguments.repo, arguments.ref))
        commit = vendor_common.fetch(arguments.repo, arguments.ref, checkout)
        print("==> Resolved to %s" % commit)

        print("==> Replacing %s" % DEST)
        vendor_common.copy_sources(checkout, DEST, COPY_TREES, COPY_FILES)
        apply_patches()
        write_vendor_notes(commit, arguments.ref)

    print("==> Done. src/rapidyenc/ now at %s" % commit)
    print("    Review with: git status && git diff --stat")
    print("    Then rebuild: pip install . -v")


if __name__ == "__main__":
    sys.exit(main())
