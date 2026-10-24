/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"
#include "multitini_vectrex_interface.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"

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
#define V4_PROFILE_SIZE 252U
#define V4_WIRE_SIZE 4088U
#define PROFILE_SIZE 290U
#define V5_WIRE_SIZE 4696U
#define V6_WIRE_SIZE 5038U
#define V7_WIRE_SIZE 5054U
#define V9_WIRE_SIZE 10950U

static mt_sd_status_t recover_status;
static mt_sd_status_t read_status;
static mt_sd_status_t write_status;
static bool file_exists;
static uint8_t stored_file[MT_SETTINGS_WIRE_SIZE];
static size_t stored_size;
static unsigned read_calls;
static bool disappear_on_retry;
static mt_sd_status_t retry_status;
static uint8_t turbo_file[MT_TURBO_CONTROLS_WIRE_SIZE];
static bool turbo_exists;
bool mt_schema10_roundtrip(const uint8_t *input, uint8_t *output);

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
    const size_t profile_size = schema >= 5U ? PROFILE_SIZE : schema == 4U ? V4_PROFILE_SIZE :
        schema == 3U ? V3_PROFILE_SIZE : LEGACY_PROFILE_SIZE;
    const size_t wire_size = schema >= 8U ? V9_WIRE_SIZE : schema == 7U ? V7_WIRE_SIZE : schema == 6U ? V6_WIRE_SIZE : schema == 5U ? V5_WIRE_SIZE : schema == 4U ? V4_WIRE_SIZE :
        schema == 3U ? V3_WIRE_SIZE : LEGACY_WIRE_SIZE;
    size_t index;
    assert(stored_size == MT_SETTINGS_WIRE_SIZE);
    memcpy(current, stored_file, sizeof(current));
    memset(stored_file, 0, sizeof(stored_file));
    memcpy(stored_file, current, 56U);
    for (index = 0U; index < MT_SETTINGS_MAX_PROFILES; ++index) {
        memcpy(stored_file + 56U + index * profile_size,
               current + 56U + index * PROFILE_SIZE, profile_size);
    }
    if (schema >= 6U) memcpy(stored_file + V5_WIRE_SIZE, current + V5_WIRE_SIZE, wire_size - V5_WIRE_SIZE);
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
    if (strcmp(path, MT_TURBO_CONTROLS_PATH) == 0) {
        assert(strcmp(journal_path, MT_TURBO_CONTROLS_JOURNAL_PATH) == 0);
        assert(capacity == sizeof turbo_file);
        return sd_result(recover_status, 0U);
    }
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
    if (strcmp(path, MT_TURBO_CONTROLS_PATH) == 0) {
        assert(expected_size == sizeof turbo_file);
        *found = turbo_exists;
        if (turbo_exists) memcpy(destination, turbo_file, sizeof turbo_file);
        return sd_result(read_status, turbo_exists ? sizeof turbo_file : 0U);
    }
    assert(strcmp(path, MT_SETTINGS_PATH) == 0);
    assert(destination != NULL && found != NULL);
    assert(expected_size == MT_SETTINGS_WIRE_SIZE || expected_size == LEGACY_WIRE_SIZE ||
           expected_size == V3_WIRE_SIZE || expected_size == V4_WIRE_SIZE || expected_size == V5_WIRE_SIZE || expected_size == V6_WIRE_SIZE || expected_size == V7_WIRE_SIZE || expected_size == V9_WIRE_SIZE);
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
    if (strcmp(path, MT_TURBO_CONTROLS_PATH) == 0) {
        assert(strcmp(journal_path, MT_TURBO_CONTROLS_JOURNAL_PATH) == 0);
        assert(size == sizeof turbo_file);
        if (write_status != MT_SD_OK) return sd_result(write_status, 0U);
        memcpy(turbo_file, source, size); turbo_exists = true;
        return sd_result(MT_SD_OK, size);
    }
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
        assert(stored_file[8U] == 10U && stored_size == MT_SETTINGS_WIRE_SIZE);
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
    static const uint8_t bad_wire[][2] = {{0U, 2U}, {1U, 4U}, {2U, 4U}, {3U, 5U}};
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
        assert(stored_file[8U] == 10U && stored_size == MT_SETTINGS_WIRE_SIZE && stored_file[56U + 251U] == 0U);
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
    stored_file[56U + (MT_SETTINGS_MAX_PROFILES - 1U) * PROFILE_SIZE + 251U] = 1U;
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
}

