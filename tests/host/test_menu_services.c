/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"

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

static uint8_t control_row(uint32_t core, unsigned control)
{
    unsigned row = 2U;
    const uint32_t mask = mt_controls_action_mask(core);
    for (unsigned index = 0U; index < control; ++index)
        if ((mask & (UINT32_C(1) << index)) != 0U) ++row;
    return (uint8_t)row;
}

static void start_capture(mt_menu_t *menu, mt_menu_view_t *view, unsigned control)
{
    menu->gamepad_field = control_row(view->gamepad_core_id, control);
    (void)press(menu, view, MT_MENU_HID_ENTER);
    check(menu->gamepad_capture, "Enter starts capture for the selected game action");
}

static void neutral_preview(mt_menu_t *menu, mt_menu_view_t *view)
{
    memset(&view->gamepad_preview, 0, sizeof(view->gamepad_preview));
    view->gamepad_preview.connected = 1U;
    view->gamepad_preview.hat = 8U;
    (void)mt_menu_gamepad_update(menu, view);
}

static void test_gamepad_modal(void)
{
    mt_menu_t menu;
    mt_menu_view_t view = {0};
    mt_controller_config_t configs[MT_GAMEPAD_SLOTS];
    mt_ui_frame_t frame;
    view.active_core_id = "gbc";
    view.gamepad_core_id = MT_GBC_CORE_ID;
    view.gamepad_devices[4].connected = view.gamepad_devices[8].connected = 1U;
    view.gamepad_devices[4].buttons_present = UINT32_MAX;
    view.gamepad_devices[4].home_button = 10U;
    (void)snprintf(view.gamepad_devices[4].name, sizeof(view.gamepad_devices[4].name), "Arcade controller");
    (void)snprintf(view.gamepad_devices[8].name, sizeof(view.gamepad_devices[8].name), "Second controller");
    for (unsigned index = 0U; index < MT_GAMEPAD_SLOTS; ++index) {
        mt_controls_default(view.gamepad_core_id, &view.gamepad_devices[index], &configs[index]);
        configs[index].assignment = MT_CONTROLLER_OFF;
    }
    configs[4].assignment = MT_CONTROLLER_PORT1;
    mt_menu_init(&menu, &view);
    menu.visible = true; menu.page = MT_MENU_PAGE_INPUT;
    mt_menu_gamepad_begin(&menu, &view, configs);
    check(menu.gamepad_slot == 4U, "editor starts with a connected controller, never a keyboard or empty input entry");
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.gamepad_slot == 8U, "controller selection skips disconnected entries");
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.gamepad_slot == 4U, "controller selection wraps only among connected controllers");
    menu.gamepad_field = 1U;
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.gamepad_config[4].assignment == MT_CONTROLLER_OFF, "Game Boy has no player two choice");
    menu.gamepad_config[4].assignment = MT_CONTROLLER_PORT1;
    neutral_preview(&menu, &view);
    view.gamepad_preview.buttons = UINT32_C(1) << 31U;
    start_capture(&menu, &view, MT_CONTROL_A);
    (void)mt_menu_gamepad_update(&menu, &view);
    check(!menu.gamepad_capture_armed && menu.gamepad_config[4].binding[MT_CONTROL_A] == configs[4].binding[MT_CONTROL_A],
          "held opening control cannot bind itself before release");
    neutral_preview(&menu, &view);
    check(menu.gamepad_capture_armed, "all controls released arms capture");
    const uint8_t row_before = menu.gamepad_field;
    (void)press(&menu, &view, MT_MENU_HID_DOWN);
    check(menu.gamepad_field == row_before, "navigation cannot move the row during capture");
    view.gamepad_preview.buttons = UINT32_C(1) << 31U;
    check(mt_menu_gamepad_update(&menu, &view) && !menu.gamepad_capture &&
          menu.gamepad_config[4].binding[MT_CONTROL_A] == 32U,
          "button 32 is independently assignable and completes capture");
    check(menu.gamepad_config[8].binding[MT_CONTROL_A] == configs[8].binding[MT_CONTROL_A],
          "editing one controller does not edit another");
    start_capture(&menu, &view, MT_CONTROL_B);
    neutral_preview(&menu, &view);
    view.gamepad_preview.buttons = UINT32_C(1) << 31U;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(menu.gamepad_conflict && menu.gamepad_config[4].binding[MT_CONTROL_A] == 32U,
          "duplicate assignment waits for an explicit choice");
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.gamepad_config[4].binding[MT_CONTROL_A] == 0U &&
          menu.gamepad_config[4].binding[MT_CONTROL_B] == 32U,
          "confirm moves the duplicate control and clears its earlier action");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(menu.gamepad_edit, "dirty editor requires Save or Discard before leaving");
    (void)press(&menu, &view, MT_MENU_HID_F12);
    check(menu.visible && menu.gamepad_edit, "F12 cannot silently discard an edited profile");
    const unsigned commands = control_row(view.gamepad_core_id, MT_CONTROL_COUNT);
    menu.gamepad_field = (uint8_t)(commands + 4U);
    check(press(&menu, &view, MT_MENU_HID_ENTER).kind == MT_MENU_ACTION_SET_GAMEPADS && mt_menu_gamepad_dirty(&menu),
          "Save dispatches a write but remains dirty until storage acknowledges it");
    mt_menu_gamepad_saved(&menu);
    check(!mt_menu_gamepad_dirty(&menu), "successful save acknowledges the new baseline");
    start_capture(&menu, &view, MT_CONTROL_SCREENSHOT_OUTPUT);
    neutral_preview(&menu, &view);
    view.gamepad_preview.buttons = UINT32_C(1) << 30U;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(menu.gamepad_config[4].binding[MT_CONTROL_SCREENSHOT_OUTPUT] == 31U,
          "full-screen screenshot action above bit fifteen remains assignable");
    menu.gamepad_field = (uint8_t)(commands + 5U);
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.gamepad_config[4].binding[MT_CONTROL_SCREENSHOT_OUTPUT] == 0U && !mt_menu_gamepad_dirty(&menu),
          "Discard restores the saved baseline including optional shortcuts");
    start_capture(&menu, &view, MT_CONTROL_SCREENSHOT);
    neutral_preview(&menu, &view);
    view.gamepad_preview.hat = 0U;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(menu.gamepad_conflict && menu.gamepad_config[4].binding[MT_CONTROL_UP] == MT_CONTROL_SOURCE_DPAD_STICK,
          "capture detects a D-pad source already used by the composite direction default");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(!mt_menu_gamepad_dirty(&menu), "cancelling a composite-source conflict leaves both assignments intact");
    start_capture(&menu, &view, MT_CONTROL_B);
    (void)press(&menu, &view, MT_MENU_HID_DELETE);
    check(!menu.gamepad_capture && menu.gamepad_config[4].binding[MT_CONTROL_B] == 0U,
          "Delete clears the chosen assignment without binding another controller control");
    menu.gamepad_field = (uint8_t)(commands + 5U);
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    menu.gamepad_field = (uint8_t)(commands + 1U);
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.gamepad_test, "test controller opens a dedicated input-only mode");
    view.gamepad_preview.buttons = UINT32_C(1) << 31U;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Button 32: HELD") && contains(&frame, "Arcade controller") && !contains(&frame, "Auxiliary"),
          "tester names each reported button instead of collapsing it into a shared flag");
    (void)press(&menu, &view, MT_MENU_HID_F12);
    check(!menu.gamepad_test && menu.gamepad_edit && menu.visible, "F12 safely returns from testing");
    start_capture(&menu, &view, MT_CONTROL_A);
    view.gamepad_devices[4].connected = 0U;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(!menu.gamepad_capture && menu.gamepad_config[4].binding[MT_CONTROL_B] == 32U,
          "disconnect cancels capture without moving or losing the saved assignment");
    view.gamepad_devices[4].connected = 1U;
    start_capture(&menu, &view, MT_CONTROL_A);
    view.gamepad_now_ms += 20001U;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(!menu.gamepad_capture && !mt_menu_gamepad_dirty(&menu), "capture timeout leaves settings unchanged");
    menu.gamepad_field = (uint8_t)commands;
    (void)press(&menu, &view, MT_MENU_HID_ENTER);
    check(menu.gamepad_wizard && menu.gamepad_control == MT_CONTROL_UP, "Set all begins with the first active-system action");
    (void)press(&menu, &view, MT_MENU_HID_TAB);
    check(menu.gamepad_control == MT_CONTROL_DOWN, "Set all can skip a control without rebinding it");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(!menu.gamepad_wizard && !menu.gamepad_capture, "Escape exits Set all");
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Controls") && contains(&frame, "Screenshot: full screen") &&
          contains(&frame, "Save changes") && !contains(&frame, "Logical control"),
          "controller editor renders plain game actions and explicit save choices");
    view.gamepad_player[4] = UINT8_MAX;
    mt_menu_render(&menu, &view, &frame);
    check(contains(&frame, "Now: Unassigned"), "current unresolved routing remains visible alongside the draft player setting");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(!menu.gamepad_edit && menu.visible, "clean editor returns to Controls page");
}

