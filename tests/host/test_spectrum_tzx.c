/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_spectrum_tzx.h"

#include <assert.h>
#include <string.h>

#define TZX_HEADER 'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1aU, 1U, 20U
#define LE16(value) (uint8_t)(value), (uint8_t)((value) >> 8U)
#define LE24(value) LE16(value), (uint8_t)((value) >> 16U)

static void expect_compiles(const uint8_t *image, size_t image_size,
                             const uint8_t *expected, size_t expected_size)
{
    uint8_t compiled[128U];
    assert(expected_size < sizeof(compiled));
    memset(compiled, 0xcd, sizeof(compiled));
    size_t size = 0U;
    assert(mt_spectrum_tzx_compile(image, image_size, compiled, sizeof(compiled), &size) ==
           MT_SPECTRUM_TZX_OK);
    assert(size == expected_size && memcmp(compiled, expected, size) == 0);
    assert(compiled[size] == 0xcdU);
}

static void expect_failure(const uint8_t *image, size_t image_size,
                            mt_spectrum_tzx_status_t expected)
{
    uint8_t compiled[128U];
    size_t size = 99U;
    assert(mt_spectrum_tzx_compile(image, image_size, compiled, sizeof(compiled), &size) == expected);
    assert(size == 0U);
    assert(mt_spectrum_tzx_status_name(expected)[0] != '\0');
}

static void test_signal_blocks(void)
{
    static const uint8_t image[] = {
        TZX_HEADER,
        0x10U, LE16(1000U), LE16(2U), 0x01U, 0x11U,
        0x10U, LE16(0U), LE16(2U), 0xffU, 0xffU,
        0x11U, LE16(2300U), LE16(650U), LE16(720U), LE16(810U), LE16(1600U),
            LE16(42U), 3U, LE16(10U), LE24(2U), 0xabU, 0xa0U,
        0x12U, LE16(65535U), LE16(1U),
        0x13U, 2U, LE16(1U), LE16(54321U),
        0x14U, LE16(11U), LE16(22U), 1U, LE16(0U), LE24(1U), 0x80U,
        0x15U, LE16(79U), LE16(65535U), 7U, LE24(2U), 0x5aU, 0xfeU,
        0x20U, LE16(0U),
        0x2aU, 0U, 0U, 0U, 0U,
        0x2bU, 1U, 0U, 0U, 0U, 1U,
    };
    static const uint8_t expected[] = {
        0x01U, LE16(2168U), LE16(8063U),
        0x02U, 2U, LE16(667U), LE16(735U),
        0x03U, LE16(855U), LE16(1710U), 8U, LE24(2U), 0x01U, 0x11U,
        0x04U, LE16(1000U),
        0x01U, LE16(2168U), LE16(3223U),
        0x02U, 2U, LE16(667U), LE16(735U),
        0x03U, LE16(855U), LE16(1710U), 8U, LE24(2U), 0xffU, 0xffU,
        0x01U, LE16(2300U), LE16(42U),
        0x02U, 2U, LE16(650U), LE16(720U),
        0x03U, LE16(810U), LE16(1600U), 3U, LE24(2U), 0xabU, 0xa0U,
        0x04U, LE16(10U),
        0x01U, LE16(65535U), LE16(1U),
        0x02U, 2U, LE16(1U), LE16(54321U),
        0x03U, LE16(11U), LE16(22U), 1U, LE24(1U), 0x80U,
        0x05U, LE16(79U), 7U, LE24(2U), 0x5aU, 0xfeU,
        0x04U, LE16(65535U),
        0x04U, LE16(0U),
        0x07U,
        0x06U, 1U,
    };
    expect_compiles(image, sizeof(image), expected, sizeof(expected));

    static const uint8_t no_pilot[] = {
        TZX_HEADER,
        0x11U, LE16(0U), LE16(650U), LE16(0U), LE16(810U), LE16(1600U),
            LE16(0U), 2U, LE16(0U), LE24(1U), 0xc0U,
    };
    static const uint8_t no_pilot_expected[] = {
        0x02U, 1U, LE16(650U),
        0x03U, LE16(810U), LE16(1600U), 2U, LE24(1U), 0xc0U,
    };
    expect_compiles(no_pilot, sizeof(no_pilot), no_pilot_expected, sizeof(no_pilot_expected));
}

