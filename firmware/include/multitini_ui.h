/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_UI_H
#define MULTITINI_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "multitini_regs.h"

typedef enum {
    MT_UI_COLOR_BACKGROUND = 0,
    MT_UI_COLOR_PANEL,
    MT_UI_COLOR_RAISED,
    MT_UI_COLOR_ROW,
    MT_UI_COLOR_ACTIVE,
    MT_UI_COLOR_TEXT,
    MT_UI_COLOR_MUTED,
    MT_UI_COLOR_ACCENT,
    MT_UI_COLOR_BLUE,
    MT_UI_COLOR_WARNING,
    MT_UI_COLOR_SUCCESS,
    MT_UI_COLOR_BORDER,
    MT_UI_COLOR_DIM,
    MT_UI_COLOR_RED,
    MT_UI_COLOR_GREEN,
    MT_UI_COLOR_WHITE
} mt_ui_color_t;

typedef struct {
    uint16_t cells[MT_UI_COLUMNS * MT_UI_ROWS];
} mt_ui_frame_t;

typedef struct {
    void *context;
    uint32_t (*read32)(void *context, uint32_t offset);
    void (*write32)(void *context, uint32_t offset, uint32_t value);
} mt_ui_bus_t;

typedef enum {
    MT_UI_PRESENT_OK = 0,
    MT_UI_PRESENT_INVALID_ARGUMENT,
    MT_UI_PRESENT_BUSY
} mt_ui_present_result_t;

uint16_t mt_ui_cell(char character, mt_ui_color_t foreground,
                    mt_ui_color_t background, bool transparent_background);
void mt_ui_clear(mt_ui_frame_t *frame, mt_ui_color_t background);
void mt_ui_fill(mt_ui_frame_t *frame, size_t column, size_t row,
                size_t width, size_t height, mt_ui_color_t background);
void mt_ui_text(mt_ui_frame_t *frame, size_t column, size_t row,
                const char *text, mt_ui_color_t foreground,
                mt_ui_color_t background, bool transparent_background);
char mt_ui_character(const mt_ui_frame_t *frame, size_t column, size_t row);
mt_ui_present_result_t mt_ui_present(const mt_ui_bus_t *bus,
                                     const mt_ui_frame_t *frame,
                                     bool visible);

#endif
