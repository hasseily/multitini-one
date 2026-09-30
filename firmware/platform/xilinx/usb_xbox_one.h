/* SPDX-License-Identifier: GPL-3.0-only */
/* Ported from Appletini ONE be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0. */
#ifndef USB_XBOX_ONE_H
#define USB_XBOX_ONE_H

#include <stdint.h>

/* Run after CherryUSB has reaped completed URBs. */
void usb_xbox_one_poll(void);
void usb_xbox_one_stop(void);
void usb_xbox_one_dump_status(void);

#endif
