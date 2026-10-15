/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_IIGS_HID_INPUT_H
#define MULTITINI_IIGS_HID_INPUT_H
#include "multitini_c64_hid_input.h"
#include "multitini_iigs_interface.h"
#include <stdbool.h>
typedef struct {
    mt_c64_hid_input_t keyboard;
    mt_c64_hid_emit_fn emit;
    void *context;
    uint16_t buttons[MT_C64_HID_SLOT_COUNT],xy[MT_C64_HID_SLOT_COUNT];
    uint16_t emitted_buttons,emitted_xy;
    uint8_t active[MT_C64_HID_SLOT_COUNT];
    int32_t mouse_x,mouse_y;
    int8_t mouse_fraction_x,mouse_fraction_y;
    uint8_t mouse_scale_quarters;
    uint32_t mouse_packet;
    uint8_t mouse_buttons,mouse_emitted_buttons,mouse_toggle;
    bool mouse_pending;
} mt_iigs_hid_input_t;
void mt_iigs_hid_init(mt_iigs_hid_input_t *,mt_c64_hid_emit_fn,void *);
int mt_iigs_hid_keyboard_report(mt_iigs_hid_input_t *,uint8_t,uint8_t,const uint8_t *,size_t);
int mt_iigs_hid_assigned_report(mt_iigs_hid_input_t *,uint8_t,uint8_t,uint8_t,int16_t,int16_t,uint8_t);
void mt_iigs_hid_mouse_report(mt_iigs_hid_input_t *,int16_t,int16_t,uint8_t);
bool mt_iigs_mouse_sensitivity_valid(uint16_t percent);
uint16_t mt_iigs_mouse_next_sensitivity(uint16_t percent);
bool mt_iigs_hid_set_mouse_sensitivity(mt_iigs_hid_input_t *,uint16_t percent);
void mt_iigs_hid_clear_mouse_motion(mt_iigs_hid_input_t *);
int mt_iigs_hid_disconnect(mt_iigs_hid_input_t *,uint8_t);
void mt_iigs_hid_reassert(mt_iigs_hid_input_t *);
int mt_iigs_hid_poll(mt_iigs_hid_input_t *);
int mt_iigs_hid_release_all(mt_iigs_hid_input_t *);
#endif
