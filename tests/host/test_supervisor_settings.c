/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LEGACY_WIRE_SIZE 3960U
#define LEGACY_PROFILE_SIZE 244U
#define V3_WIRE_SIZE 4024U
#define V3_PROFILE_SIZE 248U
#define PROFILE_SIZE 252U

static mt_sd_status_t recover_status;
static mt_sd_status_t read_status;
static mt_sd_status_t write_status;
static bool file_exists;
static uint8_t stored_file[MT_SETTINGS_WIRE_SIZE];
static size_t stored_size;
static unsigned read_calls;
static bool disappear_on_retry;
static mt_sd_status_t retry_status;

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

static void update_stored_crc(void)
{
    const uint32_t crc = crc32_bytes(stored_file + 24U, stored_size - 24U);
    store_u32_le(stored_file + 16U, crc);
    store_u32_le(stored_file + 20U, ~crc);
}

/* Derive each shipped older layout, including every unused profile slot. */
static void convert_stored_to_legacy(uint32_t schema)
{
    uint8_t current[MT_SETTINGS_WIRE_SIZE];
    const size_t profile_size = schema == 3U ? V3_PROFILE_SIZE : LEGACY_PROFILE_SIZE;
    const size_t wire_size = schema == 3U ? V3_WIRE_SIZE : LEGACY_WIRE_SIZE;
    size_t index;
    assert(stored_size == MT_SETTINGS_WIRE_SIZE);
    memcpy(current, stored_file, sizeof(current));
    memset(stored_file, 0, sizeof(stored_file));
    memcpy(stored_file, current, 56U);
    for (index = 0U; index < MT_SETTINGS_MAX_PROFILES; ++index) {
        memcpy(stored_file + 56U + index * profile_size,
               current + 56U + index * PROFILE_SIZE, profile_size);
    }
    stored_size = wire_size;
    store_u32_le(stored_file + 8U, schema);
    store_u32_le(stored_file + 12U, (uint32_t)wire_size);
    update_stored_crc();
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
    assert(expected_size == MT_SETTINGS_WIRE_SIZE || expected_size == LEGACY_WIRE_SIZE ||
           expected_size == V3_WIRE_SIZE);
    ++read_calls;
    *found = false;
    if (read_status != MT_SD_OK) return sd_result(read_status, 0U);
    if (expected_size != MT_SETTINGS_WIRE_SIZE) {
        if (disappear_on_retry) file_exists = false;
        if (retry_status != MT_SD_OK) return sd_result(retry_status, 0U);
    }
    if (!file_exists) return sd_result(MT_SD_OK, 0U);
    if (stored_size != expected_size) return sd_result(MT_SD_SIZE_MISMATCH, stored_size);
    *found = true;
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
    read_calls = 0U;
    disappear_on_retry = false;
    retry_status = MT_SD_OK;
    memset(stored_file, 0, sizeof(stored_file));
}

