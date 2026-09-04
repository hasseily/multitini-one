/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_MENU_H
#define MULTITINI_MENU_H

#include "multitini_supervisor.h"
#include "multitini_ui.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_MENU_MAX_CORES 16U
#define MT_MENU_FILE_NAME_MAX 96U

typedef enum {
    MT_MENU_PAGE_CORES = 0,
    MT_MENU_PAGE_MEDIA,
    MT_MENU_PAGE_VIDEO,
    MT_MENU_PAGE_INPUT,
    MT_MENU_PAGE_SYSTEM,
    MT_MENU_PAGE_COUNT
} mt_menu_page_t;

typedef enum {
    MT_MENU_ACTION_NONE = 0,
    MT_MENU_ACTION_SWITCH_CORE,
    MT_MENU_ACTION_BROWSE_DRIVE8,
    MT_MENU_ACTION_BROWSE_CARTRIDGE,
    MT_MENU_ACTION_BROWSE_PROGRAM,
    MT_MENU_ACTION_MOUNT_DRIVE8,
    MT_MENU_ACTION_EJECT_DRIVE8,
    MT_MENU_ACTION_LOAD_CONTENT,
    MT_MENU_ACTION_DETACH_CARTRIDGE,
    MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE,
    MT_MENU_ACTION_TOGGLE_KEYBOARD_MAP,
    MT_MENU_ACTION_SWAP_JOYSTICK_PORT,
    MT_MENU_ACTION_RESET_CORE,
    MT_MENU_ACTION_REBOOT_GOLDEN
} mt_menu_action_kind_t;

typedef enum {
    MT_MENU_BROWSER_NONE = 0,
    MT_MENU_BROWSER_DRIVE8,
    MT_MENU_BROWSER_CARTRIDGE,
    MT_MENU_BROWSER_PROGRAM
} mt_menu_browser_t;

typedef struct {
    char name[MT_MENU_FILE_NAME_MAX];
    size_t size;
} mt_menu_file_t;

typedef struct {
    mt_menu_action_kind_t kind;
    size_t core_index;
    size_t variant_index;
    size_t file_index;
} mt_menu_action_t;

typedef struct {
    const mt_core_selection_t *cores;
    size_t core_count;
    const char *active_core_id;
    const char *active_variant_id;
    const char *drive8_name;
    const char *content_name;
    const char *status_text;
    const mt_menu_file_t *files;
    size_t file_count;
    bool drive8_mounted;
    bool cartridge_attached;
    bool drive8_supported;
    bool cartridge_supported;
    bool program_supported;
    bool save_supported;
    bool save_active;
    bool save_dirty;
    bool save_faulted;
    bool c64_input_settings;
    bool video_test_enabled;
    bool symbolic_keyboard;
    uint8_t joystick_port;
} mt_menu_view_t;

typedef struct {
    bool visible;
    mt_menu_page_t page;
    size_t row;
    mt_menu_browser_t browser;
    size_t selected_variant[MT_MENU_MAX_CORES];
} mt_menu_t;

enum {
    MT_MENU_HID_ENTER = 0x28,
    MT_MENU_HID_ESCAPE = 0x29,
    MT_MENU_HID_TAB = 0x2B,
    MT_MENU_HID_F12 = 0x45,
    MT_MENU_HID_RIGHT = 0x4F,
    MT_MENU_HID_LEFT = 0x50,
    MT_MENU_HID_DOWN = 0x51,
    MT_MENU_HID_UP = 0x52
};

void mt_menu_init(mt_menu_t *menu, const mt_menu_view_t *view);
mt_menu_action_t mt_menu_key(mt_menu_t *menu, const mt_menu_view_t *view,
                             uint8_t hid_usage, bool pressed);
void mt_menu_render(const mt_menu_t *menu, const mt_menu_view_t *view,
                    mt_ui_frame_t *frame);
const char *mt_menu_page_name(mt_menu_page_t page);

#endif
