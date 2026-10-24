/* SPDX-License-Identifier: GPL-3.0-only */
#include "supervisor_settings.h"

#include <string.h>

#define MT_SETTINGS_SCHEMA_V1 UINT32_C(1)
#define MT_SETTINGS_SCHEMA_V2 UINT32_C(2)
#define MT_SETTINGS_SCHEMA_V3 UINT32_C(3)
#define MT_SETTINGS_SCHEMA_V4 UINT32_C(4)
#define MT_SETTINGS_SCHEMA_V5 UINT32_C(5)
#define MT_SETTINGS_SCHEMA_V6 UINT32_C(6)
#define MT_SETTINGS_SCHEMA_V7 UINT32_C(7)
#define MT_SETTINGS_SCHEMA_V8 UINT32_C(8)
#define MT_SETTINGS_SCHEMA_V9 UINT32_C(9)
#define MT_SETTINGS_SCHEMA UINT32_C(10)
#define MT_SETTINGS_V9_WIRE_SIZE 10950U
#define MT_SETTINGS_V7_WIRE_SIZE 5054U
#define MT_SETTINGS_V6_WIRE_SIZE 5038U
#define MT_SETTINGS_V5_WIRE_SIZE 4696U
#define MT_SETTINGS_PAD_SIZE 38U
#define MT_SETTINGS_HEADER_SIZE 24U
#define MT_SETTINGS_GLOBAL_SIZE 32U
#define MT_SETTINGS_LEGACY_PROFILE_SIZE 244U
#define MT_SETTINGS_LEGACY_WIRE_SIZE 3960U
#define MT_SETTINGS_V3_PROFILE_SIZE 248U
#define MT_SETTINGS_V3_WIRE_SIZE 4024U
#define MT_SETTINGS_V4_PROFILE_SIZE 252U
#define MT_SETTINGS_V4_WIRE_SIZE 4088U
#define MT_SETTINGS_PROFILE_SIZE 290U
#define MT_SETTINGS_EFFECTS_OFFSET 252U
#define MT_SETTINGS_CONTROLLERS_HEADER_SIZE 8U
#define MT_SETTINGS_CONTROLLER_SIZE 92U
#define MT_SETTINGS_CONTROLLER_AXES_OFFSET (24U + MT_CONTROL_COUNT * 2U)
#define MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET (MT_SETTINGS_CONTROLLER_AXES_OFFSET + MT_GAMEPAD_AXIS_COUNT * 5U)
#define MT_SETTINGS_LEGACY_PAD_MASK ((1U << MT_GAMEPAD_SLOTS) - 1U)

_Static_assert(MT_SETTINGS_EFFECTS_OFFSET + MT_VIDEO_EFFECT_COUNT * 2U ==
               MT_SETTINGS_PROFILE_SIZE, "Video controls require a settings schema update");
_Static_assert(MT_CONTROL_COUNT == 17U && MT_GAMEPAD_AXIS_COUNT == 6U,
               "Controller controls require a settings schema update");
_Static_assert(MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET + 4U == MT_SETTINGS_CONTROLLER_SIZE,
               "Controller wire-size constants disagree");

#if MT_SETTINGS_HEADER_SIZE + MT_SETTINGS_GLOBAL_SIZE + \
    MT_SETTINGS_MAX_PROFILES * MT_SETTINGS_PROFILE_SIZE + MT_GAMEPAD_SLOTS * MT_SETTINGS_PAD_SIZE + \
    MT_SETTINGS_MAX_PROFILES + MT_SETTINGS_CONTROLLERS_HEADER_SIZE + \
    MT_SETTINGS_MAX_CONTROLLER_PROFILES * MT_SETTINGS_CONTROLLER_SIZE + MT_SETTINGS_MAX_PROFILES != \
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

static uint16_t load_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static void store_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
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

