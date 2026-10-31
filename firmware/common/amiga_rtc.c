/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_rtc.h"
#include <string.h>

bool mt_amiga_rtc_encode(const mt_clock_time_t *time,uint64_t *value)
{
    uint8_t registers[7];
    if(value==NULL || !mt_clock_encode(time,registers))return false;
    const uint8_t bytes[8]={registers[0],registers[1],registers[2],registers[3],
        registers[5],registers[6],registers[4],0x40U};
    uint64_t encoded=0U;
    for(unsigned i=0U;i<8U;++i)encoded|=(uint64_t)bytes[i]<<(i*8U);
    *value=encoded;return true;
}

mt_link_result_t mt_amiga_rtc_write(mt_core_link_t *link,const mt_clock_time_t *time,uint32_t polls)
{
    uint64_t value;
    if(link==NULL || link->read32==NULL || link->write32==NULL || polls==0U ||
        !mt_amiga_rtc_encode(time,&value))return MT_LINK_BAD_ARGUMENT;
    const uint32_t status=link->read32(link->context,MT_AMIGA_HOST_PORT);
    if(!(status&MT_AMIGA_HOST_RTC))return MT_LINK_OK;
    /* Do not clear an error owned by media service or another host client. */
    if(status&MT_AMIGA_HOST_ERROR)return MT_LINK_REMOTE_ERROR;
    if(status&MT_AMIGA_HOST_BUSY)return MT_LINK_TIMEOUT;
    mt_link_result_t r=mt_amiga_host_access(link,MT_AMIGA_HOST_RTC_LOW,(uint32_t)(value&0xffffffU),polls,NULL);
    if(r==MT_LINK_OK)r=mt_amiga_host_access(link,MT_AMIGA_HOST_RTC_MIDDLE,(uint32_t)((value>>24U)&0xffffffU),polls,NULL);
    if(r==MT_LINK_OK)r=mt_amiga_host_access(link,MT_AMIGA_HOST_RTC_COMMIT,(uint32_t)(value>>48U),polls,NULL);
    if(r!=MT_LINK_OK)(void)mt_amiga_host_access(link,MT_AMIGA_HOST_CLEAR_ERROR,0U,polls,NULL);
    return r;
}

void mt_amiga_rtc_init(mt_amiga_rtc_t *rtc)
{ if(rtc!=NULL)memset(rtc,0,sizeof(*rtc)); }

mt_link_result_t mt_amiga_rtc_update(mt_amiga_rtc_t *rtc,mt_core_link_t *link,
    const mt_clock_time_t *time,uint32_t now_ms,bool force,uint32_t polls)
{
    if(rtc==NULL || link==NULL || link->read32==NULL || link->write32==NULL || polls==0U)return MT_LINK_BAD_ARGUMENT;
    if(!force && rtc->attempted && (uint32_t)(now_ms-rtc->last_attempt_ms)<1000U)return rtc->result;
    rtc->attempted=true;rtc->last_attempt_ms=now_ms;
    rtc->supported=(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_RTC)!=0U;
    uint64_t value;
    if(!rtc->supported || !mt_amiga_rtc_encode(time,&value)) { rtc->result=MT_LINK_OK;return MT_LINK_OK; }
    if(!force && rtc->sent && rtc->value==value && rtc->result==MT_LINK_OK)return MT_LINK_OK;
    rtc->result=mt_amiga_rtc_write(link,time,polls);
    if(rtc->result==MT_LINK_OK) { rtc->value=value;rtc->sent=true; }
    return rtc->result;
}
