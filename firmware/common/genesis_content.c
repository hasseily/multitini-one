/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_genesis_content.h"
#include <string.h>

/* A virtual view makes validation identical for raw, word-swapped and SMD
 * inputs, and lets us reject damaged files before modifying the buffer. */
static uint8_t rom_byte(const uint8_t *data, size_t offset,
                        mt_genesis_format_t format, bool swapped)
{
    if (format == MT_GENESIS_FORMAT_SMD) {
        const size_t within = offset & 16383U;
        offset = 512U + (offset & ~(size_t)16383U) + within / 2U +
            ((within & 1U) == 0U ? 8192U : 0U);
    } else if (swapped) {
        offset ^= 1U;
    }
    return data[offset];
}

static uint8_t country_bit(uint8_t value)
{
    if (value == 'J' || value == 'j') return 1U;
    if (value == 'U' || value == 'u') return 2U;
    if (value == 'E' || value == 'e') return 4U;
    return 0U;
}

mt_content_validation_t mt_genesis_content_inspect(
    const uint8_t *data, size_t size, mt_genesis_format_t format,
    mt_genesis_content_info_t *info)
{
    mt_genesis_content_info_t result = {0};
    uint8_t header[512];
    size_t index;
    size_t title_length;
    if (data == NULL || info == NULL ||
        (format != MT_GENESIS_FORMAT_RAW && format != MT_GENESIS_FORMAT_SMD))
        return MT_CONTENT_INVALID_ARGUMENT;
    if (format == MT_GENESIS_FORMAT_SMD) {
        if (size < 512U + 16384U) return MT_CONTENT_TOO_SMALL;
        if ((size - 512U) % 16384U != 0U) return MT_CONTENT_WRONG_SIZE;
        result.rom_size = size - 512U;
    } else {
        result.rom_size = size;
    }
    if (result.rom_size < MT_SIZE_GENESIS_CART_MIN) return MT_CONTENT_TOO_SMALL;
    if (result.rom_size > MT_SIZE_GENESIS_CART_MAX || (result.rom_size & 1U) != 0U)
        return MT_CONTENT_WRONG_SIZE;
    result.byte_swapped = format == MT_GENESIS_FORMAT_RAW &&
        memcmp(data + 0x100U, "ESAG", 4U) == 0;
    for (index = 0U; index < sizeof(header); ++index)
        header[index] = rom_byte(data, index, format, result.byte_swapped);
    if (memcmp(header + 0x100U, "SEGA", 4U) != 0) return MT_CONTENT_BAD_MAGIC;
    /* These identify different machines, despite sharing a cartridge header. */
    if (memcmp(header + 0x105U, "32X", 3U) == 0 ||
        memcmp(header + 0x105U, "PICO", 4U) == 0)
        return MT_CONTENT_UNSUPPORTED_VERSION;
    /* These boards require cartridge coprocessor/RAM paths beyond the base
     * adapter. Match the pinned upstream cartridge decoder's serial field. */
    if (memcmp(header + 0x183U, "MK-1229 ", 8U) == 0 ||
        memcmp(header + 0x183U, "G-7001  ", 8U) == 0 ||
        memcmp(header + 0x183U, "T-68??" "?-", 8U) == 0)
        return MT_CONTENT_UNSUPPORTED_VERSION;
    /* The pinned mapper initializes Sonic 1 Remastered's SRAM to zero.
     * Match its exact serial, including the leading space. */
    result.save_initial_value = memcmp(header + 0x183U, " GM 0000", 8U) == 0 ?
        0U : 0xffU;
    for (index = 0x1f0U; index < 0x200U; ++index)
        if (header[index] >= 'a' && header[index] <= 'z')
            header[index] = (uint8_t)(header[index] - 'a' + 'A');
    for (index = 0x1f0U; index < 0x200U; ++index) {
        /* Word-form headers must not treat the U in EUROPE as USA. */
        if (index + 6U <= 0x200U && memcmp(header + index, "EUROPE", 6U) == 0) {
            result.region_mask |= 4U;
            index += 5U;
        } else if (index + 5U <= 0x200U && memcmp(header + index, "JAPAN", 5U) == 0) {
            result.region_mask |= 1U;
            index += 4U;
        } else if (index + 3U <= 0x200U && memcmp(header + index, "USA", 3U) == 0) {
            result.region_mask |= 2U;
            index += 2U;
        } else {
            result.region_mask |= country_bit(header[index]);
        }
    }
    /* Numeric region masks are common. Prefer J/U/E strings if present,
     * otherwise decode the first hexadecimal digit, including A..F. */
    if (result.region_mask == 0U) {
        unsigned mask = 0U;
        const uint8_t ch = header[0x1f0U];
        if (ch >= '0' && ch <= '9') mask = (unsigned)(ch - '0');
        else if (ch >= 'A' && ch <= 'F') mask = (unsigned)(ch - 'A') + 10U;
        else if (ch >= 'a' && ch <= 'f') mask = (unsigned)(ch - 'a') + 10U;
        result.region_mask = (uint8_t)((mask & 1U) | ((mask & 4U) >> 1U) |
                                       ((mask & 8U) >> 1U));
    }
    result.region = (result.region_mask & 2U) != 0U ? MT_GENESIS_REGION_US :
        ((result.region_mask & 4U) != 0U ? MT_GENESIS_REGION_EU :
        ((result.region_mask & 1U) != 0U ? MT_GENESIS_REGION_JP : MT_GENESIS_REGION_US));
    for (index = 0x190U; index < 0x1a0U; ++index)
        if (header[index] == '6') result.six_button = true;
    result.save_declared = header[0x1b0U] == 'R' && header[0x1b1U] == 'A';
    result.checksum_header = (uint16_t)(((uint16_t)header[0x18eU] << 8U) |
                                       header[0x18fU]);
    for (index = 512U; index < result.rom_size; index += 2U) {
        const uint16_t word = (uint16_t)(((unsigned)rom_byte(
            data, index, format, result.byte_swapped) << 8U) |
            rom_byte(data, index + 1U, format, result.byte_swapped));
        result.checksum_computed = (uint16_t)(result.checksum_computed + word);
    }
    result.checksum_matches = result.checksum_header == result.checksum_computed;
    for (index = 0U; index < 48U; ++index) {
        const uint8_t ch = header[0x150U + index];
        result.title[index] = ch >= 32U && ch < 127U ? (char)ch : ' ';
    }
    title_length = 48U;
    while (title_length != 0U && result.title[title_length - 1U] == ' ')
        --title_length;
    result.title[title_length] = '\0';
    *info = result;
    return MT_CONTENT_VALID;
}

