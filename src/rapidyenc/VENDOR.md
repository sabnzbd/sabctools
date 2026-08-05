# Vendored rapidyenc

| | |
|---|---|
| Upstream | https://github.com/animetosho/rapidyenc.git |
| Ref | `27f435afa0a3a15995001219beca08de29f87a0f` |
| Commit | `27f435afa0a3a15995001219beca08de29f87a0f` |
| Vendored | 2026-08-06 |

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
