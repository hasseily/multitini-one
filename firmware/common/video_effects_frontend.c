/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_effects_frontend.h"
#include <stdio.h>
#include <string.h>

bool mt_video_effects_adjust(mt_video_effects_settings_t *settings,
    size_t field, size_t direction)
{
    int low, high, step, next, current;
    mt_video_effect_id_t id;
    if (!mt_video_effects_settings_valid(settings) ||
        field >= MT_VIDEO_EFFECT_COUNT || direction > 2U) return false;
    id = (mt_video_effect_id_t)field;
    if (!mt_video_effects_range(id, &low, &high, &step)) return false;
    current = mt_video_effects_get(settings, id);
    next = current + (direction == 1U ? -step : step);
    if (id == MT_VIDEO_EFFECT_ENABLED || id == MT_VIDEO_EFFECT_NEGATIVE)
        next = direction == 0U ? !current : direction == 2U;
    else if (next > high) next = direction == 0U ? low : high;
    else if (next < low) next = low;
    if (id == MT_VIDEO_EFFECT_POSTERIZE && next == 1)
        next = direction == 1U ? 0 : 2;
    return next != current && mt_video_effects_set(settings, id, next);
}

void mt_video_effects_frontend_apply(mt_video_effects_frontend_t *frontend,
    const mt_video_effects_settings_t *settings, bool supported,
    const char *unavailable_reason, mt_menu_view_t *view)
{
    static const char *const labels[MT_VIDEO_EFFECT_COUNT] = {
        "Enable effects", "Brightness", "Contrast", "Gamma", "Saturation", "Hue",
        "Color temperature", "Red gain", "Green gain", "Blue gain", "Display tint",
        "Negative", "Posterize", "Scanlines", "Pixel mask", "Dot bleed",
        "Persistence", "Blur", "Glow"
    };
    static const char *const help[MT_VIDEO_EFFECT_COUNT] = {
        "Apply saved effects to the game image. Menus and decorative borders keep their original colors.",
        "Lift or lower brightness. Zero preserves the original level.",
        "Expand or compress contrast around the middle level. 100 is neutral.",
        "Adjust midtone brightness while preserving black and white. 100 is neutral.",
        "Control color intensity. Zero removes color; 100 preserves it.",
        "Rotate color hue in degrees. Zero preserves the original palette.",
        "Negative values cool the image; positive values warm it. Zero is neutral.",
        "Scale red intensity. 100 is neutral.", "Scale green intensity. 100 is neutral.",
        "Scale blue intensity. 100 is neutral.",
        "Choose full color, monochrome white, green, amber, or sepia.",
        "Invert the resulting color levels.",
        "Reduce the number of levels per channel for a deliberately stepped palette.",
        "Darken alternating source-row bands after scaling, keeping the menu readable.",
        "Simulate aperture grille, staggered shadow mask, or LCD pixel grid at output resolution.",
        "Spread dots sideways with Appletini spot weights. Light keeps dots sharp; Medium brightens gap edges; Strong adds a faint gap-center contribution.",
        "Keep a fading phosphor trail behind moving pixels. Off preserves original motion.",
        "Gentle phosphor blur: Light adds slight horizontal softening; Medium adds vertical softening; Strong widens the horizontal spot.",
        "Add a soft light halo around bright pixels. Independent of Blur and persistence."
    };
    static const char *const tints[] = {"Color", "White", "Green", "Amber", "Sepia"};
    static const char *const masks[] = {"Off", "Aperture grille", "Shadow mask", "LCD grid"};
    static const char *const scans[] = {"Off", "Light", "Medium", "Strong"};
    mt_video_effects_settings_t defaults;
    size_t i;
    if (frontend == NULL || view == NULL) return;
    if (!mt_video_effects_settings_valid(settings)) {
        mt_video_effects_defaults(&defaults);
        settings = &defaults;
    }
    if (unavailable_reason == NULL) unavailable_reason = "Video effects are unavailable in this core image.";
    memset(frontend, 0, sizeof(*frontend));
    for (i = 0U; i < MT_VIDEO_EFFECT_COUNT; ++i) {
        mt_menu_item_t *const item = &frontend->items[i];
        const mt_video_effect_id_t id = (mt_video_effect_id_t)i;
        const int value = mt_video_effects_get(settings, id);
        int low = 0, high = 0, step = 1;
        (void)mt_video_effects_range(id, &low, &high, &step);
        item->label = labels[i]; item->value = frontend->values[i];
        item->help = supported ? help[i] : unavailable_reason;
        item->enabled = supported;
        item->action = MT_MENU_ACTION_SET_VIDEO_EFFECT; item->argument = i;
        item->value_color = MT_UI_COLOR_MUTED;
        if (id == MT_VIDEO_EFFECT_ENABLED || id == MT_VIDEO_EFFECT_NEGATIVE) {
            item->control = MT_MENU_CONTROL_CHECKBOX;
            item->control_value = value != 0 ? 1U : 0U;
            (void)snprintf(frontend->values[i], sizeof(frontend->values[i]), "%s", value ? "On" : "Off");
        } else {
            item->control = MT_MENU_CONTROL_SLIDER;
            item->control_max = (uint32_t)(high - low);
            item->control_value = (uint32_t)(value - low);
            (void)snprintf(frontend->values[i], sizeof(frontend->values[i]), "%d", value);
            if (id == MT_VIDEO_EFFECT_TINT || id == MT_VIDEO_EFFECT_MASK ||
                id == MT_VIDEO_EFFECT_SCANLINES || id == MT_VIDEO_EFFECT_SOFTNESS ||
                id == MT_VIDEO_EFFECT_PERSISTENCE || id == MT_VIDEO_EFFECT_BLUR ||
                id == MT_VIDEO_EFFECT_BLOOM) {
                const char *name = id == MT_VIDEO_EFFECT_TINT ? tints[value] :
                    id == MT_VIDEO_EFFECT_MASK ? masks[value] : scans[value];
                item->control = MT_MENU_CONTROL_VALUE;
                (void)snprintf(frontend->values[i], sizeof(frontend->values[i]), "%s", name);
            } else if (id == MT_VIDEO_EFFECT_POSTERIZE)
                (void)snprintf(frontend->values[i], sizeof(frontend->values[i]), value == 0 ? "Off" : "%d bits", value);
        }
    }
    view->effect_items = frontend->items;
    view->effect_item_count = MT_VIDEO_EFFECT_COUNT;
}
