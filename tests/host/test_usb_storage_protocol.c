/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef u64 XTime;
typedef struct { struct { u32 BaseAddress; } Config; } XUsbPs;
typedef struct { unsigned bRequest; } XUsbPs_SetupData;
typedef struct __attribute__((packed)) {
    u32 dCBWSignature, dCBWTag, dCBWDataTransferLength;
    u8 bmCBWFlags, cCBWLUN, bCBWCBLength, CBWCB[16];
} USB_CBW;
typedef struct __attribute__((packed)) { u8 opcode,flags; u32 block; u8 group; u16 length; u8 control; } SCSI_READ_WRITE;
#define USB_STORAGE_BLOCK_SIZE 512U
#define USB_STORAGE_MAX_TRANSFER_BLOCKS 256U
#define USB_STORAGE_MAX_TRANSFER_BYTES (256U * 512U)
#define USB_WRITE_PIPE_DEPTH 16U
#define USB_WRITE_PIPE_INVALID USB_WRITE_PIPE_DEPTH
#define USB_CBW_SIGNATURE 0x43425355U
#define USB_EP_STATE_COMMAND 0
#define USB_EP_STATE_DATA 1
#define USB_RBC_WRITE 0x2aU
#define USB_RBC_READ 0x28U
#define USB_RBC_STARTSTOP_UNIT 0x1bU
#define XUSBPS_CLASSREQ_MASS_STORAGE_RESET 0xffU
#define XUSBPS_CLASSREQ_GET_MAX_LUN 0xfeU
#define XUSBPS_EP_DIRECTION_IN 1U
#define XUSBPS_EP_DIRECTION_OUT 2U
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XUSBPS_EPFLUSH_OFFSET 1U
#define XUSBPS_EPPRIME_OFFSET 2U
#define XUSBPS_EPRDY_OFFSET 3U
#define Xil_AssertVoid assert
#define be2le(x) (x)
static u32 htonl(u32 x) { return ((x & 255U)<<24)|((x & 0xff00U)<<8)|((x >> 8)&0xff00U)|(x >> 24); }
static u16 htons(u16 x) { return (u16)((x << 8)|(x >> 8)); }
typedef struct { u8 Data[USB_STORAGE_MAX_TRANSFER_BYTES]; u32 Block,Blocks,Bytes,Pending; } usb_write_pipe_slot_t;
static usb_write_pipe_slot_t WritePipe[USB_WRITE_PIPE_DEPTH];
static USB_CBW lastCBW;
static u32 writeBlock, writeBlockCount, writeBlockFill, activeWriteSlot = USB_WRITE_PIPE_INVALID;
static u32 writePipeHead, writePipeTail, writePipeQueued, asyncWriteFailed, botResetRequired;
static u32 cswPending, cswQueued;
static int rxBytesLeft, phase, writeFailed;
static XTime writeCommandTick, writeRxStartTick;
static u8 MaxLUN;
static struct { unsigned ep1_protocol_stalls, write_async_failures, write_overflows; u64 write_received_bytes; } ClassStats;
static unsigned in_stalls, out_stalls, sd_writes, sd_syncs, replies, failed_replies;
static unsigned handled, resets, deferred_packets, ep0_replies, senses;
static unsigned expect_blocks, expect_seed;
static int sd_error;
static u32 dma_status, flush_reads, flush_writes;
static u32 XUsbPs_ReadReg(u32 base, u32 offset)
{ assert(base == 0U && offset >= 1U && offset <= 3U); ++flush_reads; return dma_status; }
static void XUsbPs_WriteReg(u32 base, u32 offset, u32 bits)
{ assert(base == 0U && offset == 1U && bits == 0x20000U); ++flush_writes; }
static void XUsbPs_StorageReset(void);
static void SENSE_CLEAR(void) { senses = 0U; }
#define SENSE_MEDIUM_WRITE_ERROR() (++senses)
#define SENSE_NOT_READY_NO_MEDIUM() (++senses)
#define SENSE_LBA_OUT_OF_RANGE() (++senses)
static u32 WritePipeIrqSave(void) { return 0U; }
static void WritePipeIrqRestore(u32 state) { (void)state; }
static void stats_update_pending_slots(void) {}
static void stats_note_write_command(u32 blocks, u32 bytes)
{ assert(bytes == blocks*512U); writeCommandTick = 1U; }
static void stats_note_write_done(u32 bytes, u64 rx, u64 total, int failed)
{ (void)bytes; (void)rx; (void)total; (void)failed; }
static void XTime_GetTime(XTime *t) { static XTime now; *t = ++now; }
static int usb_storage_medium_ready(void) { return 1; }
static u32 usb_storage_block_count(void) { return 100000U; }
static int usb_storage_write(u32 block, u32 blocks, const u8 *bytes)
{
    assert(block == 123U && blocks == expect_blocks);
    for (u32 i = 0; i < blocks*512U; ++i) assert(bytes[i] == (u8)(i + expect_seed));
    ++sd_writes; return sd_error;
}
static int usb_storage_sync(void) { ++sd_syncs; return 0; }
static void SendStatus(XUsbPs *usb, const USB_CBW *cbw, u32 residue, u8 status)
{
    (void)usb; (void)cbw; assert(residue == 0U);
    ++replies; failed_replies += status != 0U;
    if (!status) assert(sd_writes != 0U && sd_syncs == sd_writes);
}
static void XUsbPs_EpStall(XUsbPs *usb, unsigned ep, unsigned direction)
{ (void)usb; assert(ep == 1U || ep == 0U); if (direction == 1U) ++in_stalls; else { assert(direction == 2U); ++out_stalls; } }
static int StorageEp0Send(XUsbPs *usb, const u8 *bytes, u32 count)
{ (void)usb; (void)bytes; assert(count <= 1U); ++ep0_replies; return 0; }
static void usb_storage_debug_note_msc_reset(void)
{ ++resets; deferred_packets = 0U; XUsbPs_StorageReset(); }
static void usb_storage_debug_note_ep_send_failure(unsigned ep, u32 count, int status)
{ (void)ep; (void)count; (void)status; assert(0); }
static void usb_storage_debug_note_ch9_error(const XUsbPs_SetupData *setup) { (void)setup; }
static void usb_storage_debug_note_ep0_stall(void) {}
static void XUsbPs_HandleStorageReq(XUsbPs *usb, unsigned ep, u8 *bytes, u32 count)
{ (void)usb; (void)bytes; assert(ep == 1U && count == 31U); ++handled; }
#include "usb_storage_protocol.inc"
static USB_CBW command(u32 blocks)
{
    USB_CBW cbw = {0};
    cbw.dCBWSignature = USB_CBW_SIGNATURE; cbw.bCBWCBLength = 10U;
    cbw.dCBWDataTransferLength = blocks*512U;
    SCSI_READ_WRITE rw = {USB_RBC_WRITE,0,0,0,0,0};
    rw.block = htonl(123U); rw.length = htons((u16)blocks);
    memcpy(cbw.CBWCB, &rw, sizeof(rw)); return cbw;
}
static void reset(void)
{
    writePipeHead = writePipeTail = writePipeQueued = asyncWriteFailed = 0U;
    in_stalls = out_stalls = sd_writes = sd_syncs = replies = failed_replies = 0U;
    handled = resets = deferred_packets = ep0_replies = senses = 0U; sd_error = 0;
    memset(&ClassStats, 0, sizeof(ClassStats)); memset(WritePipe, 0, sizeof(WritePipe));
    XUsbPs_StorageReset();
}
int main(void)
{
    XUsbPs usb = {0}; USB_CBW valid = command(256U); u8 data[512]; unsigned cases = 0U;
    assert(sizeof(valid) == 31U);
    for (unsigned invalid = 0U; invalid < 10U; ++invalid) {
        reset(); USB_CBW cbw = valid; const u8 *p = (const u8 *)&cbw; u32 len = sizeof(cbw);
        switch (invalid) {
        case 0: p = NULL; len = 0; break;
        case 1: len = 1U; break;
        case 2: len = 30U; break;
        case 3: p = data; len = 512U; memcpy(data, &cbw, sizeof(cbw)); break;
        case 4: cbw.dCBWSignature ^= 1U; break;
        case 5: cbw.bmCBWFlags = 1U; break;
        case 6: cbw.cCBWLUN = 1U; break;
        case 7: cbw.cCBWLUN = 16U; break;
        case 8: cbw.bCBWCBLength = 0U; break;
        case 9: cbw.bCBWCBLength = 17U; break;
        }
        assert(!StorageValidateCbw(&usb, p, len));
        assert(in_stalls == 1U && out_stalls == 1U && cswPending == 0U && botResetRequired);
        assert(XUsbPs_StorageTryFastRx(&usb, (u8 *)&valid, sizeof(valid)) == 1);
        assert(in_stalls == 1U && handled == 0U && sd_writes == 0U && replies == 0U);
        XUsbPs_SetupData setup = {0xffU}; XUsbPs_ClassReq_Storage(&usb, &setup);
        assert(!botResetRequired && ep0_replies == 1U && resets == 1U);
        assert(StorageValidateCbw(&usb, (const u8 *)&valid, sizeof(valid))); ++cases;
    }
    /* Real production allocation/RX/commit/flush across every circular slot. */
    reset(); expect_blocks = 256U;
    for (unsigned cmd = 0; cmd < 20U; ++cmd) {
        expect_seed = cmd;
        assert(XUsbPs_StorageTryFastRx(&usb, (u8 *)&valid, sizeof(valid)) == 0);
        assert(StorageStartWrite(&usb, &valid, 1));
        for (unsigned sector = 0; sector < 256U; ++sector) {
            for (unsigned i = 0; i < 512U; ++i) data[i] = (u8)(sector*512U+i+expect_seed);
            assert(XUsbPs_StorageTryFastRx(&usb, data, sizeof(data)) == 0);
            StorageHandleWriteData(&usb, data, sizeof(data));
            if (sector != 255U) assert(replies == cmd && sd_writes == cmd);
        }
        assert(replies == cmd+1U && sd_writes == cmd+1U && sd_syncs == cmd+1U);
        assert(!XUsbPs_StorageHasPending() && !failed_replies); ++cases;
    }
    /* Abandoning a partial command never acknowledges or writes its bytes. */
    reset(); expect_blocks = 256U; assert(StorageStartWrite(&usb, &valid, 1));
    memset(data, 0x73, sizeof(data)); StorageHandleWriteData(&usb, data, 512U);
    assert(XUsbPs_StorageHasPending()); deferred_packets = 15U;
    XUsbPs_SetupData setup = {0xffU}; XUsbPs_ClassReq_Storage(&usb, &setup);
    assert(!XUsbPs_StorageHasPending() && deferred_packets == 0U && !replies && !sd_writes); ++cases;
    /* Fully received committed slots survive transport reset and drain once. */
    reset(); expect_blocks = 1U; expect_seed = 0U; assert(WritePipeAllocSlot(1) >= 0);
    for (unsigned i = 0; i < 512U; ++i) WritePipe[activeWriteSlot].Data[i] = (u8)i;
    WritePipeCommitActive(123U, 1U, 512U); XUsbPs_StorageReset();
    assert(writePipeQueued == 1U && XUsbPs_StorageHasPending());
    assert(XUsbPs_StorageFlushPending() == XST_SUCCESS && sd_writes == 1U && sd_syncs == 1U);
    assert(!XUsbPs_StorageHasPending() && replies == 0U); ++cases;
    /* A failed SD write cannot produce a success CSW or an apparent clean sync. */
    reset(); expect_blocks = 1U; expect_seed = 0U; sd_error = 1; USB_CBW one = command(1U);
    assert(StorageStartWrite(&usb, &one, 1)); for (unsigned i=0;i<512U;++i) data[i]=(u8)i;
    StorageHandleWriteData(&usb, data, 512U);
    assert(replies == 1U && failed_replies == 1U && asyncWriteFailed && sd_syncs == 0U);
    assert(XUsbPs_StorageFlushPending() != XST_SUCCESS); ++cases;
    reset(); assert(StorageStartWrite(&usb, &valid, 1));
    XUsbPs_StorageRxError(&usb);
    assert(botResetRequired && XUsbPs_StorageHasPending() && in_stalls == 1U && out_stalls == 1U);
    assert(XUsbPs_StorageTryFastRx(&usb, data, sizeof(data)) == 1);
    assert(replies == 0U && sd_writes == 0U && writeBlockFill == 0U && handled == 0U);
    XUsbPs_ClassReq_Storage(&usb, &setup);
    assert(!botResetRequired && !XUsbPs_StorageHasPending()); ++cases;
    /* Small read-only status commands retain the ISR fast path. */
    reset(); USB_CBW inquiry = command(0U); inquiry.CBWCB[0] = 0x12U;
    assert(XUsbPs_StorageTryFastRx(&usb, (u8 *)&inquiry, sizeof(inquiry)) == 1 && handled == 1U); ++cases;
    dma_status = 0x20000U; flush_reads = flush_writes = 0U;
    assert(StorageFlushEp1In(&usb) == XST_FAILURE && flush_reads == 300000U && flush_writes == 1U); ++cases;
    dma_status = 0x10001U; flush_reads = flush_writes = 0U;
    assert(StorageFlushEp1In(&usb) == XST_SUCCESS && flush_reads == 3U && flush_writes == 1U); ++cases;
    printf("PASS %u production BOT cases: malformed/stale DATA requires reset,20x128KiB writes exact,partial abort unacknowledged,completed writes survive reset\n", cases);
    return 0;
}
