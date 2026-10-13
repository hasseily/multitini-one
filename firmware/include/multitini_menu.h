/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_MENU_H
#define MULTITINI_MENU_H

#include "multitini_supervisor.h"
#include "multitini_ui.h"
#include "multitini_clock.h"
#include "multitini_gamepad.h"
#include "multitini_controls.h"
#include "multitini_text_reader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_MENU_MAX_CORES 16U
#define MT_MENU_FILE_NAME_MAX 96U
#define MT_MENU_MAX_CORE_OPTIONS 8U
#define MT_MENU_MAX_ITEMS 20U
#define MT_MENU_MAX_EFFECT_ITEMS 32U
/* Reserved cookie flags; ordinary file and D64 cookies retain their values. */
#define MT_MENU_FILE_DIRECTORY UINT32_C(0x80000000)
#define MT_MENU_FILE_PARENT UINT32_C(0x40000000)
#define MT_MENU_FILE_TEXT UINT32_C(0x20000000)
#define MT_MENU_TEXT_COLUMNS 95U
#define MT_MENU_TEXT_ROWS 40U
/* The content list leaves room for the shared header, help and footer. */
#define MT_MENU_LIST_ROWS (MT_UI_ROWS - 20U)

typedef enum {
    MT_MENU_PAGE_CORES = 0,
    MT_MENU_PAGE_MEDIA,
    MT_MENU_PAGE_VIDEO,
    MT_MENU_PAGE_EFFECTS,
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
    MT_MENU_ACTION_REBOOT_GOLDEN,
    MT_MENU_ACTION_OPEN_DIRECTORY,
    MT_MENU_ACTION_SET_BORDER,
    MT_MENU_ACTION_SET_VECTREX_VIDEO,
    MT_MENU_ACTION_BROWSE_OVERLAY,
    MT_MENU_ACTION_BROWSE_DOCUMENTS,
    MT_MENU_ACTION_OPEN_TEXT,
    MT_MENU_ACTION_EDIT_CLOCK,
    MT_MENU_ACTION_SET_CLOCK,
    MT_MENU_ACTION_RESTART_USB,
    MT_MENU_ACTION_SET_VIDEO_EFFECT,
    MT_MENU_ACTION_EDIT_GAMEPADS,
    MT_MENU_ACTION_SET_GAMEPADS,
    MT_MENU_ACTION_SET_DISPLAY_MODE,
    MT_MENU_ACTION_SET_VIDEO_MULTIPLIER,
    MT_MENU_ACTION_SET_BOOT_DEFAULT,
    MT_MENU_ACTION_SWITCH_FIRMWARE,
    MT_MENU_ACTION_UPDATE_GOLDEN
} mt_menu_action_kind_t;

typedef enum {
    MT_MENU_BROWSER_NONE = 0,
    MT_MENU_BROWSER_DRIVE8,
    MT_MENU_BROWSER_DISK_PROGRAM,
    MT_MENU_BROWSER_CARTRIDGE,
    MT_MENU_BROWSER_PROGRAM,
    MT_MENU_BROWSER_OVERLAY,
    MT_MENU_BROWSER_DOCUMENTS
} mt_menu_browser_t;

typedef struct {
    char name[MT_MENU_FILE_NAME_MAX];
    size_t size;
    uint32_t cookie;
} mt_menu_file_t;

typedef enum {
    MT_MENU_CONTROL_VALUE = 0,
    MT_MENU_CONTROL_CHECKBOX,
    MT_MENU_CONTROL_SLIDER
} mt_menu_control_t;

typedef struct {
    const char *label;
    const char *value;
    const char *help;
    mt_menu_action_kind_t action;
    size_t argument;
    bool enabled;
    mt_ui_color_t value_color;
    mt_menu_control_t control;
    uint32_t control_value;
    uint32_t control_min;
    uint32_t control_max;
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
    const mt_menu_item_t *system_items;
    size_t system_item_count;
    const mt_menu_item_t *effect_items;
    size_t effect_item_count;
    mt_text_reader_t *text_reader;
    const char *text_title;
    mt_gamepad_state_t gamepad_preview;
    uint32_t gamepad_core_id;
    uint64_t gamepad_now_ms;
    mt_controller_device_t gamepad_devices[MT_GAMEPAD_SLOTS];
    /* Current live routing: player/port 0 or 1; UINT8_MAX means unassigned. */
    uint8_t gamepad_player[MT_GAMEPAD_SLOTS];
    bool clock_available;
    bool clock_valid;
    mt_clock_time_t clock;
    bool save_dirty;
    bool save_faulted;
    bool display_supported;
    uint8_t display_mode;
    bool video_multiplier_supported;
    uint8_t video_multiplier;
    bool video_test_enabled;
    bool settings_auto_boot;
    bool settings_faulted;
    bool border_supported;
    bool border_enabled;
    uint8_t border_style;
    uint8_t border_color;
    uint8_t border_padding;
    bool vectrex_video_supported;
    bool vectrex_overlay_enabled;
    bool vectrex_overlay_loaded;
    bool vectrex_overlay_error;
    uint8_t vectrex_beam_gain;
    uint8_t vectrex_overlay_opacity;
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
    bool gamepad_edit;
    uint8_t gamepad_slot, gamepad_control, gamepad_field;
    bool gamepad_capture, gamepad_test, gamepad_sticks, gamepad_wizard;
    bool gamepad_capture_armed, gamepad_conflict;
    uint16_t gamepad_pending_source;
    uint64_t gamepad_capture_started;
    mt_gamepad_state_t gamepad_previous;
    mt_controller_config_t gamepad_config[MT_GAMEPAD_SLOTS];
    mt_controller_config_t gamepad_original[MT_GAMEPAD_SLOTS];
    char gamepad_notice[160];
    bool clock_edit;
    size_t clock_field;
    mt_clock_time_t clock;
    bool golden_update_confirm;
    bool golden_update_accept;
} mt_menu_t;

enum {
    MT_MENU_HID_ENTER = 0x28,
    MT_MENU_HID_ESCAPE = 0x29,
    MT_MENU_HID_BACKSPACE = 0x2A,
    MT_MENU_HID_TAB = 0x2B,
    MT_MENU_HID_HOME = 0x4A,
    MT_MENU_HID_DELETE = 0x4C,
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
/* The runtime supplies only the selected device's raw preview. Drafts remain
 * private to the menu until SET_GAMEPADS is acknowledged by saved(). */
void mt_menu_gamepad_begin(mt_menu_t *menu, const mt_menu_view_t *view,
                           const mt_controller_config_t *configs);
bool mt_menu_gamepad_update(mt_menu_t *menu, const mt_menu_view_t *view);
void mt_menu_gamepad_saved(mt_menu_t *menu);
bool mt_menu_gamepad_dirty(const mt_menu_t *menu);

#endif
