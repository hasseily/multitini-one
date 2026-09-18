/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t status;
    uint32_t writes;
    uint32_t first_offset;
    uint32_t last_offset;
    uint32_t last_value;
    uint32_t vram_writes;
    uint32_t commits;
    uint32_t pending_commit;
    bool commit_pending;
    uint32_t banks[2][MT_UI_COLUMNS * MT_UI_ROWS / 2U];
} fake_bus_t;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static uint32_t read32(void *context, uint32_t offset)
{
    fake_bus_t *const bus = context;
    check(offset == MT_REG_UI_STATUS, "only UI status is read");
    return bus->status;
}

static void write32(void *context, uint32_t offset, uint32_t value)
{
    fake_bus_t *const bus = context;
    check((bus->status & MT_UI_STATUS_READY) != 0U,
          "writes require a ready mailbox");
    if (bus->writes == 0U) bus->first_offset = offset;
    ++bus->writes;
    bus->last_offset = offset;
    bus->last_value = value;
    if (offset == MT_REG_UI_COMMIT) {
        check(!bus->commit_pending, "commit never overwrites pending commit");
        bus->commit_pending = true;
        bus->pending_commit = value;
        ++bus->commits;
    } else {
        const size_t bank = offset >= MT_UI_VRAM_BANK1_OFFSET ? 1U : 0U;
        const uint32_t base = bank != 0U ? MT_UI_VRAM_BANK1_OFFSET :
                                          MT_UI_VRAM_BANK0_OFFSET;
        check(bank != ((bus->status & MT_UI_STATUS_BANK) != 0U ? 1U : 0U),
              "VRAM writes never touch the active bank");
        check(offset >= base && (offset - base) % 4U == 0U &&
              (offset - base) / 4U < MT_UI_COLUMNS * MT_UI_ROWS / 2U,
              "VRAM word offset is aligned and in range");
        bus->banks[bank][(offset - base) / 4U] = value;
        ++bus->vram_writes;
    }
}

static void reset_counts(fake_bus_t *bus)
{
    bus->writes = 0U;
    bus->vram_writes = 0U;
    bus->commits = 0U;
    bus->first_offset = 0U;
    bus->last_offset = 0U;
    bus->last_value = 0U;
}

static void finish_commit(fake_bus_t *bus, const mt_ui_frame_t *frame)
{
    size_t cell;
    const size_t bank = (bus->pending_commit & MT_UI_COMMIT_BANK) != 0U ?
                        1U : 0U;
    check(bus->commit_pending, "vertical blank requires a pending commit");
    for (cell = 0U; cell < MT_UI_COLUMNS * MT_UI_ROWS; cell += 2U) {
        const uint32_t expected = (uint32_t)frame->cells[cell] |
                                  ((uint32_t)frame->cells[cell + 1U] << 16U);
        check(bus->banks[bank][cell / 2U] == expected,
              "committed bank contains the entire requested frame");
    }
    bus->status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
    if ((bus->pending_commit & MT_UI_COMMIT_DENSE) != 0U)
        bus->status |= MT_UI_STATUS_DENSE;
    if (bank != 0U) bus->status |= MT_UI_STATUS_BANK;
    if ((bus->pending_commit & MT_UI_COMMIT_VISIBLE) != 0U)
        bus->status |= MT_UI_STATUS_VISIBLE;
    bus->commit_pending = false;
    reset_counts(bus);
}

