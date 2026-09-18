/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_frontend.h"

#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"

#include <string.h>

typedef void (*build_profile_fn)(mt_core_frontend_t *frontend,
                                  const mt_core_frontend_state_t *state,
                                  mt_menu_view_t *view);

typedef struct {
    uint32_t core_id;
    const char *description;
    const char *input_help;
    uint32_t browser_types[MT_MENU_BROWSER_OVERLAY + 1U];
    bool disk_program_browser;
    build_profile_fn build;
} core_profile_t;

#define CONTENT_MASK(type) (UINT32_C(1) << (unsigned)(type))

static mt_menu_item_t item(const char *label, const char *value,
                            const char *help, mt_menu_action_kind_t action,
                            size_t argument, bool enabled,
                            mt_ui_color_t color)
{
    const mt_menu_item_t result = {
        label, value, help, action, argument, enabled, color
    };
    return result;
}

static const char *attached_name(const char *name)
{
    return name != NULL && name[0] != '\0' ? name : "Attached";
}

static void cartridge_item(mt_core_frontend_t *frontend,
                            const mt_core_frontend_state_t *state,
                            mt_menu_view_t *view, const char *empty_help)
{
    frontend->media_items[view->media_item_count++] = item(
        "Cartridge", state->cartridge_attached ?
            attached_name(state->cartridge_name) : "Choose file...",
        state->cartridge_attached ?
            "Enter ejects the cartridge safely before choosing another." :
            empty_help,
        state->cartridge_attached ? MT_MENU_ACTION_DETACH_CARTRIDGE :
                                    MT_MENU_ACTION_BROWSE_CARTRIDGE,
        0U, true, state->cartridge_attached ? MT_UI_COLOR_SUCCESS :
                                             MT_UI_COLOR_BLUE);
}

