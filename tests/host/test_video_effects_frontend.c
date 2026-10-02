/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_effects_frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void test_descriptors(void)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_frontend_t frontend;
    mt_menu_view_t view = {0};
    mt_video_effects_defaults(&settings);
    mt_video_effects_frontend_apply(&frontend, &settings, true, NULL, &view);
    check(view.effect_items == frontend.items &&
          view.effect_item_count == MT_VIDEO_EFFECT_COUNT, "all effect controls are exposed");
    for (size_t i = 0U; i < MT_VIDEO_EFFECT_COUNT; ++i) {
        const mt_menu_item_t *item = &frontend.items[i];
        check(item->enabled && item->action == MT_MENU_ACTION_SET_VIDEO_EFFECT &&
              item->argument == i && item->label != NULL && item->help != NULL,
              "every effect descriptor dispatches its stable field ID");
        check(item->value == frontend.values[i] && strlen(item->value) < 32U,
              "value strings remain within frontend-owned storage");
    }
    check(frontend.items[MT_VIDEO_EFFECT_ENABLED].control == MT_MENU_CONTROL_CHECKBOX &&
          frontend.items[MT_VIDEO_EFFECT_ENABLED].control_value == 0U,
          "master defaults to unchecked");
    check(strcmp(frontend.values[MT_VIDEO_EFFECT_TINT], "Color") == 0 &&
          strcmp(frontend.values[MT_VIDEO_EFFECT_SOFTNESS], "Off") == 0,
          "neutral named choices are clear");
    settings.brightness = -100;
    settings.hue = 180;
    settings.softness = MT_VIDEO_SMOOTH_STRONG;
    mt_video_effects_frontend_apply(&frontend, &settings, true, NULL, &view);
    check(frontend.items[MT_VIDEO_EFFECT_BRIGHTNESS].control_value == 0U &&
          frontend.items[MT_VIDEO_EFFECT_HUE].control_value == 360U &&
          strcmp(frontend.values[MT_VIDEO_EFFECT_SOFTNESS], "Strong") == 0,
          "signed sliders and horizontal mode render correctly");
    check(strcmp(frontend.items[MT_VIDEO_EFFECT_SOFTNESS].label,"Horizontal smoothing")==0 &&
          strcmp(frontend.items[MT_VIDEO_EFFECT_BLUR].label,"Vertical smoothing")==0 &&
          strstr(frontend.items[MT_VIDEO_EFFECT_SOFTNESS].help,"Independent")!=NULL &&
          strstr(frontend.items[MT_VIDEO_EFFECT_BLUR].help,"output resolutions")!=NULL,
          "axis controls explain their independence and resolution consistency");
    static const char *const levels[]={"Off","Light","Medium","Strong"};
    for(unsigned level=0U;level<4U;++level) {
        settings.softness=settings.blur=(uint8_t)level;
        mt_video_effects_frontend_apply(&frontend,&settings,true,NULL,&view);
        check(strcmp(frontend.values[MT_VIDEO_EFFECT_SOFTNESS],levels[level])==0 &&
              strcmp(frontend.values[MT_VIDEO_EFFECT_BLUR],levels[level])==0,
              "both axes use the same four ordered smoothing levels");
    }
    mt_video_effects_frontend_apply(&frontend, &settings, false, "CPU1 unavailable", &view);
    for (size_t i = 0; i < MT_VIDEO_EFFECT_COUNT; ++i)
        check(!frontend.items[i].enabled &&
              strcmp(frontend.items[i].help, "CPU1 unavailable") == 0,
              "unsupported backend disables controls with the supplied reason");
}

static void test_invalid_settings(void)
{
    mt_video_effects_settings_t settings;
    mt_video_effects_frontend_t frontend;
    mt_menu_view_t view = {0};
    memset(&settings, 0xFF, sizeof(settings));
    mt_video_effects_frontend_apply(&frontend, &settings, true, NULL, &view);
    check(strcmp(frontend.values[MT_VIDEO_EFFECT_TINT], "Color") == 0 &&
          strcmp(frontend.values[MT_VIDEO_EFFECT_MASK], "Off") == 0 &&
          strcmp(frontend.values[MT_VIDEO_EFFECT_GAMMA], "100") == 0,
          "malformed enum and numeric values render neutral fallback safely");
    check(settings.tint == 0xFFU, "rendering fallback does not mutate persistent settings");
    check(!mt_video_effects_adjust(&settings, MT_VIDEO_EFFECT_TINT, 0U),
          "adjustment rejects malformed settings until restored");
    mt_video_effects_frontend_apply(&frontend, NULL, false, NULL, &view);
    check(view.effect_item_count == MT_VIDEO_EFFECT_COUNT &&
          frontend.items[0].help != NULL && !frontend.items[0].enabled,
          "missing settings render safe disabled defaults");
}

