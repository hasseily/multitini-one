/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_display.h"
#include "multitini_menu.h"

#include <stdio.h>
#include <string.h>

/* Keep the bottom panels anchored to the shared text-plane dimensions.
 * Every text field has bounds so names cannot overwrite adjacent values. */
enum {
    SIDE_X = 2, SIDE_W = 21, BODY_X = SIDE_X + SIDE_W + 2,
    BODY_W = MT_UI_COLUMNS - BODY_X - 2,
    ITEM_X = BODY_X + 2, ITEM_W = BODY_W - 4,
    LABEL_X = ITEM_X + 2, LABEL_W = 35, VALUE_X = LABEL_X + LABEL_W + 2,
    VALUE_W = ITEM_X + ITEM_W - VALUE_X - 1,
    CONTENT_W = ITEM_W - 3, SIZE_W = 12,
    SIZE_X = ITEM_X + ITEM_W - SIZE_W - 1,
    NAME_W = SIZE_X - LABEL_X - 1,
    HEADER_ACTIVE_X = MT_UI_COLUMNS / 2 + 6,
    HEADER_ACTIVE_W = MT_UI_COLUMNS - HEADER_ACTIVE_X - 4,
    LIST_Y = 11, HELP_Y = MT_UI_ROWS - 8,
    STATUS_Y = MT_UI_ROWS - 4, FOOTER_Y = MT_UI_ROWS - 2
};

_Static_assert(LIST_Y + MT_MENU_LIST_ROWS < HELP_Y,
               "Menu list must stop before the help panel");

static size_t core_count(const mt_menu_view_t *view)
{
    if (view == NULL || view->cores == NULL) return 0U;
    return view->core_count > MT_MENU_MAX_CORES ? MT_MENU_MAX_CORES :
                                                 view->core_count;
}

static size_t active_core(const mt_menu_view_t *view)
{
    size_t index;
    if (view != NULL && view->active_core_id != NULL) {
        for (index = 0U; index < core_count(view); ++index) {
            if (strcmp(view->cores[index].package.id, view->active_core_id) == 0)
                return index;
        }
    }
    return core_count(view);
}

static bool has_core(const mt_menu_view_t *view)
{
    return view != NULL && view->active_core_id != NULL &&
           view->active_core_id[0] != '\0';
}

static size_t variant_count(const mt_core_selection_t *core)
{
    return core->package.variant_count > MT_CORE_PACKAGE_MAX_VARIANTS ?
           MT_CORE_PACKAGE_MAX_VARIANTS : core->package.variant_count;
}

static const mt_menu_item_t *page_items(mt_menu_page_t page,
                                       const mt_menu_view_t *view, size_t *count)
{
    const mt_menu_item_t *items = NULL;
    *count = 0U;
    if (view == NULL) return NULL;
    switch (page) {
    case MT_MENU_PAGE_MEDIA:
        items = view->media_items;
        *count = view->media_item_count;
        break;
    case MT_MENU_PAGE_INPUT:
        items = view->input_items;
        *count = view->input_item_count;
        break;
    case MT_MENU_PAGE_CORE_CONFIG:
        items = view->core_options;
        *count = view->core_option_count;
        break;
    case MT_MENU_PAGE_SYSTEM:
        items = view->system_items;
        *count = view->system_item_count;
        break;
    case MT_MENU_PAGE_ABOUT:
        items = view->about_items;
        *count = view->about_item_count;
        break;
    case MT_MENU_PAGE_EFFECTS:
        items = view->effect_items;
        *count = view->effect_item_count;
        break;
    default:
        break;
    }
    if (items == NULL) *count = 0U;
    const size_t limit = page == MT_MENU_PAGE_EFFECTS ? MT_MENU_MAX_EFFECT_ITEMS :
        page == MT_MENU_PAGE_ABOUT ? MT_MENU_MAX_ABOUT_ITEMS :
        page == MT_MENU_PAGE_SYSTEM ? MT_MENU_MAX_SYSTEM_ITEMS : MT_MENU_MAX_ITEMS;
    if (*count > limit) *count = limit;
    return items;
}

static size_t video_setting_start(const mt_menu_view_t *view)
{
    return active_core(view) < core_count(view) ? 2U : 1U;
}

static bool vectrex_video_visible(const mt_menu_view_t *view)
{
    return has_core(view) && strcmp(view->active_core_id, "vectrex") == 0;
}

static size_t row_count(const mt_menu_t *menu, const mt_menu_view_t *view)
{
    size_t count = 0U;
    if (menu->browser != MT_MENU_BROWSER_NONE)
        return view == NULL || view->files == NULL ? 0U : view->file_count;
    switch (menu->page) {
    case MT_MENU_PAGE_CORES:
        return core_count(view);
    case MT_MENU_PAGE_VIDEO:
        return video_setting_start(view) + 6U + (vectrex_video_visible(view) ? 3U : 0U);
    case MT_MENU_PAGE_SYSTEM:
        if (view == NULL || view->system_items == NULL) return 3U;
        (void)page_items(menu->page, view, &count);
        return count;
    default:
        (void)page_items(menu->page, view, &count);
        return count;
    }
}

static size_t visible_rows(const mt_menu_t *menu)
{
    return menu->browser != MT_MENU_BROWSER_NONE ||
           menu->page == MT_MENU_PAGE_CORES ?
           MT_MENU_LIST_ROWS : MT_MENU_LIST_ROWS / 2U;
}

static size_t window_start(size_t first, size_t row, size_t count, size_t capacity)
{
    if (count <= capacity) return 0U;
    if (first > count - capacity) first = count - capacity;
    if (row < first) first = row;
    if (row >= first + capacity) first = row - capacity + 1U;
    return first;
}

static void normalize(mt_menu_t *menu, const mt_menu_view_t *view)
{
    size_t count;
    if ((unsigned)menu->page >= (unsigned)MT_MENU_PAGE_COUNT)
        menu->page = MT_MENU_PAGE_CORES;
    count = row_count(menu, view);
    if (count == 0U) menu->row = 0U;
    else if (menu->row >= count) menu->row = count - 1U;
    menu->first_row = window_start(menu->first_row, menu->row, count,
                                   visible_rows(menu));
}

void mt_menu_init(mt_menu_t *menu, const mt_menu_view_t *view)
{
    size_t index;
    if (menu == NULL) return;
    memset(menu, 0, sizeof(*menu));
    for (index = 0U; index < core_count(view); ++index) {
        const size_t count = variant_count(&view->cores[index]);
        menu->selected_variant[index] = view->cores[index].variant_index < count ?
                                       view->cores[index].variant_index : 0U;
    }
}

void mt_menu_open_browser(mt_menu_t *menu, mt_menu_browser_t browser)
{
    if (menu == NULL) return;
    if (menu->browser == MT_MENU_BROWSER_NONE &&
        (unsigned)menu->page < (unsigned)MT_MENU_PAGE_COUNT)
        menu->page_row[menu->page] = menu->row;
    menu->browser = browser;
    menu->row = 0U;
    menu->first_row = 0U;
}

void mt_menu_close_browser(mt_menu_t *menu)
{
    if (menu == NULL) return;
    menu->browser = MT_MENU_BROWSER_NONE;
    menu->row = (unsigned)menu->page < (unsigned)MT_MENU_PAGE_COUNT ?
                menu->page_row[menu->page] : 0U;
    menu->first_row = 0U;
}

static mt_menu_action_t no_action(void)
{
    const mt_menu_action_t action = {MT_MENU_ACTION_NONE, 0U, 0U, 0U};
    return action;
}

static bool directory_row(const mt_menu_view_t *view, size_t index)
{
    return view != NULL && view->files != NULL && index < view->file_count &&
        (view->files[index].cookie & (MT_MENU_FILE_DIRECTORY | MT_MENU_FILE_PARENT)) != 0U;
}

static size_t parent_row(const mt_menu_view_t *view)
{
    size_t index;
    if (view != NULL && view->files != NULL) {
        for (index = 0U; index < view->file_count; ++index)
            if ((view->files[index].cookie & MT_MENU_FILE_PARENT) != 0U) return index;
    }
    return SIZE_MAX;
}

static bool has_directories(const mt_menu_view_t *view)
{
    size_t index;
    if (view != NULL && view->files != NULL) {
        for (index = 0U; index < view->file_count; ++index)
            if (directory_row(view, index)) return true;
    }
    return false;
}

static void change_variant(mt_menu_t *menu, const mt_menu_view_t *view,
                            size_t core, bool backwards)
{
    size_t count;
    size_t selected;
    if (core >= core_count(view)) return;
    count = variant_count(&view->cores[core]);
    if (count == 0U) return;
    selected = menu->selected_variant[core] < count ?
               menu->selected_variant[core] : 0U;
    menu->selected_variant[core] = backwards ?
        (selected == 0U ? count - 1U : selected - 1U) : (selected + 1U) % count;
}

static mt_menu_action_t change_border(size_t row,
                                      const mt_menu_view_t *view, uint8_t key)
{
    mt_menu_action_t action = no_action();
    unsigned enabled;
    unsigned style;
    unsigned color;
    unsigned padding;
    unsigned previous;
    unsigned config;
    const bool backwards = key == MT_MENU_HID_LEFT;
    if (!has_core(view) || !view->border_supported) return action;
    enabled = view->border_enabled ? 1U : 0U;
    style = view->border_style < 5U ? view->border_style : 0U;
    color = view->border_color < 16U ? view->border_color : 7U;
    padding = view->border_padding >= 1U && view->border_padding <= 20U ?
              view->border_padding : 6U;
    previous = enabled | (style << 1U) | (color << 4U) | (padding << 8U);
    switch (row) {
    case 0U:
        enabled = key == MT_MENU_HID_ENTER ? 1U - enabled :
                  (backwards ? 0U : 1U);
        break;
    case 1U:
        style = (style + (backwards ? 4U : 1U)) % 5U;
        break;
    case 2U:
        color = (color + (backwards ? 15U : 1U)) % 16U;
        break;
    case 3U:
        if (backwards) padding = padding > 1U ? padding - 1U : 1U;
        else if (key == MT_MENU_HID_ENTER) padding = padding == 20U ? 1U : padding + 1U;
        else if (padding < 20U) ++padding;
        break;
    default:
        return action;
    }
    config = enabled | (style << 1U) | (color << 4U) | (padding << 8U);
    if (config != previous) {
        action.kind = MT_MENU_ACTION_SET_BORDER;
        action.file_index = config;
    }
    return action;
}

