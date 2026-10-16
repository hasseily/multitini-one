/* SPDX-License-Identifier: GPL-3.0-only */
/* The runner extracts the production service lifecycle; only hardware,
 * class transport and SD synchronization are modeled here. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usb_storage_service.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition); exit(42); \
} } while (0)
#define XST_SUCCESS 0
#define XST_FAILURE 1
#define XUSBPS_IXR_ALL UINT32_MAX
#define XUSBPS_EP_DIRECTION_OUT 0U
#define USB_STORAGE_IRQ_MASK 0x157U
#define TEST_CPU_FREQUENCY 666666687U
#define COUNTS_PER_SECOND TEST_CPU_FREQUENCY / 2U

typedef uint64_t XTime;
typedef struct { struct { u32 BaseAddress; } Config; } XUsbPs;
typedef struct { u32 Ier, GicEnabled; } UsbStorageIrqState;

XUsbPs UsbInstance;
int UsbStorageStarted,UsbConnected;
int NeedFullReconfigure,NeedEndpointReset,NeedEp0Prime;
uint8_t UsbLocallyDetached,UsbConfiguredOnce,UsbSawPhysicalDisconnect;
uint8_t HostEjectPendingDetach;
XTime HostEjectRequestTick;
u32 StorageReqHead,StorageReqTail;
usb_storage_service_stats_t StorageStats;

static struct {
    bool leased,partial,dma_idle,sd_idle,medium_ready,complete_on_poll,quiesce_aborted;
    int configure_status,prime_status,dma_status,flush_status,sync_status,quiesce_status;
    u32 ier,gic,depth;
    unsigned polls,detaches,dma_checks,flushes,syncs,aborts,complete_writes,quiesces;
    XTime now,poll_ticks;
    char events[256];
    size_t event_count;
} f;

static void event(char value)
{ CHECK(f.event_count+1U<sizeof(f.events)); f.events[f.event_count++]=value; }
bool mt_sd_storage_external_active(void) { return f.leased; }
void XTime_GetTime(XTime *now) { *now=f.now; }
void usleep(unsigned usec)
{
    const uint64_t counts_per_second=COUNTS_PER_SECOND;
    f.now+=(uint64_t)usec*counts_per_second/1000000U;
}
int xil_printf(const char *format,...) { (void)format;return 0; }
static UsbStorageIrqState UsbStorageIrqSave(XUsbPs *usb)
{
    CHECK(usb==&UsbInstance);
    UsbStorageIrqState state={f.ier,f.gic};
    f.ier=f.gic=0U;++f.depth;return state;
}
static void UsbStorageIrqRestore(XUsbPs *usb,UsbStorageIrqState state)
{
    CHECK(usb==&UsbInstance && f.depth!=0U);
    f.ier=state.Ier;f.gic=state.GicEnabled;--f.depth;
}
static int UsbConfigureStorageDevice(XUsbPs *usb,uint32_t delay)
{
    CHECK(usb==&UsbInstance && delay==250000U && f.depth!=0U);
    if(f.configure_status==XST_SUCCESS) {
        f.partial=false;StorageReqHead=StorageReqTail=0U;
    }
    return f.configure_status;
}
void XUsbPs_StorageClearHostEjectRequested(void) { HostEjectPendingDetach=0U; }
void XUsbPs_IntrEnable(XUsbPs *usb,u32 mask)
{ CHECK(usb==&UsbInstance);f.ier|=mask; }
void XUsbPs_IntrDisable(XUsbPs *usb,u32 mask)
{ CHECK(usb==&UsbInstance);f.ier&=~mask; }
void XUsbPs_Start(XUsbPs *usb) { CHECK(usb==&UsbInstance); }
static void UsbSoftConnect(XUsbPs *usb) { CHECK(usb==&UsbInstance);f.dma_idle=false; }
static void UsbSoftDisconnect(XUsbPs *usb)
{ CHECK(usb==&UsbInstance && f.depth!=0U);++f.detaches;event('D'); }
static int UsbGuardedEpPrime(XUsbPs *usb,unsigned ep,unsigned direction)
{ CHECK(usb==&UsbInstance && ep==0U && direction==0U);return f.prime_status; }
static void UsbNoteEpPrime(unsigned ep,unsigned direction,int status)
{ CHECK(ep==0U && direction==0U);(void)status; }
static int UsbDmaStopped(void)
{
    CHECK(f.depth!=0U && f.ier==0U && f.gic==0U);
    ++f.dma_checks;event('I');
    if(f.dma_status==XST_SUCCESS)f.dma_idle=true;
    return f.dma_status;
}
void usb_storage_service_poll(void)
{
    ++f.polls;event('P');f.now+=f.poll_ticks;
    if(f.complete_on_poll) { f.partial=false;StorageReqHead=StorageReqTail; }
}
int XUsbPs_StorageHasPending(void) { return f.partial || f.complete_writes!=0U; }
int usb_storage_medium_ready(void) { return f.medium_ready; }
int usb_storage_sync(void)
{
    CHECK(f.leased && f.dma_idle);++f.syncs;event('S');
    if(!f.medium_ready)return XST_FAILURE;
    CHECK(f.sd_idle);return f.sync_status;
}
int usb_storage_quiesce(u8 *interrupted)
{
    CHECK(f.leased && f.dma_idle && f.depth!=0U && interrupted!=NULL);
    ++f.quiesces;event('Q');*interrupted=0U;
    if(f.quiesce_status!=XST_SUCCESS) {
        f.sd_idle=false;f.medium_ready=false;return f.quiesce_status;
    }
    f.sd_idle=true;
    if(f.quiesce_aborted) { *interrupted=1U;f.medium_ready=false; }
    return XST_SUCCESS;
}
int XUsbPs_StorageFlushPending(void)
{
    CHECK(f.dma_idle && f.depth!=0U);
    ++f.flushes;event('W');
    f.complete_writes=0U;
    return f.flush_status!=XST_SUCCESS ? f.flush_status : usb_storage_sync();
}
void XUsbPs_StorageReset(void)
{
    CHECK(f.dma_idle && f.sd_idle && f.depth!=0U && f.complete_writes==0U);
    if(f.flush_status!=XST_SUCCESS)CHECK(usb_storage_service_transfer_interrupted()!=0U);
    ++f.aborts;event('R');f.partial=false;f.flush_status=XST_SUCCESS;
}

/* These definitions come verbatim from usb_storage_service.c. */
#include "usb_storage_disconnect.inc"

