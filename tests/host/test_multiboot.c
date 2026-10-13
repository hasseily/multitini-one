/* SPDX-License-Identifier: GPL-3.0-only */
/* The production selector/updater runs on a 16 MiB NOR with 1->0 writes,
 * physical page/sector operations, and cuts after every mutation. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "multiboot.h"
#include "crc32.h"

#define PAYLOAD 8192U
#define IMAGE_SIZE (PAYLOAD+MB_MANIFEST_SIZE)
static uint8_t flash[MB_FLASH_SIZE],snapshot[MB_FLASH_SIZE];
static uint8_t app[IMAGE_SIZE],multi[IMAGE_SIZE],legacy[IMAGE_SIZE],golden[IMAGE_SIZE];
static uint8_t large[MB_APPLETINI_CAPACITY+256U+MB_MANIFEST_SIZE];
static uint32_t mutations,cut_at,fail_at,reads,fail_read,read_bytes;
static uint32_t fail_read_address=UINT32_MAX;
static jmp_buf power_cut;
static mb_flash_t device;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void put32(uint8_t *p,uint32_t v)
{
    unsigned i; for(i=0;i<4;++i) p[i]=(uint8_t)(v>>(i*8U));
}
static uint32_t crc(const uint8_t *p,uint32_t n)
{
    return crc32_finish(crc32_update(crc32_init(),p,n));
}
static void checksum(uint8_t *p,unsigned words)
{
    uint32_t sum=0; unsigned i;
    for(i=0;i<words;++i) sum+=get32(p+4U*i);
    put32(p+4U*words,~sum);
}
static void footer(uint8_t *p,uint32_t size,uint32_t version,uint32_t role,uint32_t flags)
{
    uint8_t *m=p+size;
    memcpy(m,"ATNIMG1\0",8); put32(m+8,version); put32(m+12,role); put32(m+16,flags);
    put32(m+20,size); put32(m+24,crc(p,size)); put32(m+28,crc(m,28));
}
static void make_image(uint8_t *p,uint32_t size,uint32_t product,uint32_t role,int capable)
{
    memset(p,0xa5,size+MB_MANIFEST_SIZE); memset(p,0,512);
    put32(p+0x20,0xaa995566); put32(p+0x24,0x584c4e58);
    put32(p+0x30,512); put32(p+0x34,size-512); put32(p+0x38,0x100000);
    put32(p+0x3c,0x100000); put32(p+0x40,size-512); checksum(p+0x20,10);
    put32(p+0x98,0xa0); put32(p+0x9c,0x100); put32(p+0xa0,0x01020000);
    put32(p+0xa4,1); put32(p+0xa8,0x100/4); put32(p+0xac,0xe0/4);
    put32(p+0x104,(size-512)/4); put32(p+0x108,(size-512)/4);
    put32(p+0x10c,0x100000); put32(p+0x110,0x100000);
    put32(p+0x114,512/4); put32(p+0x118,0x10); checksum(p+0x100,15);
    put32(p+0x17c,0xffffffff);
    memcpy(p+4090,MB_RECOVERY_MARKER,sizeof(MB_RECOVERY_MARKER)-1);
    if(product) {
        const char *marker=product==MB_PRODUCT_APPLETINI ? MB_APPLETINI_MARKER : MB_MULTITINI_MARKER;
        memcpy(p+8180-strlen(marker),marker,strlen(marker));
    }
    if(role==1 && capable) memcpy(p+1024,MB_GOLDEN_MARKER,sizeof(MB_GOLDEN_MARKER)-1);
    footer(p,size,product ? 2U : 1U,role,
           MB_FLAG_RECOVERY|(product ? product|MB_FLAG_MULTIBOOT : 0U));
}
static void mutation(void)
{
    ++mutations; if(cut_at==mutations) longjmp(power_cut,1);
}
static int nor_read(void *ctx,uint32_t a,void *p,uint32_t n)
{
    (void)ctx; assert(a<=MB_FLASH_SIZE && n<=MB_FLASH_SIZE-a);
    read_bytes+=n;
    if(++reads==fail_read || (fail_read_address>=a && fail_read_address-a<n)) return 1;
    memcpy(p,flash+a,n); return 0;
}
static int nor_erase(void *ctx,uint32_t a,uint32_t n)
{
    uint32_t off; (void)ctx;
    assert(!(a&(MB_SECTOR_SIZE-1U)) && !(n&(MB_SECTOR_SIZE-1U)));
    assert(a>=MB_APPLETINI_OFFSET && a<=MB_FLASH_SIZE && n<=MB_FLASH_SIZE-a);
    for(off=0;off<n;off+=MB_SECTOR_SIZE) {
        if(fail_at==mutations+1) return 1;
        memset(flash+a+off,0xff,MB_SECTOR_SIZE); mutation();
    }
    return 0;
}
static int nor_program(void *ctx,uint32_t a,const void *p,uint32_t n)
{
    const uint8_t *s=p; (void)ctx;
    assert(a>=MB_APPLETINI_OFFSET && a<=MB_FLASH_SIZE && n<=MB_FLASH_SIZE-a);
    while(n) {
        uint32_t i,chunk=256U-(a&255U); if(chunk>n) chunk=n;
        if(fail_at==mutations+1) return 1;
        for(i=0;i<chunk;++i) { assert((flash[a+i]&s[i])==s[i]); flash[a+i]&=s[i]; }
        mutation(); a+=chunk; s+=chunk; n-=chunk;
    }
    return 0;
}
static int memory_read(void *ctx,uint32_t a,void *p,uint32_t n)
{
    memcpy(p,(const uint8_t *)ctx+a,n); return 0;
}
static mb_reader_t reader(uint8_t *p,uint32_t n)
{
    mb_reader_t r={p,memory_read,n}; return r;
}
static void reset(void)
{
    memset(flash,0xff,sizeof flash);
    memcpy(flash,golden,IMAGE_SIZE); memcpy(flash+MB_GOLDEN_TRIAL_OFFSET,golden,IMAGE_SIZE);
    mutations=cut_at=fail_at=reads=fail_read=read_bytes=0;
    fail_read_address=UINT32_MAX;
}
static void old_metadata(void)
{
    uint8_t *m=flash+MB_METADATA_B_OFFSET;
    memset(m,0,168); put32(m,0x46574d54); put32(m+4,4); put32(m+8,168); put32(m+12,1);
    put32(m+16,0); put32(m+20,0x200000); put32(m+24,MB_APPLETINI_OFFSET);
    put32(m+28,0xdf0000); put32(m+32,1);
    put32(m+164,crc(m,164));
}
static void expect(uint32_t requested,uint32_t expected,int old)
{
    mb_selection_t s;
    assert(mb_select(&device,requested,&s)==MB_OK);
    assert(s.product==expected && s.legacy==(uint32_t)old);
    assert(s.boot_offset==(expected ? mb_product_offset(expected) : old ? MB_APPLETINI_OFFSET : 0));
}
static void install(uint8_t *p)
{
    mb_reader_t r=reader(p,IMAGE_SIZE); mb_image_t image;
    assert(mb_install(&device,&r,&image)==MB_OK);
}
static void selectors(void)
{
    mb_image_t image; mb_metadata_t m; mb_selection_t s; mb_reader_t r;
    uint32_t before,offset;
    reset(); expect(MB_PRODUCT_APPLETINI,0,0); expect(MB_PRODUCT_MULTITINI,0,0);
    memcpy(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE);
    expect(MB_PRODUCT_MULTITINI,MB_PRODUCT_APPLETINI,0);
    reset(); memcpy(flash+MB_MULTITINI_OFFSET,multi,IMAGE_SIZE);
    expect(MB_PRODUCT_APPLETINI,MB_PRODUCT_MULTITINI,0);
    assert(mb_set_default(&device,MB_PRODUCT_MULTITINI)==MB_OK);
    assert(mb_metadata_load(&device,&m)==MB_OK && m.slots[1].flags==MB_FLAG_SLOT_VALID);
    reset(); memcpy(flash+MB_MULTITINI_OFFSET,app,IMAGE_SIZE); expect(0,0,0);
    reset(); memcpy(flash+MB_MULTITINI_OFFSET,multi,IMAGE_SIZE);
    flash[MB_MULTITINI_OFFSET+2000]^=1; expect(0,0,0);
    reset(); install(app); install(multi);
    assert(mb_slot_verify(&device,MB_PRODUCT_APPLETINI,&image)==MB_OK);
    assert(mb_slot_verify(&device,MB_PRODUCT_MULTITINI,&image)==MB_OK);
    assert(mb_set_default(&device,MB_PRODUCT_MULTITINI)==MB_OK); expect(0,MB_PRODUCT_MULTITINI,0);
    assert(mb_set_request(&device,MB_PRODUCT_APPLETINI)==MB_OK); expect(0,MB_PRODUCT_APPLETINI,0);
    assert(mb_metadata_load(&device,&m)==MB_OK);
    assert(m.default_product==MB_PRODUCT_MULTITINI && m.request_product==MB_PRODUCT_APPLETINI);
    assert(mb_clear_request(&device)==MB_OK); expect(0,MB_PRODUCT_MULTITINI,0);
    memset(flash+MB_MULTITINI_OFFSET,0xff,IMAGE_SIZE);
    expect(0,MB_PRODUCT_APPLETINI,0); before=mutations;
    assert(mb_set_default(&device,MB_PRODUCT_MULTITINI)!=MB_OK && mutations==before);
    assert(mb_set_request(&device,MB_PRODUCT_MULTITINI)!=MB_OK && mutations==before);
    /* Full footer CRC and header validate, but stale metadata must reject. */
    memcpy(flash+MB_MULTITINI_OFFSET,multi,IMAGE_SIZE);
    flash[MB_MULTITINI_OFFSET+2000]^=1;
    footer(flash+MB_MULTITINI_OFFSET,PAYLOAD,2,2,MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT|MB_PRODUCT_MULTITINI);
    expect(MB_PRODUCT_MULTITINI,MB_PRODUCT_APPLETINI,0);
    assert(mb_update_begin(&device,MB_PRODUCT_APPLETINI)==MB_OK);
    expect(MB_PRODUCT_APPLETINI,0,0); /* intact A still ineligible during install */
    reset(); install(app); fail_read=reads+1;
    assert(mb_select(&device,0,&s)==MB_E_READ && s.boot_offset==0);
    fail_read=0; assert(mb_golden_available(&device,&offset)==MB_OK && offset==0);
    flash[2000]^=1;
    assert(mb_golden_available(&device,&offset)==MB_OK && offset==MB_GOLDEN_TRIAL_OFFSET);
    flash[MB_GOLDEN_TRIAL_OFFSET+2000]^=1; before=mutations;
    assert(mb_set_default(&device,MB_PRODUCT_APPLETINI)==MB_E_UNAVAILABLE && mutations==before);
    make_image(flash,PAYLOAD,0,1,0); make_image(flash+MB_GOLDEN_TRIAL_OFFSET,PAYLOAD,0,1,0);
    assert(mb_golden_available(&device,NULL)==MB_E_UNAVAILABLE);
    assert(mb_set_request(&device,MB_PRODUCT_APPLETINI)==MB_E_UNAVAILABLE && mutations==before);
    /* Retagging old firmware without compiled family capability is refused. */
    r=reader(legacy,IMAGE_SIZE); assert(mb_image_validate(&r,0,1,&image)==MB_OK);
    footer(legacy,PAYLOAD,2,2,MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT|MB_PRODUCT_APPLETINI);
    assert(mb_image_validate(&r,0,0,&image)==MB_E_MARKER);
    make_image(legacy,PAYLOAD,0,2,0);
    /* Family is intrinsic to the linked payload, independently of retagging
     * its correctly CRC'd footer. Neither opposite nor ambiguous identity is
     * accepted even when every other field is valid. */
    r=reader(multi,IMAGE_SIZE);
    footer(multi,PAYLOAD,2,2,MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT|MB_PRODUCT_APPLETINI);
    assert(mb_image_validate(&r,0,0,&image)==MB_E_MARKER);
    make_image(multi,PAYLOAD,MB_PRODUCT_MULTITINI,2,0);
    r=reader(app,IMAGE_SIZE);
    footer(app,PAYLOAD,2,2,MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT|MB_PRODUCT_MULTITINI);
    assert(mb_image_validate(&r,0,0,&image)==MB_E_MARKER);
    make_image(app,PAYLOAD,MB_PRODUCT_APPLETINI,2,0);
    memcpy(app+2048,MB_MULTITINI_MARKER,sizeof(MB_MULTITINI_MARKER)-1);
    footer(app,PAYLOAD,2,2,MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT|MB_PRODUCT_APPLETINI);
    assert(mb_image_validate(&r,0,0,&image)==MB_E_MARKER);
    make_image(app,PAYLOAD,MB_PRODUCT_APPLETINI,2,0);
    printf("multiboot selection: missing/erased/wrong-product/retagged identity/CRC/stale metadata/read errors covered\n");
}
static void migrations(void)
{
    mb_metadata_t m; mb_image_t image; mb_reader_t r;
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,legacy,IMAGE_SIZE); old_metadata();
    expect(0,0,1); expect(MB_PRODUCT_MULTITINI,0,1);
    assert(mb_slot_verify(&device,MB_PRODUCT_APPLETINI,&image)!=MB_OK);
    install(multi); assert(mb_metadata_load(&device,&m)==MB_OK);
    assert(m.slots[0].product==0 && m.slots[0].flags==(MB_FLAG_SLOT_VALID|MB_FLAG_SLOT_LEGACY));
    expect(0,MB_PRODUCT_MULTITINI,0);
    memset(flash+MB_MULTITINI_OFFSET,0xff,IMAGE_SIZE); expect(0,0,1);
    install(app); expect(MB_PRODUCT_MULTITINI,MB_PRODUCT_APPLETINI,0);
    reset(); make_image(large,MB_APPLETINI_CAPACITY+256U,0,2,0);
    memcpy(flash+MB_APPLETINI_OFFSET,large,sizeof large); old_metadata(); expect(0,0,1);
    memcpy(snapshot,flash,sizeof flash); r=reader(multi,IMAGE_SIZE);
    assert(mb_install(&device,&r,&image)==MB_E_OVERLAP);
    assert(!mutations && !memcmp(snapshot,flash,sizeof flash));
    fail_read_address=MB_APPLETINI_OFFSET+0x20U;
    assert(mb_install(&device,&r,&image)==MB_E_READ);
    assert(!mutations && !memcmp(snapshot,flash,sizeof flash));
    fail_read_address=MB_APPLETINI_OFFSET+sizeof large-MB_MANIFEST_SIZE;
    assert(mb_install(&device,&r,&image)==MB_E_READ);
    assert(!mutations && !memcmp(snapshot,flash,sizeof flash));
    fail_read_address=UINT32_MAX;
    install(app); expect(0,MB_PRODUCT_APPLETINI,0);
    printf("multiboot legacy: live v1 verification, unknown product retention and >6 MiB overlap refusal covered\n");
}
static void adopted_defaults(void)
{
    mb_metadata_t m;
    /* Old Golden installed tagged Appletini, retaining its v4 metadata.
     * Installing the peer under new Golden must preserve Appletini startup. */
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE); old_metadata();
    expect(0,MB_PRODUCT_APPLETINI,0); install(multi);
    assert(mb_metadata_load(&device,&m)==MB_OK);
    assert(m.default_product==MB_PRODUCT_APPLETINI && !m.request_product);
    expect(0,MB_PRODUCT_APPLETINI,0);
    /* Preserve the verified fallback in the opposite migration direction. */
    reset(); memcpy(flash+MB_MULTITINI_OFFSET,multi,IMAGE_SIZE); install(app);
    assert(mb_metadata_load(&device,&m)==MB_OK && m.default_product==MB_PRODUCT_MULTITINI);
    expect(0,MB_PRODUCT_MULTITINI,0);
    /* With both images but no owner choice, retain normal Appletini-first
     * selection even if the first batch update replaces Multitini. */
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE);
    memcpy(flash+MB_MULTITINI_OFFSET,multi,IMAGE_SIZE); install(multi);
    assert(mb_metadata_load(&device,&m)==MB_OK && m.default_product==MB_PRODUCT_APPLETINI);
    expect(0,MB_PRODUCT_APPLETINI,0);
    /* A corrupt pre-existing Appletini cannot become the adopted default. */
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE);
    flash[MB_APPLETINI_OFFSET+2000]^=1; install(multi);
    assert(mb_metadata_load(&device,&m)==MB_OK && m.default_product==MB_PRODUCT_MULTITINI);
    expect(0,MB_PRODUCT_MULTITINI,0);
    /* Empty flash still chooses the first successful installation. A saved
     * default and a separate one-shot choice survive all later installs. */
    reset(); install(multi); install(app);
    assert(mb_set_request(&device,MB_PRODUCT_APPLETINI)==MB_OK); install(multi);
    assert(mb_metadata_load(&device,&m)==MB_OK);
    assert(m.default_product==MB_PRODUCT_MULTITINI && m.request_product==MB_PRODUCT_APPLETINI);
    expect(0,MB_PRODUCT_APPLETINI,0);
    assert(mb_clear_request(&device)==MB_OK); expect(0,MB_PRODUCT_MULTITINI,0);
    printf("multiboot adoption: existing verified startup/default/one-shot choices survive peer installation\n");
}
static void advisory_probes(void)
{
    mb_image_t image; mb_metadata_t metadata;
    uint32_t bytes,before;
    reset(); install(app); install(multi); read_bytes=reads=0;
    before=mutations;
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,&image)==MB_OK);
    assert(image.product==MB_PRODUCT_APPLETINI && image.version==2 &&
           image.image_size==IMAGE_SIZE && image.payload_size==PAYLOAD && !image.image_crc32);
    bytes=read_bytes; assert(bytes<2048U && mutations==before);
    read_bytes=0;
    /* Larger images require the same bounded header/footer reads. */
    make_image(large,MB_APPLETINI_CAPACITY+256U,MB_PRODUCT_MULTITINI,2,0);
    reset(); memcpy(flash+MB_MULTITINI_OFFSET,large,sizeof large);
    read_bytes=0;
    assert(mb_slot_probe(&device,MB_PRODUCT_MULTITINI,NULL)==MB_OK);
    assert(read_bytes==bytes);
    /* Presence is advisory: unchanged headers/footer cannot prove code
     * integrity, so a damaged body may appear present but must never boot. */
    reset(); install(app); install(multi);
    flash[MB_APPLETINI_OFFSET+2000]^=1;
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)==MB_OK);
    assert(mb_slot_verify(&device,MB_PRODUCT_APPLETINI,NULL)!=MB_OK);
    expect(MB_PRODUCT_APPLETINI,MB_PRODUCT_MULTITINI,0);
    assert(mb_set_default(&device,MB_PRODUCT_APPLETINI)!=MB_OK);
    assert(mb_set_request(&device,MB_PRODUCT_APPLETINI)!=MB_OK);
    reset(); install(app); install(multi);
    assert(mb_update_begin(&device,MB_PRODUCT_APPLETINI)==MB_OK);
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,&image)==MB_E_UNAVAILABLE);
    assert(!image.product && !image.image_size);
    /* No valid multiboot metadata: probe only explicit v2 tagged identities. */
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,legacy,IMAGE_SIZE); old_metadata();
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)!=MB_OK);
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,multi,IMAGE_SIZE);
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)!=MB_OK);
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE); old_metadata();
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)==MB_OK);
    flash[MB_APPLETINI_OFFSET+PAYLOAD+28]^=1;
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)==MB_E_MANIFEST);
    reset(); install(app); install(multi);
    /* A changed, correctly checksummed footer must agree with committed
     * payload identity, even though this probe does not read that payload. */
    flash[MB_APPLETINI_OFFSET+2000]^=1;
    footer(flash+MB_APPLETINI_OFFSET,PAYLOAD,2,2,MB_FLAG_RECOVERY|MB_FLAG_MULTIBOOT|MB_PRODUCT_APPLETINI);
    assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)==MB_E_VERIFY);
    assert(mb_metadata_load(&device,&metadata)==MB_OK);
    fail_read_address=MB_METADATA_A_OFFSET;
    assert(mb_slot_probe(&device,MB_PRODUCT_MULTITINI,NULL)==MB_E_READ);
    fail_read_address=MB_MULTITINI_OFFSET+PAYLOAD;
    assert(mb_slot_probe(&device,MB_PRODUCT_MULTITINI,NULL)==MB_E_READ);
    fail_read_address=UINT32_MAX;
    assert(mb_slot_probe(&device,0,NULL)==MB_E_ARGUMENT);
    reset(); assert(mb_slot_probe(&device,MB_PRODUCT_APPLETINI,NULL)!=MB_OK);
    printf("multiboot advisory probes: %u bounded read bytes, metadata invalidation and live full-verification refusal covered\n",bytes);
}
static void invariants(uint32_t product,int old)
{
    uint32_t base=mb_product_offset(product),cap=mb_product_capacity(product);
    mb_selection_t s;
    assert(!memcmp(flash,snapshot,MB_APPLETINI_OFFSET)); /* both golden copies */
    if(product==MB_PRODUCT_APPLETINI)
        assert(!memcmp(flash+MB_MULTITINI_OFFSET,snapshot+MB_MULTITINI_OFFSET,MB_MULTITINI_CAPACITY));
    else assert(!memcmp(flash+MB_APPLETINI_OFFSET,snapshot+MB_APPLETINI_OFFSET,MB_APPLETINI_CAPACITY));
    assert(mb_select(&device,product,&s)==MB_OK);
    if(s.product==product) assert(!memcmp(flash+base,product==MB_PRODUCT_APPLETINI ? app : multi,IMAGE_SIZE));
    else if(old) assert(s.product==0 && s.legacy==1 && s.boot_offset==MB_APPLETINI_OFFSET);
    else assert(s.product==(product==MB_PRODUCT_APPLETINI ? MB_PRODUCT_MULTITINI : MB_PRODUCT_APPLETINI));
    (void)cap;
}
static void adoption_faults(void)
{
    mb_reader_t r=reader(multi,IMAGE_SIZE);
    mb_image_t image;
    uint32_t count,read_count,stop;
    reset(); memcpy(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE); old_metadata();
    memcpy(snapshot,flash,sizeof flash); mutations=reads=0;
    assert(mb_install(&device,&r,&image)==MB_OK); count=mutations; read_count=reads;
    for(stop=1;stop<=count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; cut_at=stop;
        if(!setjmp(power_cut)) { (void)mb_install(&device,&r,&image); assert(!"missing adoption cut"); }
        cut_at=0;
        assert(!memcmp(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE));
        expect(0,MB_PRODUCT_APPLETINI,0);
        assert(mb_install(&device,&r,&image)==MB_OK); expect(0,MB_PRODUCT_APPLETINI,0);
    }
    for(stop=1;stop<=count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; fail_at=stop;
        assert(mb_install(&device,&r,&image)!=MB_OK); fail_at=0;
        assert(!memcmp(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE));
        expect(0,MB_PRODUCT_APPLETINI,0);
    }
    for(stop=1;stop<=read_count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; fail_read=stop;
        (void)mb_install(&device,&r,&image); fail_read=0;
        assert(!memcmp(flash+MB_APPLETINI_OFFSET,app,IMAGE_SIZE));
        expect(0,MB_PRODUCT_APPLETINI,0);
    }
    printf("multiboot tagged adoption: Appletini startup retained through %u power cuts, %u write failures, %u read failures\n",count,count,read_count);
}
static void faults(uint32_t product,int old)
{
    mb_image_t image; uint32_t count,read_count,stop;
    mb_reader_t r=reader(product==MB_PRODUCT_APPLETINI ? app : multi,IMAGE_SIZE);
    reset();
    if(old) { memcpy(flash+MB_APPLETINI_OFFSET,legacy,IMAGE_SIZE); old_metadata(); }
    else install(product==MB_PRODUCT_APPLETINI ? multi : app);
    memcpy(snapshot,flash,sizeof flash); mutations=reads=0;
    assert(mb_install(&device,&r,&image)==MB_OK); count=mutations; read_count=reads;
    for(stop=1;stop<=count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; cut_at=stop;
        if(!setjmp(power_cut)) { (void)mb_install(&device,&r,&image); assert(!"missing power cut"); }
        cut_at=0; invariants(product,old);
        assert(mb_install(&device,&r,&image)==MB_OK); invariants(product,old);
    }
    for(stop=1;stop<=count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; fail_at=stop;
        assert(mb_install(&device,&r,&image)!=MB_OK); fail_at=0; invariants(product,old);
    }
    for(stop=1;stop<=read_count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; fail_read=stop;
        (void)mb_install(&device,&r,&image); fail_read=0; invariants(product,old);
    }
    printf("multiboot %s install%s: %u power cuts, %u write failures, %u read failures\n",
           mb_product_name(product),old ? " preserving legacy" : "",count,count,read_count);
}
static void choice_faults(void)
{
    uint32_t count,stop; mb_metadata_t m;
    reset(); install(app); install(multi); memcpy(snapshot,flash,sizeof flash); mutations=0;
    assert(mb_set_default(&device,MB_PRODUCT_MULTITINI)==MB_OK); count=mutations;
    for(stop=1;stop<=count;++stop) {
        memcpy(flash,snapshot,sizeof flash); mutations=reads=0; cut_at=stop;
        if(!setjmp(power_cut)) { (void)mb_set_default(&device,MB_PRODUCT_MULTITINI); assert(!"missing cut"); }
        cut_at=0; assert(mb_metadata_load(&device,&m)==MB_OK);
        assert(m.default_product==MB_PRODUCT_APPLETINI || m.default_product==MB_PRODUCT_MULTITINI);
        assert(!memcmp(flash,snapshot,MB_METADATA_A_OFFSET));
        expect(0,m.default_product,0);
    }
    printf("multiboot persistent default: %u transactional metadata power cuts\n",count);
}
static void make_versioned_golden(uint8_t *p,const char *version,int raw)
{
    char banner[120];
    make_image(p,PAYLOAD,0,1,1);
    /* FSBL followed by the loaded Golden executable. */
    put32(p+0x34,512); put32(p+0x38,0); put32(p+0x3c,0); put32(p+0x40,512);
    checksum(p+0x20,10); put32(p+0xa4,2);
    memset(p+0x100,0,192);
    put32(p+0x104,128); put32(p+0x108,128); put32(p+0x114,128);
    put32(p+0x118,0x10); checksum(p+0x100,15);
    put32(p+0x144,(PAYLOAD-1024)/4); put32(p+0x148,(PAYLOAD-1024)/4);
    put32(p+0x14c,0x100000); put32(p+0x150,0x100000); put32(p+0x154,256);
    put32(p+0x158,0x10); checksum(p+0x140,15); put32(p+0x1bc,0xffffffff);
    memcpy(p+1536,"[GOLDEN] boot_updater start @921600\r\n",sizeof("[GOLDEN] boot_updater start @921600\r\n"));
    snprintf(banner,sizeof banner,"[GOLDEN] Boot image version: Boot %s\r\n",version);
    /* Deliberately cross a streaming read boundary. */
    memcpy(p+5110,banner,strlen(banner)+1U);
    if(raw) memset(p+PAYLOAD,0xff,MB_MANIFEST_SIZE);
    else footer(p,PAYLOAD,1,1,MB_FLAG_RECOVERY);
}
static void golden_versions(void)
{
    char version[64]; uint32_t offset,before;
    reset(); make_versioned_golden(flash,"B1.2.0",0);
    make_versioned_golden(flash+MB_GOLDEN_TRIAL_OFFSET,"B2.0.1",0);
    before=mutations;
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
    assert(!strcmp(version,"B1.2.0") && offset==0 && mutations==before);
    /* FSBL and unused flash literals must never affect loaded identity. */
    memcpy(flash+600,"[GOLDEN] Boot image version: Boot B9.9.9\r\n",sizeof("[GOLDEN] Boot image version: Boot B9.9.9\r\n"));
    memcpy(flash+PAYLOAD+96,"[GOLDEN] Boot image version: Boot B8.8.8\r\n",sizeof("[GOLDEN] Boot image version: Boot B8.8.8\r\n"));
    footer(flash,PAYLOAD,1,1,MB_FLAG_RECOVERY);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
    assert(!strcmp(version,"B1.2.0") && offset==0);
    /* Corrupt primary CRC permits verified trial fallback. */
    flash[2000]^=1;
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
    assert(!strcmp(version,"B2.0.1") && offset==MB_GOLDEN_TRIAL_OFFSET);
    make_versioned_golden(flash,"B1.2.0",0);
    fail_read_address=PAYLOAD;
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
    assert(!strcmp(version,"B2.0.1") && offset==MB_GOLDEN_TRIAL_OFFSET);
    fail_read_address=UINT32_MAX;
    /* An intact primary with unknown/conflicting identity is Unknown, not
     * the different trial version. */
    flash[5110]='?'; footer(flash,PAYLOAD,1,1,MB_FLAG_RECOVERY);
    strcpy(version,"old"); offset=0;
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)!=MB_OK);
    assert(!version[0] && offset==UINT32_MAX);
    make_versioned_golden(flash,"B1.2.0",0);
    memcpy(flash+2500,"[GOLDEN] Boot image version: Boot B7.7.7\r\n",sizeof("[GOLDEN] Boot image version: Boot B7.7.7\r\n"));
    footer(flash,PAYLOAD,1,1,MB_FLAG_RECOVERY);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)!=MB_OK);
    assert(!version[0] && offset==UINT32_MAX);
    /* Unloaded partition padding is covered by CRC, but not version scan. */
    make_versioned_golden(flash,"B1.2.0",0);
    put32(flash+0x144,(PAYLOAD-2048)/4); checksum(flash+0x140,15);
    memcpy(flash+PAYLOAD-900,"[GOLDEN] Boot image version: Boot B6.6.6\r\n",sizeof("[GOLDEN] Boot image version: Boot B6.6.6\r\n"));
    footer(flash,PAYLOAD,1,1,MB_FLAG_RECOVERY);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
    assert(!strcmp(version,"B1.2.0") && offset==0);
    /* Historical raw B1.1 has header/loaded identity checks, no invented CRC. */
    make_versioned_golden(flash,"B1.1.0",1);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
    assert(!strcmp(version,"B1.1.0") && offset==0);
    /* Neither absent/corrupt manifests nor B2 raw images use that exception. */
    make_versioned_golden(flash,"B2.0.1",1);
    memset(flash+MB_GOLDEN_TRIAL_OFFSET,0xff,MB_GOLDEN_SIZE);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)!=MB_OK);
    make_versioned_golden(flash,"B1.2.0",0); flash[PAYLOAD]^=1;
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)!=MB_OK);
    make_versioned_golden(flash,"B2.0.1",0);
    memset(flash+1024,0xa5,sizeof(MB_GOLDEN_MARKER)-1);
    footer(flash,PAYLOAD,1,1,MB_FLAG_RECOVERY);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)!=MB_OK);
    memset(flash,0xff,MB_APPLETINI_OFFSET);
    assert(mb_golden_installed_version(&device,version,sizeof version,&offset)!=MB_OK);
    assert(!version[0] && offset==UINT32_MAX && mutations==before);
    make_versioned_golden(flash,"B1.2.0",0);
    assert(mb_golden_installed_version(&device,version,3,&offset)==MB_E_ARGUMENT);
    assert(!version[0] && offset==UINT32_MAX);
    printf("Golden version: primary/trial, CRC/read errors, raw B1.1, loaded-only identity and Unknown cases covered\n");
}
static int check_golden_files(int argc,char **argv)
{
    int i;
    if(argc<4 || (argc&1)) return 1;
    for(i=2;i<argc;i+=2) {
        FILE *f=fopen(argv[i],"rb"); long n; char version[64]; uint32_t offset;
        if(!f || fseek(f,0,SEEK_END) || (n=ftell(f))<=0 || (unsigned long)n>MB_GOLDEN_SIZE || fseek(f,0,SEEK_SET)) {
            fprintf(stderr,"Cannot read Golden: %s\n",argv[i]); if(f) fclose(f); return 1;
        }
        memset(flash,0xff,sizeof flash);
        if(fread(flash,1,(size_t)n,f)!=(size_t)n) { fclose(f); return 1; }
        fclose(f);
        assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
        assert(!strcmp(version,argv[i+1]) && offset==0);
        memcpy(flash+MB_GOLDEN_TRIAL_OFFSET,flash,(size_t)n);
        flash[2000]^=1;
        assert(mb_golden_installed_version(&device,version,sizeof version,&offset)==MB_OK);
        assert(!strcmp(version,argv[i+1]) && offset==MB_GOLDEN_TRIAL_OFFSET);
        printf("Verified installed Golden %s: %s, full CRC and trial fallback\n",argv[i],version);
    }
    return 0;
}
static int check_files(int argc,char **argv)
{
    int i;
    for(i=1;i<argc;++i) {
        FILE *f=fopen(argv[i],"rb"); long n; uint8_t *p; mb_reader_t r; mb_image_t image; int rc;
        if(!f || fseek(f,0,SEEK_END) || (n=ftell(f))<0 || n>0xdf0000L || fseek(f,0,SEEK_SET)) {
            fprintf(stderr,"Cannot read firmware: %s\n",argv[i]); if(f) fclose(f); return 1;
        }
        p=malloc((size_t)n); assert(p);
        if(fread(p,1,(size_t)n,f)!=(size_t)n) { fclose(f); free(p); return 1; }
        fclose(f); r=reader(p,(uint32_t)n); rc=mb_image_validate(&r,0,1,&image);
        if(rc!=MB_OK) { fprintf(stderr,"Invalid firmware: %s (error %d)\n",argv[i],rc); free(p); return 1; }
        printf("Verified %s: manifest v%u, product 0x%03x, payload %u, full image CRC %08x\n",
               argv[i],image.version,image.product,image.payload_size,image.image_crc32);
        free(p);
    }
    return 0;
}
int main(int argc,char **argv)
{
    device.context=NULL; device.read=nor_read; device.erase=nor_erase;
    device.program=nor_program; device.capacity=MB_FLASH_SIZE;
    if(argc>1) return !strcmp(argv[1],"--golden") ? check_golden_files(argc,argv) : check_files(argc,argv);
    make_image(app,PAYLOAD,MB_PRODUCT_APPLETINI,2,0);
    make_image(multi,PAYLOAD,MB_PRODUCT_MULTITINI,2,0);
    make_image(legacy,PAYLOAD,0,2,0); make_image(golden,PAYLOAD,0,1,1);
    selectors(); migrations(); adopted_defaults(); advisory_probes();
    faults(MB_PRODUCT_APPLETINI,0); faults(MB_PRODUCT_MULTITINI,0);
    faults(MB_PRODUCT_MULTITINI,1); adoption_faults(); choice_faults(); golden_versions();
    puts("PASS: multiboot live validation and power-failure invariants"); return 0;
}
