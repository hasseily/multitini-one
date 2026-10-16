/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef struct { unsigned unused; } XUsbPs;
typedef struct { u8 bRequest; uint16_t wValue, wLength; } XUsbPs_SetupData;
#define XST_SUCCESS 0
#define XUSBPS_REQ_GET_DESCRIPTOR 6U
#define XUSBPS_REQ_REPLY_LEN 512U
#define XUSBPS_TYPE_DEVICE_DESC 1U
#define XUSBPS_TYPE_CONFIG_DESC 2U
#define XUSBPS_TYPE_STRING_DESC 3U
#define XUSBPS_TYPE_DEVICE_QUALIFIER 6U
#define XUSBPS_TYPE_HID_DESC 0x21U
#define XUSBPS_TYPE_REPORT_DESC 0x22U
#define XUSBPS_EP_DIRECTION_IN 1U
#define XUSBPS_EP_DIRECTION_OUT 2U
#define MOUSE_SIMULATION
static unsigned sends, stalls, errors, failures, expected_len;
static int fail_send;
static int reply(u8 *bytes, unsigned size) { assert(size >= 18U); bytes[0] = 18U; return 18; }
#define XUsbPs_Ch9SetupDevDescReply reply
#define XUsbPs_Ch9SetupCfgDescReply reply
#define XUsbPs_Ch9SetupHidDescReply reply
#define XUsbPs_Ch9SetupReportDescReply reply
static int XUsbPs_Ch9SetupStrDescReply(u8 *p, unsigned n, unsigned index)
{ (void)index; return reply(p, n); }
static int usb0_personality_bos_desc(u8 *p, unsigned n) { return reply(p, n); }
static int XUsbPs_Ep0GuardedSend(XUsbPs *usb, const u8 *p, unsigned n)
{ (void)usb; assert(p && n == expected_len); ++sends; return fail_send; }
static void usb_storage_debug_note_descriptor(u8 type, unsigned requested, unsigned n)
{ (void)type; (void)requested; assert(n == expected_len); }
static void usb_storage_debug_note_ep_send_failure(unsigned ep, unsigned n, int status)
{ assert(ep == 0U && n == expected_len && status != 0); ++failures; }
static void usb_storage_debug_note_ch9_error(const XUsbPs_SetupData *setup)
{ assert(setup); ++errors; }
static void usb_storage_debug_note_ep0_stall(void) { ++stalls; }
static void XUsbPs_EpStall(XUsbPs *usb, unsigned ep, unsigned dir)
{ (void)usb; assert(ep == 0U && dir == 3U); }
#include "usb_storage_ch9.inc"
int main(void)
{
    XUsbPs usb = {0};
    const unsigned types[] = {1U, 6U, 2U, 3U, 0x21U, 0x22U};
    unsigned cases = 0U;
    for (unsigned i = 0; i < sizeof(types)/sizeof(types[0]); ++i) {
        for (unsigned short_reply = 0; short_reply < 2U; ++short_reply) {
            for (unsigned fail = 0; fail < 2U; ++fail) {
                XUsbPs_SetupData setup = {6U, types[i] << 8, short_reply ? 8U : 64U};
                expected_len = short_reply ? 8U : 18U;
                fail_send = (int)fail; sends = stalls = errors = failures = 0U;
                descriptor_request(&usb, &setup);
                assert(sends == 1U && stalls == fail && errors == fail);
                /* HID variants share recovery but have no diagnostics hook. */
                assert(failures == (types[i] < 0x20U ? fail : 0U));
                ++cases;
            }
        }
    }
    printf("PASS %u production CH9 descriptor cases: failed sends return to EP0 stall instead of hanging ISR\n", cases);
    return 0;
}