static void reset_model(void)
{
    memset(&f,0,sizeof(f));memset(&StorageStats,0,sizeof(StorageStats));
    f.leased=f.sd_idle=f.medium_ready=true;f.ier=USB_STORAGE_IRQ_MASK;f.gic=1U;
    UsbInstance.Config.BaseAddress=0xE0002000U;
    UsbStorageStarted=UsbConnected=1;
    NeedFullReconfigure=NeedEndpointReset=NeedEp0Prime=0;
    UsbLocallyDetached=UsbConfiguredOnce=UsbSawPhysicalDisconnect=0U;
    HostEjectPendingDetach=0U;HostEjectRequestTick=0U;
    UsbStopRequested=UsbTransferInterrupted=0U;UsbStopRequestTick=0U;
    f.dma_idle=true;UsbResetLocalStorageState();f.dma_idle=false;
    f.aborts=0U;f.event_count=0U;f.events[0]='\0';
}

static XTime deadline(void)
{ const uint64_t counts_per_second=COUNTS_PER_SECOND;return 2U*counts_per_second; }
static void safe_result(void)
{ CHECK(f.leased && f.dma_idle && f.sd_idle && f.depth==0U && f.ier==0U && f.gic==0U); }
static void grace_deadline(void)
{
    reset_model();f.partial=true;
    CHECK(usb_storage_service_disconnect()==1);
    CHECK(f.polls==1U && f.detaches==0U && f.syncs==0U);
    f.now=deadline()-1U;
    CHECK(usb_storage_service_disconnect()==1 && f.detaches==0U);
    ++f.now;
    CHECK(usb_storage_service_disconnect()==0 && f.dma_idle && f.syncs!=0U);
    CHECK(!f.partial && StorageStats.forced_stops==1U);
    CHECK(usb_storage_service_transfer_interrupted()!=0U);
    safe_result();
    /* Work completed by poll counts toward the existing stop deadline. */
    reset_model();f.partial=true;f.poll_ticks=deadline();
    CHECK(usb_storage_service_disconnect()==0 && StorageStats.forced_stops==1U);
    safe_result();
}
static void stale_queue(void)
{
    reset_model();StorageReqHead=5U;StorageReqTail=8U;
    CHECK(usb_storage_service_disconnect()==1);
    f.now=deadline();CHECK(usb_storage_service_disconnect()==0);
    CHECK(StorageReqHead==0U && StorageReqTail==0U && f.aborts==1U);
    CHECK(usb_storage_service_transfer_interrupted()!=0U);
    safe_result();
}
static void dma_never_stops(void)
{
    reset_model();f.partial=true;f.complete_writes=2U;
    CHECK(usb_storage_service_disconnect()==1);
    f.now=deadline();f.dma_status=XST_FAILURE;
    CHECK(usb_storage_service_disconnect()==-1);
    CHECK(f.complete_writes==2U && f.flushes==0U && f.syncs==0U && f.aborts==0U);
    CHECK(f.leased && f.partial && f.depth==0U && f.ier==0U && f.gic==0U);
    f.now+=deadline()*10U;
    CHECK(usb_storage_service_disconnect()==-1 && f.flushes==0U && f.syncs==0U);
    f.dma_status=XST_SUCCESS;
    CHECK(usb_storage_service_disconnect()==0 && f.complete_writes==0U && !f.partial);
    CHECK(usb_storage_service_transfer_interrupted()!=0U && StorageStats.forced_stops==1U);
    safe_result();
}
static void full_write_failure(void)
{
    reset_model();f.complete_writes=2U;f.flush_status=XST_FAILURE;
    CHECK(usb_storage_service_disconnect()==1);
    f.now=deadline();CHECK(usb_storage_service_disconnect()==0);
    CHECK(f.flushes==1U && f.complete_writes==0U && f.aborts==1U && f.syncs==1U);
    CHECK(usb_storage_service_transfer_interrupted()!=0U && f.flush_status==XST_SUCCESS);
    safe_result();
}
static void sync_retry(void)
{
    reset_model();f.sync_status=XST_FAILURE;
    CHECK(usb_storage_service_disconnect()==-1 && f.leased);
    CHECK(StorageStats.stop_sync_failures==1U && f.aborts==1U);
    CHECK(UsbConnected==0 && f.dma_idle && f.ier==0U && f.gic==0U);
    CHECK(usb_storage_service_disconnect()==-1 && f.leased);
    CHECK(StorageStats.stop_sync_failures==2U);
    f.sync_status=XST_SUCCESS;
    CHECK(usb_storage_service_disconnect()==0);
    CHECK(usb_storage_service_transfer_interrupted()!=0U);
    safe_result();
}
static void sd_quiesce_failure_retry(void)
{
    reset_model();f.partial=true;StorageReqHead=5U;StorageReqTail=8U;
    CHECK(usb_storage_service_disconnect()==1);
    f.now=deadline();f.quiesce_status=XST_FAILURE;
    /* The class reports the failed SD operation without touching a buffer
     * still owned by DMA. No class reset or final sync may follow it. */
    f.flush_status=XST_FAILURE;f.sd_idle=false;f.medium_ready=false;
    CHECK(usb_storage_service_disconnect()==-1);
    CHECK(f.dma_idle && !f.sd_idle && f.leased && f.aborts==0U);
    CHECK(f.flushes==1U && f.quiesces==1U && f.syncs==0U);
    CHECK(StorageReqHead==5U && StorageReqTail==8U && f.partial);
    CHECK(usb_storage_service_transfer_interrupted()!=0U);
    CHECK(usb_storage_service_disconnect()==-1 && f.aborts==0U && f.syncs==0U);
    f.quiesce_status=XST_SUCCESS;f.quiesce_aborted=true;
    CHECK(usb_storage_service_disconnect()==0);
    CHECK(f.quiesces==3U && f.aborts==1U && !f.partial && !f.medium_ready);
    CHECK(StorageReqHead==0U && StorageReqTail==0U && f.syncs==0U);
    safe_result();
}
static void sd_aborted_remount(void)
{
    reset_model();f.quiesce_aborted=true;
    CHECK(usb_storage_service_disconnect()==0);
    CHECK(f.quiesces==1U && f.aborts==1U && f.syncs==1U);
    /* Only the flush's healthy pre-reset sync ran. Final sync is skipped
     * after controller reset; local remount performs real initialization. */
    CHECK(!f.medium_ready && usb_storage_service_transfer_interrupted()!=0U);
    CHECK(f.events[f.event_count-1U]=='R');
    safe_result();
}
static void sticky_error_recorded(void)
{
    /* A completed failed write can leave only the sticky async error;
     * no partial command should be needed to mark the interruption. */
    reset_model();f.flush_status=XST_FAILURE;
    CHECK(usb_storage_service_disconnect()==0);
    CHECK(StorageStats.forced_stops==0U && f.flushes==1U && f.aborts==1U && f.syncs==1U);
    CHECK(usb_storage_service_transfer_interrupted()!=0U && f.flush_status==XST_SUCCESS);
    safe_result();
}
static void normal_completion(void)
{
    reset_model();
    CHECK(usb_storage_service_disconnect()==0);
    CHECK(StorageStats.forced_stops==0U && usb_storage_service_transfer_interrupted()==0U);
    safe_result();
    reset_model();f.partial=true;
    CHECK(usb_storage_service_disconnect()==1);
    f.now=deadline()/2U;f.complete_on_poll=true;
    CHECK(usb_storage_service_disconnect()==0 && StorageStats.forced_stops==0U);
    CHECK(usb_storage_service_transfer_interrupted()==0U);
    safe_result();
}
static void connect_resets_deadline(void)
{
    reset_model();UsbConnected=0;UsbStopRequested=1U;
    UsbTransferInterrupted=1U;UsbStopRequestTick=123U;f.now=deadline()*20U;
    CHECK(usb_storage_service_connect()==XST_SUCCESS);
    CHECK(UsbStopRequested==0U && UsbStopRequestTick==0U);
    CHECK(usb_storage_service_transfer_interrupted()==0U && UsbConnected==1);
    f.partial=true;CHECK(usb_storage_service_disconnect()==1);
    const XTime start=f.now;
    f.now=start+deadline()-1U;CHECK(usb_storage_service_disconnect()==1);
    ++f.now;CHECK(usb_storage_service_disconnect()==0);
    safe_result();
}
static void active_connect_preserves_deadline(void)
{
    reset_model();f.partial=true;f.now=1234U;
    CHECK(usb_storage_service_disconnect()==1);
    CHECK(usb_storage_service_connect()==XST_SUCCESS);
    CHECK(UsbStopRequested==1U && UsbStopRequestTick==1234U);
    f.now+=deadline();CHECK(usb_storage_service_disconnect()==0);
    safe_result();
}
static void connect_failures(void)
{
    reset_model();UsbConnected=0;UsbStopRequested=UsbTransferInterrupted=1U;
    UsbStopRequestTick=123U;f.configure_status=XST_FAILURE;
    CHECK(usb_storage_service_connect()==XST_FAILURE);
    CHECK(UsbStopRequested==0U && UsbStopRequestTick==0U && UsbTransferInterrupted==0U);
    CHECK(f.leased && f.depth==0U && f.ier==0U);
    f.configure_status=XST_SUCCESS;f.prime_status=XST_FAILURE;
    CHECK(usb_storage_service_connect()==XST_FAILURE && UsbConnected==0 && f.leased);
    f.prime_status=XST_SUCCESS;CHECK(usb_storage_service_connect()==XST_SUCCESS);
    CHECK(f.ier==USB_STORAGE_IRQ_MASK && f.gic!=0U && f.depth==0U);
}
static void tick_wrap_and_missing_controller(void)
{
    reset_model();f.partial=true;f.now=UINT64_MAX-deadline()/2U;
    CHECK(usb_storage_service_disconnect()==1);
    f.now+=deadline()-1U;CHECK(usb_storage_service_disconnect()==1);
    ++f.now;CHECK(usb_storage_service_disconnect()==0);safe_result();
    reset_model();UsbInstance.Config.BaseAddress=0U;UsbStorageStarted=UsbConnected=0;
    f.dma_idle=true;
    CHECK(usb_storage_service_disconnect()==0 && f.detaches==0U && f.dma_checks==0U);
    safe_result();
    reset_model();f.leased=false;
    CHECK(usb_storage_service_disconnect()==-1 && f.detaches==0U && f.syncs==0U);
}

int main(int argc,char **argv)
{
    const struct { const char *name;void (*run)(void); } cases[]={
        {"grace_deadline",grace_deadline},{"stale_queue",stale_queue},
        {"dma_never_stops",dma_never_stops},{"full_write_failure",full_write_failure},
        {"sync_retry",sync_retry},{"normal_completion",normal_completion},
        {"sd_quiesce_failure_retry",sd_quiesce_failure_retry},
        {"sd_aborted_remount",sd_aborted_remount},
        {"sticky_error_recorded",sticky_error_recorded},
        {"connect_resets_deadline",connect_resets_deadline},
        {"active_connect_preserves_deadline",active_connect_preserves_deadline},
        {"connect_failures",connect_failures},{"tick_wrap_and_missing_controller",tick_wrap_and_missing_controller}
    };
    unsigned ran=0U;
    for(size_t index=0U;index<sizeof(cases)/sizeof(cases[0]);++index) {
        if(argc>1 && strcmp(argv[1],cases[index].name)!=0)continue;
        cases[index].run();++ran;printf("PASS %s\n",cases[index].name);
    }
    CHECK(ran!=0U);
    printf("PASS production USB SD disconnect: %u lifecycle groups\n",ran);
    return 0;
}