static void test_legacy_migration(void)
{
    mt_supervisor_settings_t settings;
    mt_supervisor_settings_t loaded;
    mt_supervisor_settings_t expected;
    uint8_t legacy_wire[V7_WIRE_SIZE];
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
    assert(mt_video_effects_set(&settings.profiles[0].video_effects,
                                MT_VIDEO_EFFECT_HUE, -120));
    for (schema = 1U; schema <= 7U; ++schema) {
        const size_t wire_size = schema == 7U ? V7_WIRE_SIZE : schema == 6U ? V6_WIRE_SIZE : schema == 5U ? V5_WIRE_SIZE : schema == 4U ? V4_WIRE_SIZE :
            schema == 3U ? V3_WIRE_SIZE : LEGACY_WIRE_SIZE;
        expected = settings;
        expected.legacy_gamepad_pending = (1U << MT_GAMEPAD_SLOTS) - 1U;
        for (index = 0U; index < expected.profile_count; ++index) {
            if (schema < 5U) mt_video_effects_defaults(&expected.profiles[index].video_effects);
            if (schema < 3U) {
                expected.profiles[index].border_enabled = false;
                expected.profiles[index].border_style = 0U;
                expected.profiles[index].border_color = 7U;
                expected.profiles[index].border_padding = 6U;
            }
            if (schema < 4U) {
                expected.profiles[index].vectrex_overlay_enabled = true;
                expected.profiles[index].vectrex_beam_gain = 1U;
                expected.profiles[index].vectrex_overlay_opacity = 1U;
            }
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
        assert(stored_size == MT_SETTINGS_WIRE_SIZE && stored_file[8U] == 10U);
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
    store_u32_le(stored_file + 8U, 11U);
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

static void test_video_effects_settings(void)
{
    mt_supervisor_settings_t settings, loaded, before;
    mt_video_effects_settings_t effects;
    uint8_t valid_wire[MT_SETTINGS_WIRE_SIZE];
    unsigned effect;
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "ntsc"));
    mt_video_effects_defaults(&effects);
    for (effect = 0U; effect < MT_VIDEO_EFFECT_COUNT; ++effect) {
        int minimum, maximum, step;
        assert(mt_video_effects_range((mt_video_effect_id_t)effect,
                                      &minimum, &maximum, &step));
        assert(mt_video_effects_set(&effects, (mt_video_effect_id_t)effect,
                                    (effect & 1U) != 0U ? minimum : maximum));
    }
    assert(mt_supervisor_settings_remember_video_effects(&settings, "c64", &effects));
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(stored_file[8U] == 10U && stored_size == MT_SETTINGS_WIRE_SIZE);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(memcmp(&settings, &loaded, sizeof(settings)) == 0);
    /* Explicit signed LE encoding, independent of host layout/padding. */
    assert(effects.brightness == -100);
    assert(stored_file[56U + 252U + 2U] == 0x9cU);
    assert(stored_file[56U + 252U + 3U] == 0xffU);
    memcpy(valid_wire, stored_file, sizeof(valid_wire));
    before = settings;
    effects.reserved = 1U;
    assert(!mt_supervisor_settings_remember_video_effects(&settings, "newcore", &effects));
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
    assert(!mt_supervisor_settings_remember_video_effects(&settings, "c64", NULL));
    settings.profiles[0].video_effects.gamma = 0U;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_INVALID_ARGUMENT);
    for (effect = 0U; effect < MT_VIDEO_EFFECT_COUNT; ++effect) {
        memcpy(stored_file, valid_wire, sizeof(valid_wire));
        stored_file[56U + 252U + effect * 2U] = 0xffU;
        stored_file[56U + 253U + effect * 2U] = 0x7fU;
        update_stored_crc();
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
        assert(loaded.profile_count == 0U);
    }
    memcpy(stored_file, valid_wire, sizeof(valid_wire));
    convert_stored_to_legacy(4U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    mt_video_effects_defaults(&effects);
    assert(memcmp(&loaded.profiles[0].video_effects, &effects, sizeof(effects)) == 0);
    /* V4's reserved byte stays reserved, including unused profile slots. */
    stored_file[56U + 15U * V4_PROFILE_SIZE + 251U] = 1U;
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
}

static void test_video_multiplier(void)
{
    mt_supervisor_settings_t settings,loaded,before;
    reset_fake();mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings,"c64","pal"));
    assert(mt_supervisor_settings_remember_variant(&settings,"gbc","color"));
    assert(settings.profiles[0].video_multiplier==0U && settings.profiles[1].video_multiplier==0U);
    for(uint8_t multiplier=0U;multiplier<=4U;++multiplier) {
        assert(mt_supervisor_settings_remember_video_multiplier(&settings,"c64",multiplier));
        assert(mt_supervisor_settings_remember_video_multiplier(&settings,"gbc",(uint8_t)(4U-multiplier)));
        assert(mt_supervisor_settings_save(&settings).status==MT_SETTINGS_OK);
        assert(stored_file[8U]==10U && stored_file[56U+251U]==multiplier);
        assert(mt_supervisor_settings_load(&loaded).status==MT_SETTINGS_OK);
        assert(memcmp(&loaded,&settings,sizeof(settings))==0);
    }
    before=settings;
    assert(!mt_supervisor_settings_remember_video_multiplier(&settings,"c64",5U));
    assert(!mt_supervisor_settings_remember_video_multiplier(NULL,"c64",1U));
    assert(!mt_supervisor_settings_remember_video_multiplier(&settings,NULL,1U));
    assert(memcmp(&settings,&before,sizeof(settings))==0);
    settings.profiles[0].video_multiplier=5U;
    assert(mt_supervisor_settings_save(&settings).status==MT_SETTINGS_INVALID_ARGUMENT);
    settings=before;
    /* Schema 8 retains its original file size; preserve controller records and
     * all other preferences, and interpret its reserved profile byte as Auto. */
    settings.legacy_gamepad_pending=5U;
    assert(mt_supervisor_settings_save(&settings).status==MT_SETTINGS_OK);
    for(unsigned index=0U;index<MT_SETTINGS_MAX_PROFILES;++index)stored_file[56U+index*PROFILE_SIZE+251U]=0U;
    convert_stored_to_legacy(8U);
    assert(mt_supervisor_settings_load(&loaded).status==MT_SETTINGS_OK);
    settings.profiles[0].video_multiplier=settings.profiles[1].video_multiplier=0U;
    assert(memcmp(&loaded,&settings,sizeof(settings))==0 && loaded.legacy_gamepad_pending==5U);
    stored_file[56U+251U]=1U;update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status==MT_SETTINGS_CORRUPT);
    assert(mt_supervisor_settings_save(&settings).status==MT_SETTINGS_OK);
    stored_file[56U+251U]=5U;update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status==MT_SETTINGS_CORRUPT);
}

