/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_system.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static size_t find_item(const mt_menu_view_t *view, const char *label)
{
    size_t i;
    for (i = 0U; i < view->system_item_count; ++i)
        if (strcmp(view->system_items[i].label, label) == 0) return i;
    fprintf(stderr, "FAIL: missing System item %s\n", label);
    exit(1);
}

static const mt_menu_item_t *item(const mt_menu_view_t *view, const char *label)
{
    return &view->system_items[find_item(view, label)];
}

static void expect_value(const mt_menu_view_t *view, const char *label,
                         const char *value)
{
    const char *const actual = item(view, label)->value;
    if (strcmp(actual, value) != 0) {
        fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", label, value, actual);
        exit(1);
    }
}

static bool frame_contains(const mt_ui_frame_t *frame, const char *needle)
{
    size_t y, x;
    char line[MT_UI_COLUMNS + 1U];
    for (y = 0U; y < MT_UI_ROWS; ++y) {
        for (x = 0U; x < MT_UI_COLUMNS; ++x)
            line[x] = mt_ui_character(frame, x, y);
        line[MT_UI_COLUMNS] = '\0';
        if (strstr(line, needle) != NULL) return true;
    }
    return false;
}

static mt_menu_t focus_item(const mt_menu_view_t *view, const char *label)
{
    mt_menu_t menu;
    mt_menu_init(&menu, view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_SYSTEM;
    menu.row = find_item(view, label);
    menu.first_row = menu.row;
    return menu;
}

static void test_actions_and_standby(void)
{
    static const struct {
        const char *label;
        mt_menu_action_kind_t action;
    } actions[] = {
        {"Restart active core", MT_MENU_ACTION_RESET_CORE},
        {"Autoboot current core", MT_MENU_ACTION_TOGGLE_AUTO_BOOT},
        {"Read documents", MT_MENU_ACTION_BROWSE_DOCUMENTS},
        {"Date and time", MT_MENU_ACTION_EDIT_CLOCK},
        {"Restart USB", MT_MENU_ACTION_RESTART_USB},
        {"Golden updater", MT_MENU_ACTION_REBOOT_GOLDEN}
    };
    mt_system_frontend_t frontend;
    mt_system_snapshot_t snapshot = {0};
    mt_menu_view_t view = {0};
    size_t i;
    view.status_text = "Status owned by runtime";
    view.content_root = "/core/content";
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    check(view.system_items == frontend.items && view.system_item_count == 16U,
          "System descriptors fill the shared view");
    check(!item(&view, "Restart active core")->enabled &&
          !item(&view, "Autoboot current core")->enabled,
          "standby disables actions that require an active core");
    expect_value(&view, "Restart active core", "No active core");
    check(item(&view, "Autoboot current core")->control == MT_MENU_CONTROL_CHECKBOX &&
          item(&view, "Autoboot current core")->control_value == 0U,
          "autoboot is an unchecked descriptor control");
    for (i = 0U; i < sizeof(actions) / sizeof(actions[0]); ++i)
        check(item(&view, actions[i].label)->action == actions[i].action,
              "System actions retain their dispatcher contracts");
    for (i = 0U; i < view.system_item_count; ++i) {
        const mt_menu_item_t *const current = &view.system_items[i];
        check(current->help != NULL && current->help[0] != '\0',
              "all System items have contextual help");
        if (current->action == MT_MENU_ACTION_NONE)
            check(!current->enabled, "status rows are read-only");
    }
    view.active_core_id = "c64";
    view.settings_auto_boot = true;
    snapshot.clock_available = true;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    check(item(&view, "Autoboot current core")->enabled &&
          item(&view, "Autoboot current core")->control_value == 1U,
          "active core enables the checked autoboot control");
    check(strcmp(view.status_text, "Status owned by runtime") == 0 &&
          strcmp(view.content_root, "/core/content") == 0,
          "board descriptors preserve core/runtime view fields");
    for (i = 0U; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        mt_menu_t menu = focus_item(&view, actions[i].label);
        const mt_menu_action_t action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
        if (actions[i].action == MT_MENU_ACTION_EDIT_CLOCK)
            check(menu.clock_edit && action.kind == MT_MENU_ACTION_NONE,
                  "clock row opens the local editor without writing the RTC");
        else
            check(action.kind == actions[i].action,
                  "System descriptors dispatch through the shared menu");
    }
    view.active_core_id = "";
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    {
        mt_menu_t menu = focus_item(&view, "Restart active core");
        check(mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true).kind == MT_MENU_ACTION_NONE,
              "empty core ID cannot issue a reset");
    }
    mt_system_frontend_apply(NULL, &snapshot, &view);
    mt_system_frontend_apply(&frontend, NULL, &view);
    mt_system_frontend_apply(&frontend, &snapshot, NULL);
}

