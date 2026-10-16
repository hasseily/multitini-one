/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef struct { unsigned unused; } XScuGic;
typedef struct { unsigned NumEndpoints; struct { struct { void *dTDCurr; } Out; } Ep[2]; } XUsbPs_DeviceConfig;
typedef struct { struct { u32 BaseAddress; } Config; XUsbPs_DeviceConfig DeviceConfig; } XUsbPs;
typedef struct { XScuGic *Gic; u32 GicEnabled, Ier; } UsbStorageIrqState;
typedef struct { u32 BufferLen; unsigned char Data[512]; } UsbStorageReq;
#define XSCUGIC_ENABLE_SET_OFFSET 0x100U
#define XUSBPS_IER_OFFSET 0x148U
#define XUSBPS_EPFLUSH_OFFSET 0x1B4U
#define XUSBPS_EPPRIME_OFFSET 0x1B0U
#define XUSBPS_EPRDY_OFFSET 0x1B8U
#define XUSBPS_EPCOMPL_OFFSET 0x1BCU
#define XUSBPS_EPCR1_OFFSET 0x1C4U
#define XUSBPS_EPCR_RXE_MASK 0x80U
#define XUSBPS_EPCR_TXE_MASK 0x800000U
#define XUSBPS_EP_DIRECTION_OUT 2U
#define XUSBPS_EP_DIRECTION_IN 1U
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XUSBPS_EP_EVENT_DATA_RX 1U
#define XUSBPS_EP_EVENT_DATA_TX 2U
#define XUSBPS_dTDTOKEN 4U
#define Xil_AssertVoid assert
#define Xil_DCacheInvalidateRange(address,bytes) ((void)(bytes))
#define XUSBPS_IXR_ALL UINT32_MAX
#define USB_STORAGE_REQ_QUEUE_DEPTH 32U
#define USB_STORAGE_REQ_MAX_BYTES 512U
#define USB0_ID 53U
#define USB1_ID 54U
#define USB0_BIT (UINT32_C(1) << (USB0_ID % 32U))
#define USB1_BIT (UINT32_C(1) << (USB1_ID % 32U))
static XScuGic gic;
static XUsbPs UsbInstance = {.Config = {0xE0002000U}, .DeviceConfig = {.NumEndpoints = 2U}};
static u16 UsbGicIntrId = USB0_ID;
static uint8_t UsbGicIntrReady = 1U;
static u32 enabled, ier, barriers, class_resets, handled, usb1_events;
static int handler_active, inject_reset, pending_reset, in_irq;
static int expect_head_commit;
static volatile u32 StorageReqHead, StorageReqTail;
static UsbStorageReq StorageReqQueue[USB_STORAGE_REQ_QUEUE_DEPTH];
static struct {
    u32 processed_packets, processed_bytes, last_queue_depth, msc_resets;
    u32 ep1_rx_events,last_ep1_event,fast_packets,fast_bytes,ep1_rx_failures,ep1_tx_events,ep1_other_events;
    u32 queue_drops,queued_packets,queued_bytes,queue_high_water;
} StorageStats;
static volatile uint8_t NeedBulkReset;
static u32 BulkResetEnableBits, ep_control, ep_complete, busy_reads;
static unsigned reconfigured, primed;
static int dma_busy, reconfigure_failure, prime_failure;
static u32 rx_token;
static unsigned rx_received, rx_released, rx_admitted, rx_errors, rx_noted;
static int fast_handles;
static u32 rx_count;
static u8 rx_buffer[512];
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
{
    assert(base == UsbInstance.Config.BaseAddress);
    if (offset == XUSBPS_IER_OFFSET) return ier;
    if (offset == XUSBPS_EPCR1_OFFSET) return ep_control;
    if (offset == XUSBPS_EPFLUSH_OFFSET || offset == XUSBPS_EPPRIME_OFFSET || offset == XUSBPS_EPRDY_OFFSET) {
        ++busy_reads; return dma_busy ? 0x00020002U : 0U;
    }
    assert(0); return 0;
}
static void XUsbPs_WriteReg(u32 base, u32 offset, u32 value)
{
    assert(base == UsbInstance.Config.BaseAddress);
    if (offset == XUSBPS_IER_OFFSET) { ier = value; return; }
    assert((enabled & USB0_BIT) == 0U && ier == 0U);
    if (offset == XUSBPS_EPCR1_OFFSET) ep_control = value;
    else if (offset == XUSBPS_EPCOMPL_OFFSET) ep_complete &= ~value;
    else assert(offset == XUSBPS_EPFLUSH_OFFSET && value == 0x00020002U);
}
static void XUsbPs_ClrBits(XUsbPs *usb, u32 offset, u32 bits)
{ XUsbPs_WriteReg(usb->Config.BaseAddress, offset, XUsbPs_ReadReg(usb->Config.BaseAddress, offset) & ~bits); }
static int XUsbPs_ReconfigureEp(XUsbPs *usb, XUsbPs_DeviceConfig *cfg, int ep, unsigned direction, int changed)
{
    assert(usb == &UsbInstance && cfg == &usb->DeviceConfig && ep == 1 &&
           (direction == 1U || direction == 2U) && changed == 0);
    assert(!dma_busy && (ep_control & 0x800080U) == 0U && ier == 0U && (enabled & USB0_BIT) == 0U);
    ++reconfigured; return reconfigure_failure;
}
static int UsbGuardedEpPrime(XUsbPs *usb, unsigned ep, unsigned direction)
{
    assert(usb == &UsbInstance && ep == 1U && direction == 2U && !dma_busy);
    assert((ep_complete & 0x00020002U) == 0U && reconfigured == 2U);
    ++primed; return prime_failure;
}
static u32 XUsbPs_ReaddTD(void *dtd, u32 offset)
{ (void)dtd; assert(offset == XUSBPS_dTDTOKEN && rx_received == 0U); return rx_token; }
static int XUsbPs_EpBufferReceive(XUsbPs *usb, unsigned ep, u8 **bytes, u32 *count, u32 *handle)
{ assert(usb == &UsbInstance && ep == 1U); ++rx_received; rx_token = 0U; *bytes = rx_buffer; *count = rx_count; *handle = 77U; return 0; }
static void XUsbPs_EpBufferRelease(u32 handle) { assert(handle == 77U); ++rx_released; }
static void XUsbPs_StorageRxError(XUsbPs *usb) { assert(usb == &UsbInstance); ++rx_errors; }
static void UsbNoteStorageOut(u32 bytes) { assert(bytes == rx_count); ++rx_noted; }
static int UsbStorageQueueEmpty(void) { return 1; }
static int XUsbPs_StorageTryFastRx(XUsbPs *usb, u8 *bytes, u32 count)
{ assert(usb == &UsbInstance && bytes == rx_buffer && count == rx_count); ++rx_admitted; return fast_handles; }
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
    NeedBulkReset = 0U; BulkResetEnableBits = ep_complete = busy_reads = 0U;
    ep_control = 0x00800080U | 0x00010001U;
    reconfigured = primed = 0U; dma_busy = reconfigure_failure = prime_failure = 0;
    rx_token = 0U; rx_received = rx_released = rx_admitted = rx_errors = rx_noted = 0U;
    rx_count = 512U; fast_handles = 1;
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
    /* The actual MSC hook clears old software DATA before the vendor ISR
     * dispatches old completed EP1 RX from the same interrupt. */
    reset_state(1, 0xA5A5U);
    usb_storage_debug_note_msc_reset();
    assert(StorageStats.msc_resets == 1U && class_resets == 1U && StorageReqHead == 0U && StorageReqTail == 0U);
    assert(NeedBulkReset && BulkResetEnableBits == 0x800080U && (ep_control & 0x800080U) == 0U);
    assert(enabled == (USB0_BIT|USB1_BIT) && ier == 0xA5A5U); ++cases;
    /* A second MSC request must not overwrite saved enable bits with zero. */
    usb_storage_debug_note_msc_reset();
    assert(class_resets == 2U && BulkResetEnableBits == 0x800080U); ++cases;
    dma_busy = 1;
    assert(UsbCompleteBulkReset() == XST_FAILURE && busy_reads == 300000U);
    assert(NeedBulkReset && reconfigured == 0U && primed == 0U && (ep_control & 0x800080U) == 0U);
    assert(enabled == (USB0_BIT|USB1_BIT) && ier == 0xA5A5U); ++cases;
    /* Host CLEAR_FEATURE may have cleared halt/toggle bits before poll.
     * Preserve that current controller state, plus foreign endpoint status. */
    dma_busy = 0; ep_control = 0x00040004U; ep_complete = 0x00030003U;
    assert(UsbCompleteBulkReset() == XST_SUCCESS && !NeedBulkReset);
    assert(reconfigured == 2U && primed == 1U && ep_control == 0x00840084U && ep_complete == 0x00010001U);
    assert(enabled == (USB0_BIT|USB1_BIT) && ier == 0xA5A5U); ++cases;
    assert(UsbCompleteBulkReset() == XST_SUCCESS && reconfigured == 2U && primed == 1U); ++cases;
    reset_state(1, 0xA5A5U); usb_storage_debug_note_msc_reset(); prime_failure = 1;
    assert(UsbCompleteBulkReset() == XST_FAILURE && NeedBulkReset && (ep_control & 0x800080U) == 0U); ++cases;
    reset_state(1, 0xA5A5U); usb_storage_debug_note_msc_reset(); reconfigure_failure = 1;
    assert(UsbCompleteBulkReset() == XST_FAILURE && NeedBulkReset && (ep_control & 0x800080U) == 0U);
    assert(primed == 0U); ++cases;
    reset_state(1, 0xA5A5U); usb_storage_debug_note_msc_reset();
    XUsbPs_Ep1EventHandler(&UsbInstance, 1U, XUSBPS_EP_EVENT_DATA_RX, NULL);
    assert(rx_received == 1U && rx_released == 1U && rx_admitted == 0U && rx_noted == 0U && rx_errors == 0U); ++cases;
    for (u32 error = 0x08U; error <= 0x40U; error <<= 1U) {
        if (error == 0x10U) continue; /* 0x10 is not a dTD error flag. */
        reset_state(1, 0xA5A5U); rx_token = error;
        XUsbPs_Ep1EventHandler(&UsbInstance, 1U, XUSBPS_EP_EVENT_DATA_RX, NULL);
        assert(rx_received == 1U && rx_released == 1U && rx_admitted == 0U && rx_noted == 0U && rx_errors == 1U);
        assert(StorageStats.ep1_rx_failures == 1U); ++cases;
    }
    reset_state(1, 0xA5A5U);
    XUsbPs_Ep1EventHandler(&UsbInstance, 1U, XUSBPS_EP_EVENT_DATA_RX, NULL);
    assert(rx_admitted == 1U && rx_released == 1U && rx_noted == 1U && rx_errors == 0U); ++cases;
    reset_state(1, 0xA5A5U); fast_handles = 0; StorageReqHead = 0U; StorageReqTail = USB_STORAGE_REQ_QUEUE_DEPTH-1U;
    XUsbPs_Ep1EventHandler(&UsbInstance, 1U, XUSBPS_EP_EVENT_DATA_RX, NULL);
    assert(StorageStats.queue_drops == 1U && rx_errors == 1U && rx_released == 1U && StorageReqTail == USB_STORAGE_REQ_QUEUE_DEPTH-1U); ++cases;
    reset_state(1, 0xA5A5U); fast_handles = 0; rx_count = 513U;
    XUsbPs_Ep1EventHandler(&UsbInstance, 1U, XUSBPS_EP_EVENT_DATA_RX, NULL);
    assert(StorageStats.queue_drops == 1U && rx_errors == 1U && rx_released == 1U && StorageReqTail == 6U); ++cases;
    reset_state(1, 0xA5A5U); fast_handles = 0; memset(rx_buffer, 0x73, sizeof(rx_buffer));
    XUsbPs_Ep1EventHandler(&UsbInstance, 1U, XUSBPS_EP_EVENT_DATA_RX, NULL);
    assert(StorageReqTail == 7U && StorageStats.queued_packets == 1U && !rx_errors && rx_released == 1U);
    assert(memcmp(StorageReqQueue[6].Data, rx_buffer, 512U) == 0); ++cases;
    printf("PASS %u production USB0 queue cases: reset waits for head commit, nested GIC/IER restore exact state, USB1 remains enabled\n", cases);
    return 0;
}
