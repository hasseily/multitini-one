/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_boot.h"
#include <string.h>
mt_link_result_t mt_amiga_host_access(mt_core_link_t *link,uint8_t opcode,uint32_t payload,uint32_t polls,uint32_t *response)
{
    if(link==NULL || link->read32==NULL || link->write32==NULL || polls==0U || opcode>MT_AMIGA_HOST_DIAG_READ ||
        payload>0xffffffU || (opcode==MT_AMIGA_HOST_RTC_COMMIT && payload>0xffffU) ||
        (opcode==MT_AMIGA_HOST_DISPLAY_REQUEST && (payload>0xffffU || payload<4U)) ||
        (opcode==MT_AMIGA_HOST_DISPLAY_READ && payload>17U) ||
        (opcode==MT_AMIGA_HOST_DISPLAY_APPLY && payload>3U) ||
        (opcode==MT_AMIGA_HOST_READ_CONFIG && payload!=0U) ||
        (opcode==MT_AMIGA_HOST_DIAG_CONTROL && ((payload>>8U)==0U ||
            (uint8_t)payload<MT_AMIGA_DIAG_ARM || (uint8_t)payload>MT_AMIGA_DIAG_STOP)) ||
        (opcode==MT_AMIGA_HOST_DIAG_READ && payload>=MT_AMIGA_DIAG_WORDS))return MT_LINK_BAD_ARGUMENT;
    uint32_t before=0U;
    for(uint32_t i=0U;;++i) {
        before=link->read32(link->context,MT_AMIGA_HOST_PORT);
        if(!(before&MT_AMIGA_HOST_BUSY))break;
        if(i>=polls)return MT_LINK_TIMEOUT;
    }
    if(opcode>=MT_AMIGA_HOST_RTC_LOW && opcode<=MT_AMIGA_HOST_RTC_COMMIT && !(before&MT_AMIGA_HOST_RTC))return MT_LINK_ABI_MISMATCH;
    if(opcode>=MT_AMIGA_HOST_DISPLAY_REQUEST && opcode<=MT_AMIGA_HOST_DISPLAY_APPLY && !(before&MT_AMIGA_HOST_DISPLAY))return MT_LINK_ABI_MISMATCH;
    if(opcode==MT_AMIGA_HOST_READ_CONFIG && !(before&(MT_AMIGA_HOST_CPU14|MT_AMIGA_HOST_FAST_IDE)))return MT_LINK_ABI_MISMATCH;
    if(opcode>=MT_AMIGA_HOST_DIAG_CONTROL && !(before&MT_AMIGA_HOST_DIAGNOSTICS))return MT_LINK_ABI_MISMATCH;
    if(((opcode==MT_AMIGA_HOST_DISPLAY_REQUEST && (payload&3U)==3U) ||
        (opcode==MT_AMIGA_HOST_DISPLAY_APPLY && payload==3U)) && !(before&MT_AMIGA_HOST_SHRES))return MT_LINK_ABI_MISMATCH;
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
mt_link_result_t mt_amiga_diag_control(mt_core_link_t *link,uint16_t sequence,
    uint8_t action,uint32_t polls)
{
    if(sequence==0U || action<MT_AMIGA_DIAG_ARM || action>MT_AMIGA_DIAG_STOP)return MT_LINK_BAD_ARGUMENT;
    uint32_t response=0U;
    const mt_link_result_t result=mt_amiga_host_access(link,MT_AMIGA_HOST_DIAG_CONTROL,
        ((uint32_t)sequence<<8U)|action,polls,&response);
    return result!=MT_LINK_OK ? result:(uint16_t)response==sequence ? MT_LINK_OK:MT_LINK_REMOTE_ERROR;
}
mt_link_result_t mt_amiga_diag_read(mt_core_link_t *link,uint16_t sequence,
    uint32_t polls,mt_amiga_diag_snapshot_t *out)
{
    if(sequence==0U || out==NULL)return MT_LINK_BAD_ARGUMENT;
    uint16_t words[MT_AMIGA_DIAG_WORDS];
    for(unsigned i=0U;i<MT_AMIGA_DIAG_WORDS;++i) {
        uint32_t response=0U;
        const mt_link_result_t result=mt_amiga_host_access(link,MT_AMIGA_HOST_DIAG_READ,i,polls,&response);
        if(result!=MT_LINK_OK)return result;
        words[i]=(uint16_t)response;
    }
    if(words[0]!=0x4447U || words[1]!=1U || words[2]!=MT_AMIGA_DIAG_WORDS || words[3]!=sequence ||
       !(words[4]&MT_AMIGA_DIAG_FROZEN) || (words[4]&~UINT16_C(0x007f))!=0U ||
       words[7]!=0U || ((uint32_t)words[8]|((uint32_t)words[9]<<16U))!=MT_AMIGA_DIAG_SERVICE_HZ || words[12]!=1U || words[13]!=1U ||
       words[14]!=MT_AMIGA_DIAG_COUNTERS || words[15]!=0U || (words[5]&0xe000U)!=0U ||
       (words[6]&0xe000U)!=0U)return MT_LINK_ABI_MISMATCH;
    /* Reset or a competing capture must not turn a torn bank into plausible
     * counters. Recheck the identifying header after all data words. */
    static const uint8_t identity[]={0U,3U,4U,10U,11U};
    for(unsigned i=0U;i<sizeof(identity);++i) {
        uint32_t response=0U;
        const mt_link_result_t result=mt_amiga_host_access(link,MT_AMIGA_HOST_DIAG_READ,identity[i],polls,&response);
        if(result!=MT_LINK_OK)return result;
        if((uint16_t)response!=words[identity[i]])return MT_LINK_REMOTE_ERROR;
    }
    mt_amiga_diag_snapshot_t value;memset(&value,0,sizeof(value));
    value.sequence=sequence;value.status=words[4];value.config_begin=words[5];value.config_end=words[6];
    value.service_hz=(uint32_t)words[8]|((uint32_t)words[9]<<16U);
    value.epoch=(uint32_t)words[10]|((uint32_t)words[11]<<16U);
    for(unsigned i=0U;i<MT_AMIGA_DIAG_COUNTERS;++i)for(unsigned w=0U;w<4U;++w)
        value.counters[i]|=(uint64_t)words[16U+i*4U+w]<<(w*16U);
    if(value.status&MT_AMIGA_DIAG_VALID) {
        if((value.status&UINT16_C(0x0078))!=0U || value.config_begin!=value.config_end)return MT_LINK_REMOTE_ERROR;
        uint64_t sum=0U;
        for(unsigned i=MT_AMIGA_DIAG_GAP_HELD;i<=MT_AMIGA_DIAG_GAP_OTHER;++i) {
            if(UINT64_MAX-sum<value.counters[i])return MT_LINK_REMOTE_ERROR;
            sum+=value.counters[i];
        }
        if(sum!=value.counters[MT_AMIGA_DIAG_CLOSED_CYCLES] ||
           UINT64_MAX-value.counters[MT_AMIGA_DIAG_CAPTURE_ACCEPTED]<value.counters[MT_AMIGA_DIAG_CAPTURE_HELD] ||
           value.counters[MT_AMIGA_DIAG_CAPTURE_ACCEPTED]+value.counters[MT_AMIGA_DIAG_CAPTURE_HELD]!=
               value.counters[MT_AMIGA_DIAG_CAPTURE_CYCLES])return MT_LINK_REMOTE_ERROR;
    }
    *out=value;return MT_LINK_OK;
}
mt_link_result_t mt_amiga_bulk_transfer(mt_core_link_t *link,bool capture,
    uint8_t sector[MT_AMIGA_SECTOR_BYTES],uint32_t polls,bool *submitted)
{
    if(submitted!=NULL)*submitted=false;
    if(link==NULL || link->read32==NULL || link->write32==NULL || sector==NULL || polls==0U)
        return MT_LINK_BAD_ARGUMENT;
    uint32_t host_status=0U,status=0U;
    for(uint32_t i=0U;i<polls;++i) {
        host_status=link->read32(link->context,MT_AMIGA_HOST_PORT);
        if(!(host_status&MT_AMIGA_HOST_BULK))return MT_LINK_ABI_MISMATCH;
        status=link->read32(link->context,MT_AMIGA_BULK_STATUS);
        if((host_status&MT_AMIGA_HOST_ERROR) || (status&MT_AMIGA_BULK_ERROR))return MT_LINK_REMOTE_ERROR;
        if(!((host_status&MT_AMIGA_HOST_BUSY) || (status&MT_AMIGA_BULK_BUSY)))break;
        if(i+1U==polls)return MT_LINK_TIMEOUT;
    }
    uint16_t sequence=(uint16_t)((status&0xffffU)+1U);
    if(sequence==0U)sequence=1U;
    if(!capture)for(unsigned i=0U;i<128U;++i) {
        const unsigned at=i*4U;
        const uint32_t word=(uint32_t)sector[at]|((uint32_t)sector[at+1U]<<8U)|
            ((uint32_t)sector[at+2U]<<16U)|((uint32_t)sector[at+3U]<<24U);
        link->write32(link->context,MT_AMIGA_BULK_DATA+i*4U,word);
    }
    /* Check staging ownership/error once more before the sole doorbell. */
    status=link->read32(link->context,MT_AMIGA_BULK_STATUS);
    if(status&MT_AMIGA_BULK_ERROR)return MT_LINK_REMOTE_ERROR;
    if(status&MT_AMIGA_BULK_BUSY)return MT_LINK_TIMEOUT;
    link->write32(link->context,MT_AMIGA_BULK_COMMAND,
        ((uint32_t)sequence<<16U)|(256U<<1U)|(capture ? 1U:0U));
    if(submitted!=NULL)*submitted=true;
    for(uint32_t i=0U;i<polls;++i) {
        status=link->read32(link->context,MT_AMIGA_BULK_STATUS);
        if(status&MT_AMIGA_BULK_ERROR)return MT_LINK_REMOTE_ERROR;
        if(!(status&MT_AMIGA_BULK_BUSY) && (status&MT_AMIGA_BULK_DONE) &&
            (uint16_t)status==sequence) {
            if(link->read32(link->context,MT_AMIGA_BULK_PROGRESS)!=256U)return MT_LINK_REMOTE_ERROR;
            if(capture)for(unsigned word=0U;word<128U;++word) {
                const uint32_t value=link->read32(link->context,MT_AMIGA_BULK_DATA+word*4U);
                for(unsigned b=0U;b<4U;++b)sector[word*4U+b]=(uint8_t)(value>>(b*8U));
            }
            return MT_LINK_OK;
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
mt_link_result_t mt_amiga_native_configure(mt_core_link_t *link,const mt_amiga_config_t *c,bool aga_pal,uint32_t polls)
{
    if(!mt_amiga_config_valid(c) || (aga_pal && c->cpu==MT_AMIGA_CPU_68020_14))return MT_LINK_BAD_ARGUMENT;
    if(c->cpu==MT_AMIGA_CPU_68020_14 && (link==NULL || link->read32==NULL ||
        !(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_CPU14)))return MT_LINK_ABI_MISMATCH;
    const uint16_t cpu=c->cpu==MT_AMIGA_CPU_68020_14 ? 0x5fU:
        c->cpu==MT_AMIGA_CPU_68020_TURBO ? 0x1fU:aga_pal ? 0x23U:0U;
    const uint16_t chipset=(uint16_t)((aga_pal ? 0x18U:8U)|(c->ntsc<<1U)|(c->cpu!=MT_AMIGA_CPU_STOCK ? 1U:0U));
    const uint16_t memory=(uint16_t)(3U|((c->fast_mib==8U ? 3U:2U)<<4U));
    const uint32_t capabilities=link!=NULL && link->read32!=NULL ? link->read32(link->context,MT_AMIGA_HOST_PORT):0U;
    const uint16_t ide=mt_amiga_fast_ide_enabled(c,capabilities) ? 7U:0x27U;
    const uint8_t regs[]={3U,4U,5U,6U,7U,8U,9U,10U,11U,12U};
    const uint16_t values[]={chipset,cpu,memory,0U,
        (uint16_t)(4U|c->floppy_turbo),ide,0U,0U,0U,0U};
    for(unsigned i=0U;i<sizeof(regs);++i) {
        const mt_link_result_t r=uio(link,regs[i],values[i],polls);
        if(r!=MT_LINK_OK)return r;
    }
    return MT_LINK_OK;
}
mt_link_result_t mt_amiga_stage(mt_core_link_t *link,const mt_amiga_config_t *c,bool aga_pal,const uint8_t *rom,size_t bytes,
    uint32_t polls,mt_link_progress_fn progress,void *context)
{
    if(!mt_amiga_config_valid(c) || (aga_pal && c->cpu==MT_AMIGA_CPU_68020_14) ||
        rom==NULL || bytes!=MT_AMIGA_ROM_BYTES || polls==0U)return MT_LINK_BAD_ARGUMENT;
    if(c->cpu==MT_AMIGA_CPU_68020_14 && (link==NULL || link->read32==NULL ||
        !(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_CPU14)))return MT_LINK_ABI_MISMATCH;
    const uint32_t args[4]={1U,0U,0U,0U}; mt_command_response_t response;
    mt_link_result_t r=mt_core_link_command(link,MT_COMMAND_CORE_RESET,args,polls,progress,context,&response);
    if(r==MT_LINK_OK) {
        link->write32(link->context,MT_REG_ERROR,MT_ERROR_CORE_LOAD);
        r=mt_amiga_host_access(link,MT_AMIGA_HOST_CLEAR_ERROR,0U,polls,NULL);
    }
    uint8_t wire[MT_AMIGA_CONFIG_BYTES]; mt_amiga_config_encode(c,wire);
    /* Legacy loaders require byte10 zero. The saved policy is retained by
     * ARM; an older FPGA always uses the classic guest interface. */
    if(link==NULL || link->read32==NULL || !(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_FAST_IDE))wire[10]=0U;
    wire[11]=(uint8_t)aga_pal;
    if(r==MT_LINK_OK)r=mt_core_link_stream(link,MT_LOAD_AMIGA_CONFIG,wire,sizeof(wire),polls,progress,context);
    if(r==MT_LINK_OK)r=mt_amiga_native_configure(link,c,aga_pal,polls);
    if(r==MT_LINK_OK)r=mt_core_link_stream(link,MT_LOAD_AMIGA_KICKSTART,rom,bytes,polls,progress,context);
    return r;
}
