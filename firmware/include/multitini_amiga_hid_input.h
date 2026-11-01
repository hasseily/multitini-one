/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_HID_INPUT_H
#define MULTITINI_AMIGA_HID_INPUT_H
#include "multitini_c64_hid_input.h"
#include "multitini_amiga_interface.h"
typedef struct {
    uint8_t keys[MT_C64_HID_SLOT_COUNT][16], emitted_keys[16];
    uint8_t joystick[MT_C64_HID_SLOT_COUNT],player[MT_C64_HID_SLOT_COUNT],emitted_joystick[2];
    int32_t mouse_x,mouse_y;
    uint8_t mouse_buttons,emitted_mouse_buttons;
    mt_c64_hid_emit_fn emit; void *context;
    bool reset_enabled,reset_latched,reset_pending;
} mt_amiga_hid_input_t;
void mt_amiga_hid_init(mt_amiga_hid_input_t *,mt_c64_hid_emit_fn,void *);
int mt_amiga_hid_keyboard_report(mt_amiga_hid_input_t *,uint8_t slot,uint8_t modifiers,const uint8_t *,size_t);
int mt_amiga_hid_assigned_report(mt_amiga_hid_input_t *,uint8_t slot,uint8_t player,uint8_t directions,uint8_t buttons);
void mt_amiga_hid_mouse_report(mt_amiga_hid_input_t *,int16_t x,int16_t y,uint8_t buttons);
void mt_amiga_hid_clear_mouse_motion(mt_amiga_hid_input_t *);
int mt_amiga_hid_poll(mt_amiga_hid_input_t *);
int mt_amiga_hid_disconnect(mt_amiga_hid_input_t *,uint8_t slot);
int mt_amiga_hid_release_all(mt_amiga_hid_input_t *);
void mt_amiga_hid_reassert(mt_amiga_hid_input_t *);
void mt_amiga_hid_set_reset_enabled(mt_amiga_hid_input_t *,bool enabled);
bool mt_amiga_hid_take_reset(mt_amiga_hid_input_t *);
#endif
