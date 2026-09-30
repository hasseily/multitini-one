/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_genesis_content.h"
#include <assert.h>
#include <string.h>

static void header(uint8_t *rom, size_t size)
{
    size_t i;
    uint16_t checksum = 0U;
    for (i = 0U; i < size; ++i) rom[i] = (uint8_t)((i * 37U) ^ (i >> 8U));
    memset(rom + 0x100U, ' ', 256U);
    memcpy(rom + 0x100U, "SEGA MEGA DRIVE", 15U);
    memcpy(rom + 0x150U, "ORIGINAL FORMAT TEST", 20U);
    rom[0x190U] = '6';
    rom[0x1b0U] = 'R'; rom[0x1b1U] = 'A';
    memcpy(rom + 0x1f0U, "JUE", 3U);
    for (i = 512U; i < size; i += 2U)
        checksum = (uint16_t)(checksum + ((uint16_t)rom[i] << 8U) + rom[i + 1U]);
    rom[0x18eU] = (uint8_t)(checksum >> 8U);
    rom[0x18fU] = (uint8_t)checksum;
}

int main(void)
{
    static uint8_t original[32768];
    static uint8_t copy[sizeof(original) + 512U];
    mt_genesis_content_info_t info;
    mt_content_info_t common;
    size_t size;
    size_t i;
    header(original, sizeof(original));
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID);
    assert(info.rom_size == sizeof(original) && info.region_mask == 7U);
    assert(info.region == MT_GENESIS_REGION_US && info.six_button && info.save_declared);
    assert(info.checksum_matches && !info.byte_swapped);
    assert(info.save_initial_value == 0xffU);
    assert(strcmp(info.title, "ORIGINAL FORMAT TEST") == 0);
    assert(mt_content_classify("HOME.GEN") == MT_CONTENT_GENESIS_CART);
    assert(mt_content_classify("home.md") == MT_CONTENT_GENESIS_CART);
    assert(mt_content_classify("home.smd") == MT_CONTENT_GENESIS_SMD);
    assert(mt_content_classify("home.bin") == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify_for_core(MT_GENESIS_CORE_ID, "home.BIN") == MT_CONTENT_GENESIS_CART);
    assert(mt_content_inspect(MT_CONTENT_GENESIS_CART, original, sizeof(original),
        &common) == MT_CONTENT_VALID && common.payload_size == sizeof(original));

    for (i = 0U; i < sizeof(original); ++i) copy[i] = original[i ^ 1U];
    size = sizeof(original);
    assert(mt_genesis_content_decode(copy, &size, MT_GENESIS_FORMAT_RAW, &info) == MT_CONTENT_VALID);
    assert(info.byte_swapped && memcmp(copy, original, sizeof(original)) == 0);

    memset(copy, 0xa5, 512U);
    for (i = 0U; i < sizeof(original); ++i) {
        const size_t offset = (i & ~(size_t)16383U) + ((i & 16383U) / 2U) +
            ((i & 1U) == 0U ? 8192U : 0U);
        copy[512U + offset] = original[i];
    }
    size = sizeof(copy);
    assert(mt_content_inspect(MT_CONTENT_GENESIS_SMD, copy, size, &common) == MT_CONTENT_VALID);
    assert(common.payload_offset == 512U && common.payload_size == sizeof(original));
    assert(mt_genesis_content_decode(copy, &size, MT_GENESIS_FORMAT_SMD, &info) == MT_CONTENT_VALID);
    assert(size == sizeof(original) && memcmp(copy, original, size) == 0);

    original[0x200U] ^= 1U;
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && !info.checksum_matches);
    memset(original + 0x1f0U, ' ', 16U);
    original[0x1f0U] = 'J';
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.region == MT_GENESIS_REGION_JP);
    original[0x1f0U] = 'E';
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.region == MT_GENESIS_REGION_EU);
    memcpy(original + 0x1f0U, "Europe", 6U);
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.region_mask == 4U);
    memset(original + 0x1f0U, ' ', 16U);
    original[0x1f0U] = 'F';
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.region_mask == 7U);
    original[0x1f0U] = '8';
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.region == MT_GENESIS_REGION_EU);
    assert(mt_genesis_content_inspect(NULL, 0U, MT_GENESIS_FORMAT_RAW, &info) == MT_CONTENT_INVALID_ARGUMENT);
    assert(mt_genesis_content_inspect(original, 511U, MT_GENESIS_FORMAT_RAW, &info) == MT_CONTENT_TOO_SMALL);
    assert(mt_genesis_content_inspect(original, 513U, MT_GENESIS_FORMAT_RAW, &info) == MT_CONTENT_WRONG_SIZE);
    assert(mt_genesis_content_inspect(original, MT_SIZE_GENESIS_CART_MAX + 2U,
        MT_GENESIS_FORMAT_RAW, &info) == MT_CONTENT_WRONG_SIZE);
    assert(mt_genesis_content_inspect(original, 16384U + 513U, MT_GENESIS_FORMAT_SMD,
        &info) == MT_CONTENT_WRONG_SIZE);
    memcpy(original + 0x183U, " GM 0000", 8U);
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.save_initial_value == 0U);
    original[0x18aU] = '1';
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_VALID && info.save_initial_value == 0xffU);
    memcpy(original + 0x183U, "MK-1229 ", 8U);
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_UNSUPPORTED_VERSION);
    memcpy(original + 0x183U, "G-7001  ", 8U);
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_UNSUPPORTED_VERSION);
    memcpy(original + 0x183U, "T-68??" "?-", 8U);
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_UNSUPPORTED_VERSION);
    memset(original + 0x183U, ' ', 8U);
    memcpy(original + 0x100U, "SEGA 32X", 8U);
    assert(mt_genesis_content_inspect(original, sizeof(original), MT_GENESIS_FORMAT_RAW,
        &info) == MT_CONTENT_UNSUPPORTED_VERSION);
    memset(original + 0x100U, 0, 4U);
    size = sizeof(original);
    memcpy(copy, original, size);
    assert(mt_genesis_content_decode(copy, &size, MT_GENESIS_FORMAT_RAW, &info) == MT_CONTENT_BAD_MAGIC);
    assert(size == sizeof(original) && memcmp(copy, original, size) == 0);
    return 0;
}