static mt_menu_action_t change_video(const mt_menu_t *menu,
                                     const mt_menu_view_t *view, uint8_t key)
{
    const size_t start = video_setting_start(view);
    mt_menu_action_t action = no_action();
    if(menu->row == start+5U+(vectrex_video_visible(view) ? 3U : 0U)) {
        if(has_core(view) && view->video_multiplier_supported) {
            const unsigned current=view->video_multiplier<=MT_DISPLAY_SCALE_MAX ? view->video_multiplier:0U;
            action.kind=MT_MENU_ACTION_SET_VIDEO_MULTIPLIER;
            action.file_index=(current+(key==MT_MENU_HID_LEFT ? 4U:1U))%5U;
        }
        return action;
    }
    if(menu->row == start+4U+(vectrex_video_visible(view) ? 3U : 0U)) {
        if(view != NULL && view->display_supported) {
            action.kind=MT_MENU_ACTION_SET_DISPLAY_MODE;
            action.file_index=(view->display_mode+(key==MT_MENU_HID_LEFT ? 5U : 1U))%MT_DISPLAY_MODE_COUNT;
        }
        return action;
    }
    if (menu->row >= start + 4U) {
        unsigned enabled;
        unsigned gain;
        unsigned opacity;
        unsigned previous;
        unsigned config;
        const bool backwards = key == MT_MENU_HID_LEFT;
        if (!vectrex_video_visible(view) || !view->vectrex_video_supported) return action;
        enabled = view->vectrex_overlay_enabled ? 1U : 0U;
        gain = view->vectrex_beam_gain < 4U ? view->vectrex_beam_gain : 1U;
        opacity = view->vectrex_overlay_opacity < 4U ? view->vectrex_overlay_opacity : 1U;
        previous = enabled | (gain << 1U) | (opacity << 3U);
        switch (menu->row - start - 4U) {
        case 0U:
            enabled = key == MT_MENU_HID_ENTER ? 1U - enabled : (backwards ? 0U : 1U);
            break;
        case 1U:
            opacity = (opacity + (backwards ? 3U : 1U)) % 4U;
            break;
        case 2U:
            gain = (gain + (backwards ? 3U : 1U)) % 4U;
            break;
        default:
            return action;
        }
        config = enabled | (gain << 1U) | (opacity << 3U);
        if (config != previous) {
            action.kind = MT_MENU_ACTION_SET_VECTREX_VIDEO;
            action.file_index = config;
        }
        return action;
    }
    if (!has_core(view) || !view->border_supported) return action;
    if (menu->row >= start) action = change_border(menu->row - start, view, key);
    return action;
}

static mt_menu_action_t activate(const mt_menu_t *menu, const mt_menu_view_t *view)
{
    mt_menu_action_t action = no_action();
    if (menu->browser != MT_MENU_BROWSER_NONE) {
        if (view == NULL || view->files == NULL || menu->row >= view->file_count)
            return action;
        action.kind = directory_row(view, menu->row) ? MT_MENU_ACTION_OPEN_DIRECTORY :
            (view->files[menu->row].cookie & MT_MENU_FILE_TEXT) != 0U ? MT_MENU_ACTION_OPEN_TEXT :
            menu->browser == MT_MENU_BROWSER_DRIVE8 ?
            MT_MENU_ACTION_MOUNT_DRIVE8 :
            (menu->browser == MT_MENU_BROWSER_DISK_PROGRAM ?
             MT_MENU_ACTION_MOUNT_AND_RUN : MT_MENU_ACTION_LOAD_CONTENT);
        action.file_index = menu->row;
    } else if (menu->page == MT_MENU_PAGE_CORES ||
               (menu->page == MT_MENU_PAGE_VIDEO && menu->row == 1U &&
                active_core(view) < core_count(view))) {
        const size_t core = menu->page == MT_MENU_PAGE_CORES ? menu->row :
                                                               active_core(view);
        if (core < core_count(view) &&
            menu->selected_variant[core] < variant_count(&view->cores[core])) {
            action.kind = MT_MENU_ACTION_SWITCH_CORE;
            action.core_index = core;
            action.variant_index = menu->selected_variant[core];
        }
    } else if (menu->page == MT_MENU_PAGE_VIDEO) {
        if (menu->row == 0U) {
            if (has_core(view)) action.kind = MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE;
        } else action = change_video(menu, view, MT_MENU_HID_ENTER);
    } else if (menu->page == MT_MENU_PAGE_SYSTEM &&
               (view == NULL || view->system_items == NULL)) {
        if (menu->row == 2U) action.kind = MT_MENU_ACTION_REBOOT_GOLDEN;
        else if (has_core(view)) action.kind = menu->row == 0U ?
            MT_MENU_ACTION_RESET_CORE : MT_MENU_ACTION_TOGGLE_AUTO_BOOT;
    } else {
        size_t count;
        const mt_menu_item_t *items = page_items(menu->page, view, &count);
        if (menu->row < count && items[menu->row].enabled) {
            action.kind = items[menu->row].action;
            action.file_index = items[menu->row].argument;
        }
    }
    return action;
}

static unsigned gamepad_action_count(const mt_menu_view_t *view)
{
    unsigned count = 0U;
    const uint32_t mask = mt_controls_action_mask(view->gamepad_core_id);
    for (unsigned index = 0U; index < MT_CONTROL_COUNT; ++index)
        if ((mask & (1U << index)) != 0U) ++count;
    return count;
}

static unsigned gamepad_action_at(const mt_menu_view_t *view, unsigned row)
{
    const uint32_t mask = mt_controls_action_mask(view->gamepad_core_id);
    for (unsigned index = 0U; index < MT_CONTROL_COUNT; ++index) {
        if ((mask & (1U << index)) == 0U) continue;
        if (row-- == 0U) return index;
    }
    return MT_CONTROL_MENU;
}

bool mt_menu_gamepad_dirty(const mt_menu_t *menu)
{
    return menu != NULL && memcmp(menu->gamepad_config, menu->gamepad_original,
                                  sizeof(menu->gamepad_config)) != 0;
}

void mt_menu_gamepad_saved(mt_menu_t *menu)
{
    if (menu == NULL) return;
    memcpy(menu->gamepad_original, menu->gamepad_config, sizeof(menu->gamepad_original));
    (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Controller settings saved.");
}

void mt_menu_gamepad_begin(mt_menu_t *menu, const mt_menu_view_t *view,
                           const mt_controller_config_t *configs)
{
    if (menu == NULL || view == NULL || configs == NULL || !has_core(view) ||
        mt_controls_action_mask(view->gamepad_core_id) == 0U) return;
    memcpy(menu->gamepad_config, configs, sizeof(menu->gamepad_config));
    memcpy(menu->gamepad_original, configs, sizeof(menu->gamepad_original));
    menu->gamepad_edit = true;
    menu->gamepad_capture = menu->gamepad_test = menu->gamepad_sticks = false;
    menu->gamepad_wizard = menu->gamepad_conflict = false;
    menu->gamepad_field = menu->gamepad_control = 0U;
    if (menu->gamepad_slot >= MT_GAMEPAD_SLOTS ||
        !view->gamepad_devices[menu->gamepad_slot].connected) {
        menu->gamepad_slot = 0U;
        for (unsigned index = 0U; index < MT_GAMEPAD_SLOTS; ++index)
            if (view->gamepad_devices[index].connected) { menu->gamepad_slot = (uint8_t)index; break; }
    }
    (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice),
                   "Choose a game action, press Enter, then press the button or direction you want.");
}

static void gamepad_start_capture(mt_menu_t *menu, const mt_menu_view_t *view,
                                   unsigned control)
{
    if (!view->gamepad_devices[menu->gamepad_slot].connected) {
        (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Reconnect this controller before assigning a control.");
        return;
    }
    menu->gamepad_control = (uint8_t)control;
    menu->gamepad_capture = true;
    menu->gamepad_conflict = false;
    menu->gamepad_pending_source = 0U;
    menu->gamepad_capture_armed = false;
    menu->gamepad_previous = view->gamepad_preview;
    menu->gamepad_capture_started = view->gamepad_now_ms;
    (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice),
                   "Release the buttons and center the sticks before assigning an input.");
}

