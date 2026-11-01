/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_FRAME_H
#define MULTITINI_VIDEO_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#define MT_VIDEO_FRAME_JOB_MAGIC UINT32_C(0x4D544646)
#define MT_VIDEO_FRAME_JOB_ABI 2U
#define MT_VIDEO_FRAME_JOB_ABI_WIDE 3U
#define MT_VIDEO_FRAME_CAPTURE_BASE UINT32_C(0x22000000)
#define MT_VIDEO_FRAME_OUTPUT_BASE UINT32_C(0x22400000)
#define MT_VIDEO_FRAME_HISTORY_BASE UINT32_C(0x22800000)
#define MT_VIDEO_FRAME_HISTORY_BYTES UINT32_C(0x00200000)
#define MT_VIDEO_FRAME_SLOT_BYTES UINT32_C(0x00100000)
#define MT_VIDEO_FRAME_SLOT_COUNT 4U
#define MT_VIDEO_FRAME_LEGACY_MAX_WIDTH 724U
#define MT_VIDEO_FRAME_LEGACY_MAX_PIXELS 460800U
#define MT_VIDEO_FRAME_WIDE_OUTPUT_BASE UINT32_C(0x22800000)
#define MT_VIDEO_FRAME_WIDE_HISTORY_BASE UINT32_C(0x23000000)
#define MT_VIDEO_FRAME_WIDE_HISTORY_BYTES UINT32_C(0x00400000)
#define MT_VIDEO_FRAME_WIDE_SOURCE_BASE UINT32_C(0x23800000)
#define MT_VIDEO_FRAME_WIDE_SLOT_BYTES UINT32_C(0x00200000)
#define MT_VIDEO_FRAME_RESERVED_BASE MT_VIDEO_FRAME_CAPTURE_BASE
#define MT_VIDEO_FRAME_RESERVED_BYTES UINT32_C(0x02000000)
#define MT_VIDEO_FRAME_MMU_SECTION_BYTES UINT32_C(0x00100000)
#define MT_VIDEO_FRAME_MAX_WIDTH 1280U
#define MT_VIDEO_FRAME_MAX_HEIGHT 720U
/* Pure processing/screenshot array budget. Physical DDR validation is a
 * separate profile decision; legacy slots and bounds remain unchanged. */
#define MT_VIDEO_FRAME_MAX_PIXELS 655360U

static inline bool mt_video_frame_geometry_valid(uint32_t width, uint32_t height)
{
    return width != 0U && width <= MT_VIDEO_FRAME_MAX_WIDTH &&
        height != 0U && height <= MT_VIDEO_FRAME_MAX_HEIGHT &&
        width <= MT_VIDEO_FRAME_MAX_PIXELS / height;
}

typedef struct {
    uint32_t capture_base,output_base,history_base,history_bytes;
    uint32_t source_base,source_bytes,slot_bytes;
} mt_video_frame_layout_t;

/* Select only a currently committed hardware profile. Legacy global address
 * macros retain their meaning. Wide DDR addresses require Amiga, SHRES
 * capability and the exact negotiated 1280x512/2560-byte-stride geometry. */
bool mt_video_frame_select_layout(uint32_t core_id,uint32_t host_caps,
    uint32_t width,uint32_t height,uint32_t stride,mt_video_frame_layout_t *layout);

typedef struct {
    uint8_t enabled;
    uint8_t persistence;
    uint8_t blur; /* Calibrated Blur: H3 at 1/8, H3/V3 at 1/4, H5/V3 at 1/2. */
    uint8_t bloom;
    uint8_t softness; /* Dot bleed level; applied during scanout, not native filtering. */
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

/* The 48-byte reference job remains unchanged. ABI2 uses legacy bounds and
 * pools; ABI3 explicitly opts in to the exact wide profile. Production CPU1
 * only prepares controls; its host-only frame reference uses this helper. */
bool mt_video_frame_job_layout(const mt_video_frame_job_t *,mt_video_frame_layout_t *);

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
 * changes. No device access is permitted. The convenience entrypoint uses
 * the Vectrex 1080p reference scale of 3/2. */
bool mt_video_frame_process(const uint16_t *input, uint16_t *output,
                            uint32_t *history, uint32_t *base, uint32_t *scratch,
                            uint32_t width, uint32_t height,
                            const mt_video_frame_options_t *options,
                            uint32_t decay_steps,
                            bool reset_history);

/* Appletini ONE 886ea50e native-pixel Blur/Glow, independent of scale.
 * PHO2 rounds normalized Blur kernels once per pass, then blends their result
 * with the original persisted pixel at 1/8, 1/4 or 1/2. Flat colors are exact.
 * Glow floors each [1,2,1]/4 pass once and adds 1/8, 1/4, or 1/2 of the
 * fixed halo with saturation. Dot bleed runs after RGB565 frame storage.
 * Scale operands remain validated in 1..64 for caller compatibility. */
bool mt_video_frame_process_scaled(const uint16_t *input, uint16_t *output,
                            uint32_t *history, uint32_t *base, uint32_t *scratch,
                            uint32_t width, uint32_t height,
                            uint32_t scale_num, uint32_t scale_den,
                            const mt_video_frame_options_t *options,
                            uint32_t decay_steps,
                            bool reset_history);

#endif