static void test_snapshot_values(void)
{
    mt_system_frontend_t frontend;
    mt_system_snapshot_t snapshot = {0};
    mt_menu_view_t view = {0};
    const mt_clock_time_t time = {2028U, 2U, 29U, 23U, 59U, 58U};
    snapshot.firmware_build = UINT32_C(0x20260918);
    snapshot.shell_build = UINT32_C(0xDEADBEEF);
    snapshot.abi = UINT32_C(0x00010013);
    snapshot.uptime_seconds = UINT64_C(90061);
    snapshot.clock_available = snapshot.clock_valid = true;
    snapshot.clock = time;
    snapshot.usb_started = true;
    snapshot.keyboards = 2U;
    snapshot.gamepads = 3U;
    snapshot.usb_submit_errors = UINT32_MAX;
    snapshot.usb_transfer_errors = 7U;
    snapshot.usb_input_errors = 9U;
    snapshot.usb_last_error = -1008;
    snapshot.catalog_status = "ok";
    snapshot.catalog_count = 3U;
    snapshot.catalog_rejected = 1U;
    snapshot.dac_ready = true;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Firmware build", "20260918");
    expect_value(&view, "FPGA build", "DEADBEEF");
    expect_value(&view, "Interface", "1.19");
    expect_value(&view, "Uptime", "1 d  01:01:01");
    expect_value(&view, "Date and time", "2028-02-29  23:59:58");
    expect_value(&view, "USB devices", "Running; 2 keyboard(s), 3 pad(s)");
    expect_value(&view, "USB errors", "4294967295 / 7 / 9; last -1008");
    check(item(&view, "USB errors")->value_color == MT_UI_COLOR_WARNING,
          "USB failure receives a warning color");
    expect_value(&view, "SD core catalog", "ok; 3 cores, 1 rejected");
    expect_value(&view, "Audio output", "Ready");
    check(view.clock_available && view.clock_valid && view.clock.year == 2028U &&
          view.clock.day == 29U && view.clock.second == 58U,
          "clock snapshot reaches the editor view");

    snapshot.clock_valid = false;
    snapshot.audio_muted = true;
    snapshot.usb_last_error = 0;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Date and time", "Clock needs setting");
    expect_value(&view, "Audio output", "Muted");
    check(item(&view, "Date and time")->enabled && !view.clock_valid,
          "invalid but reachable clock can be set");
    check(item(&view, "USB errors")->value_color == MT_UI_COLOR_TEXT,
          "new snapshots clear stale USB warning colors");
    snapshot.clock_available = false;
    snapshot.dac_ready = snapshot.usb_started = false;
    snapshot.catalog_status = NULL;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Date and time", "Clock unavailable");
    expect_value(&view, "Audio output", "DAC unavailable");
    expect_value(&view, "USB devices", "Stopped; 2 keyboard(s), 3 pad(s)");
    expect_value(&view, "SD core catalog", "Unknown; 3 cores, 1 rejected");
    check(!item(&view, "Date and time")->enabled && !view.clock_available,
          "unreachable clock does not offer a hardware write");
}

static void test_temperatures_and_save_warnings(void)
{
    static const struct { int32_t value; const char *text; } cases[] = {
        {0, "0.00 C"}, {-62500, "-0.06 C"}, {25625000, "25.62 C"},
        {INT32_MIN, "-2147.48 C"}, {INT32_MAX, "2147.48 C"}
    };
    mt_system_frontend_t frontend;
    mt_system_snapshot_t snapshot = {0};
    mt_menu_view_t view = {0};
    size_t i;
    snapshot.temperature_valid = true;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        snapshot.temperature_micro_c = cases[i].value;
        mt_system_frontend_apply(&frontend, &snapshot, &view);
        expect_value(&view, "Board temperature", cases[i].text);
    }
    snapshot.temperature_valid = false;
    view.save_faulted = view.settings_faulted = view.save_dirty = true;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Board temperature", "Unavailable");
    expect_value(&view, "Saved data", "Save failed");
    check(item(&view, "Saved data")->value_color == MT_UI_COLOR_WARNING,
          "save failure is highlighted");
    view.save_faulted = false;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Saved data", "Settings need attention");
    view.settings_faulted = false;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Saved data", "Save pending");
    check(item(&view, "Saved data")->value_color == MT_UI_COLOR_WARNING,
          "pending save remains highlighted");
    view.save_dirty = false;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    expect_value(&view, "Saved data", "No pending warnings");
    check(item(&view, "Saved data")->value_color == MT_UI_COLOR_TEXT,
          "successful save clears previous warnings");
}

static void test_rendered_system_page(void)
{
    static const char *const labels[] = {
        "Read documents", "Firmware build", "USB errors", "Saved data", "Restart USB"
    };
    mt_system_frontend_t frontend;
    mt_system_snapshot_t snapshot = {0};
    mt_menu_view_t view = {0};
    mt_ui_frame_t frame;
    mt_menu_t menu;
    size_t i;
    snapshot.firmware_build = UINT32_C(0x20260918);
    snapshot.usb_last_error = -1008;
    view.active_core_id = "c64";
    view.settings_auto_boot = true;
    view.save_faulted = true;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    for (i = 0U; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        menu = focus_item(&view, labels[i]);
        mt_menu_render(&menu, &view, &frame);
        check(frame_contains(&frame, labels[i]) &&
              frame_contains(&frame, item(&view, labels[i])->value),
              "shared renderer shows actual System descriptors and values");
    }
    menu = focus_item(&view, "Autoboot current core");
    mt_menu_render(&menu, &view, &frame);
    check(frame_contains(&frame, "[x]"), "active autoboot renders a checked control");
    view.settings_auto_boot = false;
    view.active_core_id = NULL;
    mt_system_frontend_apply(&frontend, &snapshot, &view);
    mt_menu_render(&menu, &view, &frame);
    check(frame_contains(&frame, "[ ]"), "standby autoboot renders an unchecked control");
}

int main(void)
{
    test_actions_and_standby();
    test_snapshot_values();
    test_temperatures_and_save_warnings();
    test_rendered_system_page();
    puts("PASS: System descriptors, standby actions, clock, temperature, USB, saves and rendered controls");
    return 0;
}
