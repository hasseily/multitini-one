/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_ROM_FILES_H
#define MULTITINI_C64_ROM_FILES_H

#include <stddef.h>
#include <stdint.h>

#include "multitini_c64_boot.h"
#include "multitini_c64_interface.h"
#include "multitini_content.h"

#define MT_SD_CORE_ROOT "0:/cores"
#define MT_C64_SD_ROOT MT_SD_CORE_ROOT "/c64"
#define MT_C64_SD_ROM_ROOT MT_C64_SD_ROOT "/roms"
#define MT_C64_SD_MEDIA_ROOT MT_C64_SD_ROOT "/media"

#define MT_C64_SD_BASIC_PATH MT_C64_SD_ROM_ROOT "/basic.bin"
#define MT_C64_SD_KERNAL_PATH MT_C64_SD_ROM_ROOT "/kernal.bin"
#define MT_C64_SD_CHARACTER_PATH MT_C64_SD_ROM_ROOT "/characters.bin"
#define MT_C64_SD_1541_PATH MT_C64_SD_ROM_ROOT "/1541.bin"

#define MT_C64_SYSTEM_ROM_BUNDLE_SIZE \
    (MT_SIZE_C64_BASIC + MT_SIZE_C64_KERNAL + MT_SIZE_C64_CHARACTER)

typedef struct {
    int status;
    unsigned backend_status;
    size_t size;
} mt_c64_rom_file_read_result_t;

typedef mt_c64_rom_file_read_result_t (*mt_c64_rom_file_read_fn)(
    void *context, const char *path, uint8_t *destination,
    size_t expected_size);

typedef enum {
    MT_C64_ROM_FILES_OK = 0,
    MT_C64_ROM_FILES_INVALID_ARGUMENT,
    MT_C64_ROM_FILES_READ_FAILED,
    MT_C64_ROM_FILES_SIZE_MISMATCH,
    MT_C64_ROM_FILES_VALIDATION_FAILED
} mt_c64_rom_files_status_t;

typedef struct {
    mt_c64_rom_files_status_t status;
    const char *path;
    mt_c64_rom_kind_t kind;
    mt_c64_rom_file_read_result_t read;
    mt_content_validation_t validation;
} mt_c64_rom_files_result_t;

const char *mt_c64_sd_rom_path(mt_c64_rom_kind_t kind);

/*
 * Read and validate the three C64 system ROMs from their canonical SD paths.
 * The 1541 path is part of the namespace but is not loaded by this helper;
 * platform startup validates and streams that independent 16 KiB object only
 * when enabling drive 8. The serial system-ROM bundle therefore stays 20 KiB.
 */
mt_c64_rom_files_result_t mt_c64_rom_files_load_system(
    mt_c64_rom_file_read_fn read_file, void *read_context,
    uint8_t *bundle, size_t bundle_capacity, mt_c64_system_roms_t *roms);

const char *mt_c64_rom_files_status_name(mt_c64_rom_files_status_t status);

#endif
