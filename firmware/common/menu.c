/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_menu.h"

#include <stdio.h>
#include <string.h>

static size_t bounded_core_count(const mt_menu_view_t *view)
{
    if (view == NULL || view->cores == NULL) return 0U;
    return view->core_count > MT_MENU_MAX_CORES ? MT_MENU_MAX_CORES :
           view->core_count;
}

static size_t page_rows(mt_menu_page_t page, const mt_menu_view_t *view)
{
    switch (page) {
    case MT_MENU_PAGE_CORES: return bounded_core_count(view);
    case MT_MENU_PAGE_MEDIA: return 3U;
    case MT_MENU_PAGE_VIDEO: return 2U;
    case MT_MENU_PAGE_INPUT: return 2U;
    case MT_MENU_PAGE_SYSTEM: return 2U;
    default: return 0U;
    }
}

void mt_menu_init(mt_menu_t *menu, const mt_menu_view_t *view)
{
    size_t index;
    const size_t count = bounded_core_count(view);
    if (menu == NULL) return;
    memset(menu, 0, sizeof(*menu));
    menu->page = MT_MENU_PAGE_CORES;
    for (index = 0U; index < count; ++index) {
        const size_t variant_count = view->cores[index].package.variant_count;
        menu->selected_variant[index] =
            view->cores[index].variant_index < variant_count ?
            view->cores[index].variant_index : 0U;
    }
}

static mt_menu_action_t no_action(void)
{
    mt_menu_action_t action = {MT_MENU_ACTION_NONE, 0U, 0U, 0U};
    return action;
}

static size_t active_core_index(const mt_menu_view_t *view)
{
    size_t index;
    const size_t count = bounded_core_count(view);
    if (view == NULL || view->active_core_id == NULL) return count;
    for (index = 0U; index < count; ++index) {
        if (strcmp(view->cores[index].package.id, view->active_core_id) == 0)
            return index;
    }
    return count;
}

