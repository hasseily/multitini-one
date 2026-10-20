/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_frontend.h"

#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_nes_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_regs.h"

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
        "Keyboard joystick", state->joystick_port == 1U ? "Joystick port 1" :
                                                 "Joystick port 2",
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
                "Mount a 140 KiB DOS-order DSK, or a 5.25-inch WOZ2.",
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

static const core_profile_t profiles[] = {
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

bool mt_core_frontend_browser_folders(uint32_t core_id, mt_menu_browser_t browser)
{
    return browser==MT_MENU_BROWSER_DOCUMENTS ||
        ((core_id==MT_VECTREX_CORE_ID || core_id==MT_IIGS_CORE_ID) &&
         mt_core_frontend_browser_supported(core_id,browser));
}

bool mt_core_frontend_accepts_drive_content(uint32_t core_id, mt_menu_browser_t browser,
                                           unsigned drive, mt_content_type_t type)
{
    if(core_id==MT_IIGS_CORE_ID && browser==MT_MENU_BROWSER_DRIVE8) {
        if(drive>=MT_IIGS_DRIVE_COUNT) return false;
        if(mt_iigs_drive_hdd_unit(drive)<MT_IIGS_HDD_COUNT) return type==MT_CONTENT_IIGS_HDD;
        return type==MT_CONTENT_IIGS_WOZ ||
            (drive==2U ? type==MT_CONTENT_IIGS_HDD:type==MT_CONTENT_IIGS_FLOPPY);
    }
    return mt_core_frontend_accepts_content(core_id,browser,type);
}