static bool valid_cartridge_name(const char *core_id, const char *text)
{
    size_t index;
    size_t component_start = 0U;
    if (core_id == NULL || strcmp(core_id, "vectrex") != 0)
        return valid_file_name(text);
    if (text == NULL) return false;
    if (text[0] == '\0') return true;
    for (index = 0U; index < MT_SETTINGS_MEDIA_NAME_MAX; ++index) {
        const unsigned char value = (unsigned char)text[index];
        if (value == '\0' || value == '/') {
            if (index == component_start || text[index - 1U] == '.' ||
                text[index - 1U] == ' ') {
                return false;
            }
            if (value == '\0') return true;
            component_start = index + 1U;
        } else if (value < 0x20U || value == 0x7fU || value == '\\' ||
                   value == ':' || value == '*' || value == '?' ||
                   value == '"' || value == '<' || value == '>' ||
                   value == '|') {
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
    if (settings == NULL || settings->profile_count > MT_SETTINGS_MAX_PROFILES ||
        !valid_id(core_id, MT_CORE_PACKAGE_ID_MAX,
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
    settings->profiles[settings->profile_count].c64_sid_mode = MT_SETTINGS_C64_SID_MODE_DEFAULT;
    settings->profiles[settings->profile_count].c64_reu_enabled = MT_SETTINGS_C64_REU_DEFAULT;
    settings->profiles[settings->profile_count].border_style =
        MT_SETTINGS_BORDER_STYLE_DEFAULT;
    settings->profiles[settings->profile_count].border_color =
        MT_SETTINGS_BORDER_COLOR_DEFAULT;
    settings->profiles[settings->profile_count].border_padding =
        MT_SETTINGS_BORDER_PADDING_DEFAULT;
    settings->profiles[settings->profile_count].vectrex_overlay_enabled = true;
    settings->profiles[settings->profile_count].vectrex_beam_gain =
        MT_SETTINGS_VECTREX_BEAM_GAIN_DEFAULT;
    settings->profiles[settings->profile_count].vectrex_overlay_opacity =
        MT_SETTINGS_VECTREX_OVERLAY_OPACITY_DEFAULT;
    mt_video_effects_defaults(&settings->profiles[settings->profile_count].video_effects);
    return &settings->profiles[settings->profile_count++];
}

void mt_supervisor_settings_defaults(mt_supervisor_settings_t *settings)
{
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot)
        mt_gamepad_default_config(&settings->gamepads[slot]);
}

const mt_core_settings_profile_t *mt_supervisor_settings_find(
    const mt_supervisor_settings_t *settings, const char *core_id)
{
    size_t index;
    if (settings == NULL || settings->profile_count > MT_SETTINGS_MAX_PROFILES ||
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
    if (!valid_cartridge_name(core_id, name)) return false;
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

static bool c64_sid_mode_valid(uint8_t mode)
{
    return mode == 0U || mode == 2U || mode == 3U;
}

bool mt_supervisor_settings_remember_c64_expansion(
    mt_supervisor_settings_t *settings, const char *core_id,
    uint8_t sid_mode, bool reu_enabled)
{
    mt_core_settings_profile_t *profile;
    if (!c64_sid_mode_valid(sid_mode)) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->c64_sid_mode = sid_mode;
    profile->c64_reu_enabled = reu_enabled;
    return true;
}

bool mt_supervisor_settings_remember_genesis_pads(
    mt_supervisor_settings_t *settings, const char *core_id, uint8_t three_button_mask)
{
    mt_core_settings_profile_t *profile;
    if (three_button_mask > 3U) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->genesis_three_button_mask = three_button_mask;
    return true;
}

static bool borders_valid(uint8_t style, uint8_t color, uint8_t padding)
{
    return style <= 4U && color <= 15U && padding >= 1U && padding <= 20U;
}

bool mt_supervisor_settings_remember_borders(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled,
    uint8_t style, uint8_t color, uint8_t padding)
{
    mt_core_settings_profile_t *profile;
    if (!borders_valid(style, color, padding)) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->border_enabled = enabled;
    profile->border_style = style;
    profile->border_color = color;
    profile->border_padding = padding;
    return true;
}

bool mt_supervisor_settings_remember_vectrex_video(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled,
    uint8_t beam_gain, uint8_t overlay_opacity)
{
    mt_core_settings_profile_t *profile;
    if (core_id == NULL || strcmp(core_id, "vectrex") != 0 ||
        beam_gain > 3U || overlay_opacity > 3U) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->vectrex_overlay_enabled = enabled;
    profile->vectrex_beam_gain = beam_gain;
    profile->vectrex_overlay_opacity = overlay_opacity;
    return true;
}

bool mt_supervisor_settings_remember_video_effects(
    mt_supervisor_settings_t *settings, const char *core_id,
    const mt_video_effects_settings_t *effects)
{
    mt_core_settings_profile_t *profile;
    if (!mt_video_effects_settings_valid(effects)) return false;
    profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->video_effects = *effects;
    return true;
}

bool mt_supervisor_settings_remember_video_multiplier(
    mt_supervisor_settings_t *settings, const char *core_id, uint8_t multiplier)
{
    if (multiplier > 4U) return false;
    mt_core_settings_profile_t *const profile = find_profile(settings, core_id, true);
    if (profile == NULL) return false;
    profile->video_multiplier = multiplier;
    return true;
}

const mt_controller_settings_profile_t *mt_supervisor_settings_find_controller(
    const mt_supervisor_settings_t *settings, uint32_t core_id,
    const mt_controller_identity_t *identity)
{
    if (settings == NULL || identity == NULL || mt_controls_action_mask(core_id) == 0U ||
        settings->controller_profile_count > MT_SETTINGS_MAX_CONTROLLER_PROFILES)
        return NULL;
    for (size_t index = 0U; index < settings->controller_profile_count; ++index) {
        const mt_controller_settings_profile_t *profile =
            &settings->controller_profiles[index];
        if (profile->core_id == core_id &&
            mt_controls_identity_equal(&profile->identity, identity)) return profile;
    }
    return NULL;
}

bool mt_supervisor_settings_remember_controller(
    mt_supervisor_settings_t *settings, uint32_t core_id,
    const mt_controller_identity_t *identity,
    const mt_controller_config_t *config)
{
    const mt_controller_settings_profile_t *found;
    size_t index;
    if (settings == NULL || identity == NULL || mt_controls_action_mask(core_id) == 0U ||
        settings->controller_profile_count > MT_SETTINGS_MAX_CONTROLLER_PROFILES ||
        identity->transport > MT_CONTROLLER_TRANSPORT_PS4 ||
        !mt_controls_config_valid(core_id, config)) return false;
    found = mt_supervisor_settings_find_controller(settings, core_id, identity);
    index = found == NULL ? settings->controller_profile_count :
        (size_t)(found - settings->controller_profiles);
    if (index == MT_SETTINGS_MAX_CONTROLLER_PROFILES) return false;
    settings->controller_profiles[index].core_id = core_id;
    settings->controller_profiles[index].identity = *identity;
    settings->controller_profiles[index].config = *config;
    if (found == NULL) ++settings->controller_profile_count;
    return true;
}

static bool settings_valid(const mt_supervisor_settings_t *settings)
{
    size_t index;
    if (settings == NULL ||
        settings->profile_count > MT_SETTINGS_MAX_PROFILES ||
        settings->controller_profile_count > MT_SETTINGS_MAX_CONTROLLER_PROFILES ||
        (settings->legacy_gamepad_pending & ~MT_SETTINGS_LEGACY_PAD_MASK) != 0U ||
        !valid_id(settings->selected_core_id, MT_CORE_PACKAGE_ID_MAX, true)) {
        return false;
    }
    if (settings->display_mode >= 6U) return false;
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot)
        if (!mt_gamepad_config_valid(&settings->gamepads[slot])) return false;
    for (index = 0U; index < settings->controller_profile_count; ++index) {
        const mt_controller_settings_profile_t *profile =
            &settings->controller_profiles[index];
        if (mt_controls_action_mask(profile->core_id) == 0U ||
            profile->identity.transport > MT_CONTROLLER_TRANSPORT_PS4 ||
            !mt_controls_config_valid(profile->core_id, &profile->config)) return false;
        for (size_t previous = 0U; previous < index; ++previous) {
            const mt_controller_settings_profile_t *other =
                &settings->controller_profiles[previous];
            if (other->core_id == profile->core_id &&
                mt_controls_identity_equal(&other->identity, &profile->identity))
                return false;
        }
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
            !valid_cartridge_name(profile->core_id, profile->cartridge_name) ||
            !borders_valid(profile->border_style, profile->border_color,
                            profile->border_padding) ||
            profile->vectrex_beam_gain > 3U || profile->vectrex_overlay_opacity > 3U ||
            profile->genesis_three_button_mask > 3U || profile->video_multiplier > 4U ||
            !c64_sid_mode_valid(profile->c64_sid_mode) ||
            !mt_video_effects_settings_valid(&profile->video_effects) ||
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

static void encode_controller(uint8_t *output,
                              const mt_controller_settings_profile_t *profile)
{
    const mt_controller_identity_t *identity = &profile->identity;
    const mt_controller_config_t *config = &profile->config;
    store_u32_le(output, profile->core_id);
    store_u16_le(output + 4U, identity->vendor_id);
    store_u16_le(output + 6U, identity->product_id);
    store_u32_le(output + 8U, identity->descriptor_hash);
    store_u32_le(output + 12U, identity->serial_hash);
    store_u32_le(output + 16U, identity->port_path);
    output[20U] = identity->transport;
    output[21U] = identity->interface_id;
    for (unsigned action = 0U; action < MT_CONTROL_COUNT; ++action)
        store_u16_le(output + 24U + action * 2U, config->binding[action]);
    for (unsigned axis = 0U; axis < MT_GAMEPAD_AXIS_COUNT; ++axis) {
        const mt_gamepad_axis_config_t *value = &config->axis[axis];
        const size_t offset = MT_SETTINGS_CONTROLLER_AXES_OFFSET + axis * 5U;
        output[offset] = value->source;
        output[offset + 1U] = value->invert;
        output[offset + 2U] = value->deadzone_percent;
        store_u16_le(output + offset + 3U, value->sensitivity_percent);
    }
    output[MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET] = config->assignment;
}

static bool decode_controller(mt_controller_settings_profile_t *profile,
                              const uint8_t *input)
{
    mt_controller_identity_t *identity = &profile->identity;
    mt_controller_config_t *config = &profile->config;
    if (input[22U] != 0U || input[23U] != 0U ||
        input[MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET + 1U] != 0U ||
        input[MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET + 2U] != 0U ||
        input[MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET + 3U] != 0U) return false;
    profile->core_id = load_u32_le(input);
    identity->vendor_id = load_u16_le(input + 4U);
    identity->product_id = load_u16_le(input + 6U);
    identity->descriptor_hash = load_u32_le(input + 8U);
    identity->serial_hash = load_u32_le(input + 12U);
    identity->port_path = load_u32_le(input + 16U);
    identity->transport = input[20U];
    identity->interface_id = input[21U];
    for (unsigned action = 0U; action < MT_CONTROL_COUNT; ++action)
        config->binding[action] = load_u16_le(input + 24U + action * 2U);
    for (unsigned axis = 0U; axis < MT_GAMEPAD_AXIS_COUNT; ++axis) {
        mt_gamepad_axis_config_t *value = &config->axis[axis];
        const size_t offset = MT_SETTINGS_CONTROLLER_AXES_OFFSET + axis * 5U;
        value->source = input[offset];
        value->invert = input[offset + 1U];
        value->deadzone_percent = input[offset + 2U];
        value->sensitivity_percent = load_u16_le(input + offset + 3U);
    }
    config->assignment = input[MT_SETTINGS_CONTROLLER_ASSIGNMENT_OFFSET];
    return true;
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
    settings_wire[25U] = settings->display_mode;
    memcpy(settings_wire + 28U, settings->selected_core_id,
           strlen(settings->selected_core_id));
    store_u32_le(settings_wire + 52U, (uint32_t)settings->profile_count);
    for (index = 0U; index < settings->profile_count; ++index) {
        const mt_core_settings_profile_t *const profile =
            &settings->profiles[index];
        uint8_t *const output = settings_wire + MT_SETTINGS_HEADER_SIZE +
            MT_SETTINGS_GLOBAL_SIZE + index * MT_SETTINGS_PROFILE_SIZE;
        unsigned effect;
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
        output[244U] = profile->border_enabled ? 1U : 0U;
        output[245U] = profile->border_style;
        output[246U] = profile->border_color;
        output[247U] = profile->border_padding;
        output[248U] = profile->vectrex_overlay_enabled ? 1U : 0U;
        output[249U] = profile->vectrex_beam_gain;
        output[250U] = profile->vectrex_overlay_opacity;
        output[251U] = profile->video_multiplier;
        settings_wire[MT_SETTINGS_V6_WIRE_SIZE + index] = profile->genesis_three_button_mask;
        settings_wire[MT_SETTINGS_V9_WIRE_SIZE + index] =
            profile->c64_sid_mode | (profile->c64_reu_enabled ? 8U : 0U);
        for (effect = 0U; effect < MT_VIDEO_EFFECT_COUNT; ++effect) {
            const uint16_t value = (uint16_t)mt_video_effects_get(
                &profile->video_effects, (mt_video_effect_id_t)effect);
            output[MT_SETTINGS_EFFECTS_OFFSET + effect * 2U] = (uint8_t)value;
            output[MT_SETTINGS_EFFECTS_OFFSET + effect * 2U + 1U] = (uint8_t)(value >> 8U);
        }
    }
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        uint8_t *output = settings_wire + MT_SETTINGS_V5_WIRE_SIZE + slot * MT_SETTINGS_PAD_SIZE;
        const mt_gamepad_config_t *config = &settings->gamepads[slot];
        for (unsigned axis = 0U; axis < MT_GAMEPAD_AXIS_COUNT; ++axis) {
            const mt_gamepad_axis_config_t *value = &config->axis[axis];
            output[axis * 5U] = value->source;
            output[axis * 5U + 1U] = value->invert;
            output[axis * 5U + 2U] = value->deadzone_percent;
            output[axis * 5U + 3U] = (uint8_t)value->sensitivity_percent;
            output[axis * 5U + 4U] = (uint8_t)(value->sensitivity_percent >> 8U);
        }
        memcpy(output + 30U, config->button, 8U);
    }
    store_u32_le(settings_wire + MT_SETTINGS_V7_WIRE_SIZE,
                 (uint32_t)settings->controller_profile_count);
    store_u16_le(settings_wire + MT_SETTINGS_V7_WIRE_SIZE + 4U,
                 settings->legacy_gamepad_pending);
    for (index = 0U; index < settings->controller_profile_count; ++index)
        encode_controller(settings_wire + MT_SETTINGS_V7_WIRE_SIZE +
                          MT_SETTINGS_CONTROLLERS_HEADER_SIZE +
                          index * MT_SETTINGS_CONTROLLER_SIZE,
                          &settings->controller_profiles[index]);
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

static bool decode_settings(mt_supervisor_settings_t *settings, size_t wire_size)
{
    const uint32_t crc = load_u32_le(settings_wire + 16U);
    const uint32_t schema = load_u32_le(settings_wire + 8U);
    const uint32_t profile_count = load_u32_le(settings_wire + 52U);
    const bool legacy = schema == MT_SETTINGS_SCHEMA_V1 ||
                        schema == MT_SETTINGS_SCHEMA_V2;
    const size_t profile_size = legacy ? MT_SETTINGS_LEGACY_PROFILE_SIZE :
        (schema == MT_SETTINGS_SCHEMA_V3 ? MT_SETTINGS_V3_PROFILE_SIZE :
         schema == MT_SETTINGS_SCHEMA_V4 ? MT_SETTINGS_V4_PROFILE_SIZE : MT_SETTINGS_PROFILE_SIZE);
    const size_t expected_size = legacy ? MT_SETTINGS_LEGACY_WIRE_SIZE :
        (schema == MT_SETTINGS_SCHEMA_V3 ? MT_SETTINGS_V3_WIRE_SIZE :
         schema == MT_SETTINGS_SCHEMA_V4 ? MT_SETTINGS_V4_WIRE_SIZE :
         schema == MT_SETTINGS_SCHEMA_V5 ? MT_SETTINGS_V5_WIRE_SIZE :
         schema == MT_SETTINGS_SCHEMA_V6 ? MT_SETTINGS_V6_WIRE_SIZE :
         schema == MT_SETTINGS_SCHEMA_V7 ? MT_SETTINGS_V7_WIRE_SIZE :
         schema == MT_SETTINGS_SCHEMA_V8 || schema == MT_SETTINGS_SCHEMA_V9 ?
             MT_SETTINGS_V9_WIRE_SIZE : MT_SETTINGS_WIRE_SIZE);
    size_t index;
    if (memcmp(settings_wire, settings_magic, sizeof(settings_magic)) != 0 ||
        (!legacy && schema != MT_SETTINGS_SCHEMA_V3 && schema != MT_SETTINGS_SCHEMA_V4 &&
         schema != MT_SETTINGS_SCHEMA_V5 && schema != MT_SETTINGS_SCHEMA_V6 &&
         schema != MT_SETTINGS_SCHEMA_V7 && schema != MT_SETTINGS_SCHEMA_V8 &&
         schema != MT_SETTINGS_SCHEMA_V9 && schema != MT_SETTINGS_SCHEMA) ||
        wire_size != expected_size ||
        load_u32_le(settings_wire + 12U) != wire_size ||
        load_u32_le(settings_wire + 20U) != ~crc ||
        crc32_bytes(settings_wire + MT_SETTINGS_HEADER_SIZE,
                    wire_size - MT_SETTINGS_HEADER_SIZE) != crc ||
        settings_wire[24U] > 1U || profile_count > MT_SETTINGS_MAX_PROFILES) {
        return false;
    }
    if (schema >= MT_SETTINGS_SCHEMA_V4) {
        for (index = 0U; index < MT_SETTINGS_MAX_PROFILES; ++index) {
            const uint8_t multiplier = settings_wire[MT_SETTINGS_HEADER_SIZE + MT_SETTINGS_GLOBAL_SIZE +
                                                     index * profile_size + 251U];
            if (schema < MT_SETTINGS_SCHEMA_V9 ? multiplier != 0U :
                (multiplier > 4U || (index >= profile_count && multiplier != 0U))) return false;
        }
    }
    mt_supervisor_settings_defaults(&settings_decode_candidate);
    if (schema == MT_SETTINGS_SCHEMA) {
        for (index = 0U; index < MT_SETTINGS_MAX_PROFILES; ++index) {
            const uint8_t config = settings_wire[MT_SETTINGS_V9_WIRE_SIZE + index];
            if ((config & 0xf0U) != 0U || !c64_sid_mode_valid(config & 7U) ||
                (index >= profile_count && config != 0U)) return false;
        }
    }
    if (schema < MT_SETTINGS_SCHEMA_V8) {
        settings_decode_candidate.legacy_gamepad_pending = MT_SETTINGS_LEGACY_PAD_MASK;
    } else {
        const uint32_t count = load_u32_le(settings_wire + MT_SETTINGS_V7_WIRE_SIZE);
        if (count > MT_SETTINGS_MAX_CONTROLLER_PROFILES ||
            settings_wire[MT_SETTINGS_V7_WIRE_SIZE + 6U] != 0U ||
            settings_wire[MT_SETTINGS_V7_WIRE_SIZE + 7U] != 0U) return false;
        settings_decode_candidate.controller_profile_count = count;
        settings_decode_candidate.legacy_gamepad_pending =
            load_u16_le(settings_wire + MT_SETTINGS_V7_WIRE_SIZE + 4U);
        for (index = 0U; index < MT_SETTINGS_MAX_CONTROLLER_PROFILES; ++index) {
            const uint8_t *input = settings_wire + MT_SETTINGS_V7_WIRE_SIZE +
                MT_SETTINGS_CONTROLLERS_HEADER_SIZE + index * MT_SETTINGS_CONTROLLER_SIZE;
            if (index < count) {
                if (!decode_controller(&settings_decode_candidate.controller_profiles[index],
                                       input)) return false;
            } else {
                for (size_t byte = 0U; byte < MT_SETTINGS_CONTROLLER_SIZE; ++byte)
                    if (input[byte] != 0U) return false;
            }
        }
    }
    if (schema >= MT_SETTINGS_SCHEMA_V6) {
        settings_decode_candidate.display_mode = settings_wire[25U];
        for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
            const uint8_t *input = settings_wire + MT_SETTINGS_V5_WIRE_SIZE + slot * MT_SETTINGS_PAD_SIZE;
            mt_gamepad_config_t *config = &settings_decode_candidate.gamepads[slot];
            for (unsigned axis = 0U; axis < MT_GAMEPAD_AXIS_COUNT; ++axis) {
                mt_gamepad_axis_config_t *value = &config->axis[axis];
                value->source = input[axis * 5U];
                value->invert = input[axis * 5U + 1U];
                value->deadzone_percent = input[axis * 5U + 2U];
                value->sensitivity_percent = (uint16_t)((uint16_t)input[axis * 5U + 3U] |
                    ((uint16_t)input[axis * 5U + 4U] << 8U));
            }
            memcpy(config->button, input + 30U, 8U);
        }
    }
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
            MT_SETTINGS_GLOBAL_SIZE + index * profile_size;
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
        profile->c64_fast_roms = schema != MT_SETTINGS_SCHEMA_V1 &&
                                 input[242U] != 0U;
        profile->c64_cpu_turbo = input[243U] != 0U;
        profile->c64_sid_mode = schema == MT_SETTINGS_SCHEMA ?
            settings_wire[MT_SETTINGS_V9_WIRE_SIZE + index] & 7U : MT_SETTINGS_C64_SID_MODE_DEFAULT;
        profile->c64_reu_enabled = schema == MT_SETTINGS_SCHEMA ?
            (settings_wire[MT_SETTINGS_V9_WIRE_SIZE + index] & 8U) != 0U : MT_SETTINGS_C64_REU_DEFAULT;
        profile->genesis_three_button_mask = schema >= MT_SETTINGS_SCHEMA_V7 ?
            settings_wire[MT_SETTINGS_V6_WIRE_SIZE + index] : 0U;
        if (legacy) {
            profile->border_style = MT_SETTINGS_BORDER_STYLE_DEFAULT;
            profile->border_color = MT_SETTINGS_BORDER_COLOR_DEFAULT;
            profile->border_padding = MT_SETTINGS_BORDER_PADDING_DEFAULT;
        } else {
            if (input[244U] > 1U) return false;
            profile->border_enabled = input[244U] != 0U;
            profile->border_style = input[245U];
            profile->border_color = input[246U];
            profile->border_padding = input[247U];
        }
        if (schema < MT_SETTINGS_SCHEMA_V4) {
            profile->vectrex_overlay_enabled = true;
            profile->vectrex_beam_gain = MT_SETTINGS_VECTREX_BEAM_GAIN_DEFAULT;
            profile->vectrex_overlay_opacity = MT_SETTINGS_VECTREX_OVERLAY_OPACITY_DEFAULT;
        } else {
            if (input[248U] > 1U) return false;
            profile->vectrex_overlay_enabled = input[248U] != 0U;
            profile->vectrex_beam_gain = input[249U];
            profile->vectrex_overlay_opacity = input[250U];
            profile->video_multiplier = schema >= MT_SETTINGS_SCHEMA_V9 ? input[251U] : 0U;
        }
        mt_video_effects_defaults(&profile->video_effects);
        if (schema >= MT_SETTINGS_SCHEMA_V5) {
            unsigned effect;
            for (effect = 0U; effect < MT_VIDEO_EFFECT_COUNT; ++effect) {
                const size_t offset = MT_SETTINGS_EFFECTS_OFFSET + effect * 2U;
                int value = (int)input[offset] | ((int)input[offset + 1U] << 8U);
                if (value >= 32768) value -= 65536;
                if (!mt_video_effects_set(&profile->video_effects,
                                          (mt_video_effect_id_t)effect, value)) return false;
            }
        }
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
    /* Re-open only the known older length. The SD service still enforces
     * exact reads, and decoding verifies the schema/length pair and CRC. */
    if (sd.status == MT_SD_SIZE_MISMATCH &&
        (sd.size == MT_SETTINGS_LEGACY_WIRE_SIZE || sd.size == MT_SETTINGS_V3_WIRE_SIZE ||
         sd.size == MT_SETTINGS_V4_WIRE_SIZE || sd.size == MT_SETTINGS_V5_WIRE_SIZE ||
         sd.size == MT_SETTINGS_V6_WIRE_SIZE || sd.size == MT_SETTINGS_V7_WIRE_SIZE ||
         sd.size == MT_SETTINGS_V9_WIRE_SIZE)) {
        sd = mt_sd_storage_read_file_optional_exact(
            MT_SETTINGS_PATH, settings_wire, sd.size,
            &found);
    }
    if (sd.status != MT_SD_OK)
        return settings_result(MT_SETTINGS_READ_FAILED, sd);
    if (!found) return settings_result(MT_SETTINGS_MISSING, sd);
    if (!decode_settings(settings, sd.size)) {
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
