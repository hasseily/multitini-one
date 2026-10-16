/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"
#include "multitini_c64_interface.h"
#include "multitini_vectrex_interface.h"

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
    char position[24];
    for (index = 0U; index < MT_MENU_MAX_ITEMS; ++index) {
        mt_menu_item_t item = {
            .label = "New core setting", .value = "Custom value",
            .help = "Help supplied by a future core.",
            .action = MT_MENU_ACTION_TOGGLE_CORE_OPTION,
            .argument = 100U + index, .enabled = true, .value_color = MT_UI_COLOR_MUTED
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
    check(menu.row == MT_MENU_MAX_ITEMS - 1U && menu.first_row == 0U,
          "dense form fits every supported option without scrolling");
    mt_menu_render(&menu, &view, &frame);
    (void)snprintf(position, sizeof(position), "%u / %u", MT_MENU_MAX_ITEMS, MT_MENU_MAX_ITEMS);
    check(contains(&frame, position) && contains(&frame, "Help supplied"),
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

static void test_folder_browser(void)
{
    mt_menu_file_t files[44] = {0};
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_menu_action_t action;
    mt_ui_frame_t frame;
    char text[MT_UI_COLUMNS + 1U];
    strcpy(files[0].name, "Vecalabeth");
    files[0].cookie = MT_MENU_FILE_DIRECTORY | 17U;
    files[0].size = 9U * 1024U * 1024U;
    strcpy(files[1].name, "game.bas");
    files[1].cookie = 123U;
    files[1].size = 100U;
    view.files = files;
    view.file_count = 2U;
    view.content_root = "0:/multitini/cores/vectrex";
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_MEDIA;
    mt_menu_open_browser(&menu, MT_MENU_BROWSER_PROGRAM);
    check(press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_NONE &&
          press(&menu, &view, MT_MENU_HID_BACKSPACE).kind == MT_MENU_ACTION_NONE,
          "root directory has no parent action");
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_OPEN_DIRECTORY && action.file_index == 0U &&
          menu.browser == MT_MENU_BROWSER_PROGRAM && menu.visible, "Enter opens folder without closing browser");
    check(press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_OPEN_DIRECTORY,
          "Right opens directory");
    mt_menu_render(&menu, &view, &frame);
    row_text(&frame, 11U, text);
    check(strstr(text, "Folder") != NULL && strstr(text, "MiB") == NULL,
          "folder row displays Folder instead of its size");
    check(contains(&frame, "Enter or Right opens this folder") &&
          contains(&frame, "[Left/Backspace] Up"), "folder navigation help is visible");
    menu.row = 1U;
    check(press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_NONE,
          "Right does not load ordinary program file");
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_LOAD_CONTENT && action.file_index == 1U,
          "ordinary file still loads with Enter");
    check(files[0].cookie == (MT_MENU_FILE_DIRECTORY | 17U) && files[1].cookie == 123U,
          "menu preserves caller cookies");

    strcpy(files[41].name, "..");
    files[41].cookie = MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_PARENT;
    view.file_count = 44U;
    action = press(&menu, &view, MT_MENU_HID_BACKSPACE);
    check(action.kind == MT_MENU_ACTION_OPEN_DIRECTORY && action.file_index == 41U &&
          menu.row == 41U && menu.first_row <= 41U && menu.first_row + MT_MENU_LIST_ROWS > 41U,
          "Backspace finds and selects parent regardless of list position");
    mt_menu_render(&menu, &view, &frame);
    row_text(&frame, 11U + 41U - menu.first_row, text);
    check(strstr(text, "Up") != NULL && contains(&frame, "opens the parent folder"),
          "parent row has Up label and navigation help");
    menu.row = 0U;
    action = press(&menu, &view, MT_MENU_HID_LEFT);
    check(action.kind == MT_MENU_ACTION_OPEN_DIRECTORY && action.file_index == 41U,
          "Left navigates to parent from a child row");
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_OPEN_DIRECTORY,
          "Enter opens the selected parent");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(menu.browser == MT_MENU_BROWSER_NONE && menu.visible,
          "Escape still leaves folder browser for Media");

    files[0].cookie = 0U;
    view.file_count = 2U;
    mt_menu_open_browser(&menu, MT_MENU_BROWSER_DISK_PROGRAM);
    check(press(&menu, &view, MT_MENU_HID_BACKSPACE).kind == MT_MENU_ACTION_NONE &&
          press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_DIRECT_DISK_PROGRAM &&
          press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_LOAD_DISK_PROGRAM &&
          press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_MOUNT_AND_RUN,
          "unflagged D64 rows retain all existing launch actions");
}