static void test_vectrex_cartridge_paths(void)
{
    static const char *const invalid_paths[] = {
        "/game.vec", "//game.vec", "games//game.vec", "games/", ".", "..",
        "./game.vec", "../game.vec", "games/./game.vec", "games/../game.vec",
        "games/.. /game.vec", "games/. /game.vec", "games./game.vec",
        "games /game.vec", "games/game.vec.", "games/game.vec ",
        "C:/game.vec", "games:C/game.vec", "games\\game.vec",
        "games/*game.vec", "games/game?.vec", "games/\"game.vec",
        "games/<game.vec", "games/>game.vec", "games/|game.vec",
        "games/\x1f" "game.vec", "games/\x7f" "game.vec"
    };
    static const char *const flat_cores[] = {"c64", "gbc"};
    const char *const nested = "Classics/Arcade Games/Star Castle.vec";
    mt_supervisor_settings_t settings;
    mt_supervisor_settings_t loaded;
    mt_supervisor_settings_t preserved;
    uint8_t valid_wire[MT_SETTINGS_WIRE_SIZE];
    char maximum_path[MT_SETTINGS_MEDIA_NAME_MAX];
    char oversized_path[MT_SETTINGS_MEDIA_NAME_MAX + 1U];
    size_t index;
    const size_t cartridge_offset = 24U + 32U + 144U;

    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings, "vectrex", "native"));
    assert(mt_supervisor_settings_remember_cartridge(&settings, "vectrex", nested));
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(stored_size == MT_SETTINGS_WIRE_SIZE);
    assert(memcmp(stored_file + cartridge_offset, nested, strlen(nested) + 1U) == 0);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(strcmp(mt_supervisor_settings_find(&loaded, "vectrex")->cartridge_name,
                  nested) == 0);
    assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_OK);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(strcmp(mt_supervisor_settings_find(&loaded, "vectrex")->cartridge_name,
                  nested) == 0);
    preserved = settings;
    memcpy(valid_wire, stored_file, sizeof(valid_wire));

    for (index = 0U; index < sizeof(invalid_paths) / sizeof(invalid_paths[0]); ++index) {
        uint32_t crc;
        assert(!mt_supervisor_settings_remember_cartridge(
            &settings, "vectrex", invalid_paths[index]));
        assert(memcmp(&settings, &preserved, sizeof(settings)) == 0);
        /* Validate both encoding and a CRC-valid stored path independently of
         * the setter, so corrupted/stale files cannot bypass confinement. */
        loaded = settings;
        strcpy(loaded.profiles[0].cartridge_name, invalid_paths[index]);
        assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_INVALID_ARGUMENT);
        assert(memcmp(stored_file, valid_wire, sizeof(valid_wire)) == 0);
        memset(stored_file + cartridge_offset, 0, MT_SETTINGS_MEDIA_NAME_MAX);
        memcpy(stored_file + cartridge_offset, invalid_paths[index], strlen(invalid_paths[index]));
        crc = crc32_bytes(stored_file + 24U, stored_size - 24U);
        store_u32_le(stored_file + 16U, crc);
        store_u32_le(stored_file + 20U, ~crc);
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
        assert(loaded.profile_count == 0U);
        memcpy(stored_file, valid_wire, sizeof(valid_wire));
    }

    memset(maximum_path, 'a', sizeof(maximum_path) - 1U);
    maximum_path[1] = '/';
    maximum_path[sizeof(maximum_path) - 1U] = '\0';
    assert(strlen(maximum_path) == 95U);
    assert(mt_supervisor_settings_remember_cartridge(&settings, "vectrex", maximum_path));
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(strcmp(loaded.profiles[0].cartridge_name, maximum_path) == 0);
    preserved = settings;
    memset(oversized_path, 'a', sizeof(oversized_path) - 1U);
    oversized_path[1] = '/';
    oversized_path[sizeof(oversized_path) - 1U] = '\0';
    assert(!mt_supervisor_settings_remember_cartridge(&settings, "vectrex", oversized_path));
    assert(memcmp(&settings, &preserved, sizeof(settings)) == 0);
    memcpy(loaded.profiles[0].cartridge_name, oversized_path, MT_SETTINGS_MEDIA_NAME_MAX);
    assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_INVALID_ARGUMENT);
    assert(mt_supervisor_settings_remember_cartridge(&settings, "vectrex", ""));
    assert(settings.profiles[0].cartridge_name[0] == '\0');
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(loaded.profiles[0].cartridge_name[0] == '\0');
    assert(mt_supervisor_settings_remember_cartridge(&settings, "vectrex", NULL));
    assert(!mt_supervisor_settings_remember_drive8(&settings, "vectrex", nested));

    for (index = 0U; index < sizeof(flat_cores) / sizeof(flat_cores[0]); ++index) {
        uint32_t crc;
        mt_supervisor_settings_defaults(&settings);
        assert(mt_supervisor_settings_remember_core(&settings, flat_cores[index], "default"));
        assert(mt_supervisor_settings_remember_cartridge(&settings, flat_cores[index], "Original.cart"));
        preserved = settings;
        assert(!mt_supervisor_settings_remember_cartridge(&settings, flat_cores[index], nested));
        assert(memcmp(&settings, &preserved, sizeof(settings)) == 0);
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        strcpy(settings.profiles[0].cartridge_name, nested);
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_INVALID_ARGUMENT);
        memset(stored_file + cartridge_offset, 0, MT_SETTINGS_MEDIA_NAME_MAX);
        memcpy(stored_file + cartridge_offset, nested, strlen(nested));
        crc = crc32_bytes(stored_file + 24U, stored_size - 24U);
        store_u32_le(stored_file + 16U, crc);
        store_u32_le(stored_file + 20U, ~crc);
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    }
}

