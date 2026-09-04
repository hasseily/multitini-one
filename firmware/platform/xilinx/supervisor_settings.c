/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"

#include <string.h>

#define MT_SETTINGS_SCHEMA_V1 UINT32_C(1)
#define MT_SETTINGS_SCHEMA UINT32_C(2)
#define MT_SETTINGS_HEADER_SIZE 24U
#define MT_SETTINGS_GLOBAL_SIZE 32U
#define MT_SETTINGS_PROFILE_SIZE 244U

#if MT_SETTINGS_HEADER_SIZE + MT_SETTINGS_GLOBAL_SIZE + \
    MT_SETTINGS_MAX_PROFILES * MT_SETTINGS_PROFILE_SIZE != \
    MT_SETTINGS_WIRE_SIZE
#error Supervisor settings wire-size constants disagree
#endif

static const uint8_t settings_magic[8] = {
    'M', 'T', 'S', 'E', 'T', '0', '0', '1'
};
static uint8_t settings_wire[MT_SETTINGS_WIRE_SIZE];
static mt_supervisor_settings_t settings_decode_candidate;

static mt_sd_result_t empty_sd_result(void)
{
    mt_sd_result_t result = {MT_SD_OK, 0U, 0U};
    return result;
}

static mt_settings_result_t settings_result(mt_settings_status_t status,
                                            mt_sd_result_t sd)
{
    mt_settings_result_t result;
    result.status = status;
    result.sd = sd;
    return result;
}

static uint32_t load_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
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
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return ~crc;
}

static bool valid_id(const char *text, size_t capacity, bool allow_empty)
{
    size_t index;
    if (text == NULL) return false;
    if (text[0] == '\0') return allow_empty;
    for (index = 0U; index < capacity; ++index) {
        const char value = text[index];
        if (value == '\0') return true;
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '-' ||
              value == '_')) {
            return false;
        }
    }
    return false;
}

static bool valid_file_name(const char *text)
{
    size_t index;
    if (text == NULL) return false;
    if (text[0] == '\0') return true;
    if (strcmp(text, ".") == 0 || strcmp(text, "..") == 0) return false;
    for (index = 0U; index < MT_SETTINGS_MEDIA_NAME_MAX; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value == '\0') return true;
        if (value < 0x20U || value == 0x7fU || value == '/' ||
            value == '\\' || value == ':') {
            return false;
        }
    }
    return false;
}

static bool copy_string(char *destination, size_t capacity,
                        const char *source)
{
    const size_t length = source == NULL ? 0U : strlen(source);
    if (destination == NULL || length >= capacity) return false;
    if (length != 0U) memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

static mt_core_settings_profile_t *find_profile(
    mt_supervisor_settings_t *settings, const char *core_id, bool create)
{
    size_t index;
    if (settings == NULL || !valid_id(core_id, MT_CORE_PACKAGE_ID_MAX,
                                      false)) {
        return NULL;
    }
    for (index = 0U; index < settings->profile_count; ++index) {
        if (strcmp(settings->profiles[index].core_id, core_id) == 0)
            return &settings->profiles[index];
    }
    if (!create || settings->profile_count >= MT_SETTINGS_MAX_PROFILES)
        return NULL;
    memset(&settings->profiles[settings->profile_count], 0,
           sizeof(settings->profiles[settings->profile_count]));
    (void)copy_string(settings->profiles[settings->profile_count].core_id,
                      MT_CORE_PACKAGE_ID_MAX, core_id);
    settings->profiles[settings->profile_count].symbolic_keyboard = true;
    settings->profiles[settings->profile_count].joystick_port = 2U;
    return &settings->profiles[settings->profile_count++];
}

void mt_supervisor_settings_defaults(mt_supervisor_settings_t *settings)
{
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
}

const mt_core_settings_profile_t *mt_supervisor_settings_find(
    const mt_supervisor_settings_t *settings, const char *core_id)
{
    size_t index;
    if (settings == NULL ||
        !valid_id(core_id, MT_CORE_PACKAGE_ID_MAX, false)) {
        return NULL;
    }
    for (index = 0U; index < settings->profile_count; ++index) {
        if (strcmp(settings->profiles[index].core_id, core_id) == 0)
            return &settings->profiles[index];
    }
    return NULL;
}

bool mt_supervisor_settings_remember_variant(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *variant_id)
{
    mt_core_settings_profile_t *profile;
    if (!valid_id(variant_id, MT_CORE_VARIANT_ID_MAX, false)) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL ||
        !copy_string(profile->variant_id, sizeof(profile->variant_id),
                     variant_id)) {
        return false;
    }
    return true;
}

