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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MT_MENU_HIDE_TIMEOUT_MS UINT64_C(1000)
static mt_menu_t supervisor_menu;
static mt_ui_frame_t supervisor_frame;
static mt_ui_presenter_t supervisor_presenter;
static mt_supervisor_t supervisor;
static mt_menu_action_t pending_menu_action;
static bool supervisor_menu_ready, supervisor_menu_dirty;
static bool supervisor_menu_action_active;
static bool vectrex_basic_running, vectrex_basic_stop;
static uint8_t supervisor_menu_keys[MT_C64_HID_SLOT_COUNT][MT_C64_HID_USAGE_BYTES];
static mt_ui_color_t supervisor_status_color;
static uint64_t now_ms;
static uint32_t status_word, committed_status;
static unsigned commits, hidden_commits, shown_commits, writes, runtime_polls;
static unsigned countdown, errors, reads, actions, outcome, keyboard_checks;
static bool stall, nested_hide, expected_visible;
static const mt_menu_file_t files[8] = {{"one.d64", 1U, 0U}};
static const mt_menu_item_t media_items[5] = {
    {"Disk", "mounted", "", MT_MENU_ACTION_BROWSE_DRIVE8, 0U, true, MT_UI_COLOR_TEXT}
};
static const mt_core_selection_t cores[2] = {0};
static uint64_t monotonic_ms(void *context) {(void)context; return now_ms;}
static void usleep(unsigned microseconds) {now_ms += microseconds / 1000U;}
static uint32_t shell_read(uint32_t offset) {
    assert(offset == MT_REG_UI_STATUS); ++reads; return status_word;
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
    view.cores = cores; view.core_count = 2U;
    view.files = files; view.file_count = 8U;
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
    (void)text; supervisor_status_color = MT_UI_COLOR_TEXT;
    supervisor_menu_dirty = true;
}
static void set_supervisor_error(const char *text) {
    (void)text; ++errors; supervisor_status_color = MT_UI_COLOR_RED;
    supervisor_menu_dirty = true;
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
    mt_ui_presenter_init(&supervisor_presenter);
    supervisor_menu_ready = true; supervisor_menu_dirty = false;
    supervisor_menu_action_active = false;
    vectrex_basic_running = vectrex_basic_stop = false;
    supervisor_menu.visible = expected_visible = visible;
    supervisor_menu.page = MT_MENU_PAGE_MEDIA;
    supervisor_menu.row = 2U;
    supervisor.has_active = true;
    supervisor.active.package.core_id = MT_C64_CORE_ID;
    supervisor_status_color = MT_UI_COLOR_TEXT;
    now_ms = commits = hidden_commits = shown_commits = writes = 0U;
    runtime_polls = countdown = errors = reads = actions = keyboard_checks = 0U;
    stall = nested_hide = false; outcome = SUCCESS;
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
    case MT_MENU_ACTION_REBOOT_GOLDEN: return true;
    default: return false;
    }
}
static void handle_menu_action(mt_menu_action_t action) {
    ++actions;
    assert(supervisor_menu.visible == expected_visible);
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
        service_runtime();
    }
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
    for (kind = MT_MENU_ACTION_NONE; kind <= MT_MENU_ACTION_SET_BORDER; ++kind) {
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
int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "actions") == 0) all_actions();
    else if (strcmp(argv[1], "outcomes") == 0) result_and_browser_retention();
    else if (strcmp(argv[1], "deferred") == 0) delayed_and_nested();
    else if (strcmp(argv[1], "uart") == 0) uart_and_hidden();
    else if (strcmp(argv[1], "failure") == 0) failed_hide();
    else if (strcmp(argv[1], "exit") == 0) explicit_exit();
    else if (strcmp(argv[1], "basic") == 0) basic_keyboard_stop();
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
            "static bool supervisor_keyboard_filter(void *context,",
            "static bool hide_menu_for_core_action(void)\n{",
            "static bool menu_action_needs_core_progress(",
            "static void service_supervisor_menu(void)\n{",
        )
        harness = directory / "menu_runtime.c"
        harness.write_text(PREAMBLE + "\n" + "\n\n".join(
            production_function(source, signature) for signature in signatures
        ) + "\n" + SCENARIOS)
        cls.executable = directory / ("menu_runtime.exe" if os.name == "nt" else "menu_runtime")
        compile_result = subprocess.run([
            sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-I", str(ROOT / "firmware/include"), str(harness),
            str(ROOT / "firmware/common/menu.c"), str(ROOT / "firmware/common/ui.c"),
            "-o", str(cls.executable),
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


if __name__ == "__main__":
    unittest.main()
