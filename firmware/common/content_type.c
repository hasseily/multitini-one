/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_content.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static char ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static bool extension_is(const char *extension, const char *expected)
{
    if (extension == NULL || expected == NULL) {
        return false;
    }
    while (*extension != '\0' && *expected != '\0') {
        if (ascii_lower(*extension) != *expected) {
            return false;
        }
        ++extension;
        ++expected;
    }
    return *extension == '\0' && *expected == '\0';
}

mt_content_type_t mt_content_classify(const char *path)
{
    const char *extension = NULL;
    const char *cursor;

    if (path == NULL || *path == '\0') {
        return MT_CONTENT_UNKNOWN;
    }
    for (cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            extension = NULL;
        } else if (*cursor == '.') {
            extension = cursor + 1;
        }
    }
    if (extension == NULL || *extension == '\0') {
        return MT_CONTENT_UNKNOWN;
    }
    if (extension_is(extension, "rom")) return MT_CONTENT_SYSTEM_ROM;
    if (extension_is(extension, "prg")) return MT_CONTENT_PRG;
    if (extension_is(extension, "d64")) return MT_CONTENT_D64;
    if (extension_is(extension, "g64")) return MT_CONTENT_G64;
    if (extension_is(extension, "crt")) return MT_CONTENT_CRT;
    if (extension_is(extension, "tap")) return MT_CONTENT_TAP_FUTURE;
    if (extension_is(extension, "d81")) return MT_CONTENT_D81_FUTURE;
    return MT_CONTENT_UNKNOWN;
}

bool mt_content_supported_first_release(mt_content_type_t type)
{
    return type == MT_CONTENT_SYSTEM_ROM || type == MT_CONTENT_PRG ||
           type == MT_CONTENT_D64 || type == MT_CONTENT_G64 ||
           type == MT_CONTENT_CRT;
}

bool mt_content_may_write_first_release(mt_content_type_t type)
{
    return type == MT_CONTENT_D64;
}

const char *mt_content_type_name(mt_content_type_t type)
{
    switch (type) {
    case MT_CONTENT_SYSTEM_ROM: return "system ROM";
    case MT_CONTENT_PRG: return "PRG";
    case MT_CONTENT_D64: return "D64";
    case MT_CONTENT_G64: return "G64";
    case MT_CONTENT_CRT: return "CRT";
    case MT_CONTENT_TAP_FUTURE: return "TAP (future)";
    case MT_CONTENT_D81_FUTURE: return "D81 (future)";
    case MT_CONTENT_UNKNOWN: return "unknown";
    }
    return "unknown";
}

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static mt_content_validation_t inspect_prg(const uint8_t *data, size_t size,
                                           mt_content_info_t *info)
{
    size_t payload_size;
    uint32_t load_address;
    if (size < 3U) return MT_CONTENT_TOO_SMALL;
    payload_size = size - 2U;
    load_address = (uint32_t)data[0] | ((uint32_t)data[1] << 8);
    if (load_address + payload_size > UINT32_C(0x10000)) {
        return MT_CONTENT_BAD_STRUCTURE;
    }
    info->payload_offset = 2U;
    info->payload_size = payload_size;
    info->load_address = load_address;
    return MT_CONTENT_VALID;
}

static mt_content_validation_t inspect_d64(size_t size,
                                           mt_content_info_t *info)
{
    static const size_t valid_sizes[] = {
        174848U, 175531U, 196608U, 197376U, 205312U, 206114U
    };
    size_t index;
    for (index = 0U; index < sizeof(valid_sizes) / sizeof(valid_sizes[0]);
         ++index) {
        if (size == valid_sizes[index]) {
            info->payload_size = size;
            info->writable = true;
            return MT_CONTENT_VALID;
        }
    }
    return MT_CONTENT_WRONG_SIZE;
}

static mt_content_validation_t inspect_g64(const uint8_t *data, size_t size,
                                           mt_content_info_t *info)
{
    size_t header_size;
    uint8_t tracks;
    if (size < 12U) return MT_CONTENT_TOO_SMALL;
    if (memcmp(data, "GCR-1541", 8U) != 0) return MT_CONTENT_BAD_MAGIC;
    if (data[8] != 0U) return MT_CONTENT_UNSUPPORTED_VERSION;
    tracks = data[9];
    if (tracks == 0U || tracks > 84U) return MT_CONTENT_BAD_STRUCTURE;
    header_size = 12U + (size_t)tracks * 8U;
    if (size < header_size || (data[10] == 0U && data[11] == 0U)) {
        return MT_CONTENT_BAD_STRUCTURE;
    }
    info->payload_size = size;
    return MT_CONTENT_VALID;
}

