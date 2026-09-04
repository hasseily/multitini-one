/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mt_sd_status_t recover_status;
static mt_sd_status_t read_status;
static mt_sd_status_t write_status;
static bool file_exists;
static uint8_t stored_file[MT_SETTINGS_WIRE_SIZE];
static size_t stored_size;

static void store_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;
    for (index = 0U; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static mt_sd_result_t sd_result(mt_sd_status_t status, size_t size)
{
    mt_sd_result_t result = {status, status == MT_SD_OK ? 0U : 17U, size};
    return result;
}

mt_sd_result_t mt_sd_storage_journal_recover(const char *path,
                                             const char *journal_path,
                                             uint8_t *recovery_buffer,
                                             size_t capacity)
{
    assert(strcmp(path, MT_SETTINGS_PATH) == 0);
    assert(strcmp(journal_path, MT_SETTINGS_JOURNAL_PATH) == 0);
    assert(recovery_buffer != NULL);
    assert(capacity == MT_SETTINGS_WIRE_SIZE);
    return sd_result(recover_status, 0U);
}

mt_sd_result_t mt_sd_storage_read_file_optional_exact(
    const char *path, uint8_t *destination, size_t expected_size,
    bool *found)
{
    assert(strcmp(path, MT_SETTINGS_PATH) == 0);
    assert(destination != NULL && found != NULL);
    assert(expected_size == MT_SETTINGS_WIRE_SIZE);
    if (read_status != MT_SD_OK) return sd_result(read_status, 0U);
    *found = file_exists;
    if (!file_exists) return sd_result(MT_SD_OK, 0U);
    assert(stored_size == expected_size);
    memcpy(destination, stored_file, stored_size);
    return sd_result(MT_SD_OK, stored_size);
}

mt_sd_result_t mt_sd_storage_journal_replace(const char *path,
                                             const char *journal_path,
                                             const uint8_t *source,
                                             size_t size)
{
    assert(strcmp(path, MT_SETTINGS_PATH) == 0);
    assert(strcmp(journal_path, MT_SETTINGS_JOURNAL_PATH) == 0);
    assert(source != NULL && size == MT_SETTINGS_WIRE_SIZE);
    if (write_status != MT_SD_OK) return sd_result(write_status, 0U);
    memcpy(stored_file, source, size);
    stored_size = size;
    file_exists = true;
    return sd_result(MT_SD_OK, size);
}

static void reset_fake(void)
{
    recover_status = MT_SD_OK;
    read_status = MT_SD_OK;
    write_status = MT_SD_OK;
    file_exists = false;
    stored_size = 0U;
    memset(stored_file, 0, sizeof(stored_file));
}

int main(void)
{
    mt_supervisor_settings_t settings;
    mt_supervisor_settings_t loaded;
    mt_settings_result_t result;
    const mt_core_settings_profile_t *profile;
    size_t byte_index;

    reset_fake();
    result = mt_supervisor_settings_load(&settings);
    assert(result.status == MT_SETTINGS_MISSING);
    assert(!settings.auto_boot && settings.profile_count == 0U);

    assert(mt_supervisor_settings_remember_variant(&settings, "c64", "pal"));
    assert(settings.selected_core_id[0] == '\0');
    assert(mt_supervisor_settings_remember_c64_input(
        &settings, "c64", false, 1U));
    assert(mt_supervisor_settings_remember_c64_fast_roms(
        &settings, "c64", true));
    assert(mt_supervisor_settings_remember_c64_cpu_turbo(
        &settings, "c64", true));
    assert(mt_supervisor_settings_remember_drive8(
        &settings, "c64", "Writable Demo.d64"));
    assert(mt_supervisor_settings_remember_cartridge(
        &settings, "c64", "Fast Load.crt"));
    assert(!settings.auto_boot && settings.selected_core_id[0] == '\0');
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "pal"));
    assert(!settings.auto_boot && strcmp(settings.selected_core_id, "c64") == 0);
    assert(mt_supervisor_settings_remember_core(&settings, "gbc", "color"));
    assert(mt_supervisor_settings_remember_cartridge(
        &settings, "gbc", "Yars' Revenge.gbc"));
    settings.auto_boot = true;
    assert(mt_supervisor_settings_remember_variant(&settings, "c64", "pal"));
    assert(settings.auto_boot && strcmp(settings.selected_core_id, "gbc") == 0);

    result = mt_supervisor_settings_save(&settings);
    assert(result.status == MT_SETTINGS_OK);
    assert(file_exists && stored_size == MT_SETTINGS_WIRE_SIZE);
    result = mt_supervisor_settings_load(&loaded);
    assert(result.status == MT_SETTINGS_OK);
    assert(loaded.auto_boot);
    assert(strcmp(loaded.selected_core_id, "gbc") == 0);
    assert(loaded.profile_count == 2U);
    profile = mt_supervisor_settings_find(&loaded, "c64");
    assert(profile != NULL);
    assert(strcmp(profile->variant_id, "pal") == 0);
    assert(strcmp(profile->drive8_name, "Writable Demo.d64") == 0);
    assert(strcmp(profile->cartridge_name, "Fast Load.crt") == 0);
    assert(!profile->symbolic_keyboard && profile->joystick_port == 1U);
    assert(profile->c64_fast_roms);
    assert(profile->c64_cpu_turbo);
    profile = mt_supervisor_settings_find(&loaded, "gbc");
    assert(profile != NULL);
    assert(strcmp(profile->variant_id, "color") == 0);
    assert(strcmp(profile->cartridge_name, "Yars' Revenge.gbc") == 0);

    /* Schema 1 byte 242 was the unsafe whole-drive warp. It must not silently
     * turn into the new fast-ROM selection, while byte 243 CPU turbo retains
     * its meaning. */
    store_u32_le(stored_file + 8U, 1U);
    {
        const uint32_t crc = crc32_bytes(stored_file + 24U,
                                         stored_size - 24U);
        store_u32_le(stored_file + 16U, crc);
        store_u32_le(stored_file + 20U, ~crc);
    }
    result = mt_supervisor_settings_load(&loaded);
    assert(result.status == MT_SETTINGS_OK);
    profile = mt_supervisor_settings_find(&loaded, "c64");
    assert(profile != NULL && !profile->c64_fast_roms);
    assert(profile->c64_cpu_turbo);

    /* Put a current-schema image back before exercising corruption checks. */
    result = mt_supervisor_settings_save(&settings);
    assert(result.status == MT_SETTINGS_OK);

    for (byte_index = 0U; byte_index < stored_size; ++byte_index) {
        stored_file[byte_index] ^= 1U;
        result = mt_supervisor_settings_load(&loaded);
        assert(result.status == MT_SETTINGS_CORRUPT);
        assert(!loaded.auto_boot && loaded.profile_count == 0U);
        stored_file[byte_index] ^= 1U;
    }
    result = mt_supervisor_settings_load(&loaded);
    assert(result.status == MT_SETTINGS_OK);

    assert(mt_supervisor_settings_remember_drive8(
        &loaded, "c64", NULL));
    assert(mt_supervisor_settings_find(&loaded, "c64")
               ->drive8_name[0] == '\0');
    assert(!mt_supervisor_settings_remember_drive8(
        &loaded, "c64", "../escape.d64"));
    assert(!mt_supervisor_settings_remember_core(
        &loaded, "Bad Core", "pal"));
    assert(!mt_supervisor_settings_remember_c64_input(
        &loaded, "c64", true, 3U));

    stored_file[100U] ^= 0x80U;
    result = mt_supervisor_settings_load(&loaded);
    assert(result.status == MT_SETTINGS_CORRUPT);
    assert(!loaded.auto_boot && loaded.profile_count == 0U);

    recover_status = MT_SD_JOURNAL_INVALID;
    result = mt_supervisor_settings_load(&loaded);
    assert(result.status == MT_SETTINGS_RECOVERY_FAILED);
    assert(result.sd.backend_status == 17U);
    assert(!loaded.auto_boot && loaded.profile_count == 0U);
    recover_status = MT_SD_OK;
    read_status = MT_SD_READ_FAILED;
    result = mt_supervisor_settings_load(&loaded);
    assert(result.status == MT_SETTINGS_READ_FAILED);
    assert(!loaded.auto_boot && loaded.profile_count == 0U);

    mt_supervisor_settings_defaults(&loaded);
    loaded.auto_boot = true;
    result = mt_supervisor_settings_save(&loaded);
    assert(result.status == MT_SETTINGS_INVALID_ARGUMENT);
    loaded.auto_boot = false;
    assert(mt_supervisor_settings_remember_core(&loaded, "c64", "ntsc"));
    write_status = MT_SD_WRITE_FAILED;
    result = mt_supervisor_settings_save(&loaded);
    assert(result.status == MT_SETTINGS_WRITE_FAILED);
    assert(result.sd.backend_status == 17U);

    assert(strcmp(mt_settings_status_name(MT_SETTINGS_CORRUPT), "corrupt") ==
           0);
    assert(strcmp(mt_settings_status_name((mt_settings_status_t)99),
                  "unknown settings status") == 0);
    return 0;
}
