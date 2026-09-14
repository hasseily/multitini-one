/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void row_text(const mt_ui_frame_t *frame, size_t row, char *text)
{
    size_t column;
    for (column = 0U; column < MT_UI_COLUMNS; ++column)
        text[column] = mt_ui_character(frame, column, row);
    text[MT_UI_COLUMNS] = '\0';
}

static bool contains(const mt_ui_frame_t *frame, const char *needle)
{
    size_t row;
    char text[MT_UI_COLUMNS + 1U];
    for (row = 0U; row < MT_UI_ROWS; ++row) {
        row_text(frame, row, text);
        if (strstr(text, needle) != NULL) return true;
    }
    return false;
}

static mt_menu_action_t press(mt_menu_t *menu, mt_menu_view_t *view, uint8_t key)
{
    return mt_menu_key(menu, view, key, true);
}

static void init_core(mt_core_selection_t *core, unsigned index)
{
    memset(core, 0, sizeof(*core));
    (void)snprintf(core->package.id, sizeof(core->package.id), "core%u", index);
    (void)snprintf(core->package.name, sizeof(core->package.name),
                   "Example system %02u", index);
    strcpy(core->package.variants[0].id, "pal");
    strcpy(core->package.variants[0].label, "PAL 50 Hz");
    strcpy(core->package.variants[1].id, "ntsc");
    strcpy(core->package.variants[1].label, "NTSC 60 Hz");
    core->package.variant_count = 2U;
}

static void test_core_navigation(void)
{
    mt_core_selection_t cores[MT_MENU_MAX_CORES];
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    mt_menu_action_t action;
    size_t index;
    for (index = 0U; index < MT_MENU_MAX_CORES; ++index)
        init_core(&cores[index], (unsigned)index);
    view.cores = cores;
    view.core_count = MT_MENU_MAX_CORES;
    view.active_core_id = "core0";
    view.active_variant_id = "pal";
    view.status_text = "Ready";
    mt_menu_init(&menu, &view);
    (void)mt_menu_key(&menu, &view, MT_MENU_HID_F12, false);
    check(!menu.visible, "release does not toggle menu");
    (void)press(&menu, &view, MT_MENU_HID_DOWN);
    check(menu.row == 0U, "closed menu ignores navigation");
    (void)press(&menu, &view, MT_MENU_HID_F12);
    check(menu.visible, "F12 opens menu");
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_SWITCH_CORE && action.variant_index == 1U,
          "variant changes are staged until Enter");
    (void)press(&menu, &view, MT_MENU_HID_LEFT);
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "MULTITINI ONE") && contains(&frame, "Example system 00"),
          "header and human-readable core name render");
    check(contains(&frame, "LIVE"), "running variant is distinct from selection");
    (void)press(&menu, &view, MT_MENU_HID_END);
    check(menu.row == 15U, "End reaches final core");
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.core_index == 15U, "final core selectable");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Example system 15"), "final core stays visible");
    (void)press(&menu, &view, MT_MENU_HID_DOWN);
    check(menu.row == 0U, "core navigation wraps");
    cores[0].package.variant_count = 0U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "missing variant cannot switch");
    cores[0].package.variant_count = 2U;
    menu.selected_variant[0] = SIZE_MAX;
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.selected_variant[0] == 1U, "stale variant is bounded before change");
    menu.page = (mt_menu_page_t)99;
    (void)press(&menu, &view, MT_MENU_HID_HOME);
    check(menu.page == MT_MENU_PAGE_CORES, "invalid section is normalized");
}

