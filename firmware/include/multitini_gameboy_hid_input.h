/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_GAMEBOY_HID_INPUT_H
#define MULTITINI_GAMEBOY_HID_INPUT_H

#include <stddef.h>
#include <stdint.h>

#define MT_GAMEBOY_HID_SLOT_COUNT 9U
#define MT_GAMEBOY_HID_USAGE_BYTES 32U

typedef int (*mt_gameboy_hid_emit_fn)(void *context, uint16_t code,
                                      uint16_t value);

typedef struct {
    uint8_t usages[MT_GAMEBOY_HID_SLOT_COUNT][MT_GAMEBOY_HID_USAGE_BYTES];
    uint8_t gamepads[MT_GAMEBOY_HID_SLOT_COUNT];
    uint8_t gamepad_active[MT_GAMEBOY_HID_SLOT_COUNT];
    uint8_t emitted_buttons;
    mt_gameboy_hid_emit_fn emit;
    void *emit_context;
} mt_gameboy_hid_input_t;

void mt_gameboy_hid_input_init(mt_gameboy_hid_input_t *input,
                               mt_gameboy_hid_emit_fn emit,
                               void *emit_context);
int mt_gameboy_hid_keyboard_report(mt_gameboy_hid_input_t *input,
                                   uint8_t slot, uint8_t modifiers,
                                   const uint8_t *keys, size_t key_count);
int mt_gameboy_hid_gamepad_report(mt_gameboy_hid_input_t *input,
                                  uint8_t slot, uint8_t state);
int mt_gameboy_hid_disconnect(mt_gameboy_hid_input_t *input, uint8_t slot);
int mt_gameboy_hid_release_all(mt_gameboy_hid_input_t *input);
void mt_gameboy_hid_reassert(mt_gameboy_hid_input_t *input);
int mt_gameboy_hid_input_poll(mt_gameboy_hid_input_t *input);

#endif
