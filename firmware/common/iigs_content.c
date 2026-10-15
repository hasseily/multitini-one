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
    (c->flags & 0x80U) == 0U && (c->pulses & 0xe0U) == 0U; }
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
    mt_iigs_media_info_t r={0U,size,MT_IIGS_MEDIA_BLOCK,true};
    if (drive>=MT_IIGS_DRIVE_COUNT || name==NULL || p==NULL || info==NULL)
        return MT_CONTENT_INVALID_ARGUMENT;
    if (size==0U) return MT_CONTENT_TOO_SMALL;
    if (size>UINT32_MAX) return MT_CONTENT_WRONG_SIZE;
    if (drive>=2U) {
        if (!extension(name,"woz")) return MT_CONTENT_UNSUPPORTED_VERSION;
        if (n<80U) return MT_CONTENT_TOO_SMALL;
        if (memcmp(p,"WOZ2\xff\x0a\x0d\x0a",8U)!=0)
            return MT_CONTENT_BAD_MAGIC;
        if (memcmp(p+12U,"INFO",4U)!=0 || le32(p+16U)<60U ||
            (uint64_t)le32(p+16U)+20U>size || p[20]==0U || p[20]>3U)
            return MT_CONTENT_BAD_STRUCTURE;
        if (p[21]!=(drive==2U ? 2U:1U)) return MT_CONTENT_BAD_STRUCTURE;
        r.kind=MT_IIGS_MEDIA_WOZ; r.writable=p[22]==0U;
    } else if (extension(name,"2mg")) {
        uint32_t offset,length,blocks;
        const unsigned header_size=n>=10U ? (unsigned)p[8]+((unsigned)p[9]<<8):0U;
        if (n<64U) return MT_CONTENT_TOO_SMALL;
        if (memcmp(p,"2IMG",4U)!=0) return MT_CONTENT_BAD_MAGIC;
        if (header_size<64U || p[10]>1U || p[11]!=0U)
            return MT_CONTENT_UNSUPPORTED_VERSION;
        if (le32(p+12U)!=1U) return MT_CONTENT_UNSUPPORTED_VERSION; /* ProDOS blocks */
        offset=le32(p+24U); length=le32(p+28U); blocks=le32(p+20U);
        if (offset<header_size || length==0U || (length&511U)!=0U ||
            (uint64_t)offset+length>size || blocks!=length/512U)
            return MT_CONTENT_BAD_STRUCTURE;
        r.offset=offset; r.size=length; r.writable=(le32(p+16U)&UINT32_C(0x80000000))==0U;
    } else if (!extension(name,"hdv") && !extension(name,"po")) {
        return MT_CONTENT_UNSUPPORTED_VERSION;
    }
    if (drive<2U && ((r.size&511U)!=0U || r.size/512U>65536U))
        return MT_CONTENT_WRONG_SIZE; /* upstream HDD sector address is 16 bits */
    *info=r; return MT_CONTENT_VALID;
}
