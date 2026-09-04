/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ui.h"

uint16_t mt_ui_cell(char character, mt_ui_color_t foreground,
                    mt_ui_color_t background, bool transparent_background)
{
    const uint16_t ascii =
        ((unsigned char)character >= 32U && (unsigned char)character <= 127U) ?
        (uint16_t)(unsigned char)character : (uint16_t)'?';
    return (uint16_t)(ascii | ((uint16_t)foreground << 7U) |
                      ((uint16_t)background << 11U) |
                      (transparent_background ? UINT16_C(0x8000) :
                                                UINT16_C(0)));
}

void mt_ui_clear(mt_ui_frame_t *frame, mt_ui_color_t background)
{
    size_t index;
    if (frame == NULL) return;
    for (index = 0U; index < MT_UI_COLUMNS * MT_UI_ROWS; ++index)
        frame->cells[index] = mt_ui_cell(' ', MT_UI_COLOR_TEXT, background,
                                         false);
}

void mt_ui_fill(mt_ui_frame_t *frame, size_t column, size_t row,
                size_t width, size_t height, mt_ui_color_t background)
{
    size_t y;
    size_t x;
    if (frame == NULL || column >= MT_UI_COLUMNS || row >= MT_UI_ROWS) return;
    if (width > MT_UI_COLUMNS - column) width = MT_UI_COLUMNS - column;
    if (height > MT_UI_ROWS - row) height = MT_UI_ROWS - row;
    for (y = 0U; y < height; ++y) {
        for (x = 0U; x < width; ++x) {
            frame->cells[(row + y) * MT_UI_COLUMNS + column + x] =
                mt_ui_cell(' ', MT_UI_COLOR_TEXT, background, false);
        }
    }
}

void mt_ui_text(mt_ui_frame_t *frame, size_t column, size_t row,
                const char *text, mt_ui_color_t foreground,
                mt_ui_color_t background, bool transparent_background)
{
    if (frame == NULL || text == NULL || row >= MT_UI_ROWS) return;
    while (*text != '\0' && column < MT_UI_COLUMNS) {
        frame->cells[row * MT_UI_COLUMNS + column] = mt_ui_cell(
            *text, foreground, background, transparent_background);
        ++text;
        ++column;
    }
}

char mt_ui_character(const mt_ui_frame_t *frame, size_t column, size_t row)
{
    if (frame == NULL || column >= MT_UI_COLUMNS || row >= MT_UI_ROWS)
        return '\0';
    return (char)(frame->cells[row * MT_UI_COLUMNS + column] & UINT16_C(0x7F));
}

mt_ui_present_result_t mt_ui_present(const mt_ui_bus_t *bus,
                                     const mt_ui_frame_t *frame,
                                     bool visible)
{
    uint32_t status;
    uint32_t bank_offset;
    uint32_t commit;
    size_t cell;

    if (bus == NULL || frame == NULL || bus->read32 == NULL ||
        bus->write32 == NULL) {
        return MT_UI_PRESENT_INVALID_ARGUMENT;
    }
    status = bus->read32(bus->context, MT_REG_UI_STATUS);
    if ((status & MT_UI_STATUS_READY) == 0U) return MT_UI_PRESENT_BUSY;

    bank_offset = (status & MT_UI_STATUS_BANK) != 0U ?
                  MT_UI_VRAM_BANK0_OFFSET : MT_UI_VRAM_BANK1_OFFSET;
    for (cell = 0U; cell < MT_UI_COLUMNS * MT_UI_ROWS; cell += 2U) {
        const uint32_t word = (uint32_t)frame->cells[cell] |
                              ((uint32_t)frame->cells[cell + 1U] << 16U);
        bus->write32(bus->context, bank_offset + (uint32_t)(cell * 2U), word);
    }
    commit = visible ? MT_UI_COMMIT_VISIBLE : 0U;
    if (bank_offset == MT_UI_VRAM_BANK1_OFFSET) commit |= MT_UI_COMMIT_BANK;
    bus->write32(bus->context, MT_REG_UI_COMMIT, commit);
    return MT_UI_PRESENT_OK;
}
