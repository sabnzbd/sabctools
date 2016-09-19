import os 
os.system('python setup.py build_ext --inplace')

import sabnzbdyenc

print sabnzbdyenc.primes(10)