bool mt_supervisor_settings_remember_core(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *variant_id)
{
    return mt_supervisor_settings_remember_variant(
               settings, core_id, variant_id) &&
           copy_string(settings->selected_core_id,
                       sizeof(settings->selected_core_id), core_id);
}

bool mt_supervisor_settings_remember_drive8(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *file_name)
{
    mt_core_settings_profile_t *profile;
    const char *const name = file_name == NULL ? "" : file_name;
    if (!valid_file_name(name)) return false;
    profile = find_profile(settings, core_id, true);
    return profile != NULL &&
           copy_string(profile->drive8_name, sizeof(profile->drive8_name),
                       name);
}

bool mt_supervisor_settings_remember_cartridge(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *file_name)
{
    mt_core_settings_profile_t *profile;
    const char *const name = file_name == NULL ? "" : file_name;
    if (!valid_file_name(name)) return false;
    profile = find_profile(settings, core_id, true);
    return profile != NULL && copy_string(
        profile->cartridge_name, sizeof(profile->cartridge_name), name);
}

bool mt_supervisor_settings_remember_c64_input(
    mt_supervisor_settings_t *settings, const char *core_id,
    bool symbolic_keyboard, uint8_t joystick_port)
{
    mt_core_settings_profile_t *profile;
    if (joystick_port != 1U && joystick_port != 2U) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->symbolic_keyboard = symbolic_keyboard;
    profile->joystick_port = joystick_port;
    return true;
}

bool mt_supervisor_settings_remember_c64_fast_roms(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled)
{
    mt_core_settings_profile_t *const profile =
        find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->c64_fast_roms = enabled;
    return true;
}

bool mt_supervisor_settings_remember_c64_cpu_turbo(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled)
{
    mt_core_settings_profile_t *const profile =
        find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->c64_cpu_turbo = enabled;
    return true;
}

static bool settings_valid(const mt_supervisor_settings_t *settings)
{
    size_t index;
    if (settings == NULL ||
        settings->profile_count > MT_SETTINGS_MAX_PROFILES ||
        !valid_id(settings->selected_core_id, MT_CORE_PACKAGE_ID_MAX, true)) {
        return false;
    }
    if (settings->auto_boot && settings->selected_core_id[0] == '\0')
        return false;
    for (index = 0U; index < settings->profile_count; ++index) {
        const mt_core_settings_profile_t *const profile =
            &settings->profiles[index];
        size_t previous;
        if (!valid_id(profile->core_id, sizeof(profile->core_id), false) ||
            !valid_id(profile->variant_id, sizeof(profile->variant_id),
                      false) ||
            !valid_file_name(profile->drive8_name) ||
            !valid_file_name(profile->cartridge_name) ||
            (profile->joystick_port != 1U && profile->joystick_port != 2U)) {
            return false;
        }
        for (previous = 0U; previous < index; ++previous) {
            if (strcmp(settings->profiles[previous].core_id,
                       profile->core_id) == 0) {
                return false;
            }
        }
    }
    return settings->selected_core_id[0] == '\0' ||
           mt_supervisor_settings_find(settings,
                                       settings->selected_core_id) != NULL;
}