static void test_gamepad_analog(void)
{
    mt_menu_t menu;
    mt_menu_view_t view = {0};
    mt_controller_config_t configs[MT_GAMEPAD_SLOTS];
    view.active_core_id = "vectrex"; view.gamepad_core_id = MT_VECTREX_CORE_ID;
    view.gamepad_devices[0].connected = 1U;
    for (unsigned index = 0U; index < MT_GAMEPAD_SLOTS; ++index)
        mt_controls_default(view.gamepad_core_id, &view.gamepad_devices[index], &configs[index]);
    mt_menu_init(&menu, &view); menu.visible = true;
    mt_menu_gamepad_begin(&menu, &view, configs);
    menu.gamepad_config[1].assignment = MT_CONTROLLER_PORT1;
    menu.gamepad_field = 1U;
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.gamepad_config[0].assignment == MT_CONTROLLER_PORT1,
          "a disconnected controller's saved assignment does not prevent explicit reuse of its port");
    menu.gamepad_config[0].assignment = MT_CONTROLLER_AUTO;
    view.gamepad_devices[1].connected = 1U;
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.gamepad_config[0].assignment == MT_CONTROLLER_AUTO,
          "two connected controllers cannot be assigned to the same explicit port");
    neutral_preview(&menu, &view);
    start_capture(&menu, &view, MT_CONTROL_AXIS_X);
    neutral_preview(&menu, &view);
    view.gamepad_preview.buttons = 1U;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(menu.gamepad_capture, "a button press cannot bind an analog action");
    view.gamepad_preview.axis_valid_mask = 1U << MT_GAMEPAD_AXIS_RX;
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_RX] = 24000;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(!menu.gamepad_capture && menu.gamepad_config[0].binding[MT_CONTROL_AXIS_X] == MT_CONTROL_SOURCE_AXIS + MT_GAMEPAD_AXIS_RX,
          "analog capture preserves the whole selected stick axis");
    menu.gamepad_sticks = true; menu.gamepad_control = MT_GAMEPAD_AXIS_RX; menu.gamepad_field = 2U;
    (void)press(&menu, &view, MT_MENU_HID_RIGHT);
    check(menu.gamepad_config[0].axis[MT_GAMEPAD_AXIS_RX].deadzone_percent == 11U &&
          menu.gamepad_config[0].axis[MT_GAMEPAD_AXIS_X].deadzone_percent == 10U,
          "dead zone edits tune only the selected controller axis");
    (void)press(&menu, &view, MT_MENU_HID_ESCAPE);
    check(!menu.gamepad_sticks && menu.gamepad_edit, "stick editor returns to the unsaved assignment page");
    menu.gamepad_config[0].axis[MT_GAMEPAD_AXIS_RX].invert = 1U;
    neutral_preview(&menu, &view);
    start_capture(&menu, &view, MT_CONTROL_A);
    neutral_preview(&menu, &view);
    view.gamepad_preview.axis_valid_mask = 1U << MT_GAMEPAD_AXIS_RX;
    view.gamepad_preview.axis[MT_GAMEPAD_AXIS_RX] = 24000;
    (void)mt_menu_gamepad_update(&menu, &view);
    check(!menu.gamepad_capture && menu.gamepad_config[0].binding[MT_CONTROL_A] == MT_CONTROL_SOURCE_AXIS_NEG + MT_GAMEPAD_AXIS_RX * 2U,
          "capture accounts for configured inversion so the just-moved direction triggers the assignment");
}

int main(void)
{
    test_gamepad_modal();
    test_gamepad_analog();
    test_document_actions();
    test_reader_modal();
    test_clock_modal();
    test_slider_fallbacks();
    puts("PASS: controller capture, multiple controllers, analog tuning, documents, clock and sliders");
    return 0;
}