static void update_border_view(mt_menu_view_t *view, mt_menu_action_t action)
{
    check(action.kind == MT_MENU_ACTION_SET_BORDER, "border edit dispatches configuration");
    view->border_enabled = (action.file_index & 1U) != 0U;
    view->border_style = (uint8_t)((action.file_index >> 1U) & 7U);
    view->border_color = (uint8_t)((action.file_index >> 4U) & 15U);
    view->border_padding = (uint8_t)((action.file_index >> 8U) & 31U);
}

static void test_borders(void)
{
    static const char *const styles[] = {
        "Single line", "Double line", "Corner brackets", "Dashed", "Soft glow"
    };
    static const char *const colors[] = {
        "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Light gray",
        "Dark gray", "Light blue", "Light green", "Light cyan", "Light red",
        "Light magenta", "Yellow", "White"
    };
    mt_menu_view_t view = {0};
    mt_core_selection_t core;
    mt_menu_t menu;
    mt_ui_frame_t frame;
    mt_menu_action_t action;
    size_t index;
    init_core(&core, 0U);
    view.cores = &core;
    view.core_count = 1U;
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_VIDEO;
    menu.row = 2U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Select a core from Cores") &&
          contains(&frame, "Unavailable"), "border page explains missing active core");
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "no active core cannot edit borders");
    view.active_core_id = "old-core";
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Unsupported") && contains(&frame, "Update this core package"),
          "older shell has actionable unsupported message");
    for (index = 1U; index < 5U; ++index) {
        menu.row = index;
        check(press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_NONE &&
              press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_NONE &&
              press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
              "unsupported border rows never dispatch register values");
    }
    view.active_core_id = "core0";
    view.border_supported = true;
    view.border_color = 7U;
    view.border_padding = 6U;
    menu.row = 2U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Off") && contains(&frame, "Single line") &&
          contains(&frame, "Light gray") && contains(&frame, "Saved for this core"),
          "default border values and per-core ownership render");
    check(press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_NONE,
          "left leaves an already disabled border unchanged");
    action = press(&menu, &view, MT_MENU_HID_RIGHT);
    check(action.file_index == 0x671U && !view.border_enabled,
          "menu proposes exact packed enable value without mutating caller settings");
    update_border_view(&view, action);
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "On"), "refreshed view immediately displays applied selection");
    update_border_view(&view, press(&menu, &view, MT_MENU_HID_ENTER));
    check(!view.border_enabled, "Enter toggles border off");
    menu.row = 3U;
    for (index = 0U; index < 5U; ++index) {
        mt_menu_render(&menu, &view, &frame);
        check(contains(&frame, styles[index]), "all five border type names fit");
        update_border_view(&view, press(&menu, &view, MT_MENU_HID_RIGHT));
    }
    check(view.border_style == 0U, "style cycles after fifth type");
    update_border_view(&view, press(&menu, &view, MT_MENU_HID_LEFT));
    check(view.border_style == 4U, "left wraps style backwards");
    update_border_view(&view, press(&menu, &view, MT_MENU_HID_ENTER));
    check(view.border_style == 0U, "Enter cycles style forwards");
    menu.row = 4U;
    view.border_color = 0U;
    for (index = 0U; index < 16U; ++index) {
        mt_menu_render(&menu, &view, &frame);
        check(contains(&frame, colors[index]), "classic color order and full labels render");
        update_border_view(&view, press(&menu, &view, MT_MENU_HID_ENTER));
    }
    check(view.border_color == 0U, "all sixteen colors cycle");
    update_border_view(&view, press(&menu, &view, MT_MENU_HID_LEFT));
    check(view.border_color == 15U, "color wraps backwards");
    menu.row = 5U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Padding (px)") && contains(&frame, "between the image and border"),
          "padding names output-pixel units and the external gap");
    view.border_padding = 1U;
    check(press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_NONE,
          "padding left clamps at one");
    for (index = 2U; index <= 20U; ++index) {
        update_border_view(&view, press(&menu, &view, MT_MENU_HID_RIGHT));
        check(view.border_padding == index, "padding advances through every supported pixel inset");
    }
    check(press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_NONE,
          "padding right clamps at twenty");
    update_border_view(&view, press(&menu, &view, MT_MENU_HID_ENTER));
    check(view.border_padding == 1U, "Enter cycles maximum padding back to one");
    view.border_style = view.border_color = view.border_padding = UINT8_MAX;
    action = press(&menu, &view, MT_MENU_HID_RIGHT);
    update_border_view(&view, action);
    check(view.border_style == 0U && view.border_color == 7U && view.border_padding == 7U,
          "invalid external presentation values normalize before indexing and editing");
    mt_menu_render(&menu, &view, &frame);
    check(!contains(&frame, "Game window size") && contains(&frame, "6 / 8"),
          "Video includes resolution after border padding and exposes no game-size control");
    menu.row = 0U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE,
          "existing display-source row retains its action");
    menu.row = 1U;
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_SWITCH_CORE && action.variant_index == 1U,
          "existing variant row retains staged selection and load");
    menu.page = MT_MENU_PAGE_VIDEO;
    menu.row = 0U;
    (void)press(&menu, &view, MT_MENU_HID_TAB);
    check(menu.page == MT_MENU_PAGE_EFFECTS && MT_MENU_PAGE_COUNT == 8U,
          "shared Effects follows Video without a separate Borders tab");
    (void)press(&menu, &view, MT_MENU_HID_TAB);
    check(menu.page == MT_MENU_PAGE_INPUT, "Input follows shared Effects");
    menu.page = MT_MENU_PAGE_VIDEO;
    mt_menu_render(&menu, NULL, &frame);
}

