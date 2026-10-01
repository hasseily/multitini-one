/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX_HID_INPUT_H
#define MULTITINI_VECTREX_HID_INPUT_H
#include <stddef.h>
#include <stdint.h>
#define MT_VECTREX_HID_SLOTS 9U
typedef int (*mt_vectrex_hid_emit_fn)(void *, uint16_t, uint16_t);
typedef struct {
    uint8_t keyboard_buttons[MT_VECTREX_HID_SLOTS];
    uint8_t keyboard_directions[MT_VECTREX_HID_SLOTS];
    uint8_t pad_active[MT_VECTREX_HID_SLOTS];
    uint8_t player[MT_VECTREX_HID_SLOTS]; /* 0/1 explicit, 2 legacy, 255 off. */
    uint8_t pad_buttons[MT_VECTREX_HID_SLOTS];
    int16_t pad_x[MT_VECTREX_HID_SLOTS], pad_y[MT_VECTREX_HID_SLOTS];
    uint16_t emitted[5];
    uint8_t emitted_valid;
    mt_vectrex_hid_emit_fn emit;
    void *context;
} mt_vectrex_hid_input_t;
void mt_vectrex_hid_input_init(mt_vectrex_hid_input_t *, mt_vectrex_hid_emit_fn, void *);
int mt_vectrex_hid_keyboard_report(mt_vectrex_hid_input_t *, uint8_t,
    uint8_t modifiers, const uint8_t *, size_t);
int mt_vectrex_hid_gamepad_report(mt_vectrex_hid_input_t *, uint8_t,
    uint8_t buttons, int16_t x, int16_t y);
int mt_vectrex_hid_assigned_report(mt_vectrex_hid_input_t *, uint8_t slot,
    uint8_t player, uint8_t buttons, int16_t x, int16_t y);
int mt_vectrex_hid_disconnect(mt_vectrex_hid_input_t *, uint8_t);
int mt_vectrex_hid_release_all(mt_vectrex_hid_input_t *);
void mt_vectrex_hid_reassert(mt_vectrex_hid_input_t *);
int mt_vectrex_hid_input_poll(mt_vectrex_hid_input_t *);
void mt_vectrex_hid_controls(const mt_vectrex_hid_input_t *, int controls[2][6]);
int16_t mt_vectrex_axis_normalize(int32_t value, int32_t minimum, int32_t maximum);
int mt_vectrex_xinput_decode(const uint8_t *, size_t, uint8_t *, int16_t *, int16_t *);
#endif