static bool encode_settings(const mt_supervisor_settings_t *settings)
{
    size_t index;
    uint32_t crc;
    if (!settings_valid(settings)) return false;
    memset(settings_wire, 0, sizeof(settings_wire));
    memcpy(settings_wire, settings_magic, sizeof(settings_magic));
    store_u32_le(settings_wire + 8U, MT_SETTINGS_SCHEMA);
    store_u32_le(settings_wire + 12U, MT_SETTINGS_WIRE_SIZE);
    settings_wire[24U] = settings->auto_boot ? 1U : 0U;
    memcpy(settings_wire + 28U, settings->selected_core_id,
           strlen(settings->selected_core_id));
    store_u32_le(settings_wire + 52U, (uint32_t)settings->profile_count);
    for (index = 0U; index < settings->profile_count; ++index) {
        const mt_core_settings_profile_t *const profile =
            &settings->profiles[index];
        uint8_t *const output = settings_wire + MT_SETTINGS_HEADER_SIZE +
            MT_SETTINGS_GLOBAL_SIZE + index * MT_SETTINGS_PROFILE_SIZE;
        memcpy(output, profile->core_id, strlen(profile->core_id));
        memcpy(output + 24U, profile->variant_id,
               strlen(profile->variant_id));
        memcpy(output + 48U, profile->drive8_name,
               strlen(profile->drive8_name));
        memcpy(output + 144U, profile->cartridge_name,
               strlen(profile->cartridge_name));
        output[240U] = profile->symbolic_keyboard ? 1U : 0U;
        output[241U] = profile->joystick_port;
        output[242U] = profile->c64_fast_roms ? 1U : 0U;
        output[243U] = profile->c64_cpu_turbo ? 1U : 0U;
    }
    crc = crc32_bytes(settings_wire + MT_SETTINGS_HEADER_SIZE,
                      MT_SETTINGS_WIRE_SIZE - MT_SETTINGS_HEADER_SIZE);
    store_u32_le(settings_wire + 16U, crc);
    store_u32_le(settings_wire + 20U, ~crc);
    return true;
}

static bool wire_string(char *destination, size_t capacity,
                        const uint8_t *source)
{
    size_t length = 0U;
    while (length < capacity && source[length] != 0U) ++length;
    if (length == capacity) return false;
    memcpy(destination, source, capacity);
    destination[capacity - 1U] = '\0';
    return true;
}

static bool decode_settings(mt_supervisor_settings_t *settings)
{
    const uint32_t crc = load_u32_le(settings_wire + 16U);
    const uint32_t schema = load_u32_le(settings_wire + 8U);
    const uint32_t profile_count = load_u32_le(settings_wire + 52U);
    size_t index;
    if (memcmp(settings_wire, settings_magic, sizeof(settings_magic)) != 0 ||
        (schema != MT_SETTINGS_SCHEMA_V1 && schema != MT_SETTINGS_SCHEMA) ||
        load_u32_le(settings_wire + 12U) != MT_SETTINGS_WIRE_SIZE ||
        load_u32_le(settings_wire + 20U) != ~crc ||
        crc32_bytes(settings_wire + MT_SETTINGS_HEADER_SIZE,
                    MT_SETTINGS_WIRE_SIZE - MT_SETTINGS_HEADER_SIZE) != crc ||
        settings_wire[24U] > 1U || profile_count > MT_SETTINGS_MAX_PROFILES) {
        return false;
    }
    mt_supervisor_settings_defaults(&settings_decode_candidate);
    settings_decode_candidate.auto_boot = settings_wire[24U] != 0U;
    if (!wire_string(settings_decode_candidate.selected_core_id,
                     sizeof(settings_decode_candidate.selected_core_id),
                     settings_wire + 28U)) {
        return false;
    }
    settings_decode_candidate.profile_count = (size_t)profile_count;
    for (index = 0U; index < settings_decode_candidate.profile_count;
         ++index) {
        mt_core_settings_profile_t *const profile =
            &settings_decode_candidate.profiles[index];
        const uint8_t *const input = settings_wire + MT_SETTINGS_HEADER_SIZE +
            MT_SETTINGS_GLOBAL_SIZE + index * MT_SETTINGS_PROFILE_SIZE;
        if (!wire_string(profile->core_id, sizeof(profile->core_id), input) ||
            !wire_string(profile->variant_id, sizeof(profile->variant_id),
                         input + 24U) ||
            !wire_string(profile->drive8_name,
                         sizeof(profile->drive8_name), input + 48U) ||
            !wire_string(profile->cartridge_name,
                         sizeof(profile->cartridge_name), input + 144U) ||
            input[240U] > 1U || input[242U] > 1U || input[243U] > 1U) {
            return false;
        }
        profile->symbolic_keyboard = input[240U] != 0U;
        profile->joystick_port = input[241U];
        /* Schema 1 byte 242 meant the removed whole-drive warp. Never migrate
         * an old true value into the unrelated fast-ROM selection. */
        profile->c64_fast_roms = schema == MT_SETTINGS_SCHEMA &&
                                 input[242U] != 0U;
        profile->c64_cpu_turbo = input[243U] != 0U;
    }
    if (!settings_valid(&settings_decode_candidate)) return false;
    *settings = settings_decode_candidate;
    return true;
}