static void test_loops_and_metadata(void)
{
    static const uint8_t image[] = {
        TZX_HEADER,
        0x21U, 3U, 'L', 'O', 'P',
        0x30U, 1U, 'T',
        0x31U, 10U, 2U, 'H', 'I',
        0x32U, LE16(4U), 1U, 0U, 1U, 'A',
        0x33U, 1U, 0U, 1U, 0U,
        0x35U, 'C', 'U', 'S', 'T', 'O', 'M', ' ', ' ',
            ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 2U, 0U, 0U, 0U, 0xabU, 0xcdU,
        0x24U, LE16(3U),
        0x12U, LE16(1234U), LE16(5U),
        0x20U, LE16(0U),
        0x25U,
        0x22U,
        TZX_HEADER, /* Embedded file header is a glue block. */
        0x2bU, 1U, 0U, 0U, 0U, 0U,
    };
    static const uint8_t expected[] = {
        0x01U, LE16(1234U), LE16(5U), 0x04U, LE16(0U),
        0x01U, LE16(1234U), LE16(5U), 0x04U, LE16(0U),
        0x01U, LE16(1234U), LE16(5U), 0x04U, LE16(0U),
        0x06U, 0U,
    };
    expect_compiles(image, sizeof(image), expected, sizeof(expected));
}

static void test_capacity(void)
{
    static const uint8_t image[] = {
        TZX_HEADER,
        0x24U, LE16(65535U),
        0x2bU, 1U, 0U, 0U, 0U, 0U,
        0x25U,
        0x2bU, 1U, 0U, 0U, 0U, 1U,
        0x20U, LE16(0U),
    };
    static uint8_t compiled[MT_SPECTRUM_TZX_MAX_BYTES + 4U];
    memset(compiled, 0xcd, sizeof(compiled));
    size_t size = 0U;
    assert(mt_spectrum_tzx_compile(image, sizeof(image) - 3U, compiled, sizeof(compiled), &size) ==
           MT_SPECTRUM_TZX_OK);
    assert(size == MT_SPECTRUM_TZX_MAX_BYTES);
    assert(compiled[0] == 0x06U && compiled[1] == 0U && compiled[size - 3U] == 0U);
    assert(compiled[size - 2U] == 0x06U && compiled[size - 1U] == 1U && compiled[size] == 0xcdU);
    assert(mt_spectrum_tzx_compile(image, sizeof(image), compiled, sizeof(compiled), &size) ==
           MT_SPECTRUM_TZX_TOO_LARGE && size == 0U);
    assert(mt_spectrum_tzx_compile(image, sizeof(image) - 3U, compiled, 2U, &size) ==
           MT_SPECTRUM_TZX_TOO_LARGE && size == 0U);
    assert(compiled[MT_SPECTRUM_TZX_MAX_BYTES] == 0xcdU);
}

