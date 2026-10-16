/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_frontend.h"

#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_interface.h"

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

static void test_profile_lifecycle(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    view.status_text = "Status owned by runtime";
    view.content_root = "/content/test";
    view.settings_auto_boot = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 0U && view.input_item_count == 0U &&
          view.core_option_count == 0U && view.input_help == NULL,
          "standby has neutral empty machine pages");
    check(strcmp(view.content_root, "/content/test") == 0 &&
          strcmp(view.status_text, "Status owned by runtime") == 0 &&
          view.settings_auto_boot, "profile leaves shared runtime fields intact");

    state.core_id = MT_C64_CORE_ID;
    state.abi_minor = 10U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 0U && view.input_item_count == 0U,
          "uninitialized services expose no live actions");
    state.services_ready = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 4U && view.input_item_count == 2U &&
          view.core_option_count == 2U, "C64 profile builds supported controls");

    state.core_id = MT_GBC_CORE_ID;
    state.save_faulted = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 2U && view.input_item_count == 4U &&
          view.core_option_count == 0U && view.save_faulted,
          "switch to GBC discards C64 controls");
    state.core_id = UINT32_C(0x12345678);
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 0U && view.input_item_count == 0U &&
          view.core_option_count == 0U && !view.save_faulted &&
          !view.save_dirty && view.input_help == NULL,
          "unknown machine never inherits controls or save warnings");
    mt_core_frontend_apply(&frontend, NULL, &view);
    check(strstr(view.core_description, "Choose a core") != NULL,
          "null state restores standby guidance");
    mt_core_frontend_apply(NULL, &state, &view);
    mt_core_frontend_apply(&frontend, &state, NULL);
}

static void test_c64_capabilities(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    state.core_id = MT_C64_CORE_ID;
    state.abi_minor = 7U;
    state.services_ready = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 3U && view.core_option_count == 0U,
          "old C64 ABI omits newer launch and option actions");
    check(!view.media_items[0].enabled,
          "drive without its ROM explains why mounting is unavailable");
    check(view.media_items[1].action == MT_MENU_ACTION_BROWSE_CARTRIDGE &&
          view.media_items[2].action == MT_MENU_ACTION_BROWSE_PROGRAM,
          "media action does not depend on an old fixed row number");
    state.abi_minor = 8U;
    state.fast_roms_fallback = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_option_count == 2U && view.media_item_count == 3U &&
          view.core_options[0].argument == 0U &&
          view.core_options[1].argument == 1U &&
          view.core_options[0].action == MT_MENU_ACTION_TOGGLE_CORE_OPTION &&
          view.core_options[1].action == MT_MENU_ACTION_TOGGLE_CORE_OPTION,
          "option arguments preserve the runtime command contract");
    check(view.core_options[0].value_color == MT_UI_COLOR_WARNING,
          "ROM fallback is visible as a warning");

    state.abi_minor = 9U;
    state.drive8_ready = true;
    state.drive8_mounted = true;
    state.drive8_name = "Test.g64";
    state.cartridge_attached = true;
    state.cartridge_name = "Test.crt";
    state.symbolic_keyboard = true;
    state.joystick_port = 1U;
    state.cpu_turbo_enabled = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_items[0].enabled &&
          view.media_items[0].action == MT_MENU_ACTION_EJECT_DRIVE8 &&
          strcmp(view.media_items[0].value, "Test.g64") == 0,
          "mounted disk keeps the safe eject action");
    check(!view.media_items[1].enabled,
          "G64 mount does not enable the D64 program browser");
    check(view.media_items[2].action == MT_MENU_ACTION_DETACH_CARTRIDGE &&
          strcmp(view.media_items[2].value, "Test.crt") == 0,
          "attached cartridge keeps the detach action");
    check(strcmp(view.input_items[0].value, "US symbolic") == 0 &&
          strcmp(view.input_items[1].label, "Keyboard joystick") == 0 &&
          strcmp(view.input_items[1].value, "Joystick port 1") == 0 &&
          strcmp(view.core_options[1].value, "Smart 2x") == 0,
          "profile reflects current input and CPU settings");
    state.drive8_d64_mounted = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_items[1].enabled &&
          view.media_items[1].action == MT_MENU_ACTION_BROWSE_DISK_PROGRAM,
          "D64 mount enables its existing launch path");
}

