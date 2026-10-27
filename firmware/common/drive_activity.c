/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_drive_activity.h"

#include <string.h>

static mt_activity_unit_t *unit_slot(mt_activity_t *activity, unsigned unit,
                                     const char *label)
{
    mt_activity_unit_t *slot;
    if (activity == NULL || unit >= MT_ACTIVITY_MAX_UNITS) return NULL;
    slot = &activity->units[unit];
    if (!slot->used) {
        slot->used = true;
        if (label != NULL) {
            size_t length = strlen(label);
            if (length >= sizeof(slot->label)) length = sizeof(slot->label) - 1U;
            memcpy(slot->label, label, length);
            slot->label[length] = '\0';
        }
    }
    return slot;
}

static void touch(mt_activity_t *activity, unsigned unit, uint64_t now_ms)
{
    activity->units[unit].last_ms = now_ms;
    activity->shown = unit;
    activity->any = true;
}

void mt_activity_reset(mt_activity_t *activity)
{
    if (activity != NULL) memset(activity, 0, sizeof(*activity));
}

void mt_activity_sample(mt_activity_t *activity, unsigned unit, const char *label,
                        uint32_t reads, uint32_t writes, uint64_t now_ms)
{
    mt_activity_unit_t *slot = unit_slot(activity, unit, label);
    if (slot == NULL) return;
    if (!slot->counted) {
        slot->counted = true;
    } else {
        if (reads != slot->reads) {
            slot->read_until_ms = now_ms + MT_ACTIVITY_READ_HOLD_MS;
            touch(activity, unit, now_ms);
        }
        if (writes != slot->writes) {
            slot->write_until_ms = now_ms + MT_ACTIVITY_WRITE_HOLD_MS;
            touch(activity, unit, now_ms);
        }
    }
    slot->reads = reads;
    slot->writes = writes;
}

void mt_activity_lights(mt_activity_t *activity, unsigned unit, const char *label,
                        bool led, bool motor, uint64_t now_ms)
{
    mt_activity_unit_t *slot = unit_slot(activity, unit, label);
    if (slot == NULL) return;
    /* Only a rising light selects the unit; a light that stays on keeps the
     * badge visible without stealing it from a newer event elsewhere. */
    if ((led && !slot->led) || (motor && !slot->motor)) touch(activity, unit, now_ms);
    else if (led || motor) slot->last_ms = now_ms;
    slot->has_led = true;
    slot->led = led;
    slot->motor = motor;
}

static bool active(const mt_activity_unit_t *slot, uint64_t now_ms)
{
    return slot->used && (slot->led || slot->motor ||
        (slot->last_ms != 0U && now_ms - slot->last_ms < MT_ACTIVITY_VISIBLE_HOLD_MS) ||
        now_ms < slot->read_until_ms || now_ms < slot->write_until_ms);
}

bool mt_activity_render(mt_activity_t *activity, uint64_t now_ms,
                        mt_ui_legacy_frame_t *frame)
{
    const uint16_t clear = mt_ui_cell(' ', MT_UI_COLOR_TEXT, MT_UI_COLOR_BACKGROUND, true);
    const size_t base = MT_UI_LEGACY_COLUMNS - MT_ACTIVITY_BADGE_WIDTH;
    const mt_activity_unit_t *slot;
    bool visible = false;
    bool reading, writing, lit;
    size_t index;
    if (activity == NULL || frame == NULL) return false;
    for (index = 0U; index < MT_ACTIVITY_CELLS; ++index) frame->cells[index] = clear;
    for (index = 0U; index < MT_ACTIVITY_MAX_UNITS; ++index)
        visible = visible || active(&activity->units[index], now_ms);
    if (!activity->any || !visible) return false;
    slot = &activity->units[activity->shown];
    reading = now_ms < slot->read_until_ms;
    writing = now_ms < slot->write_until_ms;
    lit = slot->has_led ? slot->led : (reading || writing);
    for (index = 0U; index < MT_ACTIVITY_BADGE_WIDTH; ++index)
        frame->cells[base + index] = mt_ui_cell(' ', MT_UI_COLOR_TEXT, MT_UI_COLOR_PANEL, false);
    for (index = 0U; index < 4U && slot->label[index] != '\0'; ++index)
        frame->cells[base + 1U + index] = mt_ui_cell(slot->label[index],
            active(slot, now_ms) ? MT_UI_COLOR_TEXT : MT_UI_COLOR_MUTED, MT_UI_COLOR_PANEL, false);
    frame->cells[base + 6U] = mt_ui_cell(' ', MT_UI_COLOR_TEXT,
                                         lit ? MT_UI_COLOR_RED : MT_UI_COLOR_DIM, false);
    frame->cells[base + 7U] = reading ?
        mt_ui_cell('R', MT_UI_COLOR_BACKGROUND, MT_UI_COLOR_SUCCESS, false) :
        mt_ui_cell('R', MT_UI_COLOR_DIM, MT_UI_COLOR_PANEL, false);
    frame->cells[base + 8U] = writing ?
        mt_ui_cell('W', MT_UI_COLOR_BACKGROUND, MT_UI_COLOR_WARNING, false) :
        mt_ui_cell('W', MT_UI_COLOR_DIM, MT_UI_COLOR_PANEL, false);
    return true;
}
