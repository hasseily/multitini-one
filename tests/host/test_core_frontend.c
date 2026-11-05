/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_frontend.h"

#include "multitini_c64_interface.h"
#include "multitini_c64_content.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_regs.h"

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
    check(view.media_item_count == 5U && view.input_item_count == 2U &&
          view.core_option_count == 5U, "C64 profile builds supported controls");

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
    check(view.media_item_count == 4U && view.core_option_count == 3U,
          "old C64 ABI omits newer launch and option actions");
    check(!view.media_items[0].enabled,
          "drive without its ROM explains why mounting is unavailable");
    check(view.media_items[1].action == MT_MENU_ACTION_BROWSE_CARTRIDGE &&
          view.media_items[2].action == MT_MENU_ACTION_BROWSE_PROGRAM,
          "media action does not depend on an old fixed row number");
    state.abi_minor = 8U;
    state.fast_roms_fallback = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_option_count == 5U && view.media_item_count == 4U &&
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

    state.c64_expansion_caps = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_DUAL_SID | MT_C64_EXPANSION_CAPS_REU_16MB;
    state.c64_sid_mode = 3U;
    state.c64_reu_enabled = true;
    state.abi_minor = 22U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_option_count == 5U, "older ABI cannot expose expansion controls even with stale capability bits");
    state.abi_minor = 23U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_option_count == 7U && view.core_options[2].argument == 2U &&
          strcmp(view.core_options[2].value, "Dual $D500") == 0 && view.core_options[3].argument == 3U &&
          strcmp(view.core_options[3].value, "16 MB REU") == 0,
          "capable C64 exposes SID and REU with stable action arguments");
    check(view.media_items[2].enabled && view.media_items[2].action == MT_MENU_ACTION_DETACH_CARTRIDGE,
          "attached cartridge can always be ejected");
    state.cartridge_attached = false;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_items[2].enabled,
          "cartridge browser remains available so a sidecar can disable REU atomically");
    check(strstr(view.core_options[3].help, "DDR") == NULL,
          "legacy image keeps its REU description while cartridge exclusion identifies I/O conflicts");
    state.c64_expansion_caps |= MT_C64_EXPANSION_CAPS_REU_DDR;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(strstr(view.core_options[3].help, "DDR REU") != NULL && view.media_items[2].enabled,
          "DDR capability describes the new backing store with conflict checked by the launch transaction");
    state.c64_reu_enabled = false;
    state.c64_sid_mode = 2U;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_items[2].enabled && strcmp(view.core_options[2].value, "Dual $D420") == 0,
          "disabled REU restores cartridge attachment");
    state.c64_expansion_caps = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_REU_16MB;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_option_count == 6U && view.core_options[2].argument == 3U,
          "REU-only capability preserves semantic option argument");
    state.c64_expansion_caps = MT_C64_EXPANSION_CAPS_DUAL_SID | MT_C64_EXPANSION_CAPS_REU_16MB;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_option_count == 5U, "capability valid bit is required");
    state.abi_minor = 24;
    state.c64_expansion_caps = 0x8000001fU;
    state.c64_cpu_caps = MT_C64_CPU_CAPS_RELEASE;
    state.c64_cpu_caps2 = MT_C64_CPU_CAPS2_RELEASE;
    state.c64_turbo_available = true;
    state.c64_ua_available = true;
    state.c64_reu_host_available = true;
    state.c64_turbo_detail = 0x4f;
    state.c64_cpu_config = 0x55f;
    state.c64_cpu_status = 0x10000;
    mt_core_frontend_apply(&frontend, &state, &view);
    const unsigned expected_arguments[] = {0,1,4,5,6,2,3,8};
    check(view.core_option_count == 11, "release has eight hardware rows and three title actions");
    for (size_t i=0; i<8; ++i)
        check(view.core_options[i].argument == expected_arguments[i], "turbo option semantic order");
    check(view.media_item_count == 6 && view.media_items[5].action == MT_MENU_ACTION_BROWSE_REU,
        "REU image browser is a capability-gated user feature");
    state.cartridge_attached = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(!view.core_options[7].enabled && !view.media_items[5].enabled,
        "attached cartridge blocks UA and REU preload");
    state.c64_expansion_caps |= MT_C64_EXPANSION_CAPS_CART_COEXIST;
    state.c64_cartridge_policy = MT_C64_CRT_ALLOW_REU | MT_C64_CRT_ALLOW_UCI;
    state.c64_reu_enabled = state.c64_ua_enabled = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.core_options[7].enabled && view.media_items[5].enabled &&
        strcmp(view.core_options[6].value, "16 MB REU") == 0 &&
        strcmp(view.core_options[7].value, "Blocked by cartridge") == 0,
        "FC3 subtype 1 permits REU/preload while preserving the inhibited UA request");
    state.c64_cartridge_policy = 0;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(!view.media_items[5].enabled && view.core_options[7].enabled &&
        strcmp(view.core_options[6].value, "Blocked by cartridge") == 0,
        "incompatible cartridge reports effective inhibition without locking saved controls");
    state.c64_cartridge_policy = 7;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_items[5].enabled && strcmp(view.core_options[7].value, "On") == 0,
        "fully compatible cartridge leaves requested peripherals active");
    state.cartridge_attached = false; state.c64_ua_fault = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(!view.core_options[7].enabled, "UA memory fault requests core reload");
    check(mt_core_frontend_accepts_content(MT_C64_CORE_ID,MT_MENU_BROWSER_REU,MT_CONTENT_C64_REU) &&
        !mt_core_frontend_accepts_content(MT_GBC_CORE_ID,MT_MENU_BROWSER_REU,MT_CONTENT_C64_REU),
        "REU browser accepts only C64 REU images");
    state.abi_minor = 27;
    state.c64_expansion_caps |= MT_C64_EXPANSION_CAPS_UCI;
    state.cartridge_attached = true;
    state.c64_cartridge_policy = 0;
    state.c64_uci_enabled = true;
    for (unsigned mode = 0; mode < 3; ++mode) {
        state.c64_uci_active = mode != 0;
        state.c64_uci_enabled = mode != 2;
        mt_core_frontend_apply(&frontend, &state, &view);
        bool found = false;
        for (size_t i = 0; i < view.core_option_count; ++i) {
            if (view.core_options[i].argument != 15) continue;
            found = true;
            check(strcmp(view.core_options[i].value,
                mode == 0 ? "Blocked by cartridge" : mode == 1 ? "On" : "On (guest)") == 0,
                "live UCI status overrides cartridge policy after guest unlock");
        }
        check(found, "Command Interface row remains available on coexistence image");
    }
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
          strstr(view.media_items[3].help, "140 KiB DOS-order DSK or ProDOS-order PO") != NULL,
          "floppy chooser explains accepted sector sizes and ordering");
    for(unsigned drive=0U;drive<=MT_IIGS_DRIVE_COUNT;++drive) {
        for(unsigned type=0U;type<=MT_CONTENT_IIGS_FLOPPY;++type) {
            const bool expected=drive<MT_IIGS_DRIVE_COUNT &&
                (mt_iigs_drive_hdd_unit(drive)<MT_IIGS_HDD_COUNT ? type==MT_CONTENT_IIGS_HDD:
                 type==MT_CONTENT_IIGS_WOZ || type==MT_CONTENT_IIGS_HDD ||
                 (drive==3U && type==MT_CONTENT_IIGS_FLOPPY));
            check(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,
                MT_MENU_BROWSER_DRIVE8,drive,(mt_content_type_t)type)==expected,
                "IIgs drive filters permit its formats and reject other machines");
        }
    }
    check(mt_core_frontend_browser_folders(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8),
          "all IIgs drive choosers retain folder navigation");
    check(mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_DRIVE8) &&
          mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_PROGRAM) &&
          mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_CARTRIDGE) &&
          mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_REU) &&
          !mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_DISK_PROGRAM),
          "C64 host media browsers navigate folders, disk directory stays a PRG list");
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

