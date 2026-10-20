/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_iigs_content.h"
#include "multitini_media_browser.h"
#include <string.h>
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
    (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
bool mt_iigs_media_path_valid(const char *path)
{
    char component[MT_IIGS_MEDIA_NAME_MAX+1U];
    size_t used=0U;
    if(path==NULL || path[0]=='\0')return false;
    for(size_t i=0U;i<=MT_IIGS_MEDIA_NAME_MAX;++i) {
        const char ch=path[i];
        if(ch=='/' || ch=='\0') {
            component[used]='\0';
            if(!mt_media_browser_valid_component(component))return false;
            if(ch=='\0')return true;
            used=0U;
        } else component[used++]=ch;
    }
    return false;
}
static bool extension(const char *name, const char *ext)
{
    const char *p = name == NULL ? NULL : strrchr(name,'.');
    if (p == NULL) return false;
    ++p;
    while (*p != '\0' && *ext != '\0') {
        const char c = *p >= 'A' && *p <= 'Z' ? (char)(*p + ('a'-'A')) : *p;
        if (c != *ext++) return false;
        ++p;
    }
    return *p == '\0' && *ext == '\0';
}
bool mt_iigs_config_valid(const mt_iigs_config_t *c)
{ return c != NULL && c->rom3 <= 1U && c->speed < MT_IIGS_SPEED_COUNT && c->card <= 2U &&
    (c->pulses & 0xe0U) == 0U; }
uint8_t mt_iigs_next_speed(uint8_t current, uint32_t supported_mask)
{
    supported_mask &= (UINT32_C(1) << MT_IIGS_SPEED_COUNT) - UINT32_C(1);
    supported_mask |= UINT32_C(1);
    for (unsigned n=1U; n<=MT_IIGS_SPEED_COUNT; ++n) {
        const unsigned next=((unsigned)current+n)%MT_IIGS_SPEED_COUNT;
        if ((supported_mask & (UINT32_C(1)<<next))!=0U) return (uint8_t)next;
    }
    return 0U;
}
void mt_iigs_config_encode(const mt_iigs_config_t *c, uint8_t wire[MT_IIGS_CONFIG_BYTES])
{
    memset(wire,0,MT_IIGS_CONFIG_BYTES);
    wire[0]=c->rom3; wire[1]=c->speed; wire[2]=c->card; wire[3]=c->flags;
    for (unsigned i=0U;i<4U;++i) wire[4U+i]=(uint8_t)(c->timestamp>>(i*8U));
    wire[8]=c->pulses;
}
mt_content_validation_t mt_iigs_media_inspect(unsigned drive,const char *name,
    const uint8_t *p,size_t n,uint64_t size,mt_iigs_media_info_t *info)
{
    mt_iigs_media_info_t r={0U,size,MT_IIGS_MEDIA_BLOCK32,true,MT_IIGS_RAW_NONE};
    if (drive>=MT_IIGS_DRIVE_COUNT || name==NULL || p==NULL || info==NULL)
        return MT_CONTENT_INVALID_ARGUMENT;
    if (size==0U) return MT_CONTENT_TOO_SMALL;
    if (size>INT64_MAX) return MT_CONTENT_WRONG_SIZE;
    const bool hdd=mt_iigs_drive_hdd_unit(drive)<MT_IIGS_HDD_COUNT;
    if (extension(name,"woz")) {
        if (hdd) return MT_CONTENT_UNSUPPORTED_VERSION;
        if (size>UINT32_MAX) return MT_CONTENT_WRONG_SIZE;
        if (n<80U) return MT_CONTENT_TOO_SMALL;
        if (memcmp(p,"WOZ2\xff\x0a\x0d\x0a",8U)!=0)
            return MT_CONTENT_BAD_MAGIC;
        if (memcmp(p+12U,"INFO",4U)!=0 || le32(p+16U)<60U ||
            (uint64_t)le32(p+16U)+20U>size || p[20]==0U || p[20]>3U)
            return MT_CONTENT_BAD_STRUCTURE;
        if (p[21]!=(drive==2U ? 2U:1U)) return MT_CONTENT_BAD_STRUCTURE;
        r.kind=MT_IIGS_MEDIA_WOZ; r.writable=p[22]==0U;
    } else if (extension(name,"2mg")) {
        uint32_t offset,blocks;
        uint64_t length;
        const unsigned header_size=n>=10U ? (unsigned)p[8]+((unsigned)p[9]<<8):0U;
        if (n<64U) return MT_CONTENT_TOO_SMALL;
        if (memcmp(p,"2IMG",4U)!=0) return MT_CONTENT_BAD_MAGIC;
        if (header_size<64U || p[10]>1U || p[11]!=0U)
            return MT_CONTENT_UNSUPPORTED_VERSION;
        if (le32(p+12U)!=1U) return MT_CONTENT_UNSUPPORTED_VERSION; /* ProDOS blocks */
        offset=le32(p+24U); length=le32(p+28U); blocks=le32(p+20U);
        /* Sweet 16 / WOOF images can leave data_length zero. The block count
         * remains authoritative; never infer past the declared file bounds. */
        if (length==0U)length=(uint64_t)blocks*512U;
        if (offset<header_size || length==0U || (length&511U)!=0U ||
            (uint64_t)offset+length>size || blocks!=length/512U)
            return MT_CONTENT_BAD_STRUCTURE;
        r.offset=offset; r.size=length; r.writable=(le32(p+16U)&UINT32_C(0x80000000))==0U;
    } else if (drive==3U && extension(name,"dsk")) {
        if (size!=143360U) return MT_CONTENT_WRONG_SIZE;
        r.kind=MT_IIGS_MEDIA_WOZ; r.raw_format=MT_IIGS_RAW_DOS525;
    } else if (!extension(name,"hdv") && !extension(name,"po")) {
        return MT_CONTENT_UNSUPPORTED_VERSION;
    }
    if (drive==2U && r.kind!=MT_IIGS_MEDIA_WOZ) {
        if (extension(name,"hdv")) return MT_CONTENT_UNSUPPORTED_VERSION;
        if (r.size!=819200U) return MT_CONTENT_WRONG_SIZE;
        r.kind=MT_IIGS_MEDIA_WOZ; r.raw_format=MT_IIGS_RAW_PRODOS35;
    }
    if (drive==3U && r.kind!=MT_IIGS_MEDIA_WOZ) return MT_CONTENT_UNSUPPORTED_VERSION;
    if (hdd && ((r.size&511U)!=0U || r.size>MT_IIGS_MAX_HDD_BYTES))
        return MT_CONTENT_WRONG_SIZE;
    *info=r; return MT_CONTENT_VALID;
}

/* The 524-byte encoder/decoder below is a C adaptation of CiderPress2
 * DiskArc/SectorCodec.cs, commit 7a055a200e31f752f3a92bb9fe6ae6f67cd55534.
 * Copyright 2022 faddenSoft. Licensed under the Apache License, Version 2.0;
 * http://www.apache.org/licenses/LICENSE-2.0 . Distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND. See the License for the
 * specific language governing permissions and limitations under the License.
 * Modified for bounded buffers and the Multitini streaming media service.
 * Other track framing follows the standard Apple 16-sector / 800K formats.
 */
static const uint8_t gcr62[64]={
    0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6,0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3,
    0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3,
    0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec,
    0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};
static uint8_t ungcr(uint8_t value)
{
    for(unsigned i=0U;i<64U;++i)if(gcr62[i]==value)return (uint8_t)i;
    return 0xffU;
}
static unsigned floppy_tracks(uint8_t format)
{ return format==MT_IIGS_RAW_DOS525 ? 35U:format==MT_IIGS_RAW_PRODOS35 ? 160U:0U; }
static unsigned floppy_slot_blocks(uint8_t format)
{ return format==MT_IIGS_RAW_DOS525 ? 13U:20U; }
bool mt_iigs_floppy_track(uint8_t format,unsigned track,mt_iigs_floppy_track_t *info)
{
    if(info==NULL || track>=floppy_tracks(format))return false;
    mt_iigs_floppy_track_t t;
    if(format==MT_IIGS_RAW_DOS525) {
        t.raw_offset=track*4096U; t.raw_bytes=4096U; t.bit_count=52224U;
    } else {
        const unsigned cylinder=track/2U, zone=cylinder/16U, sectors=12U-zone;
        unsigned first=0U;
        for(unsigned z=0U;z<zone;++z)first+=(12U-z)*32U;
        first+=((cylinder%16U)*2U+(track&1U))*sectors;
        t.raw_offset=first*512U; t.raw_bytes=(uint16_t)(sectors*512U);
        t.bit_count=1480U+sectors*6358U;
    }
    t.first_block=(uint16_t)(3U+track*floppy_slot_blocks(format));
    t.block_count=(uint16_t)((t.bit_count+4095U)/4096U);
    *info=t; return true;
}
uint32_t mt_iigs_floppy_woz_size(uint8_t format)
{ return floppy_tracks(format)==0U ? 0U:(3U+floppy_tracks(format)*floppy_slot_blocks(format))*512U; }
static void put16(uint8_t *p,unsigned value)
{ p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8U); }
static void put32(uint8_t *p,uint32_t value)
{ for(unsigned i=0U;i<4U;++i)p[i]=(uint8_t)(value>>(i*8U)); }
bool mt_iigs_floppy_header(uint8_t format,bool writable,uint8_t header[MT_IIGS_FLOPPY_HEADER_BYTES])
{
    const unsigned count=floppy_tracks(format);
    if(header==NULL || count==0U)return false;
    memset(header,0,MT_IIGS_FLOPPY_HEADER_BYTES);
    memcpy(header,"WOZ2\xff\x0a\x0d\x0a",8U);
    memcpy(header+12U,"INFO",4U); put32(header+16U,60U);
    header[20]=2U; header[21]=format==MT_IIGS_RAW_PRODOS35 ? 2U:1U;
    header[22]=(uint8_t)(writable ? 0U:1U); header[24]=1U;
    memset(header+25U,' ',32U); memcpy(header+25U,"Multitini raw floppy",20U);
    header[57]=format==MT_IIGS_RAW_PRODOS35 ? 2U:1U;
    header[58]=format==MT_IIGS_RAW_DOS525 ? 1U:0U;
    header[59]=format==MT_IIGS_RAW_PRODOS35 ? 16U:32U;
    put16(header+64U,floppy_slot_blocks(format));
    memcpy(header+80U,"TMAP",4U); put32(header+84U,160U); memset(header+88U,0xff,160U);
    memcpy(header+248U,"TRKS",4U); put32(header+252U,mt_iigs_floppy_woz_size(format)-256U);
    for(unsigned i=0U;i<160U;++i) {
        if(i>=count)break;
        mt_iigs_floppy_track_t t;
        (void)mt_iigs_floppy_track(format,i,&t);
        if(format==MT_IIGS_RAW_PRODOS35)header[88U+i]=(uint8_t)i;
        else {
            header[88U+i*4U]=(uint8_t)i;
            header[89U+i*4U]=(uint8_t)i;
            if(i!=0U)header[87U+i*4U]=(uint8_t)i;
        }
        uint8_t *entry=header+256U+i*8U;
        put16(entry,t.first_block); put16(entry+2U,t.block_count); put32(entry+4U,t.bit_count);
    }
    return true;
}
typedef struct { uint8_t *data; uint32_t position; } bit_writer_t;
static void emit(bit_writer_t *w,unsigned value,unsigned count)
{
    for(unsigned i=count;i>0U;--i) {
        if((value&(1U<<(i-1U)))!=0U)w->data[w->position/8U]|=(uint8_t)(0x80U>>(w->position&7U));
        ++w->position;
    }
}
static void sync_bytes(bit_writer_t *w,unsigned count)
{ for(unsigned i=0U;i<count;++i)emit(w,0x3fcU,10U); }
static void prologue(bit_writer_t *w,unsigned last)
{ emit(w,0xd5U,8U); emit(w,0xaaU,8U); emit(w,last,8U); }
static void epilogue(bit_writer_t *w,unsigned last)
{ emit(w,0xdeU,8U); emit(w,0xaaU,8U); emit(w,last,8U); }
static uint8_t swap_low(uint8_t value)
{ return (uint8_t)(((value&1U)<<1U)|((value&2U)>>1U)); }
static void encode256(const uint8_t *input,uint8_t output[343])
{
    uint8_t low[86]={0},last=0U;
    for(unsigned i=0U;i<256U;++i)low[i%86U]|=(uint8_t)(swap_low(input[i])<<((i/86U)*2U));
    for(unsigned i=0U;i<342U;++i) {
        const uint8_t v=i<86U ? low[i]:(uint8_t)(input[i-86U]>>2U);
        output[i]=gcr62[v^last]; last=v;
    }
    output[342]=gcr62[last];
}
static bool decode256(const uint8_t input[343],uint8_t output[256])
{
    uint8_t low[86],last=0U;
    for(unsigned i=0U;i<343U;++i) {
        const uint8_t code=ungcr(input[i]);
        if(code==0xffU)return false;
        const uint8_t value=code^last;
        if(i==342U)return value==0U;
        if(i<86U)low[i]=value;
        else {
            const unsigned n=i-86U;
            output[n]=(uint8_t)((value<<2U)|swap_low((uint8_t)((low[n%86U]>>((n/86U)*2U))&3U)));
        }
        last=value;
    }
    return false;
}
static void encode524(const uint8_t *input,uint8_t output[703])
{
    uint8_t part[3][175]={{0}};
    uint32_t c0=0U,c1=0U,c2=0U;
    unsigned in=0U,out=0U,index=0U;
    for(;;) {
        c0=(c0&255U)<<1U; if((c0&256U)!=0U)++c0;
        uint8_t v=input[in++]; c2+=v;
        if((c0&256U)!=0U) { ++c2; c0&=255U; }
        part[0][index]=(uint8_t)(v^c0);
        v=input[in++]; c1+=v;
        if(c2>255U) { ++c1; c2&=255U; }
        part[1][index]=(uint8_t)(v^c2);
        if(in==524U)break;
        v=input[in++]; c0+=v;
        if(c1>255U) { ++c0; c1&=255U; }
        part[2][index++]=(uint8_t)(v^c1);
    }
    for(unsigned i=0U;i<175U;++i) {
        output[out++]=gcr62[((part[0][i]&0xc0U)>>2U)|((part[1][i]&0xc0U)>>4U)|((part[2][i]&0xc0U)>>6U)];
        output[out++]=gcr62[part[0][i]&63U]; output[out++]=gcr62[part[1][i]&63U];
        if(i!=174U)output[out++]=gcr62[part[2][i]&63U];
    }
    output[out++]=gcr62[((c0&0xc0U)>>6U)|((c1&0xc0U)>>4U)|((c2&0xc0U)>>2U)];
    output[out++]=gcr62[c2&63U]; output[out++]=gcr62[c1&63U]; output[out]=gcr62[c0&63U];
}
static bool decode524(const uint8_t input[703],uint8_t output[524])
{
    uint8_t part[3][175],codes[703];
    for(unsigned i=0U;i<703U;++i) { codes[i]=ungcr(input[i]); if(codes[i]==0xffU)return false; }
    unsigned in=0U,out=0U,index=0U;
    for(unsigned i=0U;i<175U;++i) {
        const unsigned twos=codes[in++];
        part[0][i]=(uint8_t)(codes[in++]|((twos<<2U)&0xc0U));
        part[1][i]=(uint8_t)(codes[in++]|((twos<<4U)&0xc0U));
        part[2][i]=i==174U ? 0U:(uint8_t)(codes[in++]|((twos<<6U)&0xc0U));
    }
    uint32_t c0=0U,c1=0U,c2=0U;
    for(;;) {
        c0=(c0&255U)<<1U; if((c0&256U)!=0U)++c0;
        uint8_t v=(uint8_t)(part[0][index]^c0); c2+=v;
        if((c0&256U)!=0U) { ++c2; c0&=255U; }
        output[out++]=v;
        v=(uint8_t)(part[1][index]^c2); c1+=v;
        if(c2>255U) { ++c1; c2&=255U; }
        output[out++]=v;
        if(out==524U)break;
        v=(uint8_t)(part[2][index++]^c1); c0+=v;
        if(c1>255U) { ++c0; c1&=255U; }
        output[out++]=v;
    }
    const unsigned twos=codes[in++];
    return (uint8_t)c2==(uint8_t)(codes[in]|((twos<<2U)&0xc0U)) &&
        (uint8_t)c1==(uint8_t)(codes[in+1U]|((twos<<4U)&0xc0U)) &&
        (uint8_t)c0==(uint8_t)(codes[in+2U]|((twos<<6U)&0xc0U));
}
bool mt_iigs_floppy_encode(uint8_t format,unsigned track,const uint8_t *raw,
    uint8_t bits[MT_IIGS_FLOPPY_TRACK_BYTES])
{
    mt_iigs_floppy_track_t t;
    if(raw==NULL || bits==NULL || !mt_iigs_floppy_track(format,track,&t))return false;
    memset(bits,0,MT_IIGS_FLOPPY_TRACK_BYTES);
    bit_writer_t w={bits,0U}; uint8_t body[703],tagged[524];
    const bool sony=format==MT_IIGS_RAW_PRODOS35;
    const unsigned sectors=sony ? t.raw_bytes/512U:16U;
    sync_bytes(&w,sony ? 148U:48U);
    /* 4:1 interleave for the IIgs 3.5-inch driver. DOS uses sector addresses
     * 0..15 with the standard DOS-order file permutation. */
    unsigned order[16]={0},entry=0U;
    for(unsigned n=0U;n<sectors;++n) {
        while(order[entry]!=0U)entry=(entry+1U)%sectors;
        order[entry]=n+1U; entry=(entry+4U)%sectors;
    }
    for(unsigned s=0U;s<sectors;++s) {
        const unsigned number=sony ? order[s]-1U:s;
        prologue(&w,0x96U);
        if(sony) {
            const unsigned low=(track/2U)&63U,side=((track&1U)<<5U)|((track/2U)>>6U);
            const unsigned address[5]={low,number,side,0x24U,low^number^side^0x24U};
            for(unsigned a=0U;a<5U;++a)emit(&w,gcr62[address[a]],8U);
        } else {
            const unsigned address[4]={254U,track,number,254U^track^number};
            for(unsigned a=0U;a<4U;++a) {
                emit(&w,(address[a]>>1U)|0xaaU,8U); emit(&w,address[a]|0xaaU,8U);
            }
        }
        epilogue(&w,sony ? 0xffU:0xebU); sync_bytes(&w,sony ? 5U:6U); prologue(&w,0xadU);
        if(sony) {
            emit(&w,gcr62[number],8U); memset(tagged,0,12U); memcpy(tagged+12U,raw+number*512U,512U);
            encode524(tagged,body);
        } else {
            const unsigned file_sector=number==15U ? 15U:(number*7U)%15U;
            encode256(raw+file_sector*256U,body);
        }
        for(unsigned i=0U;i<(sony ? 703U:343U);++i)emit(&w,body[i],8U);
        epilogue(&w,sony ? 0xffU:0xebU); sync_bytes(&w,sony ? 54U:27U);
    }
    return w.position==t.bit_count;
}
typedef struct { const uint8_t *bits; uint32_t count,position,consumed; } bit_reader_t;
static uint8_t nibble(bit_reader_t *r)
{
    unsigned value=0U;
    while(r->consumed<r->count*2U) {
        value=((value<<1U)|((r->bits[r->position/8U]>>(7U-(r->position&7U)))&1U))&255U;
        r->position=(r->position+1U)%r->count; ++r->consumed;
        if((value&128U)!=0U)return (uint8_t)value;
    }
    return 0U;
}
bool mt_iigs_floppy_decode(uint8_t format,unsigned track,const uint8_t *bits,
    uint8_t raw[MT_IIGS_FLOPPY_RAW_TRACK_BYTES])
{
    mt_iigs_floppy_track_t t;
    if(bits==NULL || raw==NULL || !mt_iigs_floppy_track(format,track,&t))return false;
    bit_reader_t r={bits,t.bit_count,0U,0U};
    const bool sony=format==MT_IIGS_RAW_PRODOS35;
    const unsigned sectors=sony ? t.raw_bytes/512U:16U;
    unsigned found=0U; uint8_t a=0U,b=0U,c=0U,body[703],decoded[524];
    while(r.consumed<r.count*2U && found!=((1U<<sectors)-1U)) {
        a=b; b=c; c=nibble(&r);
        if(a!=0xd5U || b!=0xaaU || c!=0x96U)continue;
        uint8_t address[8]; const unsigned address_bytes=sony ? 5U:8U;
        for(unsigned i=0U;i<address_bytes;++i)address[i]=nibble(&r);
        const uint8_t end1=nibble(&r),end2=nibble(&r);
        (void)nibble(&r); a=b=c=0U;
        if(end1!=0xdeU || end2!=0xaaU)continue;
        unsigned number;
        if(sony) {
            bool valid=true;
            for(unsigned i=0U;i<5U;++i) { address[i]=ungcr(address[i]); valid=valid && address[i]!=0xffU; }
            number=address[1];
            if(!valid || number>=sectors || (address[0]|((address[2]&1U)<<6U))!=track/2U ||
                ((address[2]>>5U)&1U)!=(track&1U) || (address[3]&0x20U)==0U ||
                (address[0]^address[1]^address[2]^address[3])!=address[4])continue;
        } else {
            bool valid=true; uint8_t values[4];
            for(unsigned i=0U;i<8U;++i)valid=valid && (address[i]&0xaaU)==0xaaU;
            for(unsigned i=0U;i<4U;++i)values[i]=(uint8_t)((((unsigned)address[i*2U]<<1U)|1U)&address[i*2U+1U]);
            number=values[2];
            if(!valid || values[1]!=track || number>=16U || (values[0]^values[1]^values[2])!=values[3])continue;
        }
        bool data=false;
        for(unsigned hunt=0U;hunt<64U && r.consumed<r.count*2U;++hunt) {
            a=b; b=c; c=nibble(&r);
            if(a==0xd5U && b==0xaaU) { if(c==0xadU)data=true; if(c==0xadU || c==0x96U)break; }
        }
        a=b=c=0U;
        if(!data)continue;
        if(sony && ungcr(nibble(&r))!=number)continue;
        for(unsigned i=0U;i<(sony ? 703U:343U);++i)body[i]=nibble(&r);
        const uint8_t data_end1=nibble(&r),data_end2=nibble(&r);
        if(data_end1!=0xdeU || data_end2!=0xaaU)continue;
        if(sony) {
            if(!decode524(body,decoded))continue;
            memcpy(raw+number*512U,decoded+12U,512U);
        } else {
            if(!decode256(body,decoded))continue;
            const unsigned file_sector=number==15U ? 15U:(number*7U)%15U;
            memcpy(raw+file_sector*256U,decoded,256U);
        }
        found|=1U<<number;
    }
    return found==((1U<<sectors)-1U);
}