static void update_vectrex_video_view(mt_menu_view_t *view, mt_menu_action_t action)
{
    check(action.kind == MT_MENU_ACTION_SET_VECTREX_VIDEO, "Vectrex edit dispatches video config");
    view->vectrex_overlay_enabled = (action.file_index & 1U) != 0U;
    view->vectrex_beam_gain = (uint8_t)((action.file_index >> 1U) & 3U);
    view->vectrex_overlay_opacity = (uint8_t)((action.file_index >> 3U) & 3U);
}

static void test_vectrex_video(void)
{
    static const char *const brightness[] = {"25%", "50%", "75%", "100%"};
    static const char *const beam[] = {"1x", "2x", "3x", "4x"};
    mt_core_selection_t core;
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    mt_menu_file_t file = {0};
    mt_menu_action_t action;
    size_t index;
    init_core(&core, 0U);
    view.cores = &core; view.core_count = 1U; view.active_core_id = "core0";
    view.vectrex_video_supported = true; /* A stale capability cannot expose another core's controls. */
    mt_menu_init(&menu, &view); menu.visible = true; menu.page = MT_MENU_PAGE_VIDEO;
    (void)press(&menu, &view, MT_MENU_HID_END);
    mt_menu_render(&menu, &view, &frame);
    check(menu.row == 7U && !contains(&frame, "Overlay enabled"), "other cores have eight Video rows including display resolution and size");
    check(contains(&frame, "Video size"), "video size is available in Video");
    menu.row = 6U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Display resolution"), "resolution remains available in Video");
    check(press(&menu,&view,MT_MENU_HID_RIGHT).kind==MT_MENU_ACTION_NONE,
          "older shells cannot change resolution");
    view.display_supported=true;view.display_mode=0;
    action=press(&menu,&view,MT_MENU_HID_LEFT);
    check(action.kind==MT_MENU_ACTION_SET_DISPLAY_MODE && action.file_index==5U,
          "display resolution wraps backwards");
    view.display_mode=5;
    action=press(&menu,&view,MT_MENU_HID_RIGHT);
    check(action.kind==MT_MENU_ACTION_SET_DISPLAY_MODE && action.file_index==0U,
          "display resolution wraps forward to safe default");
    menu.row=7U;
    check(press(&menu,&view,MT_MENU_HID_RIGHT).kind==MT_MENU_ACTION_NONE,
          "older cores cannot change video size");
    view.video_multiplier_supported=true;view.video_multiplier=0U;
    action=press(&menu,&view,MT_MENU_HID_LEFT);
    check(action.kind==MT_MENU_ACTION_SET_VIDEO_MULTIPLIER && action.file_index==4U,
          "Auto wraps backwards to 4x");
    for(size_t multiplier=1U;multiplier<=4U;++multiplier) {
        view.video_multiplier=(uint8_t)(multiplier-1U);
        action=press(&menu,&view,MT_MENU_HID_RIGHT);
        check(action.kind==MT_MENU_ACTION_SET_VIDEO_MULTIPLIER && action.file_index==multiplier,
              "size choices advance from Auto through each manual multiplier");
    }
    view.video_multiplier=4U;action=press(&menu,&view,MT_MENU_HID_ENTER);
    check(action.kind==MT_MENU_ACTION_SET_VIDEO_MULTIPLIER && action.file_index==0U,
          "4x wraps forwards to Auto");
    strcpy(core.package.id, "vectrex"); view.active_core_id = "vectrex";
    view.vectrex_video_supported = false;
    menu.row = 6U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Overlay enabled") && contains(&frame, "Update the Vectrex core package"),
          "old Vectrex image has clear unsupported controls");
    for (index = 6U; index < 9U; ++index) {
        menu.row = index;
        check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE &&
              press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_NONE &&
              press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_NONE,
              "unsupported Vectrex controls never dispatch");
    }
    view.vectrex_video_supported = true;
    view.vectrex_overlay_enabled = true;
    view.vectrex_beam_gain = 1U; view.vectrex_overlay_opacity = 1U;
    menu.row = 6U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "50%") && contains(&frame, "2x") && contains(&frame, "No overlay loaded"),
          "default brightness values and missing artwork help render");
    action = press(&menu, &view, MT_MENU_HID_LEFT);
    check(action.file_index == 0xAU && view.vectrex_overlay_enabled,
          "overlay Off proposes packed value without mutating caller state");
    update_vectrex_video_view(&view, action);
    check(press(&menu, &view, MT_MENU_HID_LEFT).kind == MT_MENU_ACTION_NONE,
          "overlay Off does not issue redundant writes");
    update_vectrex_video_view(&view, press(&menu, &view, MT_MENU_HID_RIGHT));
    view.vectrex_overlay_loaded = true;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "An overlay is loaded"), "loaded overlay status renders");
    view.vectrex_overlay_error = true;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Overlay load failed"), "failed artwork load has actionable help");
    menu.row = 7U; view.vectrex_overlay_opacity = 0U;
    for (index = 0U; index < 4U; ++index) {
        mt_menu_render(&menu, &view, &frame);
        check(contains(&frame, brightness[index]), "all overlay brightness percentages render");
        update_vectrex_video_view(&view, press(&menu, &view, MT_MENU_HID_RIGHT));
        check(view.vectrex_beam_gain == 1U && view.vectrex_overlay_enabled,
              "artwork brightness preserves beam and enabled setting");
    }
    update_vectrex_video_view(&view, press(&menu, &view, MT_MENU_HID_LEFT));
    check(view.vectrex_overlay_opacity == 3U, "artwork brightness wraps backward");
    menu.row = 8U; view.vectrex_beam_gain = 0U;
    for (index = 0U; index < 4U; ++index) {
        mt_menu_render(&menu, &view, &frame);
        check(contains(&frame, beam[index]) && contains(&frame, "9 / 11"), "all beam multipliers fit the Video page");
        update_vectrex_video_view(&view, press(&menu, &view, MT_MENU_HID_ENTER));
        check(view.vectrex_overlay_opacity == 3U, "beam gain preserves artwork brightness");
    }
    view.vectrex_beam_gain = view.vectrex_overlay_opacity = UINT8_MAX;
    update_vectrex_video_view(&view, press(&menu, &view, MT_MENU_HID_LEFT));
    check(view.vectrex_beam_gain == 0U && view.vectrex_overlay_opacity == 1U,
          "invalid presentation values normalize before indexing");
    strcpy(file.name, "Star Castle.ovr"); view.files = &file; view.file_count = 1U;
    mt_menu_open_browser(&menu, MT_MENU_BROWSER_OVERLAY);
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Load overlay") && !contains(&frame, "Load program"),
          "overlay browser has its own title");
    action = press(&menu, &view, MT_MENU_HID_ENTER);
    check(action.kind == MT_MENU_ACTION_LOAD_CONTENT && action.file_index == 0U,
          "overlay browser loads through the existing content action");
    file.cookie = MT_MENU_FILE_DIRECTORY;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_OPEN_DIRECTORY,
          "overlay browser retains nested directory navigation");
}