static void test_c64_title_and_sid(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    state.core_id=MT_C64_CORE_ID; state.services_ready=true; state.abi_minor=26;
    state.c64_turbo_available=true;
    state.c64_cpu_caps=MT_C64_CPU_CAPS_RELEASE;
    state.c64_cpu_caps2=MT_C64_CPU_CAPS2_RELEASE;
    state.c64_expansion_caps=MT_C64_EXPANSION_CAPS_VALID|MT_C64_EXPANSION_CAPS_MULTI_SID|
        MT_C64_EXPANSION_CAPS_DUAL_SID|MT_C64_EXPANSION_CAPS_REU_16MB|MT_C64_EXPANSION_CAPS_UCI|0x700U;
    state.c64_title_session=true;state.c64_title_automatic=false;
    state.c64_title_owned=MT_TITLE_SPEED|MT_TITLE_SID|MT_TITLE_JOYSTICK;
    state.c64_title_source="Game.cfg";
    state.c64_turbo_detail=0x4f;
    check(mt_c64_sid_preset(&state.c64_sid,MT_SID_EIGHT),"eight SID fixture valid");
    mt_core_frontend_apply(&frontend,&state,&view);
    check(strstr(view.input_items[1].value,"(title)")!=NULL,"title-owned joystick port visible");
    bool found[20]={false};
    for(size_t i=0;i<view.core_option_count;++i){
        const mt_menu_item_t *row=&view.core_options[i];
        check(row->argument<20,"all C64 semantic arguments fit registry");found[row->argument]=true;
        if(row->argument==4)check(strstr(row->value,"(title)")!=NULL,"title-owned speed visible");
        if(row->argument==17)check(!strcmp(row->value,"Off"),"automatic-off is visible");
        if(row->argument==18||row->argument==19)check(row->enabled,"running title can save/revert");
    }
    check(found[14]&&found[15]&&found[16]&&found[17]&&found[18]&&found[19],"complete extension and title rows");
    bool manual=false;
    for(size_t i=0;i<view.media_item_count;++i)
        if(view.media_items[i].action==MT_MENU_ACTION_BROWSE_CONFIG)manual=view.media_items[i].enabled;
    check(manual,"manual Apply remains available with automatic files Off");
    state.c64_sid_setup=true;state.c64_selected_sid=7;state.c64_sid_classes=1;
    state.c64_sid_owned=UINT64_C(1)<<24;
    mt_core_frontend_apply(&frontend,&state,&view);
    check(view.core_option_count==13&&!view.core_options[1].enabled,"SID subpage has bounded rows and read-only summary");
    check(strstr(view.core_options[3].value,"SID 8 of 8")!=NULL,"all physical SID slots selectable");
    check(strstr(view.core_options[4].value,"(title)")&&strstr(view.core_options[8].value,"(title)"),"group ownership propagates to address and explicit master field");
    check(!strstr(view.core_options[5].value,"(title)"),"unowned mix field remains stored");
    mt_menu_t menu;mt_menu_init(&menu,&view);menu.visible=true;menu.page=MT_MENU_PAGE_CORE_CONFIG;menu.core_subpage=true;menu.row=4;
    mt_menu_action_t action=mt_menu_key(&menu,&view,MT_MENU_HID_RIGHT,true);
    check(action.kind==MT_MENU_ACTION_SID_SETUP&&action.file_index==4&&action.variant_index==2,"right selects positive SID edit");
    (void)mt_menu_key(&menu,&view,MT_MENU_HID_ESCAPE,true);
    check(menu.visible&&!menu.core_subpage&&menu.row==0,"Escape returns from SID page without closing menu");
    state.abi_minor=25;mt_core_frontend_apply(&frontend,&state,&view);
    for(size_t i=0;i<view.core_option_count;++i)
        check(view.core_options[i].argument<14||view.core_options[i].argument>16,"stale extension bits cannot expose ABI26 actions");
}

