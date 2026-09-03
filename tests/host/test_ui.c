/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ui.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint32_t status;
    uint32_t writes;
    uint32_t first_offset;
    uint32_t last_offset;
    uint32_t last_value;
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
    if (bus->writes == 0U) bus->first_offset = offset;
    ++bus->writes;
    bus->last_offset = offset;
    bus->last_value = value;
}

int main(void)
{
    mt_ui_frame_t frame;
    fake_bus_t fake = {MT_UI_STATUS_READY, 0U, 0U, 0U, 0U};
    const mt_ui_bus_t bus = {&fake, read32, write32};

    mt_ui_clear(&frame, MT_UI_COLOR_BACKGROUND);
    mt_ui_text(&frame, 79U, 35U, "AB", MT_UI_COLOR_TEXT,
               MT_UI_COLOR_PANEL, false);
    check(mt_ui_character(&frame, 79U, 35U) == 'A', "text clips at edge");
    check(mt_ui_character(&frame, 0U, 0U) == ' ', "clear fills spaces");
    check(mt_ui_present(&bus, &frame, true) == MT_UI_PRESENT_OK,
          "frame presents");
    check(fake.writes == (MT_UI_COLUMNS * MT_UI_ROWS / 2U) + 1U,
          "one write per word plus commit");
    check(fake.first_offset == MT_UI_VRAM_BANK1_OFFSET,
          "inactive bank 1 selected");
    check(fake.last_offset == MT_REG_UI_COMMIT &&
          fake.last_value == (MT_UI_COMMIT_VISIBLE | MT_UI_COMMIT_BANK),
          "bank 1 visible commit");

    fake.status = MT_UI_STATUS_READY | MT_UI_STATUS_BANK;
    fake.writes = 0U;
    check(mt_ui_present(&bus, &frame, false) == MT_UI_PRESENT_OK,
          "hidden frame presents");
    check(fake.first_offset == MT_UI_VRAM_BANK0_OFFSET,
          "inactive bank 0 selected");
    check(fake.last_value == 0U, "bank 0 hidden commit");

    fake.status = 0U;
    fake.writes = 0U;
    check(mt_ui_present(&bus, &frame, true) == MT_UI_PRESENT_BUSY,
          "busy mailbox is non-destructive");
    check(fake.writes == 0U, "busy mailbox performs no writes");
    puts("PASS: UI frame and atomic presenter");
    return 0;
}
