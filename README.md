SABYenc 3 - yEnc Decoding for Python 3
===============================

Modified the original yenc module by Alessandro Duca <alessandro.duca@gmail.com>
for use within SABnzbd.

The module was extended to do header parsing and full yEnc decoding from a Python
list of chunks, the way in which data is retrieved from usenet.
This is particularly beneficial when SSL is enabled, which limits the size of each chunk to 16K. Parsing these chunks in python is much more costly.
Additionally, this module releases Python's GIL during decoding, greatly increasing performance of the overall download process.

---

This module only works with Python 3, the source code of the Python 2 version (`sabyenc`) can be found [here](https://github.com/sabnzbd/sabyenc/tree/sabyenc-python2).

Installing
===============================
As simple as running:
```
pip install sabyenc3 --upgrade
```
When you want to compile from sources, you can run in the `sabyenc` directory:
```
python setup.py install
```

Testing
===============================
For testing we use `pytest` (install via `pip install -r tests/requirements.txt`) and test can simply be executed by browsing to the `sabyenc` directory and running:
```
pytest
```
Note that the `sabyenc3.encode` function is currently not covered by unit tests.