static void gamepad_next_capture(mt_menu_t *menu, const mt_menu_view_t *view)
{
    if (menu->gamepad_wizard) {
        const uint32_t mask = mt_controls_action_mask(view->gamepad_core_id);
        for (unsigned index = (unsigned)menu->gamepad_control + 1U; index <= MT_CONTROL_MENU; ++index)
            if ((mask & (1U << index)) != 0U) { gamepad_start_capture(menu, view, index); return; }
    }
    menu->gamepad_capture = menu->gamepad_wizard = menu->gamepad_conflict = false;
    (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Assignment updated. Choose Save changes to keep your edits.");
}

static bool gamepad_sources_overlap(const mt_controller_config_t *config, uint16_t left, uint16_t right)
{
    if (left == 0U || right == 0U) return false;
    if (left == right) return true;
    if (left >= MT_CONTROL_SOURCE_DPAD_STICK && left < MT_CONTROL_SOURCE_COUNT) {
        const unsigned direction = (unsigned)left - MT_CONTROL_SOURCE_DPAD_STICK;
        const unsigned axis = config->axis[direction < 2U ? MT_GAMEPAD_AXIS_Y : MT_GAMEPAD_AXIS_X].source;
        return right == MT_CONTROL_SOURCE_HAT + direction ||
               (axis < MT_GAMEPAD_AXIS_COUNT && right == MT_CONTROL_SOURCE_AXIS_NEG + axis * 2U + (direction & 1U));
    }
    if (right >= MT_CONTROL_SOURCE_DPAD_STICK && right < MT_CONTROL_SOURCE_COUNT)
        return gamepad_sources_overlap(config, right, left);
    return false;
}

static void gamepad_assign(mt_menu_t *menu, const mt_menu_view_t *view, uint16_t source)
{
    mt_controller_config_t *config = &menu->gamepad_config[menu->gamepad_slot];
    if (source != 0U) {
        const uint32_t mask = mt_controls_action_mask(view->gamepad_core_id);
        for (unsigned index = 0U; index < MT_CONTROL_COUNT; ++index)
            if ((mask & (1U << index)) != 0U && index != menu->gamepad_control && gamepad_sources_overlap(config, config->binding[index], source))
                config->binding[index] = 0U;
    }
    config->binding[menu->gamepad_control] = source;
    gamepad_next_capture(menu, view);
}

bool mt_menu_gamepad_update(mt_menu_t *menu, const mt_menu_view_t *view)
{
    if (menu == NULL || view == NULL || !menu->gamepad_edit || menu->gamepad_slot >= MT_GAMEPAD_SLOTS)
        return false;
    if (!menu->gamepad_capture) return menu->gamepad_test || menu->gamepad_sticks;
    if (!view->gamepad_devices[menu->gamepad_slot].connected) {
        menu->gamepad_capture = menu->gamepad_wizard = menu->gamepad_conflict = false;
        (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Controller disconnected. Assignments are preserved; reconnect to continue.");
        return true;
    }
    if (view->gamepad_now_ms - menu->gamepad_capture_started >= UINT64_C(20000)) {
        menu->gamepad_capture = menu->gamepad_wizard = menu->gamepad_conflict = false;
        (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "No control assigned. Capture timed out; press Enter to try again.");
        return true;
    }
    if (menu->gamepad_conflict) return false;
    if (!menu->gamepad_capture_armed) {
        menu->gamepad_previous = view->gamepad_preview;
        if (!mt_controls_neutral(&view->gamepad_preview)) return false;
        menu->gamepad_capture_armed = true;
        (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "%s",
            menu->gamepad_control == MT_CONTROL_AXIS_X || menu->gamepad_control == MT_CONTROL_AXIS_Y ?
            "Ready. Move the stick axis you want to use." :
            "Ready. Press a button or D-pad direction, or move a stick or trigger.");
        return true;
    }
    uint16_t source = mt_controls_capture_source(&menu->gamepad_previous,
        &view->gamepad_preview, menu->gamepad_control == MT_CONTROL_AXIS_X || menu->gamepad_control == MT_CONTROL_AXIS_Y);
    menu->gamepad_previous = view->gamepad_preview;
    if (source == 0U) return false;
    const mt_controller_config_t *config = &menu->gamepad_config[menu->gamepad_slot];
    if (source >= MT_CONTROL_SOURCE_AXIS_NEG && source < MT_CONTROL_SOURCE_AXIS) {
        const unsigned direction = (unsigned)source - MT_CONTROL_SOURCE_AXIS_NEG;
        if (config->axis[direction / 2U].invert != 0U)
            source = (uint16_t)(MT_CONTROL_SOURCE_AXIS_NEG + (direction ^ 1U));
    }
    const uint32_t mask = mt_controls_action_mask(view->gamepad_core_id);
    for (unsigned index = 0U; index < MT_CONTROL_COUNT; ++index) {
        if ((mask & (1U << index)) == 0U || index == menu->gamepad_control || !gamepad_sources_overlap(config, config->binding[index], source)) continue;
        menu->gamepad_pending_source = source;
        menu->gamepad_conflict = true;
        (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice),
            "Already assigned to %s. Enter moves it here and clears that assignment; Esc cancels.",
            mt_controls_action_name(view->gamepad_core_id, index));
        return true;
    }
    gamepad_assign(menu, view, source);
    return true;
}

static void gamepad_choose_controller(mt_menu_t *menu, const mt_menu_view_t *view, int step)
{
    for (unsigned offset = 1U; offset <= MT_GAMEPAD_SLOTS; ++offset) {
        const unsigned candidate = (unsigned)((int)menu->gamepad_slot + step * (int)offset + (int)MT_GAMEPAD_SLOTS) % MT_GAMEPAD_SLOTS;
        if (view->gamepad_devices[candidate].connected) {
            menu->gamepad_slot = (uint8_t)candidate;
            menu->gamepad_notice[0] = '\0';
            return;
        }
    }
}

static mt_menu_action_t gamepad_key(mt_menu_t *menu, const mt_menu_view_t *view, uint8_t key)
{
    mt_menu_action_t action = no_action();
    if (view == NULL || menu->gamepad_slot >= MT_GAMEPAD_SLOTS) return action;
    mt_controller_config_t *config = &menu->gamepad_config[menu->gamepad_slot];
    const int step = key == MT_MENU_HID_LEFT ? -1 : 1;
    const unsigned actions = gamepad_action_count(view), commands = 2U + actions;
    const unsigned rows = menu->gamepad_sticks ? 6U : commands + 7U;
    if (menu->gamepad_capture) {
        if (key == MT_MENU_HID_ESCAPE || key == MT_MENU_HID_BACKSPACE) {
            menu->gamepad_capture = menu->gamepad_wizard = menu->gamepad_conflict = false;
            (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Capture cancelled. Earlier edits remain unsaved.");
        } else if (key == MT_MENU_HID_DELETE && !menu->gamepad_conflict) gamepad_assign(menu, view, 0U);
        else if (key == MT_MENU_HID_ENTER && menu->gamepad_conflict) gamepad_assign(menu, view, menu->gamepad_pending_source);
        else if (key == MT_MENU_HID_TAB && menu->gamepad_wizard && !menu->gamepad_conflict) gamepad_next_capture(menu, view);
        return action;
    }
    if (menu->gamepad_test) {
        if (key == MT_MENU_HID_ESCAPE || key == MT_MENU_HID_BACKSPACE || key == MT_MENU_HID_ENTER) menu->gamepad_test = false;
        return action;
    }
    if (key == MT_MENU_HID_ESCAPE || key == MT_MENU_HID_BACKSPACE) {
        if (menu->gamepad_sticks) { menu->gamepad_sticks = false; menu->gamepad_field = (uint8_t)(commands + 2U); }
        else if (mt_menu_gamepad_dirty(menu)) {
            menu->gamepad_field = (uint8_t)(commands + 4U);
            (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Unsaved changes. Choose Save changes or Discard changes before leaving.");
        } else menu->gamepad_edit = false;
        return action;
    }
    if (key == MT_MENU_HID_UP) menu->gamepad_field = (uint8_t)(((unsigned)menu->gamepad_field + rows - 1U) % rows);
    else if (key == MT_MENU_HID_DOWN || key == MT_MENU_HID_TAB) menu->gamepad_field = (uint8_t)(((unsigned)menu->gamepad_field + 1U) % rows);
    else if (key == MT_MENU_HID_HOME) menu->gamepad_field = 0U;
    else if (key == MT_MENU_HID_END) menu->gamepad_field = (uint8_t)(rows - 1U);
    else if (menu->gamepad_sticks) {
        mt_gamepad_axis_config_t *axis = &config->axis[menu->gamepad_control];
        if (key == MT_MENU_HID_ENTER && menu->gamepad_field == 5U) { menu->gamepad_sticks = false; menu->gamepad_field = (uint8_t)(commands + 2U); }
        else if (key == MT_MENU_HID_LEFT || key == MT_MENU_HID_RIGHT || key == MT_MENU_HID_ENTER) {
            switch (menu->gamepad_field) {
            case 0U: menu->gamepad_control = (uint8_t)(((int)menu->gamepad_control + step + (int)MT_GAMEPAD_AXIS_COUNT) % (int)MT_GAMEPAD_AXIS_COUNT); break;
            case 1U: axis->invert ^= 1U; break;
            case 2U: { const int value = (int)axis->deadzone_percent + step; if (value >= 0 && value <= 50) axis->deadzone_percent = (uint8_t)value; break; }
            case 3U: { const int value = (int)axis->sensitivity_percent + step * 5; if (value >= 25 && value <= 200) axis->sensitivity_percent = (uint16_t)value; break; }
            case 4U: axis->invert = 0U; axis->deadzone_percent = 10U; axis->sensitivity_percent = 100U; break;
            default: break;
            }
        }
    } else if (menu->gamepad_field == 0U && (key == MT_MENU_HID_LEFT || key == MT_MENU_HID_RIGHT || key == MT_MENU_HID_ENTER)) gamepad_choose_controller(menu, view, step);
    else if (menu->gamepad_field == 1U && (key == MT_MENU_HID_LEFT || key == MT_MENU_HID_RIGHT || key == MT_MENU_HID_ENTER)) {
        if (!view->gamepad_devices[menu->gamepad_slot].connected) return action;
        unsigned next = ((unsigned)config->assignment + (step < 0 ? 3U : 1U)) % 4U;
        /* A handheld has one controller input. No inactive second-player choice. */
        if (next == 2U && view->active_core_id != NULL && strcmp(view->active_core_id, "gbc") == 0) next = step < 0 ? 1U : 3U;
        for (unsigned index = 0U; index < MT_GAMEPAD_SLOTS; ++index) {
            if (index != menu->gamepad_slot && view->gamepad_devices[index].connected &&
                (next == 1U || next == 2U) && menu->gamepad_config[index].assignment == next) {
                (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "This player or port is already assigned. Set the other controller to Not used first.");
                return action;
            }
        }
        config->assignment = (uint8_t)next;
    } else if (menu->gamepad_field >= 2U && menu->gamepad_field < commands) {
        const unsigned control = gamepad_action_at(view, (unsigned)menu->gamepad_field - 2U);
        if (key == MT_MENU_HID_ENTER) { menu->gamepad_wizard = false; gamepad_start_capture(menu, view, control); }
        else if (key == MT_MENU_HID_DELETE) config->binding[control] = 0U;
    } else if (key == MT_MENU_HID_ENTER) {
        const unsigned command = (unsigned)menu->gamepad_field - commands;
        if (command < 4U && !view->gamepad_devices[menu->gamepad_slot].connected) return action;
        switch (command) {
        case 0U: menu->gamepad_wizard = true; gamepad_start_capture(menu, view, gamepad_action_at(view, 0U)); break;
        case 1U: menu->gamepad_test = true; break;
        case 2U: menu->gamepad_sticks = true; menu->gamepad_control = menu->gamepad_field = 0U; break;
        case 3U: { const uint8_t assignment = config->assignment; mt_controls_default(view->gamepad_core_id, &view->gamepad_devices[menu->gamepad_slot], config); config->assignment = assignment; break; }
        case 4U: if (mt_menu_gamepad_dirty(menu)) action.kind = MT_MENU_ACTION_SET_GAMEPADS; break;
        case 5U: memcpy(menu->gamepad_config, menu->gamepad_original, sizeof(menu->gamepad_config)); (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Unsaved changes discarded."); break;
        case 6U: if (!mt_menu_gamepad_dirty(menu)) menu->gamepad_edit = false; else (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Choose Save changes or Discard changes before leaving."); break;
        default: break;
        }
    }
    return action;
}

mt_menu_action_t mt_menu_key_with_modifiers(mt_menu_t *menu,
                                           const mt_menu_view_t *view,
                                           uint8_t hid_usage, bool pressed,
                                           uint8_t modifiers)
{
    mt_menu_action_t action = no_action();
    size_t count;
    if (menu == NULL || !pressed) return action;
    normalize(menu, view);
    if (menu->golden_update_confirm) {
        if (hid_usage == MT_MENU_HID_ESCAPE || hid_usage == MT_MENU_HID_BACKSPACE ||
            hid_usage == MT_MENU_HID_F12) {
            menu->golden_update_confirm = false;
            menu->golden_update_accept = false;
        } else if (hid_usage == MT_MENU_HID_LEFT || hid_usage == MT_MENU_HID_RIGHT ||
                   hid_usage == MT_MENU_HID_UP || hid_usage == MT_MENU_HID_DOWN ||
                   hid_usage == MT_MENU_HID_TAB) {
            menu->golden_update_accept = !menu->golden_update_accept;
        } else if (hid_usage == MT_MENU_HID_ENTER) {
            if (menu->golden_update_accept) {
                /* Recheck the descriptor: the SD file may disappear while
                 * the confirmation is open. Runtime validates it again. */
                action = activate(menu, view);
                if (action.kind != MT_MENU_ACTION_UPDATE_GOLDEN) action = no_action();
            }
            menu->golden_update_confirm = false;
            menu->golden_update_accept = false;
        }
        return action;
    }
    if (hid_usage == MT_MENU_HID_F12) {
        if (menu->gamepad_edit && (menu->gamepad_capture || menu->gamepad_test || menu->gamepad_sticks)) {
            menu->gamepad_capture = menu->gamepad_test = menu->gamepad_sticks = false;
            menu->gamepad_wizard = menu->gamepad_conflict = false;
            menu->gamepad_field = 0U;
            (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Returned to controller settings. Save or discard any edits before leaving.");
            return action;
        }
        if (menu->gamepad_edit && mt_menu_gamepad_dirty(menu)) {
            (void)snprintf(menu->gamepad_notice, sizeof(menu->gamepad_notice), "Unsaved changes. Choose Save changes or Discard changes before leaving.");
            return action;
        }
        if (view != NULL && view->text_reader != NULL)
            mt_text_reader_close(view->text_reader);
        menu->clock_edit = false;
        menu->gamepad_edit = false;
        menu->visible = !menu->visible;
        return action;
    }
    if (!menu->visible) return action;
    if (menu->gamepad_edit) return gamepad_key(menu, view, hid_usage);
    if (view != NULL && view->text_reader != NULL && view->text_reader->active) {
        if (hid_usage == MT_MENU_HID_ESCAPE || hid_usage == MT_MENU_HID_BACKSPACE) {
            mt_text_reader_close(view->text_reader);
        } else {
            switch (hid_usage) {
            case MT_MENU_HID_UP: (void)mt_text_reader_navigate(view->text_reader, MT_TEXT_READER_UP); break;
            case MT_MENU_HID_DOWN: (void)mt_text_reader_navigate(view->text_reader, MT_TEXT_READER_DOWN); break;
            case MT_MENU_HID_LEFT:
            case MT_MENU_HID_PAGE_UP: (void)mt_text_reader_navigate(view->text_reader, MT_TEXT_READER_PAGE_UP); break;
            case MT_MENU_HID_RIGHT:
            case MT_MENU_HID_PAGE_DOWN: (void)mt_text_reader_navigate(view->text_reader, MT_TEXT_READER_PAGE_DOWN); break;
            case MT_MENU_HID_HOME: (void)mt_text_reader_navigate(view->text_reader, MT_TEXT_READER_HOME); break;
            case MT_MENU_HID_END: (void)mt_text_reader_navigate(view->text_reader, MT_TEXT_READER_END); break;
            default: break;
            }
        }
        return action;
    }
    if (menu->clock_edit) {
        if (hid_usage == MT_MENU_HID_ESCAPE || hid_usage == MT_MENU_HID_BACKSPACE)
            menu->clock_edit = false;
        else if (hid_usage == MT_MENU_HID_UP)
            menu->clock_field = menu->clock_field == 0U ? 6U : menu->clock_field - 1U;
        else if (hid_usage == MT_MENU_HID_DOWN || hid_usage == MT_MENU_HID_TAB)
            menu->clock_field = (menu->clock_field + 1U) % 7U;
        else if (hid_usage == MT_MENU_HID_LEFT || hid_usage == MT_MENU_HID_RIGHT)
            mt_clock_adjust(&menu->clock, menu->clock_field,
                            hid_usage == MT_MENU_HID_LEFT ? -1 : 1);
        else if (hid_usage == MT_MENU_HID_ENTER) {
            if (menu->clock_field == 6U) action.kind = MT_MENU_ACTION_SET_CLOCK;
            else menu->clock_field++;
        }
        return action;
    }
    if (hid_usage == MT_MENU_HID_ESCAPE) {
        if (menu->browser != MT_MENU_BROWSER_NONE) mt_menu_close_browser(menu);
        else menu->visible = false;
        normalize(menu, view);
        return action;
    }
    if (hid_usage == MT_MENU_HID_TAB && menu->browser == MT_MENU_BROWSER_NONE) {
        menu->page_row[menu->page] = menu->row;
        const unsigned step = (modifiers & UINT8_C(0x22)) != 0U ?
            (unsigned)MT_MENU_PAGE_COUNT - 1U : 1U;
        menu->page = (mt_menu_page_t)(((unsigned)menu->page + step) %
                                     (unsigned)MT_MENU_PAGE_COUNT);
        menu->row = menu->page_row[menu->page];
        menu->first_row = 0U;
        normalize(menu, view);
        return action;
    }
    count = row_count(menu, view);
    if (count == 0U) return action;
    switch (hid_usage) {
    case MT_MENU_HID_UP:
        menu->row = menu->row == 0U ? count - 1U : menu->row - 1U;
        break;
    case MT_MENU_HID_DOWN:
        menu->row = (menu->row + 1U) % count;
        break;
    case MT_MENU_HID_HOME:
        menu->row = 0U;
        break;
    case MT_MENU_HID_END:
        menu->row = count - 1U;
        break;
    case MT_MENU_HID_PAGE_UP:
        menu->row = menu->row < visible_rows(menu) ? 0U :
                                                    menu->row - visible_rows(menu);
        break;
    case MT_MENU_HID_PAGE_DOWN:
        menu->row += count - 1U - menu->row < visible_rows(menu) ?
                     count - 1U - menu->row : visible_rows(menu);
        break;
    case MT_MENU_HID_LEFT:
    case MT_MENU_HID_BACKSPACE:
        if (menu->browser != MT_MENU_BROWSER_NONE) {
            const size_t parent = parent_row(view);
            if (parent != SIZE_MAX) {
                menu->row = parent;
                action.kind = MT_MENU_ACTION_OPEN_DIRECTORY;
                action.file_index = parent;
            } else if (hid_usage == MT_MENU_HID_LEFT &&
                       menu->browser == MT_MENU_BROWSER_DISK_PROGRAM &&
                       !directory_row(view, menu->row)) {
                action.kind = MT_MENU_ACTION_DIRECT_DISK_PROGRAM;
                action.file_index = menu->row;
            }
        } else if (hid_usage == MT_MENU_HID_LEFT) {
            if (menu->page == MT_MENU_PAGE_CORES)
                change_variant(menu, view, menu->row, true);
            else if (menu->page == MT_MENU_PAGE_VIDEO && menu->row == 1U &&
                     active_core(view) < core_count(view))
                change_variant(menu, view, active_core(view), true);
            else if (menu->page == MT_MENU_PAGE_VIDEO)
                action = change_video(menu, view, hid_usage);
            else if (menu->page == MT_MENU_PAGE_EFFECTS) {
                action = activate(menu, view);
                action.variant_index = 1U; /* decrease */
            }
        }
        break;
    case MT_MENU_HID_RIGHT:
        if (menu->browser != MT_MENU_BROWSER_NONE && directory_row(view, menu->row)) {
            action.kind = MT_MENU_ACTION_OPEN_DIRECTORY;
            action.file_index = menu->row;
        } else if (menu->browser == MT_MENU_BROWSER_DISK_PROGRAM) {
            action.kind = MT_MENU_ACTION_LOAD_DISK_PROGRAM;
            action.file_index = menu->row;
        } else if (menu->browser == MT_MENU_BROWSER_NONE) {
            if (menu->page == MT_MENU_PAGE_CORES)
                change_variant(menu, view, menu->row, false);
            else if (menu->page == MT_MENU_PAGE_VIDEO && menu->row == 1U &&
                     active_core(view) < core_count(view))
                change_variant(menu, view, active_core(view), false);
            else if (menu->page == MT_MENU_PAGE_VIDEO)
                action = change_video(menu, view, hid_usage);
            else if (menu->page == MT_MENU_PAGE_EFFECTS) {
                action = activate(menu, view);
                action.variant_index = 2U; /* increase */
            }
        }
        break;
    case MT_MENU_HID_ENTER:
        action = activate(menu, view);
        if (action.kind == MT_MENU_ACTION_UPDATE_GOLDEN) {
            menu->golden_update_confirm = true;
            menu->golden_update_accept = false;
            action = no_action();
        } else if (action.kind == MT_MENU_ACTION_EDIT_CLOCK && view != NULL && view->clock_available) {
            const mt_clock_time_t initial = {2026U, 1U, 1U, 0U, 0U, 0U};
            menu->clock = view->clock_valid && view->clock.year >= 2000U ? view->clock : initial;
            menu->clock_field = 0U;
            menu->clock_edit = true;
            action = no_action();
        }
        break;
    default:
        break;
    }
    normalize(menu, view);
    if (menu->browser == MT_MENU_BROWSER_NONE)
        menu->page_row[menu->page] = menu->row;
    return action;
}

mt_menu_action_t mt_menu_key(mt_menu_t *menu, const mt_menu_view_t *view,
                             uint8_t hid_usage, bool pressed)
{
    return mt_menu_key_with_modifiers(menu, view, hid_usage, pressed, 0U);
}

const char *mt_menu_page_name(mt_menu_page_t page)
{
    static const char *const names[] = {
        "Cores", "Media", "Video", "Effects", "Controls", "Options", "System", "About"
    };
    return (unsigned)page < (unsigned)MT_MENU_PAGE_COUNT ? names[page] : "Cores";
}

static const char *page_caption(mt_menu_page_t page)
{
    static const char *const captions[] = {
        "Choose a system. Your media stays with each core.",
        "Load content and manage the active session.",
        "Display and border. Saved separately for each core.",
        "Color and display effects. Saved separately for each core.",
        "Controls for the active system.",
        "Settings supplied by the active core.",
        "Board status, clock, documents and recovery.",
        "Firmware, FPGA and live board status."
    };
    return (unsigned)page < (unsigned)MT_MENU_PAGE_COUNT ? captions[page] : "";
}

static const char *browser_title(mt_menu_browser_t browser)
{
    switch (browser) {
    case MT_MENU_BROWSER_DRIVE8:
        return "Load disk";
    case MT_MENU_BROWSER_DISK_PROGRAM:
        return "Mount & Run";
    case MT_MENU_BROWSER_CARTRIDGE:
        return "Load cartridge";
    case MT_MENU_BROWSER_OVERLAY:
        return "Load overlay";
    case MT_MENU_BROWSER_DOCUMENTS:
        return "Read documents";
    default:
        return "Load program";
    }
}

static void field(mt_ui_frame_t *frame, size_t x, size_t y, size_t width,
                   const char *text, mt_ui_color_t fg, mt_ui_color_t bg)
{
    mt_ui_text_field(frame, x, y, width, text, fg, bg, false, false);
}

static void help_text(mt_ui_frame_t *frame, const char *text)
{
    size_t row;
    if (text == NULL) return;
    for (row = HELP_Y + 1U; row <= HELP_Y + 2U && *text != '\0'; ++row) {
        char line[CONTENT_W + 1];
        size_t length = strlen(text);
        size_t take = length > CONTENT_W ? CONTENT_W : length;
        if (length > take) {
            size_t space = take;
            while (space > 0U && text[space] != ' ') --space;
            if (space > 20U) take = space;
        }
        memcpy(line, text, take);
        line[take] = '\0';
        field(frame, LABEL_X, row, CONTENT_W, line,
              MT_UI_COLOR_MUTED, MT_UI_COLOR_RAISED);
        text += take;
        while (*text == ' ') ++text;
    }
}

static mt_menu_item_t builtin_item(const mt_menu_t *menu,
                                   const mt_menu_view_t *view, size_t index)
{
    mt_menu_item_t item = {0};
    item.enabled = has_core(view);
    item.value_color = MT_UI_COLOR_MUTED;
    if(menu->page==MT_MENU_PAGE_VIDEO &&
       index==video_setting_start(view)+5U+(vectrex_video_visible(view) ? 3U : 0U)) {
        item.label="Video size";
        item.value=mt_display_scale_name(view != NULL ? view->video_multiplier:0U);
        item.enabled=has_core(view) && view->video_multiplier_supported;
        item.help=item.enabled ?
            "Left/Right selects 1x, 2x, 3x, 4x or Auto. Auto keeps the current fit. Larger images clip at screen edges. Saved for this core." :
            "Load an updated core package to enable video size changes.";
        return item;
    }
    if(menu->page==MT_MENU_PAGE_VIDEO &&
       index==video_setting_start(view)+4U+(vectrex_video_visible(view) ? 3U : 0U)) {
        item.label="Display resolution";
        item.value=mt_display_mode_name(view != NULL ? view->display_mode : 0U);
        item.enabled=view != NULL && view->display_supported;
        item.help=item.enabled ?
            "Left/Right changes resolution at about 60 Hz and keeps Video size. Auto fits the complete image. Serial :display 0 restores 1080p." :
            "Update the core package to enable display resolution changes.";
        return item;
    }
    if (menu->page == MT_MENU_PAGE_VIDEO && index < video_setting_start(view)) {
        if (index == 0U) {
            item.label = "Display source";
            item.value = view != NULL && view->video_test_enabled ?
                         "Multitini standby" : "Active core";
            item.help = has_core(view) ?
                "Enter switches between the running core and the standby screen." :
                "Select a core from Cores to display a running system.";
        } else {
            const size_t core = active_core(view);
            item.label = "Core variant";
            item.value = "Unavailable";
            item.help = "Left/Right chooses a variant. Enter loads it and "
                        "restores that core's media.";
            if (core < core_count(view) &&
                menu->selected_variant[core] < variant_count(&view->cores[core]))
                item.value = view->cores[core].package
                    .variants[menu->selected_variant[core]].label;
        }
    } else if (menu->page == MT_MENU_PAGE_VIDEO) {
        static const char *const labels[] = {
            "Border", "Border type", "Border color", "Padding (px)"
        };
        static const char *const styles[] = {
            "Single line", "Double line", "Corner brackets", "Dashed", "Soft glow"
        };
        static const char *const colors[] = {
            "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "Light gray",
            "Dark gray", "Light blue", "Light green", "Light cyan", "Light red",
            "Light magenta", "Yellow", "White"
        };
        static const char *const padding[] = {
            "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
            "11", "12", "13", "14", "15", "16", "17", "18", "19", "20"
        };
        index -= video_setting_start(view);
        if (index >= 4U) {
            static const char *const vectrex_labels[] = {
                "Overlay enabled", "Overlay brightness", "Beam brightness"
            };
            static const char *const opacity[] = {"25%", "50%", "75%", "100%"};
            static const char *const gain[] = {"1x", "2x", "3x", "4x"};
            index -= 4U;
            if (index >= 3U || !vectrex_video_visible(view)) return item;
            item.label = vectrex_labels[index];
            item.control = index == 0U ? MT_MENU_CONTROL_CHECKBOX : MT_MENU_CONTROL_SLIDER;
            item.control_value = index == 0U ? (view->vectrex_overlay_enabled ? 1U : 0U) :
                index == 1U ? (view->vectrex_overlay_opacity < 4U ? view->vectrex_overlay_opacity : 1U) :
                (view->vectrex_beam_gain < 4U ? view->vectrex_beam_gain : 1U);
            item.control_max = index == 0U ? 1U : 3U;
            item.enabled = view->vectrex_video_supported;
            item.help = "Left/Right or Enter changes this setting. Saved for Vectrex.";
            if (!item.enabled) {
                item.value = "Unsupported";
                item.help = "Update the Vectrex core package to use overlays and beam brightness. Saved settings are retained.";
            } else if (index == 0U) {
                item.value = view->vectrex_overlay_enabled ? "On" : "Off";
                item.help = view->vectrex_overlay_error ?
                    "Overlay load failed. Select a valid .ovr file from Media. The game can run without an overlay." :
                    (view->vectrex_overlay_loaded ?
                     "An overlay is loaded. Enter toggles it; Left turns it off and Right turns it on." :
                     "No overlay loaded. Select a .ovr file from Media, or place a matching file beside the game.");
            } else if (index == 1U) {
                item.value = opacity[view->vectrex_overlay_opacity < 4U ?
                    view->vectrex_overlay_opacity : 1U];
                item.help = "Set artwork brightness from 25% to 100%. Beam brightness is controlled separately.";
            } else {
                item.value = gain[view->vectrex_beam_gain < 4U ? view->vectrex_beam_gain : 1U];
                item.help = "Multiply vector brightness from 1x to 4x. Brighter values may make highlights reach full white.";
            }
            return item;
        }
        item.label = labels[index];
        if (index == 0U) {
            item.control = MT_MENU_CONTROL_CHECKBOX;
            item.control_value = view != NULL && view->border_enabled ? 1U : 0U;
        } else if (index == 3U) {
            item.control = MT_MENU_CONTROL_SLIDER;
            item.control_min = 1U;
            item.control_max = 20U;
            item.control_value = view != NULL && view->border_padding >= 1U && view->border_padding <= 20U ?
                view->border_padding : 6U;
        }
        item.enabled = has_core(view) && view->border_supported;
        item.help = "Left/Right changes this setting. Enter cycles it. Saved separately for each core.";
        if (!has_core(view)) {
            item.value = "Unavailable";
            item.help = "Select a core from Cores before configuring its border.";
        } else if (!view->border_supported) {
            item.value = "Unsupported";
            item.help = "Update this core package to use borders. Saved settings are retained.";
        } else if (index == 0U) {
            item.value = view->border_enabled ? "On" : "Off";
            item.help = "Enter toggles the border. Left turns it off; Right turns it on. Saved for this core.";
        } else if (index == 1U) {
            item.value = styles[view->border_style < 5U ? view->border_style : 0U];
        } else if (index == 2U) {
            item.value = colors[view->border_color < 16U ? view->border_color : 7U];
            item.help = "Choose one of sixteen muted classic colors. Left/Right or Enter changes the color.";
        } else {
            item.value = padding[view->border_padding >= 1U && view->border_padding <= 20U ?
                                 view->border_padding - 1U : 5U];
            item.help = "Leave 1 to 20 output pixels between the image and border. Left/Right stops at limits; Enter cycles.";
        }
    } else {
        switch (index) {
        case 0:
            item.label = "Restart active core";
            item.value = "Restart";
            item.help = "Commits pending media changes before restarting "
                        "the active core.";
            break;
        case 1:
            item.label = "Autoboot current core";
            item.value = view != NULL && view->settings_auto_boot ? "On" : "Off";
            item.control = MT_MENU_CONTROL_CHECKBOX;
            item.control_value = view != NULL && view->settings_auto_boot ? 1U : 0U;
            item.help = "Starts this core at power-on. Off keeps the standby "
                        "screen until you choose a core.";
            break;
        default:
            item.label = "Golden updater";
            item.value = "Return";
            item.enabled = true;
            item.help = "Saves pending media changes, then returns to the "
                        "board's recovery updater.";
            break;
        }
        if (!item.enabled)
            item.help = "Select a core from Cores before changing this setting.";
    }
    return item;
}

static void draw_item(mt_ui_frame_t *frame, size_t row, bool selected,
                       const mt_menu_item_t *item)
{
    const mt_ui_color_t bg = selected ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_ROW;
    const mt_ui_color_t label_color = item->enabled ? MT_UI_COLOR_TEXT :
                                                     MT_UI_COLOR_MUTED;
    mt_ui_color_t color = item->value_color == MT_UI_COLOR_BACKGROUND ?
                         MT_UI_COLOR_MUTED : item->value_color;
    if (selected && item->enabled && color == MT_UI_COLOR_MUTED)
        color = MT_UI_COLOR_BLUE;
    mt_ui_fill(frame, ITEM_X, row, ITEM_W, 1U, bg);
    if (selected) mt_ui_fill(frame, ITEM_X, row, 1U, 1U, MT_UI_COLOR_BLUE);
    field(frame, LABEL_X, row, LABEL_W, item->label, label_color, bg);
    mt_ui_text_field(frame, VALUE_X, row, VALUE_W, item->value, color, bg,
                     false, true);
    if (item->control != MT_MENU_CONTROL_VALUE) {
        const mt_ui_control_state_t state = item->enabled ? MT_UI_CONTROL_ENABLED : MT_UI_CONTROL_DISABLED;
        if (item->control == MT_MENU_CONTROL_CHECKBOX)
            mt_ui_checkbox(frame, VALUE_X, row, item->control_value != 0U, state, bg);
        else
            mt_ui_slider(frame, VALUE_X, row, 20U, item->control_value,
                          item->control_min, item->control_max, state, bg);
    }
    if (!item->enabled && (item->action != MT_MENU_ACTION_NONE || item->control != MT_MENU_CONTROL_VALUE))
        mt_ui_control_hint(frame, VALUE_X, row + 1U, VALUE_W, MT_UI_CONTROL_DISABLED, MT_UI_COLOR_PANEL);
}

static void render_empty(mt_ui_frame_t *frame, const char *title, const char *detail)
{
    field(frame, LABEL_X, LIST_Y + 2U, CONTENT_W, title,
          MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    field(frame, LABEL_X, LIST_Y + 4U, CONTENT_W, detail,
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
}

static void render_cores(const mt_menu_t *menu, const mt_menu_view_t *view,
                          mt_ui_frame_t *frame, size_t first)
{
    size_t index;
    const size_t count = core_count(view);
    field(frame, LABEL_X, LIST_Y - 1U, LABEL_W, "SYSTEM",
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    field(frame, VALUE_X, LIST_Y - 1U, VALUE_W - 5U, "VARIANT",
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    if (count == 0U) {
        render_empty(frame, "Your core library is empty.",
                      "Add core packages to /multitini/cores on SD.");
        help_text(frame, "Each core has its own package and media folder. "
                         "Install packages and restart to update the library.");
        return;
    }
    for (index = first; index < count && index - first < MT_MENU_LIST_ROWS;
         ++index) {
        const mt_core_selection_t *core = &view->cores[index];
        const size_t variant = menu->selected_variant[index];
        const bool valid = variant < variant_count(core);
        const bool running = index == active_core(view) && valid &&
            view->active_variant_id != NULL &&
            strcmp(view->active_variant_id, core->package.variants[variant].id) == 0;
        const bool selected = menu->row == index;
        const size_t row = LIST_Y + index - first;
        const mt_ui_color_t bg = selected ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_ROW;
        mt_ui_fill(frame, ITEM_X, row, ITEM_W, 1U, bg);
        if (selected) mt_ui_fill(frame, ITEM_X, row, 1U, 1U, MT_UI_COLOR_BLUE);
        field(frame, LABEL_X, row, LABEL_W, core->package.name, MT_UI_COLOR_TEXT, bg);
        field(frame, VALUE_X, row, VALUE_W - 5U,
              valid ? core->package.variants[variant].label : "No image",
              selected ? MT_UI_COLOR_BLUE : MT_UI_COLOR_MUTED, bg);
        field(frame, VALUE_X + VALUE_W - 4U, row, 4U, running ? "LIVE" : "",
              MT_UI_COLOR_SUCCESS, bg);
    }
    help_text(frame, "Enter starts this system. Left/Right chooses its variant. "
                     "Pending saves finish before switching.");
}

static void format_size(char *text, size_t capacity, size_t size)
{
    if (size >= 1024U * 1024U)
        (void)snprintf(text, capacity, "%lu.%lu MiB",
            (unsigned long)(size / (1024U * 1024U)),
            (unsigned long)((size % (1024U * 1024U)) * 10U / (1024U * 1024U)));
    else if (size >= 1024U)
        (void)snprintf(text, capacity, "%lu KiB", (unsigned long)(size / 1024U));
    else
        (void)snprintf(text, capacity, "%lu B", (unsigned long)size);
}

static void render_browser(const mt_menu_t *menu, const mt_menu_view_t *view,
                            mt_ui_frame_t *frame, size_t first)
{
    size_t index;
    const size_t count = row_count(menu, view);
    field(frame, LABEL_X, LIST_Y - 1U, NAME_W, "NAME",
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    mt_ui_text_field(frame, SIZE_X, LIST_Y - 1U, SIZE_W, "SIZE",
                     MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL, false, true);
    if (count == 0U) {
        render_empty(frame, "No matching files.",
                      "Escape returns to the Media page.");
        help_text(frame, "Place compatible content in this core's media folder, "
                         "then reopen the browser.");
        return;
    }
    for (index = first; index < count && index - first < MT_MENU_LIST_ROWS;
         ++index) {
        char size_text[24];
        const bool selected = menu->row == index;
        const size_t row = LIST_Y + index - first;
        const mt_ui_color_t bg = selected ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_ROW;
        if (directory_row(view, index))
            (void)snprintf(size_text, sizeof(size_text), "%s",
                (view->files[index].cookie & MT_MENU_FILE_PARENT) != 0U ? "Up" : "Folder");
        else format_size(size_text, sizeof(size_text), view->files[index].size);
        mt_ui_fill(frame, ITEM_X, row, ITEM_W, 1U, bg);
        if (selected) mt_ui_fill(frame, ITEM_X, row, 1U, 1U, MT_UI_COLOR_BLUE);
        field(frame, LABEL_X, row, NAME_W, view->files[index].name,
              (view->files[index].cookie & MT_MENU_FILE_TEXT) != 0U ? MT_UI_COLOR_WARNING : MT_UI_COLOR_TEXT, bg);
        mt_ui_text_field(frame, SIZE_X, row, SIZE_W, size_text, MT_UI_COLOR_MUTED, bg,
                         false, true);
    }
    if (menu->row < count) {
        if ((view->files[menu->row].cookie & MT_MENU_FILE_PARENT) != 0U)
            help_text(frame, "Enter, Left or Backspace opens the parent folder.");
        else if (directory_row(view, menu->row)) {
            char text[MT_MENU_FILE_NAME_MAX + 64U];
            (void)snprintf(text, sizeof(text), "%s - Enter or Right opens this folder.",
                           view->files[menu->row].name);
            help_text(frame, text);
        } else if ((view->files[menu->row].cookie & MT_MENU_FILE_TEXT) != 0U)
            help_text(frame, "Enter reads this document. Escape returns to the same file and scroll position.");
        else help_text(frame, view->files[menu->row].name);
    }
}

static void render_settings(const mt_menu_t *menu, const mt_menu_view_t *view,
                             mt_ui_frame_t *frame, size_t first)
{
    size_t index;
    size_t count;
    const mt_menu_item_t *items = page_items(menu->page, view, &count);
    const bool builtins = menu->page == MT_MENU_PAGE_VIDEO ||
        (menu->page == MT_MENU_PAGE_SYSTEM && (view == NULL || view->system_items == NULL));
    if (builtins) count = row_count(menu, view);
    if (count == 0U) {
        render_empty(frame, has_core(view) ? "Everything is ready." :
                                            "Choose a core to get started.",
            has_core(view) ? "This core has no settings on this page." :
                             "Use Tab to open your core library.");
        help_text(frame, has_core(view) ? view->core_description :
            "Core selection, media, controls and settings share one interface "
            "across every system.");
        return;
    }
    for (index = first; index < count && index - first < visible_rows(menu);
         ++index) {
        const mt_menu_item_t item = builtins ? builtin_item(menu, view, index) :
                                              items[index];
        draw_item(frame, LIST_Y + (index - first) * 2U, menu->row == index, &item);
        if (menu->row == index)
            help_text(frame, item.help != NULL ? item.help :
                (item.enabled ? "Enter applies the selected setting." :
                                "This item is informational."));
    }
}

static void render_document(const mt_menu_view_t *view, mt_ui_frame_t *frame)
{
    const mt_text_reader_t *const reader = view->text_reader;
    char text[MT_MENU_TEXT_COLUMNS + 1U];
    size_t row;
    mt_ui_fill(frame, BODY_X, 6U, BODY_W, HELP_Y - 6U, MT_UI_COLOR_PANEL);
    field(frame, ITEM_X, 6U, ITEM_W, view->text_title, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    (void)snprintf(text, sizeof(text), "%lu-%lu / %lu lines%s",
        (unsigned long)(reader->line_count == 0U ? 0U : reader->top + 1U),
        (unsigned long)(reader->line_count - reader->top < MT_MENU_TEXT_ROWS ? reader->line_count : reader->top + MT_MENU_TEXT_ROWS),
        (unsigned long)reader->line_count, reader->truncated ? "  |  First 256 KiB only" : "");
    field(frame, ITEM_X, 8U, ITEM_W, text, reader->truncated ? MT_UI_COLOR_WARNING : MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    if (reader->line_count == 0U)
        field(frame, ITEM_X, LIST_Y, ITEM_W, "This document is empty.", MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    for (row = 0U; row < MT_MENU_TEXT_ROWS && reader->top + row < reader->line_count; ++row) {
        if (mt_text_reader_line(reader, reader->top + row, text, sizeof(text)))
            field(frame, ITEM_X, LIST_Y + row, MT_MENU_TEXT_COLUMNS, text, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    }
    help_text(frame, "Read-only document. Up/Down scrolls one line; Left/Right or Page Up/Down scrolls a page. Escape returns to the file browser.");
}

static void render_clock(const mt_menu_t *menu, mt_ui_frame_t *frame)
{
    static const char *const labels[] = {"Year", "Month", "Day", "Hour", "Minute", "Second", "Save time"};
    const unsigned values[] = {menu->clock.year, menu->clock.month, menu->clock.day,
        menu->clock.hour, menu->clock.minute, menu->clock.second};
    size_t index;
    mt_ui_fill(frame, BODY_X, 6U, BODY_W, HELP_Y - 6U, MT_UI_COLOR_PANEL);
    field(frame, ITEM_X, 6U, ITEM_W, "Set board clock", MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    field(frame, ITEM_X, 8U, ITEM_W, "Edit local date and time, then select Save time.", MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    for (index = 0U; index < 7U; ++index) {
        char value[16];
        mt_menu_item_t item = {0};
        if (index < 6U) (void)snprintf(value, sizeof(value), index == 0U ? "%04u" : "%02u", values[index]);
        else (void)snprintf(value, sizeof(value), "Apply");
        item.label = labels[index]; item.value = value;
        item.enabled = true; item.value_color = MT_UI_COLOR_BLUE;
        draw_item(frame, LIST_Y + index * 2U, menu->clock_field == index, &item);
    }
    help_text(frame, "Left/Right changes a value. Up/Down selects a field. Enter advances or saves. Escape cancels without changing the clock.");
}

static void render_gamepad(const mt_menu_t *menu, const mt_menu_view_t *view,
                            mt_ui_frame_t *frame)
{
    static const char *const commands[] = {"Set all controls", "Test controller", "Stick and trigger settings", "Restore defaults", "Save changes", "Discard changes", "Back"};
    const unsigned slot = menu->gamepad_slot < MT_GAMEPAD_SLOTS ? menu->gamepad_slot : 0U;
    const mt_controller_device_t *device = &view->gamepad_devices[slot];
    const mt_controller_config_t *config = &menu->gamepad_config[slot];
    const bool c64 = view->active_core_id != NULL && strcmp(view->active_core_id, "c64") == 0;
    const uint8_t player = view->gamepad_player[slot];
    const char *routing = !device->connected || player > 1U ? "Unassigned" :
        player == 0U ? (c64 ? "Joystick port 1" : "Player 1") : (c64 ? "Joystick port 2" : "Player 2");
    const unsigned actions = gamepad_action_count(view), command_start = 2U + actions;
    char text[160], value[80];
    mt_ui_fill(frame, BODY_X, 6U, BODY_W, HELP_Y - 6U, MT_UI_COLOR_PANEL);
    field(frame, ITEM_X, 6U, ITEM_W, menu->gamepad_test ? "Test controller" : menu->gamepad_sticks ? "Stick and trigger settings" : "Controllers", MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    (void)snprintf(text, sizeof(text), "Controller %u: %s%s  |  Now: %s", slot + 1U, device->name[0] != '\0' ? device->name : "None connected", device->connected ? "" : " (disconnected)", routing);
    field(frame, ITEM_X, 8U, ITEM_W, text, device->connected ? MT_UI_COLOR_TEXT : MT_UI_COLOR_WARNING, MT_UI_COLOR_PANEL);
    if (menu->gamepad_test) {
        unsigned position = 0U;
        const uint32_t present = device->buttons_present | view->gamepad_preview.buttons;
        for (unsigned button = 0U; button < 32U; ++button) {
            if ((present & (UINT32_C(1) << button)) == 0U) continue;
            mt_controls_source_name(device, (uint16_t)(MT_CONTROL_SOURCE_BUTTON + button), value, sizeof(value));
            (void)snprintf(text, sizeof(text), "%s: %s", value, (view->gamepad_preview.buttons & (UINT32_C(1) << button)) != 0U ? "HELD" : "released");
            field(frame, ITEM_X + (position % 2U) * (ITEM_W / 2U), LIST_Y + position / 2U, ITEM_W / 2U - 1U, text, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
            ++position;
        }
        const unsigned start = LIST_Y + (position + 1U) / 2U + 1U;
        static const char *const directions[] = {"D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right"};
        const uint8_t directions_held = mt_gamepad_hat_bits(view->gamepad_preview.hat);
        for (unsigned index = 0U; index < 4U; ++index) {
            (void)snprintf(text, sizeof(text), "%s: %s", directions[index], (directions_held & (1U << index)) != 0U ? "HELD" : "released");
            field(frame, ITEM_X + (index % 2U) * (ITEM_W / 2U), start + index / 2U, ITEM_W / 2U - 1U, text, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
        }
        for (unsigned index = 0U; index < MT_GAMEPAD_AXIS_COUNT; ++index) {
            mt_controls_source_name(device, (uint16_t)(MT_CONTROL_SOURCE_AXIS + index), value, sizeof(value));
            if ((view->gamepad_preview.axis_valid_mask & (1U << index)) != 0U)
                (void)snprintf(text, sizeof(text), "%s: %d", value, (int)view->gamepad_preview.axis[index]);
            else (void)snprintf(text, sizeof(text), "%s: not reported", value);
            field(frame, ITEM_X, start + 3U + index, ITEM_W, text, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
        }
        mt_controls_output_t mapped;
        mt_controls_evaluate(view->gamepad_core_id, config, &view->gamepad_preview, 0U, &mapped);
        size_t used = (size_t)snprintf(text, sizeof(text), "Assigned actions:");
        for (unsigned index = 0U; index < MT_CONTROL_COUNT && used < sizeof(text); ++index) {
            if ((mapped.actions & (UINT32_C(1) << index)) != 0U)
                used += (size_t)snprintf(text + used, sizeof(text) - used, " %s;", mt_controls_action_name(view->gamepad_core_id, index));
        }
        if (mapped.actions == 0U) (void)snprintf(text + used, sizeof(text) - used, " none");
        field(frame, ITEM_X, start + 10U, ITEM_W, text, MT_UI_COLOR_BLUE, MT_UI_COLOR_PANEL);
        if (strlen(text) > ITEM_W) field(frame, ITEM_X, start + 11U, ITEM_W, text + ITEM_W, MT_UI_COLOR_BLUE, MT_UI_COLOR_PANEL);
        if ((mt_controls_action_mask(view->gamepad_core_id) & (UINT32_C(1) << MT_CONTROL_AXIS_X)) != 0U) {
            (void)snprintf(text, sizeof(text), "Mapped stick: X %d  Y %d", (int)mapped.x, (int)mapped.y);
            field(frame, ITEM_X, start + 13U, ITEM_W, text, MT_UI_COLOR_BLUE, MT_UI_COLOR_PANEL);
        }
        help_text(frame, "Press any controller control to test it. Game and navigation actions are paused. F12 or hold Select+Start for 0.5s to return; keyboard Enter/Escape also returns.");
        return;
    }
    if (menu->gamepad_sticks) {
        static const char *const labels[] = {"Controller axis", "Reverse direction", "Dead zone", "Sensitivity", "Reset this axis", "Back to assignments"};
        const unsigned selected = menu->gamepad_control < MT_GAMEPAD_AXIS_COUNT ? menu->gamepad_control : 0U;
        const mt_gamepad_axis_config_t *axis = &config->axis[selected];
        for (unsigned index = 0U; index < 6U; ++index) {
            if (index == 0U) mt_controls_source_name(device, (uint16_t)(MT_CONTROL_SOURCE_AXIS + selected), value, sizeof(value));
            else if (index == 1U) (void)snprintf(value, sizeof(value), "%s", axis->invert ? "Yes" : "No");
            else if (index == 2U) (void)snprintf(value, sizeof(value), "%u%%", (unsigned)axis->deadzone_percent);
            else if (index == 3U) (void)snprintf(value, sizeof(value), "%u%%", (unsigned)axis->sensitivity_percent);
            else (void)snprintf(value, sizeof(value), "Enter");
            mt_menu_item_t item = {.label = labels[index], .value = value, .enabled = true, .value_color = MT_UI_COLOR_BLUE};
            draw_item(frame, LIST_Y + index * 2U, menu->gamepad_field == index, &item);
        }
        (void)snprintf(text, sizeof(text), "Current value: %d  (%s)", (int)view->gamepad_preview.axis[selected], (view->gamepad_preview.axis_valid_mask & (1U << selected)) != 0U ? "reported" : "not reported");
        field(frame, ITEM_X, LIST_Y + 14U, ITEM_W, text, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
        help_text(frame, "Left/Right adjusts the selected setting. Axis tuning applies to this controller for the active system. Save changes on the assignments page.");
        return;
    }
    field(frame, LABEL_X, LIST_Y - 1U, LABEL_W, "Game action / setting", MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    field(frame, VALUE_X, LIST_Y - 1U, VALUE_W, "Controller input", MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    for (unsigned index = 0U; index < command_start + 7U; ++index) {
        const char *label;
        bool enabled = true;
        if (index == 0U) { label = "Controller"; (void)snprintf(value, sizeof(value), "%u: %s", slot + 1U, device->name[0] ? device->name : "None connected"); }
        else if (index == 1U) {
            label = "Assign to";
            enabled = device->connected != 0U;
            const char *assignment = config->assignment == MT_CONTROLLER_PORT1 ? (c64 ? "Joystick port 1" : "Player 1") : config->assignment == MT_CONTROLLER_PORT2 ? (c64 ? "Joystick port 2" : "Player 2") : config->assignment == MT_CONTROLLER_OFF ? "Not used" : "Automatic";
            (void)snprintf(value, sizeof(value), "%s", assignment);
        } else if (index < command_start) {
            const unsigned control = gamepad_action_at(view, index - 2U);
            label = mt_controls_action_name(view->gamepad_core_id, control);
            mt_controls_source_name(device, config->binding[control], value, sizeof(value));
            enabled = device->connected != 0U;
        } else {
            const unsigned command = index - command_start;
            label = commands[command];
            if (command == 4U || command == 5U) enabled = mt_menu_gamepad_dirty(menu);
            else if (command < 4U) enabled = device->connected != 0U;
            (void)snprintf(value, sizeof(value), "%s", command == 4U ? (enabled ? "Unsaved changes" : "Saved") : "Enter");
        }
        mt_menu_item_t item = {.label = label, .value = value, .enabled = enabled, .value_color = enabled ? MT_UI_COLOR_BLUE : MT_UI_COLOR_MUTED};
        draw_item(frame, LIST_Y + index, menu->gamepad_field == index, &item);
        field(frame, VALUE_X, LIST_Y + index, VALUE_W, value, item.value_color,
              menu->gamepad_field == index ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_ROW);
    }
    const unsigned prompt_y = LIST_Y + command_start + 8U;
    if (menu->gamepad_capture) {
        (void)snprintf(text, sizeof(text), "%s%s", menu->gamepad_wizard ? "Set all - assign: " : "Assign: ", mt_controls_action_name(view->gamepad_core_id, menu->gamepad_control));
        field(frame, ITEM_X, prompt_y, ITEM_W, text, MT_UI_COLOR_WARNING, MT_UI_COLOR_PANEL);
    } else field(frame, ITEM_X, prompt_y, ITEM_W, "F12 or hold Select+Start for 0.5s opens the menu. Solo presses remain game actions.", MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    field(frame, ITEM_X, prompt_y + 2U, ITEM_W, menu->gamepad_notice, MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    help_text(frame, menu->gamepad_conflict ? menu->gamepad_notice : menu->gamepad_capture ?
        "Navigation pauses while assigning. Esc cancels; Delete clears; Tab skips in Set all. F12 or hold Select+Start for 0.5s returns to the menu." :
        "Up/Down chooses an action. Enter changes it. Delete clears it. Left/Right chooses the controller or player. Save changes or Discard changes before leaving.");
}

void mt_menu_render(const mt_menu_t *menu, const mt_menu_view_t *view,
                    mt_ui_frame_t *frame)
{
    mt_menu_t normalized;
    size_t page;
    size_t count;
    size_t core;
    char text[64];
    const char *active_name = "No core running";
    const char *active_variant = "Choose a system from Cores";
    if (menu == NULL || frame == NULL) return;
    normalized = *menu;
    normalize(&normalized, view);
    menu = &normalized;
    core = active_core(view);
    if (core < core_count(view)) {
        size_t index;
        const mt_core_package_t *package = &view->cores[core].package;
        active_name = package->name;
        active_variant = "Active";
        for (index = 0U; index < variant_count(&view->cores[core]); ++index) {
            if (view->active_variant_id != NULL &&
                strcmp(package->variants[index].id, view->active_variant_id) == 0)
                active_variant = package->variants[index].label;
        }
    }
    mt_ui_clear(frame, MT_UI_COLOR_BACKGROUND);
    field(frame, 3U, 2U, 32U, "MULTITINI ONE", MT_UI_COLOR_TEXT,
          MT_UI_COLOR_BACKGROUND);
    field(frame, 3U, 3U, 32U, "Systems / Media / Settings", MT_UI_COLOR_MUTED,
          MT_UI_COLOR_BACKGROUND);
    mt_ui_text_field(frame, HEADER_ACTIVE_X, 2U, HEADER_ACTIVE_W, active_name,
                     MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_BACKGROUND, false, true);
    mt_ui_text_field(frame, HEADER_ACTIVE_X, 3U, HEADER_ACTIVE_W, active_variant,
                     has_core(view) ? MT_UI_COLOR_BLUE : MT_UI_COLOR_MUTED,
                     MT_UI_COLOR_BACKGROUND, false, true);
    mt_ui_fill(frame, SIDE_X, 5U, MT_UI_COLUMNS - 4U, 1U, MT_UI_COLOR_RAISED);
    mt_ui_fill(frame, SIDE_X, 6U, SIDE_W, HELP_Y - 3U, MT_UI_COLOR_PANEL);
    mt_ui_fill(frame, BODY_X, 6U, BODY_W, HELP_Y - 7U, MT_UI_COLOR_PANEL);
    field(frame, SIDE_X + 2U, 6U, SIDE_W - 3U, "MAIN MENU",
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    for (page = 0U; page < MT_MENU_PAGE_COUNT; ++page) {
        const size_t row = 8U + page * 2U;
        const bool selected = menu->page == (mt_menu_page_t)page;
        const mt_ui_color_t bg = selected ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_PANEL;
        mt_ui_fill(frame, SIDE_X, row, SIDE_W, 1U, bg);
        if (selected) mt_ui_fill(frame, SIDE_X, row, 1U, 1U, MT_UI_COLOR_BLUE);
        field(frame, SIDE_X + 2U, row, SIDE_W - 3U,
              mt_menu_page_name((mt_menu_page_t)page),
              selected ? MT_UI_COLOR_TEXT : MT_UI_COLOR_MUTED, bg);
    }
    (void)snprintf(text, sizeof(text), "%u cores", (unsigned)core_count(view));
    if (has_core(view) && menu->visible)
        field(frame, SIDE_X + 2U, HELP_Y - 5U, SIDE_W - 3U, "Core paused",
              MT_UI_COLOR_BLUE, MT_UI_COLOR_PANEL);
    field(frame, SIDE_X + 2U, HELP_Y - 3U, SIDE_W - 3U, text,
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    field(frame, SIDE_X + 2U, HELP_Y, SIDE_W - 3U,
          view != NULL && view->text_reader != NULL && view->text_reader->active ?
          "Esc back" : menu->gamepad_test ? "Esc back" : menu->gamepad_capture ? "Esc cancels" : menu->gamepad_edit ? "Tab next item" : menu->clock_edit ? "Tab next field" : "Tab section",
          MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL);
    mt_ui_fill(frame, BODY_X, HELP_Y, BODY_W, 3U, MT_UI_COLOR_RAISED);
    mt_ui_fill(frame, BODY_X, HELP_Y, 1U, 3U, MT_UI_COLOR_BLUE);
    field(frame, LABEL_X, HELP_Y, CONTENT_W,
          menu->browser == MT_MENU_BROWSER_NONE ?
          "ABOUT THIS ITEM" : (has_directories(view) ? "SELECTED ITEM" : "SELECTED FILE"), MT_UI_COLOR_BLUE,
          MT_UI_COLOR_RAISED);
    count = row_count(menu, view);
    field(frame, ITEM_X, 6U, ITEM_W - 14U,
          menu->browser == MT_MENU_BROWSER_NONE ?
          mt_menu_page_name(menu->page) : browser_title(menu->browser),
          MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL);
    if (count != 0U) {
        (void)snprintf(text, sizeof(text), "%u / %u",
                       (unsigned)(menu->row + 1U), (unsigned)count);
        mt_ui_text_field(frame, SIZE_X, 6U, SIZE_W, text, MT_UI_COLOR_MUTED,
                         MT_UI_COLOR_PANEL, false, true);
    }
    field(frame, ITEM_X, 8U, ITEM_W - 1U,
          menu->browser == MT_MENU_BROWSER_NONE ?
          (menu->page == MT_MENU_PAGE_INPUT && view != NULL &&
           view->input_help != NULL ?
           view->input_help : page_caption(menu->page)) :
          (view != NULL ? view->content_root : NULL), MT_UI_COLOR_MUTED,
          MT_UI_COLOR_PANEL);
    if (view != NULL && view->text_reader != NULL && view->text_reader->active)
        render_document(view, frame);
    else if (menu->gamepad_edit && view != NULL)
        render_gamepad(menu, view, frame);
    else if (menu->clock_edit)
        render_clock(menu, frame);
    else if (menu->browser != MT_MENU_BROWSER_NONE)
        render_browser(menu, view, frame, menu->first_row);
    else if (menu->page == MT_MENU_PAGE_CORES)
        render_cores(menu, view, frame, menu->first_row);
    else
        render_settings(menu, view, frame, menu->first_row);
    /* Persistent faults take priority over transient success messages. */
    {
        const char *status = view != NULL ? view->status_text : NULL;
        mt_ui_color_t color = view != NULL &&
                             view->status_color != MT_UI_COLOR_BACKGROUND ?
                             view->status_color : MT_UI_COLOR_MUTED;
        if (color == MT_UI_COLOR_RED) {
            /* Keep the actionable command error, including failed recovery. */
        } else if (view != NULL && view->save_faulted) {
            status = "Save failed. Keep the system powered and check the SD card.";
            color = MT_UI_COLOR_WARNING;
        } else if (view != NULL && view->settings_faulted) {
            status = "Settings need attention. Check the SD card before restarting.";
            color = MT_UI_COLOR_WARNING;
        } else if (view != NULL && view->save_dirty) {
            status = "Saving progress. Wait for Saved before removing power.";
            color = MT_UI_COLOR_WARNING;
        }
        field(frame, 3U, STATUS_Y, MT_UI_COLUMNS - 6U, status,
              color, MT_UI_COLOR_BACKGROUND);
    }
    mt_ui_fill(frame, SIDE_X, FOOTER_Y, MT_UI_COLUMNS - 4U, 1U, MT_UI_COLOR_RAISED);
    field(frame, 3U, FOOTER_Y, MT_UI_COLUMNS - 6U,
        view != NULL && view->text_reader != NULL && view->text_reader->active ?
        "[Up/Down] Line  [Left/Right/PgUp/PgDn] Page  [Home/End] First/Last  [Esc] Back  [F12] Close" :
        menu->gamepad_capture ? (menu->gamepad_conflict ? "[Enter] Move assignment  [Esc] Cancel  [F12] Menu" : "[Press control] Assign  [Delete] Clear  [Tab] Skip in Set all  [Esc] Cancel  [F12] Menu") :
        menu->gamepad_test ? "[Esc/Enter] Back  [F12] Menu  [PrtScr] Core screenshot  [Shift+PrtScr] Screen screenshot" :
        menu->gamepad_edit ? "[Enter] Change/Open  [Delete] Clear  [Esc] Back/Cancel  [F12] Menu  [PrtScr] Game  [Shift+PrtScr] Screen" :
        menu->clock_edit ? "[Up/Down] Field  [Left/Right] Adjust  [Enter] Next/Save  [Esc] Cancel" :
        menu->browser != MT_MENU_BROWSER_NONE && has_directories(view) ?
        "[Enter] Open/Load  [Right] Open folder  [Left/Backspace] Up  [PgUp/Dn] Page  [Esc] Back" :
        menu->browser == MT_MENU_BROWSER_DISK_PROGRAM ?
        "[Enter] Launch  [Right] Load  [Left] Direct  [PgUp/Dn] Page  [Esc] Back" :
        (menu->browser != MT_MENU_BROWSER_NONE ?
         "[Enter] Open  [Up/Down] Select  [PgUp/PgDn] Page  [Home/End] First/Last  [Esc] Back" :
         (menu->page == MT_MENU_PAGE_VIDEO || menu->page == MT_MENU_PAGE_EFFECTS) ?
         "[L/R] Tab  [D-pad] Select/Adjust  [A/Enter] Apply  [B/Esc] Back  [F12/hold Select+Start] Close" :
         "[L/R or Shift+Tab/Tab] Tab  [D-pad] Select  [A/Enter] Apply  [B/Esc] Back  [F12/hold Select+Start] Close"),
        MT_UI_COLOR_MUTED, MT_UI_COLOR_RAISED);
    if (menu->golden_update_confirm) {
        const size_t x = (MT_UI_COLUMNS - 72U) / 2U;
        const size_t y = (MT_UI_ROWS - 9U) / 2U;
        mt_ui_fill(frame, x, y, 72U, 9U, MT_UI_COLOR_RAISED);
        field(frame, x + 3U, y + 1U, 66U, "Update Golden Boot?",
              MT_UI_COLOR_TEXT, MT_UI_COLOR_RAISED);
        field(frame, x + 3U, y + 3U, 66U, "Update Golden Boot from BOOT.BIN and restart?",
              MT_UI_COLOR_TEXT, MT_UI_COLOR_RAISED);
        field(frame, x + 3U, y + 4U, 66U, "Keep the card powered until the restart completes.",
              MT_UI_COLOR_MUTED, MT_UI_COLOR_RAISED);
        field(frame, x + 17U, y + 6U, 16U, "Cancel",
              MT_UI_COLOR_TEXT, menu->golden_update_accept ? MT_UI_COLOR_RAISED : MT_UI_COLOR_ACTIVE);
        field(frame, x + 37U, y + 6U, 20U, "Update and restart",
              MT_UI_COLOR_TEXT, menu->golden_update_accept ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_RAISED);
        field(frame, 3U, FOOTER_Y, MT_UI_COLUMNS - 6U,
              "[Left/Right or D-pad] Choose  [A/Enter] Confirm  [B/Esc] Cancel",
              MT_UI_COLOR_MUTED, MT_UI_COLOR_RAISED);
    }
}
