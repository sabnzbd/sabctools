#!/bin/bash
set -e -x

# Install a system package required by our libraries
yum install -y python-devel

# Remove the build made by the tests
rm -Rf /io/dist
rm -Rf /io/build

# We only care about Python 3.5+
for PYBIN in /opt/python/cp3*/bin; do
    "${PYBIN}/pip" wheel /io/ -w wheelhouse/
done

# Bundle external shared libraries into the wheels
for whl in wheelhouse/*.whl; do
    auditwheel repair "$whl" -w /io/wheelhouse/
done
