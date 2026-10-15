# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production menu transaction boundary with delayed video commits.

Hardware/content services are mocked; the menu, presenter, keyboard filter,
hide helper and dispatcher are compiled from the current production sources.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def production_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}", start) + len("\n}")
    return source[start:end]


PREAMBLE = r'''
#include "multitini_menu.h"
#include "multitini_c64_hid_input.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_tmp102.h"
#include "vectrex_content_service.h"
#include "supervisor_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MT_MENU_HIDE_TIMEOUT_MS UINT64_C(1000)
#define MT_SUPERVISOR_CORE_ID UINT32_C(0)
static mt_menu_t supervisor_menu;
static mt_ui_frame_t supervisor_frame;
static mt_ui_presenter_t supervisor_presenter;
static mt_supervisor_t supervisor;
static mt_menu_action_t pending_menu_action;
static bool supervisor_menu_ready, supervisor_menu_dirty;
static bool supervisor_menu_action_active;
static bool vectrex_basic_running, vectrex_basic_stop;
static bool usb_sd_modal, usb_sd_stop_requested;
static uint8_t supervisor_menu_keys[MT_C64_HID_SLOT_COUNT][MT_C64_HID_USAGE_BYTES];
static mt_ui_color_t supervisor_status_color;
static uint64_t now_ms;
static uint64_t last_system_refresh_ms;
static bool i2c_ready, board_temperature_valid;
static int32_t board_temperature_micro_c;
static int i2c_instance;
static unsigned board_refreshes, temperature_reads, update_file_refreshes;
static void refresh_multiboot_status(void) { ++board_refreshes; }
static void refresh_golden_update_file(void) { ++update_file_refreshes; }
static int board_i2c_read(int *instance, uint8_t address, uint8_t reg, uint8_t *raw, int count) {
    assert(instance == &i2c_instance && address == MT_TMP102_I2C_ADDRESS &&
        reg == MT_TMP102_TEMPERATURE_REGISTER && count == 2);
    raw[0] = 0x19U; raw[1] = 0x80U; ++temperature_reads; return 0;
}
static uint32_t status_word, committed_status;
static uint32_t core_id, core_status, core_control;
static unsigned commits, hidden_commits, shown_commits, writes, runtime_polls;
static unsigned countdown, errors, reads, actions, outcome, keyboard_checks;
static bool stall, nested_hide, expected_visible;
static mt_controller_device_t devices[MT_GAMEPAD_SLOTS], controller_edit_devices[MT_GAMEPAD_SLOTS];
static mt_controller_config_t controller_configs[MT_GAMEPAD_SLOTS];
static mt_gamepad_state_t raw_controllers[MT_GAMEPAD_SLOTS];
static bool controller_menu_chords[MT_GAMEPAD_SLOTS];
static uint32_t controller_edit_core;
static mt_supervisor_settings_t persistent_settings, settings_before_update;
static unsigned screenshot_requests, output_screenshot_requests, config_applies, settings_writes;
static bool gamepad_preview, save_sd_fail, stored_settings;
static bool screenshot_busy, screenshot_finish, screenshot_ui_visible, borders_supported;
static unsigned screenshot_polls, screenshot_logs;
static uint32_t screenshot_core_id, display_status, border_config, screenshot_border;
static uint8_t screenshot_display_mode;
static const mt_ui_frame_t *screenshot_ui;
static const char *screenshot_status;
static char reported_status[192], screenshot_core_name[MT_CORE_PACKAGE_ID_MAX];
static uint8_t settings_wire[MT_SETTINGS_WIRE_SIZE];
bool usb_hid_service_controller_device(uint8_t slot, mt_controller_device_t *device) {
    *device = devices[slot]; return device->connected != 0U;
}
bool usb_hid_service_get_controller_config(uint8_t slot, mt_controller_config_t *config) {
    *config = controller_configs[slot]; return true;
}
uint8_t usb_hid_service_controller_player(uint8_t slot) {
    return devices[slot].connected && slot < 2U ? slot : UINT8_MAX;
}
void usb_hid_service_controller_snapshot(uint8_t slot, mt_gamepad_state_t *raw, mt_controls_output_t *output) {
    if (raw) *raw = raw_controllers[slot];
    if (output) memset(output, 0, sizeof(*output));
}
bool usb_hid_service_controller_menu_chord(uint8_t slot) {
    return controller_menu_chords[slot];
}
bool usb_hid_service_validate_controller_configs(const mt_controller_config_t *configs) {
    uint8_t reserved = 0U;
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        if (!devices[slot].connected) continue;
        if (!mt_controls_config_valid(supervisor.active.package.core_id, &configs[slot])) return false;
        unsigned assignment = configs[slot].assignment;
        if (assignment == MT_CONTROLLER_PORT1 || assignment == MT_CONTROLLER_PORT2) {
            const uint8_t bit = (uint8_t)(1U << (assignment - 1U));
            if (reserved & bit) return false;
            reserved |= bit;
        }
    }
    return true;
}
bool usb_hid_service_set_controller_configs(const mt_controller_config_t *configs) {
    assert(usb_hid_service_validate_controller_configs(configs));
    assert(settings_writes != 0U && !save_sd_fail); /* Apply only after persisted success. */
    memcpy(controller_configs, configs, sizeof(controller_configs)); ++config_applies; return true;
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *path, const char *journal, uint8_t *buffer, size_t capacity) {
    (void)path; (void)journal; (void)buffer; (void)capacity;
    mt_sd_result_t result = {0}; return result;
}
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char *path, uint8_t *dst, size_t size, bool *found) {
    (void)path; assert(size == sizeof(settings_wire)); *found = stored_settings;
    if (*found) memcpy(dst, settings_wire, size);
    mt_sd_result_t result = {0}; result.size = *found ? size : 0U; return result;
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *path, const char *journal, const uint8_t *src, size_t size) {
    (void)path; (void)journal; ++settings_writes;
    mt_sd_result_t result = {0};
    if (save_sd_fail) result.status = MT_SD_WRITE_FAILED;
    else { assert(size == sizeof(settings_wire)); memcpy(settings_wire, src, size); stored_settings = true; }
    return result;
}
static bool save_persistent_settings(void) {
    return mt_supervisor_settings_save(&persistent_settings).status == MT_SETTINGS_OK;
}
static bool mt_screenshot_service_request(bool output) {
    ++screenshot_requests; if (output) ++output_screenshot_requests; screenshot_busy = true; return true;
}
static bool mt_screenshot_service_busy(void) { return screenshot_busy; }
static const char *mt_screenshot_service_status(void) { return screenshot_status; }
static void mt_screenshot_service_poll(uint32_t id, const char *name, uint8_t mode,
    uint32_t border, const mt_ui_frame_t *ui, bool visible) {
    ++screenshot_polls; screenshot_core_id = id; screenshot_display_mode = mode;
    screenshot_border = border; screenshot_ui = ui; screenshot_ui_visible = visible;
    (void)snprintf(screenshot_core_name, sizeof(screenshot_core_name), "%s", name);
    if (screenshot_finish) screenshot_busy = false;
}
static bool selection_supports_borders(const mt_core_selection_t *selection) {
    (void)selection; return borders_supported;
}
static int xil_printf(const char *format, ...) { (void)format; ++screenshot_logs; return 0; }
static const mt_menu_file_t supervisor_menu_files[8] = {
    {"one.d64", 1U, 0U}, {"artwork.OVR", 777600U, 0U},
    {"folder.ovr", 0U, MT_MENU_FILE_DIRECTORY}, {"game.vec", 32768U, 0U},
    {"game.bas", 100U, 0U}, {"five", 0U, 0U}, {"six", 0U, 0U}, {"seven", 0U, 0U}
};
static size_t supervisor_menu_file_count = 8U;
static bool core_ready;
static unsigned input_changes, content_loads;
static mt_vectrex_content_result_t content_result;
static const mt_menu_item_t media_items[5] = {
    {.label="Disk", .value="mounted", .help="", .action=MT_MENU_ACTION_BROWSE_DRIVE8,
     .enabled=true, .value_color=MT_UI_COLOR_TEXT}
};
static const mt_core_selection_t cores[2] = {0};
static uint64_t monotonic_ms(void *context) {(void)context; return now_ms;}
static void usleep(unsigned microseconds) {now_ms += microseconds / 1000U;}
static uint32_t shell_read(uint32_t offset) {
    ++reads;
    if (offset == MT_REG_CORE_ID) return core_id;
    if (offset == MT_REG_STATUS) return core_status;
    if (offset == MT_REG_CONTROL) return core_control;
    if (offset == MT_REG_DISPLAY_STATUS) return display_status;
    if (offset == MT_REG_BORDER_CONFIG) return border_config;
    assert(offset == MT_REG_UI_STATUS); return status_word;
}
static uint32_t supervisor_ui_read(void *context, uint32_t offset) {
    (void)context; return shell_read(offset);
}
static void supervisor_ui_write(void *context, uint32_t offset, uint32_t value) {
    (void)context; ++writes;
    if (offset != MT_REG_UI_COMMIT) return;
    assert(countdown == 0U); /* Never overwrite an unacknowledged mailbox. */
    ++commits;
    if (value & MT_UI_COMMIT_VISIBLE) ++shown_commits;
    else ++hidden_commits;
    committed_status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT |
        ((value & MT_UI_COMMIT_DENSE) ? MT_UI_STATUS_DENSE : 0U) |
        ((value & MT_UI_COMMIT_VISIBLE) ? MT_UI_STATUS_VISIBLE : 0U) |
        ((value & MT_UI_COMMIT_BANK) ? MT_UI_STATUS_BANK : 0U);
    /* READY deliberately remains stale until a later video boundary. */
    countdown = 3U;
}
static mt_menu_view_t supervisor_menu_view(void) {
    mt_menu_view_t view = {0};
    if (supervisor.has_active) view.active_core_id = "test-core";
    /* CONTROLLER_VIEW_FROM_PRODUCTION */
    view.cores = cores; view.core_count = 2U;
    view.files = supervisor_menu_files; view.file_count = supervisor_menu_file_count;
    view.media_items = media_items; view.media_item_count = 5U;
    view.status_color = supervisor_status_color;
    return view;
}
static void hardware_tick(void) {
    if (!stall && countdown && --countdown == 0U) status_word = committed_status;
}
static bool supervisor_keyboard_filter(void *, uint8_t, uint8_t,
                                       const uint8_t *, size_t);
static void service_runtime(void) {
    ++runtime_polls;
    if (supervisor_menu_action_active) {
        static const uint8_t keys[] = {MT_MENU_HID_ENTER, MT_MENU_HID_F12};
        const mt_menu_action_kind_t pending_before = pending_menu_action.kind;
        assert(supervisor_menu.visible == expected_visible);
        assert(supervisor_keyboard_filter(NULL, 0U, 0U, keys, sizeof(keys)));
        assert(supervisor_menu.visible == expected_visible);
        assert(pending_menu_action.kind == pending_before);
        assert(supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U));
        ++keyboard_checks;
    }
    hardware_tick();
}
static void set_supervisor_status(const char *text) {
    (void)snprintf(reported_status, sizeof(reported_status), "%s", text);
    supervisor_status_color = MT_UI_COLOR_TEXT;
    supervisor_menu_dirty = true;
}
static void set_supervisor_warning(const char *text) {
    (void)snprintf(reported_status, sizeof(reported_status), "%s", text);
    supervisor_status_color = MT_UI_COLOR_WARNING; supervisor_menu_dirty = true;
}
static void set_supervisor_error(const char *text) {
    (void)text; ++errors; supervisor_status_color = MT_UI_COLOR_RED;
    supervisor_menu_dirty = true;
}
void usb_hid_service_set_menu_capture(bool captured) { (void)captured; }
void usb_hid_service_set_gamepad_preview(bool preview) { gamepad_preview = preview; }
void usb_hid_service_set_core_ready(bool ready) {
    core_ready = ready; ++input_changes;
}
mt_vectrex_content_result_t vectrex_content_service_load(const char *name) {
    ++content_loads;
    if (mt_content_classify(name) != MT_CONTENT_VECTREX_OVERLAY) assert(!core_ready);
    service_runtime(); /* A blocking upload continues to receive USB reports. */
    return content_result;
}
static void handle_menu_action(mt_menu_action_t action);
'''


