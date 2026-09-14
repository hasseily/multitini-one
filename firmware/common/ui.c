/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ui.h"

#include <string.h>

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

void mt_ui_text_field(mt_ui_frame_t *frame, size_t column, size_t row,
                      size_t width, const char *text,
                      mt_ui_color_t foreground, mt_ui_color_t background,
                      bool transparent_background, bool right_aligned)
{
    size_t length = 0U;
    size_t index;
    size_t start = 0U;
    size_t dots = 0U;

    if (frame == NULL || column >= MT_UI_COLUMNS || row >= MT_UI_ROWS)
        return;
    if (width > MT_UI_COLUMNS - column) width = MT_UI_COLUMNS - column;
    if (width == 0U) return;
    if (text != NULL) {
        while (length <= width && text[length] != '\0') ++length;
    }
    if (length > width) {
        dots = width < 3U ? width : 3U;
        length = width - dots;
    } else if (right_aligned) {
        start = width - length;
    }
    for (index = 0U; index < width; ++index) {
        char character = ' ';
        if (index >= start && index - start < length)
            character = text[index - start];
        else if (dots != 0U && index >= width - dots)
            character = '.';
        frame->cells[row * MT_UI_COLUMNS + column + index] = mt_ui_cell(
            character, foreground, background, transparent_background);
    }
}

char mt_ui_character(const mt_ui_frame_t *frame, size_t column, size_t row)
{
    if (frame == NULL || column >= MT_UI_COLUMNS || row >= MT_UI_ROWS)
        return '\0';
    return (char)(frame->cells[row * MT_UI_COLUMNS + column] & UINT16_C(0x7F));
}

void mt_ui_presenter_init(mt_ui_presenter_t *presenter)
{
    mt_ui_presenter_invalidate(presenter);
}

void mt_ui_presenter_invalidate(mt_ui_presenter_t *presenter)
{
    if (presenter == NULL) return;
    presenter->bank_valid[0] = false;
    presenter->bank_valid[1] = false;
    presenter->pending = false;
    presenter->pending_status = 0U;
}

mt_ui_present_result_t mt_ui_present_cached(mt_ui_presenter_t *presenter,
                                            const mt_ui_bus_t *bus,
                                            const mt_ui_frame_t *frame,
                                            bool visible)
{
    const uint32_t state_mask = MT_UI_STATUS_BANK | MT_UI_STATUS_VISIBLE |
                                MT_UI_STATUS_DENSE;
    uint32_t status;
    uint32_t commit;
    size_t active_bank;
    size_t target_bank;
    size_t cell;
    bool same_frame;

    if (presenter == NULL || bus == NULL || frame == NULL ||
        bus->read32 == NULL || bus->write32 == NULL) {
        return MT_UI_PRESENT_INVALID_ARGUMENT;
    }
    status = bus->read32(bus->context, MT_REG_UI_STATUS);
    if ((status & MT_UI_STATUS_DENSE_LAYOUT) == 0U)
        return MT_UI_PRESENT_UNSUPPORTED_LAYOUT;
    if ((status & MT_UI_STATUS_READY) == 0U) return MT_UI_PRESENT_BUSY;
    /* READY can still describe the old state immediately after a commit.
     * Wait for both the mailbox and the separately synchronized active state
     * before choosing an inactive bank. Every submitted commit changes at
     * least one state bit, so the old state cannot acknowledge a new commit. */
    if (presenter->pending) {
        if ((status & state_mask) != presenter->pending_status)
            return MT_UI_PRESENT_BUSY;
        presenter->pending = false;
    }

    active_bank = (status & MT_UI_STATUS_BANK) != 0U ? 1U : 0U;
    same_frame = presenter->bank_valid[active_bank] &&
                 memcmp(&presenter->banks[active_bank], frame,
                        sizeof(*frame)) == 0;
    if (same_frame && (status & MT_UI_STATUS_DENSE) != 0U &&
        ((status & MT_UI_STATUS_VISIBLE) != 0U) == visible)
        return MT_UI_PRESENT_OK;

    target_bank = same_frame ? active_bank : 1U - active_bank;
    if (!same_frame) {
        const uint32_t bank_offset = target_bank != 0U ?
            MT_UI_VRAM_BANK1_OFFSET : MT_UI_VRAM_BANK0_OFFSET;
        mt_ui_frame_t *const cached = &presenter->banks[target_bank];
        for (cell = 0U; cell < MT_UI_COLUMNS * MT_UI_ROWS; cell += 2U) {
            if (!presenter->bank_valid[target_bank] ||
                cached->cells[cell] != frame->cells[cell] ||
                cached->cells[cell + 1U] != frame->cells[cell + 1U]) {
                const uint32_t word = (uint32_t)frame->cells[cell] |
                    ((uint32_t)frame->cells[cell + 1U] << 16U);
                bus->write32(bus->context,
                             bank_offset + (uint32_t)(cell * 2U), word);
            }
        }
        *cached = *frame;
        presenter->bank_valid[target_bank] = true;
    }
    commit = MT_UI_COMMIT_DENSE | (visible ? MT_UI_COMMIT_VISIBLE : 0U);
    if (target_bank != 0U) commit |= MT_UI_COMMIT_BANK;
    presenter->pending_status = MT_UI_STATUS_DENSE |
                                 (visible ? MT_UI_STATUS_VISIBLE : 0U);
    if (target_bank != 0U) presenter->pending_status |= MT_UI_STATUS_BANK;
    presenter->pending = true;
    bus->write32(bus->context, MT_REG_UI_COMMIT, commit);
    return MT_UI_PRESENT_OK;
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
    if ((status & MT_UI_STATUS_DENSE_LAYOUT) == 0U)
        return MT_UI_PRESENT_UNSUPPORTED_LAYOUT;
    if ((status & MT_UI_STATUS_READY) == 0U) return MT_UI_PRESENT_BUSY;

    bank_offset = (status & MT_UI_STATUS_BANK) != 0U ?
                  MT_UI_VRAM_BANK0_OFFSET : MT_UI_VRAM_BANK1_OFFSET;
    for (cell = 0U; cell < MT_UI_COLUMNS * MT_UI_ROWS; cell += 2U) {
        const uint32_t word = (uint32_t)frame->cells[cell] |
                              ((uint32_t)frame->cells[cell + 1U] << 16U);
        bus->write32(bus->context, bank_offset + (uint32_t)(cell * 2U), word);
    }
    commit = MT_UI_COMMIT_DENSE | (visible ? MT_UI_COMMIT_VISIBLE : 0U);
    if (bank_offset == MT_UI_VRAM_BANK1_OFFSET) commit |= MT_UI_COMMIT_BANK;
    bus->write32(bus->context, MT_REG_UI_COMMIT, commit);
    return MT_UI_PRESENT_OK;
}