mt_settings_result_t mt_supervisor_settings_load(
    mt_supervisor_settings_t *settings)
{
    mt_sd_result_t sd;
    bool found = false;
    if (settings == NULL) {
        return settings_result(MT_SETTINGS_INVALID_ARGUMENT,
                               empty_sd_result());
    }
    mt_supervisor_settings_defaults(settings);
    sd = mt_sd_storage_journal_recover(
        MT_SETTINGS_PATH, MT_SETTINGS_JOURNAL_PATH, settings_wire,
        sizeof(settings_wire));
    if (sd.status != MT_SD_OK)
        return settings_result(MT_SETTINGS_RECOVERY_FAILED, sd);
    sd = mt_sd_storage_read_file_optional_exact(
        MT_SETTINGS_PATH, settings_wire, sizeof(settings_wire), &found);
    if (sd.status != MT_SD_OK)
        return settings_result(MT_SETTINGS_READ_FAILED, sd);
    if (!found) return settings_result(MT_SETTINGS_MISSING, sd);
    if (!decode_settings(settings)) {
        mt_supervisor_settings_defaults(settings);
        return settings_result(MT_SETTINGS_CORRUPT, sd);
    }
    return settings_result(MT_SETTINGS_OK, sd);
}

mt_settings_result_t mt_supervisor_settings_save(
    const mt_supervisor_settings_t *settings)
{
    mt_sd_result_t sd;
    if (!encode_settings(settings)) {
        return settings_result(MT_SETTINGS_INVALID_ARGUMENT,
                               empty_sd_result());
    }
    sd = mt_sd_storage_journal_replace(
        MT_SETTINGS_PATH, MT_SETTINGS_JOURNAL_PATH, settings_wire,
        sizeof(settings_wire));
    return settings_result(sd.status == MT_SD_OK ? MT_SETTINGS_OK :
                           MT_SETTINGS_WRITE_FAILED, sd);
}

const char *mt_settings_status_name(mt_settings_status_t status)
{
    switch (status) {
    case MT_SETTINGS_OK: return "ok";
    case MT_SETTINGS_MISSING: return "missing";
    case MT_SETTINGS_INVALID_ARGUMENT: return "invalid argument";
    case MT_SETTINGS_RECOVERY_FAILED: return "recovery failed";
    case MT_SETTINGS_READ_FAILED: return "read failed";
    case MT_SETTINGS_CORRUPT: return "corrupt";
    case MT_SETTINGS_WRITE_FAILED: return "write failed";
    }
    return "unknown settings status";
}
