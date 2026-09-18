/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"

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

static bool contains(const mt_ui_frame_t *frame, const char *needle)
{
    char text[MT_UI_COLUMNS + 1U];
    size_t row, column;
    for (row = 0U; row < MT_UI_ROWS; ++row) {
        for (column = 0U; column < MT_UI_COLUMNS; ++column)
            text[column] = mt_ui_character(frame, column, row);
        text[MT_UI_COLUMNS] = '\0';
        if (strstr(text, needle) != NULL) return true;
    }
    return false;
}

static mt_menu_action_t press(mt_menu_t *menu, const mt_menu_view_t *view,
                              uint8_t key)
{
    return mt_menu_key(menu, view, key, true);
}

static void check_closed(const mt_text_reader_t *reader)
{
    check(!reader->active && reader->text == NULL && reader->line_starts == NULL &&
          reader->line_count == 0U, "closing modal releases both reader buffers");
}

static void test_document_actions(void)
{
    const mt_menu_browser_t browsers[] = {
        MT_MENU_BROWSER_PROGRAM, MT_MENU_BROWSER_DRIVE8,
        MT_MENU_BROWSER_CARTRIDGE, MT_MENU_BROWSER_DOCUMENTS
    };
    mt_menu_file_t files[2] = {{"notes.txt", 16U, MT_MENU_FILE_TEXT}, {"game.prg", 2U, 0U}};
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    size_t index;
    view.files = files; view.file_count = 2U;
    mt_menu_init(&menu, &view);
    menu.visible = true; menu.page = MT_MENU_PAGE_MEDIA;
    for (index = 0U; index < sizeof(browsers) / sizeof(browsers[0]); ++index) {
        mt_menu_open_browser(&menu, browsers[index]);
        check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_OPEN_TEXT,
              "TXT opens reader from media and global document browsers");
    }
    menu.browser = MT_MENU_BROWSER_PROGRAM;
    menu.row = 1U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_LOAD_CONTENT,
          "ordinary content retains its load action");
    files[0].cookie |= MT_MENU_FILE_DIRECTORY;
    menu.row = 0U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_OPEN_DIRECTORY,
          "directory flag has priority over a text extension");
}

static void test_reader_modal(void)
{
    mt_menu_file_t files[64] = {0};
    mt_text_reader_t reader;
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    char source[1024];
    char line[MT_MENU_TEXT_COLUMNS + 1U];
    size_t length = MT_MENU_TEXT_COLUMNS * 2U;
    size_t index;
    mt_text_reader_init(&reader);
    for (index = 0U; index < 64U; ++index) {
        (void)snprintf(files[index].name, sizeof(files[index].name), "Document %02u.txt", (unsigned)index);
        files[index].cookie = MT_MENU_FILE_TEXT;
    }
    view.files = files; view.file_count = 64U;
    view.text_reader = &reader; view.text_title = "Read me.txt";
    mt_menu_init(&menu, &view);
    menu.visible = true; menu.page = MT_MENU_PAGE_MEDIA;
    mt_menu_open_browser(&menu, MT_MENU_BROWSER_PROGRAM);
    menu.row = 50U; menu.first_row = 20U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).file_index == 50U,
          "text action retains the selected browser index");
    memset(source, 'A', length);
    source[length++] = '\n';
    for (index = 0U; index < 50U; ++index) {
        const int written = snprintf(source + length, sizeof(source) - length,
                                     "Line %02u\n", (unsigned)index);
        check(written > 0 && (size_t)written < sizeof(source) - length, "reader fixture fits");
        length += (size_t)written;
    }
    check(mt_text_reader_open(&reader, source, length, false,
                              MT_MENU_TEXT_COLUMNS, MT_MENU_TEXT_ROWS) == MT_TEXT_READER_OK,
          "long document opens");
    check(reader.line_count == 52U && mt_text_reader_line(&reader, 0U, line, sizeof(line)) &&
          strlen(line) == MT_MENU_TEXT_COLUMNS, "long source line wraps to the menu width");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Read me.txt") && contains(&frame, "1-40 / 52 lines"),
          "reader renders its title and visible line range");
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
          "Enter inside reader never dispatches the underlying content action");
    check(press(&menu, &view, MT_MENU_HID_TAB).kind == MT_MENU_ACTION_NONE &&
          menu.page == MT_MENU_PAGE_MEDIA && reader.active, "Tab cannot leave reader modal");
    check(press(&menu, &view, MT_MENU_HID_DOWN).kind == MT_MENU_ACTION_NONE && reader.top == 1U,
          "Down scrolls one line without dispatching a core action");
    (void)press(&menu, &view, MT_MENU_HID_UP);
    check(reader.top == 0U, "Up returns to the first line");
    (void)press(&menu, &view, MT_MENU_HID_PAGE_DOWN);
    check(reader.top == 12U, "Page Down clamps to the last full page");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "13-52 / 52 lines") && contains(&frame, "Line 49"),
          "last page renders the final document line");
    (void)press(&menu, &view, MT_MENU_HID_LEFT);
    check(reader.top == 0U, "Left scrolls a page backwards");
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(reader.top == 12U, "Right scrolls a page forwards");
    (void)press(&menu, &view, MT_MENU_HID_HOME);
    check(reader.top == 0U, "Home selects first page");
    (void)press(&menu, &view, MT_MENU_HID_END);
    check(reader.top == 12U, "End selects last page");
    check(press(&menu, &view, MT_MENU_HID_ESCAPE).kind == MT_MENU_ACTION_NONE &&
          menu.visible && menu.browser == MT_MENU_BROWSER_PROGRAM &&
          menu.row == 50U && menu.first_row == 20U,
          "Escape restores the exact browser selection and scroll window");
    check_closed(&reader);

    check(mt_text_reader_open(&reader, NULL, 0U, false, MT_MENU_TEXT_COLUMNS,
                              MT_MENU_TEXT_ROWS) == MT_TEXT_READER_OK, "empty document opens");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "This document is empty.") && contains(&frame, "0-0 / 0 lines"),
          "empty reader has clear status");
    (void)press(&menu, &view, MT_MENU_HID_BACKSPACE);
    check_closed(&reader);
    check(menu.visible && menu.row == 50U && menu.first_row == 20U,
          "Backspace returns without moving the browser");

    check(mt_text_reader_open(&reader, source, length, true, MT_MENU_TEXT_COLUMNS,
                              MT_MENU_TEXT_ROWS) == MT_TEXT_READER_OK, "truncated document opens");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "First 256 KiB only"), "truncated document visibly reports its limit");
    check(press(&menu, &view, MT_MENU_HID_F12).kind == MT_MENU_ACTION_NONE && !menu.visible,
          "F12 explicitly exits menu from reader");
    check_closed(&reader);
}