static void test_c64_directory_drive(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    state.core_id = MT_C64_CORE_ID;
    state.services_ready = true;
    state.abi_minor = 27;
    state.c64_turbo_available = state.c64_ua_available = true;
    state.c64_cpu_caps = MT_C64_CPU_CAPS_RELEASE;
    state.c64_cpu_caps2 = MT_C64_CPU_CAPS2_RELEASE;
    state.c64_expansion_caps = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_MULTI_SID |
        MT_C64_EXPANSION_CAPS_DUAL_SID | MT_C64_EXPANSION_CAPS_REU_16MB | MT_C64_EXPANSION_CAPS_UCI;
    state.c64_title_owned = UINT32_MAX;
    state.c64_directory_drive_available = true;
    for (unsigned on = 0; on < 2; ++on) {
        state.c64_directory_drive_enabled = on != 0;
        mt_core_frontend_apply(&frontend, &state, &view);
        check(view.core_option_count <= MT_MENU_MAX_CORE_OPTIONS, "directory option fits full C64 menu");
        bool found = false, uci = false;
        for (size_t i = 0; i < view.core_option_count; ++i) {
            const mt_menu_item_t *row = &view.core_options[i];
            if (row->argument == 15) uci = true;
            if (row->argument != 20) continue;
            found = true;
            check(!strcmp(row->label, "SD directory drive") &&
                  !strcmp(row->value, on ? "On" : "Off"), "directory choice is independent of title overrides");
            check(row->action == MT_MENU_ACTION_TOGGLE_CORE_OPTION && row->enabled &&
                  row->control == MT_MENU_CONTROL_CHECKBOX && row->control_value == on,
                  "directory checkbox carries its own action");
        }
        check(found && uci, "directory option and command interface remain separate");
    }
    state.c64_directory_drive_available = false;
    mt_core_frontend_apply(&frontend, &state, &view);
    for (size_t i = 0; i < view.core_option_count; ++i)
        check(view.core_options[i].argument != 20, "unavailable endpoint hides directory option");
}

