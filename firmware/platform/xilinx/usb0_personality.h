/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef USB0_PERSONALITY_H
#define USB0_PERSONALITY_H
#include "xusbps.h"
#include "xusbps_ch9.h"
/* Unsupported optional USB2 requests fall through to the common stall path. */
int usb0_personality_vendor_req(XUsbPs *,XUsbPs_SetupData *,int *);
u32 usb0_personality_bos_desc(u8 *,u32);
#endif
