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

static int row_contains(const mt_ui_frame_t *frame, size_t row,
                        const char *needle)
{
    char text[MT_UI_COLUMNS + 1U];
    size_t column;
    for (column = 0U; column < MT_UI_COLUMNS; ++column)
        text[column] = mt_ui_character(frame, column, row);
    text[MT_UI_COLUMNS] = '\0';
    return strstr(text, needle) != NULL;
}

int main(void)
{
    mt_core_selection_t cores[1] = {0};
    mt_menu_view_t view = {0};
    mt_menu_file_t files[1] = {{"demo.d64", 174848U}};
    mt_menu_t menu;
    mt_menu_action_t action;
    mt_ui_frame_t frame;

    strcpy(cores[0].package.id, "c64");
    strcpy(cores[0].package.name, "Commodore 64");
    strcpy(cores[0].package.variants[0].id, "pal");
    strcpy(cores[0].package.variants[0].label, "PAL 50 Hz");
    strcpy(cores[0].package.variants[1].id, "ntsc");
    strcpy(cores[0].package.variants[1].label, "NTSC 60 Hz");
    cores[0].package.variant_count = 2U;
    view.cores = cores;
    view.core_count = 1U;
    view.active_core_id = "c64";
    view.active_variant_id = "pal";
    view.status_text = "Ready";
    view.symbolic_keyboard = true;
    view.joystick_port = 2U;
    view.drive8_supported = true;
    view.cartridge_supported = true;
    view.program_supported = true;
    view.c64_input_settings = true;

    mt_menu_init(&menu, &view);
    action = mt_menu_key(&menu, &view, MT_MENU_HID_F12, true);
    check(menu.visible && action.kind == MT_MENU_ACTION_NONE,
          "F12 opens menu");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_RIGHT, true);
    check(menu.selected_variant[0] == 1U &&
          action.kind == MT_MENU_ACTION_NONE, "right selects NTSC");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_SWITCH_CORE &&
          action.core_index == 0U && action.variant_index == 1U,
          "enter requests selected core variant");

    mt_menu_render(&menu, &view, &frame);
    check(row_contains(&frame, 2U, "MULTITINI ONE"), "header rendered");
    check(row_contains(&frame, 7U, "Commodore 64"), "core rendered");
    check(row_contains(&frame, 7U, "NTSC 60 Hz"), "variant rendered");
    check(row_contains(&frame, 34U, "Ready"), "status rendered");

    (void)mt_menu_key(&menu, &view, MT_MENU_HID_TAB, true);
    check(menu.page == MT_MENU_PAGE_MEDIA, "Tab changes section");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_BROWSE_DRIVE8,
          "empty drive opens browser");
    menu.browser = MT_MENU_BROWSER_DRIVE8;
    view.files = files;
    view.file_count = 1U;
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_MOUNT_DRIVE8 &&
          action.file_index == 0U, "browser chooses disk image");
    menu.browser = MT_MENU_BROWSER_NONE;
    view.drive8_mounted = true;
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_EJECT_DRIVE8,
          "mounted drive requests eject");

    (void)mt_menu_key(&menu, &view, MT_MENU_HID_TAB, true);
    check(menu.page == MT_MENU_PAGE_VIDEO, "Tab reaches video section");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE,
          "video source is an active setting");
    (void)mt_menu_key(&menu, &view, MT_MENU_HID_DOWN, true);
    (void)mt_menu_key(&menu, &view, MT_MENU_HID_RIGHT, true);
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_SWITCH_CORE &&
          action.core_index == 0U && action.variant_index == 0U,
          "video timing applies the selected active-core variant");
    view.video_test_enabled = true;
    mt_menu_render(&menu, &view, &frame);
    check(row_contains(&frame, 7U, "Multitini standby"),
          "video source value rendered");
    check(row_contains(&frame, 8U, "PAL 50 Hz"),
          "core timing value rendered");

    menu.page = MT_MENU_PAGE_MEDIA;
    menu.row = 1U;
    view.drive8_supported = false;
    view.program_supported = false;
    view.c64_input_settings = false;
    mt_menu_render(&menu, &view, &frame);
    check(row_contains(&frame, 7U, "Unavailable"),
          "unsupported drive is explicit");
    check(row_contains(&frame, 8U, "Load GB/GBC"),
          "second core cartridge prompt rendered");
    view.save_supported = true;
    view.save_active = true;
    view.save_dirty = true;
    mt_menu_render(&menu, &view, &frame);
    check(row_contains(&frame, 11U, "Battery save") &&
          row_contains(&frame, 11U, "Pending write"),
          "battery-save state rendered");
    view.save_faulted = true;
    mt_menu_render(&menu, &view, &frame);
    check(row_contains(&frame, 11U, "WRITE FAILED"),
          "battery-save fault rendered");
    menu.page = MT_MENU_PAGE_INPUT;
    menu.row = 0U;
    mt_menu_render(&menu, &view, &frame);
    check(row_contains(&frame, 7U, "Arrows + Z/X"),
          "Game Boy keyboard fallback rendered");

    (void)mt_menu_key(&menu, &view, MT_MENU_HID_ESCAPE, true);
    check(!menu.visible, "Escape closes menu");
    puts("PASS: multicore menu model and renderer");
    return 0;
}
