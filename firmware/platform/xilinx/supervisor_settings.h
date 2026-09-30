/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_SUPERVISOR_SETTINGS_H
#define MULTITINI_XILINX_SUPERVISOR_SETTINGS_H

#include "multitini_core_package.h"
#include "multitini_video_effects.h"
#include "multitini_gamepad.h"
#include "sd_storage.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_SETTINGS_PATH "0:/multitini/settings.bin"
#define MT_SETTINGS_JOURNAL_PATH "0:/multitini/settings.bin.mtj"
#define MT_SETTINGS_MAX_PROFILES 16U
#define MT_SETTINGS_MEDIA_NAME_MAX 96U
#define MT_SETTINGS_WIRE_SIZE 5038U
#define MT_SETTINGS_BORDER_STYLE_DEFAULT 0U
#define MT_SETTINGS_BORDER_COLOR_DEFAULT 7U
#define MT_SETTINGS_BORDER_PADDING_DEFAULT 6U
#define MT_SETTINGS_VECTREX_BEAM_GAIN_DEFAULT 1U
#define MT_SETTINGS_VECTREX_OVERLAY_OPACITY_DEFAULT 1U

typedef struct {
    char core_id[MT_CORE_PACKAGE_ID_MAX];
    char variant_id[MT_CORE_VARIANT_ID_MAX];
    char drive8_name[MT_SETTINGS_MEDIA_NAME_MAX];
    char cartridge_name[MT_SETTINGS_MEDIA_NAME_MAX];
    bool symbolic_keyboard;
    uint8_t joystick_port;
    bool c64_fast_roms;
    bool c64_cpu_turbo;
    bool border_enabled;
    uint8_t border_style;
    uint8_t border_color;
    uint8_t border_padding;
    bool vectrex_overlay_enabled;
    uint8_t vectrex_beam_gain;
    uint8_t vectrex_overlay_opacity;
    mt_video_effects_settings_t video_effects;
} mt_core_settings_profile_t;

typedef struct {
    bool auto_boot;
    uint8_t display_mode;
    mt_gamepad_config_t gamepads[MT_GAMEPAD_SLOTS];
    char selected_core_id[MT_CORE_PACKAGE_ID_MAX];
    size_t profile_count;
    mt_core_settings_profile_t profiles[MT_SETTINGS_MAX_PROFILES];
} mt_supervisor_settings_t;

typedef enum {
    MT_SETTINGS_OK = 0,
    MT_SETTINGS_MISSING,
    MT_SETTINGS_INVALID_ARGUMENT,
    MT_SETTINGS_RECOVERY_FAILED,
    MT_SETTINGS_READ_FAILED,
    MT_SETTINGS_CORRUPT,
    MT_SETTINGS_WRITE_FAILED
} mt_settings_status_t;

typedef struct {
    mt_settings_status_t status;
    mt_sd_result_t sd;
} mt_settings_result_t;

void mt_supervisor_settings_defaults(mt_supervisor_settings_t *settings);
const mt_core_settings_profile_t *mt_supervisor_settings_find(
    const mt_supervisor_settings_t *settings, const char *core_id);
bool mt_supervisor_settings_remember_core(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *variant_id);
bool mt_supervisor_settings_remember_variant(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *variant_id);
bool mt_supervisor_settings_remember_drive8(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *file_name);
bool mt_supervisor_settings_remember_cartridge(
    mt_supervisor_settings_t *settings, const char *core_id,
    const char *file_name);
bool mt_supervisor_settings_remember_c64_input(
    mt_supervisor_settings_t *settings, const char *core_id,
    bool symbolic_keyboard, uint8_t joystick_port);
bool mt_supervisor_settings_remember_c64_fast_roms(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled);
bool mt_supervisor_settings_remember_c64_cpu_turbo(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled);
bool mt_supervisor_settings_remember_borders(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled,
    uint8_t style, uint8_t color, uint8_t padding);
bool mt_supervisor_settings_remember_vectrex_video(
    mt_supervisor_settings_t *settings, const char *core_id, bool enabled,
    uint8_t beam_gain, uint8_t overlay_opacity);
bool mt_supervisor_settings_remember_video_effects(
    mt_supervisor_settings_t *settings, const char *core_id,
    const mt_video_effects_settings_t *effects);

mt_settings_result_t mt_supervisor_settings_load(
    mt_supervisor_settings_t *settings);
mt_settings_result_t mt_supervisor_settings_save(
    const mt_supervisor_settings_t *settings);
const char *mt_settings_status_name(mt_settings_status_t status);

#endif
