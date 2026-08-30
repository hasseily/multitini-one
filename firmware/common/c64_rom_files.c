/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_rom_files.h"

#include <string.h>

typedef struct {
    mt_c64_rom_kind_t kind;
    const char *path;
    size_t offset;
    size_t size;
} mt_c64_system_rom_file_t;

static const mt_c64_system_rom_file_t system_rom_files[] = {
    {MT_C64_ROM_BASIC, MT_C64_SD_BASIC_PATH, 0U, MT_SIZE_C64_BASIC},
    {MT_C64_ROM_KERNAL, MT_C64_SD_KERNAL_PATH, MT_SIZE_C64_BASIC,
     MT_SIZE_C64_KERNAL},
    {MT_C64_ROM_CHARACTER, MT_C64_SD_CHARACTER_PATH,
     MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL, MT_SIZE_C64_CHARACTER},
};

const char *mt_c64_sd_rom_path(mt_c64_rom_kind_t kind)
{
    switch (kind) {
    case MT_C64_ROM_BASIC: return MT_C64_SD_BASIC_PATH;
    case MT_C64_ROM_KERNAL: return MT_C64_SD_KERNAL_PATH;
    case MT_C64_ROM_CHARACTER: return MT_C64_SD_CHARACTER_PATH;
    case MT_C64_ROM_DRIVE_1541: return MT_C64_SD_1541_PATH;
    }
    return NULL;
}

mt_c64_rom_files_result_t mt_c64_rom_files_load_system(
    mt_c64_rom_file_read_fn read_file, void *read_context,
    uint8_t *bundle, size_t bundle_capacity, mt_c64_system_roms_t *roms)
{
    mt_c64_rom_files_result_t result = {
        MT_C64_ROM_FILES_INVALID_ARGUMENT,
        NULL,
        MT_C64_ROM_BASIC,
        {0, 0U, 0U},
        MT_CONTENT_INVALID_ARGUMENT,
    };
    size_t index;

    if (roms != NULL) memset(roms, 0, sizeof(*roms));
    if (read_file == NULL || bundle == NULL || roms == NULL ||
        bundle_capacity < MT_C64_SYSTEM_ROM_BUNDLE_SIZE) {
        return result;
    }

    for (index = 0U;
         index < sizeof(system_rom_files) / sizeof(system_rom_files[0]);
         ++index) {
        const mt_c64_system_rom_file_t *const file = &system_rom_files[index];
        uint8_t *const destination = bundle + file->offset;

        result.path = file->path;
        result.kind = file->kind;
        result.read = read_file(read_context, file->path, destination,
                                file->size);
        if (result.read.status != 0) {
            result.status = MT_C64_ROM_FILES_READ_FAILED;
            return result;
        }
        if (result.read.size != file->size) {
            result.status = MT_C64_ROM_FILES_SIZE_MISMATCH;
            return result;
        }

        result.validation = mt_c64_rom_validate(
            file->kind, destination, file->size);
        if (result.validation != MT_CONTENT_VALID) {
            result.status = MT_C64_ROM_FILES_VALIDATION_FAILED;
            return result;
        }
    }

    roms->basic.data = bundle;
    roms->basic.size = MT_SIZE_C64_BASIC;
    roms->kernal.data = bundle + MT_SIZE_C64_BASIC;
    roms->kernal.size = MT_SIZE_C64_KERNAL;
    roms->character.data =
        bundle + MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL;
    roms->character.size = MT_SIZE_C64_CHARACTER;

    result.status = MT_C64_ROM_FILES_OK;
    result.path = NULL;
    result.validation = MT_CONTENT_VALID;
    return result;
}

const char *mt_c64_rom_files_status_name(mt_c64_rom_files_status_t status)
{
    switch (status) {
    case MT_C64_ROM_FILES_OK: return "ok";
    case MT_C64_ROM_FILES_INVALID_ARGUMENT: return "invalid argument";
    case MT_C64_ROM_FILES_READ_FAILED: return "read failed";
    case MT_C64_ROM_FILES_SIZE_MISMATCH: return "size mismatch";
    case MT_C64_ROM_FILES_VALIDATION_FAILED: return "validation failed";
    }
    return "unknown";
}
