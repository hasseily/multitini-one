/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CORE_FRONTEND_H
#define MULTITINI_CORE_FRONTEND_H

#include "multitini_content.h"
#include "multitini_menu.h"
#include "multitini_iigs_interface.h"

/* A snapshot of services already owned by the runtime. Building a menu never
 * performs I/O or changes core, media, input, or save state. String storage must
 * outlive the resulting view. A zero core ID is the standby supervisor. */
typedef struct {
    uint32_t core_id;
    uint16_t abi_minor;
    bool services_ready;
    bool drive8_ready;
    bool drive8_mounted;
    bool drive8_d64_mounted;
    const char *drive8_name;
    bool cartridge_attached;
    const char *cartridge_name;
    bool save_active;
    bool save_dirty;
    bool save_faulted;
    bool symbolic_keyboard;
    uint8_t joystick_port;
    bool fast_roms_active;
    bool fast_roms_fallback;
    bool cpu_turbo_enabled;
    uint32_t c64_expansion_caps;
    uint8_t c64_sid_mode;
    bool c64_reu_enabled;
    bool c64_turbo_available, c64_ua_available, c64_ua_enabled, c64_ua_fault;
    bool c64_reu_host_available;
    uint8_t c64_turbo_detail, c64_turbo_flags;
    uint32_t c64_cpu_caps, c64_cpu_caps2, c64_cpu_config, c64_cpu_status;
    uint8_t genesis_three_button_mask;
    const char *iigs_drive_name[MT_IIGS_DRIVE_COUNT];
    mt_iigs_config_t iigs_config;
    uint32_t iigs_speed_mask;
    bool iigs_fast_floppy_supported;
    uint16_t iigs_mouse_sensitivity;
    bool iigs_ready;
} mt_core_frontend_state_t;

typedef struct {
    mt_menu_item_t media_items[MT_MENU_MAX_ITEMS];
    mt_menu_item_t input_items[MT_MENU_MAX_ITEMS];
    mt_menu_core_option_t core_options[MT_MENU_MAX_CORE_OPTIONS];
    char c64_turbo_text[96], c64_turbo_help[192], c64_speed_text[80];
} mt_core_frontend_t;

/* Replace only the profile-owned view fields. The caller retains ownership of
 * catalog, active selection, files, status, content root, and shared settings.
 * frontend must remain alive for as long as the view is used. Unknown core IDs
 * get neutral empty pages rather than inheriting another machine's controls. */
void mt_core_frontend_apply(mt_core_frontend_t *frontend,
                            const mt_core_frontend_state_t *state,
                            mt_menu_view_t *view);
bool mt_core_frontend_browser_supported(uint32_t core_id,
                                        mt_menu_browser_t browser);
bool mt_core_frontend_accepts_content(uint32_t core_id,
                                      mt_menu_browser_t browser,
                                      mt_content_type_t type);
bool mt_core_frontend_browser_folders(uint32_t core_id, mt_menu_browser_t browser);
bool mt_core_frontend_accepts_drive_content(uint32_t core_id, mt_menu_browser_t browser,
                                           unsigned drive, mt_content_type_t type);

#endif
