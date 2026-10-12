/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SCREENSHOT_H
#define MULTITINI_SCREENSHOT_H
#include "multitini_ui.h"
#include "multitini_video_effects.h"
#include <stddef.h>

#define MT_SCREENSHOT_MAX_WIDTH 1920U
typedef bool (*mt_screenshot_write_fn)(void *context, const void *bytes, size_t size);
typedef struct {
    mt_screenshot_write_fn write;
    void *context;
    uint32_t width, height, row, adler_a, adler_b;
    bool failed;
    uint8_t chunk[MT_SCREENSHOT_MAX_WIDTH * 3U + 32U];
} mt_screenshot_png_t;
/* Streaming RGB PNG with lossless stored DEFLATE blocks; one bounded row
 * write per call, no framebuffer-sized encoder allocations or dependencies. */
bool mt_screenshot_png_begin(mt_screenshot_png_t *png, uint32_t width,
    uint32_t height, mt_screenshot_write_fn write, void *context);
bool mt_screenshot_png_row(mt_screenshot_png_t *png, const uint16_t *rgb565);
bool mt_screenshot_png_end(mt_screenshot_png_t *png);

typedef struct {
    /* Packed processed-frame snapshot, width * height. Temporal effects are
     * already present; the renderer applies display scaling and spatial/color
     * effects. Cropped video uses the visible viewport rectangle and excludes UI. */
    const uint16_t *pixels;
    uint32_t width, height, core_id, border_config;
    uint8_t display_mode;
    uint8_t video_multiplier; /* 0 = Auto; 1..4 = centered integer scale, clipped. */
    bool full_output, ui_visible; /* Full output also includes letterbox/UI/borders. */
    const mt_ui_frame_t *ui;
    const mt_video_effects_tables_t *effects;
} mt_screenshot_scene_t;
bool mt_screenshot_dimensions(const mt_screenshot_scene_t *scene,
                              uint32_t *width, uint32_t *height);
bool mt_screenshot_render_row(const mt_screenshot_scene_t *scene,
                              uint32_t y, uint16_t *row);
#endif
