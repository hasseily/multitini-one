/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_content.h"
#include <string.h>
bool mt_amiga_config_valid(const mt_amiga_config_t *c)
{ return c!=NULL && (c->cpu==MT_AMIGA_CPU_STOCK || c->cpu==MT_AMIGA_CPU_68020_TURBO || c->cpu==MT_AMIGA_CPU_68020_14) && c->ntsc==0U &&
    c->fast_blitter==0U && c->floppy_turbo<=1U && (c->fast_mib==4U || c->fast_mib==8U) && c->ide_compat<=MT_AMIGA_IDE_CLASSIC; }
bool mt_amiga_fast_ide_enabled(const mt_amiga_config_t *c,uint32_t host_status)
{ return mt_amiga_config_valid(c) && c->cpu!=MT_AMIGA_CPU_STOCK && c->ide_compat==MT_AMIGA_IDE_AUTO &&
    (host_status&MT_AMIGA_HOST_FAST_IDE)!=0U; }
void mt_amiga_config_defaults(mt_amiga_config_t *c)
{ if(c!=NULL) { const mt_amiga_config_t value={MT_AMIGA_CPU_68020_TURBO,0U,0U,0U,8U,MT_AMIGA_IDE_AUTO}; *c=value; } }
void mt_amiga_config_encode(const mt_amiga_config_t *c,uint8_t wire[MT_AMIGA_CONFIG_BYTES])
{
    memset(wire,0,MT_AMIGA_CONFIG_BYTES); memcpy(wire,"AM01",4U);
    wire[4]=c->cpu; wire[5]=c->ntsc; wire[6]=c->fast_blitter;
    wire[7]=c->floppy_turbo; wire[8]=c->fast_mib; wire[9]=2U; wire[10]=c->ide_compat;
}
bool mt_amiga_config_decode(const uint8_t wire[MT_AMIGA_CONFIG_BYTES],mt_amiga_config_t *c)
{
    if(wire==NULL || c==NULL || memcmp(wire,"AM01",4U)!=0 || wire[9]!=2U)return false;
    for(unsigned i=11U;i<MT_AMIGA_CONFIG_BYTES;++i)if(wire[i]!=0U)return false;
    const mt_amiga_config_t candidate={wire[4],wire[5],wire[6],wire[7],wire[8],wire[10]};
    if(!mt_amiga_config_valid(&candidate))return false;
    *c=candidate; return true;
}
bool mt_amiga_media_path_valid(const char *name)
{
    size_t component=0U;
    if(name==NULL || *name=='\0')return false;
    for(size_t i=0U;;++i) {
        const unsigned char ch=(unsigned char)name[i];
        if(i>MT_AMIGA_MEDIA_NAME_MAX || ch=='\\' || ch==':' || (ch!=0U && ch<32U))return false;
        if(ch=='/' || ch==0U) {
            if(i==component || name[i-1U]==' ' || name[i-1U]=='.')return false;
            if(ch==0U)return true;
            component=i+1U;
        }
    }
}
bool mt_amiga_rom_size_valid(size_t bytes)
{ return bytes==262144U || bytes==524288U || bytes==MT_AMIGA_ROM_BYTES; }
mt_content_validation_t mt_amiga_media_validate(unsigned drive,uint64_t bytes)
{
    if(drive>=MT_AMIGA_DRIVE_COUNT)return MT_CONTENT_INVALID_ARGUMENT;
    if(drive<2U)return bytes==MT_AMIGA_ADF_BYTES ? MT_CONTENT_VALID:MT_CONTENT_WRONG_SIZE;
    return bytes>=512U && bytes<=MT_AMIGA_MAX_HDF_BYTES && (bytes&511U)==0U ?
        MT_CONTENT_VALID:MT_CONTENT_WRONG_SIZE;
}
uint16_t mt_amiga_mfm_clock(uint16_t value,uint16_t *previous)
{
    value&=0x5555U;
    const uint16_t clocks=(uint16_t)(((uint32_t)value<<1U)|((uint32_t)value>>1U)|((uint32_t)(*previous&1U)<<15U));
    value|=(uint16_t)(clocks^0xaaaaU); *previous=value; return value;
}
static uint32_t decode32(const uint16_t *p)
{ return (((uint32_t)(p[0]&0x5555U)<<16U)|(p[1]&0x5555U))<<1U |
    ((uint32_t)(p[2]&0x5555U)<<16U)|(p[3]&0x5555U); }