static void init_controller_menu(mt_menu_t *menu, mt_menu_view_t *view,
                                 uint32_t core, bool generic_axes)
{
    mt_controller_config_t configs[MT_GAMEPAD_SLOTS];
    memset(view, 0, sizeof(*view));
    view->active_core_id = core == MT_C64_CORE_ID ? "c64" : "vectrex";
    view->gamepad_core_id = core;
    view->gamepad_devices[0].connected = 1U;
    view->gamepad_devices[0].generic_axes = generic_axes ? 1U : 0U;
    view->gamepad_devices[0].buttons_present = UINT32_MAX;
    strcpy(view->gamepad_devices[0].name, "Input test device");
    view->gamepad_preview.connected = 1U;
    view->gamepad_preview.generic_axes = generic_axes ? 1U : 0U;
    view->gamepad_preview.hat = 8U;
    for (unsigned slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot)
        mt_controls_default(core, &view->gamepad_devices[slot], &configs[slot]);
    mt_menu_init(menu, view);
    menu->visible = true;
    mt_menu_gamepad_begin(menu, view, configs);
    check(menu->gamepad_edit, "controller editor opens for active system");
}

static void start_controller_capture(mt_menu_t *menu, mt_menu_view_t *view,
                                     unsigned action)
{
    const uint32_t mask = mt_controls_action_mask(view->gamepad_core_id);
    unsigned row = 2U;
    check((mask & (1U << action)) != 0U, "test capture action exists");
    for (unsigned index = 0U; index < action; ++index)
        if ((mask & (1U << index)) != 0U) ++row;
    menu->gamepad_field = (uint8_t)row;
    (void)press(menu, view, MT_MENU_HID_ENTER);
    check(menu->gamepad_capture && !menu->gamepad_capture_armed,
          "assignment starts unarmed");
}

