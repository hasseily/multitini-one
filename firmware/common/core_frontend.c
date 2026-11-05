/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_frontend.h"
#include "multitini_amiga_display.h"
#include "multitini_c64_features.h"
#include "multitini_c64_content.h"

#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_spectrum_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_regs.h"

#include <string.h>
#include <stdio.h>

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
        .label = label, .value = value, .help = help, .action = action,
        .argument = argument, .enabled = enabled, .value_color = color
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

static void build_c64_sid(mt_core_frontend_t *frontend, const mt_core_frontend_state_t *state,
                          mt_menu_view_t *view)
{
    static const char *const labels[] = {"Layout",
                                         "SIDs",
                                         "Address mirroring",
                                         "Select SID",
                                         "Address",
                                         "Volume",
                                         "Pan",
                                         "Model",
                                         "Master volume",
                                         "Headroom",
                                         "DC removal",
                                         "Resampling",
                                         "Reset SID setup"};
    const mt_c64_sid_model_t *m = &state->c64_sid;
    const unsigned count = ((state->c64_expansion_caps >> 8) & 7) + 1;
    const unsigned n = state->c64_selected_sid < count ? state->c64_selected_sid : 0;
    mt_c64_sid_table_t table;
    (void)mt_c64_sid_build(
        m, count, (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_SID_MODEL_8580) != 0, &table);
    uint64_t owned = state->c64_sid_owned;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned b = 25 + 4 * i;
        if (state->c64_sid_classes & 1)
            owned |= UINT64_C(1) << b;
        if (state->c64_sid_classes & 4)
            owned |= UINT64_C(1) << (b + 1);
        if (state->c64_sid_classes & 2)
            owned |= UINT64_C(3) << (b + 2);
    }
    const unsigned b = 25 + 4 * n;
    const uint64_t masks[] = {UINT64_MAX,
                              0,
                              UINT64_C(1) << 3,
                              0,
                              UINT64_C(1) << b,
                              UINT64_C(1) << (b + 2),
                              UINT64_C(1) << (b + 3),
                              UINT64_C(1) << (b + 1),
                              UINT64_C(1) << 24,
                              UINT64_C(1) << 57,
                              UINT64_C(1) << 58,
                              UINT64_C(1) << 59,
                              UINT64_MAX};
    view->core_option_count = 13;
    for (unsigned i = 0; i < 13; ++i)
        frontend->core_options[i] = item(labels[i], frontend->c64_option_text[i],
                                         "Left/Right changes this item. Title-owned values stay "
                                         "temporary until Save. Escape returns to Options.",
                                         MT_MENU_ACTION_SID_SETUP, i, i != 1, MT_UI_COLOR_BLUE);
#define SID_TEXT(i, ...)                                                                           \
    (void)snprintf(frontend->c64_option_text[i], sizeof(frontend->c64_option_text[i]), __VA_ARGS__)
    SID_TEXT(0, "%s", mt_c64_sid_preset_name(mt_c64_sid_preset_id(m)));
    SID_TEXT(1, "%u mapped / %u available%s", table.mapped, count,
             table.dropped ? "; some addresses unavailable" : "");
    SID_TEXT(2, "%s", m->mirror ? "On" : "Off");
    SID_TEXT(3, "SID %u of %u", n + 1, count);
    if (m->sid_address[n] == 255)
        SID_TEXT(4, "Follow ($%04X)", table.base[n]);
    else if (!m->sid_address[n])
        SID_TEXT(4, "Unmapped");
    else
        SID_TEXT(4, "$%04X", mt_c64_sid_address_value(m->sid_address[n]));
    SID_TEXT(5, "%s",
             m->sid_volume[n] == 255 ? "Follow group" : mt_c64_sid_volume_name(m->sid_volume[n]));
    SID_TEXT(6, "%s", m->sid_pan[n] == 255 ? "Follow group" : mt_c64_sid_pan_name(m->sid_pan[n]));
    SID_TEXT(7, "%s", m->sid_model[n] == 255 ? "Follow group" : m->sid_model[n] ? "8580" : "6581");
    SID_TEXT(8, "%s", mt_c64_sid_volume_name(m->master));
    static const char *const headroom[] = {"Auto", "0 dB", "-6 dB", "-12 dB"};
    static const char *const dc[] = {"Auto", "On", "Off"};
    SID_TEXT(9, "%s", headroom[m->headroom]);
    SID_TEXT(10, "%s", dc[m->dc_remove]);
    SID_TEXT(11, "%s", m->resample ? "Average" : "Point");
    SID_TEXT(12, "Dual $D500 defaults");
    for (unsigned i = 0; i < 13; ++i)
        if (masks[i] & owned) {
            size_t len = strlen(frontend->c64_option_text[i]);
            (void)snprintf(frontend->c64_option_text[i] + len,
                           sizeof(frontend->c64_option_text[i]) - len, " (title)");
        }