static void test_c64_expansion_settings(void)
{
    mt_supervisor_settings_t settings, loaded, before;
    static const uint8_t modes[] = {0U, 2U, 3U};
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "pal"));
    assert(settings.profiles[0].c64_sid_mode == 3U && settings.profiles[0].c64_reu_enabled);
    for (size_t mode = 0U; mode < sizeof(modes); ++mode) {
        for (unsigned enabled = 0U; enabled < 2U; ++enabled) {
            assert(mt_supervisor_settings_remember_c64_expansion(&settings, "c64", modes[mode], enabled != 0U));
            assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
            assert(stored_file[V9_WIRE_SIZE] == (modes[mode] | (enabled ? 8U : 0U)));
            assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
            assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
        }
    }
    before = settings;
    assert(!mt_supervisor_settings_remember_c64_expansion(NULL, "c64", 0U, false));
    assert(!mt_supervisor_settings_remember_c64_expansion(&settings, NULL, 0U, false));
    for (unsigned mode = 0U; mode <= 255U; ++mode) {
        if (mode == 0U || mode == 2U || mode == 3U) continue;
        assert(!mt_supervisor_settings_remember_c64_expansion(&settings, "c64", (uint8_t)mode, false));
        assert(memcmp(&before, &settings, sizeof(settings)) == 0);
    }
    for (unsigned config = 0U; config <= 255U; ++config) {
        if (config == 0U || config == 2U || config == 3U || config == 8U || config == 10U || config == 11U) continue;
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        stored_file[V9_WIRE_SIZE] = (uint8_t)config;
        update_stored_crc();
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    }
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    stored_file[V9_WIRE_SIZE + 15U] = 3U;
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    settings.profiles[0].c64_sid_mode = 1U;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_INVALID_ARGUMENT);
    settings = before;
    assert(mt_supervisor_settings_remember_c64_expansion(&settings, "c64", 0U, false));
    assert(mt_supervisor_settings_remember_video_multiplier(&settings, "c64", 4U));
    for (unsigned schema = 8U; schema <= 9U; ++schema) {
        settings.profiles[0].video_multiplier = schema == 9U ? 4U : 0U;
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        convert_stored_to_legacy(schema);
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        assert(loaded.profiles[0].c64_sid_mode == 3U && loaded.profiles[0].c64_reu_enabled);
        assert(loaded.profiles[0].video_multiplier == settings.profiles[0].video_multiplier);
        loaded.profiles[0].c64_sid_mode = 0U;
        loaded.profiles[0].c64_reu_enabled = false;
        assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
    }
}

