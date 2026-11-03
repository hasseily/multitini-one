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

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0]<<24U)|((uint32_t)p[1]<<16U)|((uint32_t)p[2]<<8U)|p[3];
}

uint32_t mt_amiga_partition_type(const uint8_t header[4])
{
    if(memcmp(header,"DOS",3U)==0 && header[3]<=7U)return read_be32(header);
    /* PFS3 uses PFS1/PFS2 in the volume, and PFS3 to select its handler. */
    if(memcmp(header,"PFS",3U)==0 && (header[3]==1U || header[3]==2U))return MT_AMIGA_PFS3_DOSTYPE;
    return 0U;
}

bool mt_amiga_hardfile_layout(mt_amiga_hardfile_t *layout,uint32_t data_sectors,
    uint32_t dos_type,size_t handler_bytes)
{
    if(handler_bytes>MT_AMIGA_PFS_HANDLER_MAX || (handler_bytes&3U)!=0U)return false;
    unsigned heads=16U,sectors=32U;
    /* Use exact geometry. Padding the partition could change the location
     * where FFS expects to find its root block. */
    if(data_sectors%(heads*sectors)!=0U) {
        unsigned best=0U;
        for(unsigned h=1U;h<=16U;++h)for(unsigned s=1U;s<=63U;++s) {
            const unsigned span=h*s;
            if(span>best && data_sectors%span==0U) {
                heads=h;sectors=s;best=span;
            }
        }
    }
    const unsigned span=heads*sectors;
    const unsigned reserved=(512U+span-1U)/span;
    const uint32_t cylinders=data_sectors/span+reserved;
    const uint32_t first_lba=reserved*span;
    if(data_sectors==0U || cylinders>65535U || data_sectors+first_lba>MT_AMIGA_MAX_HDF_BYTES/512U)
        return false;
    *layout=(mt_amiga_hardfile_t){first_lba,cylinders,dos_type,(uint16_t)heads,(uint16_t)sectors,handler_bytes};
    return true;
}

static void rdb_word(uint8_t *block,unsigned index,uint32_t value)
{
    for(unsigned i=0U;i<4U;++i)block[index*4U+i]=(uint8_t)(value>>(24U-i*8U));
}

void mt_amiga_hardfile_sector(const mt_amiga_hardfile_t *layout,unsigned unit,
    const uint8_t *handler,uint32_t lba,uint8_t data[512])
{
    enum { RDSK=0U, PART=1U, FSHD=2U, LSEG=3U, SEGMENT_BYTES=492U };
    const unsigned span=(unsigned)layout->heads*layout->sectors;
    const uint32_t low=layout->first_lba/span,high=layout->cylinders-1U;
    const size_t segments=(layout->handler_bytes+SEGMENT_BYTES-1U)/SEGMENT_BYTES;
    unsigned words=64U;
    memset(data,0,512U);
    if(lba==RDSK) {
        memcpy(data,"RDSK",4U);
        rdb_word(data,4U,512U); /* BlockBytes */
        rdb_word(data,6U,UINT32_MAX); /* BadBlockList */
        rdb_word(data,7U,PART);
        rdb_word(data,8U,segments ? FSHD:UINT32_MAX);
        rdb_word(data,9U,UINT32_MAX); /* DriveInit */
        rdb_word(data,16U,layout->cylinders);
        rdb_word(data,17U,layout->sectors);
        rdb_word(data,18U,layout->heads);
        rdb_word(data,32U,RDSK);
        rdb_word(data,33U,layout->first_lba-1U);
        rdb_word(data,34U,low);
        rdb_word(data,35U,high);
        rdb_word(data,36U,span);
        rdb_word(data,38U,segments ? FSHD+(uint32_t)segments:PART);
    } else if(lba==PART) {
        memcpy(data,"PART",4U);
        rdb_word(data,4U,UINT32_MAX); /* Next */
        rdb_word(data,5U,unit==0U ? 1U:0U); /* Boot from master; automount both. */
        data[36]=3U;
        memcpy(data+37U,unit==0U ? "DH0":"DH1",3U);
        /* DosEnvec at longword 32. */
        rdb_word(data,32U,16U); /* TableSize */
        rdb_word(data,33U,128U); /* SizeBlock, in longs */
        rdb_word(data,35U,layout->heads);
        rdb_word(data,36U,1U); /* SectorPerBlock */
        rdb_word(data,37U,layout->sectors);
        rdb_word(data,38U,2U); /* Reserved */
        rdb_word(data,41U,low);
        rdb_word(data,42U,high);
        rdb_word(data,43U,30U); /* NumBuffers */
        rdb_word(data,45U,0x1fe00U); /* MaxTransfer */
        rdb_word(data,46U,0x7ffffffeU); /* Mask */
        rdb_word(data,48U,layout->dos_type);
    } else if(lba==FSHD && segments!=0U) {
        memcpy(data,"FSHD",4U);
        rdb_word(data,4U,UINT32_MAX); /* Next */
        rdb_word(data,8U,layout->dos_type);
        /* Let a newer handler already installed in FileSystem.resource win. */
        rdb_word(data,10U,0x180U); /* Patch SegList and GlobalVec */
        rdb_word(data,18U,LSEG);
        rdb_word(data,19U,UINT32_MAX);
    } else if(lba>=LSEG && lba-LSEG<segments) {
        const size_t offset=(size_t)(lba-LSEG)*SEGMENT_BYTES;
        size_t bytes=layout->handler_bytes-offset;
        if(bytes>SEGMENT_BYTES)bytes=SEGMENT_BYTES;
        memcpy(data,"LSEG",4U);
        rdb_word(data,4U,lba-LSEG+1U<segments ? lba+1U:UINT32_MAX);
        memcpy(data+20U,handler+offset,bytes);
        words=5U+(unsigned)bytes/4U;
    } else return;
    rdb_word(data,1U,words);
    rdb_word(data,3U,7U); /* HostID */
    uint32_t sum=0U;
    for(unsigned i=0U;i<words;++i)sum+=read_be32(data+4U*i);
    rdb_word(data,2U,0U-sum);
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