mt_menu_action_t mt_menu_key(mt_menu_t *menu, const mt_menu_view_t *view,
                             uint8_t hid_usage, bool pressed)
{
    mt_menu_action_t action = no_action();
    size_t rows;
    if (menu == NULL || !pressed) return action;
    if (hid_usage == MT_MENU_HID_F12) {
        menu->visible = !menu->visible;
        return action;
    }
    if (!menu->visible) return action;
    if (hid_usage == MT_MENU_HID_ESCAPE) {
        if (menu->browser != MT_MENU_BROWSER_NONE) {
            menu->browser = MT_MENU_BROWSER_NONE;
            menu->row = 0U;
        } else {
            menu->visible = false;
        }
        return action;
    }
    if (menu->browser != MT_MENU_BROWSER_NONE) {
        rows = view == NULL ? 0U : view->file_count;
        if (rows == 0U) menu->row = 0U;
        else if (menu->row >= rows) menu->row = rows - 1U;
        if (hid_usage == MT_MENU_HID_UP && rows != 0U)
            menu->row = menu->row == 0U ? rows - 1U : menu->row - 1U;
        else if (hid_usage == MT_MENU_HID_DOWN && rows != 0U)
            menu->row = (menu->row + 1U) % rows;
        else if (hid_usage == MT_MENU_HID_ENTER && rows != 0U) {
            action.kind = menu->browser == MT_MENU_BROWSER_DRIVE8 ?
                          MT_MENU_ACTION_MOUNT_DRIVE8 :
                          MT_MENU_ACTION_LOAD_CONTENT;
            action.file_index = menu->row;
        }
        return action;
    }
    if (hid_usage == MT_MENU_HID_TAB) {
        menu->page = (mt_menu_page_t)(((unsigned)menu->page + 1U) %
                                      (unsigned)MT_MENU_PAGE_COUNT);
        menu->row = 0U;
        return action;
    }

    rows = page_rows(menu->page, view);
    if (rows == 0U) menu->row = 0U;
    else if (menu->row >= rows) menu->row = rows - 1U;
    if (hid_usage == MT_MENU_HID_UP && rows != 0U) {
        menu->row = menu->row == 0U ? rows - 1U : menu->row - 1U;
    } else if (hid_usage == MT_MENU_HID_DOWN && rows != 0U) {
        menu->row = (menu->row + 1U) % rows;
    } else if (menu->page == MT_MENU_PAGE_CORES && rows != 0U &&
               (hid_usage == MT_MENU_HID_LEFT ||
                hid_usage == MT_MENU_HID_RIGHT)) {
        const size_t core = menu->row;
        const size_t variants = view->cores[core].package.variant_count;
        if (variants != 0U) {
            if (hid_usage == MT_MENU_HID_LEFT) {
                menu->selected_variant[core] =
                    menu->selected_variant[core] == 0U ? variants - 1U :
                    menu->selected_variant[core] - 1U;
            } else {
                menu->selected_variant[core] =
                    (menu->selected_variant[core] + 1U) % variants;
            }
        }
    } else if (menu->page == MT_MENU_PAGE_VIDEO && menu->row == 1U &&
               (hid_usage == MT_MENU_HID_LEFT ||
                hid_usage == MT_MENU_HID_RIGHT)) {
        const size_t core = active_core_index(view);
        if (core < bounded_core_count(view)) {
            const size_t variants = view->cores[core].package.variant_count;
            if (variants != 0U) {
                if (hid_usage == MT_MENU_HID_LEFT) {
                    menu->selected_variant[core] =
                        menu->selected_variant[core] == 0U ? variants - 1U :
                        menu->selected_variant[core] - 1U;
                } else {
                    menu->selected_variant[core] =
                        (menu->selected_variant[core] + 1U) % variants;
                }
            }
        }
    } else if (hid_usage == MT_MENU_HID_ENTER && rows != 0U) {
        if (menu->page == MT_MENU_PAGE_CORES) {
            action.kind = MT_MENU_ACTION_SWITCH_CORE;
            action.core_index = menu->row;
            action.variant_index = menu->selected_variant[menu->row];
        } else if (menu->page == MT_MENU_PAGE_MEDIA) {
            if (menu->row == 0U && view != NULL &&
                view->drive8_supported)
                action.kind = view != NULL && view->drive8_mounted ?
                              MT_MENU_ACTION_EJECT_DRIVE8 :
                              MT_MENU_ACTION_BROWSE_DRIVE8;
            else if (menu->row == 1U && view != NULL &&
                     view->cartridge_supported)
                action.kind = view != NULL && view->cartridge_attached ?
                              MT_MENU_ACTION_DETACH_CARTRIDGE :
                              MT_MENU_ACTION_BROWSE_CARTRIDGE;
            else if (menu->row == 2U && view != NULL &&
                     view->program_supported)
                action.kind = MT_MENU_ACTION_BROWSE_PROGRAM;
        } else if (menu->page == MT_MENU_PAGE_VIDEO) {
            if (menu->row == 0U) {
                action.kind = MT_MENU_ACTION_TOGGLE_VIDEO_SOURCE;
            } else {
                const size_t core = active_core_index(view);
                if (core < bounded_core_count(view)) {
                    action.kind = MT_MENU_ACTION_SWITCH_CORE;
                    action.core_index = core;
                    action.variant_index = menu->selected_variant[core];
                }
            }
        } else if (menu->page == MT_MENU_PAGE_INPUT) {
            if (view != NULL && view->c64_input_settings)
                action.kind = menu->row == 0U ?
                              MT_MENU_ACTION_TOGGLE_KEYBOARD_MAP :
                              MT_MENU_ACTION_SWAP_JOYSTICK_PORT;
        } else if (menu->page == MT_MENU_PAGE_SYSTEM) {
            action.kind = menu->row == 0U ? MT_MENU_ACTION_RESET_CORE :
                          MT_MENU_ACTION_REBOOT_GOLDEN;
        }
    }
    return action;
}

const char *mt_menu_page_name(mt_menu_page_t page)
{
    switch (page) {
    case MT_MENU_PAGE_CORES: return "Cores";
    case MT_MENU_PAGE_MEDIA: return "Media";
    case MT_MENU_PAGE_VIDEO: return "Video";
    case MT_MENU_PAGE_INPUT: return "Input";
    case MT_MENU_PAGE_SYSTEM: return "System";
    default: return "Unknown";
    }
}