static void test_adjustments(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_defaults(&s);
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_ENABLED, 0U) && s.enabled == 1U,
          "Enter toggles checkbox");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_ENABLED, 1U) && s.enabled == 0U,
          "Left clears checkbox");
    check(!mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_ENABLED, 1U),
          "Left on cleared checkbox does no work");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_ENABLED, 2U) && s.enabled == 1U,
          "Right sets checkbox");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_BRIGHTNESS, 1U) && s.brightness == -5,
          "Left decrements numeric setting by its readable step");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_BRIGHTNESS, 2U) && s.brightness == 0,
          "Right increments numeric setting");
    s.brightness = 100;
    check(!mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_BRIGHTNESS, 2U) && s.brightness == 100,
          "Right clamps at maximum");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_BRIGHTNESS, 0U) && s.brightness == -100,
          "Enter wraps numeric setting at maximum");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_POSTERIZE, 2U) && s.posterize == 2U,
          "posterization skips unsupported one-bit value going right");
    check(mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_POSTERIZE, 1U) && s.posterize == 0U,
          "posterization returns directly from two bits to Off");
    check(!mt_video_effects_adjust(&s, MT_VIDEO_EFFECT_COUNT, 0U) &&
          !mt_video_effects_adjust(&s, 0U, 3U) &&
          !mt_video_effects_adjust(NULL, 0U, 0U), "invalid adjustments do no work");
}

static void test_menu_dispatch(void)
{
    mt_video_effects_settings_t s;
    mt_video_effects_frontend_t frontend;
    mt_menu_view_t view = {0};
    mt_menu_t menu;
    mt_menu_action_t action;
    mt_video_effects_defaults(&s);
    mt_video_effects_frontend_apply(&frontend, &s, true, NULL, &view);
    mt_menu_init(&menu, &view);
    menu.visible = true;
    menu.page = MT_MENU_PAGE_VIDEO;
    (void)mt_menu_key(&menu, &view, MT_MENU_HID_TAB, true);
    check(menu.page == MT_MENU_PAGE_EFFECTS, "Effects is reachable after Video");
    menu.row = MT_VIDEO_EFFECT_HUE;
    action = mt_menu_key(&menu, &view, MT_MENU_HID_LEFT, true);
    check(action.kind == MT_MENU_ACTION_SET_VIDEO_EFFECT &&
          action.file_index == MT_VIDEO_EFFECT_HUE && action.variant_index == 1U,
          "Left dispatches selected effect ID and decrement direction");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_RIGHT, true);
    check(action.kind == MT_MENU_ACTION_SET_VIDEO_EFFECT &&
          action.file_index == MT_VIDEO_EFFECT_HUE && action.variant_index == 2U,
          "Right dispatches increment direction");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true);
    check(action.kind == MT_MENU_ACTION_SET_VIDEO_EFFECT &&
          action.file_index == MT_VIDEO_EFFECT_HUE && action.variant_index == 0U,
          "Enter dispatches cycle direction");
    (void)mt_menu_key(&menu, &view, MT_MENU_HID_END, true);
    check(menu.row == MT_VIDEO_EFFECT_BLOOM, "End reaches the frame effects after the first sixteen rows");
    action = mt_menu_key(&menu, &view, MT_MENU_HID_RIGHT, true);
    check(action.kind == MT_MENU_ACTION_SET_VIDEO_EFFECT && action.file_index == MT_VIDEO_EFFECT_BLOOM,
          "Bloom remains adjustable after scrolling");
    mt_video_effects_frontend_apply(&frontend, &s, false, NULL, &view);
    check(mt_menu_key(&menu, &view, MT_MENU_HID_RIGHT, true).kind == MT_MENU_ACTION_NONE &&
          mt_menu_key(&menu, &view, MT_MENU_HID_ENTER, true).kind == MT_MENU_ACTION_NONE,
          "unavailable effect controls cannot dispatch mutations");
}

int main(void)
{
    test_descriptors();
    test_invalid_settings();
    test_adjustments();
    test_menu_dispatch();
    puts("video effects frontend: descriptor, bounds and menu checks passed");
    return 0;
}