static void encode32(uint16_t *p,uint32_t v)
{ p[0]=(uint16_t)(v>>17U)&0x5555U; p[1]=(uint16_t)(v>>1U)&0x5555U;
  p[2]=(uint16_t)(v>>16U)&0x5555U; p[3]=(uint16_t)v&0x5555U; }
bool mt_amiga_mfm_encode(unsigned track,unsigned sector,uint16_t sync,const uint8_t data[512],uint16_t words[MT_AMIGA_MFM_SECTOR_WORDS],uint16_t *previous)
{
    if(track>=160U || sector>=11U || data==NULL || words==NULL || previous==NULL)return false;
    memset(words,0,MT_AMIGA_MFM_SECTOR_WORDS*sizeof(*words));
    words[2]=sync; words[3]=sync;
    encode32(words+4U,UINT32_C(0xff000000)|((uint32_t)track<<16U)|((uint32_t)sector<<8U)|(11U-sector));
    uint32_t header=0U,checksum=0U;
    for(unsigned i=4U;i<24U;i+=2U)header^=((uint32_t)words[i]<<16U)|words[i+1U];
    encode32(words+24U,header&UINT32_C(0x55555555));
    for(unsigned i=0U;i<256U;++i) {
        const uint16_t value=(uint16_t)((uint16_t)data[2U*i]<<8U)|data[2U*i+1U];
        words[32U+i]=(uint16_t)(value>>1U)&0x5555U;
        words[288U+i]=value&0x5555U;
    }
    for(unsigned i=32U;i<544U;i+=2U)checksum^=((uint32_t)words[i]<<16U)|words[i+1U];
    encode32(words+28U,checksum&UINT32_C(0x55555555));
    for(unsigned i=0U;i<544U;++i) {
        if(i==2U || i==3U)*previous=words[i];
        else words[i]=mt_amiga_mfm_clock(words[i],previous);
    }
    return true;
}
bool mt_amiga_mfm_decode(const uint16_t words[MT_AMIGA_MFM_SECTOR_WORDS],unsigned expected_track,unsigned *sector,uint8_t data[512])
{
    if(words==NULL || sector==NULL || data==NULL || expected_track>=160U || words[2]!=0x4489U || words[3]!=0x4489U)return false;
    const uint32_t header=decode32(words+4U);
    const unsigned s=(unsigned)((header>>8U)&255U);
    if((header>>24U)!=255U || ((header>>16U)&255U)!=expected_track || s>=11U ||
        (header&255U)==0U || (header&255U)>11U)return false;
    uint32_t hc=0U,dc=0U;
    for(unsigned i=4U;i<24U;i+=2U)hc^=((uint32_t)words[i]<<16U)|words[i+1U];
    for(unsigned i=32U;i<544U;i+=2U)dc^=((uint32_t)words[i]<<16U)|words[i+1U];
    if((hc&UINT32_C(0x55555555))!=decode32(words+24U) ||
        (dc&UINT32_C(0x55555555))!=decode32(words+28U))return false;
    for(unsigned i=0U;i<256U;++i) {
        const uint16_t value=(uint16_t)((words[32U+i]&0x5555U)<<1U)|(words[288U+i]&0x5555U);
        data[2U*i]=(uint8_t)(value>>8U); data[2U*i+1U]=(uint8_t)value;
    }
    *sector=s; return true;
}
