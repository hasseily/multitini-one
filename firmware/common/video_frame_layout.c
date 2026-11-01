/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_frame.h"
#include "multitini_amiga_interface.h"
#include <stddef.h>

static bool legacy_layout(uint32_t width,uint32_t height,uint32_t stride,
                          mt_video_frame_layout_t *layout)
{
    if(layout==NULL || width==0U || width>MT_VIDEO_FRAME_LEGACY_MAX_WIDTH ||
       height==0U || height>MT_VIDEO_FRAME_MAX_HEIGHT ||
       width>MT_VIDEO_FRAME_LEGACY_MAX_PIXELS/height ||
       stride!=((width*2U+127U)&~UINT32_C(127)) ||
       stride>MT_VIDEO_FRAME_SLOT_BYTES/height)return false;
    *layout=(mt_video_frame_layout_t){MT_VIDEO_FRAME_CAPTURE_BASE,MT_VIDEO_FRAME_OUTPUT_BASE,
        MT_VIDEO_FRAME_HISTORY_BASE,MT_VIDEO_FRAME_HISTORY_BYTES,UINT32_C(0x23000000),
        UINT32_C(0x00400000),MT_VIDEO_FRAME_SLOT_BYTES};
    return true;
}
static bool wide_layout(uint32_t width,uint32_t height,uint32_t stride,
                        mt_video_frame_layout_t *layout)
{
    if(layout==NULL || width!=1280U || height!=512U || stride!=2560U)return false;
    *layout=(mt_video_frame_layout_t){MT_VIDEO_FRAME_CAPTURE_BASE,MT_VIDEO_FRAME_WIDE_OUTPUT_BASE,
        MT_VIDEO_FRAME_WIDE_HISTORY_BASE,MT_VIDEO_FRAME_WIDE_HISTORY_BYTES,
        MT_VIDEO_FRAME_WIDE_SOURCE_BASE,UINT32_C(0x00800000),MT_VIDEO_FRAME_WIDE_SLOT_BYTES};
    return true;
}
bool mt_video_frame_select_layout(uint32_t core_id,uint32_t host_caps,
    uint32_t width,uint32_t height,uint32_t stride,mt_video_frame_layout_t *layout)
{
    if(core_id==MT_AMIGA_CORE_ID && (host_caps&MT_AMIGA_HOST_SHRES)!=0U &&
       wide_layout(width,height,stride,layout))return true;
    return legacy_layout(width,height,stride,layout);
}
bool mt_video_frame_job_layout(const mt_video_frame_job_t *job,mt_video_frame_layout_t *layout)
{
    if(job==NULL || job->magic!=MT_VIDEO_FRAME_JOB_MAGIC ||
       job->source_slot>=MT_VIDEO_FRAME_SLOT_COUNT || job->output_slot>=MT_VIDEO_FRAME_SLOT_COUNT)return false;
    if(job->abi==MT_VIDEO_FRAME_JOB_ABI_WIDE)return wide_layout(job->width,job->height,job->stride,layout);
    return job->abi==MT_VIDEO_FRAME_JOB_ABI && legacy_layout(job->width,job->height,job->stride,layout);
}

