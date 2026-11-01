/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_frame.h"
#include "multitini_amiga_interface.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(mt_video_frame_job_t)==48U,"Reference job wire size must remain48bytes");
_Static_assert(offsetof(mt_video_frame_job_t,options)==40U,"Legacy job fields must retain their offsets");
int main(void)
{
    mt_video_frame_layout_t layout;
    const uint32_t sizes[][2]={{640,512},{724,566},{724,636},{640,720}};
    for(unsigned i=0U;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        uint32_t width=sizes[i][0],height=sizes[i][1],stride=(width*2U+127U)&~127U;
        assert(mt_video_frame_select_layout(MT_AMIGA_CORE_ID,MT_AMIGA_HOST_SHRES,width,height,stride,&layout));
        assert(layout.capture_base==0x22000000U && layout.output_base==0x22400000U && layout.history_base==0x22800000U);
        assert(layout.source_base==0x23000000U && layout.slot_bytes==1048576U && layout.history_bytes==2097152U);
    }
    assert(mt_video_frame_select_layout(MT_AMIGA_CORE_ID,MT_AMIGA_HOST_SHRES,1280,512,2560,&layout));
    assert(layout.capture_base==0x22000000U && layout.output_base==0x22800000U && layout.history_base==0x23000000U);
    assert(layout.source_base==0x23800000U && layout.source_bytes==0x800000U && layout.slot_bytes==2097152U && layout.history_bytes==4194304U);
    for(unsigned slot=0U;slot<4U;++slot) {
        assert(layout.capture_base+slot*layout.slot_bytes+1280U*512U*2U<=layout.output_base);
        assert(layout.output_base+slot*layout.slot_bytes+1280U*512U*2U<=layout.history_base);
        assert(layout.source_base+slot*layout.slot_bytes+1280U*512U*2U<=0x24000000U);
    }
    const mt_video_frame_layout_t retained=layout;
    const uint32_t invalid[][5]={{MT_AMIGA_CORE_ID,0,1280,512,2560},{0x43363420U,MT_AMIGA_HOST_SHRES,1280,512,2560},
        {MT_AMIGA_CORE_ID,MT_AMIGA_HOST_SHRES,1279,512,2560},{MT_AMIGA_CORE_ID,MT_AMIGA_HOST_SHRES,1280,513,2560},
        {MT_AMIGA_CORE_ID,MT_AMIGA_HOST_SHRES,1280,512,2559},{MT_AMIGA_CORE_ID,MT_AMIGA_HOST_SHRES,724,637,1536}};
    for(unsigned i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        assert(!mt_video_frame_select_layout(invalid[i][0],invalid[i][1],invalid[i][2],invalid[i][3],invalid[i][4],&layout));
        assert(!memcmp(&layout,&retained,sizeof(layout)));
    }
    mt_video_frame_job_t job={.magic=MT_VIDEO_FRAME_JOB_MAGIC,.abi=MT_VIDEO_FRAME_JOB_ABI,
        .source_slot=3,.output_slot=2,.width=1280,.height=512,.stride=2560};
    assert(!mt_video_frame_job_layout(&job,&layout));
    job.abi=MT_VIDEO_FRAME_JOB_ABI_WIDE;assert(mt_video_frame_job_layout(&job,&layout) && layout.slot_bytes==2097152U);
    job.width=640;job.stride=1280;assert(!mt_video_frame_job_layout(&job,&layout));
    job.abi=MT_VIDEO_FRAME_JOB_ABI;assert(mt_video_frame_job_layout(&job,&layout) && layout.slot_bytes==1048576U);
    job.abi=4;assert(!mt_video_frame_job_layout(&job,&layout));
    job.abi=MT_VIDEO_FRAME_JOB_ABI;job.source_slot=4;assert(!mt_video_frame_job_layout(&job,&layout));
    assert(MT_VIDEO_FRAME_OUTPUT_BASE==0x22400000U && MT_VIDEO_FRAME_HISTORY_BASE==0x22800000U && MT_VIDEO_FRAME_SLOT_BYTES==1048576U);
    puts("PASS exact legacy/wide DDR layouts, capability gates, bounds and ABI2/3 immutable jobs");return 0;
}
