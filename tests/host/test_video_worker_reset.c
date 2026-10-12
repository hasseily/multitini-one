/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#define MT_VIDEO_WORKER_HOST_TEST 1
/* Include the real worker so the private release helper is exercised without
 * copying code into a fixture or mapping the board's physical DDR addresses. */
#include "../../firmware/platform/xilinx/video_worker.c"

const unsigned char mt_video_core1_blob_start[1]={0};
const unsigned char mt_video_core1_blob_end[1]={0};
static mt_video_worker_mailbox_t test_mailbox;
static uint8_t test_private[16];
static uint32_t reset_control,write_count,control_count,ignore_control_write;
static struct { uintptr_t address; uint32_t value; } writes[16];

uint32_t Xil_In32(uintptr_t address)
{
    assert(address==MT_SLCR_A9_CPU_RESET); return reset_control;
}
void Xil_Out32(uintptr_t address,uint32_t value)
{
    assert(write_count<16); writes[write_count].address=address;
    writes[write_count++].value=value;
    if(address==MT_SLCR_A9_CPU_RESET && ++control_count!=ignore_control_write)
        reset_control=value;
}
void Xil_DCacheFlushRange(INTPTR address,u32 size) { (void)address; (void)size; }
void Xil_SetTlbAttributes(uintptr_t address,uint32_t attributes) { (void)address; (void)attributes; }
void XTime_GetTime(XTime *value) { *value=0; }

static void reset_fixture(uint32_t control)
{
    memset(&test_mailbox,0,sizeof test_mailbox); mailbox=&test_mailbox;
    memset(test_private,0xC3,sizeof test_private);
    started=true; timed_out=false; held_for_reset=false; accepting=true;
    test_worker_entry=(uintptr_t)test_private; test_worker_size=1;
    reset_control=control; write_count=control_count=ignore_control_write=0;
}
static void expect_write(unsigned index,uintptr_t address,uint32_t value)
{
    assert(index<write_count && writes[index].address==address && writes[index].value==value);
}
int main(void)
{
    reset_fixture(0x100U);
    test_mailbox.request_sequence=2; test_mailbox.complete_sequence=1;
    assert(!mt_video_worker_hold_for_reset() && !write_count && !accepting);
    test_mailbox.complete_sequence=2;
    assert(mt_video_worker_hold_for_reset());
    assert(held_for_reset && !started && reset_control==0x122U && write_count==4);
    expect_write(0,MT_SLCR_UNLOCK,MT_SLCR_UNLOCK_KEY);
    expect_write(1,MT_SLCR_A9_CPU_RESET,0x102U);
    expect_write(2,MT_SLCR_A9_CPU_RESET,0x122U);
    expect_write(3,MT_SLCR_LOCK,MT_SLCR_LOCK_KEY);
    write_count=control_count=0;
    assert(cpu1_release() && reset_control==0x100U && write_count==4);
    expect_write(0,MT_SLCR_UNLOCK,MT_SLCR_UNLOCK_KEY);
    expect_write(1,MT_SLCR_A9_CPU_RESET,0x120U);
    expect_write(2,MT_SLCR_A9_CPU_RESET,0x100U);
    expect_write(3,MT_SLCR_LOCK,MT_SLCR_LOCK_KEY);
    write_count=0; assert(cpu1_release() && !write_count); /* cold boot */
    reset_fixture(0x100U); ignore_control_write=1;
    assert(!mt_video_worker_hold_for_reset() && write_count==3 && !started);
    assert(reset_control==0x100U && held_for_reset);
    expect_write(2,MT_SLCR_LOCK,MT_SLCR_LOCK_KEY);
    ignore_control_write=0; write_count=control_count=0;
    assert(mt_video_worker_resume() && started && !held_for_reset && accepting);
    assert(test_private[0]==0); /* Actual worker publication ran after hold. */
    assert(reset_control==0x100U && write_count==9);
    expect_write(1,MT_SLCR_A9_CPU_RESET,0x102U);
    expect_write(2,MT_SLCR_A9_CPU_RESET,0x122U);
    expect_write(4,MT_CPU1_BOOT_VECTOR,MT_VIDEO_WORKER_ENTRY);
    expect_write(6,MT_SLCR_A9_CPU_RESET,0x120U);
    expect_write(7,MT_SLCR_A9_CPU_RESET,0x100U);
    reset_fixture(0x100U); ignore_control_write=2;
    assert(!mt_video_worker_hold_for_reset() && reset_control==0x102U && held_for_reset);
    ignore_control_write=0; write_count=control_count=0;
    assert(mt_video_worker_resume() && started && !held_for_reset && reset_control==0x100U);
    reset_fixture(0x100U); ignore_control_write=1;
    assert(!mt_video_worker_hold_for_reset());
    write_count=control_count=0; /* Persistent failed reset keeps restart blocked. */
    assert(!mt_video_worker_resume() && !started && held_for_reset && !accepting);
    assert(write_count==3); /* No boot-vector publication or code copy. */
    assert(test_private[0]==0xC3);
    reset_fixture(0x122U); ignore_control_write=1;
    assert(!cpu1_release() && reset_control==0x122U && write_count==3);
    expect_write(2,MT_SLCR_LOCK,MT_SLCR_LOCK_KEY);
    reset_fixture(0x122U); ignore_control_write=2;
    assert(!cpu1_release() && reset_control==0x120U && write_count==4);
    expect_write(3,MT_SLCR_LOCK,MT_SLCR_LOCK_KEY);
    reset_fixture(0x102U);
    assert(cpu1_release() && reset_control==0x100U && write_count==5);
    expect_write(1,MT_SLCR_A9_CPU_RESET,0x122U); /* complete partial clock hold */
    expect_write(2,MT_SLCR_A9_CPU_RESET,0x120U);
    expect_write(3,MT_SLCR_A9_CPU_RESET,0x100U);
    puts("PASS: CPU1 UG585 hold/release, preserved controls, busy refusal, failed readback and partial-reset recovery");
    return 0;
}