static void test_generic_axis_capture(void)
{
    mt_menu_t menu;
    mt_menu_view_t view;
    init_controller_menu(&menu, &view, MT_C64_CORE_ID, true);
    check(menu.gamepad_config[0].assignment == MT_CONTROLLER_OFF,
          "generic devices enter the editor disabled");
    for (unsigned action = 0U; action < MT_CONTROL_COUNT; ++action)
        check(menu.gamepad_config[0].binding[action] == 0U,
              "generic devices enter the editor unassigned");
    view.gamepad_preview.axis_valid_mask = (uint16_t)(1U << MT_INPUT_AXIS_SLIDER);
    view.gamepad_preview.axis[MT_INPUT_AXIS_SLIDER] = 10000;
    start_controller_capture(&menu, &view, MT_CONTROL_A);
    check(strstr(menu.gamepad_notice, "stop moving") != NULL,
          "generic capture asks for rest rather than a centered stick");
    view.gamepad_preview.axis[MT_INPUT_AXIS_SLIDER] = 11000;
    check(!mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture_armed,
          "moving slider does not establish the capture baseline");
    check(mt_menu_gamepad_update(&menu, &view) && menu.gamepad_capture_armed,
          "stable off-center slider arms capture");
    for (int value = 12000; value <= 19000; value += 1000) {
        view.gamepad_preview.axis[MT_INPUT_AXIS_SLIDER] = (int16_t)value;
        check(!mt_menu_gamepad_update(&menu, &view) && menu.gamepad_capture,
              "small slider movement does not assign prematurely");
        check(menu.gamepad_previous.axis[MT_INPUT_AXIS_SLIDER] == 11000,
              "capture retains the original stable position");
    }
    view.gamepad_preview.axis[MT_INPUT_AXIS_SLIDER] = 20000;
    check(mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture &&
          menu.gamepad_config[0].binding[MT_CONTROL_A] ==
              mt_controls_axis_source(MT_INPUT_AXIS_SLIDER, false, true),
          "cumulative slider motion assigns the extra positive direction");
    check(menu.gamepad_config[0].assignment == MT_CONTROLLER_OFF,
          "assigning a generic control does not implicitly enable routing");

    init_controller_menu(&menu, &view, MT_VECTREX_CORE_ID, true);
    view.gamepad_preview.axis_valid_mask = (uint16_t)(1U << MT_INPUT_AXIS_PAN);
    view.gamepad_preview.axis[MT_INPUT_AXIS_PAN] = -20000;
    start_controller_capture(&menu, &view, MT_CONTROL_AXIS_X);
    check(mt_menu_gamepad_update(&menu, &view), "off-center analog capture arms");
    view.gamepad_preview.axis[MT_INPUT_AXIS_PAN] = -11000;
    check(mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture &&
          menu.gamepad_config[0].binding[MT_CONTROL_AXIS_X] ==
              mt_controls_axis_source(MT_INPUT_AXIS_PAN, true, false),
          "analog assignment accepts an extra full axis at an arbitrary endpoint");
}

