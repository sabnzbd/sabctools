
SABCTools - C implementations of functions for use within SABnzbd  
===============================  
  
This module implements three main sets of C implementations that are used within SABnzbd:   
* yEnc decoding and encoding using SIMD routines  
* CRC32 calculations  
* Non-blocking SSL-socket reading  
* Marking files as sparse

Of course, they can also be used in any other application.

## yEnc decoding and encoding using SIMD routines
yEnc decoding and encoding performed by using [yencode](https://github.com/animetosho/node-yencode) from animetosho,   
which utilizes x86/ARM SIMD optimised routines if such CPU features are available.  
  
## CRC32 calculations
We used the `crcutil` library for very fast CRC calculations.

## Non-blocking SSL-socket reading  
When Python reads data from a non-blocking SSL socket, it is limited to receiving 16K data at once. This module implements a patched version that can read as much data is available at once.
For more details, see the [cpython pull request](https://github.com/python/cpython/pull/31492).

## Marking files as sparse
Uses Windows specific system calls to mark files as sparse and set the desired size.
On other platforms the same is achieved by calling `truncate`.

## Utility functions
Use `sabctools.bytearray_malloc(size)` to get an `bytearray` that is uninitialized (not set to `0`'s). 
This is much faster than the built-in `bytearray(size)` because the data inside the new `bytearray` will be whatever is present in the memory block.
  
# Installing  
  
As simple as running:  
```  
pip install sabctools --upgrade  
```  
When you want to compile from sources, you can run in the `sabctools` directory:  
```  
pip install .  
```  

## SIMD detection  

To see which SIMD set was detected on your system, run:  
```  
python -c "import sabctools; print(sabctools.simd);"  
```  
  
## OpenSSL detection  

To see if we could link to OpenSSL library on your system, run:  
```  
python -c "import sabctools; print(sabctools.openssl_linked);"  
```  

# Testing  
  
For testing we use `pytest` (install via `pip install -r tests/requirements.txt`) and test can simply be executed by browsing to the `sabctools` directory and running:  
```  
pytest  
```  
Note that tests can fail if `git` modified the line endings of data files when checking out the repository!  