static void test_gamepad_settings(void)
{
    mt_supervisor_settings_t settings, loaded;
    mt_gamepad_config_t defaults;
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    mt_gamepad_default_config(&defaults);
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        settings.gamepads[slot].axis[slot % 6U].source = MT_GAMEPAD_AXIS_OFF;
        settings.gamepads[slot].axis[0].invert = 1U;
        settings.gamepads[slot].axis[0].deadzone_percent = (uint8_t)(slot + 10U);
        settings.gamepads[slot].axis[0].sensitivity_percent = 175U;
        settings.gamepads[slot].button[slot % 8U] = 8U;
    }
    settings.display_mode = 5U;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
    /* Correct CRC does not excuse invalid mapping ranges. */
    stored_file[V5_WIRE_SIZE + 4U] = 1U;
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    convert_stored_to_legacy(5U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(loaded.display_mode == 0U);
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot)
        assert(memcmp(&loaded.gamepads[slot], &defaults, sizeof(defaults)) == 0);
    settings.gamepads[8].button[7] = 9U;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_INVALID_ARGUMENT);
}

static void test_genesis_settings(void)
{
    mt_supervisor_settings_t settings, loaded, before;
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings, "genesis", "ntsc"));
    assert(settings.profiles[0].genesis_three_button_mask == 0U);
    assert(mt_supervisor_settings_remember_genesis_pads(&settings, "genesis", 2U));
    settings.display_mode = 4U;
    settings.gamepads[8].button[0] = 5U;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(stored_file[V6_WIRE_SIZE] == 2U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
    before = settings;
    assert(!mt_supervisor_settings_remember_genesis_pads(&settings, "newcore", 4U));
    assert(memcmp(&before, &settings, sizeof(settings)) == 0);
    stored_file[V6_WIRE_SIZE] = 4U;
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    convert_stored_to_legacy(6U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(loaded.profiles[0].genesis_three_button_mask == 0U);
    assert(loaded.display_mode == 4U && loaded.gamepads[8].button[0] == 5U);
    assert(mt_supervisor_settings_save(&loaded).status == MT_SETTINGS_OK);
    assert(stored_file[8U] == 10U && stored_size == MT_SETTINGS_WIRE_SIZE);
}

static mt_controller_device_t controller_device(uint32_t port, uint32_t serial)
{
    mt_controller_device_t device = {0};
    device.identity.vendor_id = 0x045eU;
    device.identity.product_id = 0x028eU;
    device.identity.descriptor_hash = UINT32_C(0x12345678);
    device.identity.serial_hash = serial;
    device.identity.port_path = port;
    device.identity.transport = MT_CONTROLLER_TRANSPORT_XBOX360;
    device.connected = 1U;
    device.buttons_present = UINT32_MAX;
    device.axis_mask = 0x3fU;
    device.home_button = 10U;
    return device;
}

static void test_controller_profiles(void)
{
    enum { POOL_OFFSET = V7_WIRE_SIZE + 8U, RECORD_SIZE = 92U };
    mt_supervisor_settings_t settings, loaded, before, candidate;
    mt_controller_device_t first = controller_device(1U, 0U);
    mt_controller_device_t second = controller_device(2U, 0U);
    mt_controller_device_t serial = controller_device(3U, 1234U);
    mt_controller_config_t config, other;
    const mt_controller_settings_profile_t *profile;
    uint8_t valid_wire[MT_SETTINGS_WIRE_SIZE];
    static const struct { unsigned offset; uint8_t value; } bad[] = {
        {V7_WIRE_SIZE, MT_SETTINGS_MAX_CONTROLLER_PROFILES + 1U}, {V7_WIRE_SIZE + 5U, 2U},
        {V7_WIRE_SIZE + 6U, 1U}, {POOL_OFFSET + 20U, 255U},
        {POOL_OFFSET + 22U, 1U}, {POOL_OFFSET + 24U, MT_CONTROL_SOURCE_COUNT},
        {POOL_OFFSET + 88U, 4U}, {POOL_OFFSET + 89U, 1U},
        {POOL_OFFSET + 4U * RECORD_SIZE, 1U}
    };
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(settings.legacy_gamepad_pending == 0U);
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "pal"));
    assert(mt_supervisor_settings_remember_drive8(&settings, "c64", "keep.d64"));
    mt_controls_default(MT_GENESIS_CORE_ID, &first, &config);
    config.binding[MT_CONTROL_A] = 32U; /* Physical button 32 survives the wire. */
    config.binding[MT_CONTROL_MENU] = 11U;
    config.binding[MT_CONTROL_SCREENSHOT] = 19U;
    config.binding[MT_CONTROL_SCREENSHOT_OUTPUT] = 20U;
    config.assignment = MT_CONTROLLER_PORT2;
    config.axis[MT_GAMEPAD_AXIS_RX].invert = 1U;
    config.axis[MT_GAMEPAD_AXIS_RX].deadzone_percent = 23U;
    config.axis[MT_GAMEPAD_AXIS_RX].sensitivity_percent = 175U;
    assert(mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &first.identity, &config));
    assert(settings.controller_profile_count == 1U);
    assert(mt_supervisor_settings_find_controller(&settings,
        MT_GENESIS_CORE_ID, &second.identity) == NULL);
    other = config; other.binding[MT_CONTROL_A] = 7U;
    other.assignment = MT_CONTROLLER_PORT1;
    assert(mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &second.identity, &other));
    mt_controls_default(MT_GBC_CORE_ID, &first, &other);
    other.binding[MT_CONTROL_A] = 22U;
    assert(mt_supervisor_settings_remember_controller(&settings,
        MT_GBC_CORE_ID, &first.identity, &other));
    assert(mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &serial.identity, &config));
    serial.identity.port_path = 99U;
    profile = mt_supervisor_settings_find_controller(&settings,
        MT_GENESIS_CORE_ID, &serial.identity);
    assert(profile != NULL && profile->config.binding[MT_CONTROL_A] == 32U);
    assert(mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &serial.identity, &config));
    assert(settings.controller_profile_count == 4U);
    settings.legacy_gamepad_pending = 0x101U;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    memcpy(valid_wire, stored_file, sizeof(valid_wire));
    assert(stored_file[POOL_OFFSET + 4U] == 0x5eU &&
           stored_file[POOL_OFFSET + 5U] == 0x04U);
    assert(stored_file[POOL_OFFSET + 24U + MT_CONTROL_A * 2U] == 32U);
    assert(stored_file[POOL_OFFSET + 24U + MT_CONTROL_SCREENSHOT_OUTPUT * 2U] == 20U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
    profile = mt_supervisor_settings_find_controller(&loaded,
        MT_GENESIS_CORE_ID, &second.identity);
    assert(profile != NULL && profile->config.binding[MT_CONTROL_A] == 7U &&
           profile->config.assignment == MT_CONTROLLER_PORT1);
    profile = mt_supervisor_settings_find_controller(&loaded,
        MT_GBC_CORE_ID, &first.identity);
    assert(profile != NULL && profile->config.binding[MT_CONTROL_A] == 22U);
    assert(strcmp(mt_supervisor_settings_find(&loaded, "c64")->drive8_name,
                  "keep.d64") == 0);
    /* Schema 8 retains every identified-controller map during migration. */
    convert_stored_to_legacy(8U);
    assert(mt_supervisor_settings_load(&loaded).status==MT_SETTINGS_OK);
    assert(memcmp(&loaded,&settings,sizeof(settings))==0);
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);

    before = settings;
    other = config; other.assignment = 4U;
    assert(!mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &first.identity, &other));
    assert(!mt_supervisor_settings_remember_controller(&settings,
        0U, &first.identity, &config));
    assert(!mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, NULL, &config));
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
    candidate = settings;
    candidate.controller_profiles[0].config.binding[MT_CONTROL_A] = 15U;
    write_status = MT_SD_WRITE_FAILED;
    assert(mt_supervisor_settings_save(&candidate).status == MT_SETTINGS_WRITE_FAILED);
    assert(memcmp(stored_file, valid_wire, sizeof(valid_wire)) == 0);
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
    write_status = MT_SD_OK;

    for (unsigned index = 0U; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        memcpy(stored_file, valid_wire, sizeof(valid_wire));
        stored_file[bad[index].offset] = bad[index].value;
        update_stored_crc();
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
        assert(loaded.profile_count == 0U && loaded.controller_profile_count == 0U);
    }
    memcpy(stored_file, valid_wire, sizeof(valid_wire));
    memcpy(stored_file + POOL_OFFSET + RECORD_SIZE,
           stored_file + POOL_OFFSET, RECORD_SIZE);
    update_stored_crc();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_CORRUPT);
    candidate = settings;
    candidate.controller_profiles[1] = candidate.controller_profiles[0];
    assert(mt_supervisor_settings_save(&candidate).status == MT_SETTINGS_INVALID_ARGUMENT);

    /* The fixed pool rejects overflow without replacing another controller. */
    mt_supervisor_settings_defaults(&settings);
    for (unsigned index = 0U; index < MT_SETTINGS_MAX_CONTROLLER_PROFILES; ++index) {
        first.identity.port_path = index + 1U;
        assert(mt_supervisor_settings_remember_controller(&settings,
            MT_GENESIS_CORE_ID, &first.identity, &config));
    }
    before = settings;
    first.identity.port_path = 1000U;
    assert(!mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &first.identity, &config));
    assert(memcmp(&settings, &before, sizeof(settings)) == 0);
    first.identity.port_path = 1U;
    config.assignment = MT_CONTROLLER_OFF;
    assert(mt_supervisor_settings_remember_controller(&settings,
        MT_GENESIS_CORE_ID, &first.identity, &config));
    assert(settings.controller_profile_count == MT_SETTINGS_MAX_CONTROLLER_PROFILES);
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
}

