/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;typedef u64 XTime;
typedef struct { void *dQH; } XUsbPs_EpOut;
typedef struct {
    struct { u32 BaseAddress; } Config;
    struct { struct { XUsbPs_EpOut Out; } Ep[1]; } DeviceConfig;
} XUsbPs;
typedef struct { u32 tag; } USB_CBW;
typedef struct { u32 residue;u8 status; } USB_CSW;
typedef struct { u8 Data[512]; } usb_write_pipe_slot_t;
#define USB_STORAGE_BLOCK_SIZE 512U
#define USB_WRITE_PIPE_DEPTH 1U
#define USB_EP_STATE_COMMAND 0
#define USB_RBC_WRITE 0x2aU
#define USB_RBC_READ 0x28U
#define USB_RBC_STARTSTOP_UNIT 0x1bU
#define XST_SUCCESS 0
#define XUSBPS_ISR_OFFSET 0U
#define XUSBPS_EPSTAT_OFFSET 1U
#define XUSBPS_IXR_UR_MASK 0x40U
#define XUSBPS_IXR_PC_MASK 0x04U
#define XUSBPS_dQHSUB0 0U
#define XUSBPS_dQHSUB1 1U
#define XUSBPS_REQ_TYPE_MASK 0x60U
#define XUSBPS_CMD_CLASSREQ 0x20U
#define XUSBPS_CMD_STDREQ 0U
#define XUSBPS_CLASSREQ_MASS_STORAGE_RESET 0xffU
#define XUSBPS_REQ_SET_ADDRESS 5U
#define XUSBPS_REQ_SET_CONFIGURATION 9U
#define XUSBPS_REQ_SET_INTERFACE 11U
static usb_write_pipe_slot_t WritePipe[USB_WRITE_PIPE_DEPTH];
static u32 activeWriteSlot,writeBlockFill,writeBlockCount,writeBlock;
static int writeFailed,rxBytesLeft,phase;
static USB_CBW lastCBW;
static XTime writeRxStartTick,writeCommandTick;
static struct { u64 write_overflows,write_received_bytes;u32 csw_sends,last_status_residue,last_status_code,csw_send_failures; } ClassStats;
static unsigned commits,flushes,releases,replies,reply_error,senses;
static int flush_error,inject_reset,tripwire_busy;
static u32 usb_regs[2],setup_words[2],cswPending,cswQueued;
static USB_CSW statusCSW,pendingCSW;
static u32 XUsbPs_ReadReg(u32 base,u32 offset) { (void)base;return usb_regs[offset]; }
static void XUsbPs_SetSetupTripwire(XUsbPs *p) { (void)p; }
static void XUsbPs_ClrSetupTripwire(XUsbPs *p) { (void)p; }
static int XUsbPs_SetupTripwireIsSet(XUsbPs *p) { (void)p;return !tripwire_busy; }
static void XUsbPs_dQHInvalidateCache(void *p) { (void)p; }
static u32 XUsbPs_ReaddQH(void *p,u32 offset) { (void)p;return setup_words[offset]; }
static void XTime_GetTime(XTime *p) { static XTime now;*p=++now; }
static void WritePipeCommitActive(u32 block,u32 whole,u32 bytes)
{ assert(block==123U && whole==1U && bytes==512U);++commits;activeWriteSlot=USB_WRITE_PIPE_DEPTH; }
static void WritePipeReleaseActive(void) { ++releases;activeWriteSlot=USB_WRITE_PIPE_DEPTH; }
static int XUsbPs_StorageFlushPending(void)
{
    assert(commits==1U);++flushes;
    if(inject_reset==1)usb_regs[XUSBPS_ISR_OFFSET]=XUSBPS_IXR_UR_MASK;
    if(inject_reset==2)usb_regs[XUSBPS_ISR_OFFSET]=XUSBPS_IXR_PC_MASK;
    if(inject_reset>=3) {
        usb_regs[XUSBPS_EPSTAT_OFFSET]=1U;
        setup_words[0]=inject_reset==3 ? 0xff21U :
            inject_reset==4 ? 0x0900U : inject_reset==5 ? 0x0080U : 0x0b01U;
    }
    return flush_error;
}
#define SENSE_MEDIUM_WRITE_ERROR() (++senses)
static void stats_note_write_done(u32 bytes,u64 rx,u64 total,int failed)
{ (void)bytes;(void)rx;(void)total;(void)failed; }
static USB_CSW *BuildStatus(const USB_CBW *cbw,u32 residue,u8 failed)
{ (void)cbw;statusCSW.residue=residue;statusCSW.status=failed;return &statusCSW; }
static int StorageEp1Send(XUsbPs *p,const u8 *data,u32 bytes)
{
    const USB_CSW *status=(const USB_CSW *)data;(void)p;assert(bytes==sizeof(*status));
    ++replies;reply_error=status->status;
    if(status->status==0U)assert(commits==1U && flushes==1U);
    return XST_SUCCESS;
}
#include "usb_storage_handlers.inc"
static void reset(void)
{
    activeWriteSlot=writeBlockFill=0U;writeBlockCount=1U;writeBlock=123U;
    writeFailed=0;rxBytesLeft=512;phase=1;writeRxStartTick=writeCommandTick=0U;
    commits=flushes=releases=replies=reply_error=senses=0U;flush_error=0;
    inject_reset=tripwire_busy=0;memset(usb_regs,0,sizeof(usb_regs));
    memset(setup_words,0,sizeof(setup_words));cswPending=cswQueued=0U;
}
int main(void)
{
    XUsbPs usb={0};u8 bytes[512];memset(bytes,0x73,sizeof(bytes));
    assert(StorageOpcodeNeedsDefer(USB_RBC_WRITE) && StorageOpcodeNeedsDefer(USB_RBC_READ));
    assert(StorageOpcodeNeedsDefer(0x35U) && StorageOpcodeNeedsDefer(USB_RBC_STARTSTOP_UNIT));
    assert(!StorageOpcodeNeedsDefer(0x12U)); /* INQUIRY stays fast and never touches SD. */
    reset();StorageHandleWriteData(&usb,bytes,256U);
    assert(replies==0U && commits==0U && flushes==0U && rxBytesLeft==256);
    StorageHandleWriteData(&usb,bytes+256U,256U);
    assert(replies==1U && reply_error==0U && phase==USB_EP_STATE_COMMAND && memcmp(WritePipe[0].Data,bytes,512U)==0);
    reset();flush_error=1;StorageHandleWriteData(&usb,bytes,512U);
    assert(replies==1U && reply_error==1U && senses==1U && commits==1U && flushes==1U);
    reset();writeBlockCount=2U;StorageHandleWriteData(&usb,bytes,512U);
    assert(replies==1U && reply_error==1U && commits==0U && flushes==0U && releases==1U);
    for(int kind=1;kind<=6;++kind) {
        reset();inject_reset=kind;StorageHandleWriteData(&usb,bytes,512U);
        assert(commits==1U && flushes==1U);
        if(kind==5)assert(replies==1U && cswPending==1U); /* GET_STATUS does not obsolete BOT. */
        else assert(replies==0U && cswPending==0U);
        if(kind>=3)assert(usb_regs[XUSBPS_EPSTAT_OFFSET]==1U); /* Leave SETUP for EP0 ISR. */
    }
    reset();usb_regs[XUSBPS_EPSTAT_OFFSET]=1U;tripwire_busy=1;
    assert(StorageResetPending(&usb)); /* Changing SETUP fails closed after bounded retries. */
    puts("PASS production USB WRITE: partial transfer unacknowledged, SD+sync precede success, reset suppresses obsolete CSW without consuming EP0 SETUP");
    return 0;
}
