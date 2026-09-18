/* SPDX-License-Identifier: GPL-3.0-only */
/* Preview fixtures only. All cells come from the production menu renderer. */
#include "multitini_core_frontend.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_system.h"

#include <stdio.h>
#include <string.h>

static void core(mt_core_selection_t *selection, const char *id,
                 const char *name, const char *variant, const char *label)
{
    (void)snprintf(selection->package.id, sizeof(selection->package.id), "%s", id);
    (void)snprintf(selection->package.name, sizeof(selection->package.name), "%s", name);
    (void)snprintf(selection->package.variants[0].id,
                   sizeof(selection->package.variants[0].id), "%s", variant);
    (void)snprintf(selection->package.variants[0].label,
                   sizeof(selection->package.variants[0].label), "%s", label);
    selection->package.variant_count = 1U;
}

int main(int argc, char **argv)
{
    mt_core_selection_t cores[MT_MENU_MAX_CORES] = {0};
    mt_menu_file_t files[144] = {0};
    mt_menu_item_t options[MT_MENU_MAX_ITEMS] = {0};
    char option_labels[MT_MENU_MAX_ITEMS][32];
    mt_core_frontend_state_t state = {0};
    mt_core_frontend_t frontend;
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_system_frontend_t system_frontend;
    mt_system_snapshot_t snapshot = {0};
    mt_text_reader_t reader = {0};
    mt_ui_frame_t frame;
    size_t index;
    const char *scenario;

    if (argc != 2) {
        fprintf(stderr, "Usage: menu_preview SCENARIO\n");
        return 2;
    }
    scenario = argv[1];
    core(&cores[0], "c64", "Commodore 64", "pal", "PAL 50 Hz");
    cores[0].package.variant_count = 2U;
    (void)snprintf(cores[0].package.variants[1].id,
                   sizeof(cores[0].package.variants[1].id), "ntsc");
    (void)snprintf(cores[0].package.variants[1].label,
                   sizeof(cores[0].package.variants[1].label), "NTSC 60 Hz");
    core(&cores[1], "gbc", "Game Boy Color", "gbc", "GB / GBC");
    view.cores = cores;
    view.core_count = 2U;
    view.status_text = "Ready";
    view.status_color = MT_UI_COLOR_MUTED;
    view.settings_auto_boot = true;
    state.abi_minor = MT_ABI_MINOR;
    state.services_ready = true;

    if (strncmp(scenario, "c64-", 4U) == 0) {
        state.core_id = MT_C64_CORE_ID;
        state.drive8_ready = true;
        state.drive8_mounted = true;
        state.drive8_d64_mounted = true;
        state.drive8_name = "Preview demo collection.d64";
        state.symbolic_keyboard = true;
        state.joystick_port = 2U;
        state.fast_roms_active = true;
        state.cpu_turbo_enabled = true;
        view.active_core_id = "c64";
        view.active_variant_id = "pal";
        view.content_root = "/multitini/content/c64";
    } else if (strncmp(scenario, "gbc-", 4U) == 0 ||
               strcmp(scenario, "browser-long") == 0) {
        state.core_id = MT_GBC_CORE_ID;
        state.cartridge_attached = true;
        state.cartridge_name = "Preview color adventure.gbc";
        state.save_active = true;
        state.save_faulted = strcmp(scenario, "gbc-save-failure") == 0;
        view.active_core_id = "gbc";
        view.active_variant_id = "gbc";
        view.content_root = "/multitini/content/gbc/Preview folder with a very long name";
    }

    mt_core_frontend_apply(&frontend, &state, &view);
    mt_menu_init(&menu, &view);
    (void)mt_menu_key(&menu, &view, MT_MENU_HID_F12, true);
    if (strcmp(scenario, "standby") == 0) {
        view.status_text = "Choose a core, then press Enter to start.";
    } else if (strcmp(scenario, "c64-media") == 0) {
        menu.page = MT_MENU_PAGE_MEDIA;
        menu.row = 1U;
    } else if (strcmp(scenario, "c64-options") == 0) {
        menu.page = MT_MENU_PAGE_CORE_CONFIG;
        menu.row = 1U;
    } else if (strcmp(scenario, "gbc-media") == 0 ||
               strcmp(scenario, "gbc-save-failure") == 0) {
        menu.page = MT_MENU_PAGE_MEDIA;
        menu.row = 1U;
    } else if (strcmp(scenario, "c64-video") == 0 ||
               strcmp(scenario, "gbc-video-off") == 0 ||
               strcmp(scenario, "gbc-video-legacy") == 0) {
        view.border_supported = strcmp(scenario, "gbc-video-legacy") != 0;
        view.border_enabled = strcmp(scenario, "c64-video") == 0;
        view.border_style = 2U;
        view.border_color = 9U;
        view.border_padding = 6U;
        menu.page = MT_MENU_PAGE_VIDEO;
        menu.row = 4U;
    } else if (strcmp(scenario, "gbc-input") == 0) {
        menu.page = MT_MENU_PAGE_INPUT;
    } else if (strcmp(scenario, "browser-long") == 0) {
        for (index = 0U; index < sizeof(files) / sizeof(files[0]); ++index) {
            (void)snprintf(files[index].name, sizeof(files[index].name),
                           "%03u - Preview cartridge with a long descriptive filename and alternate edition.gbc",
                           (unsigned)(index + 1U));
            files[index].size = 32768U * (index + 1U);
        }
        view.files = files;
        view.file_count = sizeof(files) / sizeof(files[0]);
        menu.page = MT_MENU_PAGE_MEDIA;
        mt_menu_open_browser(&menu, MT_MENU_BROWSER_CARTRIDGE);
        (void)mt_menu_key(&menu, &view, MT_MENU_HID_END, true);
        (void)mt_menu_key(&menu, &view, MT_MENU_HID_PAGE_UP, true);
        (void)mt_menu_key(&menu, &view, MT_MENU_HID_PAGE_UP, true);
    } else if (strcmp(scenario, "options-16") == 0) {
        for (index = 0U; index < MT_MENU_MAX_ITEMS; ++index) {
            (void)snprintf(option_labels[index], sizeof(option_labels[index]),
                           "Preview option %02u", (unsigned)(index + 1U));
            options[index].label = option_labels[index];
            options[index].value = "A longer setting value from a future core profile";
            options[index].help = "All sixteen profile settings fit on one page. "
                                  "This is a synthetic layout fixture.";
            options[index].enabled = true;
            options[index].value_color = MT_UI_COLOR_MUTED;
        }
        view.core_options = options;
        view.core_option_count = MT_MENU_MAX_ITEMS;
        view.status_text = "Preview fixtures: settings are illustrative only.";
        menu.page = MT_MENU_PAGE_CORE_CONFIG;
        (void)mt_menu_key(&menu, &view, MT_MENU_HID_END, true);
    } else if (strcmp(scenario, "system") == 0 || strcmp(scenario, "clock") == 0) {
        view.settings_auto_boot = false;
        snapshot.firmware_build = UINT32_C(0x55494631);
        snapshot.shell_build = UINT32_C(0x56454333);
        snapshot.abi = 12U;
        snapshot.uptime_seconds = 93784U;
        snapshot.temperature_valid = true;
        snapshot.temperature_micro_c = 42375000;
        snapshot.clock_available = snapshot.clock_valid = true;
        snapshot.clock = (mt_clock_time_t){2026U, 9U, 18U, 14U, 32U, 8U};
        snapshot.usb_started = snapshot.dac_ready = snapshot.audio_muted = true;
        snapshot.keyboards = 1U; snapshot.gamepads = 2U;
        snapshot.catalog_status = "ok"; snapshot.catalog_count = 3U;
        mt_system_frontend_apply(&system_frontend, &snapshot, &view);
        menu.page = MT_MENU_PAGE_SYSTEM;
        menu.row = 3U;
        if (strcmp(scenario, "clock") == 0)
            (void)mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    } else if (strcmp(scenario, "text-reader") == 0) {
        static const char document[] =
            "MULTITINI ONE - QUICK START\r\n\r\n"
            "Open the menu with F12. Use Tab to choose a section, arrows to select an item, and Enter to open it.\r\n\r\n"
            "READING DOCUMENTS\r\n"
            "TXT files appear beside your games. You can also use System > Read documents to browse the multitini folder.\r\n\r\n"
            "Up / Down\tScroll one line\r\n"
            "Left / Right\tScroll one page\r\n"
            "Home / End\tJump to the beginning or end\r\n"
            "Escape\t\tReturn to the same selected file\r\n\r\n"
            "The active core stays paused while you read. Opening a document does not load, eject, or replace game media.\r\n\r\n"
            "This preview uses synthetic instructions and sample status values. It is not a board capture.\r\n";
        if (mt_text_reader_open(&reader, document, sizeof(document) - 1U, false,
                               MT_MENU_TEXT_COLUMNS, MT_MENU_TEXT_ROWS) != MT_TEXT_READER_OK) return 2;
        view.text_reader = &reader; view.text_title = "Getting started.txt";
        menu.page = MT_MENU_PAGE_SYSTEM;
        menu.browser = MT_MENU_BROWSER_DOCUMENTS;
    } else if (strcmp(scenario, "catalog-16") == 0) {
        for (index = 2U; index < MT_MENU_MAX_CORES; ++index) {
            char id[24];
            char name[48];
            (void)snprintf(id, sizeof(id), "preview-%02u", (unsigned)(index + 1U));
            (void)snprintf(name, sizeof(name), "Preview core %02u with extended display name",
                           (unsigned)(index + 1U));
            core(&cores[index], id, name, "preview", "Preview video timing variant");
        }
        view.core_count = MT_MENU_MAX_CORES;
        view.status_text = "Preview fixtures: additional cores are illustrative only.";
        mt_menu_init(&menu, &view);
        menu.visible = true;
        (void)mt_menu_key(&menu, &view, MT_MENU_HID_END, true);
    } else {
        fprintf(stderr, "Unknown preview scenario: %s\n", scenario);
        return 2;
    }

    mt_menu_render(&menu, &view, &frame);
    printf("MTUI1 %u %u\n", (unsigned)MT_UI_COLUMNS, (unsigned)MT_UI_ROWS);
    for (index = 0U; index < MT_UI_COLUMNS * MT_UI_ROWS; ++index) {
        printf("%04x%c", (unsigned)frame.cells[index],
               (index + 1U) % MT_UI_COLUMNS == 0U ? '\n' : ' ');
    }
    mt_text_reader_close(&reader);
    return ferror(stdout) ? 1 : 0;
}
