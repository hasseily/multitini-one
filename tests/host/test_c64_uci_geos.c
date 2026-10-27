/* SPDX-License-Identifier: GPL-3.0-only */
#include "c64_uci_geos.h"
#include "c64_uci_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t reu[16777216],file_data[1048576],staging[16777216];
uint8_t *c64_uci_reu_staging(void){return staging;}
static size_t file_size,file_at;static unsigned holds,fences,reads,writes,hold_wait,fence_wait;
static bool held,enabled=true,write_file,close_error,protected_path,file_live;
c64_uci_environment_t c64_uci_environment;
struct mt_sd_file {int dummy;};static struct mt_sd_file file;
bool c64_uci_reu_enabled(void){return enabled;}
uint32_t c64_uci_read(unsigned a){assert(a==MT_REG_C64_EXPANSION_CONFIG);return enabled?MT_C64_EXPANSION_REU_ENABLE:0;}
int c64_uci_service_hold_begin(void){holds++;held=true;if(hold_wait){hold_wait--;return 1;}return 0;}
void c64_uci_service_hold_end(void){held=false;}
void c64_uci_service_hold_fault(void){c64_uci_environment.fault=true;}
void c64_uci_reu_read(uint32_t a,void *p,size_t n){assert(held&&n<=4096&&a+n<=sizeof(reu));memcpy(p,reu+a,n);reads++;}
void c64_uci_reu_write(uint32_t a,const void *p,size_t n){assert(held&&n<=4096&&a+n<=sizeof(reu));memcpy(reu+a,p,n);writes++;}
int c64_uci_reu_fence(void){assert(held);fences++;if(fence_wait){fence_wait--;return 1;}return 0;}
bool c64_uci_path(const char *cwd,const uint8_t *name,size_t size,char out[512]){(void)cwd;if(size>=512)return false;memcpy(out,name,size);out[size]=0;return true;}
bool c64_uci_protected(const char *p,bool w){(void)p;(void)w;return protected_path;}
const char *c64_uci_sd_status(mt_sd_result_t s){(void)s;return "87,INTERNAL ERROR";}
mt_sd_result_t mt_sd_file_stat(const char *p,mt_sd_info_t *i){assert(!held);(void)p;memset(i,0,sizeof(*i));i->size=file_size;return (mt_sd_result_t){MT_SD_OK,0,0};}
mt_sd_result_t mt_sd_file_open(const char *p,const char *mode,mt_sd_file_t **f){assert(!held);(void)p;write_file=mode[0]=='w';file_at=0;if(write_file)file_size=0;*f=&file;file_live=true;return (mt_sd_result_t){MT_SD_OK,0,0};}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *f,void *p,size_t n){assert(!held&&f==&file&&!write_file&&n<=4096);if(n>file_size-file_at)n=file_size-file_at;memcpy(p,file_data+file_at,n);file_at+=n;return (mt_sd_result_t){MT_SD_OK,0,n};}
mt_sd_result_t mt_sd_file_write_buffered(mt_sd_file_t *f,const void *p,size_t n){assert(!held&&f==&file&&write_file&&file_at+n<=sizeof(file_data)&&n<=4096);memcpy(file_data+file_at,p,n);file_at+=n;file_size=file_at;return (mt_sd_result_t){MT_SD_OK,0,n};}
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *f){assert(!held&&f==&file&&file_live);if(!close_error)file_live=false;return (mt_sd_result_t){close_error?MT_SD_CLOSE_FAILED:MT_SD_OK,0,0};}
int64_t mt_sd_file_tell(const mt_sd_file_t *f){assert(f==&file);return file_live?(int64_t)file_at:-1;}
static mt_uci_result_t finish(mt_uci_result_t a,mt_uci_reply_t *r){for(unsigned i=0;a==MT_UCI_WAIT&&i<3000;i++)a=c64_uci_geos_poll(r);assert(a==MT_UCI_REPLY||a==MT_UCI_FAILED);return a;}
static bool status(mt_uci_reply_t *r,const char *s){return r->status_length==strlen(s)&&!memcmp(r->status,s,strlen(s));}
static void encode(const uint8_t *p,unsigned n,uint8_t *out){static const uint8_t code[16]={10,11,18,19,14,15,22,23,9,25,26,27,13,29,30,21};memset(out,0,n*10/8);unsigned bit=0;for(unsigned i=0;i<n;i++)for(unsigned half=0;half<2;half++){unsigned v=code[(p[i]>>(half?0:4))&15];for(unsigned j=0;j<5;j++,bit++)out[bit/8]|=(uint8_t)(((v>>(4-j))&1)<<(7-bit%8));}}
int main(void){uint8_t c[896]={4,0x40};mt_uci_reply_t r;mt_uci_result_t a;
    reu[0xbb0e]=0x81;reu[0xbb0f]=0x82;reu[0xbb10]=0x83;reu[0xbb11]=0x84;
    reu[0x7dc7]=1;reu[0x7dc8]=4;reu[0x7dc9]=10;reu[0x7dca]=20;reu[0xbb05]=0x90;reu[0x9084]=32;
    hold_wait=2;assert(finish(c64_uci_geos_begin(c,2,"",&r),&r)==MT_UCI_REPLY);assert(r.data_length==8&&!memcmp(r.data,"\x41\x00\x71\x00\x81\x00\xdd\x20",8)&&!held);
    c[0]=1;c[1]=0x41;c[2]=0;memcpy(c+3,"disk.d64",8);file_size=174848;for(size_t i=0;i<file_size;i++)file_data[i]=(uint8_t)(i^(i>>8));
    fence_wait=2;a=c64_uci_geos_begin(c,11,"",&r);assert(finish(a,&r)==MT_UCI_REPLY&&status(&r,"00,OK")&&!held&&fences>=3&&!memcmp(reu+65536,file_data,file_size));
    c[1]=0x42;assert(finish(c64_uci_geos_begin(c,11,"",&r),&r)==MT_UCI_REPLY&&status(&r,"00,OK")&&file_size==174848&&!memcmp(file_data,reu+65536,file_size));
    c[1]=0x41;c[2]=1;memcpy(c+3,"disk.d71",8);file_size=349696;memset(file_data,0x5a,174848);memset(file_data+174848,0xc3,174848);memset(reu+4*65536,0xff,354048);
    assert(finish(c64_uci_geos_begin(c,11,"",&r),&r)==MT_UCI_REPLY&&status(&r,"00,OK"));assert(reu[4*65536+174847]==0x5a&&reu[4*65536+174848]==255&&reu[4*65536+700*256]==0xc3);
    c[2]=0x81;unsigned before=writes;assert(finish(c64_uci_geos_begin(c,11,"",&r),&r)==MT_UCI_REPLY&&writes==before);c[2]=4;assert(c64_uci_geos_begin(c,11,"",&r)==MT_UCI_REPLY&&status(&r,"81,INVALID PARAMS"));
    /* Cancel after accepted REU writes: cache fence must finish before HOLD drops. */
    c[2]=1;a=c64_uci_geos_begin(c,11,"",&r);assert(a==MT_UCI_WAIT);before=writes;for(unsigned i=0;writes==before&&i<3000;i++)assert(c64_uci_geos_poll(&r)==MT_UCI_WAIT);assert(writes>before);fence_wait=1;assert(c64_uci_geos_abort()==MT_UCI_WAIT&&held);assert(c64_uci_geos_abort()==MT_UCI_REPLY&&!held);
    /* A circular, bit-shifted GCR sector, including source/destination overlap. */
    uint8_t raw[400],header[8]={8,0,0,1,0x12,0x34,15,15},data[260]={7};memset(raw,0x55,sizeof(raw));memset(raw,255,5);encode(header,8,raw+5);memset(raw+24,255,5);for(unsigned i=1;i<=256;i++)data[i]=(uint8_t)(i-1);data[257]=0;encode(data,260,raw+29);
    memset(c,0,sizeof(c));c[0]=4;c[1]=0x11;c[2]=1;c[3]=1;c[6]=1;c[10]=1;c[12]=144;c[13]=1;
    for(unsigned rotation=0;rotation<8;rotation++){
        for(unsigned i=0;i<400;i++)reu[65536+i]=(uint8_t)((raw[i]>>rotation)|(raw[(i+399)%400]<<(8-rotation)));
        assert(finish(c64_uci_geos_begin(c,14,"",&r),&r)==MT_UCI_REPLY&&status(&r,"00,OK")&&r.data_length==3&&r.data[0]==1&&r.data[1]==0&&r.data[2]==0);for(unsigned i=0;i<256;i++)assert(reu[65536+i]==i);
    }
    data[257]=1;encode(data,260,raw+29);memcpy(reu+65536,raw,400);assert(finish(c64_uci_geos_begin(c,14,"",&r),&r)==MT_UCI_REPLY&&status(&r,"82,ERRORS ON TRACK")&&r.data[2]==0xe3);
    c[8]=255;c[9]=255;c[10]=255;assert(c64_uci_geos_begin(c,14,"",&r)==MT_UCI_REPLY&&status(&r,"81,INVALID PARAMS"));
    enabled=false;c[1]=0x40;assert(c64_uci_geos_begin(c,2,"",&r)==MT_UCI_REPLY&&status(&r,"84,REU NOT ENABLED"));
    enabled=true;c[0]=1;c[1]=0x42;c[2]=0;memcpy(c+3,"disk.d64",8);a=c64_uci_geos_begin(c,11,"",&r);assert(a==MT_UCI_WAIT);close_error=true;assert(c64_uci_geos_abort()==MT_UCI_FAILED&&!held&&c64_uci_environment.fault);
    close_error=false;c64_uci_environment.fault=false;assert(c64_uci_geos_abort()==MT_UCI_REPLY&&!file_live);
    puts("PASS GEOS driveinfo/D64/D71gap/what-if/abort-fence; circular GCR8bit-alignments/overlap/checksum/ranges; failed-close ownership");return 0;
}
