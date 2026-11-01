/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_boot.h"
#include <assert.h>
#include <string.h>
typedef struct {
    uint32_t stage[128],status,command;
    unsigned writes,reads,commands,ticks,progress;
    bool capable,hold,error,wrong_sequence;
} bulk_t;
static uint32_t rd(void *ctx,uint32_t offset)
{
    bulk_t *b=ctx;
    if(offset==MT_AMIGA_HOST_PORT)return b->capable ? MT_AMIGA_HOST_BULK:0U;
    if(offset==MT_AMIGA_BULK_STATUS) {
        if((b->status&MT_AMIGA_BULK_BUSY)!=0U && !b->hold && ++b->ticks>=3U) {
            b->status=MT_AMIGA_BULK_DONE|(b->command>>16U);
            if(b->wrong_sequence)b->status^=1U;
            if(b->error)b->status|=MT_AMIGA_BULK_ERROR|(3U<<24U);
        }
        return b->status;
    }
    if(offset==MT_AMIGA_BULK_PROGRESS)return b->progress;
    assert(offset>=MT_AMIGA_BULK_DATA && offset<MT_AMIGA_BULK_DATA+512U && (offset&3U)==0U);
    assert(!(b->status&MT_AMIGA_BULK_BUSY));++b->reads;
    return b->stage[(offset-MT_AMIGA_BULK_DATA)/4U];
}
static void wr(void *ctx,uint32_t offset,uint32_t value)
{
    bulk_t *b=ctx;
    if(offset==MT_AMIGA_BULK_COMMAND) {
        assert(!(b->status&MT_AMIGA_BULK_BUSY));assert((value>>16U)!=0U && (value&0xfffeU)==512U);
        b->command=value;++b->commands;b->ticks=0U;b->status=MT_AMIGA_BULK_BUSY;return;
    }
    assert(offset>=MT_AMIGA_BULK_DATA && offset<MT_AMIGA_BULK_DATA+512U && (offset&3U)==0U);
    assert(!(b->status&MT_AMIGA_BULK_BUSY));++b->writes;b->stage[(offset-MT_AMIGA_BULK_DATA)/4U]=value;
}
int main(void)
{
    bulk_t b={0};mt_core_link_t link;mt_core_link_init(&link,&b,rd,wr);
    uint8_t data[512],expected[512];for(unsigned i=0U;i<512U;++i)data[i]=(uint8_t)(i^(i>>3U));
    memcpy(expected,data,sizeof(data));bool submitted=true;
    assert(mt_amiga_bulk_transfer(&link,false,data,5U,&submitted)==MT_LINK_ABI_MISMATCH && !submitted);
    assert(b.writes==0U && b.commands==0U);
    b.capable=true;b.progress=256U;b.status=MT_AMIGA_BULK_DONE|0xffffU;
    assert(mt_amiga_bulk_transfer(&link,false,data,5U,&submitted)==MT_LINK_OK && submitted);
    assert(b.command==(1U<<16U|512U) && b.writes==128U && b.reads==0U && b.commands==1U);
    assert(b.stage[0]==((uint32_t)data[0]|(uint32_t)data[1]<<8U|(uint32_t)data[2]<<16U|(uint32_t)data[3]<<24U));
    memset(data,0,sizeof(data));
    assert(mt_amiga_bulk_transfer(&link,true,data,5U,&submitted)==MT_LINK_OK);
    assert(b.command==(2U<<16U|513U) && b.writes==128U && b.reads==128U && b.commands==2U);
    assert(memcmp(data,expected,sizeof(data))==0);
    /* Neither stale DONE nor another job's completion may publish a capture. */
    b.wrong_sequence=true;memset(data,0xa5,sizeof(data));
    assert(mt_amiga_bulk_transfer(&link,true,data,5U,&submitted)==MT_LINK_TIMEOUT && submitted);
    assert(b.commands==3U && b.reads==128U && data[0]==0xa5U);
    b.wrong_sequence=false;b.hold=true;
    assert(mt_amiga_bulk_transfer(&link,true,data,5U,&submitted)==MT_LINK_TIMEOUT && submitted);
    const unsigned commands=b.commands;
    assert(mt_amiga_bulk_transfer(&link,true,data,5U,&submitted)==MT_LINK_TIMEOUT && !submitted && b.commands==commands);
    b.hold=false;b.error=true;
    assert(mt_amiga_bulk_transfer(&link,true,data,5U,&submitted)==MT_LINK_REMOTE_ERROR && !submitted);
    assert(b.commands==commands && data[0]==0xa5U);
    b.error=false;b.status=0U;b.progress=19U;
    assert(mt_amiga_bulk_transfer(&link,true,data,5U,&submitted)==MT_LINK_REMOTE_ERROR && submitted);
    assert(b.reads==128U && data[0]==0xa5U);
    assert(mt_amiga_bulk_transfer(&link,false,NULL,5U,NULL)==MT_LINK_BAD_ARGUMENT);
    return 0;
}
