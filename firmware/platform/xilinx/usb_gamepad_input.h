/* SPDX-License-Identifier: GPL-3.0-only */
/* Ported from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0. */
#ifndef USB_GAMEPAD_INPUT_H
#define USB_GAMEPAD_INPUT_H

#include <stdint.h>

#include "multitini_gamepad.h"

enum {
    MT_USB_PAD_LEFT_STICK = 8, MT_USB_PAD_RIGHT_STICK = 9,
    MT_USB_PAD_HOME = 10, MT_USB_PAD_LEFT_TRIGGER = 11,
    MT_USB_PAD_RIGHT_TRIGGER = 12, MT_USB_PAD_TOUCHPAD = 13
};
struct usbh_hubport;

/* Vendor gamepads share the eight menu/input slots with HID interfaces. */
int usb_gamepad_input_connect(void);
void usb_gamepad_input_disconnect(uint8_t slot);
void usb_gamepad_input_identify(uint8_t slot, struct usbh_hubport *port,
                                uint8_t interface_id, uint8_t transport);

/* Vendor buttons: A, B, X, Y, LB, RB, Select, Start in bits 0..7;
 * independent stick clicks, Home and auxiliary buttons follow above.
 * Hat is clockwise from north (0..7), or 8 for neutral. */
void usb_gamepad_input_report(
    uint8_t slot,
    const mt_gamepad_report_t *report,
    uint8_t hat,
    uint8_t extra_active);

#endif /* USB_GAMEPAD_INPUT_H */
