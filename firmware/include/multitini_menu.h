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
#define MT_MENU_MAX_CORE_OPTIONS 8U
#define MT_MENU_MAX_ITEMS 16U
/* The content list leaves room for the shared header, help and footer. */
#define MT_MENU_LIST_ROWS (MT_UI_ROWS - 20U)

typedef enum {
    MT_MENU_PAGE_CORES = 0,
    MT_MENU_PAGE_MEDIA,
    MT_MENU_PAGE_VIDEO,
    MT_MENU_PAGE_INPUT,
    MT_MENU_PAGE_CORE_CONFIG,
    MT_MENU_PAGE_SYSTEM,
    MT_MENU_PAGE_COUNT
} mt_menu_page_t;

typedef enum {
    MT_MENU_ACTION_NONE = 0,
    MT_MENU_ACTION_SWITCH_CORE,
    MT_MENU_ACTION_BROWSE_DRIVE8,
    MT_MENU_ACTION_BROWSE_DISK_PROGRAM,
    MT_MENU_ACTION_BROWSE_CARTRIDGE,
    MT_MENU_ACTION_BROWSE_PROGRAM,
    MT_MENU_ACTION_MOUNT_DRIVE8,
    MT_MENU_ACTION_MOUNT_AND_RUN,
    MT_MENU_ACTION_LOAD_DISK_PROGRAM,
    MT_MENU_ACTION_DIRECT_DISK_PROGRAM,
    MT_MENU_ACTION_EJECT_DRIVE8,
    MT_MENU_ACTION_LOAD_CONTENT,
    MT_MENU_ACTION_DETACH_CARTRIDGE,
    MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE,
    MT_MENU_ACTION_TOGGLE_KEYBOARD_MAP,
    MT_MENU_ACTION_SWAP_JOYSTICK_PORT,
    MT_MENU_ACTION_TOGGLE_CORE_OPTION,
    MT_MENU_ACTION_RESET_CORE,
    MT_MENU_ACTION_TOGGLE_AUTO_BOOT,
    MT_MENU_ACTION_REBOOT_GOLDEN
} mt_menu_action_kind_t;

typedef enum {
    MT_MENU_BROWSER_NONE = 0,
    MT_MENU_BROWSER_DRIVE8,
    MT_MENU_BROWSER_DISK_PROGRAM,
    MT_MENU_BROWSER_CARTRIDGE,
    MT_MENU_BROWSER_PROGRAM
} mt_menu_browser_t;

typedef struct {
    char name[MT_MENU_FILE_NAME_MAX];
    size_t size;
    uint32_t cookie;
} mt_menu_file_t;

typedef struct {
    const char *label;
    const char *value;
    const char *help;
    mt_menu_action_kind_t action;
    size_t argument;
    bool enabled;
    mt_ui_color_t value_color;
} mt_menu_item_t;

typedef mt_menu_item_t mt_menu_core_option_t;

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
    const char *status_text;
    const char *content_root;
    const char *core_description;
    const char *input_help;
    const mt_menu_file_t *files;
    size_t file_count;
    const mt_menu_core_option_t *core_options;
    size_t core_option_count;
    const mt_menu_item_t *media_items;
    size_t media_item_count;
    const mt_menu_item_t *input_items;
    size_t input_item_count;
    bool save_dirty;
    bool save_faulted;
    bool video_test_enabled;
    bool settings_auto_boot;
    bool settings_faulted;
    mt_ui_color_t status_color;
} mt_menu_view_t;

typedef struct {
    bool visible;
    mt_menu_page_t page;
    size_t row;
    mt_menu_browser_t browser;
    size_t selected_variant[MT_MENU_MAX_CORES];
    size_t page_row[MT_MENU_PAGE_COUNT];
    size_t first_row;
} mt_menu_t;

enum {
    MT_MENU_HID_ENTER = 0x28,
    MT_MENU_HID_ESCAPE = 0x29,
    MT_MENU_HID_TAB = 0x2B,
    MT_MENU_HID_HOME = 0x4A,
    MT_MENU_HID_PAGE_UP = 0x4B,
    MT_MENU_HID_END = 0x4D,
    MT_MENU_HID_PAGE_DOWN = 0x4E,
    MT_MENU_HID_F12 = 0x45,
    MT_MENU_HID_RIGHT = 0x4F,
    MT_MENU_HID_LEFT = 0x50,
    MT_MENU_HID_DOWN = 0x51,
    MT_MENU_HID_UP = 0x52
};

void mt_menu_init(mt_menu_t *menu, const mt_menu_view_t *view);
void mt_menu_open_browser(mt_menu_t *menu, mt_menu_browser_t browser);
void mt_menu_close_browser(mt_menu_t *menu);
mt_menu_action_t mt_menu_key(mt_menu_t *menu, const mt_menu_view_t *view,
                             uint8_t hid_usage, bool pressed);
mt_menu_action_t mt_menu_key_with_modifiers(mt_menu_t *menu,
                                            const mt_menu_view_t *view,
                                            uint8_t hid_usage, bool pressed,
                                            uint8_t modifiers);
void mt_menu_render(const mt_menu_t *menu, const mt_menu_view_t *view,
                    mt_ui_frame_t *frame);
const char *mt_menu_page_name(mt_menu_page_t page);

#endif