static void test_generic_items_and_focus(void)
{
    mt_menu_item_t items[MT_MENU_MAX_ITEMS];
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    mt_menu_action_t action;
    size_t index;
    for (index = 0U; index < MT_MENU_MAX_ITEMS; ++index) {
        mt_menu_item_t item = {
            "New core setting", "Custom value", "Help supplied by a future core.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 100U + index, true, MT_UI_COLOR_MUTED
        };
        items[index] = item;
    }
    view.active_core_id = "future";
    view.core_options = items;
    view.core_option_count = MT_MENU_MAX_ITEMS;
    view.media_items = items;
    view.media_item_count = 2U;
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_CORE_CONFIG;
    menu.row = 1U;
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_TOGGLE_CORE_OPTION &&
          action.file_index == 101U,
          "descriptor arguments are independent of presentation index");
    items[1].enabled = false;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "disabled items cannot dispatch");
    items[1].enabled = true;
    (void)press(&menu, &view, MT_MENU_HID_END);
    check(menu.row == 15U && menu.first_row == 0U,
          "dense form fits every supported option without scrolling");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "16 / 16") && contains(&frame, "Help supplied"),
          "long option list shows position and item help");
    (void)press(&menu, &view, MT_MENU_HID_PAGE_UP);
    check(menu.row == 0U && menu.first_row == 0U,
          "PageUp reaches the first setting when the full form fits");
    (void)press(&menu, &view, MT_MENU_HID_DOWN);
    for (index = 0U; index < MT_MENU_PAGE_COUNT; ++index)
        (void)press(&menu, &view, MT_MENU_HID_TAB);
    check(menu.page == MT_MENU_PAGE_CORE_CONFIG && menu.row == 1U,
          "section navigation remembers each selection");
    (void)mt_menu_key_with_modifiers(&menu, &view, MT_MENU_HID_TAB, true,
                                    UINT8_C(0x02));
    check(menu.page == MT_MENU_PAGE_INPUT, "Shift+Tab selects previous section");
    (void)press(&menu, &view, MT_MENU_HID_TAB);
    check(menu.page == MT_MENU_PAGE_CORE_CONFIG && menu.row == 1U,
          "previous-section navigation preserves focus too");
    menu.page = MT_MENU_PAGE_MEDIA;
    menu.row = 1U;
    mt_menu_open_browser(&menu, MT_MENU_BROWSER_CARTRIDGE);
    check(menu.row == 0U && menu.first_row == 0U, "browser starts from first item");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(menu.browser == MT_MENU_BROWSER_NONE && menu.row == 1U && menu.visible,
          "Escape returns to the originating media item");
    view.media_item_count = 0U;
    menu.row = SIZE_MAX;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE &&
          menu.row == 0U,
          "shrinking dynamic page cannot dispatch stale item");
    view.media_item_count = MT_MENU_MAX_ITEMS;
    (void)press(&menu, &view, MT_MENU_HID_END);
    check(menu.row == MT_MENU_MAX_ITEMS - 1U && menu.first_row == 0U,
          "growing dynamic page exposes all descriptors");
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.file_index == 100U + MT_MENU_MAX_ITEMS - 1U,
          "grown form keeps stable action arguments");
}

