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

typedef enum {
    MT_UI_CONTROL_ENABLED = 0,
    MT_UI_CONTROL_DISABLED,
    MT_UI_CONTROL_LOCKED
} mt_ui_control_state_t;

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
    MT_UI_PRESENT_BUSY,
    MT_UI_PRESENT_UNSUPPORTED_LAYOUT
} mt_ui_present_result_t;

/* Keep one presenter per overlay bus, preferably in static storage. The
 * presenter must be the only writer of that overlay's VRAM and commits. */
typedef struct {
    mt_ui_frame_t banks[2];
    bool bank_valid[2];
    bool pending;
    uint32_t pending_status;
} mt_ui_presenter_t;

uint16_t mt_ui_cell(char character, mt_ui_color_t foreground,
                    mt_ui_color_t background, bool transparent_background);
void mt_ui_clear(mt_ui_frame_t *frame, mt_ui_color_t background);
void mt_ui_fill(mt_ui_frame_t *frame, size_t column, size_t row,
                size_t width, size_t height, mt_ui_color_t background);
void mt_ui_text(mt_ui_frame_t *frame, size_t column, size_t row,
                const char *text, mt_ui_color_t foreground,
                mt_ui_color_t background, bool transparent_background);
/* Fill exactly width cells (clipped to the row), with an ellipsis when text
 * does not fit. Short text may be right aligned. A NULL text leaves blanks. */
void mt_ui_text_field(mt_ui_frame_t *frame, size_t column, size_t row,
                      size_t width, const char *text,
                      mt_ui_color_t foreground, mt_ui_color_t background,
                      bool transparent_background, bool right_aligned);
/* ASCII controls use opaque backgrounds and mute unavailable controls.
 * Draw a separate control hint when the reason must remain visible. */
void mt_ui_checkbox(mt_ui_frame_t *frame, size_t column, size_t row,
                    bool checked, mt_ui_control_state_t state,
                    mt_ui_color_t background);
/* Width includes both brackets. Values clamp to the inclusive range; an
 * empty or reversed range places the thumb at the start. Widths of one and
 * two cells draw a thumb and brackets respectively. The caller supplies a
 * numeric value beside the slider. Clipping uses the visible width. */
void mt_ui_slider(mt_ui_frame_t *frame, size_t column, size_t row,
                  size_t width, uint32_t value, uint32_t minimum,
                  uint32_t maximum, mt_ui_control_state_t state,
                  mt_ui_color_t background);
/* Clears the field for enabled controls; otherwise shows Disabled or Locked. */
void mt_ui_control_hint(mt_ui_frame_t *frame, size_t column, size_t row,
                        size_t width, mt_ui_control_state_t state,
                        mt_ui_color_t background);
char mt_ui_character(const mt_ui_frame_t *frame, size_t column, size_t row);
void mt_ui_presenter_init(mt_ui_presenter_t *presenter);
/* Call after every overlay/PL reset or reconfiguration, or external VRAM
 * access. Invalidation requires any previous commit to be drained or reset. */
void mt_ui_presenter_invalidate(mt_ui_presenter_t *presenter);
/* Upload changed words to the inactive bank and commit at vertical blank.
 * An unchanged active frame needs no writes; visibility alone needs only a
 * commit. BUSY performs no writes and the caller must retry the latest frame.
 * OK means either unchanged or submitted, not necessarily visible yet. */
mt_ui_present_result_t mt_ui_present_cached(mt_ui_presenter_t *presenter,
                                            const mt_ui_bus_t *bus,
                                            const mt_ui_frame_t *frame,
                                            bool visible);
mt_ui_present_result_t mt_ui_present(const mt_ui_bus_t *bus,
                                     const mt_ui_frame_t *frame,
                                     bool visible);

#endif
