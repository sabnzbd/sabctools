import pytest
import sabctools


@pytest.mark.parametrize(
    "crc1,crc2,len2,expected",
    [
        (0, 0, 0, 0),
        (4294967295, 0, 0, 4294967295),
        (0, 4294967295, 0, 4294967295),
        (4294967295, 4294967295, 0, 0),
        (4, 16, 256, 2385497022),
        (100, 200, 300, 1009376567),
        (0, 0, 18446744073709551615, 0),
        (4294967295, 4294967295, 18446744073709551615, 0),
        (0, 100, 1234567890123, 100),
        (100, 0, 1234567890123, 1829446317),
    ],
)
def test_crc32_combine_expected(crc1, crc2, len2, expected):
    assert sabctools.crc32_combine(crc1, crc2, len2) == expected


@pytest.mark.parametrize(
    "crc1,crc2,expected",
    [
        (0, 0, 0),
        (4294967295, 0, 0),
        (0, 4294967295, 0),
        (4294967295, 4294967295, 1048090088),
        (100, 200, 4155012749),
    ],
)
def test_crc32_multiply_expected(crc1, crc2, expected):
    assert sabctools.crc32_multiply(crc1, crc2) == expected


@pytest.mark.parametrize(
    "crc1,zeroes,expected",
    [
        (0, 0, 0),
        (4294967295, 0, 4294967295),
        (4294967295, 4294967295, 4294967295),
        (100, 200, 1523530880),
        (0, 18446744073709551615, 0),
        (4294967295, 18446744073709551615, 4294967295),
        (100, 1234567890123, 980217485),
    ],
)
def test_crc32_zero_unpad_expected(crc1, zeroes, expected):
    assert sabctools.crc32_zero_unpad(crc1, zeroes) == expected


@pytest.mark.parametrize(
    "n,expected",
    [
        (0, 2147483648),
        (1, 1073741824),
        (8, 8388608),
        (30, 2),
        (31, 1),
        (4294967295, 2147483648),
        (4294967296, 1073741824),  # 1
    ],
)
def test_crc32_xpown_expected(n, expected):
    assert sabctools.crc32_xpown(n) == expected


@pytest.mark.parametrize(
    "n,expected",
    [
        (0, 2147483648),
        (1, 8388608),
        (4294967295, 2147483648),
        (4294967296, 8388608),  # 1
    ],
)
def test_crc32_xpow8n_expected(n, expected):
    assert sabctools.crc32_xpow8n(n) == expected