static void test_clock_modal(void)
{
    mt_menu_item_t items[2] = {{0}};
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t frame;
    size_t index;
    items[0].label = "Set clock"; items[0].value = "Edit";
    items[0].action = MT_MENU_ACTION_EDIT_CLOCK; items[0].enabled = true;
    items[1].label = "Restart core"; items[1].action = MT_MENU_ACTION_RESET_CORE;
    items[1].enabled = true;
    view.system_items = items; view.system_item_count = 2U;
    view.clock_available = true; view.clock_valid = true;
    view.clock = (mt_clock_time_t){2024U, 1U, 31U, 12U, 34U, 56U};
    mt_menu_init(&menu, &view);
    menu.visible = true; menu.page = MT_MENU_PAGE_SYSTEM;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE &&
          menu.clock_edit && menu.clock_field == MT_CLOCK_FIELD_YEAR,
          "clock descriptor enters editor without dispatching a board write");
    (void)press(&menu, &view, MT_MENU_HID_DOWN);
    check(menu.clock_field == MT_CLOCK_FIELD_MONTH &&
          press(&menu, &view, MT_MENU_HID_RIGHT).kind == MT_MENU_ACTION_NONE &&
          menu.clock.month == 2U && menu.clock.day == 29U,
          "clock arrows select and adjust month with calendar clamping");
    check(view.clock.month == 1U && view.clock.day == 31U, "editing does not mutate the live clock view");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Set board clock") && contains(&frame, "Save time"),
          "clock editor renders fields and explicit save action");
    check(press(&menu, &view, MT_MENU_HID_ESCAPE).kind == MT_MENU_ACTION_NONE &&
          !menu.clock_edit && menu.visible && menu.row == 0U,
          "Escape cancels editor without closing menu or dispatching a write");
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.clock.month == 1U && menu.clock.day == 31U, "reopening discards cancelled draft edits");
    for (index = 0U; index < MT_CLOCK_FIELD_COUNT; ++index)
        check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE,
              "Enter advances clock fields without invoking underlying actions");
    check(menu.clock_field == MT_CLOCK_FIELD_COUNT &&
          press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_SET_CLOCK,
          "Enter on Save time dispatches the explicit clock write");
    check(menu.clock_edit, "editor stays open until board acknowledges the save");
    check(press(&menu, &view, MT_MENU_HID_F12).kind == MT_MENU_ACTION_NONE &&
          !menu.visible && !menu.clock_edit, "F12 exits and discards clock editor state");
    (void)press(&menu, &view, MT_MENU_HID_F12);
    view.clock_valid = false;
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.clock_edit && mt_clock_valid(&menu.clock) && menu.clock.year >= 2000U,
          "invalid RTC gets a valid editable default");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    items[0].enabled = false;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_NONE && !menu.clock_edit,
          "unavailable clock descriptor cannot enter editor");
    menu.row = 1U;
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_RESET_CORE,
          "ordinary core action still dispatches outside modals");
}

static void test_slider_fallbacks(void)
{
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_ui_frame_t expected, actual;
    view.active_core_id = "vectrex";
    view.border_supported = true; view.border_enabled = true;
    view.border_color = 7U; view.border_padding = 6U;
    view.vectrex_video_supported = true; view.vectrex_overlay_enabled = true;
    view.vectrex_overlay_opacity = 1U; view.vectrex_beam_gain = 1U;
    mt_menu_init(&menu, &view);
    menu.visible = true; menu.page = MT_MENU_PAGE_VIDEO;
    mt_menu_render(&menu, &view, &expected);
    check(contains(&expected, "50%") && contains(&expected, "2x") &&
          contains(&expected, "Padding (px)"), "slider values remain readable beside their tracks");
    view.border_padding = UINT8_MAX;
    view.vectrex_overlay_opacity = UINT8_MAX;
    view.vectrex_beam_gain = UINT8_MAX;
    mt_menu_render(&menu, &view, &actual);
    check(memcmp(&expected, &actual, sizeof(actual)) == 0,
          "invalid slider positions use the same fallback as numeric labels");
    view.border_padding = 0U;
    mt_menu_render(&menu, &view, &actual);
    check(memcmp(&expected, &actual, sizeof(actual)) == 0,
          "below-minimum padding also uses the numeric fallback position");
}

int main(void)
{
    test_document_actions();
    test_reader_modal();
    test_clock_modal();
    test_slider_fallbacks();
    puts("PASS: document and clock modals, preserved browser focus, and slider consistency");
    return 0;
}