static const mt_menu_item_t *media_row(const mt_menu_view_t *view, const char *label)
{
    for (size_t i = 0U; i < view->media_item_count; ++i)
        if (strcmp(view->media_items[i].label, label) == 0) return &view->media_items[i];
    return NULL;
}

static void test_c64_easyflash_rows(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state = {0};
    mt_menu_view_t view = {0};
    const mt_menu_item_t *row;
    state.core_id = MT_C64_CORE_ID;
    state.services_ready = true;
    state.abi_minor = 27;
    state.cartridge_attached = true;
    state.cartridge_name = "Game.crt";
    mt_core_frontend_apply(&frontend, &state, &view);
    check(media_row(&view, "Cartridge save") == NULL && media_row(&view, "Export cartridge") == NULL &&
          media_row(&view, "Boot jumper") == NULL, "ordinary cartridges have no EasyFlash rows");
    state.save_active = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    check(view.media_item_count <= MT_MENU_MAX_ITEMS, "EasyFlash rows fit the Media page");
    row = media_row(&view, "Export cartridge");
    check(row != NULL && row->enabled && row->action == MT_MENU_ACTION_EXPORT_CARTRIDGE &&
          strstr(row->help, "NAME-saved.crt") != NULL, "EasyFlash export writes a CRT copy");
    row = media_row(&view, "Boot jumper");
    check(row != NULL && row->enabled && row->action == MT_MENU_ACTION_TOGGLE_CORE_OPTION &&
          row->argument == 21U && strcmp(row->value, "Boot") == 0, "the jumper starts at Boot");
    state.c64_ef_jumper_disabled = true;
    mt_core_frontend_apply(&frontend, &state, &view);
    row = media_row(&view, "Boot jumper");
    check(row != NULL && strcmp(row->value, "Disable") == 0, "the jumper shows Disable");
}

