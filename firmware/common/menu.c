/* SPDX-License-Identifier: GPL-3.0-only */
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
    default:
        break;
    }
    if (items == NULL) *count = 0U;
    if (*count > MT_MENU_MAX_ITEMS) *count = MT_MENU_MAX_ITEMS;
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
        return video_setting_start(view) + 4U + (vectrex_video_visible(view) ? 3U : 0U);
    case MT_MENU_PAGE_SYSTEM:
        return 3U;
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
        action.kind = directory_row(view, menu->row) ? MT_MENU_ACTION_OPEN_DIRECTORY :
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
    } else if (menu->page == MT_MENU_PAGE_SYSTEM) {
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

mt_menu_action_t mt_menu_key_with_modifiers(mt_menu_t *menu,
                                           const mt_menu_view_t *view,
                                           uint8_t hid_usage, bool pressed,
                                           uint8_t modifiers)
{
    mt_menu_action_t action = no_action();
    size_t count;
    if (menu == NULL || !pressed) return action;
    normalize(menu, view);
    if (hid_usage == MT_MENU_HID_F12) {
        menu->visible = !menu->visible;
        return action;
    }
    if (!menu->visible) return action;
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
        }
        break;
    case MT_MENU_HID_ENTER:
        action = activate(menu, view);
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
        "Cores", "Media", "Video", "Input", "Options", "System"
    };
    return (unsigned)page < (unsigned)MT_MENU_PAGE_COUNT ? names[page] : "Cores";
}

static const char *page_caption(mt_menu_page_t page)
{
    static const char *const captions[] = {
        "Choose a system. Your media stays with each core.",
        "Load content and manage the active session.",
        "Display and border. Saved separately for each core.",
        "Controls for the active system.",
        "Settings supplied by the active core.",
        "Startup, restart and recovery."
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
              MT_UI_COLOR_TEXT, bg);
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
        } else help_text(frame, view->files[menu->row].name);
    }
}

static void render_settings(const mt_menu_t *menu, const mt_menu_view_t *view,
                             mt_ui_frame_t *frame, size_t first)
{
    size_t index;
    size_t count;
    const mt_menu_item_t *items = page_items(menu->page, view, &count);
    const bool builtins = menu->page == MT_MENU_PAGE_VIDEO ||
                          menu->page == MT_MENU_PAGE_SYSTEM;
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
    field(frame, SIDE_X + 2U, HELP_Y, SIDE_W - 3U, "Tab section",
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
    if (menu->browser != MT_MENU_BROWSER_NONE)
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
        menu->browser != MT_MENU_BROWSER_NONE && has_directories(view) ?
        "[Enter] Open/Load  [Right] Open folder  [Left/Backspace] Up  [PgUp/Dn] Page  [Esc] Back" :
        menu->browser == MT_MENU_BROWSER_DISK_PROGRAM ?
        "[Enter] Launch  [Right] Load  [Left] Direct  [PgUp/Dn] Page  [Esc] Back" :
        (menu->browser != MT_MENU_BROWSER_NONE ?
         "[Enter] Load  [Up/Down] Select  [PgUp/PgDn] Page  [Home/End] First/Last  [Esc] Back" :
         menu->page == MT_MENU_PAGE_VIDEO ?
         "[Tab/Shift+Tab] Section  [Up/Down] Select  [Left/Right] Adjust  [Enter] Apply/Cycle  [Esc/F12] Close" :
         "[Tab/Shift+Tab] Section  [Up/Down] Select  [Enter] Apply  [Esc/F12] Close"),
        MT_UI_COLOR_MUTED, MT_UI_COLOR_RAISED);
}
