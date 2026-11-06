/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact schema-10 codec at the pin in fixtures/settings_schema10_reference.json.
 * The original decoder/encoder bytes remain unchanged.
 * Current in-memory types have additive fields; none changes the wire layout.
 * Re-encoding independently verifies every old field, not just acceptance. */
#include "multitini_controls.h"
#include "multitini_atari_xt_interface.h"
#include "multitini_spectrum_interface.h"

static uint32_t schema10_controls_action_mask(uint32_t core)
{
    /* The codec delegates core ID validation to controls.c. The pinned
     * firmware predates these cores, unlike the test's linked controls.c. */
    if (core == MT_ATARI_XT_CORE_ID || core == MT_SPECTRUM_CORE_ID) return 0U;
    return mt_controls_action_mask(core);
}

#define mt_controls_action_mask schema10_controls_action_mask
#define mt_settings_status_name schema10_mt_settings_status_name
#define mt_supervisor_settings_defaults schema10_mt_supervisor_settings_defaults
#define mt_supervisor_settings_find schema10_mt_supervisor_settings_find
#define mt_supervisor_settings_find_controller schema10_mt_supervisor_settings_find_controller
#define mt_supervisor_settings_load schema10_mt_supervisor_settings_load
#define mt_supervisor_settings_remember_borders schema10_mt_supervisor_settings_remember_borders
#define mt_supervisor_settings_remember_cartridge schema10_mt_supervisor_settings_remember_cartridge
#define mt_supervisor_settings_remember_controller schema10_mt_supervisor_settings_remember_controller
#define mt_supervisor_settings_remember_core schema10_mt_supervisor_settings_remember_core
#define mt_supervisor_settings_remember_genesis_pads schema10_mt_supervisor_settings_remember_genesis_pads
#define mt_supervisor_settings_remember_variant schema10_mt_supervisor_settings_remember_variant
#define mt_supervisor_settings_remember_vectrex_video schema10_mt_supervisor_settings_remember_vectrex_video
#define mt_supervisor_settings_remember_video_effects schema10_mt_supervisor_settings_remember_video_effects
#define mt_supervisor_settings_remember_video_multiplier schema10_mt_supervisor_settings_remember_video_multiplier
#define mt_supervisor_settings_save schema10_mt_supervisor_settings_save
#define mt_supervisor_settings_remember_drive8 schema10_mt_supervisor_settings_remember_drive8
#define mt_supervisor_settings_remember_c64_input schema10_mt_supervisor_settings_remember_c64_input
#define mt_supervisor_settings_remember_c64_fast_roms schema10_mt_supervisor_settings_remember_c64_fast_roms
#define mt_supervisor_settings_remember_c64_cpu_turbo schema10_mt_supervisor_settings_remember_c64_cpu_turbo
#define mt_supervisor_settings_remember_c64_expansion schema10_mt_supervisor_settings_remember_c64_expansion
#include "fixtures/settings_schema10_accept.c"

bool mt_schema10_roundtrip(const uint8_t *input, uint8_t *output)
{
    mt_supervisor_settings_t decoded;
    memcpy(settings_wire, input, sizeof(settings_wire));
    if (!decode_settings(&decoded, sizeof(settings_wire))) return false;
    if (!encode_settings(&decoded)) return false;
    memcpy(output, settings_wire, sizeof(settings_wire));
    return true;
}