static void test_extended_axis_profile_wire(void)
{
    enum { POOL_OFFSET = V7_WIRE_SIZE + 8U, RECORD_SIZE = 92U };
    mt_supervisor_settings_t settings, loaded;
    mt_controller_device_t pad = controller_device(1U, 0U);
    mt_controller_config_t config;
    uint8_t legacy_record[RECORD_SIZE];
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    mt_controls_default(MT_GENESIS_CORE_ID, &pad, &config);
    assert(mt_supervisor_settings_remember_controller(&settings, MT_GENESIS_CORE_ID, &pad.identity, &config));
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    memcpy(legacy_record, stored_file + POOL_OFFSET, sizeof(legacy_record));
    /* Literal pre-extension IDs and offsets verify the unchanged record. */
    assert(legacy_record[24] == 55U && legacy_record[26] == 56U &&
           legacy_record[28] == 57U && legacy_record[30] == 58U);
    assert(legacy_record[58] == 0U && legacy_record[60] == 10U &&
           legacy_record[61] == 100U && legacy_record[83] == 5U && legacy_record[88] == 0U);
    pad.identity.port_path = 2U; pad.generic_axes = 1U;
    mt_controls_default(MT_VECTREX_CORE_ID, &pad, &config);
    config.assignment = MT_CONTROLLER_PORT1;
    config.binding[MT_CONTROL_A] = 64U; /* Wheel + */
    config.binding[MT_CONTROL_B] = 65U; /* Pan - */
    config.binding[MT_CONTROL_AXIS_X] = 67U; /* Slider */
    config.binding[MT_CONTROL_AXIS_Y] = 70U; /* Pan */
    assert(mt_supervisor_settings_remember_controller(&settings, MT_VECTREX_CORE_ID, &pad.identity, &config));
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(memcmp(legacy_record, stored_file + POOL_OFFSET, sizeof(legacy_record)) == 0);
    const uint8_t *record = stored_file + POOL_OFFSET + RECORD_SIZE;
    assert(record[32] == 64U && record[34] == 65U && record[48] == 67U && record[50] == 70U);
    assert(record[88] == 1U && record[89] == 0U);
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(memcmp(&loaded, &settings, sizeof(settings)) == 0);
}

