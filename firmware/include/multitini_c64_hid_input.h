/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_HID_INPUT_H
#define MULTITINI_C64_HID_INPUT_H

#include <stddef.h>
#include <stdint.h>

#define MT_C64_HID_SLOT_COUNT 9U
#define MT_C64_HID_USAGE_BYTES 32U
#define MT_C64_HID_SCAN_BYTES 64U

typedef int (*mt_c64_hid_emit_fn)(void *context, uint16_t code,
                                  uint16_t value);

typedef enum {
    MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC = 0,
    MT_C64_KEYBOARD_LAYOUT_POSITIONAL = 1,
} mt_c64_keyboard_layout_t;

typedef struct {
    uint8_t usages[MT_C64_HID_SLOT_COUNT][MT_C64_HID_USAGE_BYTES];
    uint8_t joysticks[MT_C64_HID_SLOT_COUNT];
    uint8_t joystick_active[MT_C64_HID_SLOT_COUNT];
    uint8_t emitted_scans[MT_C64_HID_SCAN_BYTES];
    uint8_t emitted_joystick;
    uint8_t joystick_port;
    uint8_t emitted_joystick_port;
    mt_c64_keyboard_layout_t keyboard_layout;
    mt_c64_hid_emit_fn emit;
    void *emit_context;
} mt_c64_hid_input_t;

void mt_c64_hid_input_init(mt_c64_hid_input_t *input,
                           mt_c64_hid_emit_fn emit, void *emit_context);
int mt_c64_hid_keyboard_report(mt_c64_hid_input_t *input, uint8_t slot,
                               uint8_t modifiers, const uint8_t *keys,
                               size_t key_count);
int mt_c64_hid_joystick_report(mt_c64_hid_input_t *input, uint8_t slot,
                               uint8_t state);
int mt_c64_hid_disconnect(mt_c64_hid_input_t *input, uint8_t slot);
int mt_c64_hid_set_joystick_port(mt_c64_hid_input_t *input, uint8_t port);
int mt_c64_hid_set_keyboard_layout(mt_c64_hid_input_t *input,
                                   mt_c64_keyboard_layout_t layout);
mt_c64_keyboard_layout_t mt_c64_hid_get_keyboard_layout(
    const mt_c64_hid_input_t *input);
int mt_c64_hid_release_all(mt_c64_hid_input_t *input);
void mt_c64_hid_reassert(mt_c64_hid_input_t *input);
int mt_c64_hid_input_poll(mt_c64_hid_input_t *input);

#endif
