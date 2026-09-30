/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_GENESIS_HID_INPUT_H
#define MULTITINI_GENESIS_HID_INPUT_H
#include "multitini_gamepad.h"
#include <stddef.h>
#include <stdint.h>

typedef int (*mt_genesis_hid_emit_fn)(void *, uint16_t, uint16_t);
typedef struct {
    uint16_t keyboard[MT_GAMEPAD_SLOTS];
    uint16_t gamepad[MT_GAMEPAD_SLOTS];
    uint8_t connected[MT_GAMEPAD_SLOTS];
    uint8_t player[MT_GAMEPAD_SLOTS]; /* 0/1 or 0xff while unassigned. */
    uint16_t emitted[2];
    uint8_t emitted_valid;
    mt_genesis_hid_emit_fn emit;
    void *context;
} mt_genesis_hid_input_t;

void mt_genesis_hid_input_init(mt_genesis_hid_input_t *, mt_genesis_hid_emit_fn, void *);
int mt_genesis_hid_keyboard_report(mt_genesis_hid_input_t *, uint8_t slot,
    uint8_t modifiers, const uint8_t *keys, size_t count);
/* Logical A/B/X = Genesis A/B/C; Y/LB/RB = X/Y/Z; View/Menu = Mode/Start.
 * The common controller setup applies remapping before this function. */
int mt_genesis_hid_gamepad_report(mt_genesis_hid_input_t *, uint8_t slot,
    const mt_gamepad_state_t *);
int mt_genesis_hid_disconnect(mt_genesis_hid_input_t *, uint8_t slot);
int mt_genesis_hid_release_all(mt_genesis_hid_input_t *);
void mt_genesis_hid_reassert(mt_genesis_hid_input_t *);
int mt_genesis_hid_input_poll(mt_genesis_hid_input_t *);
#endif
