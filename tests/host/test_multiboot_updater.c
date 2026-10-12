/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production FatFs-to-multiboot bridge against live NOR bytes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "multiboot_platform.h"
#include "updater.h"
#include "crc32.h"

#define PAYLOAD 8192U
#define IMAGE_SIZE (PAYLOAD + MB_MANIFEST_SIZE)
static uint8_t nor[MB_FLASH_SIZE], source[IMAGE_SIZE];
static FSIZE_t source_size;
static uint32_t file_offset, seek_count, read_count, mutations;
static FRESULT open_result, mount_result, rename_result;
static int flash_available, short_read;

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i=0; i<4; ++i) p[i]=(uint8_t)(v>>(8*i));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint32_t crc(const void *p, unsigned n)
{
    return crc32_finish(crc32_update(crc32_init(),p,n));
}
static void checksum(uint8_t *p, unsigned words)
{
    uint32_t sum=0;
    for (unsigned i=0; i<words; ++i) sum+=get32(p+4*i);
    put32(p+4*words,~sum);
}
static void make_image(uint32_t product, int legacy)
{
    uint8_t *m=source+PAYLOAD;
    memset(source,0x39,sizeof(source));
    memset(source,0,512);
    put32(source+0x20,0xaa995566); put32(source+0x24,0x584c4e58);
    put32(source+0x30,512); put32(source+0x34,PAYLOAD-512);
    put32(source+0x38,0x100000); put32(source+0x3c,0x100000);
    put32(source+0x40,PAYLOAD-512); checksum(source+0x20,10);
    put32(source+0x98,0xa0); put32(source+0x9c,0x100);
    put32(source+0xa0,0x01020000); put32(source+0xa4,1);
    put32(source+0xa8,0x100/4); put32(source+0xac,0xe0/4);
    put32(source+0x104,(PAYLOAD-512)/4); put32(source+0x108,(PAYLOAD-512)/4);
    put32(source+0x10c,0x100000); put32(source+0x110,0x100000);
    put32(source+0x114,512/4); put32(source+0x118,0x10);
    checksum(source+0x100,15); put32(source+0x17c,0xffffffff);
    memcpy(source+4090,MB_RECOVERY_MARKER,sizeof(MB_RECOVERY_MARKER)-1);
    if (!legacy) {
        const char *marker=product==MB_PRODUCT_APPLETINI ? MB_APPLETINI_MARKER : MB_MULTITINI_MARKER;
        memcpy(source+4200,marker,strlen(marker));
    }
    memcpy(m,"ATNIMG1\0",8); put32(m+8,legacy ? 1 : 2); put32(m+12,2);
    put32(m+16,legacy ? MB_FLAG_RECOVERY : product|MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT);
    put32(m+20,PAYLOAD); put32(m+24,crc(source,PAYLOAD)); put32(m+28,crc(m,28));
    source_size=sizeof(source);
}
static int flash_read(void *context, uint32_t off, void *dst, uint32_t len)
{
    (void)context; assert(off<=MB_FLASH_SIZE && len<=MB_FLASH_SIZE-off);
    memcpy(dst,nor+off,len); return 0;
}
static int flash_erase(void *context, uint32_t off, uint32_t len)
{
    (void)context; assert(off<=MB_FLASH_SIZE && len<=MB_FLASH_SIZE-off);
    memset(nor+off,0xff,len); ++mutations; return 0;
}
static int flash_program(void *context, uint32_t off, const void *src, uint32_t len)
{
    const uint8_t *p=src; (void)context;
    assert(off<=MB_FLASH_SIZE && len<=MB_FLASH_SIZE-off);
    for (uint32_t i=0; i<len; ++i) { assert((nor[off+i]&p[i])==p[i]); nor[off+i]&=p[i]; }
    ++mutations; return 0;
}
static const mb_flash_t flash={NULL,flash_read,flash_erase,flash_program,MB_FLASH_SIZE};
const mb_flash_t *mb_platform_flash(void) { return flash_available ? &flash : NULL; }
void uart_puts(uint32_t base, const char *s) { (void)base; (void)s; }
void golden_led_on(void) {}
void golden_led_toggle(void) {}
FRESULT f_mount(FATFS *fs, const char *path, BYTE opt)
{
    (void)fs; assert(!strcmp(path,"0:/") && opt==1); return mount_result;
}
FRESULT f_open(FIL *f, const char *path, BYTE mode)
{
    (void)f; assert(!strcmp(path,"0:/FIRMWARE.BIN") && mode==FA_READ);
    file_offset=0; return open_result;
}
FSIZE_t f_size(const FIL *f) { (void)f; return source_size; }
FRESULT f_lseek(FIL *f, FSIZE_t pos)
{
    (void)f; assert(pos<=source_size); file_offset=(uint32_t)pos; ++seek_count; return FR_OK;
}
FRESULT f_read(FIL *f, void *dst, UINT len, UINT *got)
{
    (void)f; assert(file_offset<=IMAGE_SIZE && len<=IMAGE_SIZE-file_offset);
    ++read_count; *got=short_read ? len-1 : len;
    memcpy(dst,source+file_offset,*got); file_offset+=*got; return FR_OK;
}
FRESULT f_close(FIL *f) { (void)f; return FR_OK; }
FRESULT f_unlink(const char *path) { assert(!strcmp(path,"0:/FIRMWARE.OK")); return FR_OK; }
FRESULT f_rename(const char *from, const char *to)
{
    assert(!strcmp(from,"0:/FIRMWARE.BIN") && !strcmp(to,"0:/FIRMWARE.OK")); return rename_result;
}
static void reset_fixture(void)
{
    memset(nor,0xff,sizeof(nor));
    seek_count=read_count=mutations=0;
    open_result=mount_result=rename_result=FR_OK;
    flash_available=1; short_read=0;
}
int main(void)
{
    updater_result_t r;
    uint32_t boot=0xdeadbeef;
    reset_fixture(); make_image(MB_PRODUCT_MULTITINI,0);
    assert(!updater_has_verified_firmware());
    assert(updater_select_firmware(&boot)!=0 && boot==0xdeadbeef);
    assert(updater_run(&r)==0 && r.found_file && r.updated && r.verified);
    assert(r.file_size==IMAGE_SIZE && r.file_crc32==crc(source,IMAGE_SIZE) && r.file_crc32==r.flash_crc32);
    assert(seek_count==read_count && read_count>10);
    assert(!memcmp(nor+MB_MULTITINI_OFFSET,source,IMAGE_SIZE));
    assert(get32(nor+MB_APPLETINI_OFFSET+0x24)==0xffffffff);
    assert(updater_select_firmware(&boot)==0 && boot==MB_MULTITINI_OFFSET);
    /* Product A routes by its tag despite the same FIRMWARE.BIN filename. */
    make_image(MB_PRODUCT_APPLETINI,0);
    assert(updater_run(&r)==0 && r.verified && !memcmp(nor+MB_APPLETINI_OFFSET,source,IMAGE_SIZE));
    assert(mb_set_request(&flash,MB_PRODUCT_APPLETINI)==MB_E_UNAVAILABLE); /* no new golden */
    /* A linked multiboot golden enables the one-shot request. */
    memcpy(nor,source,IMAGE_SIZE);
    memcpy(nor+4300,MB_GOLDEN_MARKER,sizeof(MB_GOLDEN_MARKER)-1);
    put32(nor+PAYLOAD+8,1); put32(nor+PAYLOAD+12,1); put32(nor+PAYLOAD+16,1);
    put32(nor+PAYLOAD+24,crc(nor,PAYLOAD)); put32(nor+PAYLOAD+28,crc(nor+PAYLOAD,28));
    assert(mb_set_request(&flash,MB_PRODUCT_APPLETINI)==MB_OK);
    /* Missing/corrupt requested default cannot select empty A; valid B survives. */
    nor[MB_APPLETINI_OFFSET+800]^=1;
    assert(updater_select_firmware(&boot)==0 && boot==MB_MULTITINI_OFFSET);
    mb_metadata_t metadata;
    assert(mb_metadata_load(&flash,&metadata)==MB_OK && metadata.request_product==MB_PRODUCT_NONE);
    assert(metadata.default_product==MB_PRODUCT_MULTITINI);
    /* Ambiguous v1 and incomplete source reads must never touch the flash. */
    uint32_t before=mutations;
    make_image(MB_PRODUCT_APPLETINI,1);
    assert(updater_run(&r)!=0 && !r.verified && mutations==before);
    assert(updater_select_firmware(&boot)==0 && boot==MB_MULTITINI_OFFSET);
    make_image(MB_PRODUCT_APPLETINI,0); short_read=1;
    assert(updater_run(&r)!=0 && mutations==before); short_read=0;
    source_size=(FSIZE_t)UINT32_MAX+1;
    assert(updater_run(&r)!=0 && mutations==before);
    open_result=FR_NO_FILE;
    assert(updater_run(&r)==0 && !r.found_file && mutations==before);
    open_result=FR_NO_PATH;
    assert(updater_run(&r)==0 && !r.found_file && mutations==before);
    open_result=FR_OK; make_image(MB_PRODUCT_APPLETINI,0); rename_result=FR_DISK_ERR;
    assert(updater_run(&r)==0 && r.updated && r.verified);
    flash_available=0;
    assert(!updater_has_verified_firmware());
    puts("PASS: product-routed FatFs install, exact random reads, missing/corrupt fallback, legacy rejection, source-size guards");
    return 0;
}
