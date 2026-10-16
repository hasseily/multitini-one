/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_system.h"
#include "../golden/lib/multiboot.h"

#include <stdio.h>
#include <string.h>

void mt_system_frontend_apply(mt_system_frontend_t *frontend,
                              const mt_system_snapshot_t *snapshot,
                              mt_menu_view_t *view)
{
    static const char *const labels[] = {
        "Restart active core", "Autoboot current core", "Read documents", "Date and time",
        "Firmware build", "FPGA build", "Interface", "Uptime", "Board temperature",
        "USB devices", "USB errors", "SD core catalog", "Saved data", "Audio output",
        "Restart USB", "Golden updater", "Startup firmware", "Switch to Appletini",
        "Golden Boot", "USB SD card", "Update Golden Boot..."
    };
    static const char *const help[] = {
        "Commits pending media changes before restarting the active core.",
        "Starts this core at power-on. Off keeps the standby screen until you choose a core.",
        "Browse /multitini for TXT instructions. Reading never changes mounted media.",
        "Edit the board's battery-backed clock. Changes apply only when you select Save time.",
        "Release name and build identifier of the ARM firmware currently running.",
        "Build identifier read from the currently loaded FPGA image.",
        "The active shell interface version. Different core images may have different versions.",
        "Elapsed time since the board started. Core switches do not reset this counter.",
        "TMP102 reading sampled once per second while this page is visible.",
        "Connected keyboard and gamepad counts reported by the USB host.",
        "Submit / transfer / core-input errors and the last error code. These are cumulative host counters.",
        "Result of the last core catalog scan, including rejected packages; not a live SD health test.",
        "Battery-save and settings status. Disk writes retain their existing safe flush and eject handling.",
        "DAC initialization and output mute state. Opening the menu also mutes core audio.",
        "Stops and restarts the USB host to rediscover attached devices. Held inputs are released.",
        "Saves pending media changes, then returns to the board's recovery updater.",
        "Choose the firmware used at power-on. A missing or damaged choice falls back to the other verified image.",
        "Saves pending media changes and restarts into Appletini. Requires a verified Appletini image and multiboot updater.",
        "Version read from a verified Golden image installed in flash. Unknown means its version could not be verified.",
        "Exports the SD card to a PC on USB0. Flushes media first; eject on PC before stopping. Restarts the active core afterward.",
        "Validates BOOT.BIN on the SD card, stages Golden Boot, then restarts to complete the update."
    };
    size_t i;
    const bool active = view != NULL && view->active_core_id != NULL && view->active_core_id[0] != '\0';
    if (frontend == NULL || snapshot == NULL || view == NULL) return;
    memset(frontend, 0, sizeof(*frontend));
    for (i = 0U; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        frontend->items[i].label = labels[i];
        frontend->items[i].help = help[i];
        frontend->items[i].value = frontend->values[i];
        frontend->items[i].value_color = MT_UI_COLOR_TEXT;
    }
#define VALUE(index, ...) ((void)snprintf(frontend->values[index], sizeof(frontend->values[index]), __VA_ARGS__))
    VALUE(0, "%s", active ? "Restart" : "No active core");
    VALUE(1, "%s", view->settings_auto_boot ? "On" : "Off");
    VALUE(2, "Choose TXT file...");
    if (snapshot->clock_valid)
        VALUE(3, "%04u-%02u-%02u  %02u:%02u:%02u",
            (unsigned)snapshot->clock.year, (unsigned)snapshot->clock.month,
            (unsigned)snapshot->clock.day, (unsigned)snapshot->clock.hour,
            (unsigned)snapshot->clock.minute, (unsigned)snapshot->clock.second);
    else VALUE(3, "%s", snapshot->clock_available ? "Clock needs setting" : "Clock unavailable");
    if (snapshot->firmware_version != NULL && snapshot->firmware_version[0] != '\0')
        VALUE(4, "%s (%08lX)", snapshot->firmware_version, (unsigned long)snapshot->firmware_build);
    else VALUE(4, "%08lX", (unsigned long)snapshot->firmware_build);
    VALUE(5, "%08lX", (unsigned long)snapshot->shell_build);
    VALUE(6, "%lu.%lu", (unsigned long)(snapshot->abi >> 16U),
        (unsigned long)(snapshot->abi & UINT32_C(0xFFFF)));
    VALUE(7, "%lu d  %02lu:%02lu:%02lu", (unsigned long)(snapshot->uptime_seconds / 86400U),
        (unsigned long)(snapshot->uptime_seconds / 3600U % 24U),
        (unsigned long)(snapshot->uptime_seconds / 60U % 60U),
        (unsigned long)(snapshot->uptime_seconds % 60U));
    if (snapshot->temperature_valid) {
        const int64_t temp = snapshot->temperature_micro_c;
        const uint64_t magnitude = (uint64_t)(temp < 0 ? -temp : temp);
        VALUE(8, "%s%lu.%02lu C", temp < 0 ? "-" : "", (unsigned long)(magnitude / 1000000U),
            (unsigned long)(magnitude / 10000U % 100U));
    } else VALUE(8, "Unavailable");
    VALUE(9, "%s; %u keyboard(s), %u pad(s)", snapshot->usb_started ? "Running" : "Stopped",
        (unsigned)snapshot->keyboards, (unsigned)snapshot->gamepads);
    VALUE(10, "%lu / %lu / %lu; last %d", (unsigned long)snapshot->usb_submit_errors,
        (unsigned long)snapshot->usb_transfer_errors, (unsigned long)snapshot->usb_input_errors,
        snapshot->usb_last_error);
    VALUE(11, "%s; %lu cores, %lu rejected", snapshot->catalog_status != NULL ? snapshot->catalog_status : "Unknown",
        (unsigned long)snapshot->catalog_count, (unsigned long)snapshot->catalog_rejected);
    VALUE(12, "%s", view->save_faulted ? "Save failed" : view->settings_faulted ? "Settings need attention" :
        view->save_dirty ? "Save pending" : "No pending warnings");
    VALUE(13, "%s", !snapshot->dac_ready ? "DAC unavailable" : snapshot->audio_muted ? "Muted" : "Ready");
    VALUE(14, "Re-scan devices");
    VALUE(15, "Return to updater");
    VALUE(16, "%s", !snapshot->multiboot_ready ? "Update golden first" :
        snapshot->startup_product == MB_PRODUCT_APPLETINI ? "Appletini" : "Multitini");
    VALUE(17, "%s", !snapshot->multiboot_ready ? "Update golden first" :
        snapshot->appletini_present ? "Restart into Appletini" : "Not installed or invalid");
    VALUE(18, "%s", snapshot->golden_version != NULL && snapshot->golden_version[0] != '\0' ?
        snapshot->golden_version : "Unknown");
    VALUE(19, "Mount on PC; restart core on exit");
    VALUE(20, "BOOT.BIN on SD card");
#undef VALUE
    frontend->items[0].action = MT_MENU_ACTION_RESET_CORE;
    frontend->items[0].enabled = active;
    frontend->items[1].action = MT_MENU_ACTION_TOGGLE_AUTO_BOOT;
    frontend->items[1].enabled = active;
    frontend->items[1].control = MT_MENU_CONTROL_CHECKBOX;
    frontend->items[1].control_value = view->settings_auto_boot ? 1U : 0U;
    frontend->items[2].action = MT_MENU_ACTION_BROWSE_DOCUMENTS;
    frontend->items[2].enabled = true;
    frontend->items[3].action = MT_MENU_ACTION_EDIT_CLOCK;
    frontend->items[3].enabled = snapshot->clock_available;
    frontend->items[14].action = MT_MENU_ACTION_RESTART_USB;
    frontend->items[14].enabled = true;
    frontend->items[15].action = MT_MENU_ACTION_REBOOT_GOLDEN;
    frontend->items[15].enabled = true;
    frontend->items[16].action = MT_MENU_ACTION_SET_BOOT_DEFAULT;
    frontend->items[16].argument = snapshot->startup_product == MB_PRODUCT_APPLETINI ?
        MB_PRODUCT_MULTITINI : MB_PRODUCT_APPLETINI;
    frontend->items[16].enabled = snapshot->multiboot_ready &&
        (snapshot->startup_product == MB_PRODUCT_APPLETINI ? snapshot->multitini_present : snapshot->appletini_present);
    frontend->items[17].action = MT_MENU_ACTION_SWITCH_FIRMWARE;
    frontend->items[17].argument = MB_PRODUCT_APPLETINI;
    frontend->items[17].enabled = snapshot->multiboot_ready && snapshot->appletini_present;
    frontend->items[19].action = MT_MENU_ACTION_USB_SD;
    frontend->items[19].enabled = true;
    frontend->items[20].action = MT_MENU_ACTION_UPDATE_GOLDEN;
    frontend->items[20].enabled = snapshot->golden_update_present;
    if (view->save_faulted || view->settings_faulted || view->save_dirty)
        frontend->items[12].value_color = MT_UI_COLOR_WARNING;
    if (snapshot->usb_last_error != 0) frontend->items[10].value_color = MT_UI_COLOR_WARNING;
    /* About shows the installed Golden version directly below Interface.
     * Keep value pointers into the same cache and updater actions in System. */
    memcpy(frontend->about_items, frontend->items + 4U,
        3U * sizeof(frontend->items[0]));
    frontend->about_items[3] = frontend->items[18];
    memcpy(frontend->about_items + 4U, frontend->items + 7U,
        7U * sizeof(frontend->items[0]));
    memmove(frontend->items + 4U, frontend->items + 14U,
        4U * sizeof(frontend->items[0]));
    memmove(frontend->items + 8U, frontend->items + 19U,
        2U * sizeof(frontend->items[0]));
    view->system_items = frontend->items;
    view->system_item_count = sizeof(labels) / sizeof(labels[0]) - MT_MENU_MAX_ABOUT_ITEMS -
        (snapshot->golden_update_present ? 0U : 1U);
    view->about_items = frontend->about_items;
    view->about_item_count = MT_MENU_MAX_ABOUT_ITEMS;
    view->clock_available = snapshot->clock_available;
    view->clock_valid = snapshot->clock_valid;
    view->clock = snapshot->clock;
}