static void draw_item(mt_ui_frame_t *frame, size_t row, bool selected,
                      const char *label, const char *value)
{
    const mt_ui_color_t background = selected ? MT_UI_COLOR_ACTIVE :
                                             MT_UI_COLOR_ROW;
    mt_ui_fill(frame, 24U, row, 53U, 1U, background);
    mt_ui_text(frame, 26U, row, selected ? "> " : "  ",
               selected ? MT_UI_COLOR_ACCENT : MT_UI_COLOR_TEXT,
               background, false);
    mt_ui_text(frame, 28U, row, label, MT_UI_COLOR_TEXT, background, false);
    if (value != NULL)
        mt_ui_text(frame, 53U, row, value, MT_UI_COLOR_MUTED, background,
                   false);
}

static void render_cores(const mt_menu_t *menu, const mt_menu_view_t *view,
                         mt_ui_frame_t *frame)
{
    size_t index;
    const size_t count = bounded_core_count(view);
    for (index = 0U; index < count && index < 21U; ++index) {
        const mt_core_selection_t *const core = &view->cores[index];
        const size_t variant_index = menu->selected_variant[index];
        const char *variant = variant_index < core->package.variant_count ?
                              core->package.variants[variant_index].label :
                              "No image";
        char value[64];
        const bool active = view->active_core_id != NULL &&
            strcmp(view->active_core_id, core->package.id) == 0 &&
            view->active_variant_id != NULL && variant_index <
                core->package.variant_count &&
            strcmp(view->active_variant_id,
                   core->package.variants[variant_index].id) == 0;
        (void)snprintf(value, sizeof(value), "[< %s >]%s", variant,
                       active ? "  RUNNING" : "");
        draw_item(frame, 7U + index, menu->row == index, core->package.name,
                  value);
    }
    if (count == 0U)
        mt_ui_text(frame, 26U, 8U, "No core packages found on SD card",
                   MT_UI_COLOR_WARNING, MT_UI_COLOR_PANEL, false);
}

static void render_page(const mt_menu_t *menu, const mt_menu_view_t *view,
                        mt_ui_frame_t *frame)
{
    char value[48];
    switch (menu->page) {
    case MT_MENU_PAGE_CORES:
        render_cores(menu, view, frame);
        break;
    case MT_MENU_PAGE_MEDIA:
        draw_item(frame, 7U, menu->row == 0U, "Drive 8",
                  view == NULL || !view->drive8_supported ? "Unavailable" :
                  (view->drive8_mounted ? view->drive8_name : "Load disk..."));
        draw_item(frame, 8U, menu->row == 1U, "Cartridge",
                  view == NULL || !view->cartridge_supported ? "Unavailable" :
                  (view->cartridge_attached ? view->content_name :
                   (view->drive8_supported ? "Load CRT..." : "Load GB/GBC...")));
        draw_item(frame, 9U, menu->row == 2U, "Program",
                  view != NULL && view->program_supported ?
                  "Load PRG..." : "Unavailable");
        break;
    case MT_MENU_PAGE_VIDEO:
        draw_item(frame, 7U, menu->row == 0U, "Video source",
                  view != NULL && view->video_test_enabled ?
                  "Multitini standby" : "Active core");
        {
            const size_t core = active_core_index(view);
            const char *timing = "Unavailable";
            if (core < bounded_core_count(view) &&
                menu->selected_variant[core] <
                    view->cores[core].package.variant_count) {
                timing = view->cores[core].package
                    .variants[menu->selected_variant[core]].label;
            }
            draw_item(frame, 8U, menu->row == 1U, "Core timing", timing);
        }
        break;
    case MT_MENU_PAGE_INPUT:
        draw_item(frame, 7U, menu->row == 0U, "Keyboard",
                  view != NULL && view->c64_input_settings ?
                  (view->symbolic_keyboard ?
                   "US symbolic" : "MiSTer positional") :
                  "Arrows + Z/X + Enter/Shift");
        if (view != NULL && view->c64_input_settings)
            (void)snprintf(value, sizeof(value), "C64 port %u",
                           (unsigned)view->joystick_port);
        else
            (void)snprintf(value, sizeof(value), "Game Boy controls");
        draw_item(frame, 8U, menu->row == 1U, "Gamepad", value);
        break;
    case MT_MENU_PAGE_SYSTEM:
        draw_item(frame, 7U, menu->row == 0U, "Restart active core", NULL);
        draw_item(frame, 8U, menu->row == 1U, "Return to golden updater", NULL);
        break;
    default:
        break;
    }
}