static void assert_schema10_downgrade(void)
{
    uint8_t expected[MT_SETTINGS_WIRE_SIZE];
    uint8_t decoded[MT_SETTINGS_WIRE_SIZE];
    memcpy(expected, stored_file, sizeof(expected));
    assert(mt_schema10_roundtrip(stored_file, decoded));
    /* The old firmware drops only the newly allocated global bytes. Every
     * legacy field, including controller records and profile 243, survives. */
    expected[26] = expected[27] = 0;
    const uint32_t crc = crc32_bytes(expected + 24, sizeof(expected) - 24);
    store_u32_le(expected + 16, crc);
    store_u32_le(expected + 20, ~crc);
    assert(memcmp(expected, decoded, sizeof(expected)) == 0);
    expected[100] ^= 1;
    assert(!mt_schema10_roundtrip(expected, decoded));
}

static void test_c64_turbo_storage(void)
{
    mt_supervisor_settings_t settings, loaded;
    reset_fake();
    mt_supervisor_settings_defaults(&settings);
    assert(mt_supervisor_settings_remember_core(&settings, "c64", "pal"));
    for (unsigned detail = 0; detail < 256; ++detail) {
        settings.c64_turbo_detail = (uint8_t)detail;
        settings.c64_turbo_flags = (uint8_t)(detail & 3U);
        settings.c64_ultimate_audio = (detail & 4U) != 0;
        assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
        assert(stored_file[26] == (detail & 7U) && stored_file[27] == detail);
        /* Future flags remain downgrade-safe rather than rejecting settings. */
        stored_file[26] |= 0xF8U; update_stored_crc();
        assert_schema10_downgrade();
        assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
        assert(loaded.c64_turbo_detail == detail && loaded.c64_turbo_flags == (detail & 3U));
        assert(loaded.c64_ultimate_audio == settings.c64_ultimate_audio);
        assert(loaded.profiles[0].c64_reu_enabled);
    }
    const mt_controller_device_t pad = controller_device(1U, 43U);
    settings.controller_profile_count = 1;
    settings.controller_profiles[0].core_id = MT_C64_CORE_ID;
    settings.controller_profiles[0].identity = pad.identity;
    mt_controls_default(MT_C64_CORE_ID, &pad, &settings.controller_profiles[0].config);
    settings.controller_profiles[0].config.extra_binding[0] = 23;
    settings.controller_profiles[0].config.extra_binding[1] = 24;
    settings.controller_profiles[0].config.extra_binding[2] = 25;
    assert(mt_supervisor_settings_save(&settings).status == MT_SETTINGS_OK);
    assert(mt_supervisor_turbo_controls_save(&settings));
    assert_schema10_downgrade();
    assert(mt_supervisor_settings_load(&loaded).status == MT_SETTINGS_OK);
    assert(loaded.controller_profiles[0].config.extra_binding[0] == 0);
    assert(mt_supervisor_turbo_controls_load(&loaded));
    assert(memcmp(loaded.controller_profiles[0].config.extra_binding,
        settings.controller_profiles[0].config.extra_binding, 6) == 0);
    loaded.controller_profiles[0].identity.port_path ^= 1U;
    /* Serial identity follows the controller across physical USB ports. */
    assert(mt_supervisor_turbo_controls_load(&loaded));
    assert(loaded.controller_profiles[0].config.extra_binding[0] == 23);
    loaded.controller_profiles[0].identity.serial_hash ^= 1U;
    assert(mt_supervisor_turbo_controls_load(&loaded));
    assert(loaded.controller_profiles[0].config.extra_binding[0] == 0);
    loaded = settings;
    for (size_t i = 0; i < sizeof turbo_file; ++i) {
        turbo_file[i] ^= 1U;
        assert(!mt_supervisor_turbo_controls_load(&loaded));
        assert(loaded.controller_profiles[0].config.extra_binding[0] == 0);
        turbo_file[i] ^= 1U;
    }
    turbo_exists = false;
    assert(mt_supervisor_turbo_controls_load(&loaded));
    assert(loaded.controller_profiles[0].config.extra_binding[1] == 0);
}

int main(void)
{
    test_extended_axis_profile_wire();
    test_controller_profiles();
    test_gamepad_settings();
    test_genesis_settings();
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
    test_video_effects_settings();
    test_video_multiplier();
    test_c64_expansion_settings();
    test_c64_turbo_storage();
    return 0;
}
