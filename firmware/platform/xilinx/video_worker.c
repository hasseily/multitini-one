/* SPDX-License-Identifier: GPL-3.0-only
 * CPU1 release adapted from Appletini One ps_sources/lib/amp.c at
 * 1fd4f371e8f4ef08bc63875550564590609fcfeb. Mailbox service is original.
 */
#include "video_worker.h"
#include "multitini_video_frame.h"

#include <string.h>
#include "xil_cache.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xiltimer.h"

#define MT_CPU1_BOOT_VECTOR UINT32_C(0xFFFFFFF0)
#define MT_SLCR_LOCK UINT32_C(0xF8000004)
#define MT_SLCR_UNLOCK UINT32_C(0xF8000008)
#define MT_SLCR_A9_CPU_RESET UINT32_C(0xF8000244)
#define MT_SLCR_LOCK_KEY UINT32_C(0x0000767B)
#define MT_SLCR_UNLOCK_KEY UINT32_C(0x0000DF0D)
#define MT_CPU1_RESET UINT32_C(0x00000002)
#define MT_CPU1_CLOCK_STOP UINT32_C(0x00000020)
#define MT_VIDEO_WORKER_BOOT_TIMEOUT_MS 1000U
#define MT_VIDEO_WORKER_JOB_TIMEOUT_MS 2000U

extern const unsigned char mt_video_core1_blob_start[];
extern const unsigned char mt_video_core1_blob_end[];

#ifdef MT_VIDEO_WORKER_HOST_TEST
static volatile mt_video_worker_mailbox_t *mailbox;
static uintptr_t test_worker_entry;
static uintptr_t test_worker_size;
#else
static volatile mt_video_worker_mailbox_t *const mailbox =
    (volatile mt_video_worker_mailbox_t *)(uintptr_t)MT_VIDEO_WORKER_SHARED_BASE;
#endif
static bool started;
static bool timed_out;
static bool accepting = true;
static bool held_for_reset;
static uint32_t collected_sequence;
static XTime operation_started;