static mt_content_validation_t inspect_crt(const uint8_t *data, size_t size,
                                           mt_content_info_t *info)
{
    size_t offset;
    uint32_t header_size;
    uint16_t version;
    bool found_chip = false;
    if (size < 0x40U) return MT_CONTENT_TOO_SMALL;
    if (memcmp(data, "C64 CARTRIDGE   ", 16U) != 0) {
        return MT_CONTENT_BAD_MAGIC;
    }
    header_size = read_be32(data + 0x10U);
    version = read_be16(data + 0x14U);
    if (version != UINT16_C(0x0100) && version != UINT16_C(0x0101)) {
        return MT_CONTENT_UNSUPPORTED_VERSION;
    }
    if (header_size < UINT32_C(0x40) || header_size > size) {
        return MT_CONTENT_BAD_STRUCTURE;
    }
    info->hardware_type = read_be16(data + 0x16U);
    offset = header_size;
    while (offset < size) {
        uint32_t packet_size;
        uint16_t image_size;
        if (size - offset < 0x10U || memcmp(data + offset, "CHIP", 4U) != 0) {
            return MT_CONTENT_BAD_STRUCTURE;
        }
        packet_size = read_be32(data + offset + 4U);
        image_size = read_be16(data + offset + 0x0EU);
        if (packet_size < UINT32_C(0x10) ||
            packet_size != (uint32_t)image_size + UINT32_C(0x10) ||
            packet_size > size - offset) {
            return MT_CONTENT_BAD_STRUCTURE;
        }
        found_chip = true;
        offset += packet_size;
    }
    if (!found_chip) return MT_CONTENT_BAD_STRUCTURE;
    info->payload_offset = header_size;
    info->payload_size = size - header_size;
    return MT_CONTENT_VALID;
}

mt_content_validation_t mt_content_inspect(mt_content_type_t type,
                                           const uint8_t *data, size_t size,
                                           mt_content_info_t *info)
{
    mt_content_info_t result = {0U, 0U, 0U, 0U, false};
    mt_content_validation_t status;
    if (data == NULL || info == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    switch (type) {
    case MT_CONTENT_PRG:
        status = inspect_prg(data, size, &result);
        break;
    case MT_CONTENT_D64:
        status = inspect_d64(size, &result);
        break;
    case MT_CONTENT_G64:
        status = inspect_g64(data, size, &result);
        break;
    case MT_CONTENT_CRT:
        status = inspect_crt(data, size, &result);
        break;
    case MT_CONTENT_SYSTEM_ROM:
        result.payload_size = size;
        status = size == 0U ? MT_CONTENT_TOO_SMALL : MT_CONTENT_VALID;
        break;
    case MT_CONTENT_TAP_FUTURE:
    case MT_CONTENT_D81_FUTURE:
    case MT_CONTENT_UNKNOWN:
        status = MT_CONTENT_UNSUPPORTED_VERSION;
        break;
    default:
        status = MT_CONTENT_INVALID_ARGUMENT;
        break;
    }
    if (status == MT_CONTENT_VALID) *info = result;
    return status;
}

mt_content_validation_t mt_c64_rom_validate(mt_c64_rom_kind_t kind,
                                            const uint8_t *data, size_t size)
{
    size_t expected;
    size_t index;
    uint8_t difference = 0U;
    if (data == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    switch (kind) {
    case MT_C64_ROM_BASIC:
    case MT_C64_ROM_KERNAL:
        expected = 8192U;
        break;
    case MT_C64_ROM_CHARACTER:
        expected = 4096U;
        break;
    case MT_C64_ROM_DRIVE_1541:
        expected = 16384U;
        break;
    default:
        return MT_CONTENT_INVALID_ARGUMENT;
    }
    if (size != expected) return MT_CONTENT_WRONG_SIZE;
    for (index = 1U; index < size; ++index) difference |= data[index] ^ data[0];
    return difference == 0U ? MT_CONTENT_BAD_STRUCTURE : MT_CONTENT_VALID;
}

const char *mt_content_validation_name(mt_content_validation_t result)
{
    switch (result) {
    case MT_CONTENT_VALID: return "valid";
    case MT_CONTENT_INVALID_ARGUMENT: return "invalid argument";
    case MT_CONTENT_TOO_SMALL: return "too small";
    case MT_CONTENT_WRONG_SIZE: return "wrong size";
    case MT_CONTENT_BAD_MAGIC: return "bad magic";
    case MT_CONTENT_UNSUPPORTED_VERSION: return "unsupported version";
    case MT_CONTENT_BAD_STRUCTURE: return "bad structure";
    }
    return "unknown validation result";
}
