/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_boot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t words[MT_AMIGA_DIAG_WORDS];
    uint32_t response;
    unsigned writes,reads,fail_at,torn_index;
    bool capable,hold,no_toggle,error,wrong_ack,torn;
} fixture_t;
static uint32_t rd(void *context,uint32_t address)
{
    fixture_t *f=context;assert(address==MT_AMIGA_HOST_PORT);
    return f->response|(f->capable ? MT_AMIGA_HOST_DIAGNOSTICS:0U)|
        (f->hold ? MT_AMIGA_HOST_BUSY:0U)|(f->error ? MT_AMIGA_HOST_ERROR:0U);
}
static void wr(void *context,uint32_t address,uint32_t command)
{
    fixture_t *f=context;assert(address==MT_AMIGA_HOST_PORT);++f->writes;
    const unsigned op=command>>24U,payload=command&0xffffffU;
    uint16_t value=0U;
    if(op==MT_AMIGA_HOST_DIAG_CONTROL) {
        assert((payload>>8U)!=0U && (uint8_t)payload>=1U && (uint8_t)payload<=2U);
        value=(uint16_t)(payload>>8U);f->words[3]=value;
        if(f->wrong_ack)++value;
    } else {
        assert(op==MT_AMIGA_HOST_DIAG_READ && payload<MT_AMIGA_DIAG_WORDS);
        ++f->reads;value=f->words[payload];
        if(f->fail_at==f->reads)f->error=true;
        if(f->torn && f->reads>MT_AMIGA_DIAG_WORDS && payload==f->torn_index)value^=1U;
    }
    const uint32_t toggle=(f->response^(f->no_toggle ? 0U:MT_AMIGA_HOST_TOGGLE))&MT_AMIGA_HOST_TOGGLE;
    f->response=toggle|value;
}
static void count(fixture_t *f,unsigned index,uint64_t value)
{ for(unsigned w=0U;w<4U;++w)f->words[16U+index*4U+w]=(uint16_t)(value>>(w*16U)); }
static void setup(fixture_t *f)
{
    memset(f,0,sizeof(*f));f->capable=true;
    f->words[0]=0x4447U;f->words[1]=1U;f->words[2]=MT_AMIGA_DIAG_WORDS;f->words[3]=0x2345U;
    f->words[4]=MT_AMIGA_DIAG_FROZEN|MT_AMIGA_DIAG_VALID|MT_AMIGA_DIAG_OPEN_TAIL_DROPPED;
    f->words[5]=f->words[6]=0x166bU;
    f->words[8]=(uint16_t)28375196U;f->words[9]=(uint16_t)(28375196U>>16U);
    f->words[10]=0x5678U;f->words[11]=0x1234U;
    f->words[12]=1U;f->words[13]=1U;f->words[14]=MT_AMIGA_DIAG_COUNTERS;
    for(unsigned i=0U;i<MT_AMIGA_DIAG_COUNTERS;++i)count(f,i,UINT64_C(0x0001000200030004)+i);
    count(f,MT_AMIGA_DIAG_CAPTURE_CYCLES,UINT64_C(0x0010000000000064));
    count(f,MT_AMIGA_DIAG_CAPTURE_ACCEPTED,UINT64_C(0x001000000000003c));
    count(f,MT_AMIGA_DIAG_CAPTURE_HELD,40U);
    count(f,MT_AMIGA_DIAG_CLOSED_CYCLES,UINT64_C(0x100000001a));
    count(f,MT_AMIGA_DIAG_GAP_HELD,UINT64_C(0x1000000000));
    count(f,MT_AMIGA_DIAG_GAP_WRITE_WAIT,3U);count(f,MT_AMIGA_DIAG_GAP_READ_WAIT,5U);
    count(f,MT_AMIGA_DIAG_GAP_IDE_ACTIVE,7U);count(f,MT_AMIGA_DIAG_GAP_OTHER,11U);
}
static void rejected(mt_core_link_t *link,mt_amiga_diag_snapshot_t *out)
{
    mt_amiga_diag_snapshot_t before=*out;
    assert(mt_amiga_diag_read(link,0x2345U,4U,out)!=MT_LINK_OK);
    assert(memcmp(&before,out,sizeof(before))==0);
}
int main(void)
{
    fixture_t f;setup(&f);mt_core_link_t link;mt_core_link_init(&link,&f,rd,wr);
    mt_amiga_diag_snapshot_t out;memset(&out,0xa5,sizeof(out));
    f.capable=false;
    assert(mt_amiga_diag_control(&link,1U,MT_AMIGA_DIAG_ARM,4U)==MT_LINK_ABI_MISMATCH);
    rejected(&link,&out);assert(f.writes==0U);
    f.capable=true;
    assert(mt_amiga_diag_control(&link,0U,MT_AMIGA_DIAG_ARM,4U)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_diag_control(&link,1U,0U,4U)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_diag_control(&link,1U,3U,4U)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_diag_read(&link,1U,4U,NULL)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DIAG_READ,128U,4U,NULL)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DIAG_CONTROL,0x10003U,4U,NULL)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_host_access(&link,14U,0U,4U,NULL)==MT_LINK_BAD_ARGUMENT);
    assert(f.writes==0U);
    /* Capability22 alone is sufficient; unrelated display/CPU caps are not required. */
    assert(mt_amiga_diag_control(&link,UINT16_MAX,MT_AMIGA_DIAG_ARM,4U)==MT_LINK_OK);
    assert(mt_amiga_diag_control(&link,0x2345U,MT_AMIGA_DIAG_STOP,4U)==MT_LINK_OK);
    assert(mt_amiga_diag_read(&link,0x2345U,4U,&out)==MT_LINK_OK);
    assert(f.reads==133U && out.sequence==0x2345U && out.service_hz==28375196U && out.epoch==0x12345678U);
    assert(out.status==7U && out.config_begin==0x166bU && out.config_end==0x166bU);
    assert(out.counters[MT_AMIGA_DIAG_CAPTURE_CYCLES]==UINT64_C(0x0010000000000064));
    assert(out.counters[MT_AMIGA_DIAG_CPU_DDR_WRITE_TX]==UINT64_C(0x0001000200030004)+27U);
    assert(out.counters[MT_AMIGA_DIAG_CLOSED_CYCLES]==UINT64_C(0x100000001a));
    for(unsigned reason=MT_AMIGA_DIAG_ABORT_RESET;reason<=MT_AMIGA_DIAG_EVENT_ORDER_ERROR;reason<<=1U) {
        setup(&f);f.words[4]=(uint16_t)(MT_AMIGA_DIAG_FROZEN|reason);f.words[6]^=1U;
        assert(mt_amiga_diag_read(&link,0x2345U,4U,&out)==MT_LINK_OK);
        assert(!(out.status&MT_AMIGA_DIAG_VALID) && (out.status&reason)!=0U);
    }
    static const unsigned bad_headers[]={0U,1U,2U,3U,4U,7U,12U,13U,14U,15U};
    for(unsigned i=0U;i<sizeof(bad_headers)/sizeof(bad_headers[0]);++i) {
        setup(&f);f.words[bad_headers[i]]^=0x8000U;rejected(&link,&out);
    }
    setup(&f);f.words[8]=f.words[9]=0U;rejected(&link,&out);
    setup(&f);++f.words[8];rejected(&link,&out);
    setup(&f);f.words[4]=MT_AMIGA_DIAG_VALID;rejected(&link,&out);
    setup(&f);f.words[4]|=MT_AMIGA_DIAG_ABORT_RESET;rejected(&link,&out);
    setup(&f);f.words[6]^=1U;rejected(&link,&out);
    setup(&f);count(&f,MT_AMIGA_DIAG_GAP_OTHER,12U);rejected(&link,&out);
    setup(&f);count(&f,MT_AMIGA_DIAG_GAP_HELD,UINT64_MAX);rejected(&link,&out);
    setup(&f);count(&f,MT_AMIGA_DIAG_CAPTURE_ACCEPTED,UINT64_MAX);rejected(&link,&out);
    static const unsigned torn_headers[]={0U,3U,4U,10U,11U};
    for(unsigned i=0U;i<sizeof(torn_headers)/sizeof(torn_headers[0]);++i) {
        setup(&f);f.torn=true;f.torn_index=torn_headers[i];rejected(&link,&out);
    }
    setup(&f);f.fail_at=61U;rejected(&link,&out);assert(f.reads==61U);
    setup(&f);f.no_toggle=true;rejected(&link,&out);assert(f.reads==1U);
    setup(&f);f.hold=true;rejected(&link,&out);assert(f.writes==0U);
    setup(&f);f.wrong_ack=true;assert(mt_amiga_diag_control(&link,1U,MT_AMIGA_DIAG_ARM,4U)==MT_LINK_REMOTE_ERROR);
    puts("PASS coherent optional native diagnostic ABI, full64 endian decode, ACK/epoch/schema/counter validation and no partial output");
    return 0;
}
