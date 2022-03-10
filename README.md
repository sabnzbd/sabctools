SABYenc 3 - yEnc decoding of usenet data using SIMD routines
===============================

Modification of the original [yenc](https://pypi.org/project/yenc/) module for use within SABnzbd.
The module was extended to do header parsing and full yEnc decoding from a Python
list of chunks, the way in which data is retrieved from usenet.
This is particularly beneficial when SSL is enabled, which limits the size of each chunk to 16K. Parsing these chunks in python is much more costly.
Additionally, this module releases Python's GIL during decoding, greatly increasing performance of the overall download process.

Further improved by using [yencode](https://github.com/animetosho/node-yencode) from animetosho, which utilizes x86/ARM SIMD optimised routines if such CPU features are available.

Installing
===============================
As simple as running:
```
pip install sabyenc3 --upgrade
```
When you want to compile from sources, you can run in the `sabyenc` directory:
```
pip install .
```

SIMD detection
===============================
To see which SIMD set was detected on your system, run:
```
python -c "import sabyenc3; print(sabyenc3.simd);"
```

Testing
===============================
For testing we use `pytest` (install via `pip install -r tests/requirements.txt`) and test can simply be executed by browsing to the `sabyenc` directory and running:
```
pytest
```
Note that tests can fail if `git` modified the line endings of data files when checking out the repository!
