/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_boot.h"
mt_link_result_t mt_amiga_host_access(mt_core_link_t *link,uint8_t opcode,uint32_t payload,uint32_t polls,uint32_t *response)
{
    if(link==NULL || link->read32==NULL || link->write32==NULL || polls==0U || opcode>MT_AMIGA_HOST_RTC_COMMIT ||
        payload>0xffffffU || (opcode==MT_AMIGA_HOST_RTC_COMMIT && payload>0xffffU))return MT_LINK_BAD_ARGUMENT;
    uint32_t before=0U;
    for(uint32_t i=0U;;++i) {
        before=link->read32(link->context,MT_AMIGA_HOST_PORT);
        if(!(before&MT_AMIGA_HOST_BUSY))break;
        if(i>=polls)return MT_LINK_TIMEOUT;
    }
    if(opcode>=MT_AMIGA_HOST_RTC_LOW && !(before&MT_AMIGA_HOST_RTC))return MT_LINK_ABI_MISMATCH;
    link->write32(link->context,MT_AMIGA_HOST_PORT,((uint32_t)opcode<<24U)|payload);
    for(uint32_t i=0U;i<polls;++i) {
        const uint32_t value=link->read32(link->context,MT_AMIGA_HOST_PORT);
        if(!(value&MT_AMIGA_HOST_BUSY) && ((value^before)&MT_AMIGA_HOST_TOGGLE)) {
            if(response!=NULL)*response=value;
            return value&MT_AMIGA_HOST_ERROR ? MT_LINK_REMOTE_ERROR:MT_LINK_OK;
        }
    }
    return MT_LINK_TIMEOUT;
}
static mt_link_result_t uio(mt_core_link_t *link,uint8_t reg,uint16_t value,uint32_t polls)
{
    mt_link_result_t r=mt_amiga_host_access(link,MT_AMIGA_HOST_SELECT,2U,polls,NULL);
    if(r==MT_LINK_OK)r=mt_amiga_host_access(link,MT_AMIGA_HOST_TRANSFER,0xf0U|reg,polls,NULL);
    if(r==MT_LINK_OK)r=mt_amiga_host_access(link,MT_AMIGA_HOST_TRANSFER,value,polls,NULL);
    const mt_link_result_t deselect=mt_amiga_host_access(link,MT_AMIGA_HOST_SELECT,0U,polls,NULL);
    return r==MT_LINK_OK ? deselect:r;
}
mt_link_result_t mt_amiga_native_configure(mt_core_link_t *link,const mt_amiga_config_t *c,uint32_t polls)
{
    if(!mt_amiga_config_valid(c))return MT_LINK_BAD_ARGUMENT;
    const uint16_t cpu=c->cpu==MT_AMIGA_CPU_68020_TURBO ? 0x1fU:0U;
    const uint16_t chipset=(uint16_t)(8U|(c->ntsc<<1U)|(c->cpu!=MT_AMIGA_CPU_STOCK ? 1U:0U));
    const uint16_t memory=(uint16_t)(3U|((c->fast_mib==8U ? 3U:2U)<<4U));
    const uint8_t regs[]={3U,4U,5U,6U,7U,8U,9U,10U,11U,12U};
    const uint16_t values[]={chipset,cpu,memory,0U,
        (uint16_t)(4U|c->floppy_turbo),7U,0U,0U,0U,0U};
    for(unsigned i=0U;i<sizeof(regs);++i) {
        const mt_link_result_t r=uio(link,regs[i],values[i],polls);
        if(r!=MT_LINK_OK)return r;
    }
    return MT_LINK_OK;
}
mt_link_result_t mt_amiga_stage(mt_core_link_t *link,const mt_amiga_config_t *c,const uint8_t *rom,size_t bytes,
    uint32_t polls,mt_link_progress_fn progress,void *context)
{
    if(!mt_amiga_config_valid(c) || rom==NULL || bytes!=MT_AMIGA_ROM_BYTES || polls==0U)return MT_LINK_BAD_ARGUMENT;
    const uint32_t args[4]={1U,0U,0U,0U}; mt_command_response_t response;
    mt_link_result_t r=mt_core_link_command(link,MT_COMMAND_CORE_RESET,args,polls,progress,context,&response);
    if(r==MT_LINK_OK) {
        link->write32(link->context,MT_REG_ERROR,MT_ERROR_CORE_LOAD);
        r=mt_amiga_host_access(link,MT_AMIGA_HOST_CLEAR_ERROR,0U,polls,NULL);
    }
    uint8_t wire[MT_AMIGA_CONFIG_BYTES]; mt_amiga_config_encode(c,wire);
    if(r==MT_LINK_OK)r=mt_core_link_stream(link,MT_LOAD_AMIGA_CONFIG,wire,sizeof(wire),polls,progress,context);
    if(r==MT_LINK_OK)r=mt_amiga_native_configure(link,c,polls);
    if(r==MT_LINK_OK)r=mt_core_link_stream(link,MT_LOAD_AMIGA_KICKSTART,rom,bytes,polls,progress,context);
    return r;
}
