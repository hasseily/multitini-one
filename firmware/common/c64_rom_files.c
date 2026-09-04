/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_rom_files.h"

#include <string.h>

typedef struct {
    mt_c64_rom_kind_t kind;
    size_t offset;
    size_t size;
} mt_c64_system_rom_file_t;

static const mt_c64_system_rom_file_t system_rom_files[] = {
    {MT_C64_ROM_BASIC, 0U, MT_SIZE_C64_BASIC},
    {MT_C64_ROM_KERNAL, MT_SIZE_C64_BASIC,
     MT_SIZE_C64_KERNAL},
    {MT_C64_ROM_CHARACTER,
     MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL, MT_SIZE_C64_CHARACTER},
};

static const mt_c64_rom_paths_t stock_paths = {
    MT_C64_SD_BASIC_PATH,
    MT_C64_SD_KERNAL_PATH,
    MT_C64_SD_CHARACTER_PATH,
    MT_C64_SD_1541_PATH,
};

static const mt_c64_rom_paths_t fast_paths = {
    MT_C64_SD_BASIC_PATH,
    MT_C64_SD_FAST_KERNAL_PATH,
    MT_C64_SD_CHARACTER_PATH,
    MT_C64_SD_FAST_1541_PATH,
};

static const char *system_path(const mt_c64_rom_paths_t *paths,
                               mt_c64_rom_kind_t kind)
{
    if (paths == NULL) return NULL;
    switch (kind) {
    case MT_C64_ROM_BASIC: return paths->basic;
    case MT_C64_ROM_KERNAL: return paths->kernal;
    case MT_C64_ROM_CHARACTER: return paths->character;
    case MT_C64_ROM_DRIVE_1541: return paths->drive_1541;
    }
    return NULL;
}

const char *mt_c64_sd_rom_path(mt_c64_rom_kind_t kind)
{
    return system_path(&stock_paths, kind);
}

const mt_c64_rom_paths_t *mt_c64_stock_rom_paths(void)
{
    return &stock_paths;
}

const mt_c64_rom_paths_t *mt_c64_fast_rom_paths(void)
{
    return &fast_paths;
}

mt_c64_rom_files_result_t mt_c64_rom_file_load(
    mt_c64_rom_kind_t kind, const char *path,
    mt_c64_rom_file_read_fn read_file, void *read_context,
    uint8_t *destination, size_t destination_capacity)
{
    mt_c64_rom_files_result_t result = {
        MT_C64_ROM_FILES_INVALID_ARGUMENT,
        path,
        kind,
        {0, 0U, 0U},
        MT_CONTENT_INVALID_ARGUMENT,
    };
    size_t expected_size;

    switch (kind) {
    case MT_C64_ROM_BASIC:
        expected_size = MT_SIZE_C64_BASIC;
        break;
    case MT_C64_ROM_KERNAL:
        expected_size = MT_SIZE_C64_KERNAL;
        break;
    case MT_C64_ROM_CHARACTER:
        expected_size = MT_SIZE_C64_CHARACTER;
        break;
    case MT_C64_ROM_DRIVE_1541:
        expected_size = MT_SIZE_C64_1541_ROM;
        break;
    default:
        return result;
    }
    if (path == NULL || path[0] == '\0' || read_file == NULL ||
        destination == NULL || destination_capacity < expected_size) {
        return result;
    }

    result.read = read_file(read_context, path, destination, expected_size);
    if (result.read.status != 0) {
        result.status = MT_C64_ROM_FILES_READ_FAILED;
        return result;
    }
    if (result.read.size != expected_size) {
        result.status = MT_C64_ROM_FILES_SIZE_MISMATCH;
        return result;
    }
    result.validation = mt_c64_rom_validate(kind, destination, expected_size);
    if (result.validation != MT_CONTENT_VALID) {
        result.status = MT_C64_ROM_FILES_VALIDATION_FAILED;
        return result;
    }
    result.status = MT_C64_ROM_FILES_OK;
    return result;
}

mt_c64_rom_files_result_t mt_c64_rom_files_load_system_from(
    const mt_c64_rom_paths_t *paths, mt_c64_rom_file_read_fn read_file,
    void *read_context, uint8_t *bundle, size_t bundle_capacity,
    mt_c64_system_roms_t *roms)
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
    if (paths == NULL || read_file == NULL || bundle == NULL || roms == NULL ||
        bundle_capacity < MT_C64_SYSTEM_ROM_BUNDLE_SIZE) {
        return result;
    }

    for (index = 0U;
         index < sizeof(system_rom_files) / sizeof(system_rom_files[0]);
         ++index) {
        const mt_c64_system_rom_file_t *const file = &system_rom_files[index];
        uint8_t *const destination = bundle + file->offset;
        const char *const path = system_path(paths, file->kind);

        result = mt_c64_rom_file_load(
            file->kind, path, read_file, read_context, destination,
            file->size);
        if (result.status != MT_C64_ROM_FILES_OK) return result;
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

mt_c64_rom_files_result_t mt_c64_rom_files_load_system(
    mt_c64_rom_file_read_fn read_file, void *read_context,
    uint8_t *bundle, size_t bundle_capacity, mt_c64_system_roms_t *roms)
{
    return mt_c64_rom_files_load_system_from(
        &stock_paths, read_file, read_context, bundle, bundle_capacity, roms);
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
