/* SPDX-License-Identifier: GPL-3.0-only */
#include "amiga_display_fixture.h"
#include <stdio.h>
static void legacy_and_codec(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,false);
    mt_amiga_display_init(&d,&f.link,display_apply,display_save,&f);
    mt_amiga_display_poll(&d,0U);assert(!mt_amiga_display_request(&d,2U,0U));
    assert(f.commands==0U && d.applied==UINT8_MAX && d.requested==0U && !d.saved_valid);
    uint8_t wire[MT_AMIGA_DISPLAY_SIDECAR_BYTES],mode=99U;
    for(uint8_t m=0U;m<4U;++m) { mt_amiga_display_sidecar_encode(m,wire);assert(mt_amiga_display_sidecar_decode(wire,&mode) && mode==m); }
    wire[9]^=1U;assert(!mt_amiga_display_sidecar_decode(wire,&mode));
    mt_amiga_display_sidecar_encode(1U,wire);wire[8]=2U;assert(!mt_amiga_display_sidecar_decode(wire,&mode));
    mt_amiga_display_sidecar_encode(4U,wire);assert(!mt_amiga_display_sidecar_decode(wire,&mode));
    assert(!mt_amiga_display_sidecar_decode(NULL,&mode));
}
static void acknowledgement_and_metadata(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    assert(mt_amiga_display_request(&d,2U,1100U));const uint16_t request=f.words[2];
    display_metadata(&f,2U,0U);f.words[3]=0U;mt_amiga_display_poll(&d,1200U);
    assert(d.pending && f.applies==0U && f.saves==0U);
    display_metadata(&f,2U,1U);f.words[3]=0U;mt_amiga_display_poll(&d,1300U);
    assert(d.status==MT_AMIGA_DISPLAY_BUSY && d.pending && f.applies==0U);
    assert(strstr(mt_amiga_display_status_name(&d),"requester")!=NULL && f.saves==0U);
    display_metadata(&f,2U,0U);f.race_ack=true;mt_amiga_display_poll(&d,1400U);
    assert(d.pending && f.applies==0U);f.words[3]=request;
    mt_amiga_display_poll(&d,1500U);
    assert(!d.pending && d.applied==2U && d.saved==2U && f.applies==1U && f.saves==1U);
    assert(d.info.min_x==-42 && d.info.min_y==-14 && d.info.view_x==129);
    mt_amiga_display_poll(&d,1600U);assert(f.applies==1U && f.saves==1U);
    for(unsigned bad=0U;bad<7U;++bad) {
        const uint32_t now=1700U+bad*200U;
        assert(mt_amiga_display_request(&d,0U,now));display_metadata(&f,0U,0U);
        if(bad==0U)f.words[6]=639U;
        else if(bad==1U)f.words[7]=512U;
        else if(bad==2U)f.words[8]=3U;
        else if(bad==3U)f.words[9]=0x9004U;
        else if(bad==4U)f.words[10]=3U;
        else if(bad==5U)f.words[15]&=(uint16_t)~32U;
        else f.words[15]&=(uint16_t)~8U;
        mt_amiga_display_poll(&d,now+100U);
        assert(!d.pending && d.status==MT_AMIGA_DISPLAY_FAILED && d.applied==2U && d.saved==2U && f.applies==1U);
    }
}
static void failures_and_retry(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    mt_amiga_display_restore(&d,1U);
    for(uint16_t result=2U;result<=4U;++result) {
        const uint32_t now=1000U+(uint32_t)result*200U;
        assert(mt_amiga_display_request(&d,2U,now));display_metadata(&f,2U,result);
        mt_amiga_display_poll(&d,now+100U);assert(!d.pending && d.saved==1U && f.applies==0U && f.saves==0U);
    }
    assert(mt_amiga_display_request(&d,0U,2100U));display_metadata(&f,0U,0U);f.apply_ok=false;
    mt_amiga_display_poll(&d,2200U);assert(f.applies==1U && f.saves==0U && d.applied==UINT8_MAX && d.saved==1U);
    f.apply_ok=true;f.save_ok=false;assert(mt_amiga_display_request(&d,0U,2300U));display_metadata(&f,0U,0U);
    mt_amiga_display_poll(&d,2400U);assert(d.applied==0U && d.saved==1U && d.status==MT_AMIGA_DISPLAY_SAVE_FAILED);
    f.save_ok=true;assert(mt_amiga_display_request(&d,0U,2500U));display_metadata(&f,0U,0U);
    mt_amiga_display_poll(&d,2600U);assert(d.applied==0U && d.saved==0U && d.status==MT_AMIGA_DISPLAY_APPLIED);
    assert(mt_amiga_display_request(&d,1U,2700U));const uint16_t old=f.words[2];
    mt_amiga_display_poll(&d,5000U);assert(d.pending && d.saved==0U && d.applied==0U);
    mt_amiga_display_poll(&d,122800U);assert(!d.pending && d.status==MT_AMIGA_DISPLAY_FAILED);
    ++f.words[5];mt_amiga_display_poll(&d,122900U);assert(mt_amiga_display_available(&d));
    assert(mt_amiga_display_request(&d,1U,123000U));display_metadata(&f,1U,0U);f.words[3]=old;
    mt_amiga_display_poll(&d,123100U);assert(d.pending && d.applied==0U);
    f.words[3]=f.words[2];mt_amiga_display_poll(&d,123200U);assert(d.applied==1U && d.saved==1U);
}
static void heartbeat_health_is_not_pending_lifetime(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    mt_amiga_display_restore(&d,0U);
    assert(mt_amiga_display_request(&d,2U,1100U));display_metadata(&f,2U,1U);
    f.words[3]=0U;f.words[15]=127U;
    mt_amiga_display_poll(&d,4101U);
    assert(d.pending && d.status==MT_AMIGA_DISPLAY_BUSY && !mt_amiga_display_available(&d));
    assert(f.applies==0U && f.saves==0U && d.saved==0U && d.applied==UINT8_MAX);
    const unsigned before=f.commands;
    assert(!mt_amiga_display_request(&d,1U,4150U) && f.commands==before);
    /* A completion crossing the twoACK reads must remain pending even when
     * the heartbeat is stale. Only the next stable terminal snapshot applies. */
    display_metadata(&f,2U,0U);f.race_ack=true;mt_amiga_display_poll(&d,4201U);
    assert(d.pending && f.applies==0U && f.saves==0U);
    f.words[3]=f.words[2];mt_amiga_display_poll(&d,4301U);
    assert(!d.pending && d.status==MT_AMIGA_DISPLAY_APPLIED && d.applied==2U && d.saved==2U);
    assert(!mt_amiga_display_available(&d) && f.applies==1U && f.saves==1U);
    mt_amiga_display_poll(&d,4401U);assert(f.applies==1U && f.saves==1U);
    ++f.words[5];mt_amiga_display_poll(&d,4501U);assert(mt_amiga_display_available(&d));

    assert(mt_amiga_display_request(&d,1U,4601U));display_metadata(&f,1U,1U);f.words[3]=0U;
    mt_amiga_display_poll(&d,8000U);assert(d.pending && d.status==MT_AMIGA_DISPLAY_BUSY);
    mt_amiga_display_poll(&d,124601U);assert(d.pending); /* Exact120s boundary. */
    mt_amiga_display_poll(&d,124702U);
    assert(!d.pending && d.status==MT_AMIGA_DISPLAY_FAILED && d.guest_result==256U);
    assert(d.applied==2U && d.saved==2U && f.applies==1U && f.saves==1U);
}
static void stale_heartbeat_does_not_relax_validation(void)
{
    for(unsigned bad=0U;bad<7U;++bad) {
        display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
        mt_amiga_display_restore(&d,1U);
        assert(mt_amiga_display_request(&d,0U,1100U));display_metadata(&f,0U,0U);
        if(bad==0U)f.cap=0U;
        else if(bad==1U)f.fail_read=true;
        else if(bad==2U)f.words[0]=0U;
        else if(bad==3U)f.words[1]=0U;
        else if(bad==4U)f.words[15]&=(uint16_t)~1U;
        else if(bad==5U)f.words[8]=3U;
        else f.words[4]=3U;
        mt_amiga_display_poll(&d,4101U);
        assert(!d.pending && d.saved==1U && d.applied==UINT8_MAX && f.applies==0U && f.saves==0U);
        if(bad==0U)assert(d.status==MT_AMIGA_DISPLAY_UNAVAILABLE);
        else if(bad>=2U && bad<=4U)assert(d.status==MT_AMIGA_DISPLAY_NO_HELPER);
        else assert(d.status==MT_AMIGA_DISPLAY_FAILED && d.guest_result==(bad==1U ? 257U:bad==5U ? 4U:3U));
    }
}
static void sequence_timeout_and_wrap(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    f.words[2]=(uint16_t)(0x3fffU<<2U);f.words[3]=4U;
    assert(mt_amiga_display_request(&d,2U,1100U));assert((f.words[2]>>2U)==2U);
    display_metadata(&f,2U,1U);
    for(uint32_t now=2100U;now<123000U;now+=1000U) { ++f.words[5];mt_amiga_display_poll(&d,now); }
    assert(!d.pending && f.applies==0U && f.saves==0U);
    display_fixture_init(&f,true);mt_amiga_display_init(&d,&f.link,display_apply,display_save,&f);
    mt_amiga_display_poll(&d,UINT32_MAX-1000U);++f.words[5];mt_amiga_display_poll(&d,UINT32_MAX-100U);
    assert(mt_amiga_display_request(&d,0U,UINT32_MAX-50U));display_metadata(&f,0U,0U);
    mt_amiga_display_poll(&d,50U);assert(d.applied==0U && d.saved==0U);
    f.words[15]=0U;++f.words[5];mt_amiga_display_poll(&d,150U);assert(!mt_amiga_display_available(&d));
}
static void transport_recovery(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    assert(mt_amiga_display_request(&d,2U,1100U));f.fail_read=true;
    mt_amiga_display_poll(&d,1200U);
    assert(!d.pending && !mt_amiga_display_available(&d) && f.applies==0U && f.saves==0U);
    ++f.words[5];mt_amiga_display_poll(&d,1300U);
    assert(f.clears==1U && mt_amiga_display_available(&d));
    assert(mt_amiga_display_request(&d,0U,1400U));display_metadata(&f,0U,0U);
    mt_amiga_display_poll(&d,1500U);assert(d.applied==0U && d.saved==0U && f.applies==1U);
    const unsigned before=f.commands;f.status|=MT_AMIGA_HOST_ERROR;
    assert(!mt_amiga_display_request(&d,2U,1600U));mt_amiga_display_poll(&d,1700U);
    assert(f.commands==before && f.clears==1U && (f.status&MT_AMIGA_HOST_ERROR)!=0U && f.saves==1U);
}
static void shres_capabilities_and_metadata(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    assert(mt_amiga_display_mode_count(&d)==3U && !mt_amiga_display_request(&d,3U,1100U));
    const unsigned before=f.commands;f.cap|=MT_AMIGA_HOST_SHRES;
    assert(!mt_amiga_display_request(&d,3U,1100U) && f.commands==before);
    f.words[15]|=64U;mt_amiga_display_poll(&d,1200U);
    assert(mt_amiga_display_mode_count(&d)==4U && mt_amiga_display_mode_available(&d,3U));
    for(unsigned bad=0U;bad<6U;++bad) {
        const uint32_t now=1300U+bad*200U;
        assert(mt_amiga_display_request(&d,3U,now));display_metadata(&f,3U,0U);
        if(bad==0U)f.words[8]=4U;
        else if(bad==1U)f.words[9]=0x9004U;
        else if(bad==2U)f.words[6]=1279U;
        else if(bad==3U)f.words[7]=511U;
        else if(bad==4U)f.words[15]&=(uint16_t)~32U;
        else f.words[15]&=(uint16_t)~64U;
        mt_amiga_display_poll(&d,now+100U);assert(!d.pending && f.applies==0U && f.saves==0U);
        f.words[15]|=64U;mt_amiga_display_poll(&d,now+200U);
    }
    assert(mt_amiga_display_request(&d,3U,2600U));display_metadata(&f,3U,0U);mt_amiga_display_poll(&d,2700U);
    assert(d.applied==3U && d.saved==3U && d.info.depth==2U && d.info.mode_id==0x29024U && f.saves==1U);
    assert(strstr(mt_amiga_display_mode_name(3U),"4 colours")!=NULL);
    f.cap&=~MT_AMIGA_HOST_SHRES;assert(mt_amiga_display_mode_count(&d)==3U && !mt_amiga_display_mode_available(&d,3U));
}
static void deliberate_pause_and_native_precondition(void)
{
    display_fixture_t f;mt_amiga_display_t d;display_fixture_init(&f,true);display_ready(&d,&f);
    mt_amiga_display_set_paused(&d,true,1100U);const unsigned before=f.commands;
    mt_amiga_display_poll(&d,61000U);
    assert(mt_amiga_display_available(&d) && f.commands==before && !mt_amiga_display_request(&d,1U,61000U));
    mt_amiga_display_set_paused(&d,false,61100U);
    assert(mt_amiga_display_request(&d,1U,61200U));mt_amiga_display_poll(&d,61300U);
    assert(d.pending && d.helper_healthy && f.saves==0U);
    mt_amiga_display_set_paused(&d,true,61400U);const uint32_t request_time=d.request_ms;
    mt_amiga_display_poll(&d,300000U);assert(d.pending);
    mt_amiga_display_set_paused(&d,false,300100U);
    assert(d.request_ms==request_time+(300100U-61400U));
    mt_amiga_display_poll(&d,300200U);assert(d.pending && d.helper_healthy);
    display_metadata(&f,1U,0U);mt_amiga_display_poll(&d,300300U);assert(d.applied==1U && d.saved==1U);
    f.native_paused=true;
    assert(!mt_amiga_display_request(&d,0U,300400U));
    assert(!d.pending && d.saved==1U && d.applied==1U && f.saves==1U);
    f.native_paused=false;f.status&=~MT_AMIGA_HOST_BUSY;f.status^=MT_AMIGA_HOST_TOGGLE;
    assert(mt_amiga_display_request(&d,0U,300500U));display_metadata(&f,0U,0U);
    mt_amiga_display_poll(&d,300600U);assert(d.applied==0U && d.saved==0U);
}
int main(void)
{
    legacy_and_codec();acknowledgement_and_metadata();failures_and_retry();heartbeat_health_is_not_pending_lifetime();stale_heartbeat_does_not_relax_validation();sequence_timeout_and_wrap();transport_recovery();shres_capabilities_and_metadata();deliberate_pause_and_native_precondition();
    puts("Amiga display protocol/metadata/sidecar/timeout tests passed");return 0;
}