static void assert_default_borders(const mt_core_settings_profile_t *profile)
{
    assert(profile != NULL && !profile->border_enabled);
    assert(profile->border_style == 0U && profile->border_color == 7U &&
           profile->border_padding == 6U);
}

static void test_borders(void)
{
    static const uint8_t invalid[][3] = {
        {5U, 7U, 6U}, {255U, 7U, 6U}, {0U, 16U, 6U},
        {0U, 255U, 6U}, {0U, 7U, 0U}, {0U, 7U, 21U}, {0U, 7U, 255U}
    };
    static const uint8_t invalid_wire[][2] = {
        {0U, 2U}, {1U, 5U}, {2U, 16U}, {3U, 0U}, {3U, 21U}
    };
    mt_supervisor_settings_t settings;
    mt_supervisor_settings_t loaded;
    mt_supervisor_settings_t preserved;
    uint8_t valid_wire[MT_SETTINGS_WIRE_SIZE];
    size_t index;

    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    preserved = settings;
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert(!mt_supervisor_settings_remember_borders(&settings, "vectrex", true,
            invalid[index][0], invalid[index][1], invalid[index][2]));
        assert(memcmp(&settings, &preserved, sizeof(settings)) == 0);
    }
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "pal"));
    assert(mt_supervisor_settings_remember_core(&settings, "gbc", "color"));
    assert(mt_supervisor_settings_remember_core(&settings, "vectrex", "native"));
    assert_default_borders(mt_supervisor_settings_find(&settings, "c64"));
    assert_default_borders(mt_supervisor_settings_find(&settings, "gbc"));
    assert_default_borders(mt_supervisor_settings_find(&settings, "vectrex"));
    /* Exercise each accepted style, palette entry and padding value, both
     * enable states, and retention when another core's profile is changed. */
    for (index = 0U; index < 20U; ++index) {
        const uint8_t style = (uint8_t)(index % 5U);
        const uint8_t color = (uint8_t)(index % 16U);
        const uint8_t padding = (uint8_t)(index + 1U);
        const bool enabled = (index & 1U) != 0U;
        const mt_core_settings_profile_t *profile;
        assert(mt_supervisor_settings_remember_borders(&settings, "vectrex",
            enabled, style, color, padding));
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        assert(stored_file[8U] == 4U && stored_size == 4088U);
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        profile = mt_supervisor_settings_find(&loaded, "vectrex");
        assert(profile->border_enabled == enabled && profile->border_style == style &&
               profile->border_color == color && profile->border_padding == padding);
        assert_default_borders(mt_supervisor_settings_find(&loaded, "c64"));
        assert_default_borders(mt_supervisor_settings_find(&loaded, "gbc"));
    }
    assert(mt_supervisor_settings_remember_borders(&settings, "c64", true, 3U, 2U, 10U));
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    memcpy(valid_wire, stored_file, sizeof(valid_wire));
    preserved = settings;
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert(!mt_supervisor_settings_remember_borders(&settings, "vectrex", false,
            invalid[index][0], invalid[index][1], invalid[index][2]));
        assert(memcmp(&settings, &preserved, sizeof(settings)) == 0);
        loaded = settings;
        loaded.profiles[0].border_style = invalid[index][0];
        loaded.profiles[0].border_color = invalid[index][1];
        loaded.profiles[0].border_padding = invalid[index][2];
        assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_INVALID_ARGUMENT);
        assert(memcmp(stored_file, valid_wire, sizeof(valid_wire)) == 0);
    }
    for (index = 0U; index < sizeof(invalid_wire) / sizeof(invalid_wire[0]); ++index) {
        stored_file[56U + 244U + invalid_wire[index][0]] = invalid_wire[index][1];
        update_stored_crc();
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
        assert(loaded.profile_count == 0U && !loaded.auto_boot);
        memcpy(stored_file, valid_wire, sizeof(valid_wire));
    }
    assert(!mt_supervisor_settings_remember_borders(NULL, "vectrex", true, 0U, 7U, 6U));
    assert(!mt_supervisor_settings_remember_borders(&settings, "invalid core", true, 0U, 7U, 6U));
    assert(memcmp(&settings, &preserved, sizeof(settings)) == 0);
}