static void test_text(void)
{
    mt_ui_frame_t frame;
    mt_ui_frame_t saved;
    mt_ui_clear(&frame, MT_UI_COLOR_BACKGROUND);
    mt_ui_text(&frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U, "AB", MT_UI_COLOR_TEXT,
               MT_UI_COLOR_PANEL, false);
    check(mt_ui_character(&frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U) == 'A', "text clips at edge");
    check(mt_ui_character(&frame, 0U, 0U) == ' ', "clear fills spaces");
    mt_ui_text_field(&frame, 3U, 2U, 8U, "A long title", MT_UI_COLOR_ACCENT,
                     MT_UI_COLOR_PANEL, false, false);
    check(mt_ui_character(&frame, 3U, 2U) == 'A' &&
          mt_ui_character(&frame, 7U, 2U) == 'n' &&
          mt_ui_character(&frame, 8U, 2U) == '.' &&
          mt_ui_character(&frame, 10U, 2U) == '.' &&
          mt_ui_character(&frame, 11U, 2U) == ' ',
          "long field clips with an ellipsis inside its boundary");
    mt_ui_text_field(&frame, 3U, 2U, 8U, "Hi", MT_UI_COLOR_ACCENT,
                     MT_UI_COLOR_PANEL, true, true);
    check(mt_ui_character(&frame, 3U, 2U) == ' ' &&
          mt_ui_character(&frame, 8U, 2U) == ' ' &&
          mt_ui_character(&frame, 9U, 2U) == 'H' &&
          mt_ui_character(&frame, 10U, 2U) == 'i',
          "short field right aligns and clears previous contents");
    check(frame.cells[2U * MT_UI_COLUMNS + 3U] ==
          mt_ui_cell(' ', MT_UI_COLOR_ACCENT, MT_UI_COLOR_PANEL, true),
          "padding uses the requested colors and transparency");
    mt_ui_text_field(&frame, 3U, 2U, 8U, "12345678", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, false);
    check(mt_ui_character(&frame, 10U, 2U) == '8',
          "exact width text is not truncated");
    mt_ui_text_field(&frame, MT_UI_COLUMNS - 2U, MT_UI_ROWS - 1U, SIZE_MAX, "ABCD", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, false);
    check(mt_ui_character(&frame, MT_UI_COLUMNS - 2U, MT_UI_ROWS - 1U) == '.' &&
          mt_ui_character(&frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U) == '.',
          "oversized width clips safely at the final row boundary");
    mt_ui_text_field(&frame, MT_UI_COLUMNS - 2U, MT_UI_ROWS - 1U, 1U, "AB", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, true);
    check(mt_ui_character(&frame, MT_UI_COLUMNS - 2U, MT_UI_ROWS - 1U) == '.',
          "one column field keeps ellipsis within the field");
    mt_ui_text_field(&frame, 3U, 2U, 8U, NULL, MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, true);
    check(mt_ui_character(&frame, 3U, 2U) == ' ' &&
          mt_ui_character(&frame, 10U, 2U) == ' ', "NULL text clears field");
    saved = frame;
    mt_ui_text_field(&frame, SIZE_MAX, 0U, 3U, "bad", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, false);
    mt_ui_text_field(&frame, 0U, SIZE_MAX, 3U, "bad", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, false);
    mt_ui_text_field(&frame, 0U, 0U, 0U, "bad", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, false);
    mt_ui_text_field(NULL, 0U, 0U, 3U, "bad", MT_UI_COLOR_TEXT,
                     MT_UI_COLOR_PANEL, false, false);
    mt_ui_text(&frame, SIZE_MAX, 0U, "bad", MT_UI_COLOR_TEXT,
               MT_UI_COLOR_PANEL, false);
    check(memcmp(&frame, &saved, sizeof(frame)) == 0,
          "out of bounds and empty fields perform no writes");
}

static void check_text(const mt_ui_frame_t *frame, size_t column, size_t row,
                       const char *text, const char *message)
{
    size_t index;
    for (index = 0U; text[index] != '\0'; ++index)
        check(mt_ui_character(frame, column + index, row) == text[index], message);
}