#undef SID_TEXT
}
static void build_c64(mt_core_frontend_t *frontend,
                       const mt_core_frontend_state_t *state,
                       mt_menu_view_t *view)
{
    const bool expansion_valid = state->abi_minor >= MT_C64_EXPANSION_MIN_ABI_MINOR &&
        (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_VALID) != 0U;
    const bool extended = expansion_valid && state->abi_minor >= 26U;
    const bool reu_supported = expansion_valid &&
        (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_REU_16MB) != 0U;
    const bool coexist = expansion_valid &&
        (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_CART_COEXIST) != 0U;
    const bool reu_blocked = state->cartridge_attached &&
        (!coexist || !(state->c64_cartridge_policy & MT_C64_CRT_ALLOW_REU));
    const bool ua_blocked = state->cartridge_attached &&
        (!coexist || !(state->c64_cartridge_policy & MT_C64_CRT_ALLOW_ULTIMATE_AUDIO));
    const bool uci_blocked = coexist && !state->c64_uci_active;
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
            "Choose a program from the mounted D64, or * for the first one, and launch through drive 8.",
            MT_MENU_ACTION_BROWSE_DISK_PROGRAM, 0U,
            state->drive8_d64_mounted, state->drive8_d64_mounted ?
                MT_UI_COLOR_BLUE : MT_UI_COLOR_MUTED);
    }
    cartridge_item(frontend, state, view, "Load a CRT cartridge image.");
    if (state->save_active) {
        frontend->media_items[view->media_item_count++] = item(
            "Cartridge save", state->save_faulted ? "Write failed" :
                (state->save_dirty ? "Changes pending" : "Saved to SD"),
            "EasyFlash writes save to the CRT's .sav file; delete it to restore the CRT.",
            MT_MENU_ACTION_NONE, 0U, false,
            state->save_faulted ? MT_UI_COLOR_RED :
                (state->save_dirty ? MT_UI_COLOR_WARNING : MT_UI_COLOR_SUCCESS));
        view->save_dirty = state->save_dirty;
        view->save_faulted = state->save_faulted;
        frontend->media_items[view->media_item_count++] = item(
            "Export cartridge", "Write CRT copy",
            "Write the current flash to NAME-saved.crt beside the CRT, for VICE, Ultimate or a "
            "real EasyFlash. NAME.sav and the CRT stay unchanged.",
            MT_MENU_ACTION_EXPORT_CARTRIDGE, 0U, true, MT_UI_COLOR_BLUE);
        frontend->media_items[view->media_item_count++] = item(
            "Boot jumper", state->c64_ef_jumper_disabled ? "Disable" : "Boot",
            "Boot starts the cartridge at reset; Disable starts BASIC with it hidden. "
            "Applies at the next reset.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 21U, true, MT_UI_COLOR_BLUE);
    }
    frontend->media_items[view->media_item_count++] = item(
        "Program", "Choose file...", "Load a PRG file into the C64.",
        MT_MENU_ACTION_BROWSE_PROGRAM, 0U, true, MT_UI_COLOR_BLUE);
    frontend->media_items[view->media_item_count++] =
        item("Apply configuration", "Choose .cfg file...",
             "Apply a configuration to the running session, including .cfg/.reu pairs. Automatic "
             "files Off does not disable this action.",
             MT_MENU_ACTION_BROWSE_CONFIG, 0U, true, MT_UI_COLOR_BLUE);
    if (state->c64_reu_host_available)
        frontend->media_items[view->media_item_count++] = item(
            "Load into REU", "Choose image...",
            "Load a raw .reu image into expansion memory without resetting the C64.",
            MT_MENU_ACTION_BROWSE_REU, 0U, !reu_blocked, MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard", state->symbolic_keyboard ? "US symbolic" :
                                                "MiSTer positional",
        "Enter changes between symbolic US keys and C64 key positions.",
        MT_MENU_ACTION_TOGGLE_KEYBOARD_MAP, 0U, true, MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard joystick", state->c64_title_owned & MT_TITLE_JOYSTICK ?
            (state->joystick_port == 1U ? "Joystick port 1 (title)" : "Joystick port 2 (title)") :
            (state->joystick_port == 1U ? "Joystick port 1" : "Joystick port 2"),
        "Choose the C64 joystick port used by keyboard joystick controls.",
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
        frontend->core_options[0].control = MT_MENU_CONTROL_CHECKBOX;
        frontend->core_options[0].control_value = state->fast_roms_active ? 1U : 0U;
        frontend->core_options[1].control = MT_MENU_CONTROL_CHECKBOX;
        frontend->core_options[1].control_value = state->cpu_turbo_enabled ? 1U : 0U;
        if (state->c64_turbo_available) {
            static const char *const modes[] = {"Off", "Manual", "U64 registers", "TurboEnable bit"};
            const uint8_t detail = mt_c64_turbo_detail(state->c64_turbo_detail);
            const unsigned index = mt_c64_clamp_speed(detail & 15U, (uint16_t)state->c64_cpu_caps);
            const unsigned requested = mt_c64_speed_mhz(index);
            const unsigned effective = mt_c64_speed_mhz(state->c64_cpu_status & 15U);
            const unsigned mode = state->cpu_turbo_enabled ? detail >> 6 : 0U;
            const uint32_t status = state->c64_cpu_status;
            const char *reason = mt_c64_turbo_reason(status);
            (void)snprintf(frontend->c64_turbo_text, sizeof(frontend->c64_turbo_text),
                "%s: %u MHz, live $%02X, now %u MHz (%s)", modes[mode], requested,
                (unsigned)((status >> 8) & 255U), effective, reason);
            const char *hint = (status & MT_C64_CPU_STATUS_REGISTER_REQUEST) ?
                "Software tried to set the CPU speed; select U64 registers" :
                (status & MT_C64_CPU_STATUS_TURBO_ENABLE_REQUEST) ?
                "Software tried to use the C128 turbo bit; select TurboEnable bit" :
                (status & MT_C64_CPU_STATUS_SOFTWARE_SPEED) ? "Speed set by software" :
                mode == 3 ? "For C128-aware games; try CPU speed 2 MHz for C128 border timing." :
                "Choose Off, Manual, U64 registers or TurboEnable bit.";
            (void)snprintf(frontend->c64_turbo_help, sizeof(frontend->c64_turbo_help), "%s", hint);
            frontend->core_options[1] = item("CPU turbo", frontend->c64_turbo_text,
                frontend->c64_turbo_help, MT_MENU_ACTION_TOGGLE_CORE_OPTION, 1U, true, MT_UI_COLOR_BLUE);
            if (index == 0 && (detail & 0x20U))
                (void)snprintf(frontend->c64_speed_text, sizeof(frontend->c64_speed_text),
                    "1 MHz, badlines off (not cycle-exact)");
            else (void)snprintf(frontend->c64_speed_text, sizeof(frontend->c64_speed_text), "%u MHz%s",
                    requested, index != (detail & 15U) ? " (image limit)" : "");
            frontend->core_options[view->core_option_count++] = item("CPU speed", frontend->c64_speed_text,
                "Speed in multiples of the C64 clock; 48 and 64 MHz give 48x and 64x.",
                MT_MENU_ACTION_TOGGLE_CORE_OPTION, 4U, true, MT_UI_COLOR_BLUE);
            frontend->core_options[view->core_option_count++] = item("Badline timing",
                (detail & 0x20U) ? "Disabled" : "Enabled",
                "Disabled: the CPU and internal I/O ignore badlines, like the Ultimate 64. Timing-sensitive code may break.",
                MT_MENU_ACTION_TOGGLE_CORE_OPTION, 5U, true, MT_UI_COLOR_BLUE);
            frontend->core_options[view->core_option_count++] = item("Disk auto-slow",
                (state->c64_turbo_flags & 1U) ? "Off" : "On",
                "Off: fast loaders, JiffyDOS and GEOS may crash, and disk writes can be corrupted.",
                MT_MENU_ACTION_TOGGLE_CORE_OPTION, 6U, true, MT_UI_COLOR_BLUE);
            if (state->c64_cpu_caps & MT_C64_CPU_CAPS_CARTRIDGE_TURBO)
                frontend->core_options[view->core_option_count++] = item("Cartridge turbo",
                    (state->c64_turbo_flags & 2U) ? "Forces 1 MHz" : "Allowed", "Choose cartridge speed policy.",
                    MT_MENU_ACTION_TOGGLE_CORE_OPTION, 7U, true, MT_UI_COLOR_BLUE);
            if (state->c64_cpu_caps2 & MT_C64_CPU_CAPS2_FOLLOW_CPU)
                frontend->core_options[view->core_option_count++] = item("Expansion memory speed",
                    (detail & 0x10U) ? "Follow CPU" : "C64U (1 byte per cycle)", "Choose REU transfer timing.",
                    MT_MENU_ACTION_TOGGLE_CORE_OPTION, 10U, true, MT_UI_COLOR_BLUE);
        }
    }
    if (expansion_valid && (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_DUAL_SID) != 0U) {
        frontend->core_options[view->core_option_count++] = item(
            "SID audio", state->c64_sid_mode == 2U ? "Dual $D420" :
                state->c64_sid_mode == 3U ? "Dual $D500" : "Single",
            "Cycle single SID or dual 6581 stereo at $D420 or $D500.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 2U, true, MT_UI_COLOR_BLUE);
    }
    if (extended && (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_MULTI_SID)) {
        for (size_t i = 0; i < view->core_option_count; ++i)
            if (frontend->core_options[i].argument == 2) {
                frontend->core_options[i].value =
                    mt_c64_sid_preset_name(mt_c64_sid_preset_id(&state->c64_sid));
                frontend->core_options[i].help =
                    "Choose a SID layout; SID setup edits individual addresses, pan and volume.";
            }
    }
    if (reu_supported) {
        const size_t index = view->core_option_count++;
        frontend->core_options[index] = item(
            "RAM expansion", state->c64_reu_enabled ?
                (coexist && reu_blocked ? "Blocked by cartridge" : "16 MB REU") : "Off",
            coexist ? "Enable the 16 MB DDR REU. A conflicting cartridge temporarily blocks it; the saved choice is retained." :
            (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_REU_DDR) != 0U ?
                "Enable the 16 MB DDR REU; eject any cartridge first." :
                "Enable the 16 MB REU; eject any cartridge first.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 3U, true, MT_UI_COLOR_BLUE);
        frontend->core_options[index].control = MT_MENU_CONTROL_CHECKBOX;
        frontend->core_options[index].control_value = state->c64_reu_enabled ? 1U : 0U;
    }
    if (state->c64_ua_available) {
        frontend->core_options[view->core_option_count++] = item("Ultimate Audio $DF20-$DFFF",
            state->c64_ua_fault ? "Fault" : state->c64_ua_enabled ?
                (coexist && ua_blocked ? "Blocked by cartridge" : "On") :
                (!coexist && state->cartridge_attached) ? "Eject cartridge first" : "Off",
            state->c64_ua_fault ? "Reload the C64 core to clear the memory fault." :
                coexist && ua_blocked ? "This cartridge blocks sample audio. The requested setting returns after eject." :
                "Seven sample voices in REU memory. On also enables the 16 MB REU; Expand needs this off.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 8U,
            !state->c64_ua_fault && (coexist || state->c64_ua_enabled || !state->cartridge_attached),
            state->c64_ua_fault ? MT_UI_COLOR_WARNING : MT_UI_COLOR_BLUE);
    }
    if (state->abi_minor >= 26U && state->c64_turbo_available &&
        (state->c64_cpu_caps2 & MT_C64_CPU_CAPS2_SCPU_DETECT))
        frontend->core_options[view->core_option_count++] =
            item("SuperCPU Detect", state->c64_turbo_flags & 8 ? "On" : "Off",
                 "Only enables detection registers; this is still an NMOS 6510. Turn off or Revert "
                 "title settings before GEOS.",
                 MT_MENU_ACTION_TOGGLE_CORE_OPTION, 14, true, MT_UI_COLOR_BLUE);
    if (extended && (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_UCI))
        frontend->core_options[view->core_option_count++] =
            item("Command Interface", state->c64_uci_enabled ?
                 (uci_blocked ? "Blocked by cartridge" : "On") :
                 (coexist && state->c64_uci_active ? "On (guest)" : "Off"),
                 "Allow C64 programs to access the rooted command interface.",
                 MT_MENU_ACTION_TOGGLE_CORE_OPTION, 15, true, MT_UI_COLOR_BLUE);
    if (extended && (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_MULTI_SID))
        frontend->core_options[view->core_option_count++] =
            item("SID setup...", "Addresses, pan and volume",
                 "Edit all SID channels and output settings. Escape returns to Options.",
                 MT_MENU_ACTION_TOGGLE_CORE_OPTION, 16, true, MT_UI_COLOR_BLUE);
    if (state->c64_directory_drive_available) {
        const size_t index = view->core_option_count++;
        frontend->core_options[index] = item("SD directory drive",
            state->c64_directory_drive_enabled ? "On" : "Off",
            "Expose the SD directory as drive 9. Turn Off for loaders that require one drive; UCI storage remains available.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, 20U, true, MT_UI_COLOR_BLUE);
        frontend->core_options[index].control = MT_MENU_CONTROL_CHECKBOX;
        frontend->core_options[index].control_value = state->c64_directory_drive_enabled ? 1U : 0U;
    }
    frontend->core_options[view->core_option_count++] =
        item("Title .cfg files", state->c64_title_automatic ? "On" : "Off",
             "Automatic sidecars on launch; manual Apply remains available when Off.",
             MT_MENU_ACTION_TOGGLE_CORE_OPTION, 17, true, MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++] =
        item("Revert title settings",
             state->c64_title_source && *state->c64_title_source ? state->c64_title_source
                                                                 : "No title overrides",
             "Restore saved defaults and end every title override, including settings loaded "
             "through UCI.",
             MT_MENU_ACTION_TOGGLE_CORE_OPTION, 18, state->c64_title_owned != 0, MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++] =
        item("Save settings for this title",
             state->c64_title_session ? "Write title .cfg" : "Launch or Apply first",
             "Journal current requested settings beside this title, preserving unsupported items "
             "and comments.",
             MT_MENU_ACTION_TOGGLE_CORE_OPTION, 19, state->c64_title_session, MT_UI_COLOR_BLUE);
    static const uint32_t group[20] = {0,
                                       MT_TITLE_TURBO,
                                       MT_TITLE_SID,
                                       MT_TITLE_MEMORY,
                                       MT_TITLE_SPEED,
                                       MT_TITLE_BADLINE,
                                       MT_TITLE_AUTOSLOW,
                                       MT_TITLE_CART,
                                       MT_TITLE_MEMORY,
                                       MT_TITLE_TURBO,
                                       MT_TITLE_FOLLOW,
                                       0,
                                       MT_TITLE_TURBO | MT_TITLE_SPEED,
                                       MT_TITLE_TURBO | MT_TITLE_SPEED,
                                       MT_TITLE_SCPU,
                                       MT_TITLE_UCI,
                                       MT_TITLE_SID,
                                       0,
                                       0,
                                       0};
    for (size_t i = 0; i < view->core_option_count; ++i) {
        mt_menu_item_t *row = &frontend->core_options[i];
        if (row->argument < 20 && (group[row->argument] & state->c64_title_owned)) {
            (void)snprintf(frontend->c64_option_text[i], sizeof(frontend->c64_option_text[i]),
                           "%.80s (title)", row->value);
            row->value = frontend->c64_option_text[i];
        }
    }
    if (extended && state->c64_sid_setup &&
        (state->c64_expansion_caps & MT_C64_EXPANSION_CAPS_MULTI_SID))
        build_c64_sid(frontend, state, view);
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
    cartridge_item(frontend, state, view, state->core_id == MT_NES_CORE_ID ?
        "Load a supported NTSC iNES cartridge image." : "Load a GB or GBC cartridge image.");
    frontend->media_items[view->media_item_count++] = item(
        "Battery save", save_value,
        "Battery RAM saves automatically to SD; status updates here.",
        MT_MENU_ACTION_NONE, 0U, false, save_color);
    view->save_dirty = saves_supported && state->save_dirty;
    view->save_faulted = saves_supported && state->save_faulted;
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard directions", "Arrow keys", "Keyboard direction controls.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard buttons", "Z = A   X = B", "Keyboard action buttons.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard Start", "Enter", "Keyboard Start button.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard Select", "Right Shift", "Keyboard Select button.",
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
        "Keyboard directions", "Arrow keys", "Keyboard controls player 1. Configure each controller separately.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard buttons 1-4", "Z X C V", "Four independent action buttons per player.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
}

static void build_genesis(mt_core_frontend_t *frontend,
                          const mt_core_frontend_state_t *state,
                          mt_menu_view_t *view)
{
    cartridge_item(frontend, state, view, "Load an MD, GEN, BIN or SMD cartridge image.");
    frontend->media_items[view->media_item_count++] = item(
        "Cartridge save", state->save_faulted ? "Write failed" :
            (state->save_dirty ? "Changes pending" :
             (state->save_active ? "Saved to SD" : "No cartridge")),
        "SRAM and EEPROM changes save automatically; eject waits for a safe flush.",
        MT_MENU_ACTION_NONE, 0U, false,
        state->save_faulted ? MT_UI_COLOR_RED :
            (state->save_dirty ? MT_UI_COLOR_WARNING :
             (state->save_active ? MT_UI_COLOR_SUCCESS : MT_UI_COLOR_MUTED)));
    view->save_dirty = state->save_dirty;
    view->save_faulted = state->save_faulted;
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard directions", "Arrow keys", "Keyboard controls player 1. Configure each controller separately.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard A / B / C", "Z / X / C", "Keyboard action buttons for player 1.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard X / Y / Z", "A / S / D", "Keyboard action buttons for player 1.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item(
        "Keyboard Start / Mode", "Enter / Right Shift", "Keyboard Start and Mode controls for player 1.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    for (unsigned player = 0U; player < 2U; ++player) {
        const bool three = (state->genesis_three_button_mask & (1U << player)) != 0U;
        frontend->core_options[view->core_option_count++] = item(
            player == 0U ? "Controller 1" : "Controller 2",
            three ? "3 buttons" : "6 buttons",
            "Change controller protocol. Reloads the cartridge after saving its RAM.",
            MT_MENU_ACTION_TOGGLE_CORE_OPTION, player, true, MT_UI_COLOR_BLUE);
    }
}

static void build_iigs(mt_core_frontend_t *frontend,
    const mt_core_frontend_state_t *state, mt_menu_view_t *view)
{
    static const char *const labels[] = {"Hard disk 1", "Hard disk 2", "3.5-inch floppy", "5.25-inch floppy",
        "Hard disk 3", "Hard disk 4", "Hard disk 5", "Hard disk 6", "Hard disk 7", "Hard disk 8"};
    static const char *const speeds[] = {"2.8 MHz (stock)","3.6 MHz","4.8 MHz","7.2 MHz","14.3 MHz","28.6 MHz","57.3 MHz"};
    const uint32_t speed_mask=state->iigs_speed_mask!=0U ? state->iigs_speed_mask:MT_IIGS_ACCEL_LEGACY_SPEED_MASK;
    const bool speed_valid=state->iigs_config.speed<MT_IIGS_SPEED_COUNT &&
        (speed_mask&(UINT32_C(1)<<state->iigs_config.speed))!=0U;
    for(unsigned d=0U;d<MT_IIGS_DRIVE_COUNT;++d) {
        const char *name=state->iigs_drive_name[d];
        frontend->media_items[view->media_item_count++]=item(labels[d],name!=NULL ? name:"Choose disk...",
            name!=NULL ? "Flush writes and eject this disk.":mt_iigs_drive_hdd_unit(d)<MT_IIGS_HDD_COUNT ? "Mount HDV, ProDOS-order PO or ProDOS 2MG; capacity is checked before mounting.":
                d==2U ? "Mount an 800 KiB PO or 2MG, or a 3.5-inch WOZ2.":
                "Mount a 140 KiB DOS-order DSK or ProDOS-order PO, or a 5.25-inch WOZ2.",
            name!=NULL ? MT_MENU_ACTION_EJECT_IIGS_DRIVE:MT_MENU_ACTION_BROWSE_IIGS_DRIVE,d,true,name!=NULL ? MT_UI_COLOR_SUCCESS:MT_UI_COLOR_BLUE);
    }
    frontend->media_items[view->media_item_count++]=item("System ROMs",state->iigs_ready ? "Loaded":"Load / retry...",
        "Load user-supplied system, character and ADB ROMs from the roms folder.",MT_MENU_ACTION_IIGS_RELOAD,0U,true,state->iigs_ready ? MT_UI_COLOR_SUCCESS:MT_UI_COLOR_WARNING);
    frontend->media_items[view->media_item_count++]=item("Save PRAM","Save now","Save the current control-panel settings safely to SD.",MT_MENU_ACTION_IIGS_PRAM,0U,state->iigs_ready,MT_UI_COLOR_BLUE);
    frontend->media_items[view->media_item_count++]=item("Load PRAM","Restore saved","Restore saved control-panel settings and warm-reset the machine.",MT_MENU_ACTION_IIGS_PRAM,1U,state->iigs_ready,MT_UI_COLOR_BLUE);
    frontend->media_items[view->media_item_count++]=item("Retry media",state->save_faulted ? "I/O error; retry":"No errors",
        "Retry a failed SD read or durable disk write without losing its sector.",MT_MENU_ACTION_IIGS_PRAM,2U,state->save_faulted,MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++]=item("Keyboard","USB keyboard","F12 opens this menu; Ctrl+F11 resets; Windows keys are Apple keys.",MT_MENU_ACTION_NONE,0U,false,MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++]=item("Mouse","USB boot mouse","Mouse movement and buttons reach the Apple IIgs ADB controller.",MT_MENU_ACTION_NONE,0U,false,MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++]=item("Joystick","USB analog gamepad","Stick and three fire buttons; configure bindings in Controllers.",MT_MENU_ACTION_NONE,0U,false,MT_UI_COLOR_TEXT);
    const bool zipgs=state->iigs_config.card==0U;
    frontend->core_options[view->core_option_count++]=item("CPU acceleration",!zipgs ? speeds[0]:speed_valid ? speeds[state->iigs_config.speed]:"Unavailable",
        zipgs ? "Choose a 65816 speed. Peripheral timing and compatibility delays stay active.":
            "No accelerator card: stock speed. Select ZipGS to enable acceleration.",
        MT_MENU_ACTION_TOGGLE_CORE_OPTION,0U,zipgs,MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("ROM version",state->iigs_config.rom3 ? "ROM 3":"ROM 1",
        "Changing ROM restarts the machine after saving and ejecting media.",MT_MENU_ACTION_TOGGLE_CORE_OPTION,1U,true,MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("Accelerator card",state->iigs_config.card==0U ? "ZipGS":"None",
        "None runs at stock speed. Changing cards saves media and restarts the IIgs to clear ZipGS state.",MT_MENU_ACTION_TOGGLE_CORE_OPTION,2U,true,MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("Force self test",(state->iigs_config.pulses&1U)!=0U ? "On":"Off",
        "Run Apple's self test on reset.",MT_MENU_ACTION_TOGGLE_CORE_OPTION,3U,true,MT_UI_COLOR_BLUE);
    static const char *const options[]={"Speaker delay","Joystick delay","Counter delay","Sync to 1 MHz","IRQ delay"};
    for(unsigned i=0U;i<5U;++i) {
        const bool bit=(state->iigs_config.flags&(1U<<i))!=0U;
        const bool enabled=i==3U ? bit:!bit;
        frontend->core_options[view->core_option_count++]=item(options[i],enabled ? "On":"Off",
            i==4U ? "AppleTalk compatibility: slows the CPU while interrupts are disabled. Leave Off unless needed.":
                "Apply this upstream acceleration compatibility setting.",MT_MENU_ACTION_TOGGLE_CORE_OPTION,4U+i,true,MT_UI_COLOR_BLUE);
    }
    static const uint16_t mouse_steps[]={25U,50U,75U,100U,150U,200U,300U,400U};
    static const char *const mouse_values[]={"25%","50%","75%","100%","150%","200%","300%","400%"};
    const char *mouse_value="100%";
    for(size_t i=0U;i<sizeof(mouse_steps)/sizeof(mouse_steps[0]);++i)
        if(state->iigs_mouse_sensitivity==mouse_steps[i])mouse_value=mouse_values[i];
    frontend->core_options[view->core_option_count++]=item("Mouse sensitivity",mouse_value,
        "Adjust USB mouse movement immediately; 100% is normal. Saved for Apple IIgs.",
        MT_MENU_ACTION_TOGGLE_CORE_OPTION,9U,true,MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("Floppy speed",
        !state->iigs_fast_floppy_supported ? "Unavailable":
            (state->iigs_config.flags&MT_IIGS_FLAG_FAST_FLOPPY)!=0U ? "Fast":"Normal",
        state->iigs_fast_floppy_supported ?
            "Fast uses 4x floppy timing with ZipGS at 14 MHz or higher. Changing saves media and restarts the IIgs.":
            "Update the Apple IIgs FPGA core to enable Fast floppy.",
        MT_MENU_ACTION_TOGGLE_CORE_OPTION,10U,state->iigs_fast_floppy_supported,MT_UI_COLOR_BLUE);
    view->save_faulted=state->save_faulted;
}

static void build_amiga(mt_core_frontend_t *frontend,const mt_core_frontend_state_t *state,mt_menu_view_t *view)
{
    static const char *const labels[]={"DF0: DD floppy","DF1: DD floppy","IDE master","IDE slave"};
    for(unsigned d=0U;d<MT_AMIGA_DRIVE_COUNT;++d) {
        const char *name=state->amiga_drive_name[d];
        frontend->media_items[view->media_item_count++]=item(labels[d],name!=NULL ? name:"Choose disk...",
            name!=NULL ? "Finish disk writes and eject this image.":d<2U ? "Mount an 880 KiB DD ADF image.":"Mount a raw HDF hard disk with 512-byte sectors.",
            name!=NULL ? MT_MENU_ACTION_EJECT_AMIGA_DRIVE:MT_MENU_ACTION_BROWSE_AMIGA_DRIVE,d,true,name!=NULL ? MT_UI_COLOR_SUCCESS:MT_UI_COLOR_BLUE);
    }
    frontend->media_items[view->media_item_count++]=item("Kickstart ROM",state->amiga_ready ? "Loaded":"Load / retry...",
        state->amiga_aga_pal ? "Load your A1200 ROM from roms/kick-a1200.rom." :
        "Load your 256 KiB, 512 KiB or 1 MiB roms/kick.rom.",MT_MENU_ACTION_AMIGA_RELOAD,0U,true,state->amiga_ready ? MT_UI_COLOR_SUCCESS:MT_UI_COLOR_WARNING);
    frontend->media_items[view->media_item_count++]=item("Retry media",state->save_faulted ? "I/O error; retry":"No errors",
        "Retry a retained disk journal or failed transfer.",MT_MENU_ACTION_AMIGA_RETRY,0U,state->save_faulted,MT_UI_COLOR_BLUE);
    const char *const cpu=state->amiga_ap68040 ?
        (state->amiga_config.cpu==MT_AMIGA_CPU_AP68040_2X ? "56.75 MHz":"28 MHz"):
        state->amiga_config.cpu==MT_AMIGA_CPU_STOCK ?
        (state->amiga_aga_pal ? "68EC020 stock":"68000 stock"):
        state->amiga_config.cpu==MT_AMIGA_CPU_68020_14 ? "68020 14 MHz":"68020 28 MHz";
    frontend->core_options[view->core_option_count++]=item(state->amiga_ap68040 ? "CPU speed":"CPU",cpu,
        state->amiga_ap68040 ? (state->amiga_ap68040_2x_available ?
            "Switch between 28 MHz and 56.75 MHz. Changing speed saves disks and restarts Amiga.":
            "56.75 MHz requires an updated AP68040 FPGA image."):
        state->amiga_aga_pal ? "68EC020 stock or 68020 accelerated. Changing CPU saves disks and restarts.":
        state->amiga_cpu14_fallback ? "14 MHz unavailable on this core; using 28 MHz. Saved preference retained.":
        state->amiga_cpu14_available ? "Stock 68000, 68020 14 MHz, 68020 28 MHz. Changing CPU saves disks and restarts.":
        "Stock 68000 or 68020 28 MHz. 14 MHz requires an updated core. Changing CPU saves disks and restarts.",
        MT_MENU_ACTION_TOGGLE_CORE_OPTION,0U,
        !state->amiga_ap68040 || (state->amiga_ap68040_2x_available && !state->amiga_display_pending),MT_UI_COLOR_BLUE);
    const char *const requested=mt_amiga_display_mode_name(state->amiga_display_requested);
    const bool verified=state->amiga_display_applied<MT_AMIGA_DISPLAY_MODES;
    const char *const applied=verified ? mt_amiga_display_mode_name(state->amiga_display_applied):"unverified";
    if(!state->amiga_display_available)
        (void)snprintf(frontend->amiga_resolution_text,sizeof(frontend->amiga_resolution_text),"Unavailable");
    else if(!verified || state->amiga_display_pending || state->amiga_display_requested!=state->amiga_display_applied)
        (void)snprintf(frontend->amiga_resolution_text,sizeof(frontend->amiga_resolution_text),"Requested %s; active %s",requested,applied);
    else (void)snprintf(frontend->amiga_resolution_text,sizeof(frontend->amiga_resolution_text),"%s",applied);
    (void)snprintf(frontend->amiga_resolution_help,sizeof(frontend->amiga_resolution_help),"%s. %s",
        state->amiga_display_status!=NULL ? state->amiga_display_status:"Install the MTDisplay guest helper",
        state->amiga_display_available ? state->amiga_display_mode_count>=4U ?
            "Changes Workbench without resetting the guest; an Amiga requester may need confirmation.":
            "1280 x 512 requires an updated core and guest helper. Resolution changes do not reset the guest.":
                                         "Install MTDisplay in the Amiga and use a display-capable core.");
    frontend->core_options[view->core_option_count++]=item("Resolution",frontend->amiga_resolution_text,
        frontend->amiga_resolution_help,MT_MENU_ACTION_AMIGA_DISPLAY,state->amiga_display_requested,
        state->amiga_display_available && !state->amiga_display_pending,
        state->amiga_display_pending ? MT_UI_COLOR_WARNING:MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("Chipset",
        state->amiga_aga_pal ? "AGA PAL / 2 MiB chip RAM":"ECS PAL / 2 MiB chip RAM",
        "CPU speed is independent of the PAL chipset clock.",MT_MENU_ACTION_NONE,1U,false,MT_UI_COLOR_TEXT);
    frontend->core_options[view->core_option_count++]=item("Fast RAM",state->amiga_config.fast_mib==4U ? "4 MiB":"8 MiB",
        "Zorro II fast memory; chipset always has 2 MiB chip RAM.",MT_MENU_ACTION_TOGGLE_CORE_OPTION,2U,true,MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("Floppy speed",state->amiga_config.floppy_turbo ? "Fast":"Normal",
        "Normal DD drive timing provides the widest compatibility.",MT_MENU_ACTION_TOGGLE_CORE_OPTION,3U,true,MT_UI_COLOR_BLUE);
    frontend->core_options[view->core_option_count++]=item("IDE mode",state->amiga_config.ide_compat==MT_AMIGA_IDE_CLASSIC ? "Classic":
        state->amiga_fast_ide_expected ? "Auto / Fast":"Auto / Classic",
        !state->amiga_fast_ide_available ? "Fast IDE requires an updated core. Saved policy retained; Classic is active. Changing mode restarts Amiga; save your work first.":
        state->amiga_config.cpu==MT_AMIGA_CPU_STOCK ? "Stock CPU mode uses Classic IDE. Auto enables Fast IDE with acceleration. Changing mode restarts Amiga.":
        "Auto enables Fast IDE in accelerated CPU modes. Classic retains Gayle compatibility. Changing mode restarts Amiga.",
        MT_MENU_ACTION_TOGGLE_CORE_OPTION,4U,!state->amiga_display_pending,MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++]=item("Keyboard","USB keyboard","Windows keys are Amiga keys; Menu is Right Amiga; Ctrl + both Amiga restarts. F12 opens this menu.",MT_MENU_ACTION_NONE,0U,false,MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++]=item("Mouse","USB mouse","Relative movement and three mouse buttons.",MT_MENU_ACTION_NONE,0U,false,MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++]=item("Joysticks","Two USB gamepads","Digital joystick directions and two fire buttons per port.",MT_MENU_ACTION_NONE,0U,false,MT_UI_COLOR_TEXT);
    view->save_faulted=state->save_faulted;
}
static void build_spectrum(mt_core_frontend_t *frontend,
                           const mt_core_frontend_state_t *state, mt_menu_view_t *view)
{
    frontend->media_items[view->media_item_count++] = item("System ROM",
        state->spectrum_ready ? "Loaded" : "Load / retry...",
        state->spectrum_boot_error != NULL ? state->spectrum_boot_error :
            "Supply a 16 KiB 48K or 32 KiB 128K ROM as roms/spectrum.rom.",
        MT_MENU_ACTION_SPECTRUM_RELOAD, 0U, true,
        state->spectrum_ready ? MT_UI_COLOR_SUCCESS : MT_UI_COLOR_WARNING);
    frontend->media_items[view->media_item_count++] = item("Tape",
        state->spectrum_tape_name != NULL ? state->spectrum_tape_name : "Choose TAP image...",
        "Start the Spectrum tape loader, then press F1 to play or pause. F2 rewinds.",
        MT_MENU_ACTION_BROWSE_PROGRAM, 0U, state->spectrum_ready, MT_UI_COLOR_BLUE);
    frontend->input_items[view->input_item_count++] = item("Keyboard", "USB keyboard",
        "F1 plays or pauses the tape. F2 rewinds. F12 opens the Multitini menu.",
        MT_MENU_ACTION_NONE, 0U, false, MT_UI_COLOR_TEXT);
    frontend->input_items[view->input_item_count++] = item("Joystick", "Kempston",
        "Use a USB gamepad for directions and fire.", MT_MENU_ACTION_NONE,
        0U, false, MT_UI_COLOR_TEXT);
}

static const core_profile_t profiles[] = {
    {
        MT_SPECTRUM_CORE_ID, "Sinclair ZX Spectrum",
        "USB keyboard and Kempston joystick. F12 opens the Multitini menu.",
        {0U, 0U, 0U, 0U, CONTENT_MASK(MT_CONTENT_SPECTRUM_TAP)},
        false, build_spectrum
    },
    {
        MT_AMIGA_CORE_ID,"Amiga",
        "USB keyboard, mouse and two digital joysticks; F12 opens the menu.",
        {0U,CONTENT_MASK(MT_CONTENT_AMIGA_ADF)|CONTENT_MASK(MT_CONTENT_AMIGA_HDF),0U,0U,0U},
        false,build_amiga
    },
    {
        MT_NES_CORE_ID, "Nintendo Entertainment System",
        "Two USB gamepads or keyboard; F12 opens the menu.",
        {0U, 0U, 0U, CONTENT_MASK(MT_CONTENT_NES_CART), 0U},
        false, build_gameboy
    },
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
    },
    {
        MT_GENESIS_CORE_ID, "Genesis / Mega Drive",
        "Two gamepads with six action buttons or the keyboard controls below.",
        {0U, 0U, 0U, CONTENT_MASK(MT_CONTENT_GENESIS_CART) |
                         CONTENT_MASK(MT_CONTENT_GENESIS_SMD), 0U},
        false, build_genesis
    },
    {
        MT_IIGS_CORE_ID, "Apple IIgs",
        "USB keyboard, mouse and analog gamepad; F12 opens the menu.",
        {0U, CONTENT_MASK(MT_CONTENT_IIGS_HDD)|CONTENT_MASK(MT_CONTENT_IIGS_WOZ)|CONTENT_MASK(MT_CONTENT_IIGS_FLOPPY),0U,0U,0U},
        false, build_iigs
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
    if (browser == MT_MENU_BROWSER_REU || browser == MT_MENU_BROWSER_CONFIG)
        return core_id == MT_C64_CORE_ID;
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
    if (browser == MT_MENU_BROWSER_REU)
        return core_id == MT_C64_CORE_ID && type == MT_CONTENT_C64_REU;
    const core_profile_t *const profile = find_profile(core_id);
    if (profile == NULL || (unsigned)browser > MT_MENU_BROWSER_OVERLAY ||
        (unsigned)type >= 32U) return false;
    return (profile->browser_types[browser] & CONTENT_MASK(type)) != 0U;
}

bool mt_core_frontend_browser_folders(uint32_t core_id, mt_menu_browser_t browser)
{
    return browser == MT_MENU_BROWSER_DOCUMENTS ||
           (core_id == MT_C64_CORE_ID &&
            (browser == MT_MENU_BROWSER_REU || browser == MT_MENU_BROWSER_CONFIG ||
             browser == MT_MENU_BROWSER_PROGRAM || browser == MT_MENU_BROWSER_CARTRIDGE ||
             browser == MT_MENU_BROWSER_DRIVE8)) ||
           ((core_id == MT_VECTREX_CORE_ID || core_id == MT_IIGS_CORE_ID ||
             core_id == MT_AMIGA_CORE_ID || core_id == MT_SPECTRUM_CORE_ID) &&
            mt_core_frontend_browser_supported(core_id, browser));
}

bool mt_core_frontend_accepts_drive_content(uint32_t core_id, mt_menu_browser_t browser,
                                           unsigned drive, mt_content_type_t type)
{
    if(core_id==MT_AMIGA_CORE_ID && browser==MT_MENU_BROWSER_DRIVE8)
        return drive<MT_AMIGA_DRIVE_COUNT && type==(drive<2U ? MT_CONTENT_AMIGA_ADF:MT_CONTENT_AMIGA_HDF);
    if(core_id==MT_IIGS_CORE_ID && browser==MT_MENU_BROWSER_DRIVE8) {
        if(drive>=MT_IIGS_DRIVE_COUNT) return false;
        if(mt_iigs_drive_hdd_unit(drive)<MT_IIGS_HDD_COUNT) return type==MT_CONTENT_IIGS_HDD;
        return type==MT_CONTENT_IIGS_WOZ || type==MT_CONTENT_IIGS_HDD ||
            (drive==3U && type==MT_CONTENT_IIGS_FLOPPY);
    }
    return mt_core_frontend_accepts_content(core_id,browser,type);
}