static void test_rejection(void)
{
    static const uint8_t data[] = {
        TZX_HEADER, 0x15U, LE16(158U), LE16(0U), 8U, LE24(2U), 0xaaU, 0x55U,
    };
    for (size_t i = 0U; i < 10U; ++i)
        expect_failure(data, i, MT_SPECTRUM_TZX_BAD_HEADER);
    expect_failure(data, 10U, MT_SPECTRUM_TZX_EMPTY);
    for (size_t i = 11U; i < sizeof(data); ++i)
        expect_failure(data, i, MT_SPECTRUM_TZX_TRUNCATED);

    uint8_t bad[sizeof(data)];
    memcpy(bad, data, sizeof(bad));
    bad[8] = 2U;
    expect_failure(bad, sizeof(bad), MT_SPECTRUM_TZX_UNSUPPORTED_VERSION);
    bad[8] = 1U;
    bad[15] = 9U;
    expect_failure(bad, sizeof(bad), MT_SPECTRUM_TZX_INVALID_BLOCK);
    bad[15] = 8U;
    bad[11] = bad[12] = 0U;
    expect_failure(bad, sizeof(bad), MT_SPECTRUM_TZX_INVALID_BLOCK);
    bad[0] = 0U;
    expect_failure(bad, sizeof(bad), MT_SPECTRUM_TZX_BAD_HEADER);

    static const uint8_t csw[] = {TZX_HEADER, 0x18U};
    static const uint8_t generalized[] = {TZX_HEADER, 0x19U};
    static const uint8_t jump[] = {TZX_HEADER, 0x23U, LE16(2U)};
    static const uint8_t unknown[] = {TZX_HEADER, 0x7fU};
    static const uint8_t open_loop[] = {TZX_HEADER, 0x24U, LE16(2U), 0x20U, LE16(1U)};
    static const uint8_t no_loop[] = {TZX_HEADER, 0x25U};
    static const uint8_t nested_loop[] = {TZX_HEADER, 0x24U, LE16(2U), 0x24U, LE16(2U)};
    static const uint8_t bad_custom[] = {
        TZX_HEADER, 0x35U, 'C', 'U', 'S', 'T', 'O', 'M', ' ', ' ',
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0xffU, 0xffU, 0xffU, 0xffU,
    };
    static const uint8_t bad_level[] = {TZX_HEADER, 0x2bU, 1U, 0U, 0U, 0U, 2U};
    static const uint8_t bad_pulse[] = {TZX_HEADER, 0x13U, 1U, LE16(0U)};
    static const uint8_t bad_group[] = {TZX_HEADER, 0x22U};
    expect_failure(csw, sizeof(csw), MT_SPECTRUM_TZX_UNSUPPORTED_CSW);
    expect_failure(generalized, sizeof(generalized), MT_SPECTRUM_TZX_UNSUPPORTED_GENERALIZED);
    expect_failure(jump, sizeof(jump), MT_SPECTRUM_TZX_UNSUPPORTED_NAVIGATION);
    expect_failure(unknown, sizeof(unknown), MT_SPECTRUM_TZX_UNSUPPORTED_BLOCK);
    expect_failure(open_loop, sizeof(open_loop), MT_SPECTRUM_TZX_INVALID_BLOCK);
    expect_failure(no_loop, sizeof(no_loop), MT_SPECTRUM_TZX_INVALID_BLOCK);
    expect_failure(nested_loop, sizeof(nested_loop), MT_SPECTRUM_TZX_INVALID_BLOCK);
    expect_failure(bad_custom, sizeof(bad_custom), MT_SPECTRUM_TZX_TRUNCATED);
    expect_failure(bad_level, sizeof(bad_level), MT_SPECTRUM_TZX_INVALID_BLOCK);
    expect_failure(bad_pulse, sizeof(bad_pulse), MT_SPECTRUM_TZX_INVALID_BLOCK);
    expect_failure(bad_group, sizeof(bad_group), MT_SPECTRUM_TZX_INVALID_BLOCK);

    size_t size = 10U;
    assert(mt_spectrum_tzx_compile(data, sizeof(data), NULL, 0U, &size) ==
           MT_SPECTRUM_TZX_BAD_ARGUMENT && size == 0U);
    assert(mt_spectrum_tzx_compile(bad, sizeof(bad), bad, sizeof(bad), &size) ==
           MT_SPECTRUM_TZX_BAD_ARGUMENT && size == 0U);
}

int main(void)
{
    test_signal_blocks();
    test_loops_and_metadata();
    test_capacity();
    test_rejection();
    return 0;
}