static void barrier(void)
{
#if defined(__arm__)
    __asm__ volatile ("dsb sy" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

static void wake(void)
{
#if defined(__arm__)
    __asm__ volatile ("sev" ::: "memory");
#endif
}

static bool cpu1_control_step(uint32_t set, uint32_t clear)
{
    const uint32_t control = (Xil_In32(MT_SLCR_A9_CPU_RESET) | set) & ~clear;
    Xil_Out32(MT_SLCR_A9_CPU_RESET, control);
    barrier();
    return Xil_In32(MT_SLCR_A9_CPU_RESET) == control;
}

static bool cpu1_release(void)
{
    const uint32_t control = Xil_In32(MT_SLCR_A9_CPU_RESET);
    bool released;
    if ((control & (MT_CPU1_RESET | MT_CPU1_CLOCK_STOP)) == 0U) return true;
    /* UG585 Reset Functionality requires releasing reset while the clock is
     * stopped, then restarting that clock. Preserve CPU0/peripheral controls.
     * https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/Reset-Functionality */
    Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
    barrier();
    /* Complete a partial reset-only hold before following the release order. */
    released = (!(control & MT_CPU1_RESET) || (control & MT_CPU1_CLOCK_STOP) ||
                cpu1_control_step(MT_CPU1_CLOCK_STOP, 0U)) &&
               cpu1_control_step(0U, MT_CPU1_RESET) &&
               cpu1_control_step(0U, MT_CPU1_CLOCK_STOP);
    Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
    barrier();
    return released;
}

static bool elapsed(uint32_t milliseconds)
{
    XTime now;
    XTime_GetTime(&now);
    return now - operation_started >=
        ((XTime)COUNTS_PER_SECOND * milliseconds) / 1000U;
}

bool mt_video_worker_start(void)
{
#ifdef MT_VIDEO_WORKER_HOST_TEST
    const uintptr_t size = test_worker_size;
    const uintptr_t entry = test_worker_entry;
#else
    const uintptr_t size = (uintptr_t)mt_video_core1_blob_end -
                           (uintptr_t)mt_video_core1_blob_start;
    const uintptr_t entry = MT_VIDEO_WORKER_ENTRY;
#endif
    if (started) return !timed_out;
    if (size == 0U || size > MT_VIDEO_WORKER_PRIVATE_BYTES) return false;
    /* An earlier failed hold/release may have partially changed CPU1 state.
     * Reestablish verified reset+clock hold before recycling any DDR bytes. */
    if (held_for_reset && !mt_video_worker_hold_for_reset()) return false;
    /* Neither CPU has accessed this reserved DDR section before mapping.
     * Avoid cacheable aliases: AMP CPU1 cannot clean shared L2 itself. */
    Xil_SetTlbAttributes(MT_VIDEO_WORKER_SHARED_BASE, NORM_NONCACHE);
    for (uint32_t slot = 0U; slot < MT_VIDEO_FRAME_SLOT_COUNT; ++slot) {
        Xil_SetTlbAttributes(MT_VIDEO_FRAME_CAPTURE_BASE + slot * MT_VIDEO_FRAME_SLOT_BYTES,
                             NORM_NONCACHE);
        Xil_SetTlbAttributes(MT_VIDEO_FRAME_OUTPUT_BASE + slot * MT_VIDEO_FRAME_SLOT_BYTES,
                             NORM_NONCACHE);
    }
    memset((void *)(uintptr_t)mailbox, 0, sizeof(*mailbox));
    mailbox->magic = MT_VIDEO_WORKER_MAGIC;
    mailbox->abi = MT_VIDEO_WORKER_ABI;
    memcpy((void *)entry,
           mt_video_core1_blob_start, size);
    Xil_DCacheFlushRange((INTPTR)MT_VIDEO_WORKER_ENTRY, (u32)size);
    Xil_SetTlbAttributes(UINT32_C(0xFFF00000), STRONG_ORDERED);
    barrier();
    Xil_Out32(MT_CPU1_BOOT_VECTOR, MT_VIDEO_WORKER_ENTRY);
    barrier();
    /* A firmware handoff leaves CPU1 reset and clock-stopped. Publish code,
     * mailbox and vector completely before releasing it into the new image. */
    held_for_reset = true;
    if (!cpu1_release()) return false;
    held_for_reset = false;
    wake();
    XTime_GetTime(&operation_started);
    started = true;
    return true;
}

mt_video_worker_status_t mt_video_worker_status(void)
{
    mt_video_worker_status_t status = {MT_VIDEO_WORKER_STOPPED, 0U, 0U, 0U, 0};
    uint32_t complete;
    if (!started) return status;
    if (timed_out) {
        status.state = MT_VIDEO_WORKER_FAILED;
        status.result = -3;
        return status;
    }
    if (mailbox->ready != MT_VIDEO_WORKER_MAGIC) {
        status.state = MT_VIDEO_WORKER_STARTING;
        if (elapsed(MT_VIDEO_WORKER_BOOT_TIMEOUT_MS)) {
            timed_out = true;
            status.state = MT_VIDEO_WORKER_FAILED;
            status.result = -3;
        }
        return status;
    }
    complete = mailbox->complete_sequence;
    barrier();
    status.sequence = mailbox->request_sequence;
    status.epoch = mailbox->request_epoch;
    if (complete != status.sequence) {
        status.state = MT_VIDEO_WORKER_BUSY;
        if (elapsed(MT_VIDEO_WORKER_JOB_TIMEOUT_MS)) {
            timed_out = true;
            status.state = MT_VIDEO_WORKER_FAILED;
            status.result = -3;
        }
        return status;
    }
    status.state = complete == collected_sequence ?
        MT_VIDEO_WORKER_IDLE : MT_VIDEO_WORKER_COMPLETE;
    status.epoch = mailbox->complete_epoch;
    status.output_size = mailbox->output_size;
    status.result = mailbox->result;
    if (status.state == MT_VIDEO_WORKER_COMPLETE &&
        (status.result != 0 || status.output_size > MT_VIDEO_WORKER_PAYLOAD_BYTES))
        status.state = MT_VIDEO_WORKER_FAILED;
    return status;
}

bool mt_video_worker_submit(uint32_t epoch, const void *input, uint32_t size)
{
    const mt_video_worker_status_t status = mt_video_worker_status();
    uint32_t sequence;
    uint32_t i;
    if (!accepting || input == NULL || size == 0U || size > MT_VIDEO_WORKER_PAYLOAD_BYTES ||
        (status.state != MT_VIDEO_WORKER_IDLE &&
         status.state != MT_VIDEO_WORKER_COMPLETE &&
         !(status.state == MT_VIDEO_WORKER_FAILED && !timed_out))) return false;
    sequence = mailbox->request_sequence + 1U;
    if (sequence == 0U) sequence = 1U;
    for (i = 0U; i < size; ++i) mailbox->input[i] = ((const uint8_t *)input)[i];
    mailbox->input_size = size;
    mailbox->request_epoch = epoch;
    barrier();
    mailbox->request_sequence = sequence;
    barrier();
    XTime_GetTime(&operation_started);
    wake();
    return true;
}

bool mt_video_worker_quiesce(void)
{
    uint32_t complete;
    accepting = false;
    if (!started) return true;
    complete = mailbox->complete_sequence;
    barrier();
    return complete == mailbox->request_sequence;
}

bool mt_video_worker_resume(void)
{
    if (timed_out) return false;
    if (held_for_reset && !mt_video_worker_start()) return false;
    accepting = true;
    return true;
}

bool mt_video_worker_hold_for_reset(void)
{
    bool held;
    if (!mt_video_worker_quiesce()) return false;
    /* Once a reset write is attempted, the old worker cannot be resumed by
     * mailbox flags alone. Even a failed readback routes resume through a
     * verified hardware hold and a complete worker restart. */
    held_for_reset = true;
    started = false;
    timed_out = false;
    collected_sequence = 0U;
    /* Completion follows the worker's final DDR writes. Stop it before an
     * FSBL or another firmware can recycle those memory regions. UG585 orders
     * software reset assertion before the associated clock is stopped. */
    barrier();
    Xil_Out32(MT_SLCR_UNLOCK, MT_SLCR_UNLOCK_KEY);
    barrier();
    held = cpu1_control_step(MT_CPU1_RESET, 0U) &&
           cpu1_control_step(MT_CPU1_CLOCK_STOP, 0U);
    Xil_Out32(MT_SLCR_LOCK, MT_SLCR_LOCK_KEY);
    barrier();
    if (!held) return false;
    return true;
}

bool mt_video_worker_collect(uint32_t epoch, void *output, uint32_t capacity,
                              uint32_t *size)
{
    const mt_video_worker_status_t status = mt_video_worker_status();
    uint32_t i;
    if (size != NULL) *size = 0U;
    if (status.state != MT_VIDEO_WORKER_COMPLETE || status.epoch != epoch ||
        output == NULL || size == NULL || capacity < status.output_size)
        return false;
    for (i = 0U; i < status.output_size; ++i)
        ((uint8_t *)output)[i] = mailbox->output[i];
    barrier();
    *size = status.output_size;
    collected_sequence = status.sequence;
    return true;
}