static const mt_menu_item_t *core_row(const mt_menu_view_t *view,const char *label)
{
    for(size_t i=0U;i<view->core_option_count;++i)
        if(strcmp(view->core_options[i].label,label)==0)return &view->core_options[i];
    return NULL;
}
static void test_amiga_resolution(void)
{
    mt_core_frontend_t frontend;
    mt_core_frontend_state_t state={0};
    mt_menu_view_t view={0};
    state.core_id=MT_AMIGA_CORE_ID;state.services_ready=true;state.amiga_config.cpu=MT_AMIGA_CPU_STOCK;
    state.amiga_display_applied=UINT8_MAX;
    mt_core_frontend_apply(&frontend,&state,&view);
    check(strcmp(view.core_description,"Amiga")==0,"Amiga core has its stable user-facing name");
    const mt_menu_item_t *row=core_row(&view,"Resolution");
    check(row!=NULL && !row->enabled && strstr(row->value,"Unavailable")!=NULL &&
          strstr(row->help,"MTDisplay")!=NULL,"legacy/helper-absent resolution is disabled with installation help");
    check(row->action==MT_MENU_ACTION_AMIGA_DISPLAY && row->action!=MT_MENU_ACTION_TOGGLE_CORE_OPTION,
          "resolution has a separate action from CPU/reset options");
    state.amiga_display_available=true;state.amiga_display_status="Ready";state.amiga_display_mode_count=3U;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"Resolution");
    check(row->enabled && strstr(row->value,"640 x 256")!=NULL && strstr(row->value,"unverified")!=NULL,
          "default request is standard nonlace while capture remains unverified");
    state.amiga_display_requested=2U;state.amiga_display_applied=1U;state.amiga_display_pending=true;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"Resolution");
    check(!row->enabled && strstr(row->value,"Requested 724 x 566")!=NULL &&
          strstr(row->value,"active 640 x 512")!=NULL && row->argument==2U,
          "pending row distinguishes requested and committed sizes");
    state.amiga_display_pending=false;state.amiga_display_applied=2U;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"Resolution");
    check(row->enabled && strcmp(row->value,"724 x 566 / 16 colours")==0 &&
          strstr(row->help,"do not reset")!=NULL,"committed resolution exposes sixteen colours without reset");
    check(strstr(row->help,"updated core")!=NULL,"legacy helper/core explains unavailable fourth mode");
    state.amiga_display_mode_count=4U;state.amiga_display_requested=state.amiga_display_applied=3U;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"Resolution");
    check(row->enabled && strcmp(row->value,"1280 x 512 / 4 colours")==0 && row->argument==3U,
          "fourth mode has four colours and its own selection");
    row=core_row(&view,"CPU");
    check(row!=NULL && row->argument==0U && row->action==MT_MENU_ACTION_TOGGLE_CORE_OPTION &&
          strcmp(row->value,"68000 stock")==0,"display row preserves the stock CPU control contract");
    state.amiga_config.cpu=MT_AMIGA_CPU_68020_TURBO;mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"CPU");
    check(row!=NULL && strcmp(row->value,"68020 28 MHz")==0 && view.core_option_count<=MT_MENU_MAX_CORE_OPTIONS,
          "existing accelerated CPU option and menu capacity are unchanged");
    check(strstr(row->help,"requires an updated core")!=NULL,"old core describes unavailable14MHz");
    state.amiga_cpu14_available=true;state.amiga_config.cpu=MT_AMIGA_CPU_68020_14;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"CPU");
    check(strcmp(row->value,"68020 14 MHz")==0,"newcore14MHzlabelmatchesactualselector");
    state.amiga_cpu14_available=false;state.amiga_cpu14_fallback=true;state.amiga_config.cpu=MT_AMIGA_CPU_68020_TURBO;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"CPU");
    check(strcmp(row->value,"68020 28 MHz")==0 && strstr(row->help,"preference retained")!=NULL,"legacyfallbackexplicitlylabelsactual28MHz");
    row=core_row(&view,"IDE mode");
    check(row!=NULL && row->argument==4U && strcmp(row->value,"Auto / Classic")==0 &&
        strstr(row->help,"updated core")!=NULL,"old FPGA exposes Classic fallback without discarding Auto preference");
    state.amiga_fast_ide_available=true;state.amiga_fast_ide_expected=true;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"IDE mode");
    check(strcmp(row->value,"Auto / Fast")==0 && strstr(row->help,"restarts")!=NULL,"fast-capable 68020 has an explicit restart option");
    state.amiga_config.ide_compat=MT_AMIGA_IDE_CLASSIC;state.amiga_fast_ide_expected=false;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"IDE mode");
    check(strcmp(row->value,"Classic")==0,"compatibility selection remains explicit");
    state.amiga_config.ide_compat=MT_AMIGA_IDE_AUTO;state.amiga_config.cpu=MT_AMIGA_CPU_STOCK;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"IDE mode");
    check(strcmp(row->value,"Auto / Classic")==0 && strstr(row->help,"Stock CPU mode uses Classic")!=NULL,"stock CPU always keeps Classic IDE");
    state.amiga_display_pending=true;mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"IDE mode");
    check(!row->enabled,"a pending display transaction cannot queue an IDE restart");
    state.amiga_aga_pal=true;state.amiga_cpu14_fallback=false;
    mt_core_frontend_apply(&frontend,&state,&view);
    check(strcmp(core_row(&view,"CPU")->value,"68EC020 stock")==0 &&
          strcmp(core_row(&view,"Chipset")->value,"AGA PAL / 2 MiB chip RAM")==0,
          "A1200 reports its chipset and stock CPU");
    state.amiga_config.cpu=MT_AMIGA_CPU_68020_TURBO;
    mt_core_frontend_apply(&frontend,&state,&view);
    check(strcmp(core_row(&view,"CPU")->value,"68020 28 MHz")==0,"A1200 offers acceleration");
    state.amiga_ap68040=true;
    state.amiga_display_pending=false;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"CPU speed");
    check(row!=NULL && !row->enabled && strcmp(row->value,"28 MHz")==0,
          "A1200 68040 reports 28 MHz on the fixed-speed image");
    state.amiga_ap68040_2x_available=true;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"CPU speed");
    check(row->enabled && row->argument==0U && strcmp(row->value,"28 MHz")==0,
          "AP68040 dual-clock image offers the 28 MHz setting");
    state.amiga_config.cpu=MT_AMIGA_CPU_AP68040_2X;
    mt_core_frontend_apply(&frontend,&state,&view);row=core_row(&view,"CPU speed");
    check(row->enabled && strcmp(row->value,"56.75 MHz")==0,"AP68040 Options shows the selected 56.75 MHz");
}
int main(void)
{
    test_profile_lifecycle();
    test_c64_easyflash_rows();
    test_c64_capabilities();
    test_c64_title_and_sid();
    test_c64_directory_drive();
    test_gameboy_saves_and_input();
    test_browser_filters();
    test_genesis_profile();
    test_iigs_drive_formats();
    test_iigs_card_speed();
    test_amiga_resolution();
    puts("Core front-end profile tests passed");
    return 0;
}