static void test_vectrex_video_settings(void)
{
    static const uint8_t bad_wire[][2] = {{0U, 2U}, {1U, 4U}, {2U, 4U}, {3U, 1U}};
    mt_supervisor_settings_t settings;
    mt_supervisor_settings_t loaded;
    mt_supervisor_settings_t before;
    uint8_t valid_wire[MT_SETTINGS_WIRE_SIZE];
    size_t value;
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    before = settings;
    assert(!mt_supervisor_settings_remember_vectrex_video(&settings, "vectrex", true, 4U, 1U));
    assert(!mt_supervisor_settings_remember_vectrex_video(&settings, "vectrex", true, 1U, 255U));
    assert(!mt_supervisor_settings_remember_vectrex_video(&settings, "c64", true, 1U, 1U));
    assert(!mt_supervisor_settings_remember_vectrex_video(&settings, NULL, true, 1U, 1U));
    assert(!mt_supervisor_settings_remember_vectrex_video(NULL, "vectrex", true, 1U, 1U));
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
    assert(mt_supervisor_settings_remember_core(&settings, "vectrex", "native"));
    assert(mt_supervisor_settings_remember_variant(&settings, "c64", "pal"));
    assert(settings.profiles[0].vectrex_overlay_enabled &&
           settings.profiles[0].vectrex_beam_gain == 1U && settings.profiles[0].vectrex_overlay_opacity == 1U);
    for (value = 0U; value < 32U; ++value) {
        const bool enabled = (value & 1U) != 0U;
        const uint8_t gain = (uint8_t)((value >> 1U) & 3U);
        const uint8_t opacity = (uint8_t)((value >> 3U) & 3U);
        assert(mt_supervisor_settings_remember_vectrex_video(&settings, "vectrex", enabled, gain, opacity));
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        assert(stored_file[8U] == 4U && stored_size == 4088U && stored_file[56U + 251U] == 0U);
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
        assert(loaded.profiles[1].vectrex_beam_gain == 1U &&
               loaded.profiles[1].vectrex_overlay_opacity == 1U);
    }
    memcpy(valid_wire, stored_file, sizeof(valid_wire));
    before = settings;
    loaded = settings; loaded.profiles[0].vectrex_beam_gain = 4U;
    assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_INVALID_ARGUMENT);
    loaded = settings; loaded.profiles[0].vectrex_overlay_opacity = 255U;
    assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_INVALID_ARGUMENT);
    assert(memcmp(stored_file, valid_wire, sizeof(valid_wire)) == 0);
    for (value = 0U; value < sizeof(bad_wire) / sizeof(bad_wire[0]); ++value) {
        stored_file[56U + 248U + bad_wire[value][0]] = bad_wire[value][1];
        update_stored_crc();
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
        assert(loaded.profile_count == 0U);
        memcpy(stored_file, valid_wire, sizeof(valid_wire));
    }
    /* Reserved bytes are rejected even in unused profile slots with a valid CRC. */
    stored_file[MT_SETTINGS_WIRE_SIZE - 1U] = 1U;
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
}

