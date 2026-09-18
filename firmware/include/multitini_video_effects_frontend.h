/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_EFFECTS_FRONTEND_H
#define MULTITINI_VIDEO_EFFECTS_FRONTEND_H
#include "multitini_menu.h"
#include "multitini_video_effects.h"
typedef struct {
    mt_menu_item_t items[MT_VIDEO_EFFECT_COUNT];
    char values[MT_VIDEO_EFFECT_COUNT][32];
} mt_video_effects_frontend_t;
void mt_video_effects_frontend_apply(mt_video_effects_frontend_t *frontend,
    const mt_video_effects_settings_t *settings, bool supported,
    const char *unavailable_reason, mt_menu_view_t *view);
bool mt_video_effects_adjust(mt_video_effects_settings_t *settings,
    size_t field, size_t direction);
#endif
