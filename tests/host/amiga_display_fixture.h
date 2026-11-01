/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_display.h"
#include <assert.h>
#include <string.h>
typedef struct {
    mt_core_link_t link;
    uint16_t words[18];
    uint32_t status,cap;
    unsigned commands,requests,applies,saves,clears;
    bool apply_ok,save_ok,fail_read,race_ack,native_paused;
    uint8_t last_applied,last_saved;
} display_fixture_t;
static uint32_t display_read(void *context,uint32_t offset)
{
    display_fixture_t *f=context;assert(offset==MT_AMIGA_HOST_PORT);return f->cap|f->status;
}
static void display_write(void *context,uint32_t offset,uint32_t value)
{
    display_fixture_t *f=context;assert(offset==MT_AMIGA_HOST_PORT);++f->commands;
    const uint8_t op=(uint8_t)(value>>24U);const uint32_t payload=value&0xffffffU;
    uint16_t result=0U;
    if(op==MT_AMIGA_HOST_CLEAR_ERROR) { f->status&=~MT_AMIGA_HOST_ERROR;++f->clears; }
    else if(op==MT_AMIGA_HOST_DISPLAY_READ) {
        assert(payload<18U);
        if(f->fail_read) { f->fail_read=false;f->status|=MT_AMIGA_HOST_ERROR; }
        result=f->words[payload];
        if(payload==17U && f->race_ack) { f->race_ack=false;f->words[3]^=4U; }
    } else if(op==MT_AMIGA_HOST_DISPLAY_REQUEST) {
        assert(payload<=0xffffU && (payload>>2U)!=0U);
        f->words[2]=(uint16_t)payload;++f->requests;
        if(f->native_paused) { f->status|=MT_AMIGA_HOST_BUSY;return; }
    } else assert(!"unexpected display host command");
    f->status=(f->status&0xffff0000U)|result;
    f->status^=MT_AMIGA_HOST_TOGGLE;
}
static bool display_apply(void *context,uint8_t mode)
{ display_fixture_t *f=context;++f->applies;f->last_applied=mode;return f->apply_ok; }
static bool display_save(void *context,uint8_t mode)
{ display_fixture_t *f=context;assert(f->applies>f->saves);++f->saves;f->last_saved=mode;return f->save_ok; }
static void display_fixture_init(display_fixture_t *f,bool capable)
{
    memset(f,0,sizeof(*f));f->cap=capable ? MT_AMIGA_HOST_DISPLAY:0U;
    /* Startup has no target yet: VIEWPORT_MATCH must not gate the first request. */
    f->apply_ok=true;f->save_ok=true;f->words[0]=0x4d54U;f->words[1]=1U;f->words[15]=1U;
    f->link.context=f;f->link.read32=display_read;f->link.write32=display_write;
}
static void display_metadata(display_fixture_t *f,uint8_t mode,uint16_t result)
{
    f->words[3]=f->words[2];f->words[4]=result;f->words[6]=mode==3U ? 1280U:mode==2U ? 724U:640U;
    f->words[7]=mode==0U ? 256U:mode==2U ? 566U:512U;f->words[8]=mode==3U ? 2U:4U;
    f->words[9]=mode==3U ? 0x9024U:mode==0U ? 0x9000U:0x9004U;f->words[10]=2U;
    f->words[11]=(uint16_t)(int16_t)-42;f->words[12]=(uint16_t)(int16_t)-14;
    f->words[13]=681U;f->words[14]=551U;f->words[15]=result==1U ? 63U:47U;
    if(mode==3U)f->words[15]|=64U;
    f->words[16]=129U;f->words[17]=44U;
}
static void display_ready(mt_amiga_display_t *d,display_fixture_t *f)
{
    mt_amiga_display_init(d,&f->link,display_apply,display_save,f);
    mt_amiga_display_poll(d,0U);assert(!mt_amiga_display_available(d));
    ++f->words[5];mt_amiga_display_poll(d,1000U);assert(mt_amiga_display_available(d));
}