static void test_legacy_migration(void)
{
    mt_supervisor_settings_t settings;
    mt_supervisor_settings_t loaded;
    mt_supervisor_settings_t expected;
    uint8_t legacy_wire[V3_WIRE_SIZE];
    uint32_t schema;
    size_t index;
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "ntsc"));
    assert(mt_supervisor_settings_remember_c64_input(&settings, "c64", false, 1U));
    assert(mt_supervisor_settings_remember_c64_fast_roms(&settings, "c64", true));
    assert(mt_supervisor_settings_remember_c64_cpu_turbo(&settings, "c64", true));
    assert(mt_supervisor_settings_remember_drive8(&settings, "c64", "Writable Demo.d64"));
    assert(mt_supervisor_settings_remember_cartridge(&settings, "c64", "Fast Load.crt"));
    assert(mt_supervisor_settings_remember_core(&settings, "gbc", "color"));
    assert(mt_supervisor_settings_remember_cartridge(&settings, "gbc", "Yars' Revenge.gbc"));
    assert(mt_supervisor_settings_remember_core(&settings, "vectrex", "native"));
    assert(mt_supervisor_settings_remember_cartridge(&settings, "vectrex",
        "Classics/Arcade Games/Star Castle.vec"));
    /* Populate every slot to expose incorrect strides or end-of-buffer reads. */
    for (index = 3U; index < MT_SETTINGS_MAX_PROFILES; ++index) {
        char core_id[24];
        (void)snprintf(core_id, sizeof(core_id), "core%u", (unsigned)index);
        assert(mt_supervisor_settings_remember_variant(&settings, core_id, "default"));
        assert(mt_supervisor_settings_remember_borders(&settings, core_id, true,
            (uint8_t)(index % 5U), (uint8_t)index, (uint8_t)(index + 1U)));
    }
    settings.auto_boot = true;
    assert(mt_supervisor_settings_remember_vectrex_video(&settings, "vectrex", false, 3U, 0U));
    for (schema = 1U; schema <= 3U; ++schema) {
        const size_t wire_size = schema == 3U ? V3_WIRE_SIZE : LEGACY_WIRE_SIZE;
        expected = settings;
        for (index = 0U; index < expected.profile_count; ++index) {
            if (schema < 3U) {
                expected.profiles[index].border_enabled = false;
                expected.profiles[index].border_style = 0U;
                expected.profiles[index].border_color = 7U;
                expected.profiles[index].border_padding = 6U;
            }
            expected.profiles[index].vectrex_overlay_enabled = true;
            expected.profiles[index].vectrex_beam_gain = 1U;
            expected.profiles[index].vectrex_overlay_opacity = 1U;
        }
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        convert_stored_to_legacy(schema);
        memcpy(legacy_wire, stored_file, wire_size);
        read_calls = 0U;
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        assert(read_calls == 2U && stored_size == wire_size);
        expected.profiles[0].c64_fast_roms = schema != 1U;
        assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        if (schema < 3U) {
            for (index = 0U; index < loaded.profile_count; ++index)
                assert_default_borders(&loaded.profiles[index]);
        }
        /* Read-only migration leaves old bytes intact until the user saves. */
        assert(memcmp(stored_file, legacy_wire, wire_size) == 0);
        assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_OK);
        assert(stored_size == MT_SETTINGS_WIRE_SIZE && stored_file[8U] == 4U);
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        assert(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
        memcpy(stored_file, legacy_wire, wire_size);
        stored_size = wire_size;
        stored_file[stored_size - 1U] ^= 1U;
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
        assert(loaded.profile_count == 0U && !loaded.auto_boot);
    }
    /* Unknown/truncated/oversized files cannot be mistaken for the old size. */
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    stored_size = MT_SETTINGS_WIRE_SIZE - 1U;
    read_calls = 0U;
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_READ_FAILED);
    assert(read_calls == 1U && loaded.profile_count == 0U);
    stored_size = MT_SETTINGS_WIRE_SIZE + 1U;
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_READ_FAILED);
    stored_size = 4152U; /* Unknown larger layout. */
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_READ_FAILED);
    stored_size = 0U;
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_READ_FAILED);
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    store_u32_le(stored_file + 8U, 2U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    store_u32_le(stored_file + 8U, 5U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    convert_stored_to_legacy(1U);
    store_u32_le(stored_file + 8U, 3U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    store_u32_le(stored_file + 8U, 2U);
    retry_status = MT_SD_READ_FAILED;
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_READ_FAILED);
    assert(loaded.profile_count == 0U);
    retry_status = MT_SD_OK;
    disappear_on_retry = true;
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_MISSING);
    assert(loaded.profile_count == 0U);
    expected = settings;
    assert(!mt_supervisor_settings_remember_borders(&settings, "extra", true, 0U, 7U, 6U));
    assert(memcmp(&settings, &expected, sizeof(settings)) == 0);
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
    convert_stored_to_legacy(1U);
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
    test_vectrex_cartridge_paths();
    test_borders();
    test_vectrex_video_settings();
    test_legacy_migration();
    return 0;
}