static void build_c64(mt_core_frontend_t *frontend,
                       const mt_core_frontend_state_t *state,
                       mt_menu_view_t *view)
{
    frontend->media_items[view->media_item_count++] = item(
        "Drive 8", state->drive8_mounted ? attached_name(state->drive8_name) :
            (state->drive8_ready ? "Choose disk..." : "Drive ROM required"),
        state->drive8_mounted ?
            "Enter flushes pending writes and ejects the mounted disk." :
            "Mount a D64 or G64 disk image in drive 8.",
        state->drive8_mounted ? MT_MENU_ACTION_EJECT_DRIVE8 :
                               MT_MENU_ACTION_BROWSE_DRIVE8,
        0U, state->drive8_ready || state->drive8_mounted,
        state->drive8_mounted ? MT_UI_COLOR_SUCCESS :
            (state->drive8_ready ? MT_UI_COLOR_BLUE : MT_UI_COLOR_WARNING));
    if (state->abi_minor >= 9U) {
        frontend->media_items[view->media_item_count++] = item(
            "Mount & Run", state->drive8_d64_mounted ? "Choose program..." :
                                                        "Mount a D64 first",
            "Choose a program from the mounted D64 and launch through drive 8.",
            MT_MENU_ACTION_BROWSE_DISK_PROGRAM, 0U,
            state->drive8_d64_mounted, state->drive8_d64_mounted ?
                MT_UI_COLOR_BLUE : MT_UI_COLOR_MUTED);
    }
    cartridge_item(frontend, state, view, "Load a CRT cartridge image.");
    frontend->media_items[view->media_item_count++] = item(
        "Program", "Choose file...", "Load a PRG file into the C64.",
        MT_MENU_ACTION_BROWSE_PROGRAM, 0U, true, MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard", state->symbolic_keyboard ? "US symbolic" :
                                                "MiSTer positional",
        "Enter changes between symbolic US keys and C64 key positions.",
        MT_MENU_ACTION_TOGGLE_KEYBOARD_MAP, 0U, true, MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++] = item(
        "Gamepad", state->joystick_port == 1U ? "Joystick port 1" :
                                                 "Joystick port 2",
        "Enter swaps the gamepad between C64 joystick ports 1 and 2.",
        MT_MENU_ACTION_SWAP_JOYSTICK_PORT, 0U, true, MT_UI_COLOR_BLUE);
    if (state->abi_minor >= 8U) {
        frontend->core_options[view->core_option_count++] = item(
            "Fast serial ROMs", state->fast_roms_active ? "On" :
                (state->fast_roms_fallback ? "Unavailable; stock" : "Off"),
            "Use your fast ROM pair; changing ROMs restarts the C64.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 0U, true,
            state->fast_roms_fallback && !state->fast_roms_active ?
                MT_UI_COLOR_WARNING : MT_UI_COLOR_BLUE);
        frontend->core_options[view->core_option_count++] = item(
            "CPU turbo", state->cpu_turbo_enabled ? "Smart 2x" : "Off",
            "Enable Smart 2x CPU acceleration; drive timing remains native.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 1U, true, MT_UI_COLOR_BLUE);
    }
}

static void build_gameboy(mt_core_frontend_t *frontend,
                           const mt_core_frontend_state_t *state,
                           mt_menu_view_t *view)
{
    const bool saves_supported = state->abi_minor >= 7U;
    const char *save_value = !saves_supported ? "Unavailable in this image" :
        (state->save_faulted ? "Write failed" :
        (state->save_dirty ? "Changes pending" :
        (state->save_active ? "Saved to SD" :
        (state->cartridge_attached ? "No battery RAM" : "No cartridge"))));
    const mt_ui_color_t save_color = !saves_supported ? MT_UI_COLOR_MUTED :
        (state->save_faulted ? MT_UI_COLOR_RED :
        (state->save_dirty ? MT_UI_COLOR_WARNING :
        (state->save_active ? MT_UI_COLOR_SUCCESS : MT_UI_COLOR_MUTED)));
    cartridge_item(frontend, state, view, "Load a GB or GBC cartridge image.");
    frontend->media_items[view->media_item_count++] = item(
        "Battery save", save_value,
        "Battery RAM saves automatically to SD; status updates here.",
        MT_MENU_ACTION_NONE, 0U, false, save_color);
    view->save_dirty = saves_supported && state->save_dirty;
    view->save_faulted = saves_supported && state->save_faulted;
    frontend->input_items[view->input_item_count++] = item(
        "Directions", "Arrow keys", "Keyboard direction controls.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Buttons", "Z = A   X = B", "Keyboard action buttons.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Start", "Enter", "Keyboard Start button.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Select", "Right Shift", "Keyboard Select button.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
}

/* Keep machine capabilities and labels together. Adding a machine does not
 * require another branch in the shared menu renderer or file browser. */
static void build_vectrex(mt_core_frontend_t *frontend,
                           const mt_core_frontend_state_t *state,
                           mt_menu_view_t *view)
{
    cartridge_item(frontend, state, view, "Load a VEC or BIN cartridge; BIOS is required.");
    frontend->media_items[view->media_item_count++] = item(
        "Overlay", "Choose file...", "Load MiSTer OVR artwork without restarting the game.",
        MT_MENU_ACTION_BROWSE_OVERLAY, 0U,
        view->vectrex_video_supported, MT_UI_COLOR_BLUE);
    frontend->media_items[view->media_item_count++] = item(
        "Vectrex32 BASIC", "Choose program...", "Run a BAS program; F12 stops it and returns here.",
        MT_MENU_ACTION_BROWSE_PROGRAM, 0U, true, MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++] = item(
        "Directions", "Analog stick / arrows", "Two gamepads; keyboard controls player 1.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Buttons 1-4", "Z X C V / A B X Y", "Four independent action buttons per player.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
}

static const core_profile_t profiles[] = {
    {
        MT_C64_CORE_ID, "Commodore 64",
        "USB keyboard and gamepad; input choices are saved per core.",
        {0U, CONTENT_MASK(MT_CONTENT_D64) | CONTENT_MASK(MT_CONTENT_G64),
         0U, CONTENT_MASK(MT_CONTENT_CRT), CONTENT_MASK(MT_CONTENT_PRG)},
        true, build_c64
    },
    {
        MT_GBC_CORE_ID, "Game Boy / Game Boy Color",
        "USB gamepad or the keyboard controls below.",
        {0U, 0U, 0U,
         CONTENT_MASK(MT_CONTENT_GB_CART) | CONTENT_MASK(MT_CONTENT_GBC_CART),
         0U},
        false, build_gameboy
    },
    {
        MT_VECTREX_CORE_ID, "Vectrex / Vectrex32 BASIC",
        "USB analog gamepads or keyboard; F12 opens the menu.",
        {0U, 0U, 0U, CONTENT_MASK(MT_CONTENT_VECTREX_CART),
         CONTENT_MASK(MT_CONTENT_VECTREX_BASIC), CONTENT_MASK(MT_CONTENT_VECTREX_OVERLAY)},
        false, build_vectrex
    }
};

static const core_profile_t *find_profile(uint32_t core_id)
{
    size_t index;
    for (index = 0U; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        if (profiles[index].core_id == core_id) return &profiles[index];
    }
    return NULL;
}

void mt_core_frontend_apply(mt_core_frontend_t *frontend,
                            const mt_core_frontend_state_t *state,
                            mt_menu_view_t *view)
{
    const core_profile_t *profile;
    if (frontend == NULL || view == NULL) return;
    memset(frontend, 0, sizeof(*frontend));
    view->media_items = frontend->media_items;
    view->input_items = frontend->input_items;
    view->core_options = frontend->core_options;
    view->media_item_count = 0U;
    view->input_item_count = 0U;
    view->core_option_count = 0U;
    view->save_dirty = false;
    view->save_faulted = false;
    profile = state == NULL ? NULL : find_profile(state->core_id);
    view->core_description = profile != NULL ? profile->description :
        (state == NULL || state->core_id == 0U ? "Choose a core to get started." :
                                               "Core controls unavailable.");
    view->input_help = profile != NULL ? profile->input_help : NULL;
    if (profile != NULL && state->services_ready)
        profile->build(frontend, state, view);
}

bool mt_core_frontend_browser_supported(uint32_t core_id,
                                        mt_menu_browser_t browser)
{
    const core_profile_t *const profile = find_profile(core_id);
    if (profile == NULL || (unsigned)browser > MT_MENU_BROWSER_OVERLAY)
        return false;
    return browser == MT_MENU_BROWSER_DISK_PROGRAM ?
        profile->disk_program_browser : profile->browser_types[browser] != 0U;
}

bool mt_core_frontend_accepts_content(uint32_t core_id,
                                      mt_menu_browser_t browser,
                                      mt_content_type_t type)
{
    const core_profile_t *const profile = find_profile(core_id);
    if (profile == NULL || (unsigned)browser > MT_MENU_BROWSER_OVERLAY ||
        (unsigned)type >= 32U) return false;
    return (profile->browser_types[browser] & CONTENT_MASK(type)) != 0U;
}
