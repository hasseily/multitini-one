/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_USB_HID_SERVICE_H
#define MULTITINI_USB_HID_SERVICE_H

#include "multitini_core_link.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t reports;
    uint32_t submit_errors;
    uint32_t transfer_errors;
    uint32_t input_errors;
    uint32_t portsc;
    int last_error;
    uint8_t started;
    uint8_t keyboards;
    uint8_t gamepads;
    uint8_t joystick_port;
    uint8_t symbolic_keyboard;
} mt_usb_hid_status_t;

void usb_hid_service_init(mt_core_link_t *link);
int usb_hid_service_start(void);
int usb_hid_service_stop(void);
void usb_hid_service_poll(void);
void usb_hid_service_set_core_ready(bool ready);
int usb_hid_service_set_joystick_port(uint8_t port);
uint8_t usb_hid_service_joystick_port(void);
int usb_hid_service_set_symbolic_keyboard(bool enabled);
bool usb_hid_service_symbolic_keyboard(void);
void usb_hid_service_get_status(mt_usb_hid_status_t *status);
void usb_hid_service_dump_status(void);

#endif
