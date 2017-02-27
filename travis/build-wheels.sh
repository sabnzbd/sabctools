#!/bin/bash
set -e -x

# Install a system package required by our libraries
yum install -y python-devel

# We only care about Python 2.7
for PYBIN in /opt/python/cp27*/bin; do
    "${PYBIN}/pip" wheel /io/ -w wheelhouse/
done


# Compile wheels for all platforms
#for PYBIN in /opt/python/*/bin; do
#    "${PYBIN}/pip" wheel /io/ -w wheelhouse/
#done

# Bundle external shared libraries into the wheels
for whl in wheelhouse/*.whl; do
    auditwheel repair "$whl" -w /io/wheelhouse/
done