static void test_relative_axis_capture_and_conflicts(void)
{
    mt_menu_t menu;
    mt_menu_view_t view;
    init_controller_menu(&menu, &view, MT_C64_CORE_ID, true);
    const uint16_t wheel_negative = mt_controls_axis_source(MT_INPUT_AXIS_WHEEL, false, false);
    const uint16_t slider_positive = mt_controls_axis_source(MT_INPUT_AXIS_SLIDER, false, true);
    menu.gamepad_config[0].binding[MT_CONTROL_B] = slider_positive;
    view.gamepad_preview.axis_valid_mask = (uint16_t)(1U << MT_INPUT_AXIS_WHEEL);
    view.gamepad_preview.relative_mask = view.gamepad_preview.axis_valid_mask;
    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = -1;
    start_controller_capture(&menu, &view, MT_CONTROL_A);
    check(!mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture_armed,
          "even a one-unit relative motion prevents arming");
    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = 0;
    check(mt_menu_gamepad_update(&menu, &view), "expired relative motion allows arming");
    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = -1;
    check(mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture &&
          menu.gamepad_config[0].binding[MT_CONTROL_A] == wheel_negative &&
          menu.gamepad_config[0].binding[MT_CONTROL_B] == slider_positive,
          "relative capture assigns one-unit motion without aliasing another extra axis");

    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = 0;
    start_controller_capture(&menu, &view, MT_CONTROL_C);
    check(mt_menu_gamepad_update(&menu, &view), "second relative capture arms");
    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = -1;
    check(mt_menu_gamepad_update(&menu, &view) && menu.gamepad_conflict &&
          menu.gamepad_pending_source == wheel_negative,
          "same extra axis produces an explicit assignment conflict");
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(!menu.gamepad_capture && menu.gamepad_config[0].binding[MT_CONTROL_A] == 0U &&
          menu.gamepad_config[0].binding[MT_CONTROL_C] == wheel_negative &&
          menu.gamepad_config[0].binding[MT_CONTROL_B] == slider_positive,
          "conflict confirmation moves only the matching extra source");

    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = 0;
    start_controller_capture(&menu, &view, MT_CONTROL_A);
    check(mt_menu_gamepad_update(&menu, &view), "timeout capture arms");
    view.gamepad_now_ms += UINT64_C(20000);
    check(mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture &&
          menu.gamepad_config[0].binding[MT_CONTROL_A] == 0U,
          "generic capture retains the existing timeout without changing assignments");

    init_controller_menu(&menu, &view, MT_C64_CORE_ID, false);
    view.gamepad_preview.axis_valid_mask = 1U;
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_X] = 16000;
    start_controller_capture(&menu, &view, MT_CONTROL_A);
    check(!mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture_armed,
          "ordinary pads still require centered sticks");
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_X] = 0;
    check(mt_menu_gamepad_update(&menu, &view), "centered ordinary pad arms");
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_X] = -20000;
    check(mt_menu_gamepad_update(&menu, &view) && menu.gamepad_conflict,
          "ordinary stick capture still overlaps its composite D-pad binding");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(menu.gamepad_config[0].binding[MT_CONTROL_LEFT] == MT_CONTROL_SOURCE_DPAD_STICK + 2U,
          "cancel preserves the existing D-pad/stick assignment");
}

