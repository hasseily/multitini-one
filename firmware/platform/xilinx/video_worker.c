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
#define MT_VIDEO_WORKER_BOOT_TIMEOUT_MS 1000U
#define MT_VIDEO_WORKER_JOB_TIMEOUT_MS 2000U

extern const unsigned char mt_video_core1_blob_start[];
extern const unsigned char mt_video_core1_blob_end[];

#ifdef MT_VIDEO_WORKER_HOST_TEST
static volatile mt_video_worker_mailbox_t *mailbox;
#else
static volatile mt_video_worker_mailbox_t *const mailbox =
    (volatile mt_video_worker_mailbox_t *)(uintptr_t)MT_VIDEO_WORKER_SHARED_BASE;
#endif
static bool started;
static bool timed_out;
static bool accepting = true;
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

static bool elapsed(uint32_t milliseconds)
{
    XTime now;
    XTime_GetTime(&now);
    return now - operation_started >=
        ((XTime)COUNTS_PER_SECOND * milliseconds) / 1000U;
}

bool mt_video_worker_start(void)
{
    const uintptr_t size = (uintptr_t)mt_video_core1_blob_end -
                           (uintptr_t)mt_video_core1_blob_start;
    if (started) return !timed_out;
    if (size == 0U || size > MT_VIDEO_WORKER_PRIVATE_BYTES) return false;
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
    memcpy((void *)(uintptr_t)MT_VIDEO_WORKER_ENTRY,
           mt_video_core1_blob_start, size);
    Xil_DCacheFlushRange((INTPTR)MT_VIDEO_WORKER_ENTRY, (u32)size);
    Xil_SetTlbAttributes(UINT32_C(0xFFF00000), STRONG_ORDERED);
    barrier();
    Xil_Out32(MT_CPU1_BOOT_VECTOR, MT_VIDEO_WORKER_ENTRY);
    barrier();
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
    accepting = true;
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
