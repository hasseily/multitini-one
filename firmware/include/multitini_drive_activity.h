/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_DRIVE_ACTIVITY_H
#define MULTITINI_DRIVE_ACTIVITY_H

#include <stdbool.h>
#include <stdint.h>

#include "multitini_ui.h"

/* Storage-activity badge for any core, after Appletini's storage widget:
 * a unit label, an LED light and read/write lights in the top-right corner.
 * Services keep their own counters; the runtime samples them and the core's
 * live lights. The badge uses the legacy 80x36 overlay layout, which ABI
 * 0.11 shows without pausing the core or muting its audio. */
#define MT_ACTIVITY_MAX_UNITS 12U
#define MT_ACTIVITY_LABEL_MAX 5U
#define MT_ACTIVITY_READ_HOLD_MS 133U
#define MT_ACTIVITY_WRITE_HOLD_MS 267U
#define MT_ACTIVITY_VISIBLE_HOLD_MS 1500U
#define MT_ACTIVITY_BADGE_WIDTH 10U
#define MT_ACTIVITY_CELLS (MT_UI_LEGACY_COLUMNS * MT_UI_LEGACY_ROWS)

typedef struct {
    char label[MT_ACTIVITY_LABEL_MAX];
    bool used, counted;
    bool has_led;
    bool led, motor;
    uint32_t reads, writes;
    uint64_t read_until_ms, write_until_ms, last_ms;
} mt_activity_unit_t;

typedef struct {
    mt_activity_unit_t units[MT_ACTIVITY_MAX_UNITS];
    unsigned shown;
    bool any;
    uint64_t visible_until_ms;
} mt_activity_t;

void mt_activity_reset(mt_activity_t *activity);
/* Compare a service's cumulative counters with the last sample. The first
 * sample of a unit only records them, so existing totals do not flash. */
void mt_activity_sample(mt_activity_t *activity, unsigned unit, const char *label,
                        uint32_t reads, uint32_t writes, uint64_t now_ms);
/* Live LED and spindle state reported by the core, such as a 1541's. */
void mt_activity_lights(mt_activity_t *activity, unsigned unit, const char *label,
                        bool led, bool motor, uint64_t now_ms);
/* Render the badge into a transparent legacy frame. Returns whether it is
 * visible; an invisible result leaves every cell transparent. */
bool mt_activity_render(mt_activity_t *activity, uint64_t now_ms,
                        mt_ui_legacy_frame_t *frame);

#endif
