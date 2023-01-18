import pytest
import sabyenc3


@pytest.mark.parametrize(
    "crc1,crc2,len2,expected",
    [
        (0, 0, 0, 0),
        (4294967295, 0, 0, 4294967295),
        (0, 4294967295, 0, 4294967295),
        (4294967295, 4294967295, 0, 0),
        (4, 16, 256, 2385497022),
        (18446744073709551615, 0, 0, 4294967295),
        (18446744073709551615, 18446744073709551615, 0, 0),
        (100, 200, 300, 1009376567)
    ],
)
def test_crc32_combine_expected(crc1, crc2, len2, expected):
    assert sabyenc3.crc32_combine(crc1, crc2, len2) == expected


@pytest.mark.parametrize(
    "crc1,crc2,expected",
    [
        (0, 0, 0),
        (4294967295, 0, 0),
        (0, 4294967295, 0),
        (4294967295, 4294967295, 1048090088),
        (18446744073709551615, 18446744073709551615, 2496806722),
        (100, 200, 4155012749),
    ],
)
def test_crc32_multiply_expected(crc1, crc2, expected):
    assert sabyenc3.crc32_multiply(crc1, crc2) == expected


@pytest.mark.parametrize(
    "crc1,zeroes,expected",
    [
        (0, 0, 0),
        (4294967295, 0, 4294967295),
        (4294967295, 4294967295, 4294967295),
        (100, 200, 1523530880),
    ],
)
def test_crc32_zero_unpad_expected(crc1, zeroes, expected):
    assert sabyenc3.crc32_zero_unpad(crc1, zeroes) == expected


@pytest.mark.parametrize(
    "n,expected",
    [
        (0, 2147483648),
        (1, 1073741824),
        (8, 8388608),
        (30, 2),
        (31, 1),
        (4294967295, 2147483648),
        (18446744073709551615, 2147483648),
    ],
)
def test_crc32_xpown_expected(n, expected):
    assert sabyenc3.crc32_xpown(n) == expected


@pytest.mark.parametrize(
    "n,expected",
    [
        (0, 2147483648),
        (1, 8388608),
        (4294967295, 2147483648),
        (18446744073709551615, 3742066410),
        (112233445566, 1480064961),
    ],
)
def test_crc32_xpow8n_expected(n, expected):
    assert sabyenc3.crc32_xpow8n(n) == expected
