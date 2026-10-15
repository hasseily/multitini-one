/* SPDX-License-Identifier: GPL-3.0-only */
#include "iigs_content_service.h"
#include "sd_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define FILES 20U
#define TEST_MEDIA_ROOT "0:/multitini/cores/apple-iigs/media"
#define TEST_ROM_ROOT "0:/multitini/cores/apple-iigs/roms"
enum { ROM3_FILE,ROM1_FILE,CHARACTER_FILE,ADB3_FILE,ADB1_FILE,HDD_FILE,ROM_FILES };
static const struct { const char *name; size_t size; uint8_t kind; } rom_files[ROM_FILES]={
    {"rom3.rom",MT_SIZE_IIGS_ROM3,MT_LOAD_IIGS_ROM3},
    {"rom1.rom",MT_SIZE_IIGS_ROM1,MT_LOAD_IIGS_ROM1},
    {"char.rom",MT_SIZE_IIGS_CHARACTER,MT_LOAD_IIGS_CHARACTER},
    {"adb3.rom",MT_SIZE_IIGS_ADB,MT_LOAD_IIGS_ADB3},
    {"adb1.rom",MT_SIZE_IIGS_ADB,MT_LOAD_IIGS_ADB1},
    {"hdd.rom",MT_SIZE_IIGS_HDD_ROM,MT_LOAD_IIGS_HDD_ROM}
};
static unsigned rom_reads[32],rom_streams[32],rom_read_count,rom_stream_count;
static int failed_rom;
static mt_sd_status_t rom_failure_status;
typedef struct { char path[256]; uint8_t data[819456]; size_t size; bool present; } test_file_t;
struct mt_sd_file { test_file_t *file; size_t position; bool open; };
static test_file_t files[FILES]; static struct mt_sd_file handles[FILES];
static uint32_t regs[1024], generations[5], mount_sizes[5];
static uint8_t stream_kind, stream_data[512]; static unsigned stream_index,reverse_index;
static const uint8_t *reverse_data;
static uint8_t config_stream[MT_IIGS_CONFIG_BYTES];
static uint32_t completions,completion_status; static bool fail_disk_write,fail_journal,deny_writes,early_last;
static unsigned journal_writes,disk_writes,events; static bool journal_durable;
static unsigned raw_snapshots;
static uint8_t dirty_drives;
static bool pram_eject_write,eject_pending_test;
static unsigned pram_load_packets,warm_reset_packets,pram_load_complete_event,warm_reset_event,recovery_calls;
static bool pram_restore_pending,fail_sector_transfer;
static void request(unsigned slot,uint32_t lba,bool writing);
static mt_sd_result_t result(mt_sd_status_t s,size_t n) { return (mt_sd_result_t){s,0U,n}; }
static test_file_t *file_find(const char *path,bool create)
{
    for(unsigned i=0U;i<FILES;++i)if(files[i].present && strcmp(path,files[i].path)==0)return &files[i];
    if(create)for(unsigned i=0U;i<FILES;++i)if(!files[i].present) {
        memset(&files[i],0,sizeof(files[i])); files[i].present=true; strcpy(files[i].path,path); return &files[i];
    }
    return NULL;
}
static void store32(uint8_t *p,uint32_t v) { for(unsigned i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(i*8U)); }
mt_sd_result_t mt_sd_storage_journal_recover(const char *p,const char *j,uint8_t *b,size_t n)
{ (void)p;(void)j;(void)b;(void)n;++recovery_calls;return result(MT_SD_OK,0U); }
mt_sd_result_t mt_sd_storage_journal_replace(const char *p,const char *j,const uint8_t *b,size_t n)
{
    (void)j; if(fail_journal)return result(MT_SD_WRITE_FAILED,0U);
    test_file_t *f=file_find(p,true); assert(f!=NULL && n<=sizeof(f->data));
    memcpy(f->data,b,n); f->size=n;
    if(strstr(p,".gsj")!=NULL) { ++journal_writes; journal_durable=true; ++events; }
    if(strstr(p,".gsraw")!=NULL) { ++raw_snapshots; ++events; }
    return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char *p,uint8_t *b,size_t n,bool *found)
{
    test_file_t *f=file_find(p,false); *found=f!=NULL;
    if(f==NULL)return result(MT_SD_OK,0U);
    if(f->size!=n)return result(MT_SD_SIZE_MISMATCH,0U);
    memcpy(b,f->data,n); return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_storage_read_file_exact(const char *p,uint8_t *b,size_t n)
{
    unsigned which;
    for(which=0U;which<ROM_FILES;++which) {
        char expected[256];(void)snprintf(expected,sizeof(expected),"%s/%s",TEST_ROM_ROOT,rom_files[which].name);
        if(strcmp(p,expected)==0)break;
    }
    /* Every attempted ROM load must use the exact package sibling path. */
    assert(which<ROM_FILES && n==rom_files[which].size && rom_read_count<32U);
    rom_reads[rom_read_count++]=which;
    if((int)which==failed_rom)return result(rom_failure_status,0U);
    for(size_t i=0U;i<n;++i)b[i]=(uint8_t)(i*7U+1U);
    return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_file_open(const char *p,const char *mode,mt_sd_file_t **out)
{
    if(deny_writes && strchr(mode,'+')!=NULL)return result(MT_SD_OPEN_FAILED,0U);
    test_file_t *f=file_find(p,false); if(f==NULL)return result(MT_SD_OPEN_FAILED,0U);
    for(unsigned i=0U;i<FILES;++i)if(!handles[i].open) { handles[i]=(struct mt_sd_file){f,0U,true}; *out=&handles[i]; return result(MT_SD_OK,0U); }
    return result(MT_SD_FILE_LIMIT,0U);
}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *h,void *b,size_t n)
{ assert(h->open); if(n>h->file->size-h->position)n=h->file->size-h->position; memcpy(b,h->file->data+h->position,n);h->position+=n;return result(MT_SD_OK,n); }
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *h,const void *b,size_t n)
{
    assert(h->open && h->position+n<=sizeof(h->file->data));
    assert(journal_durable); ++disk_writes; ++events;
    if(fail_disk_write) { memcpy(h->file->data+h->position,b,n/2U); return result(MT_SD_WRITE_FAILED,n/2U); }
    memcpy(h->file->data+h->position,b,n); h->position+=n; return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *h,int64_t offset,int origin)
{
    int64_t p=origin==SEEK_END ? (int64_t)h->file->size:origin==SEEK_CUR ? (int64_t)h->position:0;
    p+=offset; assert(p>=0 && (uint64_t)p<=h->file->size); h->position=(size_t)p;return result(MT_SD_OK,0U);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *h) { return (int64_t)h->position; }
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *h) { assert(h->open); h->open=false;return result(MT_SD_OK,0U); }
mt_sd_result_t mt_sd_file_remove(const char *p)
{ test_file_t *f=file_find(p,false);if(f!=NULL)f->present=false;return result(MT_SD_OK,0U); }
const char *mt_sd_status_name(mt_sd_status_t s)
{ return s==MT_SD_OPEN_FAILED ? "SD open failed":s==MT_SD_SIZE_MISMATCH ? "SD size mismatch":"simulated SD failure"; }
static uint32_t read_reg(void *c,uint32_t o)
{
    (void)c;
    if(o==MT_REG_MEDIA_STATE)return (dirty_drives ? MT_MEDIA_STATE_DIRTY:0U)|(eject_pending_test ? MT_MEDIA_STATE_EJECT_PENDING:0U);
    if(o==MT_REG_MEDIA_DATA) {
        if(reverse_index>=512U)return 0U;
        const unsigned i=reverse_index++;
        return MT_MEDIA_DATA_VALID|((uint32_t)(reverse_data==NULL ? (uint8_t)(i+0x31U):reverse_data[i])<<MT_MEDIA_DATA_BYTE_SHIFT)|
            ((i==511U || (early_last && i==10U)) ? MT_MEDIA_DATA_LAST:0U);
    }
    return regs[o/4U];
}
static void write_reg(void *c,uint32_t o,uint32_t v)
{
    (void)c;regs[o/4U]=v;
    if(o==MT_REG_STREAM_DATA_BURST) {
        const unsigned total=regs[MT_REG_COMMAND_ARG1/4U];
        for(unsigned i=0U;i<4U && stream_index<total;++i) {
            if(stream_kind==MT_LOAD_IIGS_SECTOR) { assert(stream_index<512U);stream_data[stream_index]=(uint8_t)(v>>(i*8U)); }
            if(stream_kind==MT_LOAD_IIGS_CONFIG) { assert(stream_index<sizeof(config_stream));config_stream[stream_index]=(uint8_t)(v>>(i*8U)); }
            ++stream_index;
        }
    }
    if(o==MT_REG_COMMAND_DOORBELL) {
        const uint32_t a=regs[MT_REG_COMMAND_ARG0/4U], b=regs[MT_REG_COMMAND_ARG1/4U], d=regs[MT_REG_COMMAND_ARG3/4U];
        uint32_t response=MT_RESPONSE_OK;
        switch(regs[MT_REG_COMMAND_OPCODE/4U]) {
        case MT_COMMAND_MEDIA_MOUNT:
            assert((a&255U)<5U);generations[a&255U]=regs[MT_REG_COMMAND_ARG2/4U];mount_sizes[a&255U]=b;break;
        case MT_COMMAND_MEDIA_EJECT:
            dirty_drives&=(uint8_t)~(1U<<(a&255U));
            if(a==4U && pram_eject_write) {
                regs[MT_REG_MEDIA_REQUEST/4U]=MT_MEDIA_REQUEST_VALID|MT_MEDIA_REQUEST_WRITE;
                regs[MT_REG_MEDIA_REQUEST_ID/4U]=0xeeeeU;regs[MT_REG_MEDIA_REQUEST_LBA/4U]=0U;
                regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]=generations[4];
                regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]=0x402U;reverse_index=0U;eject_pending_test=true;
            }
            break;
        case MT_COMMAND_STREAM_BEGIN:
            stream_kind=(uint8_t)a;stream_index=0U;
            for(unsigned i=0U;i<ROM_FILES;++i)if(a==rom_files[i].kind) {
                assert(rom_stream_count<32U && b==rom_files[i].size);rom_streams[rom_stream_count++]=i;break;
            }
            break;
        case MT_COMMAND_STREAM_END:
            if(stream_kind==MT_LOAD_IIGS_SECTOR) {
                assert(stream_index==512U);
                if(fail_sector_transfer)response=MT_RESPONSE_IO_ERROR;
            }
            if(stream_kind==MT_LOAD_IIGS_CONFIG) {
                assert(stream_index==sizeof(config_stream));
                if((config_stream[8]&16U)!=0U) {
                    assert((config_stream[8]&2U)==0U && !pram_restore_pending);
                    ++pram_load_packets;pram_restore_pending=true;
                    request(4U,(config_stream[0]&1U)!=0U ? 0U:1U,false);
                }
                if((config_stream[8]&2U)!=0U) {
                    assert(!pram_restore_pending && (regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)==0U);
                    ++warm_reset_packets;warm_reset_event=++events;
                }
            }
            break;
        case MT_COMMAND_MEDIA_COMPLETE:
            ++completions;completion_status=b;
            if(b==MT_RESPONSE_OK) {
                assert(regs[MT_REG_COMMAND_ARG2/4U]==regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]);
                if(pram_restore_pending) {
                    assert(regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]==0x402U && stream_kind==MT_LOAD_IIGS_SECTOR && stream_index==512U);
                    pram_restore_pending=false;pram_load_complete_event=++events;
                }
                regs[MT_REG_MEDIA_REQUEST/4U]=0U;eject_pending_test=false;
            }
            else assert(regs[MT_REG_COMMAND_ARG2/4U]==0U);
            assert(d==regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]);break;
        default: break;
        }
        regs[MT_REG_RESPONSE_SEQ/4U]=v;regs[MT_REG_RESPONSE_STATUS/4U]=response;
    }
}
static mt_core_link_t core;
static void reset(void)
{
    memset(files,0,sizeof(files));memset(handles,0,sizeof(handles));memset(regs,0,sizeof(regs));
    memset(generations,0,sizeof(generations));memset(mount_sizes,0,sizeof(mount_sizes));
    fail_disk_write=fail_journal=deny_writes=early_last=pram_eject_write=eject_pending_test=false;
    pram_restore_pending=fail_sector_transfer=false;
    failed_rom=-1;rom_failure_status=MT_SD_OPEN_FAILED;rom_read_count=rom_stream_count=0U;
    pram_load_packets=warm_reset_packets=pram_load_complete_event=warm_reset_event=recovery_calls=0U;
    journal_writes=disk_writes=events=completions=0U; journal_durable=false;dirty_drives=0U;
    raw_snapshots=0U;reverse_data=NULL;
    regs[MT_REG_ABI_VERSION/4U]=MT_ABI_VERSION;
    regs[MT_REG_STREAM_STATUS/4U]=MT_STREAM_STATUS_READY|MT_STREAM_STATUS_DRAINED;
    regs[MT_REG_STATUS/4U]=MT_STATUS_CORE_READY;
    mt_core_link_init(&core,NULL,read_reg,write_reg);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
}
static test_file_t *disk(const char *name)
{
    char p[256];(void)snprintf(p,sizeof(p),"0:/multitini/cores/apple-iigs/media/%s",name);
    test_file_t *f=file_find(p,true);assert(f!=NULL);f->size=2048U;
    for(unsigned i=0U;i<f->size;++i)f->data[i]=(uint8_t)i;
    return f;
}
static void request(unsigned slot,uint32_t lba,bool writing)
{
    static uint32_t id=1U;
    regs[MT_REG_MEDIA_REQUEST/4U]=MT_MEDIA_REQUEST_VALID|(writing ? MT_MEDIA_REQUEST_WRITE:0U);
    regs[MT_REG_MEDIA_REQUEST_ID/4U]=id++;regs[MT_REG_MEDIA_REQUEST_LBA/4U]=lba;
    regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]=generations[slot];
    regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]=(slot<<8U)|2U;reverse_index=0U;
}
static void assert_rom_loads(const unsigned order[4],unsigned read_start,unsigned stream_start)
{
    assert(rom_read_count==read_start+4U && rom_stream_count==stream_start+4U);
    for(unsigned i=0U;i<4U;++i)assert(rom_reads[read_start+i]==order[i] && rom_streams[stream_start+i]==order[i]);
}
static void test_rom_paths(void)
{
    static const unsigned rom3_order[4]={ROM3_FILE,CHARACTER_FILE,ADB3_FILE,HDD_FILE};
    static const unsigned rom1_order[4]={ROM1_FILE,CHARACTER_FILE,ADB1_FILE,HDD_FILE};
    reset();assert(iigs_content_service_boot()==0);assert_rom_loads(rom3_order,0U,0U);
    mt_iigs_config_t c=*iigs_content_service_config();c.rom3=0U;
    assert(iigs_content_service_configure(&c)==0);assert_rom_loads(rom1_order,4U,4U);
    assert(file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false)!=NULL);
    /* A fresh activation restores the ROM1 setting and uses the same paths. */
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);rom_read_count=rom_stream_count=0U;
    assert(iigs_content_service_boot()==0);assert_rom_loads(rom1_order,0U,0U);
    assert(iigs_content_service_boot()==0);assert_rom_loads(rom1_order,4U,4U);

    for(unsigned missing=0U;missing<ROM_FILES;++missing) {
        reset();const unsigned *order=rom3_order;
        if(missing==ROM1_FILE || missing==ADB1_FILE) {
            c=*iigs_content_service_config();c.rom3=0U;
            assert(iigs_content_service_configure(&c)==0);order=rom1_order;
            rom_read_count=rom_stream_count=0U;
        }
        unsigned before_failure=0U;while(order[before_failure]!=missing)++before_failure;
        failed_rom=(int)missing;
        assert(iigs_content_service_boot()!=0 && !iigs_content_service_ready());
        assert(rom_read_count==before_failure+1U && rom_stream_count==before_failure);
        char expected[128];(void)snprintf(expected,sizeof(expected),"ROM %s: SD open failed",rom_files[missing].name);
        assert(strcmp(iigs_content_service_error(),expected)==0);
        /* Explicit System ROMs retry must retry the new path, with no legacy fallback. */
        failed_rom=-1;assert(iigs_content_service_boot()==0);
        assert_rom_loads(order,before_failure+1U,before_failure);
    }
    reset();failed_rom=CHARACTER_FILE;rom_failure_status=MT_SD_SIZE_MISMATCH;
    assert(iigs_content_service_boot()!=0 && strcmp(iigs_content_service_error(),"ROM char.rom: SD size mismatch")==0);
    assert(rom_read_count==2U && rom_stream_count==1U);

    static const struct { const char *path,*error; } invalid[]={
        {NULL,"Invalid Apple IIgs content folder"},
        {"0:/multitini/cores/apple-iigs/media/../media","Invalid Apple IIgs content folder"},
        {"0:/multitini/cores/apple-iigs","Apple IIgs content folder must end in /media"},
        {TEST_MEDIA_ROOT "/","Apple IIgs content folder must end in /media"},
        {TEST_MEDIA_ROOT "2","Apple IIgs content folder must end in /media"}
    };
    for(unsigned i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        reset();const unsigned recovery_before=recovery_calls;
        iigs_content_service_init(&core,invalid[i].path,NULL,NULL);
        assert(iigs_content_service_faulted() && strcmp(iigs_content_service_error(),invalid[i].error)==0);
        assert(recovery_calls==recovery_before && rom_read_count==0U);
    }
    char too_long[140];memset(too_long,'a',sizeof(too_long));memcpy(too_long,"0:/",3U);
    memcpy(too_long+sizeof(too_long)-7U,"/media",7U);
    reset();iigs_content_service_init(&core,too_long,NULL,NULL);
    assert(iigs_content_service_faulted() && strcmp(iigs_content_service_error(),"Invalid Apple IIgs content folder")==0);
}
static void test_speed_capability(void)
{
    reset();
    assert(iigs_content_service_speed_mask()==MT_IIGS_ACCEL_LEGACY_SPEED_MASK);
    mt_iigs_config_t c=*iigs_content_service_config();c.speed=6U;
    assert(iigs_content_service_configure(&c)!=0);
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=MT_IIGS_ACCEL_CAPS_VALID|MT_IIGS_ACCEL_SPEED_MASK;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_speed_mask()==MT_IIGS_ACCEL_SPEED_MASK);
    c=*iigs_content_service_config();c.speed=6U;
    assert(iigs_content_service_configure(&c)==0);
    /* Retained ABI21 images do not implement the new register. Saved settings
     * clamp to their actual legacy range without rejecting the settings file. */
    regs[MT_REG_ABI_VERSION/4U]=21U;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_speed_mask()==MT_IIGS_ACCEL_LEGACY_SPEED_MASK);
    assert(iigs_content_service_config()->speed==4U && !iigs_content_service_faulted());
    regs[MT_REG_ABI_VERSION/4U]=MT_ABI_VERSION;
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=MT_IIGS_ACCEL_CAPS_VALID|UINT32_C(0x21);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_config()->speed==5U);
    c=*iigs_content_service_config();c.speed=4U;
    assert(iigs_content_service_configure(&c)!=0);
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=MT_IIGS_ACCEL_CAPS_VALID;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_speed_mask()==1U && iigs_content_service_config()->speed==0U);
}
static void test_irq_default_and_saved_choices(void)
{
    reset();
    assert(iigs_content_service_config()->flags==UINT8_C(0x10));
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=MT_IIGS_ACCEL_CAPS_VALID|MT_IIGS_ACCEL_SPEED_MASK;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_boot()==0 && config_stream[3]==UINT8_C(0x10));
    /* A pre-existing explicit On survives initialization byte-for-byte, even
     * when unrelated compatibility choices differ from the fresh default. */
    mt_iigs_config_t c=*iigs_content_service_config();c.speed=6U;c.flags=UINT8_C(0x05);
    assert(iigs_content_service_configure(&c)==0);
    assert(config_stream[1]==6U && config_stream[3]==UINT8_C(0x05));
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);
    assert(saved!=NULL && saved->data[11]==UINT8_C(0x05));
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_config()->flags==UINT8_C(0x05) && iigs_content_service_config()->speed==6U);
    assert(saved->data[11]==UINT8_C(0x05));
    /* Explicit Off is equally persistent; no existing file is migrated. */
    c=*iigs_content_service_config();c.flags=UINT8_C(0x15);
    assert(iigs_content_service_configure(&c)==0);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_config()->flags==UINT8_C(0x15) && iigs_content_service_config()->speed==6U);
    assert(saved->data[11]==UINT8_C(0x15));
    fail_journal=true;c=*iigs_content_service_config();c.flags=UINT8_C(0x05);
    assert(iigs_content_service_configure(&c)!=0);
    assert(iigs_content_service_config()->flags==UINT8_C(0x15) && config_stream[3]==UINT8_C(0x15));
    assert(saved->data[11]==UINT8_C(0x15));
}
static void test_nested_media_paths(void)
{
    static const char *const first="Operating Systems/GS OS/System.hdv";
    static const char *const second="Archive/System.hdv";
    reset();test_file_t *f=disk(first);(void)disk(second);
    assert(iigs_content_service_mount(0U,first)==0);
    assert(iigs_content_service_mount(1U,second)==0);
    assert(strcmp(iigs_content_service_name(0U),first)==0 && strcmp(iigs_content_service_name(1U),second)==0);
    request(0U,1U,true);fail_disk_write=true;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR);
    assert(file_find(TEST_MEDIA_ROOT "/Operating Systems/GS OS/System.hdv.gsj",false)!=NULL);
    assert(file_find(TEST_MEDIA_ROOT "/System.hdv.gsj",false)==NULL);
    fail_disk_write=false;assert(iigs_content_service_retry()==0 && completion_status==MT_RESPONSE_OK);
    assert(f->data[512U]==0x31U && file_find(TEST_MEDIA_ROOT "/Operating Systems/GS OS/System.hdv.gsj",false)==NULL);
    request(0U,1U,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK && stream_data[0]==0x31U);
    assert(iigs_content_service_prepare_reset()==0);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_boot()==0);
    assert(strcmp(iigs_content_service_name(0U),first)==0 && strcmp(iigs_content_service_name(1U),second)==0);
    static const char *const invalid[]={"../outside.hdv","Operating Systems/../outside.hdv","/outside.hdv","0:/outside.hdv","A//disk.hdv","A\\disk.hdv"};
    for(size_t i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        reset();assert(iigs_content_service_mount(0U,invalid[i])!=0);
        assert(iigs_content_service_name(0U)==NULL);
    }
}
static void settings_checksum(test_file_t *file)
{
    store32(file->data+20U,0U);uint32_t crc=UINT32_MAX;
    for(size_t i=0U;i<file->size;++i) {
        crc^=file->data[i];
        for(unsigned bit=0U;bit<8U;++bit)crc=(crc>>1U)^((0U-(crc&1U))&UINT32_C(0xedb88320));
    }
    store32(file->data+20U,~crc);
}
static void test_mouse_settings(void)
{
    reset();assert(iigs_content_service_mouse_sensitivity()==100U);
    (void)disk("Games/boot.hdv");assert(iigs_content_service_mount(0U,"Games/boot.hdv")==0);
    mt_iigs_config_t chosen=*iigs_content_service_config();chosen.flags=5U;chosen.speed=4U;
    assert(iigs_content_service_configure(&chosen)==0);
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);
    assert(saved!=NULL && saved->data[17]==4U);
    const size_t legacy_size=saved->size;
    /* The original record's unused byte is zero; all existing choices and
     * remembered media must load without a file migration or size change. */
    saved->data[17]=0U;settings_checksum(saved);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_mouse_sensitivity()==100U && saved->data[17]==0U);
    assert(iigs_content_service_config()->flags==5U && iigs_content_service_config()->speed==4U);
    const uint16_t steps[]={25U,50U,75U,100U,150U,200U,300U,400U};
    const unsigned before=rom_stream_count;
    for(unsigned i=0U;i<8U;++i) {
        assert(iigs_content_service_set_mouse_sensitivity(steps[i])==0);
        assert(saved->size==legacy_size && saved->data[17]==steps[i]/25U);
        assert(memcmp(saved->data+24U,"Games/boot.hdv",14U)==0);
        iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
        assert(iigs_content_service_mouse_sensitivity()==steps[i]);
        assert(iigs_content_service_config()->flags==5U && iigs_content_service_config()->speed==4U);
    }
    assert(rom_stream_count==before); /* ARM-only option sends no FPGA config. */
    assert(iigs_content_service_set_mouse_sensitivity(0U)!=0 && iigs_content_service_mouse_sensitivity()==400U);
    assert(iigs_content_service_set_mouse_sensitivity(125U)!=0 && saved->data[17]==16U);
    fail_journal=true;
    assert(iigs_content_service_set_mouse_sensitivity(25U)!=0 && iigs_content_service_mouse_sensitivity()==400U);
    assert(saved->data[17]==16U && saved->size==legacy_size);fail_journal=false;
    saved->data[17]=5U;settings_checksum(saved); /* Valid CRC, unsupported value. */
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_faulted() && iigs_content_service_mouse_sensitivity()==100U);
    assert(strstr(iigs_content_service_error(),"settings checksum or values invalid")!=NULL);
}
static uint8_t raw_source[MT_IIGS_FLOPPY_RAW_TRACK_BYTES],raw_changed[MT_IIGS_FLOPPY_RAW_TRACK_BYTES];
static uint8_t encoded[MT_IIGS_FLOPPY_TRACK_BYTES];
static test_file_t *raw_disk(const char *name,uint8_t format,unsigned offset)
{
    test_file_t *f=disk(name);
    const unsigned size=format==MT_IIGS_RAW_DOS525 ? 143360U:819200U;
    f->size=size+offset+(offset==0U ? 0U:24U);
    for(unsigned i=0U;i<f->size;++i)f->data[i]=(uint8_t)(i*31U+(i>>9U)*17U);
    if(offset!=0U) {
        memset(f->data,0,64U);memcpy(f->data,"2IMG",4U);f->data[8]=64U;f->data[10]=1U;
        store32(f->data+12U,1U);store32(f->data+20U,size/512U);
        store32(f->data+24U,offset);store32(f->data+28U,size);
    }
    return f;
}
static void raw_prepare(test_file_t *f,uint8_t format,unsigned track,unsigned offset)
{
    mt_iigs_floppy_track_t t;assert(mt_iigs_floppy_track(format,track,&t));
    memcpy(raw_source,f->data+offset+t.raw_offset,t.raw_bytes);
    memcpy(raw_changed,raw_source,t.raw_bytes);
    for(unsigned i=0U;i<t.raw_bytes;++i)raw_changed[i]^=(uint8_t)(i*13U+0x61U);
    assert(mt_iigs_floppy_encode(format,track,raw_changed,encoded));
}
static void raw_send(unsigned slot,const mt_iigs_floppy_track_t *t,unsigned block)
{
    reverse_data=encoded+block*512U;request(slot,t->first_block+block,true);iigs_content_service_poll();
}
static void simulate_power_cut(void)
{
    memset(handles,0,sizeof(handles));memset(regs,0,sizeof(regs));
    regs[MT_REG_ABI_VERSION/4U]=MT_ABI_VERSION;
    regs[MT_REG_STREAM_STATUS/4U]=MT_STREAM_STATUS_READY|MT_STREAM_STATUS_DRAINED;
    regs[MT_REG_STATUS/4U]=MT_STATUS_CORE_READY;
    fail_disk_write=fail_journal=false;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
}
static void test_raw_floppy_transport(void)
{
    const uint8_t formats[]={MT_IIGS_RAW_DOS525,MT_IIGS_RAW_PRODOS35,MT_IIGS_RAW_PRODOS35};
    const char *const names[]={"Games/DOS.dsk","Games/Sony.po","Games/Sony.2mg"};
    for(unsigned test=0U;test<3U;++test) {
        const unsigned slot=test==0U ? 3U:2U,offset=test==2U ? 128U:0U;
        const uint8_t format=formats[test];
        reset();test_file_t *f=raw_disk(names[test],format,offset);
        if(test==2U)store32(f->data+28U,0U); /* Preserve the WOOF header variant. */
        uint8_t container[128],tail[24];
        if(offset!=0U) { memcpy(container,f->data,offset);memcpy(tail,f->data+f->size-24U,24U); }
        assert(iigs_content_service_mount(slot,names[test])==0);
        assert(mount_sizes[slot]==mt_iigs_floppy_woz_size(format));
        request(slot,0U,false);iigs_content_service_poll();
        assert(completion_status==MT_RESPONSE_OK && memcmp(stream_data,"WOZ2",4U)==0);
        assert(stream_data[21]==(slot==2U ? 2U:1U));
        const unsigned track=test==0U ? 17U:137U;
        mt_iigs_floppy_track_t t;assert(mt_iigs_floppy_track(format,track,&t));
        raw_prepare(f,format,track,offset);
        uint8_t original_bits[MT_IIGS_FLOPPY_TRACK_BYTES];
        assert(mt_iigs_floppy_encode(format,track,raw_source,original_bits));
        for(unsigned block=0U;block<t.block_count;++block) {
            request(slot,t.first_block+block,false);iigs_content_service_poll();
            assert(completion_status==MT_RESPONSE_OK && memcmp(stream_data,original_bits+block*512U,512U)==0);
        }
        for(unsigned block=0U;block<t.block_count;++block) {
            raw_send(slot,&t,block);assert(completion_status==MT_RESPONSE_OK);
            assert(raw_snapshots==block+1U);
            if(block+1U<t.block_count)assert(disk_writes==0U && memcmp(f->data+offset+t.raw_offset,raw_source,t.raw_bytes)==0);
        }
        assert(disk_writes==t.raw_bytes/512U && memcmp(f->data+offset+t.raw_offset,raw_changed,t.raw_bytes)==0);
        char sidecar[256];(void)snprintf(sidecar,sizeof(sidecar),TEST_MEDIA_ROOT "/%s.gsraw",names[test]);
        assert(file_find(sidecar,false)==NULL);
        if(offset!=0U)assert(memcmp(container,f->data,offset)==0 && memcmp(tail,f->data+f->size-24U,24U)==0);
        assert(iigs_content_service_eject(slot)==0);
        assert(iigs_content_service_mount(slot,names[test])==0);
        request(slot,t.first_block,false);iigs_content_service_poll();
        assert(completion_status==MT_RESPONSE_OK && memcmp(stream_data,encoded,512U)==0);
    }
}
static void test_raw_floppy_recovery(void)
{
    reset();test_file_t *live=raw_disk("live.2mg",MT_IIGS_RAW_PRODOS35,64U);
    assert(iigs_content_service_mount(2U,"live.2mg")==0);
    mt_iigs_floppy_track_t live_track;assert(mt_iigs_floppy_track(MT_IIGS_RAW_PRODOS35,0U,&live_track));
    raw_prepare(live,MT_IIGS_RAW_PRODOS35,0U,64U);
    for(unsigned i=0U;i<live_track.block_count;++i) {
        if(i+1U==live_track.block_count)fail_disk_write=true;
        raw_send(2U,&live_track,i);
    }
    assert(completion_status==MT_RESPONSE_IO_ERROR && reverse_index==512U);
    fail_disk_write=false;
    /* Raw commit reused the sector scratch buffer. Retry must restore the
     * original final bitstream block rather than replay that scratch data. */
    assert(iigs_content_service_retry()==0 && completion_status==MT_RESPONSE_OK && reverse_index==512U);
    assert(memcmp(live->data+64U,raw_changed,live_track.raw_bytes)==0);

    reset();test_file_t *f=raw_disk("retry.po",MT_IIGS_RAW_PRODOS35,0U);
    assert(iigs_content_service_mount(2U,"retry.po")==0);
    mt_iigs_floppy_track_t t;assert(mt_iigs_floppy_track(MT_IIGS_RAW_PRODOS35,0U,&t));
    raw_prepare(f,MT_IIGS_RAW_PRODOS35,0U,0U);
    fail_journal=true;raw_send(2U,&t,0U);
    assert(completion_status==MT_RESPONSE_IO_ERROR && disk_writes==0U && raw_snapshots==0U);
    fail_journal=false;assert(iigs_content_service_retry()==0 && completion_status==MT_RESPONSE_OK && reverse_index==512U);
    for(unsigned i=1U;i<t.block_count;++i) {
        if(i+1U==t.block_count)fail_disk_write=true;
        raw_send(2U,&t,i);
    }
    assert(completion_status==MT_RESPONSE_IO_ERROR);
    assert(file_find(TEST_MEDIA_ROOT "/retry.po.gsraw",false)!=NULL && file_find(TEST_MEDIA_ROOT "/retry.po.gsj",false)!=NULL);
    /* Both the partially written raw sector and remaining track sectors are
     * replayed after RAM state and the old transport FIFO disappear. */
    simulate_power_cut();assert(iigs_content_service_mount(2U,"retry.po")==0);
    assert(memcmp(f->data,raw_changed,t.raw_bytes)==0);
    assert(file_find(TEST_MEDIA_ROOT "/retry.po.gsraw",false)==NULL && file_find(TEST_MEDIA_ROOT "/retry.po.gsj",false)==NULL);

    reset();f=raw_disk("short.dsk",MT_IIGS_RAW_DOS525,0U);
    assert(iigs_content_service_mount(3U,"short.dsk")==0);
    assert(mt_iigs_floppy_track(MT_IIGS_RAW_DOS525,0U,&t));raw_prepare(f,MT_IIGS_RAW_DOS525,0U,0U);
    raw_send(3U,&t,0U);assert(completion_status==MT_RESPONSE_OK);
    simulate_power_cut();assert(iigs_content_service_mount(3U,"short.dsk")!=0);
    assert(strstr(iigs_content_service_error(),"cannot decode")!=NULL);
    assert(memcmp(f->data,raw_source,t.raw_bytes)==0 && file_find(TEST_MEDIA_ROOT "/short.dsk.gsraw",false)!=NULL);
    for(unsigned h=0U;h<FILES;++h)assert(!handles[h].open);

    /* A complete changed sector can be recovered even if power interrupts
     * transfer of unchanged final padding blocks. */
    reset();f=raw_disk("partial.po",MT_IIGS_RAW_PRODOS35,0U);
    assert(iigs_content_service_mount(2U,"partial.po")==0);
    assert(mt_iigs_floppy_track(MT_IIGS_RAW_PRODOS35,0U,&t));raw_prepare(f,MT_IIGS_RAW_PRODOS35,0U,0U);
    memcpy(raw_changed+512U,raw_source+512U,t.raw_bytes-512U);
    assert(mt_iigs_floppy_encode(MT_IIGS_RAW_PRODOS35,0U,raw_changed,encoded));
    for(unsigned i=0U;i<t.block_count-1U;++i)raw_send(2U,&t,i);
    simulate_power_cut();assert(iigs_content_service_mount(2U,"partial.po")==0);
    assert(memcmp(f->data,raw_changed,t.raw_bytes)==0);
}
static void test_raw_floppy_protection(void)
{
    reset();test_file_t *f=raw_disk("locked.2mg",MT_IIGS_RAW_PRODOS35,64U);
    store32(f->data+16U,UINT32_C(0x80000000));
    assert(iigs_content_service_mount(2U,"locked.2mg")==0);
    request(2U,0U,false);iigs_content_service_poll();assert(stream_data[22]==1U);
    request(2U,3U,true);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && reverse_index==0U && disk_writes==0U && raw_snapshots==0U);
    static const char *const suffixes[]={".gsraw",".gsraw.mtj"};
    for(unsigned i=0U;i<2U;++i) {
        reset();(void)raw_disk("readonly.po",MT_IIGS_RAW_PRODOS35,0U);deny_writes=true;
        char path[256];(void)snprintf(path,sizeof(path),TEST_MEDIA_ROOT "/readonly.po%s",suffixes[i]);
        test_file_t *s=file_find(path,true);assert(s!=NULL);s->size=10304U;memset(s->data,0xa5,s->size);
        assert(iigs_content_service_mount(2U,"readonly.po")!=0);
        assert(strstr(iigs_content_service_error(),"requires writable journal recovery")!=NULL);
        assert(disk_writes==0U && file_find(path,false)==s && s->data[0]==0xa5U);
    }
}
int main(void)
{
    test_raw_floppy_transport();
    test_raw_floppy_recovery();
    test_raw_floppy_protection();
    test_speed_capability();
    test_irq_default_and_saved_choices();
    test_mouse_settings();
    test_rom_paths();
    test_nested_media_paths();
    reset();test_file_t *f=disk("test.2mg");memset(f->data,0,64U);memcpy(f->data,"2IMG",4U);f->data[8]=64U;f->data[10]=1U;
    store32(f->data+12U,1U);store32(f->data+20U,3U);store32(f->data+24U,512U);store32(f->data+28U,1536U);
    assert(iigs_content_service_mount(1U,"test.2mg")==0 && mount_sizes[1]==1536U);
    request(1U,1U,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK && stream_data[1]==1U);
    request(1U,1U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK && journal_writes==1U && disk_writes==1U);
    assert(f->data[1024U]==0x31U && f->data[1535U]==0x30U && memcmp(f->data,"2IMG",4U)==0);
    assert(file_find("0:/multitini/cores/apple-iigs/media/test.2mg.gsj",false)==NULL);
    request(1U,3U,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_INVALID_ARGUMENT || completion_status==MT_RESPONSE_IO_ERROR);
    assert(iigs_content_service_eject(1U)!=0); /* A failed live request blocks eject. */

    reset();f=disk("write.hdv");assert(iigs_content_service_mount(0U,"write.hdv")==0);
    request(0U,0U,true);fail_disk_write=true;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && iigs_content_service_faulted());
    assert(file_find("0:/multitini/cores/apple-iigs/media/write.hdv.gsj",false)!=NULL);
    fail_disk_write=false;assert(iigs_content_service_retry()==0 && completion_status==MT_RESPONSE_OK);
    assert(f->data[511U]==0x30U && reverse_index==512U); /* Retry uses retained sector, not exhausted FIFO. */
    assert(iigs_content_service_eject(0U)==0);

    reset();f=disk("powerloss.hdv");assert(iigs_content_service_mount(0U,"powerloss.hdv")==0);
    request(0U,1U,true);fail_disk_write=true;iigs_content_service_poll();
    assert(file_find("0:/multitini/cores/apple-iigs/media/powerloss.hdv.gsj",false)!=NULL);
    /* A new boot discards the old handle and RAM transfer state. The durable
     * journal repairs the partially written image before it is mounted. */
    memset(handles,0,sizeof(handles));regs[MT_REG_MEDIA_REQUEST/4U]=0U;fail_disk_write=false;
    iigs_content_service_init(&core,"0:/multitini/cores/apple-iigs/media",NULL,NULL);
    assert(iigs_content_service_mount(0U,"powerloss.hdv")==0);
    assert(f->data[512U]==0x31U && f->data[1023U]==0x30U);
    assert(file_find("0:/multitini/cores/apple-iigs/media/powerloss.hdv.gsj",false)==NULL);

    reset();f=disk("protected.hdv");deny_writes=true;assert(iigs_content_service_mount(0U,"protected.hdv")==0);
    request(0U,0U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_IO_ERROR && disk_writes==0U);

    static const char *const sidecars[]={".gsj",".gsj.mtj"};
    for(unsigned i=0U;i<2U;++i) {
        reset();f=disk("recovery.hdv");deny_writes=true;
        char p[256];(void)snprintf(p,sizeof(p),"0:/multitini/cores/apple-iigs/media/recovery.hdv%s",sidecars[i]);
        test_file_t *sidecar=file_find(p,true);assert(sidecar!=NULL);
        sidecar->size=i==0U ? 544U:576U;memset(sidecar->data,0xa5,sidecar->size);
        const unsigned recovery_before=recovery_calls;
        assert(iigs_content_service_mount(0U,"recovery.hdv")!=0);
        assert(strcmp(iigs_content_service_error(),"Disk requires writable journal recovery")==0);
        assert(generations[0]==0U && mount_sizes[0]==0U && recovery_calls==recovery_before && disk_writes==0U);
        assert(file_find(p,false)==sidecar && sidecar->data[0]==0xa5U && f->data[0]==0U);
        for(unsigned h=0U;h<FILES;++h)assert(!handles[h].open);
    }

    reset();f=disk("disk.woz");memset(f->data,0,sizeof(f->data));memcpy(f->data,"WOZ2\xff\x0a\x0d\x0a",8U);
    memcpy(f->data+12U,"INFO",4U);store32(f->data+16U,60U);f->data[20]=2U;f->data[21]=1U;store32(f->data+8U,0x12345678U);
    assert(iigs_content_service_mount(3U,"disk.woz")==0);
    request(3U,1U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK && journal_writes==2U);
    assert(f->data[8U]==0U && f->data[9U]==0U && f->data[512U]==0x31U);
    dirty_drives=12U; /* Two motor-on floppy tracks only flush after eject. */
    assert(iigs_content_service_eject(3U)==0 && dirty_drives==4U);

    reset();assert(iigs_content_service_boot()==0 && iigs_content_service_ready());
    request(4U,1U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK);
    f=file_find("0:/multitini/cores/apple-iigs/media/pram.bin",false);assert(f!=NULL && f->size==1024U && f->data[512U]==0x31U);
    mt_iigs_config_t c=*iigs_content_service_config();c.speed=4U;
    assert(iigs_content_service_configure(&c)==0);c.card=1U;assert(iigs_content_service_configure(&c)!=0);
    assert(iigs_content_service_pram(false)==0);
    assert(pram_load_packets==1U && warm_reset_packets==1U && !pram_restore_pending);
    assert(pram_load_complete_event!=0U && warm_reset_event>pram_load_complete_event);
    assert(memcmp(stream_data,f->data,512U)==0);
    pram_eject_write=true;
    assert(iigs_content_service_prepare_reset()==0 && !eject_pending_test && completion_status==MT_RESPONSE_OK);
    f=file_find("0:/multitini/cores/apple-iigs/media/pram.bin",false);assert(f!=NULL && f->data[0]==0x31U);

    reset();assert(iigs_content_service_boot()==0);
    request(4U,0U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK);
    fail_sector_transfer=true;
    assert(iigs_content_service_pram(false)!=0 && completion_status==MT_RESPONSE_IO_ERROR);
    assert(pram_load_packets==1U && warm_reset_packets==0U && pram_restore_pending);
    puts("Apple IIgs sectors, 2MG offsets, protected recovery, retry journals, WOZ CRC and PRAM reset ordering passed");return 0;
}
