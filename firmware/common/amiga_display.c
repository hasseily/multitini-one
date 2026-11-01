/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_display.h"
#include <string.h>

#define DISPLAY_POLLS 100000U
#define DISPLAY_POLL_MS 100U
#define DISPLAY_HEARTBEAT_MS 3000U
#define DISPLAY_REQUEST_MS 120000U
#define DISPLAY_FLAGS_VALID 47U
#define DISPLAY_FLAG_MAX 4U
#define DISPLAY_FLAG_BUSY 16U
#define DISPLAY_FLAG_SHRES 64U
#define DISPLAY_ERROR_TIMEOUT 256U
#define DISPLAY_ERROR_LINK 257U
#define DISPLAY_ERROR_APPLY 258U

static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0U;i<n;++i) {
        c^=p[i];
        for(unsigned b=0U;b<8U;++b)c=(c>>1U)^((0U-(c&1U))&UINT32_C(0xedb88320));
    }
    return ~c;
}
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U); }
static void put32(uint8_t *p,uint32_t v)
{ for(unsigned i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(8U*i)); }
static bool capable(const mt_amiga_display_t *d)
{
    return d!=NULL && d->link!=NULL && d->link->read32!=NULL && d->link->write32!=NULL &&
        (d->link->read32(d->link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_DISPLAY)!=0U;
}
static bool host(mt_amiga_display_t *d,uint8_t op,uint32_t payload,uint16_t *word)
{
    /* Other Amiga services own their sticky failures. Never clear an error
     * already present before this display transaction was issued. */
    if((d->link->read32(d->link->context,MT_AMIGA_HOST_PORT)&
        (MT_AMIGA_HOST_BUSY|MT_AMIGA_HOST_ERROR))!=0U)return false;
    uint32_t value=0U;
    const mt_link_result_t result=mt_amiga_host_access(d->link,op,payload,DISPLAY_POLLS,&value);
    if(result!=MT_LINK_OK) {
        if(result==MT_LINK_REMOTE_ERROR)
            (void)mt_amiga_host_access(d->link,MT_AMIGA_HOST_CLEAR_ERROR,0U,DISPLAY_POLLS,NULL);
        return false;
    }
    if(word!=NULL)*word=(uint16_t)value;
    return true;
}
static void fail(mt_amiga_display_t *d,uint16_t code)
{ d->pending=false;d->guest_result=code;d->status=MT_AMIGA_DISPLAY_FAILED; }
void mt_amiga_display_init(mt_amiga_display_t *d,mt_core_link_t *link,
    mt_amiga_display_apply_fn apply,mt_amiga_display_save_fn save,void *context)
{
    if(d==NULL)return;
    memset(d,0,sizeof(*d));d->link=link;d->apply=apply;d->save=save;d->context=context;
    d->requested=0U;d->applied=UINT8_MAX;d->saved=0U;
    d->capable=capable(d);
    d->status=d->capable ? MT_AMIGA_DISPLAY_NO_HELPER:MT_AMIGA_DISPLAY_UNAVAILABLE;
}
void mt_amiga_display_restore(mt_amiga_display_t *d,uint8_t mode)
{
    if(d==NULL || mode>=MT_AMIGA_DISPLAY_MODES)return;
    d->saved=mode;d->requested=mode;d->saved_valid=true;
}
bool mt_amiga_display_available(const mt_amiga_display_t *d)
{ return d!=NULL && d->capable && d->helper_healthy; }
uint8_t mt_amiga_display_mode_count(const mt_amiga_display_t *d)
{
    return mt_amiga_display_available(d) && (d->helper_flags&DISPLAY_FLAG_SHRES)!=0U &&
        (d->link->read32(d->link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_SHRES)!=0U ? 4U:3U;
}
bool mt_amiga_display_mode_available(const mt_amiga_display_t *d,uint8_t mode)
{ return mt_amiga_display_available(d) && mode<mt_amiga_display_mode_count(d); }
void mt_amiga_display_set_paused(mt_amiga_display_t *d,bool paused,uint32_t now_ms)
{
    if(d==NULL || paused==d->paused)return;
    if(paused) { d->paused=true;d->pause_ms=now_ms;return; }
    const uint32_t elapsed=now_ms-d->pause_ms;
    d->heartbeat_ms+=elapsed;d->request_ms+=elapsed;d->next_poll_ms+=elapsed;
    /* A deliberately stopped guest needs one heartbeat interval to wake.
     * Retain cached health, but grant only the ordinary bounded grace. */
    if(d->helper_healthy)d->heartbeat_ms=now_ms;
    d->paused=false;
}
bool mt_amiga_display_request(mt_amiga_display_t *d,uint8_t mode,uint32_t now_ms)
{
    if(d==NULL || d->paused || mode>=MT_AMIGA_DISPLAY_MODES || d->pending || !mt_amiga_display_mode_available(d,mode))return false;
    if(!capable(d)) { d->capable=false;d->status=MT_AMIGA_DISPLAY_UNAVAILABLE;return false; }
    uint16_t current=0U,ack=0U;
    if(!host(d,MT_AMIGA_HOST_DISPLAY_READ,2U,&current) || !host(d,MT_AMIGA_HOST_DISPLAY_READ,3U,&ack)) {
        fail(d,DISPLAY_ERROR_LINK);return false;
    }
    uint16_t sequence=(uint16_t)(((current>>2U)+1U)&0x3fffU);
    if(sequence==0U)sequence=1U;
    if(sequence==(ack>>2U)) { sequence=(uint16_t)((sequence+1U)&0x3fffU);if(sequence==0U)sequence=1U; }
    const uint16_t request=(uint16_t)((sequence<<2U)|mode);
    d->requested=mode;
    if(!host(d,MT_AMIGA_HOST_DISPLAY_REQUEST,request,NULL)) { fail(d,DISPLAY_ERROR_LINK);return false; }
    d->sequence=sequence;d->request_word=request;d->request_ms=now_ms;d->pending=true;
    d->guest_result=1U;d->status=MT_AMIGA_DISPLAY_PENDING;d->next_poll_ms=now_ms;
    return true;
}
static mt_amiga_display_info_t info(const uint16_t w[18])
{
    const mt_amiga_display_info_t value={w[6],w[7],w[8],(uint32_t)w[9]|((uint32_t)w[10]<<16U),
        (int16_t)w[11],(int16_t)w[12],(int16_t)w[13],(int16_t)w[14],(int16_t)w[16],(int16_t)w[17],w[15]};
    return value;
}
static bool matches(uint8_t mode,const mt_amiga_display_info_t *actual)
{
    const uint16_t width=mode==3U ? 1280U:mode==2U ? 724U:640U;
    const uint16_t height=mode==0U ? 256U:mode==2U ? 566U:512U;
    const uint32_t id=mode==3U ? UINT32_C(0x29024):mode==0U ? UINT32_C(0x29000):UINT32_C(0x29004);
    return actual->width==width && actual->height==height && actual->depth==(mode==3U ? 2U:4U) && actual->mode_id==id &&
        (actual->flags&DISPLAY_FLAGS_VALID)==DISPLAY_FLAGS_VALID &&
        (mode!=3U || (actual->flags&DISPLAY_FLAG_SHRES)!=0U) &&
        (mode!=2U || (actual->flags&DISPLAY_FLAG_MAX)!=0U) && (actual->flags&DISPLAY_FLAG_BUSY)==0U;
}
void mt_amiga_display_poll(mt_amiga_display_t *d,uint32_t now_ms)
{
    if(d==NULL || d->paused)return;
    if(!capable(d)) {
        d->capable=false;d->helper_healthy=false;d->pending=false;d->status=MT_AMIGA_DISPLAY_UNAVAILABLE;return;
    }
    d->capable=true;
    if(d->poll_started && (int32_t)(now_ms-d->next_poll_ms)<0)return;
    d->poll_started=true;
    d->next_poll_ms=now_ms+DISPLAY_POLL_MS;
    uint16_t words[18],ack_after=0U;
    /* The helper retains its old ACK while BUSY, publishes complete metadata
     * and result before its terminal ACK, then freezes that snapshot until
     * the next request. Read result before metadata and sample ACK again to
     * reject a concurrent completion; BUSY is progress, never completion. */
    for(unsigned i=0U;i<18U;++i)if(!host(d,MT_AMIGA_HOST_DISPLAY_READ,i,&words[i])) {
        d->helper_healthy=false;fail(d,DISPLAY_ERROR_LINK);return;
    }
    if(!host(d,MT_AMIGA_HOST_DISPLAY_READ,3U,&ack_after)) { fail(d,DISPLAY_ERROR_LINK);return; }
    if(words[0]!=UINT16_C(0x4d54) || words[1]!=1U || (words[15]&1U)==0U) {
        d->helper_healthy=false;d->pending=false;d->status=MT_AMIGA_DISPLAY_NO_HELPER;return;
    }
    d->helper_flags=words[15];
    if(!d->heartbeat_seen) {
        d->heartbeat_seen=true;d->heartbeat=words[5];d->heartbeat_ms=now_ms;
    } else if(words[5]!=d->heartbeat) {
        d->heartbeat=words[5];d->heartbeat_ms=now_ms;d->helper_healthy=true;
    }
    if((uint32_t)(now_ms-d->heartbeat_ms)>DISPLAY_HEARTBEAT_MS)d->helper_healthy=false;
    if(!d->pending) {
        if(!d->helper_healthy) { d->status=MT_AMIGA_DISPLAY_NO_HELPER;return; }
        if(d->status==MT_AMIGA_DISPLAY_NO_HELPER || d->status==MT_AMIGA_DISPLAY_UNAVAILABLE)d->status=MT_AMIGA_DISPLAY_READY;
        return;
    }
    /* ScreenMode/Intuition can suspend the daemon heartbeat while a valid
     * request is BUSY. Health gates new requests; an accepted request keeps
     * its own 120s lifetime and may finish with a stable matching terminal ACK. */
    if((uint32_t)(now_ms-d->request_ms)>DISPLAY_REQUEST_MS) { fail(d,DISPLAY_ERROR_TIMEOUT);return; }
    if(words[2]!=d->request_word)return;
    /* BUSY is progress guidance for the current request. The helper keeps
     * its old completion ACK until preferences and metadata are final. */
    if(words[4]==1U || (words[15]&DISPLAY_FLAG_BUSY)!=0U) {
        d->guest_result=1U;d->status=MT_AMIGA_DISPLAY_BUSY;return;
    }
    if(words[3]!=d->request_word || ack_after!=words[3])return;
    d->guest_result=words[4];
    if(words[4]!=0U) { fail(d,words[4]);return; }
    const mt_amiga_display_info_t actual=info(words);
    if(!matches(d->requested,&actual)) { fail(d,4U);return; }
    d->info=actual;
    if(d->apply==NULL || !d->apply(d->context,d->requested)) { fail(d,DISPLAY_ERROR_APPLY);return; }
    d->applied=d->requested;d->pending=false;d->status=MT_AMIGA_DISPLAY_APPLIED;
    if(d->save!=NULL && !d->save(d->context,d->applied)) { d->status=MT_AMIGA_DISPLAY_SAVE_FAILED;return; }
    d->saved=d->applied;d->saved_valid=true;
}
const char *mt_amiga_display_mode_name(uint8_t mode)
{
    static const char *const names[]={"640 x 256 / 16 colours","640 x 512 / 16 colours","724 x 566 / 16 colours","1280 x 512 / 4 colours"};
    return mode<MT_AMIGA_DISPLAY_MODES ? names[mode]:"Unknown resolution";
}
const char *mt_amiga_display_status_name(const mt_amiga_display_t *d)
{
    if(d==NULL || d->status==MT_AMIGA_DISPLAY_UNAVAILABLE)return "Display extension unavailable";
    switch(d->status) {
    case MT_AMIGA_DISPLAY_NO_HELPER:return "Install the MTDisplay guest helper";
    case MT_AMIGA_DISPLAY_READY:return "Ready";
    case MT_AMIGA_DISPLAY_PENDING:return "Waiting for MTDisplay";
    case MT_AMIGA_DISPLAY_BUSY:return "Waiting for Amiga preferences; answer any requester";
    case MT_AMIGA_DISPLAY_APPLIED:return "Applied and saved";
    case MT_AMIGA_DISPLAY_SAVE_FAILED:return "Active; display settings were not saved";
    case MT_AMIGA_DISPLAY_FAILED:
        switch(d->guest_result) {
        case 2U:return "Requested mode is unsupported";
        case 3U:return "Amiga preferences update failed";
        case 4U:return "Amiga screen does not match the request";
        case DISPLAY_ERROR_TIMEOUT:return "Display request timed out; retry";
        case DISPLAY_ERROR_APPLY:return "Capture profile could not be applied";
        default:return "Display transport failed; retry";
        }
    default:return "Ready";
    }
}
void mt_amiga_display_sidecar_encode(uint8_t mode,uint8_t out[MT_AMIGA_DISPLAY_SIDECAR_BYTES])
{
    memset(out,0,MT_AMIGA_DISPLAY_SIDECAR_BYTES);memcpy(out,"MTAMDIS1",8U);out[8]=1U;out[9]=mode;
    put32(out+12U,crc32(out,MT_AMIGA_DISPLAY_SIDECAR_BYTES));
}
bool mt_amiga_display_sidecar_decode(const uint8_t in[MT_AMIGA_DISPLAY_SIDECAR_BYTES],uint8_t *mode)
{
    if(in==NULL || mode==NULL || memcmp(in,"MTAMDIS1",8U)!=0 || in[8]!=1U ||
       in[9]>=MT_AMIGA_DISPLAY_MODES || in[10]!=0U || in[11]!=0U)return false;
    uint8_t copy[MT_AMIGA_DISPLAY_SIDECAR_BYTES];memcpy(copy,in,sizeof(copy));memset(copy+12U,0,4U);
    if(crc32(copy,sizeof(copy))!=le32(in+12U))return false;
    *mode=in[9];return true;
}
