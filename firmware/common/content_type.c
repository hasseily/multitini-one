/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_content.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_vectrex_overlay.h"
#include "multitini_genesis_content.h"
#include "multitini_nes_content.h"
#include "multitini_iigs_interface.h"

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
    if (extension_is(extension, "gb")) return MT_CONTENT_GB_CART;
    if (extension_is(extension, "nes")) return MT_CONTENT_NES_CART;
    if (extension_is(extension, "gbc")) return MT_CONTENT_GBC_CART;
    if (extension_is(extension, "vec")) return MT_CONTENT_VECTREX_CART;
    if (extension_is(extension, "bas")) return MT_CONTENT_VECTREX_BASIC;
    if (extension_is(extension, "ovr")) return MT_CONTENT_VECTREX_OVERLAY;
    if (extension_is(extension, "md") || extension_is(extension, "gen"))
        return MT_CONTENT_GENESIS_CART;
    if (extension_is(extension, "smd")) return MT_CONTENT_GENESIS_SMD;
    if (extension_is(extension, "hdv") || extension_is(extension, "po") ||
        extension_is(extension, "2mg")) return MT_CONTENT_IIGS_HDD;
    if (extension_is(extension, "woz")) return MT_CONTENT_IIGS_WOZ;
    if (extension_is(extension, "dsk")) return MT_CONTENT_IIGS_FLOPPY;
    return MT_CONTENT_UNKNOWN;
}

mt_content_type_t mt_content_classify_for_core(uint32_t core_id, const char *path)
{
    const mt_content_type_t type = mt_content_classify(path);
    const char *extension;
    if (type != MT_CONTENT_UNKNOWN || path == NULL)
        return type;
    extension = strrchr(path, '.');
    if (extension == NULL || !extension_is(extension + 1, "bin"))
        return MT_CONTENT_UNKNOWN;
    if (core_id == MT_VECTREX_CORE_ID) return MT_CONTENT_VECTREX_CART;
    if (core_id == MT_GENESIS_CORE_ID) return MT_CONTENT_GENESIS_CART;
    return MT_CONTENT_UNKNOWN;
}

