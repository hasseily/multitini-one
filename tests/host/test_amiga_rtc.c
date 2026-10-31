/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_rtc.h"
#include <assert.h>
#include <string.h>
typedef struct {
    uint32_t status,low,middle;
    uint64_t committed;
    unsigned writes,commits,clears,stage,fail_op;
    bool busy;
} native_t;
static uint32_t rd(void *context,uint32_t address)
{ native_t *n=context;assert(address==MT_AMIGA_HOST_PORT);return n->status|(n->busy ? MT_AMIGA_HOST_BUSY:0U); }
static void wr(void *context,uint32_t address,uint32_t word)
{
    native_t *n=context;assert(address==MT_AMIGA_HOST_PORT);++n->writes;
    const unsigned op=word>>24U;const uint32_t value=word&0xffffffU;
    n->status^=MT_AMIGA_HOST_TOGGLE;
    if(op==n->fail_op) { n->fail_op=0U;n->status|=MT_AMIGA_HOST_ERROR;return; }
    if(op==MT_AMIGA_HOST_CLEAR_ERROR) { n->status&=~MT_AMIGA_HOST_ERROR;n->stage=0U;++n->clears; }
    else if(op==MT_AMIGA_HOST_RTC_LOW) { n->low=value;n->stage=1U; }
    else if(op==MT_AMIGA_HOST_RTC_MIDDLE) { assert(n->stage==1U);n->middle=value;n->stage=3U; }
    else if(op==MT_AMIGA_HOST_RTC_COMMIT) {
        assert(n->stage==3U && value<=0xffffU);
        n->committed=(uint64_t)n->low|((uint64_t)n->middle<<24U)|((uint64_t)value<<48U);
        n->stage=0U;++n->commits;
    } else assert(false);
}
int main(void)
{
    mt_clock_time_t time={2024U,2U,29U,23U,59U,58U};uint64_t value=0U;
    assert(mt_amiga_rtc_encode(&time,&value) && value==UINT64_C(0x4004240229235958));
    mt_clock_time_t saturday={2000U,1U,1U,0U,0U,0U};
    assert(mt_amiga_rtc_encode(&saturday,&value) && value==UINT64_C(0x4006000101000000));
    value=0xabcU;time.day=30U;assert(!mt_amiga_rtc_encode(&time,&value) && value==0xabcU);
    assert(!mt_amiga_rtc_encode(NULL,&value) && !mt_amiga_rtc_encode(&saturday,NULL));time.day=29U;
    native_t native={.status=MT_AMIGA_HOST_RTC};mt_core_link_t link;
    mt_core_link_init(&link,&native,rd,wr);mt_amiga_rtc_t rtc;mt_amiga_rtc_init(&rtc);
    assert(mt_amiga_rtc_update(&rtc,&link,&time,0U,true,5U)==MT_LINK_OK);
    assert(rtc.supported && native.writes==3U && native.commits==1U && native.committed==UINT64_C(0x4004240229235958));
    ++time.second;
    assert(mt_amiga_rtc_update(&rtc,&link,&time,999U,false,5U)==MT_LINK_OK && native.writes==3U);
    assert(mt_amiga_rtc_update(&rtc,&link,&time,1000U,false,5U)==MT_LINK_OK && native.commits==2U);
    assert(native.committed==UINT64_C(0x4004240229235959));
    assert(mt_amiga_rtc_update(&rtc,&link,&time,2000U,false,5U)==MT_LINK_OK && native.commits==2U);
    assert(mt_amiga_rtc_update(&rtc,&link,NULL,3000U,false,5U)==MT_LINK_OK && native.commits==2U);
    assert(mt_amiga_rtc_update(&rtc,&link,&saturday,3001U,true,5U)==MT_LINK_OK && native.commits==3U);
    /* Old804 has no capability; no extended opcode may reach it. */
    native.status=0U;const unsigned writes=native.writes;
    assert(mt_amiga_rtc_update(&rtc,&link,&time,4000U,true,5U)==MT_LINK_OK && !rtc.supported && native.writes==writes);
    assert(mt_amiga_rtc_write(&link,&time,5U)==MT_LINK_OK && native.writes==writes);
    native.status=MT_AMIGA_HOST_RTC;
    for(unsigned op=5U;op<=7U;++op) {
        const uint64_t previous=native.committed;const unsigned clears=native.clears;
        native.fail_op=op;
        assert(mt_amiga_rtc_update(&rtc,&link,&time,5000U+op,true,5U)==MT_LINK_REMOTE_ERROR);
        assert(native.committed==previous && native.stage==0U && native.clears==clears+1U && !(native.status&MT_AMIGA_HOST_ERROR));
        assert(mt_amiga_rtc_update(&rtc,&link,&time,6000U+op,false,5U)==MT_LINK_OK);
        assert(native.committed==UINT64_C(0x4004240229235959));
    }
    native.status|=MT_AMIGA_HOST_ERROR;const unsigned before=native.writes;
    assert(mt_amiga_rtc_write(&link,&time,5U)==MT_LINK_REMOTE_ERROR && native.writes==before);
    native.status&=~MT_AMIGA_HOST_ERROR;native.busy=true;
    assert(mt_amiga_rtc_write(&link,&time,2U)==MT_LINK_TIMEOUT && native.writes==before);native.busy=false;
    /* Millisecond wrap and an unchanged failed timestamp must still retry. */
    mt_amiga_rtc_init(&rtc);native.fail_op=5U;
    assert(mt_amiga_rtc_update(&rtc,&link,&time,UINT32_MAX-499U,false,5U)==MT_LINK_REMOTE_ERROR);
    const unsigned failed_writes=native.writes;
    assert(mt_amiga_rtc_update(&rtc,&link,&time,499U,false,5U)==MT_LINK_REMOTE_ERROR && native.writes==failed_writes);
    assert(mt_amiga_rtc_update(&rtc,&link,&time,500U,false,5U)==MT_LINK_OK && native.writes==failed_writes+3U);
    assert(mt_amiga_rtc_write(NULL,&time,5U)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_rtc_write(&link,&time,0U)==MT_LINK_BAD_ARGUMENT);
    return 0;
}
