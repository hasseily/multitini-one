/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef struct { unsigned unused; } XScuGic;
typedef struct { struct { u32 BaseAddress; } Config; } XUsbPs;
typedef struct { XScuGic *Gic; u32 GicEnabled, Ier; } UsbStorageIrqState;
typedef struct { u32 BufferLen; unsigned char Data[512]; } UsbStorageReq;
#define XSCUGIC_ENABLE_SET_OFFSET 0x100U
#define XUSBPS_IER_OFFSET 0x148U
#define XUSBPS_IXR_ALL UINT32_MAX
#define USB_STORAGE_REQ_QUEUE_DEPTH 32U
#define USB0_ID 53U
#define USB1_ID 54U
#define USB0_BIT (UINT32_C(1) << (USB0_ID % 32U))
#define USB1_BIT (UINT32_C(1) << (USB1_ID % 32U))
static XScuGic gic;
static XUsbPs UsbInstance = {{0xE0002000U}};
static u16 UsbGicIntrId = USB0_ID;
static uint8_t UsbGicIntrReady = 1U;
static u32 enabled, ier, barriers, class_resets, handled, usb1_events;
static int handler_active, inject_reset, pending_reset, in_irq;
static int expect_head_commit;
static volatile u32 StorageReqHead, StorageReqTail;
static UsbStorageReq StorageReqQueue[USB_STORAGE_REQ_QUEUE_DEPTH];
static struct { u32 processed_packets, processed_bytes, last_queue_depth; } StorageStats;
static void UsbResetLocalStorageState(void);

static XScuGic *gic_get(void) { return &gic; }
static u32 XScuGic_DistReadReg(XScuGic *p, u32 offset)
{
    assert(p == &gic && offset == XSCUGIC_ENABLE_SET_OFFSET + (USB0_ID / 32U) * 4U);
    return enabled;
}
static void XScuGic_Disable(XScuGic *p, u16 id)
{
    assert(p == &gic && id == USB0_ID);
    enabled &= ~USB0_BIT;
    assert((enabled & USB1_BIT) != 0U);
}
static void dispatch_pending(void)
{
    /* A latched GIC interrupt can enter the vendor raw-ISR handler even if
     * IER is now zero. Only the USB0 GIC enable blocks it here. */
    if (pending_reset && !in_irq && (enabled & USB0_BIT) != 0U) {
        assert(!handler_active);
        if (expect_head_commit) {
            assert(StorageReqHead == 6U && StorageReqTail == 6U);
            assert(StorageStats.processed_packets == 1U && StorageStats.processed_bytes == 512U);
            assert(StorageStats.last_queue_depth == 0U);
            expect_head_commit = 0;
        }
        in_irq = 1;
        pending_reset = 0;
        UsbResetLocalStorageState();
        in_irq = 0;
    }
}
static void XScuGic_Enable(XScuGic *p, u16 id)
{
    assert(p == &gic && id == USB0_ID);
    enabled |= USB0_BIT;
    dispatch_pending();
}
static void dsb(void) { ++barriers; }
static u32 XUsbPs_ReadReg(u32 base, u32 offset)
{ assert(base == UsbInstance.Config.BaseAddress && offset == XUSBPS_IER_OFFSET); return ier; }
static void XUsbPs_WriteReg(u32 base, u32 offset, u32 value)
{ assert(base == UsbInstance.Config.BaseAddress && offset == XUSBPS_IER_OFFSET); ier = value; }
static void XUsbPs_IntrDisable(XUsbPs *p, u32 mask)
{ assert(p == &UsbInstance); ier &= ~mask; }
static u32 UsbStorageQueueDepth(u32 head, u32 tail)
{ return tail >= head ? tail - head : USB_STORAGE_REQ_QUEUE_DEPTH - head + tail; }
static void XUsbPs_StorageReset(void)
{
    assert(!handler_active && (enabled & USB0_BIT) == 0U && ier == 0U);
    assert(StorageReqHead == 0U && StorageReqTail == 0U);
    ++class_resets;
}
static void XUsbPs_HandleStorageReq(XUsbPs *p, unsigned ep, unsigned char *data, u32 bytes)
{
    assert(p == &UsbInstance && ep == 1U && data == StorageReqQueue[5].Data && bytes == 512U);
    assert((enabled & USB0_BIT) == 0U && ier == 0U);
    assert((enabled & USB1_BIT) != 0U);
    handler_active = 1;
    ++handled;
    if (inject_reset) {
        pending_reset = 1;
        dispatch_pending();
        assert(class_resets == 0U && StorageReqHead == 5U && StorageReqTail == 6U);
    }
    ++usb1_events; /* Other interrupt sources remain serviceable during SD I/O. */
    handler_active = 0;
}
#include "usb_storage_queue.inc"

