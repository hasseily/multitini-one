/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SYSTEM_H
#define MULTITINI_SYSTEM_H

#include "multitini_menu.h"

typedef struct {
    uint32_t firmware_build, shell_build, abi;
    const char *firmware_version;
    uint64_t uptime_seconds;
    bool temperature_valid;
    int32_t temperature_micro_c;
    bool clock_available, clock_valid;
    mt_clock_time_t clock;
    bool usb_started;
    uint8_t keyboards, gamepads;
    uint32_t usb_submit_errors, usb_transfer_errors, usb_input_errors;
    int usb_last_error;
    const char *catalog_status;
    size_t catalog_count, catalog_rejected;
    bool dac_ready, audio_muted;
} mt_system_snapshot_t;

typedef struct {
    mt_menu_item_t items[MT_MENU_MAX_ITEMS];
    char values[MT_MENU_MAX_ITEMS][96];
} mt_system_frontend_t;

/* Presentation only: callers supply cached sensor values; no hardware/file I/O. */
void mt_system_frontend_apply(mt_system_frontend_t *frontend,
                              const mt_system_snapshot_t *snapshot,
                              mt_menu_view_t *view);

#endif
