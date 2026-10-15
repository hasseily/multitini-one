/* SPDX-License-Identifier: GPL-3.0-only */
/* Appletini USB0 dispatch narrowed to the mass-storage personality. */
#include "usb0_personality.h"
#include "xusbps_ch9_storage.h"
#include "xusbps_class_storage.h"
u32 XUsbPs_Ch9SetupDevDescReply(u8 *p,u32 n) { return XUsbPs_Ch9SetupDevDescReply_Storage(p,n); }
u32 XUsbPs_Ch9SetupCfgDescReply(u8 *p,u32 n) { return XUsbPs_Ch9SetupCfgDescReply_Storage(p,n); }
u32 XUsbPs_Ch9SetupStrDescReply(u8 *p,u32 n,u8 i) { return XUsbPs_Ch9SetupStrDescReply_Storage(p,n,i); }
void XUsbPs_SetConfiguration(XUsbPs *p,int i) { XUsbPs_SetConfiguration_Storage(p,i); }
void XUsbPs_ClassReq(XUsbPs *p,XUsbPs_SetupData *s) { XUsbPs_ClassReq_Storage(p,s); }
/* USB2.00 MSC has no vendor requests or BOS descriptor. Common Chapter9
 * handling explicitly stalls these unsupported requests. */
int usb0_personality_vendor_req(XUsbPs *p,XUsbPs_SetupData *s,int *status)
{ (void)p;(void)s;(void)status;return 0; }
u32 usb0_personality_bos_desc(u8 *p,u32 n) { (void)p;(void)n;return 0U; }
