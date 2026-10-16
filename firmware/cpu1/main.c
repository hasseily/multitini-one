/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_worker.h"
#include "multitini_video_frame.h"
#include "multitini_video_effects.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifndef MT_VIDEO_WORKER_HOST_TEST
#include "xil_mmu.h"
#endif

static uint8_t input_scratch[MT_VIDEO_WORKER_PAYLOAD_BYTES]
    __attribute__((aligned(32)));
static uint8_t output_scratch[MT_VIDEO_WORKER_PAYLOAD_BYTES]
    __attribute__((aligned(32)));

static void worker_barrier(void)
{
#if defined(__arm__)
    __asm__ volatile ("dsb sy" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

#ifndef MT_VIDEO_WORKER_NO_DISPATCH
#ifdef MT_VIDEO_WORKER_FRAME_REFERENCE
/* Host-only reference dispatcher. Production frame ownership belongs to
 * the FPGA scheduler; CPU1 must never write its capture/output pools. */
#ifndef MT_VIDEO_FRAME_CAPTURE_POINTER
#define MT_VIDEO_FRAME_CAPTURE_POINTER(slot) \
    ((const uint16_t *)(uintptr_t)(MT_VIDEO_FRAME_CAPTURE_BASE + \
                                  (slot) * MT_VIDEO_FRAME_SLOT_BYTES))
#endif
#ifndef MT_VIDEO_FRAME_OUTPUT_POINTER
#define MT_VIDEO_FRAME_OUTPUT_POINTER(slot) \
    ((uint16_t *)(uintptr_t)(MT_VIDEO_FRAME_OUTPUT_BASE + \
                            (slot) * MT_VIDEO_FRAME_SLOT_BYTES))
#endif
static uint16_t frame_input[MT_VIDEO_FRAME_MAX_PIXELS] __attribute__((aligned(32)));
static uint16_t frame_output[MT_VIDEO_FRAME_MAX_PIXELS] __attribute__((aligned(32)));
static uint32_t frame_history[MT_VIDEO_FRAME_MAX_PIXELS] __attribute__((aligned(32)));
static uint32_t frame_base[MT_VIDEO_FRAME_MAX_PIXELS] __attribute__((aligned(32)));
static uint32_t frame_scratch[MT_VIDEO_FRAME_MAX_PIXELS] __attribute__((aligned(32)));
static mt_video_frame_job_t last_frame;
static bool frame_history_valid;
static uint32_t decay_remainder;

static bool frame_job_valid(const mt_video_frame_job_t *job)
{
    return job->magic == MT_VIDEO_FRAME_JOB_MAGIC && job->abi == MT_VIDEO_FRAME_JOB_ABI &&
        job->source_slot < MT_VIDEO_FRAME_SLOT_COUNT && job->output_slot < MT_VIDEO_FRAME_SLOT_COUNT &&
        mt_video_frame_geometry_valid(job->width, job->height) &&
        job->stride == ((job->width + 63U) / 64U) * 128U &&
        job->height * job->stride <= MT_VIDEO_FRAME_SLOT_BYTES &&
        job->options.enabled <= 1U && job->options.persistence <= 3U &&
        job->options.blur <= 3U && job->options.bloom <= 3U && job->options.softness <= 3U;
}

static int execute_frame(const mt_video_frame_job_t *job, void *output, uint32_t *size)
{
    const uint16_t *source;
    uint16_t *destination;
    bool reset_history;
    uint32_t decay_steps;
    mt_video_frame_result_t result;
    if (!frame_job_valid(job) || *size < sizeof(result)) return -4;
    source = MT_VIDEO_FRAME_CAPTURE_POINTER(job->source_slot);
    destination = MT_VIDEO_FRAME_OUTPUT_POINTER(job->output_slot);
    reset_history = !frame_history_valid || last_frame.epoch != job->epoch ||
        last_frame.width != job->width || last_frame.height != job->height ||
        memcmp(&last_frame.options, &job->options, sizeof(job->options)) != 0;
    if (reset_history) {
        decay_steps = 1U;
        decay_remainder = 0U;
    } else {
        const uint64_t ticks = (uint64_t)(job->timestamp_ms - last_frame.timestamp_ms) *
                               60U + decay_remainder;
        if (ticks >= UINT64_C(64000)) {
            decay_steps = 64U;
            decay_remainder = 0U;
        } else {
            decay_steps = (uint32_t)(ticks / 1000U);
            decay_remainder = (uint32_t)(ticks % 1000U);
        }
    }
    for (uint32_t y = 0U; y < job->height; ++y)
        memcpy(frame_input + y * job->width,
               source + y * (job->stride / sizeof(uint16_t)), job->width * sizeof(uint16_t));
    if (!mt_video_frame_process(frame_input, frame_output, frame_history, frame_base,
                                 frame_scratch, job->width, job->height, &job->options,
                                 decay_steps, reset_history)) {
        frame_history_valid = false;
        return -5;
    }
    for (uint32_t y = 0U; y < job->height; ++y)
        memcpy(destination + y * (job->stride / sizeof(uint16_t)),
               frame_output + y * job->width, job->width * sizeof(uint16_t));
    /* Noncacheable writes must reach DDR before CPU0 publishes the scanout
     * slot. This avoids relying on the AMP BSP's disabled shared L2 clean. */
    worker_barrier();
    frame_history_valid = true;
    last_frame = *job;
    result.magic = MT_VIDEO_FRAME_JOB_MAGIC;
    result.epoch = job->epoch;
    result.sequence = job->sequence;
    result.output_slot = job->output_slot;
    result.width = job->width;
    result.height = job->height;
    result.stride = job->stride;
    memcpy(output, &result, sizeof(result));
    *size = sizeof(result);
    return 0;
}

#endif

int mt_video_worker_execute(const void *input, uint32_t input_size,
                            void *output, uint32_t *output_size)
{
    if (input == NULL || output == NULL || output_size == NULL) return -1;
#ifdef MT_VIDEO_WORKER_FRAME_REFERENCE
    if (input_size == sizeof(mt_video_frame_job_t)) {
        mt_video_frame_job_t job;
        memcpy(&job, input, sizeof(job));
        return execute_frame(&job, output, output_size);
    }
#endif
    return mt_video_effects_worker_execute(input, input_size, output, output_size);
}
#endif

/* Separate one-job step permits the exact protocol to run in host tests.
 * CPU0 must retain request/input until completion. CPU1 snapshots metadata
 * and computes in private cached memory; only publication hits shared DDR. */
static bool worker_step(volatile mt_video_worker_mailbox_t *shared)
{
    const uint32_t sequence = shared->request_sequence;
    uint32_t epoch;
    uint32_t input_size;
    uint32_t output_size = sizeof(output_scratch);
    uint32_t i;
    int result = -1;
    if (sequence == shared->complete_sequence) return false;
    worker_barrier();
    epoch = shared->request_epoch;
    input_size = shared->input_size;
    if (shared->magic == MT_VIDEO_WORKER_MAGIC && shared->abi == MT_VIDEO_WORKER_ABI &&
        input_size != 0U && input_size <= sizeof(input_scratch)) {
        for (i = 0U; i < input_size; ++i) input_scratch[i] = shared->input[i];
        result = mt_video_worker_execute(input_scratch, input_size,
                                         output_scratch, &output_size);
        if (output_size > sizeof(output_scratch)) result = -2;
    }
    if (result == 0) {
        for (i = 0U; i < output_size; ++i) shared->output[i] = output_scratch[i];
    } else {
        output_size = 0U;
    }
    shared->complete_epoch = epoch;
    shared->result = result;
    shared->output_size = output_size;
    worker_barrier();
    shared->complete_sequence = sequence;
    worker_barrier();
    return true;
}

#ifndef MT_VIDEO_WORKER_HOST_TEST
int main(void)
{
    volatile mt_video_worker_mailbox_t *const shared =
        (volatile mt_video_worker_mailbox_t *)(uintptr_t)MT_VIDEO_WORKER_SHARED_BASE;
    Xil_SetTlbAttributes(MT_VIDEO_WORKER_SHARED_BASE, NORM_NONCACHE);
    /* CPU0 alone initializes the mailbox. CPU1 only acknowledges it; no
     * UART, timers, interrupt controller, PL registers or shared drivers. */
    if (shared->magic != MT_VIDEO_WORKER_MAGIC || shared->abi != MT_VIDEO_WORKER_ABI)
        for (;;) __asm__ volatile ("wfe");
    worker_barrier();
    shared->ready = MT_VIDEO_WORKER_MAGIC;
    worker_barrier();
    for (;;) {
        if (!worker_step(shared)) __asm__ volatile ("wfe");
    }
}
#endif