bool mt_content_supported_first_release(mt_content_type_t type)
{
    return type == MT_CONTENT_SYSTEM_ROM || type == MT_CONTENT_PRG ||
           type == MT_CONTENT_D64 || type == MT_CONTENT_G64 ||
           type == MT_CONTENT_CRT || type == MT_CONTENT_GB_CART ||
           type == MT_CONTENT_GBC_CART || type == MT_CONTENT_NES_CART || type == MT_CONTENT_VECTREX_CART ||
           type == MT_CONTENT_VECTREX_BASIC || type == MT_CONTENT_VECTREX_OVERLAY ||
           type == MT_CONTENT_GENESIS_CART || type == MT_CONTENT_GENESIS_SMD ||
           type == MT_CONTENT_IIGS_HDD || type == MT_CONTENT_IIGS_WOZ ||
           type == MT_CONTENT_IIGS_FLOPPY;
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
    case MT_CONTENT_GB_CART: return "Game Boy cartridge";
    case MT_CONTENT_NES_CART: return "Nintendo Entertainment System cartridge";
    case MT_CONTENT_GBC_CART: return "Game Boy Color cartridge";
    case MT_CONTENT_VECTREX_CART: return "Vectrex cartridge";
    case MT_CONTENT_VECTREX_BASIC: return "Vectrex32 BASIC";
    case MT_CONTENT_VECTREX_OVERLAY: return "Vectrex overlay";
    case MT_CONTENT_GENESIS_CART: return "Genesis / Mega Drive cartridge";
    case MT_CONTENT_GENESIS_SMD: return "Genesis / Mega Drive SMD cartridge";
    case MT_CONTENT_IIGS_HDD: return "Apple IIgs block disk";
    case MT_CONTENT_IIGS_WOZ: return "Apple IIgs WOZ floppy";
    case MT_CONTENT_IIGS_FLOPPY: return "Apple IIgs sector floppy";
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

static size_t gameboy_rom_size(uint8_t code)
{
    if (code <= 8U) {
        return (size_t)32768U << code;
    }
    switch (code) {
    case 0x52U: return 1179648U;
    case 0x53U: return 1310720U;
    case 0x54U: return 1572864U;
    default: return 0U;
    }
}

static mt_content_validation_t inspect_gameboy_cart(
    const uint8_t *data, size_t size, bool require_color,
    mt_content_info_t *info)
{
    uint8_t checksum = 0U;
    uint8_t color_flag;
    size_t index;
    size_t expected_size;

    if (size < 0x150U) return MT_CONTENT_TOO_SMALL;
    if (size > MT_SIZE_GB_CART_MAX) return MT_CONTENT_WRONG_SIZE;

    color_flag = data[0x143U];
    if (color_flag != 0x00U && color_flag != 0x80U && color_flag != 0xC0U) {
        return MT_CONTENT_BAD_STRUCTURE;
    }
    if (require_color && color_flag == 0x00U) {
        return MT_CONTENT_BAD_STRUCTURE;
    }

    expected_size = gameboy_rom_size(data[0x148U]);
    if (expected_size == 0U || size != expected_size) {
        return MT_CONTENT_WRONG_SIZE;
    }
    if (data[0x149U] > 5U) return MT_CONTENT_BAD_STRUCTURE;

    for (index = 0x134U; index <= 0x14CU; ++index) {
        checksum = (uint8_t)((unsigned)checksum - data[index] - 1U);
    }
    if (checksum != data[0x14DU]) return MT_CONTENT_BAD_STRUCTURE;

    info->payload_size = size;
    info->mapper_type = data[0x147U];
    info->rom_size_code = data[0x148U];
    info->ram_size_code = data[0x149U];
    info->color_capable = color_flag != 0x00U;
    info->color_required = color_flag == 0xC0U;
    return MT_CONTENT_VALID;
}

mt_content_validation_t mt_content_inspect(mt_content_type_t type,
                                           const uint8_t *data, size_t size,
                                           mt_content_info_t *info)
{
    mt_content_info_t result;
    mt_content_validation_t status;
    if (data == NULL || info == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    memset(&result, 0, sizeof(result));
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
    case MT_CONTENT_GB_CART:
        status = inspect_gameboy_cart(data, size, false, &result);
        break;
    case MT_CONTENT_GBC_CART:
        status = inspect_gameboy_cart(data, size, true, &result);
        break;
    case MT_CONTENT_VECTREX_CART:
        result.payload_size = size;
        status = size == 0U ? MT_CONTENT_TOO_SMALL :
            (size > MT_SIZE_VECTREX_CART_MAX ? MT_CONTENT_WRONG_SIZE : MT_CONTENT_VALID);
        break;
    case MT_CONTENT_VECTREX_BASIC:
        result.payload_size = size;
        status = size == 0U ? MT_CONTENT_TOO_SMALL :
            (size > MT_SIZE_VECTREX_BASIC_MAX ? MT_CONTENT_WRONG_SIZE :
             (memchr(data, 0, size) != NULL ? MT_CONTENT_BAD_STRUCTURE : MT_CONTENT_VALID));
        break;
    case MT_CONTENT_VECTREX_OVERLAY:
        result.payload_size = size;
        status = size == 0U ? MT_CONTENT_TOO_SMALL :
            (mt_vectrex_overlay_valid_size(size) ? MT_CONTENT_VALID : MT_CONTENT_WRONG_SIZE);
        break;
    case MT_CONTENT_NES_CART: {
        mt_nes_content_info_t nes;
        status = mt_nes_content_inspect(data, size, &nes);
        if (status == MT_CONTENT_VALID) {
            result.payload_offset = 16U; result.payload_size = size - 16U;
            result.hardware_type = nes.mapper; result.writable = nes.battery;
        }
        break;
    }
    case MT_CONTENT_GENESIS_CART:
    case MT_CONTENT_GENESIS_SMD: {
        mt_genesis_content_info_t genesis;
        status = mt_genesis_content_inspect(data, size,
            type == MT_CONTENT_GENESIS_SMD ? MT_GENESIS_FORMAT_SMD : MT_GENESIS_FORMAT_RAW,
            &genesis);
        if (status == MT_CONTENT_VALID) {
            result.payload_size = genesis.rom_size;
            result.payload_offset = type == MT_CONTENT_GENESIS_SMD ? 512U : 0U;
        }
        break;
    }
    case MT_CONTENT_TAP_FUTURE:
    case MT_CONTENT_IIGS_HDD:
    case MT_CONTENT_IIGS_WOZ:
    case MT_CONTENT_IIGS_FLOPPY:
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

mt_content_validation_t mt_gbc_boot_rom_validate(const uint8_t *data,
                                                  size_t size)
{
    size_t index;
    uint8_t difference = 0U;
    if (data == NULL) return MT_CONTENT_INVALID_ARGUMENT;
    if (size != MT_SIZE_GBC_BOOT) return MT_CONTENT_WRONG_SIZE;
    for (index = 1U; index < size; ++index) difference |= data[index] ^ data[0];
    return difference == 0U ? MT_CONTENT_BAD_STRUCTURE : MT_CONTENT_VALID;
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
        expected = MT_SIZE_C64_BASIC;
        break;
    case MT_C64_ROM_CHARACTER:
        expected = MT_SIZE_C64_CHARACTER;
        break;
    case MT_C64_ROM_DRIVE_1541:
        expected = MT_SIZE_C64_1541_ROM;
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
