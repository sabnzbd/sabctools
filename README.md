
SABCTools - C implementations of functions for use within SABnzbd
===============================

This module implements the main sets of C implementations that are used within SABnzbd: 
* yEnc decoding and encoding using SIMD routines
* CRC32 calculations
* Non-blocking SSL-socket reading
* Positional file writing
* Marking files as sparse

Of course, they can also be used in any other application.

## yEnc decoding and encoding using SIMD routines
yEnc decoding and encoding performed by using [rapidyenc](https://github.com/animetosho/rapidyenc) from animetosho,
which utilizes x86/ARM/RISC-V SIMD optimised routines if such CPU features are available.

## CRC32 calculations
Also from rapidyenc, which uses the `crcutil` library and a PCLMULQDQ/ARMv8-CRC folding
approach for very fast CRC calculations.

See `src/rapidyenc/VENDOR.md` for the vendored version.

## Non-blocking SSL-socket reading
When Python reads data from a non-blocking SSL socket, it is limited to receiving 16K data at once. This module implements a patched version that can read as much data is available at once.
For more details, see the [cpython pull request](https://github.com/python/cpython/pull/31492).

## Positional file writing
`sabctools.FileWriter` opens a file for writing at absolute offsets, which several threads can do at once without holding a lock:
```python
writer = sabctools.FileWriter(path)
writer.preallocate(size)    # set the length, marking the file sparse where needed
writer.write(data, offset)  # short writes are retried internally
writer.close()              # idempotent, and waits for writes still in flight
```
It owns its own descriptor, so nothing outside can close it while a write is in progress. On Windows the writes use `WriteFile` with an `OVERLAPPED` offset, because `os.pwrite` is not available there.

`sabctools.write_stats()` returns totals for every write through every `FileWriter` since import — `count`, `bytes`, `nanos` spent inside the write itself, and `max_nanos` for the slowest one. Only `write()` is timed, and closing a file does not subtract what it wrote.

The decoder can write into one directly: pass a `FileWriter` as the `sink` argument of `Decoder.expect(context, sink)`, and each decoded body is written at the offset given by its yEnc headers rather than returned as a `bytearray`.

## Marking files as sparse
Uses Windows specific system calls to mark files as sparse and set the desired size.
On other platforms the same is achieved by calling `truncate`.
Superseded by `FileWriter.preallocate`, but kept for existing callers.

## Utility functions
Use `sabctools.bytearray_malloc(size)` to get an `bytearray` that is uninitialized (not set to `0`'s). 
This is much faster than the built-in `bytearray(size)` because the data inside the new `bytearray` will be whatever is present in the memory block.

Use `sabctools.rarfile_rar3_s2k` as a native replacement for `rarfile` via `rarfile.rar3_s2k = sabctools.rarfile_rar3_s2k`.   
It provides a significant speed increase for decrypting RAR4 headers when the password length exceeds 28 characters.

# Installing

As simple as running:
```
pip install sabctools --upgrade
```
When you want to compile from sources, you can run in the `sabctools` directory:
```
pip install .
```

> [!NOTE]
> You need a compiler that supports at least C++17 to compile the extension.

## SIMD detection

To see which SIMD set was detected on your system, run:
```
python -c "import sabctools; print(sabctools.simd);"
```

The CRC32 routines are selected independently of the yEnc ones, so they can report a
different set:
```
python -c "import sabctools; print(sabctools.crc_simd);"
```
Either is empty when no accelerated implementation was available for this CPU and a
generic one is in use.

## OpenSSL detection

To see if we could link to OpenSSL library on your system, run:
```
python -c "import sabctools; print(sabctools.openssl_linked);"
```

# Testing

For testing we use `pytest` (install via `pip install --group test`) and test can simply be executed by browsing to the `sabctools` directory and running:
```
pytest
```
Note that tests can fail if `git` modified the line endings of data files when checking out the repository!