/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CORE_FRONTEND_H
#define MULTITINI_CORE_FRONTEND_H

#include "multitini_content.h"
#include "multitini_menu.h"
#include "multitini_iigs_interface.h"
#include "multitini_c64_title.h"
#include "multitini_amiga_interface.h"

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
    bool c64_ef_jumper_disabled;
    bool symbolic_keyboard;
    uint8_t joystick_port;
    bool fast_roms_active;
    bool fast_roms_fallback;
    bool cpu_turbo_enabled;
    uint32_t c64_expansion_caps;
    uint8_t c64_cartridge_policy;
    bool c64_uci_active;
    uint8_t c64_sid_mode;
    bool c64_reu_enabled;
    bool c64_turbo_available, c64_ua_available, c64_ua_enabled, c64_ua_fault;
    bool c64_reu_host_available;
    uint8_t c64_turbo_detail, c64_turbo_flags;
    uint32_t c64_cpu_caps, c64_cpu_caps2, c64_cpu_config, c64_cpu_status;
    bool c64_uci_enabled,c64_title_automatic,c64_title_session,c64_sid_setup;
    bool c64_directory_drive_available, c64_directory_drive_enabled;
    uint32_t c64_title_owned;
    uint64_t c64_sid_owned;
    uint8_t c64_sid_classes,c64_selected_sid;
    const char *c64_title_source;
    mt_c64_sid_model_t c64_sid;
    uint8_t genesis_three_button_mask;
    const char *iigs_drive_name[MT_IIGS_DRIVE_COUNT];
    mt_iigs_config_t iigs_config;
    uint32_t iigs_speed_mask;
    bool iigs_fast_floppy_supported;
    uint16_t iigs_mouse_sensitivity;
    bool iigs_ready;
    const char *amiga_drive_name[MT_AMIGA_DRIVE_COUNT];
    mt_amiga_config_t amiga_config;
    bool amiga_ready;
    bool amiga_cpu14_available,amiga_cpu14_fallback;
    bool amiga_fast_ide_available,amiga_fast_ide_expected;
    bool amiga_display_available,amiga_display_pending;
    uint8_t amiga_display_requested,amiga_display_applied,amiga_display_mode_count;
    const char *amiga_display_status;
} mt_core_frontend_state_t;

typedef struct {
    mt_menu_item_t media_items[MT_MENU_MAX_ITEMS];
    mt_menu_item_t input_items[MT_MENU_MAX_ITEMS];
    mt_menu_core_option_t core_options[MT_MENU_MAX_CORE_OPTIONS];
    char c64_turbo_text[96], c64_turbo_help[192], c64_speed_text[80];
    char c64_option_text[MT_MENU_MAX_CORE_OPTIONS][96];
    char amiga_resolution_text[128],amiga_resolution_help[224];
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