static void test_controller_axis_display(void)
{
    mt_menu_t menu;
    mt_menu_view_t view;
    mt_ui_frame_t frame;
    init_controller_menu(&menu, &view, MT_VECTREX_CORE_ID, true);
    menu.gamepad_test = true;
    view.gamepad_preview.buttons = UINT32_C(1) << 31U;
    view.gamepad_preview.axis_valid_mask = (uint16_t)((1U << MT_INPUT_AXIS_COUNT) - 1U);
    view.gamepad_preview.relative_mask = (uint16_t)((1U << MT_INPUT_AXIS_WHEEL) | (1U << MT_INPUT_AXIS_PAN));
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_X] = -32768;
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_Y] = 32767;
    view.gamepad_preview.axis[MT_INPUT_AXIS_SLIDER] = 12345;
    view.gamepad_preview.axis[MT_INPUT_AXIS_DIAL] = -12345;
    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = 1;
    view.gamepad_preview.axis[MT_INPUT_AXIS_PAN] = -1;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Button 32: HELD") && contains(&frame, "Button 1: released"),
          "tester retains all 32 buttons with expanded axes");
    check(contains(&frame, "Left: X/Y") && contains(&frame, "Down: X/Y") &&
          !contains(&frame, "Left: D-pad"),
          "axis-encoded directions are visible without mislabeling an analog stick as D-pad");
    check(contains(&frame, "Slider: 12345") && contains(&frame, "Dial: -12345") &&
          contains(&frame, "Wheel: 1 (relative)") && contains(&frame, "Pan: -1 (relative)"),
          "tester renders all extra axis values with relative semantics");
    check(contains(&frame, "Assigned actions: none") && contains(&frame, "Mapped stick: X 0  Y 0") &&
          contains(&frame, "Game and navigation actions are paused"),
          "maximum buttons and axes leave room for mapped output and help");
    view.gamepad_preview.hat = 6U;
    view.gamepad_preview.axis[MT_INPUT_AXIS_WHEEL] = 0;
    view.gamepad_preview.axis[MT_INPUT_AXIS_PAN] = 0;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Left: D-pad + X/Y") && contains(&frame, "Wheel: 0 (relative)") &&
          contains(&frame, "Pan: 0 (relative)"),
          "tester distinguishes simultaneous direction sources and shows expired relative motion");
}

int main(void)
{
    test_core_navigation();
    test_generic_items_and_focus();
    test_browser();
    test_folder_browser();
    test_borders();
    test_vectrex_video();
    test_empty_and_faults();
    test_generic_axis_capture();
    test_relative_axis_capture_and_conflicts();
    test_controller_axis_display();
    puts("PASS: scalable menu navigation, descriptors, clipping and save status");
    return 0;
}
