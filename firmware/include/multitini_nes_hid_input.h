/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_NES_HID_INPUT_H
#define MULTITINI_NES_HID_INPUT_H
#include "multitini_gamepad.h"
#include <stddef.h>
#include <stdint.h>

typedef int (*mt_nes_hid_emit_fn)(void *, uint16_t, uint16_t);
typedef struct {
    uint16_t keyboard[MT_GAMEPAD_SLOTS];
    uint16_t gamepad[MT_GAMEPAD_SLOTS];
    uint8_t connected[MT_GAMEPAD_SLOTS];
    uint8_t player[MT_GAMEPAD_SLOTS]; /* 0/1 or 0xff while unassigned. */
    uint8_t assignment_explicit[MT_GAMEPAD_SLOTS];
    uint16_t emitted[2];
    uint8_t emitted_valid;
    mt_nes_hid_emit_fn emit;
    void *context;
} mt_nes_hid_input_t;

void mt_nes_hid_input_init(mt_nes_hid_input_t *, mt_nes_hid_emit_fn, void *);
int mt_nes_hid_keyboard_report(mt_nes_hid_input_t *, uint8_t slot,
    uint8_t modifiers, const uint8_t *keys, size_t count);
/* Two standard pads; common controller setup applies remapping first. */
int mt_nes_hid_gamepad_report(mt_nes_hid_input_t *, uint8_t slot,
    const mt_gamepad_state_t *);
int mt_nes_hid_assigned_report(mt_nes_hid_input_t *, uint8_t slot,
    uint8_t player, const mt_gamepad_state_t *);
int mt_nes_hid_disconnect(mt_nes_hid_input_t *, uint8_t slot);
int mt_nes_hid_release_all(mt_nes_hid_input_t *);
void mt_nes_hid_reassert(mt_nes_hid_input_t *);
int mt_nes_hid_input_poll(mt_nes_hid_input_t *);
#endif