static void test_gameboy_saves_and_input(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_menu_action_t action;
    state.core_id = MT_GBC_CORE_ID;
    state.abi_minor = 7U;
    state.services_ready = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 2U &&
          strcmp(view.media_items[0].label, "Cartridge") == 0 &&
          strcmp(view.media_items[1].label, "Battery save") == 0,
          "GBC exposes cartridge and save status without C64 disk rows");
    check(strcmp(view.media_items[1].value, "No cartridge") == 0 &&
          !view.media_items[1].enabled &&
          view.media_items[1].action == MT_MENU_ACTION_NONE,
          "battery status is read-only");
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_MEDIA;
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_BROWSE_CARTRIDGE,
          "first GBC media item browses cartridges");
    menu.row = 1U;
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_NONE, "save status cannot trigger media I/O");

    state.cartridge_attached = true;
    state.cartridge_name = "Adventure.gbc";
    state.save_active = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(strcmp(view.media_items[1].value, "Saved to SD") == 0 &&
          view.media_items[1].value_color == MT_UI_COLOR_SUCCESS,
          "clean active battery RAM reports saved");
    state.save_dirty = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.save_dirty &&
          view.media_items[1].value_color == MT_UI_COLOR_WARNING,
          "pending save shows warning");
    state.save_faulted = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.save_faulted &&
          view.media_items[1].value_color == MT_UI_COLOR_RED &&
          strcmp(view.media_items[1].value, "Write failed") == 0,
          "save error takes precedence over pending state");
    state.abi_minor = 6U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(!view.save_faulted && !view.save_dirty &&
          strstr(view.media_items[1].value, "Unavailable") != NULL,
          "old ABI does not claim persistent save support");
    check(view.input_item_count == 4U &&
          strcmp(view.input_items[1].label, "Keyboard buttons") == 0 &&
          strcmp(view.input_items[1].value, "Z = A   X = B") == 0 &&
          strcmp(view.input_items[2].value, "Enter") == 0 &&
          strcmp(view.input_items[3].value, "Right Shift") == 0,
          "keyboard guidance matches the existing Game Boy HID mapping");
    menu.page = MT_MENU_PAGE_INPUT;
    menu.row = 0U;
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_NONE,
          "Game Boy input guide cannot invoke C64 input settings");
}

static void test_browser_filters(void)
{
    unsigned browser;
    unsigned type;
    for (browser = 0U; browser <= MT_MENU_BROWSER_PROGRAM; ++browser) {
        for (type = 0U; type <= MT_CONTENT_IIGS_FLOPPY; ++type) {
            const bool c64_expected =
                (browser == MT_MENU_BROWSER_DRIVE8 &&
                 (type == MT_CONTENT_D64 || type == MT_CONTENT_G64)) ||
                (browser == MT_MENU_BROWSER_CARTRIDGE && type == MT_CONTENT_CRT) ||
                (browser == MT_MENU_BROWSER_PROGRAM && type == MT_CONTENT_PRG);
            const bool gbc_expected = browser == MT_MENU_BROWSER_CARTRIDGE &&
                (type == MT_CONTENT_GB_CART || type == MT_CONTENT_GBC_CART);
            const bool genesis_expected = browser == MT_MENU_BROWSER_CARTRIDGE &&
                (type == MT_CONTENT_GENESIS_CART || type == MT_CONTENT_GENESIS_SMD);
            check(mt_core_frontend_accepts_content(MT_C64_CORE_ID,
                      (mt_menu_browser_t)browser, (mt_content_type_t)type) ==
                  c64_expected, "C64 browser filters match its load services");
            check(mt_core_frontend_accepts_content(MT_GBC_CORE_ID,
                      (mt_menu_browser_t)browser, (mt_content_type_t)type) ==
                  gbc_expected, "GBC browser accepts only GB/GBC cartridges");
            check(mt_core_frontend_accepts_content(MT_GENESIS_CORE_ID,
                      (mt_menu_browser_t)browser, (mt_content_type_t)type) ==
                  genesis_expected, "Genesis browser accepts only raw/SMD cartridges");
            check(!mt_core_frontend_accepts_content(0U,
                      (mt_menu_browser_t)browser, (mt_content_type_t)type),
                  "standby cannot select content");
        }
    }
    check(mt_core_frontend_browser_supported(MT_C64_CORE_ID,
              MT_MENU_BROWSER_DISK_PROGRAM) &&
          !mt_core_frontend_browser_supported(MT_GBC_CORE_ID,
              MT_MENU_BROWSER_DISK_PROGRAM),
          "D64 directory browser belongs only to C64");
    check(!mt_core_frontend_accepts_content(MT_GBC_CORE_ID,
              MT_MENU_BROWSER_CARTRIDGE, (mt_content_type_t)-1) &&
          !mt_core_frontend_accepts_content(MT_GBC_CORE_ID,
              (mt_menu_browser_t)-1, MT_CONTENT_GBC_CART) &&
          !mt_core_frontend_browser_supported(MT_C64_CORE_ID,
              (mt_menu_browser_t)100), "invalid enums cannot index or shift outside masks");
}