static void render_browser(const mt_menu_t *menu, const mt_menu_view_t *view,
                           mt_ui_frame_t *frame)
{
    const char *title = menu->browser == MT_MENU_BROWSER_DRIVE8 ?
                        "Choose disk image" :
                        (menu->browser == MT_MENU_BROWSER_CARTRIDGE ?
                         "Choose cartridge" : "Choose program");
    size_t index;
    const size_t first = menu->row >= 21U ? menu->row - 20U : 0U;
    mt_ui_text(frame, 26U, 5U, title, MT_UI_COLOR_BLUE,
               MT_UI_COLOR_PANEL, false);
    if (view == NULL || view->files == NULL || view->file_count == 0U) {
        mt_ui_text(frame, 26U, 8U, "No matching files in this core's folder",
                   MT_UI_COLOR_WARNING, MT_UI_COLOR_PANEL, false);
        return;
    }
    for (index = first; index < view->file_count && index < first + 21U;
         ++index) {
        char size_text[24];
        (void)snprintf(size_text, sizeof(size_text), "%u bytes",
                       (unsigned)view->files[index].size);
        draw_item(frame, 7U + index - first, menu->row == index,
                  view->files[index].name, size_text);
    }
}

void mt_menu_render(const mt_menu_t *menu, const mt_menu_view_t *view,
                    mt_ui_frame_t *frame)
{
    size_t page;
    const char *active_name = "No core";
    if (menu == NULL || frame == NULL) return;
    mt_ui_clear(frame, MT_UI_COLOR_BACKGROUND);
    mt_ui_fill(frame, 1U, 1U, 78U, 3U, MT_UI_COLOR_RAISED);
    mt_ui_text(frame, 3U, 2U, "MULTITINI ONE", MT_UI_COLOR_ACCENT,
               MT_UI_COLOR_RAISED, false);
    if (view != NULL && view->active_core_id != NULL)
        active_name = view->active_core_id;
    mt_ui_text(frame, 52U, 2U, "ACTIVE:", MT_UI_COLOR_MUTED,
               MT_UI_COLOR_RAISED, false);
    mt_ui_text(frame, 60U, 2U, active_name, MT_UI_COLOR_TEXT,
               MT_UI_COLOR_RAISED, false);

    mt_ui_fill(frame, 1U, 5U, 21U, 26U, MT_UI_COLOR_PANEL);
    mt_ui_fill(frame, 23U, 5U, 56U, 26U, MT_UI_COLOR_PANEL);
    for (page = 0U; page < MT_MENU_PAGE_COUNT; ++page) {
        const size_t row = 7U + page * 3U;
        const bool selected = menu->page == (mt_menu_page_t)page;
        if (selected) mt_ui_fill(frame, 3U, row, 17U, 1U,
                                 MT_UI_COLOR_ACTIVE);
        mt_ui_text(frame, 5U, row, mt_menu_page_name((mt_menu_page_t)page),
                   selected ? MT_UI_COLOR_ACCENT : MT_UI_COLOR_MUTED,
                   selected ? MT_UI_COLOR_ACTIVE : MT_UI_COLOR_PANEL, false);
    }
    if (menu->browser == MT_MENU_BROWSER_NONE) {
        mt_ui_text(frame, 26U, 5U, mt_menu_page_name(menu->page),
                   MT_UI_COLOR_BLUE, MT_UI_COLOR_PANEL, false);
        render_page(menu, view, frame);
    } else {
        render_browser(menu, view, frame);
    }

    mt_ui_fill(frame, 1U, 32U, 78U, 3U, MT_UI_COLOR_RAISED);
    mt_ui_text(frame, 3U, 33U,
               menu->browser == MT_MENU_BROWSER_NONE ?
               "F12 close   Tab section   Up/Down select   Left/Right change   Enter apply" :
               "Escape back   Up/Down select   Enter load",
               MT_UI_COLOR_MUTED, MT_UI_COLOR_RAISED, false);
    if (view != NULL && view->status_text != NULL)
        mt_ui_text(frame, 3U, 34U, view->status_text, MT_UI_COLOR_SUCCESS,
                   MT_UI_COLOR_RAISED, false);
}
