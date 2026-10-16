/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb_storage_backend.h"
#include "sd_storage.h"
#include "diskio.h"
#include "xiltimer.h"
#include "xstatus.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
unsigned test_cpsr;
static bool leased,lost,sync_failed,io_failed,initialized,controller_fault,quiesce_failed;
static unsigned initializations,reads,writes,syncs,status_calls,last_block,quiesces;
bool mt_sd_storage_external_active(void) { return leased; }
void XTime_GetTime(XTime *p) { static XTime now;*p=++now; }
void usleep(unsigned usec) { (void)usec; }
int xil_printf(const char *format,...) { (void)format;return 0; }
DSTATUS disk_initialize(BYTE drive)
{ assert(drive==0U && !controller_fault);++initializations;initialized=!lost;return lost ? STA_NOINIT:0U; }
DSTATUS disk_status(BYTE drive)
{ assert(drive==0U);++status_calls;return lost ? STA_NODISK:(initialized ? 0U:STA_NOINIT); }
DRESULT disk_ioctl(BYTE drive,BYTE command,void *data)
{
    assert(leased && drive==0U && (test_cpsr&31U)!=18U);
    if(command==MT_SD_CTRL_QUIESCE) {
        ++quiesces;
        if(quiesce_failed)return RES_ERROR;
        *(BYTE *)data=controller_fault ? MT_SD_QUIESCE_RESET:0U;
        if(controller_fault)initialized=false;
        controller_fault=false;return RES_OK;
    }
    if(command==GET_SECTOR_COUNT) { *(DWORD *)data=8192U;return RES_OK; }
    assert(command==CTRL_SYNC);++syncs;return sync_failed ? RES_ERROR:RES_OK;
}
DRESULT disk_read(BYTE drive,BYTE *data,LBA_t block,UINT count)
{
    assert(leased && drive==0U && (test_cpsr&31U)!=18U);
    ++reads;last_block=block;memset(data,0x55,(size_t)count*512U);
    if(block==0U) { /* An MBR must remain visible; no partition remapping. */
        memset(data,0,512U);data[510]=0x55U;data[511]=0xaaU;
        data[0x1beU+4U]=0x0cU;data[0x1beU+9U]=8U;data[0x1beU+13U]=16U;
    }
    if(io_failed) { initialized=false;controller_fault=true;return RES_ERROR; }
    return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t block,UINT count)
{
    assert(leased && drive==0U && data!=NULL && count!=0U && (test_cpsr&31U)!=18U);
    ++writes;last_block=block;
    if(io_failed) { initialized=false;controller_fault=true;return RES_ERROR; }
    return RES_OK;
}
int main(void)
{
    uint8_t data[512]={0};
    assert(usb_storage_init()!=XST_SUCCESS && initializations==0U);
    assert(usb_storage_sync()!=XST_SUCCESS && syncs==0U);
    leased=true;test_cpsr=18U;
    assert(usb_storage_init()!=XST_SUCCESS && initializations==0U);
    test_cpsr=31U;
    assert(usb_storage_init()==XST_SUCCESS && usb_storage_medium_ready() && usb_storage_block_count()==8192U);
    assert(usb_storage_read(123U,1U,data)==XST_SUCCESS && last_block==123U && data[0]==0x55U);
    assert(usb_storage_write(124U,1U,data)==XST_SUCCESS && last_block==124U && writes==1U);
    const unsigned read_before=reads,write_before=writes;
    assert(usb_storage_read(8192U,1U,data)!=XST_SUCCESS);
    assert(usb_storage_write(8191U,2U,data)!=XST_SUCCESS && reads==read_before && writes==write_before);
    test_cpsr=18U;assert(usb_storage_read(1U,1U,data)!=XST_SUCCESS && reads==read_before);
    assert(usb_storage_write(1U,1U,data)!=XST_SUCCESS && writes==write_before);test_cpsr=31U;
    sync_failed=true;assert(usb_storage_sync()!=XST_SUCCESS && syncs==1U);
    sync_failed=false;assert(usb_storage_sync()==XST_SUCCESS && syncs==2U);
    /* A still-present card fault must retire DMA before its buffer returns,
     * invalidate ready state and force real disk_initialize on recovery. */
    io_failed=true;const unsigned quiet_before=quiesces,init_before=initializations;
    assert(usb_storage_write(124U,1U,data)!=XST_SUCCESS);
    assert(quiesces==quiet_before+1U && !controller_fault && !usb_storage_medium_ready());
    assert(usb_storage_sync()!=XST_SUCCESS && syncs==2U);
    u8 interrupted=0U;
    assert(usb_storage_quiesce(&interrupted)==XST_SUCCESS && interrupted==1U);
    assert(usb_storage_quiesce(&interrupted)==XST_SUCCESS && interrupted==0U);
    io_failed=false;
    assert(usb_storage_try_reinit()==XST_SUCCESS && initializations==init_before+1U && usb_storage_medium_ready());
    /* Reset failure preserves the old DMA buffer and prevents probe/reinit. */
    io_failed=quiesce_failed=true;
    assert(usb_storage_read(123U,1U,data)!=XST_SUCCESS && controller_fault && !usb_storage_medium_ready());
    const unsigned blocked_reads=reads,blocked_writes=writes,blocked_inits=initializations;
    assert(usb_storage_read(123U,1U,data)!=XST_SUCCESS && reads==blocked_reads);
    assert(usb_storage_write(124U,1U,data)!=XST_SUCCESS && writes==blocked_writes);
    assert(usb_storage_try_reinit()!=XST_SUCCESS && initializations==blocked_inits && reads==blocked_reads);
    assert(usb_storage_quiesce(&interrupted)!=XST_SUCCESS && interrupted==0U && controller_fault);
    quiesce_failed=io_failed=false;
    assert(usb_storage_quiesce(&interrupted)==XST_SUCCESS && interrupted==1U && !controller_fault);
    assert(!usb_storage_medium_ready() && usb_storage_sync()!=XST_SUCCESS);
    assert(usb_storage_try_reinit()==XST_SUCCESS && usb_storage_medium_ready());
    io_failed=lost=true;assert(usb_storage_read(1U,1U,data)!=XST_SUCCESS && status_calls!=0U && !usb_storage_medium_ready());
    io_failed=lost=false;assert(usb_storage_try_reinit()==XST_SUCCESS && usb_storage_medium_ready());
    leased=false;
    assert(!usb_storage_medium_ready() && usb_storage_block_count()==0U);
    const unsigned after_reads=reads;
    assert(usb_storage_read(1U,1U,data)!=XST_SUCCESS && reads==after_reads);
    assert(usb_storage_sync()!=XST_SUCCESS && syncs==2U);
    puts("PASS USB SD physical-sector routing, exclusive lease, IRQ guard, bounds, sync errors and card recovery");
    return 0;
}