static void test_genesis_profile(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    state.core_id = MT_GENESIS_CORE_ID;
    state.abi_minor = 16U;
    state.services_ready = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count == 2U && view.input_item_count == 4U && view.core_option_count == 2U,
          "Genesis provides cartridge/save and complete six-button keyboard guide");
    check(strcmp(view.core_options[0].value, "6 buttons") == 0 &&
          view.core_options[0].argument == 0U && view.core_options[1].argument == 1U,
          "Genesis protocols default to six buttons on independent ports");
    state.genesis_three_button_mask = 2U;
    check(view.media_items[0].action == MT_MENU_ACTION_BROWSE_CARTRIDGE &&
          view.media_items[1].action == MT_MENU_ACTION_NONE,
          "Genesis cartridge chooser and read-only save state");
    state.cartridge_attached = true;
    state.cartridge_name = "Original.md";
    state.save_active = true;
    state.save_dirty = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.save_dirty && !view.save_faulted &&
          view.media_items[0].action == MT_MENU_ACTION_DETACH_CARTRIDGE &&
          view.media_items[1].value_color == MT_UI_COLOR_WARNING,
          "Genesis eject remains available while safe save drain is pending");
    check(strcmp(view.core_options[1].value, "3 buttons") == 0 &&
          strcmp(view.core_options[0].value, "6 buttons") == 0,
          "Genesis three-button override applies only to the selected port");
    state.save_faulted = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.save_faulted && view.media_items[1].value_color == MT_UI_COLOR_RED,
          "Genesis save failure takes precedence over pending state");
}

static void test_iigs_drive_formats(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    state.core_id = MT_IIGS_CORE_ID;
    state.services_ready = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(strstr(view.media_items[2].help, "800 KiB PO or 2MG") != NULL &&
          strstr(view.media_items[3].help, "140 KiB DOS-order DSK") != NULL,
          "floppy chooser explains accepted sector sizes and ordering");
    for(unsigned drive=0U;drive<=MT_IIGS_DRIVE_COUNT;++drive) {
        for(unsigned type=0U;type<=MT_CONTENT_IIGS_FLOPPY;++type) {
            const bool expected=drive<MT_IIGS_DRIVE_COUNT &&
                (mt_iigs_drive_hdd_unit(drive)<MT_IIGS_HDD_COUNT ? type==MT_CONTENT_IIGS_HDD:
                 type==MT_CONTENT_IIGS_WOZ ||
                 (drive==2U ? type==MT_CONTENT_IIGS_HDD:type==MT_CONTENT_IIGS_FLOPPY));
            check(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,
                MT_MENU_BROWSER_DRIVE8,drive,(mt_content_type_t)type)==expected,
                "IIgs drive filters permit its formats and reject other machines");
        }
    }
    check(mt_core_frontend_browser_folders(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8),
          "all IIgs drive choosers retain folder navigation");
}

static void test_iigs_card_speed(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    state.core_id = MT_IIGS_CORE_ID;
    state.services_ready = true;
    state.iigs_speed_mask = MT_IIGS_ACCEL_SPEED_MASK;
    state.iigs_config.speed = 6U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_options[0].enabled &&
          strcmp(view.core_options[0].value, "57.3 MHz") == 0,
          "ZipGS exposes live CPU speed selection");
    state.iigs_config.card = 2U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(!view.core_options[0].enabled &&
          strcmp(view.core_options[0].value, "2.8 MHz (stock)") == 0,
          "no accelerator always shows stock and disables speed changes");
    check(strstr(view.core_options[2].help, "restarts") != NULL,
          "card changes explain the machine restart");
    state.iigs_config.card = 0U;
    state.iigs_config.speed = 0U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_options[0].enabled &&
          strcmp(view.core_options[0].value, "2.8 MHz (stock)") == 0,
          "returning to ZipGS permits speed selection from stock");
}

int main(void)
{
    test_profile_lifecycle();
    test_c64_capabilities();
    test_gameboy_saves_and_input();
    test_browser_filters();
    test_genesis_profile();
    test_iigs_drive_formats();
    test_iigs_card_speed();
    puts("Core front-end profile tests passed");
    return 0;
}