static void test_browser(void)
{
    mt_menu_file_t files[144] = {0};
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    mt_menu_action_t action;
    size_t index;
    char text[MT_UI_COLUMNS + 1U];
    for (index = 0U; index < 144U; ++index) {
        (void)snprintf(files[index].name, sizeof(files[index].name),
                       "Game %03u.gbc", (unsigned)index);
        files[index].size = 4U * 1024U * 1024U;
    }
    strcpy(files[143].name, "A very long cartridge name that must never overwrite "
                            "the size or the panel margin.gbc");
    view.files = files;
    view.file_count = 144U;
    view.content_root = "0:/multitini/cores/example/media";
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_MEDIA;
    mt_menu_open_browser(&menu, MT_MENU_BROWSER_CARTRIDGE);
    check(MT_MENU_LIST_ROWS == 40U,
          "dense browser displays forty files at once");
    mt_menu_render(&menu, &view, &frame);
    for (index = 0U; index < MT_MENU_LIST_ROWS; ++index) {
        char expected[32];
        (void)snprintf(expected, sizeof(expected), "Game %03u.gbc", (unsigned)index);
        row_text(&frame, 11U + index, text);
        check(strstr(text, expected) != NULL, "all forty file rows are visible");
    }
    check(!contains(&frame, "Game 040.gbc"),
          "browser clips the next file before help panel");
    (void)press(&menu, &view, MT_MENU_HID_PAGE_DOWN);
    check(menu.row == MT_MENU_LIST_ROWS && menu.first_row == 1U,
          "PageDown scrolls without losing selection");
    (void)press(&menu, &view, MT_MENU_HID_END);
    check(menu.row == 143U && menu.first_row == 144U - MT_MENU_LIST_ROWS,
          "End reaches large browser tail");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "144 / 144") && contains(&frame, "4.0 MiB"),
          "browser position and sizes render");
    row_text(&frame, 11U + MT_MENU_LIST_ROWS - 1U, text);
    check(strstr(text, "...") != NULL && strstr(text, "4.0 MiB") != NULL,
          "long filename clips independently from size");
    check(text[MT_UI_COLUMNS - 3U] == ' ' &&
          text[MT_UI_COLUMNS - 2U] == ' ' && text[MT_UI_COLUMNS - 1U] == ' ',
          "text remains inside right margin");
    check(contains(&frame, "panel margin.gbc"),
          "full selected filename remains available in help");
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_LOAD_CONTENT && action.file_index == 143U,
          "browser action identifies original file index");
    check(press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_NONE,
          "cartridge browser never invokes C64 native load");
    menu.browser = MT_MENU_BROWSER_DISK_PROGRAM;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_MOUNT_AND_RUN,
          "Enter preserves native disk launch");
    check(press(&menu, &view, MT_MENU_HID_RIGHT).kind ==
          MT_MENU_ACTION_LOAD_DISK_PROGRAM,
          "Right preserves native LOAD only");
    check(press(&menu, &view, MT_MENU_HID_LEFT).kind ==
          MT_MENU_ACTION_DIRECT_DISK_PROGRAM,
          "Left preserves explicit direct launch");
    view.file_count = 1U;
    (void)press(&menu, &view, MT_MENU_HID_DOWN);
    check(menu.row == 0U && menu.first_row == 0U, "browser handles shrink");
    view.file_count = MT_MENU_LIST_ROWS + 3U;
    (void)press(&menu, &view, MT_MENU_HID_END);
    check(menu.row == MT_MENU_LIST_ROWS + 2U && menu.first_row == 3U,
          "browser handles growth beyond the visible capacity");
    view.file_count = MT_MENU_LIST_ROWS;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "40 / 40") && !contains(&frame, "Game 040.gbc"),
          "render clamps stale selection when list shrinks to one page");
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.row == MT_MENU_LIST_ROWS - 1U && menu.first_row == 0U,
          "navigation also clamps a shortened one-page list");
    view.files = NULL;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "missing file array cannot activate");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "No matching files."), "empty browser is clear");
}

static void test_empty_and_faults(void)
{
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_INPUT;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Choose a core") && !contains(&frame, "Game Boy"),
          "no-core input never guesses a machine");
    menu.page = MT_MENU_PAGE_SYSTEM;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "restart unavailable without core");
    menu.row = 1U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "autoboot unavailable without core");
    menu.row = 2U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_REBOOT_GOLDEN,
          "recovery always available");
    view.active_core_id = "future";
    view.settings_auto_boot = true;
    menu.row = 1U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind ==
          MT_MENU_ACTION_TOGGLE_AUTO_BOOT,
          "autoboot remains an explicit action");
    view.status_text = "Ready";
    view.status_color = MT_UI_COLOR_SUCCESS;
    view.save_faulted = true;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Save failed.") && !contains(&frame, "Ready"),
          "save error overrides transient success");
    check(((frame.cells[(MT_UI_ROWS - 4U) * MT_UI_COLUMNS + 3U] >> 7U) & 15U) ==
          MT_UI_COLOR_WARNING,
          "error uses warning color");
    view.save_faulted = false;
    view.save_dirty = true;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Saving progress."),
          "pending save remains visible across pages");
    view.settings_faulted = true;
    view.status_text = "Settings failed; ROM rollback failed";
    view.status_color = MT_UI_COLOR_RED;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "ROM rollback failed") &&
          !contains(&frame, "Settings need attention"),
          "actionable command error takes priority over generic warnings");
    mt_menu_render(&menu, NULL, &frame);
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(!menu.visible, "Escape closes top-level menu");
}

int main(void)
{
    test_core_navigation();
    test_generic_items_and_focus();
    test_browser();
    test_empty_and_faults();
    puts("PASS: scalable menu navigation, descriptors, clipping and save status");
    return 0;
}
