/* SPDX-License-Identifier: GPL-3.0-only */
/* Validate real packaged artifacts with the production parser/selector in NOR. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "multiboot.h"

static unsigned char nor[MB_FLASH_SIZE];
typedef struct { unsigned char *data; uint32_t size; } artifact_t;
static artifact_t app, multi, golden;
static unsigned selections;

static artifact_t load(const char *path, uint32_t limit)
{
    artifact_t a={0};
    FILE *f=fopen(path,"rb");
    assert(f && !fseek(f,0,SEEK_END));
    long size=ftell(f);
    assert(size>32 && (unsigned long)size<=limit && !fseek(f,0,SEEK_SET));
    a.size=(uint32_t)size; a.data=malloc(a.size); assert(a.data);
    assert(fread(a.data,1,a.size,f)==a.size && !fclose(f));
    return a;
}
static int artifact_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    artifact_t *a=ctx;
    if (off>a->size || len>a->size-off) return -1;
    memcpy(dst,a->data+off,len); return 0;
}
static int flash_read(void *ctx, uint32_t off, void *dst, uint32_t len)
{
    (void)ctx;
    if (off>MB_FLASH_SIZE || len>MB_FLASH_SIZE-off) return -1;
    memcpy(dst,nor+off,len); return 0;
}
static int flash_erase(void *ctx, uint32_t off, uint32_t len)
{
    (void)ctx;
    assert(off>=MB_APPLETINI_OFFSET && off<=MB_FLASH_SIZE && len<=MB_FLASH_SIZE-off);
    memset(nor+off,0xff,len); return 0;
}
static int flash_program(void *ctx, uint32_t off, const void *src, uint32_t len)
{
    const unsigned char *p=src; (void)ctx;
    assert(off>=MB_APPLETINI_OFFSET && off<=MB_FLASH_SIZE && len<=MB_FLASH_SIZE-off);
    for (uint32_t i=0;i<len;++i) {
        assert((nor[off+i]&p[i])==p[i]); nor[off+i]&=p[i];
    }
    return 0;
}
static mb_flash_t flash={NULL,flash_read,flash_erase,flash_program,MB_FLASH_SIZE};
static void select_image(uint32_t requested, uint32_t expected)
{
    mb_selection_t choice;
    assert(mb_select(&flash,requested,&choice)==MB_OK);
    assert(choice.product==expected && !choice.legacy);
    assert(choice.boot_offset==(expected ? mb_product_offset(expected) : MB_GOLDEN_OFFSET));
    ++selections;
}
static void populate(unsigned mask)
{
    memset(nor,0xff,sizeof(nor));
    memcpy(nor,golden.data,golden.size);
    if (mask&1U) memcpy(nor+MB_APPLETINI_OFFSET,app.data,app.size);
    if (mask&2U) memcpy(nor+MB_MULTITINI_OFFSET,multi.data,multi.size);
}
int main(int argc, char **argv)
{
    assert(argc==4);
    app=load(argv[1],MB_APPLETINI_CAPACITY);
    multi=load(argv[2],MB_MULTITINI_CAPACITY);
    golden=load(argv[3],MB_GOLDEN_SIZE);
    mb_reader_t a={&app,artifact_read,app.size}, m={&multi,artifact_read,multi.size};
    mb_image_t ai,mi,installed;
    assert(mb_image_validate(&a,MB_PRODUCT_APPLETINI,0,&ai)==MB_OK && ai.version==2);
    assert(mb_image_validate(&m,MB_PRODUCT_MULTITINI,0,&mi)==MB_OK && mi.version==2);
    assert(mb_image_validate(&a,MB_PRODUCT_MULTITINI,0,&installed)!=MB_OK);
    assert(mb_image_validate(&m,MB_PRODUCT_APPLETINI,0,&installed)!=MB_OK);
    for (unsigned mask=0;mask<4;++mask) {
        populate(mask);
        uint32_t golden_offset=UINT32_MAX;
        assert(mb_golden_available(&flash,&golden_offset)==MB_OK && golden_offset==0);
        assert((mb_slot_verify(&flash,MB_PRODUCT_APPLETINI,NULL)==MB_OK)==!!(mask&1U));
        assert((mb_slot_verify(&flash,MB_PRODUCT_MULTITINI,NULL)==MB_OK)==!!(mask&2U));
        select_image(0,(mask&1U) ? MB_PRODUCT_APPLETINI : (mask&2U) ? MB_PRODUCT_MULTITINI : 0);
        select_image(MB_PRODUCT_APPLETINI,(mask&1U) ? MB_PRODUCT_APPLETINI : (mask&2U) ? MB_PRODUCT_MULTITINI : 0);
        select_image(MB_PRODUCT_MULTITINI,(mask&2U) ? MB_PRODUCT_MULTITINI : (mask&1U) ? MB_PRODUCT_APPLETINI : 0);
    }
    for (unsigned product=0;product<2;++product) {
        uint32_t p=product ? MB_PRODUCT_MULTITINI : MB_PRODUCT_APPLETINI;
        uint32_t other=product ? MB_PRODUCT_APPLETINI : MB_PRODUCT_MULTITINI;
        populate(3); nor[mb_product_offset(p)+4096]^=1U;
        assert(mb_slot_verify(&flash,p,NULL)!=MB_OK);
        select_image(p,other);
        populate(3); memset(nor+mb_product_offset(p),0xff,MB_SECTOR_SIZE);
        select_image(p,other);
    }
    /* Run real transactional installs and choices, then damage the requested
     * image after metadata claimed it valid. Selection must check live bytes. */
    populate(0);
    assert(mb_install(&flash,&a,&installed)==MB_OK && installed.image_crc32==ai.image_crc32);
    select_image(MB_PRODUCT_MULTITINI,MB_PRODUCT_APPLETINI);
    assert(mb_install(&flash,&m,&installed)==MB_OK && installed.image_crc32==mi.image_crc32);
    assert(!memcmp(nor+MB_APPLETINI_OFFSET,app.data,app.size));
    assert(!memcmp(nor+MB_MULTITINI_OFFSET,multi.data,multi.size));
    assert(!memcmp(nor,golden.data,golden.size));
    assert(mb_set_default(&flash,MB_PRODUCT_MULTITINI)==MB_OK);
    assert(mb_set_request(&flash,MB_PRODUCT_APPLETINI)==MB_OK);
    select_image(0,MB_PRODUCT_APPLETINI);
    nor[MB_APPLETINI_OFFSET+4096]^=1U;
    select_image(0,MB_PRODUCT_MULTITINI);
    assert(mb_clear_request(&flash)==MB_OK);
    mb_metadata_t metadata;
    assert(mb_metadata_load(&flash,&metadata)==MB_OK);
    assert(metadata.request_product==0 && metadata.default_product==MB_PRODUCT_MULTITINI);
    memset(nor+MB_MULTITINI_OFFSET,0xff,MB_SECTOR_SIZE);
    select_image(0,0);
    printf("{\"status\":\"passed\",\"selection_cases\":%u,\"appletini\":{\"image_size\":%u,\"payload_size\":%u,\"image_crc32\":%u},\"multitini\":{\"image_size\":%u,\"payload_size\":%u,\"image_crc32\":%u},\"golden_size\":%u}\n",
           selections,ai.image_size,ai.payload_size,ai.image_crc32,mi.image_size,mi.payload_size,mi.image_crc32,golden.size);
    free(app.data); free(multi.data); free(golden.data);
    return 0;
}