SCENARIOS = r'''
enum { SUCCESS, WARNING, FAILURE, NO_CHANGE, ROLLBACK };
static void reset_fixture(bool visible) {
    memset(&supervisor_menu, 0, sizeof(supervisor_menu));
    memset(&supervisor, 0, sizeof(supervisor));
    memset(supervisor_menu_keys, 0, sizeof(supervisor_menu_keys));
    memset(&pending_menu_action, 0, sizeof(pending_menu_action));
    memset(devices, 0, sizeof(devices));
    memset(controller_edit_devices, 0, sizeof(controller_edit_devices));
    memset(raw_controllers, 0, sizeof(raw_controllers));
    memset(controller_menu_chords, 0, sizeof(controller_menu_chords));
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        raw_controllers[slot].hat = 8U;
        mt_controls_default(MT_C64_CORE_ID, NULL, &controller_configs[slot]);
    }
    mt_supervisor_settings_defaults(&persistent_settings);
    screenshot_requests = output_screenshot_requests = config_applies = settings_writes = 0U;
    gamepad_preview = save_sd_fail = stored_settings = false;
    screenshot_busy = screenshot_finish = screenshot_ui_visible = borders_supported = false;
    screenshot_polls = screenshot_logs = 0U; screenshot_core_id = screenshot_border = 0U;
    display_status = 0x12U; border_config = 0x1234U; screenshot_display_mode = 0U;
    screenshot_status = NULL; screenshot_ui = NULL;
    reported_status[0] = screenshot_core_name[0] = '\0';
    mt_ui_presenter_init(&supervisor_presenter);
    supervisor_menu_ready = true; supervisor_menu_dirty = false;
    supervisor_menu_action_active = false;
    vectrex_basic_running = vectrex_basic_stop = false;
    supervisor_menu.visible = expected_visible = visible;
    supervisor_menu.page = MT_MENU_PAGE_MEDIA;
    supervisor_menu.row = 2U;
    supervisor.has_active = true;
    supervisor.active.package.core_id = MT_C64_CORE_ID;
    core_id = MT_C64_CORE_ID; core_status = MT_STATUS_CORE_READY; core_control = 0U;
    core_ready = true; input_changes = content_loads = 0U;
    memset(&content_result, 0, sizeof(content_result));
    supervisor_status_color = MT_UI_COLOR_TEXT;
    now_ms = commits = hidden_commits = shown_commits = writes = 0U;
    runtime_polls = countdown = errors = reads = actions = keyboard_checks = 0U;
    stall = nested_hide = false; outcome = SUCCESS;
    usb_sd_modal = usb_sd_stop_requested = false;
    status_word = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT | MT_UI_STATUS_DENSE;
    if (visible) status_word |= MT_UI_STATUS_VISIBLE;
}
static bool progress_expected(mt_menu_action_kind_t kind) {
    switch (kind) {
    case MT_MENU_ACTION_SWITCH_CORE: case MT_MENU_ACTION_MOUNT_DRIVE8:
    case MT_MENU_ACTION_MOUNT_AND_RUN: case MT_MENU_ACTION_LOAD_DISK_PROGRAM:
    case MT_MENU_ACTION_DIRECT_DISK_PROGRAM: case MT_MENU_ACTION_EJECT_DRIVE8:
    case MT_MENU_ACTION_LOAD_CONTENT: case MT_MENU_ACTION_DETACH_CARTRIDGE:
    case MT_MENU_ACTION_TOGGLE_CORE_OPTION: case MT_MENU_ACTION_RESET_CORE:
    case MT_MENU_ACTION_REBOOT_GOLDEN: case MT_MENU_ACTION_SWITCH_FIRMWARE: return true;
    default: return false;
    }
}
static void handle_menu_action(mt_menu_action_t action) {
    ++actions;
    assert(supervisor_menu.visible == expected_visible);
    assert(supervisor_menu_action_active);
    if (menu_action_needs_core_progress(action)) {
        assert(supervisor_menu_action_active);
        assert(!supervisor_presenter.pending);
        assert((status_word & MT_UI_STATUS_VISIBLE) == 0U);
        if (nested_hide) {
            const unsigned commits_before = commits;
            assert(hide_menu_for_core_action());
            assert(hide_menu_for_core_action());
            assert(commits == commits_before);
            assert(supervisor_menu.visible == expected_visible);
        }
    }
    service_runtime();
    if (action.kind == MT_MENU_ACTION_LOAD_CONTENT && core_id == MT_VECTREX_CORE_ID)
        (void)load_vectrex_content_with_input(supervisor_menu_files[action.file_index].name);
    if (action.kind == MT_MENU_ACTION_SWITCH_CORE && outcome != NO_CHANGE) {
        /* Reconfiguration/rollback resets both banks; production load() does this. */
        mt_ui_presenter_invalidate(&supervisor_presenter);
        status_word = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
        countdown = 0U;
    }
    if (outcome == SUCCESS &&
        (action.kind == MT_MENU_ACTION_LOAD_CONTENT ||
         action.kind == MT_MENU_ACTION_MOUNT_DRIVE8))
        mt_menu_close_browser(&supervisor_menu);
    if (outcome == FAILURE || outcome == ROLLBACK)
        set_supervisor_error("action failed");
    else {
        set_supervisor_status(outcome == NO_CHANGE ? "no change" : "applied");
        if (outcome == WARNING) supervisor_status_color = MT_UI_COLOR_WARNING;
    }
}
static void settle_menu(void) {
    unsigned poll;
    for (poll = 0U; poll < 20U; ++poll) {
        hardware_tick();
        service_supervisor_menu();
        /* The idle service need not poll an acknowledged commit: the presenter
         * reconciles its cached pending flag on the next changed frame. */
        if (!countdown && !supervisor_menu_dirty)
            break;
    }
    assert(poll < 20U);
    assert(supervisor_menu.visible == expected_visible);
    assert(((status_word & MT_UI_STATUS_VISIBLE) != 0U) == expected_visible);
}
static void dispatch(mt_menu_action_kind_t kind) {
    pending_menu_action.kind = kind;
    service_supervisor_menu();
    assert(supervisor_menu.visible == expected_visible);
    settle_menu();
}
static void all_actions(void) {
    unsigned kind;
    for (kind = MT_MENU_ACTION_NONE; kind <= MT_MENU_ACTION_SWITCH_FIRMWARE; ++kind) {
        mt_menu_action_t action = {(mt_menu_action_kind_t)kind, 0U, 0U, 0U};
        reset_fixture(true);
        assert(menu_action_needs_core_progress(action) == progress_expected(action.kind));
        dispatch(action.kind);
        assert(actions == (kind == MT_MENU_ACTION_NONE ? 0U : 1U));
        assert(hidden_commits == (progress_expected(action.kind) ? 1U : 0U));
        if (progress_expected(action.kind)) assert(keyboard_checks != 0U);
    }
    reset_fixture(true);
    {
        mt_menu_action_t action = {MT_MENU_ACTION_TOGGLE_CORE_OPTION, 0U, 0U, 1U};
        assert(!menu_action_needs_core_progress(action));
        action.file_index = 0U;
        supervisor.active.package.core_id = MT_GBC_CORE_ID;
        assert(!menu_action_needs_core_progress(action));
        supervisor.active.package.core_id = MT_GENESIS_CORE_ID;
        assert(menu_action_needs_core_progress(action));
        action.file_index = 1U;
        assert(menu_action_needs_core_progress(action));
    }
}
static void result_and_browser_retention(void) {
    unsigned result;
    const mt_menu_action_kind_t kinds[] = {
        MT_MENU_ACTION_MOUNT_DRIVE8, MT_MENU_ACTION_LOAD_CONTENT,
        MT_MENU_ACTION_SWITCH_CORE
    };
    size_t index;
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        for (result = SUCCESS; result <= ROLLBACK; ++result) {
            reset_fixture(true); outcome = result;
            if (kinds[index] == MT_MENU_ACTION_LOAD_CONTENT)
                supervisor.active.package.core_id = MT_GBC_CORE_ID;
            if (kinds[index] == MT_MENU_ACTION_SWITCH_CORE) {
                supervisor_menu.page = MT_MENU_PAGE_CORES;
                supervisor_menu.row = 1U;
                supervisor_menu.selected_variant[1] = 1U;
            } else {
                mt_menu_open_browser(&supervisor_menu, MT_MENU_BROWSER_CARTRIDGE);
                supervisor_menu.row = 6U;
                supervisor_menu.first_row = 3U;
            }
            dispatch(kinds[index]);
            if (kinds[index] == MT_MENU_ACTION_SWITCH_CORE) {
                assert(supervisor_menu.page == MT_MENU_PAGE_CORES);
                assert(supervisor_menu.row == 1U);
                assert(supervisor_menu.selected_variant[1] == 1U);
            } else if (result == SUCCESS) {
                assert(supervisor_menu.browser == MT_MENU_BROWSER_NONE);
                assert(supervisor_menu.page == MT_MENU_PAGE_MEDIA);
                assert(supervisor_menu.row == 2U);
            } else {
                assert(supervisor_menu.browser == MT_MENU_BROWSER_CARTRIDGE);
                assert(supervisor_menu.row == 6U);
                assert(supervisor_menu.first_row == 3U);
            }
        }
    }
}
static void delayed_and_nested(void) {
    reset_fixture(true); nested_hide = true;
    /* Begin with an earlier visible commit pending in the other bank. */
    supervisor_presenter.pending = true;
    supervisor_presenter.pending_status = MT_UI_STATUS_DENSE |
        MT_UI_STATUS_VISIBLE | MT_UI_STATUS_BANK;
    committed_status = status_word | MT_UI_STATUS_BANK;
    countdown = 2U;
    pending_menu_action.kind = MT_MENU_ACTION_LOAD_CONTENT;
    service_supervisor_menu();
    assert(actions == 1U && hidden_commits == 1U);
    assert(supervisor_menu.visible && supervisor_presenter.pending);
    assert((status_word & MT_UI_STATUS_VISIBLE) == 0U);
    assert(countdown == 3U); /* Reopening is pending, not prematurely acknowledged. */
    settle_menu();
    assert(shown_commits == 1U && keyboard_checks >= 5U);
}
static void uart_and_hidden(void) {
    unsigned visible;
    for (visible = 0U; visible <= 1U; ++visible) {
        reset_fixture(visible != 0U);
        supervisor_menu_action_active = true; /* Outer UART command capture. */
        assert(hide_menu_for_core_action());
        assert(supervisor_menu.visible == expected_visible);
        assert(hide_menu_for_core_action());
        service_runtime();
        supervisor_menu_action_active = false;
        service_supervisor_menu();
        settle_menu();
        assert(keyboard_checks != 0U);
        reset_fixture(visible != 0U);
        dispatch(MT_MENU_ACTION_LOAD_CONTENT);
    }
}
static void failed_hide(void) {
    unsigned visible;
    reset_fixture(true); stall = true;
    pending_menu_action.kind = MT_MENU_ACTION_SWITCH_CORE;
    service_supervisor_menu();
    assert(actions == 0U && errors == 1U);
    assert(now_ms == MT_MENU_HIDE_TIMEOUT_MS);
    assert(supervisor_menu.visible && supervisor_menu_dirty);
    stall = false;
    settle_menu();
    for (visible = 0U; visible <= 1U; ++visible) {
        reset_fixture(visible != 0U);
        status_word = MT_UI_STATUS_READY | MT_UI_STATUS_VISIBLE;
        pending_menu_action.kind = MT_MENU_ACTION_LOAD_CONTENT;
        service_supervisor_menu();
        assert(actions == 0U && errors == 1U);
        assert(supervisor_menu.visible == expected_visible);
        assert(hidden_commits == 0U && !supervisor_menu_action_active);
    }
    reset_fixture(true); supervisor_menu_ready = false;
    assert(hide_menu_for_core_action());
    assert(reads == 0U && writes == 0U && supervisor_menu.visible);
}
static void explicit_exit(void) {
    const uint8_t keys[] = {MT_MENU_HID_ESCAPE, MT_MENU_HID_F12};
    size_t index;
    for (index = 0U; index < sizeof(keys); ++index) {
        reset_fixture(true);
        assert(supervisor_keyboard_filter(NULL, 0U, 0U, &keys[index], 1U));
        expected_visible = false;
        assert(!supervisor_menu.visible);
        settle_menu();
        /* A later command must not reopen an explicitly closed menu. */
        dispatch(MT_MENU_ACTION_LOAD_CONTENT);
    }
    reset_fixture(true);
    mt_menu_open_browser(&supervisor_menu, MT_MENU_BROWSER_DRIVE8);
    assert(supervisor_keyboard_filter(NULL, 0U, 0U, keys, 1U));
    assert(supervisor_menu.visible && supervisor_menu.browser == MT_MENU_BROWSER_NONE);
    settle_menu();
}
static void basic_keyboard_stop(void) {
    const uint8_t game_keys[] = {0x04U, MT_MENU_HID_ENTER, MT_MENU_HID_ESCAPE,
                                MT_MENU_HID_RIGHT};
    const uint8_t stop_key = MT_MENU_HID_F12;
    unsigned visible;
    for (visible = 0U; visible <= 1U; ++visible) {
        reset_fixture(visible != 0U);
        vectrex_basic_running = true;
        supervisor_menu_action_active = true; /* BASIC runs within a content action. */
        assert(!supervisor_keyboard_filter(NULL, 0U, 0U, game_keys, sizeof(game_keys)));
        assert(!vectrex_basic_stop && supervisor_menu.visible == expected_visible);
        assert(supervisor_menu.page == MT_MENU_PAGE_MEDIA && supervisor_menu.row == 2U);
        assert(pending_menu_action.kind == MT_MENU_ACTION_NONE);
        assert(supervisor_keyboard_filter(NULL, 0U, 0U, &stop_key, 1U));
        assert(vectrex_basic_stop && supervisor_menu.visible == expected_visible);
        assert(pending_menu_action.kind == MT_MENU_ACTION_NONE);
        /* Starting another BASIC run while F12 is held must not stop it until
         * a fresh press; reports still update the shared key bitmap. */
        vectrex_basic_stop = false;
        assert(!supervisor_keyboard_filter(NULL, 0U, 0U, &stop_key, 1U));
        assert(!vectrex_basic_stop);
        assert(!supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U));
        assert(supervisor_keyboard_filter(NULL, 0U, 0U, &stop_key, 1U));
        assert(vectrex_basic_stop);
        assert(supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U));
        assert(vectrex_basic_stop && pending_menu_action.kind == MT_MENU_ACTION_NONE);
        assert(supervisor_menu.visible == expected_visible && writes == 0U);
    }
}
static void overlay_upload_stays_paused(void) {
    unsigned result, ready;
    for (result = SUCCESS; result <= FAILURE; ++result) {
        for (ready = 0U; ready <= 1U; ++ready) {
            mt_menu_action_t action = {MT_MENU_ACTION_LOAD_CONTENT, 0U, 0U, 1U};
            reset_fixture(true);
            core_id = supervisor.active.package.core_id = MT_VECTREX_CORE_ID;
            core_ready = ready != 0U;
            outcome = result;
            content_result.status = result == FAILURE ? MT_VECTREX_CONTENT_TRANSFER_FAILED :
                                                       MT_VECTREX_CONTENT_OK;
            mt_menu_open_browser(&supervisor_menu, MT_MENU_BROWSER_OVERLAY);
            supervisor_menu.row = 1U;
            assert(!menu_action_needs_core_progress(action));
            pending_menu_action = action;
            service_supervisor_menu();
            assert(actions == 1U && content_loads == 1U && hidden_commits == 0U);
            assert((status_word & MT_UI_STATUS_VISIBLE) != 0U);
            assert(input_changes == 0U && core_ready == (ready != 0U));
            assert(keyboard_checks >= 2U && !supervisor_menu_action_active);
            assert(pending_menu_action.kind == MT_MENU_ACTION_NONE);
            settle_menu();
            assert(supervisor_menu.browser ==
                (result == SUCCESS ? MT_MENU_BROWSER_NONE : MT_MENU_BROWSER_OVERLAY));
            /* The exemption requires an actual artwork file on live Vectrex. */
            action.file_index = 2U; assert(menu_action_needs_core_progress(action));
            action.file_index = 3U; assert(menu_action_needs_core_progress(action));
            action.file_index = 4U; assert(menu_action_needs_core_progress(action));
            action.file_index = 8U; assert(menu_action_needs_core_progress(action));
            action.file_index = 1U; core_id = MT_GBC_CORE_ID;
            assert(menu_action_needs_core_progress(action));
        }
    }
}
static void failed_content_restores_running_input(void) {
    static const char *const names[] = {"games/deep/missing.vec", "programs/game.bas"};
    unsigned status, ready, reset;
    size_t name;
    for (name = 0U; name < sizeof(names) / sizeof(names[0]); ++name) {
        for (status = MT_VECTREX_CONTENT_OK; status <= MT_VECTREX_CONTENT_OVERLAY_UNSUPPORTED; ++status) {
            for (ready = 0U; ready <= 1U; ++ready) {
                for (reset = 0U; reset <= 1U; ++reset) {
                    mt_vectrex_content_result_t actual;
                    const bool early_failure = status == MT_VECTREX_CONTENT_READ_FAILED ||
                        status == MT_VECTREX_CONTENT_REJECTED ||
                        status == MT_VECTREX_CONTENT_INVALID_ARGUMENT;
                    const bool recover = status == MT_VECTREX_CONTENT_OK ||
                        (early_failure && ready != 0U && reset == 0U);
                    reset_fixture(true);
                    core_status = ready ? MT_STATUS_CORE_READY : 0U;
                    core_control = reset ? MT_CONTROL_CORE_RESET_REQUEST : 0U;
                    content_result.status = (mt_vectrex_content_status_t)status;
                    content_result.sd.status = MT_SD_READ_FAILED;
                    content_result.sd.backend_status = 17U;
                    actual = load_vectrex_content_with_input(names[name]);
                    assert(actual.status == content_result.status);
                    assert(actual.sd.status == MT_SD_READ_FAILED && actual.sd.backend_status == 17U);
                    assert(core_ready == recover && input_changes == (recover ? 2U : 1U));
                    assert(content_loads == 1U);
                    /* Manual artwork, including read/transfer failures, never
                     * changes an existing game's input readiness. */
                    input_changes = 0U; core_ready = ready != 0U;
                    (void)load_vectrex_content_with_input("games/deep/choice.OVR");
                    assert(input_changes == 0U && core_ready == (ready != 0U));
                }
            }
        }
    }
}
static void screenshot_keyboard_and_controller(void) {
    const uint8_t printscreen = 0x46U;
    reset_fixture(false);
    assert(!supervisor_keyboard_filter(NULL, 0U, 0U, &printscreen, 1U));
    assert(screenshot_requests == 1U && output_screenshot_requests == 0U);
    (void)supervisor_keyboard_filter(NULL, 0U, 2U, &printscreen, 1U);
    assert(screenshot_requests == 1U); /* Changing modifiers does not repeat held key. */
    (void)supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U);
    (void)supervisor_keyboard_filter(NULL, 0U, 0x20U, &printscreen, 1U);
    assert(screenshot_requests == 2U && output_screenshot_requests == 1U);
    supervisor_menu_action_active = true;
    (void)supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U);
    assert(supervisor_keyboard_filter(NULL, 0U, 2U, &printscreen, 1U));
    assert(screenshot_requests == 3U && output_screenshot_requests == 2U);
    assert(pending_menu_action.kind == MT_MENU_ACTION_NONE && !supervisor_menu.visible);
    supervisor_menu_action_active = false; vectrex_basic_running = true;
    (void)supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U);
    assert(!supervisor_keyboard_filter(NULL, 0U, 0U, &printscreen, 1U));
    assert(screenshot_requests == 4U && !vectrex_basic_stop);
    supervisor_controller_action(NULL, MT_CONTROL_SCREENSHOT);
    supervisor_controller_action(NULL, MT_CONTROL_SCREENSHOT_OUTPUT);
    supervisor_controller_action(NULL, MT_CONTROL_A);
    assert(screenshot_requests == 6U && output_screenshot_requests == 3U && settings_writes == 0U);
}
static void screenshot_waits_for_displayed_ui_commit(void) {
    reset_fixture(true);
    memcpy(supervisor.active.package.id, "c64", 4U);
    borders_supported = true;
    supervisor_menu_dirty = true;
    service_supervisor_menu(); /* The real presenter submits bank 1 asynchronously. */
    assert(supervisor_presenter.pending && !supervisor_menu_dirty && countdown != 0U);
    const unsigned submitted_writes = writes;
    assert(mt_screenshot_service_request(true));
    service_screenshots();
    assert(screenshot_polls == 0U && supervisor_presenter.pending);
    while (countdown) hardware_tick();
    /* A READY-low status cannot acknowledge an otherwise matching state. */
    status_word &= ~MT_UI_STATUS_READY;
    service_screenshots();
    assert(screenshot_polls == 0U && supervisor_presenter.pending);
    status_word |= MT_UI_STATUS_READY;
    /* Every pending state bit matters, not just the displayed bank. */
    const uint32_t mismatches[] = {MT_UI_STATUS_BANK, MT_UI_STATUS_VISIBLE, MT_UI_STATUS_DENSE};
    for (size_t index = 0U; index < sizeof(mismatches) / sizeof(mismatches[0]); ++index) {
        status_word ^= mismatches[index];
        service_screenshots();
        assert(screenshot_polls == 0U && supervisor_presenter.pending);
        status_word ^= mismatches[index];
    }
    assert(!supervisor_menu_dirty);
    service_screenshots(); /* No edit or presenter call is needed to make progress. */
    const unsigned bank = (status_word & MT_UI_STATUS_BANK) != 0U ? 1U : 0U;
    assert(screenshot_polls == 1U && !supervisor_presenter.pending && screenshot_busy);
    assert(screenshot_ui == &supervisor_presenter.banks[bank] && screenshot_ui_visible);
    assert(screenshot_core_id == MT_C64_CORE_ID && strcmp(screenshot_core_name, "c64") == 0);
    assert(screenshot_display_mode == 2U && screenshot_border == border_config);
    assert(writes == submitted_writes && !supervisor_menu_dirty && screenshot_logs == 0U);
    screenshot_finish = true; screenshot_status = "Screenshot saved: 0:/screenshots/test.bmp";
    service_screenshots();
    assert(!screenshot_busy && screenshot_polls == 2U && screenshot_logs == 1U);
    assert(supervisor_menu_dirty && supervisor_status_color == MT_UI_COLOR_TEXT);
    assert(strcmp(reported_status, screenshot_status) == 0);
    service_screenshots(); assert(screenshot_polls == 2U && screenshot_logs == 1U);

    for (unsigned selected_bank = 0U; selected_bank < 2U; ++selected_bank) {
        for (unsigned visible = 0U; visible < 2U; ++visible) {
            reset_fixture(visible == 0U); /* Logical menu state deliberately differs. */
            supervisor.has_active = false;
            status_word = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT | MT_UI_STATUS_DENSE |
                (selected_bank ? MT_UI_STATUS_BANK : 0U) | (visible ? MT_UI_STATUS_VISIBLE : 0U);
            supervisor_presenter.bank_valid[selected_bank] = true;
            (void)mt_screenshot_service_request(true);
            service_screenshots();
            assert(screenshot_ui == &supervisor_presenter.banks[selected_bank]);
            assert(screenshot_ui_visible == (visible != 0U));
            assert(screenshot_core_id == 0U && strcmp(screenshot_core_name, "supervisor") == 0);
            assert(screenshot_border == 0U);
            supervisor_presenter.bank_valid[selected_bank] = false;
            screenshot_finish = true; screenshot_status = "Screenshot failed: SD full";
            service_screenshots();
            assert(!screenshot_ui_visible && !screenshot_busy && screenshot_logs == 1U);
            assert(supervisor_status_color == MT_UI_COLOR_WARNING && strcmp(reported_status, screenshot_status) == 0);
        }
    }
}
static void connect_fixture(unsigned slot, uint32_t serial) {
    mt_controller_device_t *device = &devices[slot];
    memset(device, 0, sizeof(*device));
    device->connected = 1U; device->home_button = 10U; device->buttons_present = UINT32_MAX;
    device->identity.vendor_id = 0x045eU; device->identity.product_id = 0x02eaU;
    device->identity.descriptor_hash = 101U; device->identity.serial_hash = serial;
    device->identity.port_path = slot + 1U; device->identity.transport = MT_CONTROLLER_TRANSPORT_XBOXONE;
    raw_controllers[slot].connected = 1U; raw_controllers[slot].hat = 8U;
    mt_controls_default(supervisor.active.package.core_id, device, &controller_configs[slot]);
}
static void raw_capture_is_immediate_and_held_safe(void) {
    reset_fixture(true); connect_fixture(0U, 1U); connect_fixture(1U, 2U);
    const mt_menu_action_t edit = {MT_MENU_ACTION_EDIT_GAMEPADS, 0U, 0U, 0U};
    controller_edit_action(edit); assert(supervisor_menu.gamepad_edit);
    supervisor_menu.gamepad_control = MT_CONTROL_A; supervisor_menu.gamepad_capture = true;
    supervisor_menu.gamepad_capture_started = now_ms;
    controller_preview_capture(); assert(gamepad_preview);
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(supervisor_menu.gamepad_capture_armed);
    raw_controllers[1].buttons = UINT32_C(1) << 30U;
    supervisor_controller_report(NULL, 1U, &raw_controllers[1]);
    assert(supervisor_menu.gamepad_capture); /* Other controllers cannot satisfy capture. */
    raw_controllers[0].buttons = UINT32_C(1) << 31U;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(!supervisor_menu.gamepad_capture && supervisor_menu.gamepad_config[0].binding[MT_CONTROL_A] == 32U);
    assert(gamepad_preview && supervisor_menu_dirty); /* Same report must not synthesize Enter. */
    raw_controllers[0].buttons = 0U;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(!gamepad_preview && settings_writes == 0U && config_applies == 0U);
    assert(controller_configs[0].binding[MT_CONTROL_A] == 1U); /* Draft stays private until save. */
    supervisor_menu.gamepad_capture = true; supervisor_menu.gamepad_capture_armed = false;
    supervisor_menu_action_active = true;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(!supervisor_menu.gamepad_capture_armed);
}
static void stick_settings_accept_controller_navigation(void) {
    reset_fixture(true); connect_fixture(0U, 1U);
    const mt_menu_action_t edit = {MT_MENU_ACTION_EDIT_GAMEPADS, 0U, 0U, 0U};
    controller_edit_action(edit);
    supervisor_menu.gamepad_sticks = true;
    supervisor_menu.gamepad_control = MT_GAMEPAD_AXIS_RX;
    supervisor_menu.gamepad_field = 2U;
    controller_preview_capture();
    assert(!gamepad_preview); /* Tuning must leave controller navigation enabled. */

    raw_controllers[0].hat = 2U;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(!gamepad_preview && supervisor_menu.gamepad_sticks);
    const uint8_t right[] = {MT_MENU_HID_RIGHT};
    assert(supervisor_keyboard_filter(NULL, 0U, 0U, right, sizeof(right)));
    assert(supervisor_menu.gamepad_config[0].axis[MT_GAMEPAD_AXIS_RX].deadzone_percent == 11U);
    assert(supervisor_menu.gamepad_config[0].axis[MT_GAMEPAD_AXIS_X].deadzone_percent == 10U);
    assert(controller_configs[0].axis[MT_GAMEPAD_AXIS_RX].deadzone_percent == 10U);
    assert(settings_writes == 0U && config_applies == 0U);

    raw_controllers[0].hat = 8U;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    (void)supervisor_keyboard_filter(NULL, 0U, 0U, NULL, 0U);
    const uint8_t back[] = {MT_MENU_HID_ESCAPE};
    assert(supervisor_keyboard_filter(NULL, 0U, 0U, back, sizeof(back)));
    assert(!supervisor_menu.gamepad_sticks && supervisor_menu.gamepad_edit && supervisor_menu.visible);
    assert(!gamepad_preview && settings_writes == 0U && config_applies == 0U);
}
static void menu_chord_does_not_assign_controller_controls(void) {
    reset_fixture(true); connect_fixture(0U, 1U);
    const mt_menu_action_t edit = {MT_MENU_ACTION_EDIT_GAMEPADS, 0U, 0U, 0U};
    controller_edit_action(edit);
    supervisor_menu.gamepad_control = MT_CONTROL_A;
    supervisor_menu.gamepad_capture = supervisor_menu.gamepad_capture_armed = true;
    supervisor_menu.gamepad_previous = raw_controllers[0];
    const mt_controller_config_t before = supervisor_menu.gamepad_config[0];
    raw_controllers[0].buttons = 0xc0U;
    controller_menu_chords[0] = true;

    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(supervisor_menu.gamepad_capture && !supervisor_menu.gamepad_conflict);
    assert(memcmp(&before, &supervisor_menu.gamepad_config[0], sizeof(before)) == 0);
    service_supervisor_menu(); /* Periodic snapshots must reserve the pair too. */
    assert(supervisor_menu.gamepad_capture && !supervisor_menu.gamepad_conflict);
    assert(memcmp(&before, &supervisor_menu.gamepad_config[0], sizeof(before)) == 0);
    assert(settings_writes == 0U && config_applies == 0U);

    controller_menu_chords[0] = false;
    raw_controllers[0].buttons = 0U;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    raw_controllers[0].buttons = UINT32_C(1) << 31U;
    supervisor_controller_report(NULL, 0U, &raw_controllers[0]);
    assert(!supervisor_menu.gamepad_capture &&
        supervisor_menu.gamepad_config[0].binding[MT_CONTROL_A] == 32U);
}
static void controller_profiles_and_legacy_migration(void) {
    reset_fixture(true); connect_fixture(0U, 11U); connect_fixture(1U, 22U);
    mt_controller_config_t config;
    persistent_settings.gamepads[0].button[0] = 4U;
    persistent_settings.legacy_gamepad_pending = 1U;
    assert(supervisor_controller_config(NULL, MT_SUPERVISOR_CORE_ID, 0U, &devices[0], &config));
    assert(persistent_settings.controller_profile_count == 0U && persistent_settings.legacy_gamepad_pending == 1U);
    assert(supervisor_controller_config(NULL, MT_C64_CORE_ID, 0U, &devices[0], &config));
    assert(config.binding[MT_CONTROL_A] == 5U && persistent_settings.controller_profile_count == 6U);
    assert(mt_supervisor_settings_find_controller(&persistent_settings, MT_IIGS_CORE_ID, &devices[0].identity) != NULL);
    assert(mt_supervisor_settings_find_controller(&persistent_settings, MT_NES_CORE_ID, &devices[0].identity) != NULL);
    assert(persistent_settings.legacy_gamepad_pending == 0U && settings_writes == 0U);
    config.binding[MT_CONTROL_A] = 32U;
    assert(mt_supervisor_settings_remember_controller(&persistent_settings, MT_C64_CORE_ID, &devices[0].identity, &config));
    mt_controller_device_t moved = devices[0]; moved.identity.port_path = 88U;
    assert(supervisor_controller_config(NULL, MT_C64_CORE_ID, 7U, &moved, &config));
    assert(config.binding[MT_CONTROL_A] == 32U);
    assert(supervisor_controller_config(NULL, MT_GENESIS_CORE_ID, 7U, &moved, &config));
    assert(config.binding[MT_CONTROL_A] != 32U);
    assert(supervisor_controller_config(NULL, MT_C64_CORE_ID, 0U, &devices[1], &config));
    assert(config.binding[MT_CONTROL_A] == 1U); /* Same model, different serial. */
    assert(!supervisor_controller_config(NULL, MT_C64_CORE_ID, MT_GAMEPAD_SLOTS, &devices[0], &config));
    assert(save_persistent_settings());
    mt_supervisor_settings_defaults(&persistent_settings);
    assert(mt_supervisor_settings_load(&persistent_settings).status == MT_SETTINGS_OK);
    assert(supervisor_controller_config(NULL, MT_C64_CORE_ID, 3U, &moved, &config));
    assert(config.binding[MT_CONTROL_A] == 32U);
}
static void save_drafts_atomically_and_freeze_identity(void) {
    const mt_menu_action_t edit = {MT_MENU_ACTION_EDIT_GAMEPADS, 0U, 0U, 0U};
    const mt_menu_action_t save = {MT_MENU_ACTION_SET_GAMEPADS, 0U, 0U, 0U};
    reset_fixture(true); connect_fixture(0U, 11U); connect_fixture(1U, 22U);
    controller_configs[0].assignment = MT_CONTROLLER_PORT1;
    controller_configs[1].assignment = MT_CONTROLLER_PORT2;
    controller_edit_action(edit);
    supervisor_menu.gamepad_config[0].assignment = MT_CONTROLLER_PORT2;
    supervisor_menu.gamepad_config[1].assignment = MT_CONTROLLER_PORT1;
    supervisor_menu.gamepad_config[0].binding[MT_CONTROL_A] = 32U;
    save_sd_fail = true; controller_edit_action(save);
    assert(errors == 1U && supervisor_menu.gamepad_edit && config_applies == 0U);
    assert(persistent_settings.controller_profile_count == 0U && mt_menu_gamepad_dirty(&supervisor_menu));
    assert(controller_configs[0].assignment == MT_CONTROLLER_PORT1);
    save_sd_fail = false; controller_edit_action(save);
    assert(config_applies == 1U && !supervisor_menu.gamepad_edit);
    assert(controller_configs[0].assignment == MT_CONTROLLER_PORT2 && controller_configs[1].assignment == MT_CONTROLLER_PORT1);
    mt_supervisor_settings_defaults(&persistent_settings);
    assert(mt_supervisor_settings_load(&persistent_settings).status == MT_SETTINGS_OK);
    assert(persistent_settings.controller_profile_count == 2U);
    controller_edit_action(edit);
    supervisor_menu.gamepad_config[0].binding[MT_CONTROL_A] = 31U;
    const mt_controller_identity_t edited_identity = devices[0].identity;
    connect_fixture(0U, 33U);
    const mt_menu_view_t replaced = supervisor_menu_view();
    assert(!replaced.gamepad_devices[0].connected && replaced.gamepad_devices[0].identity.serial_hash == 11U);
    assert(!replaced.gamepad_preview.connected && strstr(supervisor_menu.gamepad_notice, "replaced") != NULL);
    controller_edit_action(save);
    assert(config_applies == 2U && controller_configs[0].binding[MT_CONTROL_A] == 1U);
    const mt_controller_settings_profile_t *old = mt_supervisor_settings_find_controller(&persistent_settings, MT_C64_CORE_ID, &edited_identity);
    assert(old && old->config.binding[MT_CONTROL_A] == 31U);
    assert(mt_supervisor_settings_find_controller(&persistent_settings, MT_C64_CORE_ID, &devices[0].identity) == NULL);
    controller_edit_action(edit);
    const unsigned saved_before = settings_writes;
    supervisor.active.package.core_id = MT_GENESIS_CORE_ID;
    controller_edit_action(save);
    assert(errors == 2U && settings_writes == saved_before && config_applies == 2U);
}
static void about_refreshes_live_status(void) {
    reset_fixture(true);
    i2c_ready = true;board_temperature_valid = false;last_system_refresh_ms = 0U;
    board_refreshes = temperature_reads = update_file_refreshes = 0U;
    supervisor_menu.page = MT_MENU_PAGE_ABOUT;now_ms = 1000U;
    service_system_page();
    assert(board_temperature_valid && board_temperature_micro_c == 25500000 &&
        temperature_reads == 1U && board_refreshes == 1U && update_file_refreshes == 1U && supervisor_menu_dirty);
    supervisor_menu_dirty = false;now_ms = 1999U;
    service_system_page();assert(temperature_reads == 1U && !supervisor_menu_dirty);
    now_ms = 2000U;service_system_page();
    assert(temperature_reads == 2U && update_file_refreshes == 2U && supervisor_menu_dirty);
    supervisor_menu.page = MT_MENU_PAGE_SYSTEM;now_ms = 3000U;service_system_page();
    assert(temperature_reads == 3U);
    supervisor_menu.page = MT_MENU_PAGE_MEDIA;service_system_page();
    assert(temperature_reads == 3U && last_system_refresh_ms == 0U);
    supervisor_menu.page = MT_MENU_PAGE_ABOUT;supervisor_menu.visible = false;
    service_system_page();assert(temperature_reads == 3U);
    supervisor_menu.visible = true;supervisor_menu.browser = MT_MENU_BROWSER_DOCUMENTS;
    service_system_page();assert(temperature_reads == 3U);
    supervisor_menu.browser = MT_MENU_BROWSER_NONE;supervisor_menu.clock_edit = true;
    service_system_page();assert(temperature_reads == 3U);
    supervisor_menu.clock_edit = false;service_system_page();
    assert(temperature_reads == 4U && board_refreshes == 2U);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "actions") == 0) all_actions();
    else if (strcmp(argv[1], "outcomes") == 0) result_and_browser_retention();
    else if (strcmp(argv[1], "deferred") == 0) delayed_and_nested();
    else if (strcmp(argv[1], "uart") == 0) uart_and_hidden();
    else if (strcmp(argv[1], "failure") == 0) failed_hide();
    else if (strcmp(argv[1], "exit") == 0) explicit_exit();
    else if (strcmp(argv[1], "basic") == 0) basic_keyboard_stop();
    else if (strcmp(argv[1], "overlay") == 0) overlay_upload_stays_paused();
    else if (strcmp(argv[1], "content-input") == 0) failed_content_restores_running_input();
    else if (strcmp(argv[1], "screenshots") == 0) screenshot_keyboard_and_controller();
    else if (strcmp(argv[1], "screenshot-presenter") == 0) screenshot_waits_for_displayed_ui_commit();
    else if (strcmp(argv[1], "raw-capture") == 0) raw_capture_is_immediate_and_held_safe();
    else if (strcmp(argv[1], "stick-navigation") == 0) stick_settings_accept_controller_navigation();
    else if (strcmp(argv[1], "capture-chord") == 0) menu_chord_does_not_assign_controller_controls();
    else if (strcmp(argv[1], "controller-profiles") == 0) controller_profiles_and_legacy_migration();
    else if (strcmp(argv[1], "controller-save") == 0) save_drafts_atomically_and_freeze_identity();
    else if (strcmp(argv[1], "about-refresh") == 0) about_refreshes_live_status();
    else assert(false);
    printf("PASS: menu runtime %s\n", argv[1]);
    return 0;
}
'''


class MenuRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if compiler is None:
            raise unittest.SkipTest("A host C compiler is required for menu runtime checks")
        cls.temporary = tempfile.TemporaryDirectory(prefix="multitini-menu-runtime-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        signatures = (
            "static bool key_bitmap_get(",
            "static void controller_preview_capture(void)",
            "static void supervisor_controller_report(void *context,",
            "static void supervisor_controller_action(void *context,",
            "static bool supervisor_controller_config(void *context,",
            "static bool supervisor_keyboard_filter(void *context,",
            "static bool hide_menu_for_core_action(void)\n{",
            "static bool menu_action_needs_core_progress(",
            "static mt_vectrex_content_result_t load_vectrex_content_with_input(",
            "static void service_supervisor_menu(void)\n{",
            "static void service_screenshots(void)\n{",
            "static void service_system_page(void)\n{",
        )
        view_start = source.index("    view.gamepad_core_id = state.core_id;")
        view_end = source.index("    view.status_text =", view_start)
        controller_view = source[view_start:view_end].replace(
            "state.core_id", "supervisor.has_active ? supervisor.active.package.core_id : 0U")
        action_start = source.index("    } else if (action.kind == MT_MENU_ACTION_EDIT_GAMEPADS)")
        action_end = source.index("    } else if (action.kind == MT_MENU_ACTION_SET_CLOCK)", action_start)
        controller_actions = "static void controller_edit_action(mt_menu_action_t action) {\n" + \
            source[action_start:action_end].replace("    } else if", "    if", 1) + "    }\n}\n"
        harness = directory / "menu_runtime.c"
        harness.write_text(PREAMBLE.replace("    /* CONTROLLER_VIEW_FROM_PRODUCTION */", controller_view) + "\n" + "\n\n".join(
            production_function(source, signature) for signature in signatures
        ) + "\n" + controller_actions + "\n" + SCENARIOS)
        cls.executable = directory / ("menu_runtime.exe" if os.name == "nt" else "menu_runtime")
        compile_result = subprocess.run([
            sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-I", str(ROOT / "firmware/include"), str(harness),
            "-I", str(ROOT / "firmware/platform/xilinx"),
            str(ROOT / "firmware/common/display.c"), str(ROOT / "firmware/common/menu.c"), str(ROOT / "firmware/common/gamepad.c"), str(ROOT / "firmware/common/controls.c"), str(ROOT / "firmware/common/ui.c"),
            str(ROOT / "firmware/common/text_reader.c"), str(ROOT / "firmware/common/clock.c"),
            str(ROOT / "firmware/common/tmp102.c"),
            str(ROOT / "firmware/common/content_type.c"),
            str(ROOT / "firmware/common/genesis_content.c"),
            str(ROOT / "firmware/common/nes_content.c"),
            str(ROOT / "firmware/common/vectrex_overlay.c"),
            str(ROOT / "firmware/common/core_link.c"),
            str(ROOT / "firmware/common/video_effects.c"),
            str(ROOT / "firmware/platform/xilinx/supervisor_settings.c"),
            "-lm", "-o", str(cls.executable),
        ], capture_output=True, text=True, timeout=60)
        if compile_result.returncode:
            raise AssertionError(compile_result.stdout + compile_result.stderr)

    def run_scenario(self, scenario):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"),
                                 "--", str(self.executable), scenario], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS: menu runtime {scenario}", result.stdout)

    def test_every_action_retains_requested_visibility(self):
        self.run_scenario("actions")

    def test_success_warning_failure_and_core_rollback_keep_menu_context(self):
        self.run_scenario("outcomes")

    def test_pending_bank_and_nested_hides_wait_then_restore_at_video_boundary(self):
        self.run_scenario("deferred")

    def test_uart_and_hidden_start_preserve_visibility_and_capture_keyboard(self):
        self.run_scenario("uart")

    def test_failed_hide_cancels_action_without_changing_requested_visibility(self):
        self.run_scenario("failure")

    def test_explicit_escape_and_f12_remain_closed(self):
        self.run_scenario("exit")

    def test_basic_game_keys_pass_through_and_only_fresh_f12_requests_stop(self):
        self.run_scenario("basic")

    def test_manual_overlay_upload_keeps_game_paused_and_blocks_menu_reentry(self):
        self.run_scenario("overlay")

    def test_failed_load_restores_input_only_for_a_preserved_running_machine(self):
        self.run_scenario("content-input")

    def test_screenshot_keys_and_controller_actions_queue_once(self):
        self.run_scenario("screenshots")

    def test_screenshot_waits_for_ack_then_uses_displayed_ui_without_menu_edit(self):
        self.run_scenario("screenshot-presenter")

    def test_raw_capture_handles_quick_presses_before_navigation(self):
        self.run_scenario("raw-capture")

    def test_stick_settings_allow_controller_navigation_without_applying_draft(self):
        self.run_scenario("stick-navigation")

    def test_menu_chord_is_reserved_during_immediate_and_periodic_capture(self):
        self.run_scenario("capture-chord")

    def test_controller_profiles_follow_serial_and_keep_cores_independent(self):
        self.run_scenario("controller-profiles")

    def test_save_failure_assignment_swap_and_replaced_controller(self):
        self.run_scenario("controller-save")

    def test_about_retains_live_one_second_status_refresh(self):
        self.run_scenario("about-refresh")


if __name__ == "__main__":
    unittest.main()