static void reset_state(int usb0_enabled, u32 initial_ier)
{
    enabled = USB1_BIT | (usb0_enabled ? USB0_BIT : 0U);
    ier = initial_ier;
    barriers = class_resets = handled = usb1_events = 0U;
    handler_active = inject_reset = pending_reset = in_irq = expect_head_commit = 0;
    StorageReqHead = 5U;
    StorageReqTail = 6U;
    memset(&StorageStats, 0, sizeof(StorageStats));
    StorageReqQueue[5].BufferLen = 512U;
}
int main(void)
{
    const u32 masks[] = {0U, 0xA5A5U, UINT32_MAX};
    unsigned cases = 0U;
    for (unsigned active = 0U; active < 2U; ++active) {
        for (unsigned i = 0U; i < sizeof(masks) / sizeof(masks[0]); ++i) {
            reset_state((int)active, masks[i]);
            const u32 original_enabled = enabled;
            UsbStorageIrqState outer = UsbStorageIrqSave(&UsbInstance);
            assert((enabled & USB0_BIT) == 0U && ier == 0U);
            UsbStorageIrqState inner = UsbStorageIrqSave(&UsbInstance);
            UsbStorageIrqRestore(&UsbInstance, inner);
            assert((enabled & USB0_BIT) == 0U && ier == 0U);
            UsbStorageIrqRestore(&UsbInstance, outer);
            assert(enabled == original_enabled && ier == masks[i] && barriers >= 4U);
            ++cases;

            reset_state((int)active, masks[i]);
            assert(UsbProcessQueuedStorageReq(&UsbInstance) == 1);
            assert(StorageReqHead == 6U && StorageReqTail == 6U && handled == 1U);
            assert(class_resets == 0U && usb1_events == 1U && enabled == original_enabled && ier == masks[i]);
            assert(UsbProcessQueuedStorageReq(&UsbInstance) == 0 && handled == 1U);
            assert(enabled == original_enabled && ier == masks[i]);
            ++cases;

            reset_state((int)active, masks[i]);
            UsbResetLocalStorageState();
            assert(class_resets == 1U && StorageReqHead == 0U && StorageReqTail == 0U);
            assert(enabled == original_enabled && ier == masks[i]);
            ++cases;
        }
    }
    for (unsigned i = 0U; i < sizeof(masks) / sizeof(masks[0]); ++i) {
        reset_state(1, masks[i]);
        inject_reset = expect_head_commit = 1;
        assert(UsbProcessQueuedStorageReq(&UsbInstance) == 1);
        assert(class_resets == 1U && pending_reset == 0 && expect_head_commit == 0);
        assert(StorageReqHead == 0U && StorageReqTail == 0U && StorageStats.last_queue_depth == 0U);
        assert(handled == 1U && usb1_events == 1U && enabled == (USB0_BIT | USB1_BIT) && ier == masks[i]);
        assert(UsbProcessQueuedStorageReq(&UsbInstance) == 0 && handled == 1U);
        ++cases;
    }
    printf("PASS %u production USB0 queue cases: reset waits for head commit, nested GIC/IER restore exact state, USB1 remains enabled\n", cases);
    return 0;
}
