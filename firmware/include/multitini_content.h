/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CONTENT_H
#define MULTITINI_CONTENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MT_CONTENT_UNKNOWN = 0,
    MT_CONTENT_SYSTEM_ROM,
    MT_CONTENT_PRG,
    MT_CONTENT_D64,
    MT_CONTENT_G64,
    MT_CONTENT_CRT,
    MT_CONTENT_TAP_FUTURE,
    MT_CONTENT_D81_FUTURE,
    MT_CONTENT_GB_CART,
    MT_CONTENT_GBC_CART,
    MT_CONTENT_VECTREX_CART,
    MT_CONTENT_VECTREX_BASIC,
    MT_CONTENT_VECTREX_OVERLAY,
    MT_CONTENT_GENESIS_CART,
    MT_CONTENT_GENESIS_SMD,
    MT_CONTENT_IIGS_HDD,
    MT_CONTENT_IIGS_WOZ,
    MT_CONTENT_NES_CART
} mt_content_type_t;

typedef enum {
    MT_CONTENT_VALID = 0,
    MT_CONTENT_INVALID_ARGUMENT,
    MT_CONTENT_TOO_SMALL,
    MT_CONTENT_WRONG_SIZE,
    MT_CONTENT_BAD_MAGIC,
    MT_CONTENT_UNSUPPORTED_VERSION,
    MT_CONTENT_BAD_STRUCTURE
} mt_content_validation_t;

typedef enum {
    MT_C64_ROM_BASIC = 0,
    MT_C64_ROM_KERNAL,
    MT_C64_ROM_CHARACTER,
    MT_C64_ROM_DRIVE_1541
} mt_c64_rom_kind_t;

typedef struct {
    size_t payload_offset;
    size_t payload_size;
    uint32_t load_address;
    uint16_t hardware_type;
    bool writable;
    uint8_t mapper_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    bool color_capable;
    bool color_required;
} mt_content_info_t;

mt_content_type_t mt_content_classify(const char *path);
/* .bin remains ambiguous globally; machine browsers resolve its meaning. */
mt_content_type_t mt_content_classify_for_core(uint32_t core_id, const char *path);
bool mt_content_supported_first_release(mt_content_type_t type);
bool mt_content_may_write_first_release(mt_content_type_t type);
const char *mt_content_type_name(mt_content_type_t type);
mt_content_validation_t mt_content_inspect(mt_content_type_t type,
                                           const uint8_t *data, size_t size,
                                           mt_content_info_t *info);
mt_content_validation_t mt_c64_rom_validate(mt_c64_rom_kind_t kind,
                                            const uint8_t *data, size_t size);
mt_content_validation_t mt_gbc_boot_rom_validate(const uint8_t *data,
                                                  size_t size);
const char *mt_content_validation_name(mt_content_validation_t result);

#endif