mt_content_validation_t mt_genesis_content_decode(
    uint8_t *data, size_t *size, mt_genesis_format_t format,
    mt_genesis_content_info_t *info)
{
    mt_content_validation_t status;
    size_t offset;
    if (size == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    status = mt_genesis_content_inspect(data, *size, format, info);
    if (status != MT_CONTENT_VALID) return status;
    if (format == MT_GENESIS_FORMAT_SMD) {
        /* CPU0 serializes cartridge loads. Keep scratch in DDR .bss rather
         * than consuming the standalone firmware's small stack. */
        static uint8_t block[16384];
        for (offset = 0U; offset < info->rom_size; offset += sizeof(block)) {
            size_t byte;
            memcpy(block, data + 512U + offset, sizeof(block));
            for (byte = 0U; byte < sizeof(block); ++byte) {
                const size_t source = byte / 2U + ((byte & 1U) == 0U ? 8192U : 0U);
                data[offset + byte] = block[source];
            }
        }
    } else if (info->byte_swapped) {
        for (offset = 0U; offset < info->rom_size; offset += 2U) {
            const uint8_t first = data[offset];
            data[offset] = data[offset + 1U];
            data[offset + 1U] = first;
        }
    }
    *size = info->rom_size;
    return MT_CONTENT_VALID;
}
