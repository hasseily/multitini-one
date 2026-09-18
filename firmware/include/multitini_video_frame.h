/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_FRAME_H
#define MULTITINI_VIDEO_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define MT_VIDEO_FRAME_JOB_MAGIC UINT32_C(0x4D544646)
#define MT_VIDEO_FRAME_JOB_ABI 1U
#define MT_VIDEO_FRAME_CAPTURE_BASE UINT32_C(0x22000000)
#define MT_VIDEO_FRAME_OUTPUT_BASE UINT32_C(0x22400000)
#define MT_VIDEO_FRAME_HISTORY_BASE UINT32_C(0x22800000)
#define MT_VIDEO_FRAME_HISTORY_BYTES UINT32_C(0x00200000)
#define MT_VIDEO_FRAME_SLOT_BYTES UINT32_C(0x00100000)
#define MT_VIDEO_FRAME_SLOT_COUNT 4U
#define MT_VIDEO_FRAME_MAX_WIDTH 640U
#define MT_VIDEO_FRAME_MAX_HEIGHT 720U
#define MT_VIDEO_FRAME_MAX_PIXELS (MT_VIDEO_FRAME_MAX_WIDTH * MT_VIDEO_FRAME_MAX_HEIGHT)

typedef struct {
    uint8_t enabled;
    uint8_t persistence;
    uint8_t blur;
    uint8_t bloom;
} mt_video_frame_options_t;

/* Source and output are slot indices, never addresses. CPU0 reserves both
 * until completion; CPU1 never changes capture/scanout hardware ownership.
 * Rows in the DDR slots have 128-byte aligned stride, while processing uses
 * packed private cached arrays. Epoch changes invalidate temporal history.
 */
typedef struct {
    uint32_t magic;
    uint32_t abi;
    uint32_t epoch;
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t source_slot;
    uint32_t output_slot;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    mt_video_frame_options_t options;
} mt_video_frame_job_t;

typedef struct {
    uint32_t magic;
    uint32_t epoch;
    uint32_t sequence;
    uint32_t output_slot;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} mt_video_frame_result_t;

/* The five arrays are distinct and contain width*height pixels. Input and
 * output are RGB565; history/base/scratch are packed 0x00BBGGRR RGB888.
 * History survives calls; base/scratch are disposable. Decay steps count
 * elapsed 60Hz periods, clamped to 0..64. Reset on epoch, geometry, or option
 * changes. No device access is permitted. */
bool mt_video_frame_process(const uint16_t *input, uint16_t *output,
                            uint32_t *history, uint32_t *base, uint32_t *scratch,
                            uint32_t width, uint32_t height,
                            const mt_video_frame_options_t *options,
                            uint32_t decay_steps,
                            bool reset_history);

#endif