static void test_controls(void)
{
    struct {
        uint32_t before;
        mt_ui_frame_t frame;
        uint32_t after;
    } guarded = {UINT32_C(0x12345678), {{0}}, UINT32_C(0x87654321)};
    mt_ui_frame_t *const frame = &guarded.frame;
    mt_ui_frame_t saved;
    mt_ui_clear(frame, MT_UI_COLOR_BACKGROUND);
    mt_ui_checkbox(frame, 2U, 3U, true, MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    check_text(frame, 2U, 3U, "[x]", "checked control has visible mark");
    mt_ui_checkbox(frame, 2U, 3U, false, MT_UI_CONTROL_DISABLED, MT_UI_COLOR_ROW);
    check_text(frame, 2U, 3U, "[ ]", "unchecked control erases the mark");
    check(frame->cells[3U * MT_UI_COLUMNS + 3U] ==
          mt_ui_cell(' ', MT_UI_COLOR_MUTED, MT_UI_COLOR_ROW, false),
          "disabled checkbox uses muted opaque row colors");

    mt_ui_slider(frame, 2U, 4U, 7U, 15U, 10U, 20U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ACTIVE);
    check_text(frame, 2U, 4U, "[--|--]", "midpoint slider position");
    mt_ui_slider(frame, 2U, 4U, 7U, 0U, 10U, 20U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ACTIVE);
    check_text(frame, 2U, 4U, "[|----]", "slider clamps below minimum");
    mt_ui_slider(frame, 2U, 4U, 7U, UINT32_MAX, 10U, 20U,
                  MT_UI_CONTROL_LOCKED, MT_UI_COLOR_ACTIVE);
    check_text(frame, 2U, 4U, "[----|]", "slider clamps above maximum");
    check(frame->cells[4U * MT_UI_COLUMNS + 7U] ==
          mt_ui_cell('|', MT_UI_COLOR_MUTED, MT_UI_COLOR_ACTIVE, false),
          "locked slider retains position with muted colors");
    mt_ui_slider(frame, 2U, 4U, 7U, UINT32_MAX / 2U, 0U, UINT32_MAX,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ACTIVE);
    check_text(frame, 2U, 4U, "[--|--]", "full uint32 range scales without overflow");
    mt_ui_slider(frame, 2U, 4U, 7U, 10U, 10U, 10U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ACTIVE);
    check_text(frame, 2U, 4U, "[|----]", "empty range has a stable first position");
    mt_ui_slider(frame, 2U, 4U, 7U, 10U, 20U, 10U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ACTIVE);
    check_text(frame, 2U, 4U, "[|----]", "reversed range does not divide or underflow");
    mt_ui_slider(frame, 2U, 5U, 1U, 10U, 0U, 10U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_slider(frame, 4U, 5U, 2U, 10U, 0U, 10U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    check_text(frame, 2U, 5U, "| [] ", "narrow controls stay inside their bounds");

    mt_ui_control_hint(frame, 2U, 6U, 9U, MT_UI_CONTROL_DISABLED, MT_UI_COLOR_ROW);
    check_text(frame, 2U, 6U, "Disabled ", "disabled hint is explicit and padded");
    mt_ui_control_hint(frame, 2U, 6U, 9U, MT_UI_CONTROL_LOCKED, MT_UI_COLOR_ROW);
    check_text(frame, 2U, 6U, "Locked   ", "locked hint clears the longer old hint");
    mt_ui_control_hint(frame, 2U, 6U, 9U, MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    check_text(frame, 2U, 6U, "         ", "enabled hint clears stale state text");

    mt_ui_slider(frame, MT_UI_COLUMNS - 7U, MT_UI_ROWS - 1U, SIZE_MAX,
                  UINT32_MAX, 0U, UINT32_MAX, MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    check_text(frame, MT_UI_COLUMNS - 7U, MT_UI_ROWS - 1U, "[----|]",
                "SIZE_MAX width clips before range arithmetic");
    mt_ui_checkbox(frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U, true,
                    MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    check(mt_ui_character(frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U) == '[',
          "checkbox clips at the final cell");
    mt_ui_control_hint(frame, MT_UI_COLUMNS - 2U, MT_UI_ROWS - 1U, SIZE_MAX,
                        MT_UI_CONTROL_LOCKED, MT_UI_COLOR_ROW);
    check_text(frame, MT_UI_COLUMNS - 2U, MT_UI_ROWS - 1U, "..",
                "state hint clips without crossing the last row");
    saved = *frame;
    mt_ui_checkbox(frame, SIZE_MAX, 0U, true, MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_checkbox(frame, 0U, SIZE_MAX, true, MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_checkbox(NULL, 0U, 0U, true, MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_slider(frame, SIZE_MAX, 0U, SIZE_MAX, 1U, 0U, 2U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_slider(frame, 0U, SIZE_MAX, SIZE_MAX, 1U, 0U, 2U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_slider(frame, 0U, 0U, 0U, 1U, 0U, 2U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_slider(NULL, 0U, 0U, 7U, 1U, 0U, 2U,
                  MT_UI_CONTROL_ENABLED, MT_UI_COLOR_ROW);
    mt_ui_control_hint(frame, SIZE_MAX, 0U, SIZE_MAX,
                        MT_UI_CONTROL_DISABLED, MT_UI_COLOR_ROW);
    mt_ui_control_hint(frame, 0U, SIZE_MAX, SIZE_MAX,
                        MT_UI_CONTROL_DISABLED, MT_UI_COLOR_ROW);
    mt_ui_control_hint(NULL, 0U, 0U, 7U, MT_UI_CONTROL_DISABLED, MT_UI_COLOR_ROW);
    check(memcmp(frame, &saved, sizeof(saved)) == 0,
          "invalid and empty controls do not change the frame");
    check(guarded.before == UINT32_C(0x12345678) &&
          guarded.after == UINT32_C(0x87654321), "controls preserve surrounding memory");
}

static void test_legacy_presenter(void)
{
    mt_ui_frame_t frame;
    fake_bus_t fake = {0};
    const mt_ui_bus_t bus = {&fake, read32, write32};

    fake.status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
    mt_ui_clear(&frame, MT_UI_COLOR_BACKGROUND);
    mt_ui_text(&frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U, "AB", MT_UI_COLOR_TEXT,
               MT_UI_COLOR_PANEL, false);
    check(mt_ui_character(&frame, MT_UI_COLUMNS - 1U, MT_UI_ROWS - 1U) == 'A', "text clips at edge");
    check(mt_ui_character(&frame, 0U, 0U) == ' ', "clear fills spaces");
    check(mt_ui_present(&bus, &frame, true) == MT_UI_PRESENT_OK,
          "frame presents");
    check(fake.writes == (MT_UI_COLUMNS * MT_UI_ROWS / 2U) + 1U,
          "one write per word plus commit");
    check(fake.first_offset == MT_UI_VRAM_BANK1_OFFSET,
          "inactive bank 1 selected");
    check(fake.last_offset == MT_REG_UI_COMMIT &&
          fake.last_value == (MT_UI_COMMIT_VISIBLE | MT_UI_COMMIT_BANK |
                              MT_UI_COMMIT_DENSE),
          "bank 1 visible commit");

    finish_commit(&fake, &frame);
    check(mt_ui_present(&bus, &frame, false) == MT_UI_PRESENT_OK,
          "hidden frame presents");
    check(fake.first_offset == MT_UI_VRAM_BANK0_OFFSET,
          "inactive bank 0 selected");
    check(fake.last_value == MT_UI_COMMIT_DENSE, "bank 0 hidden dense commit");

    finish_commit(&fake, &frame);
    fake.status = MT_UI_STATUS_DENSE_LAYOUT;
    check(mt_ui_present(&bus, &frame, true) == MT_UI_PRESENT_BUSY,
          "busy mailbox is non-destructive");
    check(fake.writes == 0U, "busy mailbox performs no writes");
}

static void test_cached_presenter(void)
{
    const uint32_t full_words = MT_UI_COLUMNS * MT_UI_ROWS / 2U;
    mt_ui_frame_t frame;
    mt_ui_frame_t previous;
    mt_ui_presenter_t presenter;
    fake_bus_t fake = {0};
    const mt_ui_bus_t bus = {&fake, read32, write32};
    mt_ui_bus_t invalid_bus = bus;

    mt_ui_presenter_init(&presenter);
    mt_ui_presenter_init(NULL);
    mt_ui_presenter_invalidate(NULL);
    mt_ui_clear(&frame, MT_UI_COLOR_BACKGROUND);
    fake.status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
    check(mt_ui_present_cached(NULL, &bus, &frame, true) ==
          MT_UI_PRESENT_INVALID_ARGUMENT, "NULL presenter rejected");
    check(mt_ui_present_cached(&presenter, NULL, &frame, true) ==
          MT_UI_PRESENT_INVALID_ARGUMENT, "NULL bus rejected");
    check(mt_ui_present_cached(&presenter, &bus, NULL, true) ==
          MT_UI_PRESENT_INVALID_ARGUMENT, "NULL frame rejected");
    invalid_bus.read32 = NULL;
    check(mt_ui_present_cached(&presenter, &invalid_bus, &frame, true) ==
          MT_UI_PRESENT_INVALID_ARGUMENT, "missing read function rejected");
    invalid_bus = bus;
    invalid_bus.write32 = NULL;
    check(mt_ui_present_cached(&presenter, &invalid_bus, &frame, true) ==
          MT_UI_PRESENT_INVALID_ARGUMENT, "missing write function rejected");
    check(fake.writes == 0U, "invalid arguments perform no writes");

    fake.status = MT_UI_STATUS_READY;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_UNSUPPORTED_LAYOUT && fake.writes == 0U,
          "legacy overlay rejected without any VRAM or commit writes");
    check(mt_ui_present(&bus, &frame, true) == MT_UI_PRESENT_UNSUPPORTED_LAYOUT &&
          fake.writes == 0U, "stateless presenter also protects legacy overlays");
    fake.status |= MT_UI_STATUS_DENSE_LAYOUT;

    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK, "initial cached frame submits");
    check(fake.vram_writes == full_words && fake.commits == 1U &&
          fake.first_offset == MT_UI_VRAM_BANK1_OFFSET,
          "unknown inactive bank receives full first frame");
    reset_counts(&fake);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_BUSY && fake.writes == 0U,
          "stale READY cannot acknowledge pending bank change");
    fake.status = MT_UI_STATUS_DENSE_LAYOUT | MT_UI_STATUS_DENSE |
                  MT_UI_STATUS_BANK | MT_UI_STATUS_VISIBLE;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_BUSY && fake.writes == 0U,
          "matching active state still waits for mailbox READY");
    finish_commit(&fake, &frame);
    fake.status &= ~MT_UI_STATUS_DENSE;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_BUSY && fake.writes == 0U,
          "bank and visible acknowledgment alone cannot acknowledge dense mode");
    fake.status |= MT_UI_STATUS_DENSE;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.writes == 0U,
          "unchanged frame skips all writes and commits");

    frame.cells[0] = mt_ui_cell('B', MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL, false);
    fake.status &= ~MT_UI_STATUS_READY;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_BUSY && fake.writes == 0U,
          "busy update does not write or corrupt cache");
    fake.status |= MT_UI_STATUS_READY;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == full_words &&
          fake.first_offset == MT_UI_VRAM_BANK0_OFFSET,
          "second frame initializes the other bank");
    finish_commit(&fake, &frame);

    frame.cells[4] = mt_ui_cell('C', MT_UI_COLOR_ACCENT, MT_UI_COLOR_PANEL, false);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == 2U && fake.commits == 1U,
          "returning to bank 1 accounts for both changes since that bank");
    finish_commit(&fake, &frame);
    previous = frame;
    frame.cells[5] = mt_ui_cell('D', MT_UI_COLOR_ACCENT, MT_UI_COLOR_PANEL, false);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == 1U && fake.commits == 1U &&
          fake.first_offset == MT_UI_VRAM_BANK0_OFFSET + 8U,
          "two changed cells in one packed word need a single VRAM write");
    finish_commit(&fake, &frame);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.writes == 0U,
          "repeated edited frame requires no writes");
    frame = previous;
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == 0U && fake.commits == 1U,
          "returning to an already cached frame needs only a bank commit");
    finish_commit(&fake, &frame);

    check(mt_ui_present_cached(&presenter, &bus, &frame, false) ==
          MT_UI_PRESENT_OK && fake.vram_writes == 0U && fake.commits == 1U &&
          fake.last_value == (MT_UI_COMMIT_BANK | MT_UI_COMMIT_DENSE),
          "hide commits the active bank without uploading text");
    reset_counts(&fake);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_BUSY && fake.writes == 0U,
          "stale READY cannot acknowledge a visibility only commit");
    finish_commit(&fake, &frame);
    check(mt_ui_present_cached(&presenter, &bus, &frame, false) ==
          MT_UI_PRESENT_OK && fake.writes == 0U,
          "already hidden frame skips commit");
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == 0U && fake.commits == 1U &&
          fake.last_value == (MT_UI_COMMIT_BANK | MT_UI_COMMIT_VISIBLE |
                              MT_UI_COMMIT_DENSE),
          "show commits the active bank without uploading text");
    finish_commit(&fake, &frame);

    mt_ui_presenter_invalidate(&presenter);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == full_words &&
          fake.first_offset == MT_UI_VRAM_BANK0_OFFSET,
          "invalidate discards both banks even when the frame is unchanged");
    /* Model PL reconfiguration while this commit was pending. */
    memset(&fake, 0, sizeof(fake));
    fake.status = MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT;
    mt_ui_presenter_invalidate(&presenter);
    check(mt_ui_present_cached(&presenter, &bus, &frame, true) ==
          MT_UI_PRESENT_OK && fake.vram_writes == full_words &&
          fake.first_offset == MT_UI_VRAM_BANK1_OFFSET,
          "PL reset invalidation forgets the pending commit and stale VRAM");
    finish_commit(&fake, &frame);
}

int main(void)
{
    test_text();
    test_controls();
    test_legacy_presenter();
    test_cached_presenter();
    puts("PASS: clipped UI fields and controls, atomic commits, and per-bank presentation cache");
    return 0;
}
