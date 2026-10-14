/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_PLATFORM_VIDEO_WORKER_H
#define MULTITINI_PLATFORM_VIDEO_WORKER_H

#include "multitini_video_worker.h"
#include <stdbool.h>

typedef enum {
    MT_VIDEO_WORKER_STOPPED = 0,
    MT_VIDEO_WORKER_STARTING,
    MT_VIDEO_WORKER_IDLE,
    MT_VIDEO_WORKER_BUSY,
    MT_VIDEO_WORKER_COMPLETE,
    MT_VIDEO_WORKER_FAILED
} mt_video_worker_state_t;

typedef struct {
    mt_video_worker_state_t state;
    uint32_t epoch;
    uint32_t sequence;
    uint32_t output_size;
    int32_t result;
} mt_video_worker_status_t;

/* Start once; startup is asynchronous. All subsequent calls do bounded
 * mailbox work. No API waits for CPU1 computation or accesses PL. */
bool mt_video_worker_start(void);
mt_video_worker_status_t mt_video_worker_status(void);
bool mt_video_worker_submit(uint32_t epoch, const void *input, uint32_t size);
/* A successful collect acknowledges the result locally. A different epoch
 * is never copied. Submitting a new job explicitly discards an old result. */
bool mt_video_worker_collect(uint32_t epoch, void *output, uint32_t capacity,
                              uint32_t *size);
/* Stop accepting new jobs and report whether all previous DDR reads/writes
 * have completed. Poll this before recycling frame pools across PL reload.
 * CPU1 can stay running: its only device-independent activity is this mailbox. */
bool mt_video_worker_quiesce(void);
/* Once quiescent, hold CPU1 in reset with its clock stopped before switching
 * firmware. Returns false while a job is active or control readback fails.
 * A later start/resume republishes the worker and releases the held CPU. */
bool mt_video_worker_hold_for_reset(void);
bool mt_video_worker_resume(void);

#endif
