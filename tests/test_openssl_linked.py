from tests.testsupport import *


def test_openssl_linked():
    assert sabyenc3.openssl_linked == True
