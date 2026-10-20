/* SPDX-License-Identifier: GPL-3.0-only */
#include "iigs_content_service.h"
#include "sd_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define FILES 20U
#define TEST_MEDIA_ROOT "0:/multitini/cores/apple-iigs/media"
#define TEST_ROM_ROOT "0:/multitini/cores/apple-iigs/roms"
#define TEST_IIGS_CAPS (MT_IIGS_ACCEL_CAPS_VALID|MT_IIGS_ACCEL_SPEED_MASK|MT_IIGS_MEDIA_TARGET_DRAIN_SUPPORTED|MT_IIGS_FAST_FLOPPY_SUPPORTED)
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
typedef struct { char path[256]; uint8_t data[819456]; uint64_t size; bool present,sparse,sparse_written;
    uint64_t sparse_offset;uint8_t sparse_data[512]; } test_file_t;
struct mt_sd_file { test_file_t *file; uint64_t position; bool open; };
static test_file_t files[FILES]; static struct mt_sd_file handles[FILES];
static uint32_t regs[1024], generations[MT_IIGS_MEDIA_SLOT_COUNT];
static uint64_t mount_sizes[MT_IIGS_MEDIA_SLOT_COUNT];
static uint8_t mount_kinds[MT_IIGS_MEDIA_SLOT_COUNT];
static uint8_t stream_kind, stream_data[512]; static unsigned stream_index,reverse_index;
static const uint8_t *reverse_data;
static uint8_t config_stream[MT_IIGS_CONFIG_BYTES];
static uint32_t completions,completion_status; static bool fail_disk_write,fail_journal,fail_settings_journal,deny_writes,early_last;
static unsigned journal_writes,disk_writes,events; static bool journal_durable;
static unsigned raw_snapshots;
static uint16_t dirty_drives;
static bool pram_eject_write,eject_pending_test;
static unsigned pram_load_packets,warm_reset_packets,pram_load_complete_event,warm_reset_event,recovery_calls;
static bool pram_restore_pending,fail_sector_transfer;
static unsigned reset_assertions,reset_releases,reset_event,release_event,config_event,close_event,mount_event[MT_IIGS_MEDIA_SLOT_COUNT];
static bool core_reset_asserted;
static uint8_t applied_fast_floppy;
static bool fail_media_complete;
static bool fail_config_begin,fail_config_end_once,config_committed;
static unsigned pram_save_pulses;
static bool reject_smartport_rom;
static uint64_t last_read_offset,last_write_offset;
static unsigned progress_calls,sector_streams,file_open_calls;
/* Optional adapter model for drains: unrelated HDD work continues while a
 * target eject crosses clock domains and publishes a durable raw-track flush.
 * The corrected IIgs shell preserves a live read snapshot on MEDIA_EJECT. */
static struct {
    bool enabled,hdd_admit,eject_wait,busy_target,fail_target_completion,fail_pram_completion;
    bool legacy_eject_cancels_read,orphaned_read,hdd_turn,prior_pram_eject_pending;
    unsigned hdd_left,hdd_completions,ejects,eject_delay,target_completions;
    unsigned target_closes,completion_attempts,pram_completions,pram_admission_delay,coalesced_pram_pulses;
    unsigned flush_lba,flush_blocks,flush_index;
    const uint8_t *flush_data;
    test_file_t *target;
} media_model;
static bool queued_pram,queued_pram_write;
static unsigned queued_pram_lba,queued_pram_hdd_delay;
static void count_progress(void *context) { assert(context==&progress_calls);++progress_calls; }
static void request(unsigned slot,uint32_t lba,bool writing);
static unsigned request_slot(void)
{ return (regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]>>8U)&255U; }
static void post_queued_request(void)
{
    if((regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)!=0U || media_model.orphaned_read)return;
    if(queued_pram && queued_pram_hdd_delay==0U) {
        request(MT_IIGS_PRAM_SLOT,queued_pram_lba,queued_pram_write);
        reverse_data=NULL;queued_pram=false;return;
    }
    if(media_model.enabled && media_model.eject_wait && media_model.eject_delay==0U &&
        !media_model.busy_target && (!media_model.hdd_turn || !media_model.hdd_admit || media_model.hdd_left==0U)) {
        assert(media_model.flush_index<media_model.flush_blocks);
        request(2U,media_model.flush_lba+media_model.flush_index,true);
        reverse_data=media_model.flush_data+media_model.flush_index*512U;return;
    }
    if(media_model.enabled && media_model.hdd_admit && media_model.hdd_left!=0U) {
        --media_model.hdd_left;media_model.hdd_turn=false;request(0U,0U,false);
    }
}
static void queue_pram(bool writing)
{
    if(queued_pram) {
        /* The adapter holds a pending boolean, not a queue of pulse IDs. A
         * second same-direction pulse cannot create another hidden request. */
        assert(media_model.enabled && queued_pram_write==writing);
        ++media_model.coalesced_pram_pulses;return;
    }
    queued_pram=true;queued_pram_write=writing;
    queued_pram_lba=(config_stream[0]&1U)!=0U ? 0U:1U;
    /* A pending adapter PRAM request can be hidden behind HDD admissions for
     * longer than the drain's sixteen idle samples. */
    queued_pram_hdd_delay=media_model.enabled ? media_model.pram_admission_delay:0U;
    post_queued_request();
}
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
static uint8_t sparse_byte(uint64_t offset)
{ return (uint8_t)(UINT64_C(0xa7)^offset^(offset>>16U)^(offset>>32U)); }
mt_sd_result_t mt_sd_storage_journal_recover(const char *p,const char *j,uint8_t *b,size_t n)
{ (void)p;(void)j;(void)b;(void)n;++recovery_calls;return result(MT_SD_OK,0U); }
mt_sd_result_t mt_sd_storage_journal_replace(const char *p,const char *j,const uint8_t *b,size_t n)
{
    (void)j; if(fail_journal || (fail_settings_journal && strstr(p,"gs-settings.bin")!=NULL))return result(MT_SD_WRITE_FAILED,0U);
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
mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *p,uint8_t *b,size_t n,bool *found)
{
    test_file_t *f=file_find(p,false);*found=f!=NULL;
    if(f==NULL)return result(MT_SD_OK,0U);
    if(f->size>n)return result(MT_SD_TOO_LARGE,0U);
    memcpy(b,f->data,f->size);return result(MT_SD_OK,f->size);
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
    ++file_open_calls;
    if(deny_writes && strchr(mode,'+')!=NULL)return result(MT_SD_OPEN_FAILED,0U);
    test_file_t *f=file_find(p,false); if(f==NULL)return result(MT_SD_OPEN_FAILED,0U);
    for(unsigned i=0U;i<FILES;++i)if(!handles[i].open) { handles[i]=(struct mt_sd_file){f,0U,true}; *out=&handles[i]; return result(MT_SD_OK,0U); }
    return result(MT_SD_FILE_LIMIT,0U);
}
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *h,void *b,size_t n)
{
    assert(h->open);if(n>h->file->size-h->position)n=(size_t)(h->file->size-h->position);
    last_read_offset=h->position;
    if(h->file->sparse && h->position>=512U) {
        for(size_t i=0U;i<n;++i)((uint8_t *)b)[i]=sparse_byte(h->position+i);
        if(h->file->sparse_written && h->position==h->file->sparse_offset) {
            assert(n<=512U);memcpy(b,h->file->sparse_data,n);
        }
    } else { assert(h->position+n<=sizeof(h->file->data));memcpy(b,h->file->data+h->position,n); }
    h->position+=n;return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *h,const void *b,size_t n)
{
    assert(h->open && h->position+n<=h->file->size);
    assert(journal_durable); ++disk_writes; ++events;
    last_write_offset=h->position;
    uint8_t *dst;
    if(h->file->sparse) {
        assert(n<=512U);
        if(!h->file->sparse_written || h->file->sparse_offset!=h->position)
            for(size_t i=0U;i<512U;++i)h->file->sparse_data[i]=sparse_byte(h->position+i);
        h->file->sparse_written=true;h->file->sparse_offset=h->position;dst=h->file->sparse_data;
    } else { assert(h->position+n<=sizeof(h->file->data));dst=h->file->data+h->position; }
    if(fail_disk_write) { memcpy(dst,b,n/2U);return result(MT_SD_WRITE_FAILED,n/2U); }
    memcpy(dst,b,n);h->position+=n;return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *h,int64_t offset,int origin)
{
    int64_t p=origin==SEEK_END ? (int64_t)h->file->size:origin==SEEK_CUR ? (int64_t)h->position:0;
    p+=offset; assert(p>=0 && (uint64_t)p<=h->file->size); h->position=(uint64_t)p;return result(MT_SD_OK,0U);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *h) { return (int64_t)h->position; }
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *h)
{
    assert(h->open);
    if(media_model.enabled && h->file==media_model.target) {
        assert(!media_model.eject_wait && media_model.eject_delay==0U && !eject_pending_test);
        assert((dirty_drives&4U)==0U && media_model.flush_index==media_model.flush_blocks);
        assert((regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)==0U || request_slot()!=2U);
        ++media_model.target_closes;
    }
    h->open=false;close_event=++events;return result(MT_SD_OK,0U);
}
mt_sd_result_t mt_sd_file_remove(const char *p)
{ test_file_t *f=file_find(p,false);if(f!=NULL)f->present=false;return result(MT_SD_OK,0U); }
const char *mt_sd_status_name(mt_sd_status_t s)
{ return s==MT_SD_OPEN_FAILED ? "SD open failed":s==MT_SD_SIZE_MISMATCH ? "SD size mismatch":"simulated SD failure"; }
static uint32_t read_reg(void *c,uint32_t o)
{
    (void)c;
    if(o==MT_REG_MEDIA_STATE) {
        if(media_model.enabled && media_model.eject_wait && media_model.eject_delay!=0U &&
            --media_model.eject_delay==0U) {
            eject_pending_test=true;post_queued_request();
        }
        return (dirty_drives ? MT_MEDIA_STATE_DIRTY:0U)|(eject_pending_test ? MT_MEDIA_STATE_EJECT_PENDING:0U);
    }
    if(o==MT_REG_MEDIA_DATA) {
        if(reverse_index>=512U)return 0U;
        const unsigned i=reverse_index++;
        return MT_MEDIA_DATA_VALID|((uint32_t)(reverse_data==NULL ? (uint8_t)(i+0x31U):reverse_data[i])<<MT_MEDIA_DATA_BYTE_SHIFT)|
            ((i==511U || (early_last && i==10U)) ? MT_MEDIA_DATA_LAST:0U);
    }
    return regs[o/4U];
}
static void commit_config(void)
{
    assert(stream_index==sizeof(config_stream) && !config_committed);
    const uint8_t next_fast=config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY;
    if(next_fast!=applied_fast_floppy) {
        assert(core_reset_asserted && dirty_drives==0U && !eject_pending_test &&
            (regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)==0U);
        for(unsigned i=0U;i<FILES;++i)assert(!handles[i].open);
    }
    applied_fast_floppy=next_fast;
    config_committed=true;config_event=++events;
    if((config_stream[8]&16U)!=0U) {
        assert((config_stream[8]&2U)==0U && !pram_restore_pending);
        ++pram_load_packets;pram_restore_pending=true;queue_pram(false);
    }
    if((config_stream[8]&8U)!=0U) { ++pram_save_pulses;queue_pram(true); }
    if((config_stream[8]&2U)!=0U) {
        assert(!pram_restore_pending && (regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)==0U);
        ++warm_reset_packets;warm_reset_event=++events;
    }
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
        /* The production loader commits a config on its final payload byte,
         * before the later STREAM_END command can report a transport error. */
        if(stream_kind==MT_LOAD_IIGS_CONFIG && stream_index==total && !config_committed)commit_config();
    }
    if(o==MT_REG_COMMAND_DOORBELL) {
        const uint32_t a=regs[MT_REG_COMMAND_ARG0/4U], b=regs[MT_REG_COMMAND_ARG1/4U], d=regs[MT_REG_COMMAND_ARG3/4U];
        uint32_t response=MT_RESPONSE_OK;
        switch(regs[MT_REG_COMMAND_OPCODE/4U]) {
        case MT_COMMAND_CORE_RESET:
            if(a!=0U) {
                /* A machine reset is forbidden until all durable media work
                 * is complete and every ARM disk handle has been closed. */
                assert(dirty_drives==0U && !eject_pending_test &&
                    (regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)==0U);
                for(unsigned i=0U;i<FILES;++i)assert(!handles[i].open);
                ++reset_assertions;reset_event=++events;core_reset_asserted=true;
            } else {
                assert(core_reset_asserted && config_event>reset_event);
                ++reset_releases;release_event=++events;core_reset_asserted=false;
            }
            break;
        case MT_COMMAND_MEDIA_MOUNT:
            assert((a&255U)<MT_IIGS_MEDIA_SLOT_COUNT && d==0U);
            generations[a&255U]=regs[MT_REG_COMMAND_ARG2/4U];
            mount_kinds[a&255U]=(uint8_t)(a>>MT_MEDIA_KIND_SHIFT);
            mount_sizes[a&255U]=((a>>MT_MEDIA_KIND_SHIFT)&255U)==MT_IIGS_MEDIA_BLOCK32 ? (uint64_t)b*512U:b;
            mount_event[a&255U]=++events;break;
        case MT_COMMAND_MEDIA_EJECT:
            if(media_model.enabled) {
                if(media_model.legacy_eject_cancels_read &&
                    (regs[MT_REG_MEDIA_REQUEST/4U]&(MT_MEDIA_REQUEST_VALID|MT_MEDIA_REQUEST_WRITE))==MT_MEDIA_REQUEST_VALID) {
                    /* Old shell cleared the snapshot while the adapter kept
                     * its source request pending: no new request can arrive. */
                    regs[MT_REG_MEDIA_REQUEST/4U]=0U;media_model.orphaned_read=true;
                }
                if(a==0U)media_model.hdd_admit=false;
                if(a==2U) {
                    ++media_model.ejects;media_model.eject_wait=true;media_model.eject_delay=3U;
                    break;
                }
            }
            dirty_drives&=(uint16_t)~(1U<<(a&255U));
            if(a==4U && pram_eject_write) {
                regs[MT_REG_MEDIA_REQUEST/4U]=MT_MEDIA_REQUEST_VALID|MT_MEDIA_REQUEST_WRITE;
                regs[MT_REG_MEDIA_REQUEST_ID/4U]=0xeeeeU;regs[MT_REG_MEDIA_REQUEST_LBA/4U]=0U;
                regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]=generations[4];
                regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]=0x402U;reverse_index=0U;eject_pending_test=true;
            }
            break;
        case MT_COMMAND_STREAM_BEGIN:
            if(a==MT_LOAD_IIGS_HDD_ROM && reject_smartport_rom) { response=MT_RESPONSE_UNSUPPORTED;break; }
            if(a==MT_LOAD_IIGS_CONFIG && fail_config_begin) { response=MT_RESPONSE_IO_ERROR;break; }
            stream_kind=(uint8_t)a;stream_index=0U;config_committed=false;
            if(stream_kind==MT_LOAD_IIGS_SECTOR)++sector_streams;
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
                assert(stream_index==sizeof(config_stream) && config_committed);
                if(fail_config_end_once) { fail_config_end_once=false;response=MT_RESPONSE_IO_ERROR; }
            }
            break;
        case MT_COMMAND_MEDIA_COMPLETE: {
            const unsigned completed_slot=request_slot();
            if(media_model.enabled)++media_model.completion_attempts;
            if(media_model.enabled && completed_slot==2U && media_model.fail_target_completion) {
                response=MT_RESPONSE_IO_ERROR;break;
            }
            if(media_model.enabled && completed_slot==MT_IIGS_PRAM_SLOT && media_model.fail_pram_completion) {
                response=MT_RESPONSE_IO_ERROR;break;
            }
            if(fail_media_complete) { response=MT_RESPONSE_IO_ERROR;break; }
            /* Match multitini_control_regs: SmartPort result bytes are not
             * legal shell completion statuses, and errors carry no blocks. */
            if((regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)==0U ||
                (regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]&UINT32_C(0xffff))==0U ||
                a!=regs[MT_REG_MEDIA_REQUEST_ID/4U] ||
                (b!=MT_RESPONSE_OK && b!=MT_RESPONSE_INVALID_ARGUMENT &&
                    b!=MT_RESPONSE_IO_ERROR && b!=MT_RESPONSE_TIMEOUT) ||
                (b==MT_RESPONSE_OK && regs[MT_REG_COMMAND_ARG2/4U]!=regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]) ||
                (b!=MT_RESPONSE_OK && regs[MT_REG_COMMAND_ARG2/4U]!=0U) ||
                d!=regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]) {
                response=MT_RESPONSE_INVALID_ARGUMENT;break;
            }
            ++completions;completion_status=b;
            if(b==MT_RESPONSE_OK) {
                assert(regs[MT_REG_COMMAND_ARG2/4U]==regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]);
                if(pram_restore_pending && completed_slot==MT_IIGS_PRAM_SLOT) {
                    assert(regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]==0x402U && stream_kind==MT_LOAD_IIGS_SECTOR && stream_index==512U);
                    pram_restore_pending=false;pram_load_complete_event=++events;
                }
                regs[MT_REG_MEDIA_REQUEST/4U]=0U;
                if(!media_model.enabled)eject_pending_test=false;
                else if(completed_slot==0U) {
                    ++media_model.hdd_completions;
                    if(queued_pram_hdd_delay!=0U)--queued_pram_hdd_delay;
                } else if(completed_slot==MT_IIGS_PRAM_SLOT) {
                    ++media_model.pram_completions;
                    if(media_model.prior_pram_eject_pending) {
                        media_model.prior_pram_eject_pending=false;eject_pending_test=false;
                    }
                }
                else if(completed_slot==2U && media_model.eject_wait) {
                    ++media_model.target_completions;++media_model.flush_index;media_model.hdd_turn=true;
                    if(media_model.flush_index==media_model.flush_blocks) {
                        media_model.eject_wait=false;eject_pending_test=false;dirty_drives&=(uint16_t)~4U;
                    }
                }
            }
            else {
                assert(regs[MT_REG_COMMAND_ARG2/4U]==0U);
                const unsigned slot=(regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]>>8U)&255U;
                if(mt_iigs_drive_hdd_unit(mt_iigs_wire_slot_drive(slot))<MT_IIGS_HDD_COUNT)
                    regs[MT_REG_MEDIA_REQUEST/4U]=0U; /* HDD error returns to guest. */
                else if(media_model.enabled) {
                    /* Current adapter retains the buffer but retries with a
                     * fresh ID and reverse stream, never the old seen ID. */
                    request(slot,regs[MT_REG_MEDIA_REQUEST_LBA/4U],
                        (regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_WRITE)!=0U);
                }
            }
            assert(d==regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]);post_queued_request();break;
        }
        default: break;
        }
        regs[MT_REG_RESPONSE_SEQ/4U]=v;regs[MT_REG_RESPONSE_STATUS/4U]=response;
    }
}
static mt_core_link_t core;
static void reset(void)
{
    progress_calls=sector_streams=file_open_calls=0U;fail_media_complete=false;
    fail_config_begin=fail_config_end_once=config_committed=false;pram_save_pulses=0U;applied_fast_floppy=0U;
    memset(&media_model,0,sizeof(media_model));queued_pram=queued_pram_write=false;
    queued_pram_lba=queued_pram_hdd_delay=0U;
    reject_smartport_rom=false;last_read_offset=last_write_offset=0U;
    memset(files,0,sizeof(files));memset(handles,0,sizeof(handles));memset(regs,0,sizeof(regs));
    memset(generations,0,sizeof(generations));memset(mount_sizes,0,sizeof(mount_sizes));
    memset(mount_kinds,0,sizeof(mount_kinds));
    fail_disk_write=fail_journal=fail_settings_journal=deny_writes=early_last=pram_eject_write=eject_pending_test=false;
    pram_restore_pending=fail_sector_transfer=false;
    failed_rom=-1;rom_failure_status=MT_SD_OPEN_FAILED;rom_read_count=rom_stream_count=0U;
    pram_load_packets=warm_reset_packets=pram_load_complete_event=warm_reset_event=recovery_calls=0U;
    journal_writes=disk_writes=events=completions=0U; journal_durable=false;dirty_drives=0U;
    reset_assertions=reset_releases=reset_event=release_event=config_event=close_event=0U;
    core_reset_asserted=false;memset(mount_event,0,sizeof(mount_event));
    raw_snapshots=0U;reverse_data=NULL;
    regs[MT_REG_ABI_VERSION/4U]=MT_ABI_VERSION;
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS;
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
static void test_media_progress_and_completion_retry(void)
{
    reset();test_file_t *f=disk("progress.hdv");
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,count_progress,&progress_calls);
    assert(iigs_content_service_mount(0U,"progress.hdv")==0);
    progress_calls=0U;
    request(0U,1U,false);iigs_content_service_poll();
    assert(completions==1U && completion_status==MT_RESPONSE_OK && sector_streams==1U);
    assert(memcmp(stream_data,f->data+512U,512U)==0 && progress_calls==3U);
    /* Retry only the completion: the already transferred sector is not sent twice. */
    request(0U,1U,false);fail_media_complete=true;iigs_content_service_poll();
    assert(completions==1U && sector_streams==2U);
    fail_media_complete=false;iigs_content_service_poll();
    assert(completions==2U && completion_status==MT_RESPONSE_OK && sector_streams==2U);
    const unsigned before=progress_calls;
    request(0U,2U,true);iigs_content_service_poll();
    assert(completions==3U && completion_status==MT_RESPONSE_OK && disk_writes==1U);
    assert(progress_calls>=before+512U); /* Reverse-stream ticks retain the caller's context. */
    request(0U,9U,false);iigs_content_service_poll();
    assert(completions==4U && completion_status==MT_RESPONSE_IO_ERROR && sector_streams==2U);
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
    reset();regs[MT_REG_IIGS_ACCEL_CAPS/4U]=0U;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_speed_mask()==MT_IIGS_ACCEL_LEGACY_SPEED_MASK);
    mt_iigs_config_t c=*iigs_content_service_config();c.speed=6U;
    assert(iigs_content_service_configure(&c)!=0);
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS;
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
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS;
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
    fail_disk_write=false;assert(iigs_content_service_retry()==0 && !iigs_content_service_faulted());
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
static void start_accelerated(void)
{
    reset();regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_boot()==0 && reset_assertions==1U && reset_releases==1U);
    mt_iigs_config_t c=*iigs_content_service_config();c.speed=6U;c.flags=UINT8_C(0x05);
    assert(iigs_content_service_configure(&c)==0);
    assert(reset_assertions==1U && config_stream[1]==6U && config_stream[2]==0U);
}
static void test_card_mode_lifecycle(void)
{
    start_accelerated();test_file_t *image=disk("System/boot.hdv");
    assert(iigs_content_service_mount(0U,"System/boot.hdv")==0);
    assert(iigs_content_service_set_mouse_sensitivity(75U)==0);
    /* Pending writes and motor-on dirty state must flush before the full
     * reset that clears all software-controlled ZipGS state. */
    request(0U,1U,true);dirty_drives=1U;pram_eject_write=true;
    mt_iigs_config_t c=*iigs_content_service_config();c.card=2U;
    assert(iigs_content_service_configure(&c)==0);
    assert(reset_assertions==2U && reset_releases==2U && !core_reset_asserted);
    assert(close_event<reset_event && config_event>reset_event &&
        mount_event[0]>config_event && mount_event[4]>config_event && release_event>mount_event[0]);
    assert(config_stream[1]==0U && config_stream[2]==2U && rom_stream_count==8U);
    assert(iigs_content_service_config()->card==2U && iigs_content_service_config()->speed==0U);
    assert(iigs_content_service_config()->flags==UINT8_C(0x05) && iigs_content_service_mouse_sensitivity()==75U);
    assert(strcmp(iigs_content_service_name(0U),"System/boot.hdv")==0 && image->data[512U]==0x31U);
    assert(file_find(TEST_MEDIA_ROOT "/System/boot.hdv.gsj",false)==NULL && dirty_drives==0U);
    assert(file_find(TEST_MEDIA_ROOT "/pram.bin",false)!=NULL);
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);
    assert(saved!=NULL && saved->data[9]==0U && saved->data[10]==2U);
    /* A direct caller cannot leave turbo selected while the card is None. */
    c=*iigs_content_service_config();c.speed=6U;
    assert(iigs_content_service_configure(&c)==0 && reset_assertions==2U && config_stream[1]==0U);
    /* Returning to ZipGS also resets, even if the requested speed is stock.
     * Only subsequent speed changes remain live. */
    c=*iigs_content_service_config();c.card=0U;
    assert(iigs_content_service_configure(&c)==0 && reset_assertions==3U && reset_releases==3U);
    assert(config_stream[1]==0U && config_stream[2]==0U);
    c=*iigs_content_service_config();c.speed=6U;
    assert(iigs_content_service_configure(&c)==0 && reset_assertions==3U && config_stream[1]==6U);
    c.speed=4U;assert(iigs_content_service_configure(&c)==0 && reset_assertions==3U && config_stream[1]==4U);
}
static void test_fast_floppy_lifecycle(void)
{
    reset();regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS&~MT_IIGS_ACCEL_CAPS_VALID;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(!iigs_content_service_fast_floppy_supported());
    mt_iigs_config_t unsupported=*iigs_content_service_config();unsupported.flags|=MT_IIGS_FLAG_FAST_FLOPPY;
    assert(iigs_content_service_configure(&unsupported)!=0 && events==0U);
    start_accelerated();assert(iigs_content_service_fast_floppy_supported());
    assert((iigs_content_service_config()->flags&MT_IIGS_FLAG_FAST_FLOPPY)==0U);
    test_file_t *image=disk("fast.hdv");
    assert(iigs_content_service_mount(0U,"fast.hdv")==0);
    request(0U,1U,true);dirty_drives=1U;pram_eject_write=true;
    mt_iigs_config_t c=*iigs_content_service_config();c.flags|=MT_IIGS_FLAG_FAST_FLOPPY;
    /* commit_config asserts that every fast-bit transition is made only with
     * reset asserted, no pending/dirty request and no open disk handles. */
    assert(iigs_content_service_configure(&c)==0 && reset_assertions==2U && reset_releases==2U);
    assert(image->data[512U]==0x31U && dirty_drives==0U &&
        (config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);
    assert(saved!=NULL && (saved->data[11]&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    assert(strcmp(iigs_content_service_name(0U),"fast.hdv")==0);
    assert(iigs_content_service_shutdown()==0);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert((iigs_content_service_config()->flags&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    assert(iigs_content_service_boot()==0 && (config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    /* Older safe-media cores retain the saved preference but never receive
     * their formerly reserved bit, including on live unrelated changes. */
    assert(iigs_content_service_shutdown()==0);
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS&~MT_IIGS_FAST_FLOPPY_SUPPORTED;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(!iigs_content_service_fast_floppy_supported() &&
        (iigs_content_service_config()->flags&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    assert(iigs_content_service_boot()==0 && (config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY)==0U);
    c=*iigs_content_service_config();c.speed=4U;
    assert(iigs_content_service_configure(&c)==0 && (config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY)==0U &&
        (saved->data[11]&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    const unsigned before=events;c.flags&=(uint8_t)~MT_IIGS_FLAG_FAST_FLOPPY;
    assert(iigs_content_service_configure(&c)!=0 && events==before &&
        (iigs_content_service_config()->flags&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    assert(iigs_content_service_shutdown()==0);
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_boot()==0 && (config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY)!=0U);
    c=*iigs_content_service_config();c.flags&=(uint8_t)~MT_IIGS_FLAG_FAST_FLOPPY;
    assert(iigs_content_service_configure(&c)==0 && (config_stream[3]&MT_IIGS_FLAG_FAST_FLOPPY)==0U &&
        (saved->data[11]&MT_IIGS_FLAG_FAST_FLOPPY)==0U);

    start_accelerated();(void)disk("dirty.hdv");assert(iigs_content_service_mount(0U,"dirty.hdv")==0);
    request(0U,1U,true);dirty_drives=1U;fail_disk_write=true;
    c=*iigs_content_service_config();c.flags|=MT_IIGS_FLAG_FAST_FLOPPY;
    assert(iigs_content_service_configure(&c)!=0 && reset_assertions==1U && applied_fast_floppy==0U);
    assert((iigs_content_service_config()->flags&MT_IIGS_FLAG_FAST_FLOPPY)==0U &&
        file_find(TEST_MEDIA_ROOT "/dirty.hdv.gsj",false)!=NULL);

    start_accelerated();(void)disk("rollback.hdv");assert(iigs_content_service_mount(0U,"rollback.hdv")==0);
    fail_settings_journal=true;c=*iigs_content_service_config();c.flags|=MT_IIGS_FLAG_FAST_FLOPPY;
    assert(iigs_content_service_configure(&c)!=0 && reset_assertions==3U && reset_releases==3U &&
        (iigs_content_service_config()->flags&MT_IIGS_FLAG_FAST_FLOPPY)==0U && applied_fast_floppy==0U);
    assert(iigs_content_service_ready() && iigs_content_service_faulted() &&
        strcmp(iigs_content_service_error(),"simulated SD failure")==0);
}
static void test_legacy_none_and_card_rollback(void)
{
    start_accelerated();
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);
    assert(saved!=NULL);saved->data[10]=2U;settings_checksum(saved);
    /* Old None+turbo records remain valid, but load as stock without an
     * unsolicited settings rewrite. Boot must transmit only stock. */
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_config()->card==2U && iigs_content_service_config()->speed==0U);
    assert(saved->data[9]==6U && saved->data[10]==2U);
    assert(iigs_content_service_boot()==0 && config_stream[1]==0U && config_stream[2]==2U);
    assert(iigs_content_service_set_mouse_sensitivity(200U)==0 && saved->data[9]==0U);
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=MT_IIGS_ACCEL_CAPS_VALID|MT_IIGS_MEDIA_TARGET_DRAIN_SUPPORTED|UINT32_C(1);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    mt_iigs_config_t c=*iigs_content_service_config();c.speed=6U;
    assert(iigs_content_service_configure(&c)==0 && config_stream[1]==0U);
    c.speed=7U;assert(iigs_content_service_configure(&c)!=0); /* Invalid wire values still fail. */

    start_accelerated();(void)disk("boot.hdv");assert(iigs_content_service_mount(0U,"boot.hdv")==0);
    saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);assert(saved!=NULL);
    /* Fail settings persistence after reset, allowing the preceding durable
     * PRAM save to complete before testing rollback. */
    fail_settings_journal=true;c=*iigs_content_service_config();c.card=2U;
    assert(iigs_content_service_configure(&c)!=0);
    assert(reset_assertions==3U && reset_releases==3U && !core_reset_asserted);
    assert(iigs_content_service_ready() && iigs_content_service_faulted());
    assert(strcmp(iigs_content_service_error(),"simulated SD failure")==0);
    assert(iigs_content_service_config()->card==0U && iigs_content_service_config()->speed==6U);
    assert(config_stream[1]==6U && config_stream[2]==0U && saved->data[9]==6U && saved->data[10]==0U);
    assert(strcmp(iigs_content_service_name(0U),"boot.hdv")==0);
}
static void test_card_restart_failures(void)
{
    start_accelerated();(void)disk("busy.hdv");assert(iigs_content_service_mount(0U,"busy.hdv")==0);
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);assert(saved!=NULL);
    request(0U,1U,true);dirty_drives=1U;fail_disk_write=true;
    mt_iigs_config_t c=*iigs_content_service_config();c.card=2U;
    assert(iigs_content_service_configure(&c)!=0);
    /* A failed durable write must never be bypassed by the mode reset. */
    assert(reset_assertions==1U && reset_releases==1U && !core_reset_asserted);
    assert(iigs_content_service_config()->card==0U && saved->data[9]==6U && saved->data[10]==0U);
    assert(file_find(TEST_MEDIA_ROOT "/busy.hdv.gsj",false)!=NULL);
    assert(strcmp(iigs_content_service_name(0U),"busy.hdv")==0);
    assert(strstr(iigs_content_service_error(),"rollback failed")!=NULL);
    fail_disk_write=false;assert(iigs_content_service_retry()==0);
    c=*iigs_content_service_config();c.card=2U;
    assert(iigs_content_service_configure(&c)==0 && reset_assertions==2U && reset_releases==2U);

    start_accelerated();saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);assert(saved!=NULL);
    failed_rom=ROM3_FILE;c=*iigs_content_service_config();c.card=2U;
    assert(iigs_content_service_configure(&c)!=0);
    assert(core_reset_asserted && !iigs_content_service_ready() && iigs_content_service_faulted());
    assert(reset_assertions==3U && reset_releases==1U && saved->data[10]==0U);
    assert(iigs_content_service_config()->card==0U && iigs_content_service_config()->speed==6U);
    assert(strstr(iigs_content_service_error(),"rollback failed")!=NULL);
    failed_rom=-1;assert(iigs_content_service_boot()==0 && !core_reset_asserted);
    assert(config_stream[1]==6U && config_stream[2]==0U && iigs_content_service_ready());
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
    regs[MT_REG_IIGS_ACCEL_CAPS/4U]=TEST_IIGS_CAPS;
    regs[MT_REG_STREAM_STATUS/4U]=MT_STREAM_STATUS_READY|MT_STREAM_STATUS_DRAINED;
    regs[MT_REG_STATUS/4U]=MT_STATUS_CORE_READY;
    fail_disk_write=fail_journal=false;
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
}
static void assert_request_rejected(uint32_t status,const char *message)
{
    const unsigned completed_before=completions,streams_before=sector_streams;
    const unsigned writes_before=disk_writes,journals_before=journal_writes,snapshots_before=raw_snapshots;
    const uint32_t id=regs[MT_REG_MEDIA_REQUEST_ID/4U],gen=regs[MT_REG_MEDIA_REQUEST_GENERATION/4U];
    const uint32_t descriptor=regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U],lba=regs[MT_REG_MEDIA_REQUEST_LBA/4U];
    iigs_content_service_poll();
    assert(iigs_content_service_faulted() && strcmp(iigs_content_service_error(),message)==0);
    assert(completions==completed_before+1U && completion_status==status);
    assert(regs[MT_REG_COMMAND_ARG0/4U]==id && regs[MT_REG_COMMAND_ARG2/4U]==0U &&
        regs[MT_REG_COMMAND_ARG3/4U]==gen);
    assert(sector_streams==streams_before && reverse_index==0U && disk_writes==writes_before &&
        journal_writes==journals_before && raw_snapshots==snapshots_before);
    /* This model retains a rejected floppy/PRAM snapshot. Rejection must not
     * alter its identity or consume the retained write stream. A fresh adapter
     * ID, tested separately, is required before ordinary polling retries it. */
    assert((regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)!=0U &&
        regs[MT_REG_MEDIA_REQUEST_ID/4U]==id && regs[MT_REG_MEDIA_REQUEST_GENERATION/4U]==gen &&
        regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]==descriptor && regs[MT_REG_MEDIA_REQUEST_LBA/4U]==lba);
    iigs_content_service_poll();assert(completions==completed_before+1U);
}
static void test_media_request_rejection_errors(void)
{
    reset();(void)raw_disk("request.po",MT_IIGS_RAW_PRODOS35,0U);
    assert(iigs_content_service_mount(2U,"request.po")==0 && mount_sizes[2]==3203U*512U);
    /* Live-board failure: a corrupt track pointer looked like the WOZ header's
     * first little-endian word. Report the range error without serving bytes. */
    request(2U,UINT32_C(0x4f57),false);
    assert_request_rejected(MT_RESPONSE_IO_ERROR,"Apple IIgs disk request exceeds image");
    assert(strcmp(iigs_content_service_name(2U),"request.po")==0);
    request(2U,0U,false);++regs[MT_REG_MEDIA_REQUEST_GENERATION/4U];
    assert_request_rejected(MT_RESPONSE_INVALID_ARGUMENT,"Apple IIgs media request slot or generation invalid");
    request(2U,0U,false);regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]=UINT32_C(0xff02);
    assert_request_rejected(MT_RESPONSE_INVALID_ARGUMENT,"Apple IIgs media request slot or generation invalid");
    const uint32_t malformed[]={UINT32_C(0x0201),UINT32_C(0x0203),UINT32_C(0x10202)};
    for(unsigned i=0U;i<sizeof(malformed)/sizeof(malformed[0]);++i) {
        request(2U,0U,false);regs[MT_REG_MEDIA_REQUEST_BLOCK_COUNT/4U]=malformed[i];
        assert_request_rejected(MT_RESPONSE_INVALID_ARGUMENT,"Apple IIgs media request descriptor invalid");
    }
    request(2U,0U,false);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK && memcmp(stream_data,"WOZ2",4U)==0);
    assert(iigs_content_service_retry()==0 && !iigs_content_service_faulted() &&
        iigs_content_service_error()[0]=='\0' && strcmp(iigs_content_service_name(2U),"request.po")==0);

    reset();(void)raw_disk("readonly.po",MT_IIGS_RAW_PRODOS35,0U);deny_writes=true;
    assert(iigs_content_service_mount(2U,"readonly.po")==0);
    request(2U,3U,true);
    assert_request_rejected(MT_RESPONSE_IO_ERROR,"Apple IIgs disk is read-only");

    reset();assert(iigs_content_service_boot()==0);
    request(MT_IIGS_PRAM_SLOT,2U,false);
    assert_request_rejected(MT_RESPONSE_INVALID_ARGUMENT,"Apple IIgs PRAM request exceeds image");
}
static uint32_t metadata_le32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8U|(uint32_t)p[2]<<16U|(uint32_t)p[3]<<24U; }
static void test_raw_metadata_transport(void)
{
    const uint8_t formats[]={MT_IIGS_RAW_DOS525,MT_IIGS_RAW_PRODOS35,MT_IIGS_RAW_PRODOS35};
    const char *const names[]={"metadata.dsk","metadata.po","metadata.2mg"};
    for(unsigned test=0U;test<3U;++test) {
        const uint8_t format=formats[test];
        const unsigned slot=format==MT_IIGS_RAW_DOS525 ? 3U:2U;
        const unsigned tracks=format==MT_IIGS_RAW_DOS525 ? 35U:160U;
        const unsigned slots=format==MT_IIGS_RAW_DOS525 ? 13U:20U;
        reset();(void)raw_disk(names[test],format,test==2U ? 128U:0U);
        assert(iigs_content_service_mount(slot,names[test])==0);
        uint8_t delivered[MT_IIGS_FLOPPY_HEADER_BYTES];
        for(unsigned block=0U;block<3U;++block) {
            const unsigned streams_before=sector_streams;
            request(slot,block,false);iigs_content_service_poll();
            assert(completion_status==MT_RESPONSE_OK && sector_streams==streams_before+1U);
            memcpy(delivered+block*512U,stream_data,512U);
        }
        assert(memcmp(delivered,"WOZ2\xff\x0a\x0d\x0a",8U)==0 &&
            memcmp(delivered+80U,"TMAP",4U)==0 && memcmp(delivered+248U,"TRKS",4U)==0);
        assert(metadata_le32(delivered+252U)==(3U+tracks*slots)*512U-256U);
        for(unsigned track=0U;track<160U;++track) {
            const uint8_t *entry=delivered+256U+track*8U;
            if(track>=tracks) {
                for(unsigned b=0U;b<8U;++b)assert(entry[b]==0U);
                continue;
            }
            const unsigned bits=format==MT_IIGS_RAW_DOS525 ? 52224U:1480U+(12U-track/32U)*6358U;
            const unsigned first_block=(unsigned)entry[0]|(unsigned)entry[1]<<8U;
            const unsigned block_count=(unsigned)entry[2]|(unsigned)entry[3]<<8U;
            assert(first_block==3U+track*slots && block_count==(bits+4095U)/4096U &&
                metadata_le32(entry+4U)==bits && first_block+block_count<=3U+tracks*slots);
            if(format==MT_IIGS_RAW_PRODOS35)assert(delivered[88U+track]==track);
            else assert(delivered[88U+track*4U]==track);
        }
    }
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
static void test_eight_hdd_and_legacy_settings(void)
{
    reset();assert(iigs_content_service_boot()==0);
    for(unsigned unit=0U;unit<MT_IIGS_HDD_COUNT;++unit) {
        char name[40];(void)snprintf(name,sizeof(name),"Hard disks/volume%u.hdv",unit+1U);
        test_file_t *image=disk(name);image->data[0]=(uint8_t)(0x80U+unit);
        const unsigned drive=mt_iigs_hdd_drive(unit),slot=mt_iigs_drive_wire_slot(drive);
        assert(iigs_content_service_mount(drive,name)==0);
        assert(generations[slot]!=0U && mount_sizes[slot]==2048U && mount_kinds[slot]==MT_IIGS_MEDIA_BLOCK32);
        request(slot,0U,false);iigs_content_service_poll();
        assert(completion_status==MT_RESPONSE_OK && stream_data[0]==(uint8_t)(0x80U+unit));
        request(slot,1U,true);iigs_content_service_poll();
        assert(completion_status==MT_RESPONSE_OK && image->data[512U]==0x31U && image->data[0]==(uint8_t)(0x80U+unit));
    }
    (void)raw_disk("Floppy/Sony.po",MT_IIGS_RAW_PRODOS35,0U);
    (void)raw_disk("Floppy/DOS.dsk",MT_IIGS_RAW_DOS525,0U);
    assert(iigs_content_service_mount(2U,"Floppy/Sony.po")==0);
    assert(iigs_content_service_mount(3U,"Floppy/DOS.dsk")==0);
    assert(mount_kinds[2]==MT_IIGS_MEDIA_WOZ && mount_kinds[3]==MT_IIGS_MEDIA_WOZ &&
        mount_kinds[MT_IIGS_PRAM_SLOT]==MT_IIGS_MEDIA_PRAM);
    /* The reserved PRAM wire slot never aliases saved index4 (hard disk3). */
    request(MT_IIGS_PRAM_SLOT,0U,true);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK);
    request(mt_iigs_drive_wire_slot(4U),0U,false);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK && stream_data[0]==0x82U);
    assert(iigs_content_service_set_mouse_sensitivity(75U)==0);
    assert(iigs_content_service_prepare_reset()==0);
    for(unsigned i=0U;i<FILES;++i)assert(!handles[i].open);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(iigs_content_service_boot()==0 && iigs_content_service_mouse_sensitivity()==75U);
    for(unsigned unit=0U;unit<MT_IIGS_HDD_COUNT;++unit) {
        char name[40];(void)snprintf(name,sizeof(name),"Hard disks/volume%u.hdv",unit+1U);
        assert(strcmp(iigs_content_service_name(mt_iigs_hdd_drive(unit)),name)==0);
    }
    assert(strcmp(iigs_content_service_name(2U),"Floppy/Sony.po")==0);
    assert(strcmp(iigs_content_service_name(3U),"Floppy/DOS.dsk")==0);
    /* Reconstruct an exact pre-extension MTGSSET1 record. Migration reads
     * its original length/CRC and retains the first four drive meanings. */
    assert(iigs_content_service_prepare_reset()==0);
    test_file_t *saved=file_find(TEST_MEDIA_ROOT "/gs-settings.bin",false);
    assert(saved!=NULL && saved->size==24U+MT_IIGS_DRIVE_COUNT*96U && memcmp(saved->data,"MTGSSET2",8U)==0);
    saved->size=24U+4U*96U;memcpy(saved->data,"MTGSSET1",8U);settings_checksum(saved);
    iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
    assert(!iigs_content_service_faulted() && iigs_content_service_boot()==0);
    assert(strcmp(iigs_content_service_name(0U),"Hard disks/volume1.hdv")==0);
    assert(strcmp(iigs_content_service_name(1U),"Hard disks/volume2.hdv")==0);
    assert(strcmp(iigs_content_service_name(2U),"Floppy/Sony.po")==0);
    assert(strcmp(iigs_content_service_name(3U),"Floppy/DOS.dsk")==0);
    for(unsigned drive=4U;drive<MT_IIGS_DRIVE_COUNT;++drive)assert(iigs_content_service_name(drive)==NULL);
    assert(iigs_content_service_mouse_sensitivity()==75U && saved->size==408U);
    assert(iigs_content_service_set_mouse_sensitivity(150U)==0);
    assert(saved->size==984U && memcmp(saved->data,"MTGSSET2",8U)==0);
    for(size_t i=408U;i<saved->size;++i)assert(saved->data[i]==0U);
}
static void test_wide_hdd_and_error_recovery(void)
{
    reset();test_file_t *image=disk("wide.2mg");image->sparse=true;
    const uint32_t blocks=UINT32_C(0x02000000),lba=UINT32_C(0x01000002);
    const uint64_t expected=(uint64_t)lba*512U+128U;
    image->size=(uint64_t)blocks*512U+128U;
    memset(image->data,0,512U);memcpy(image->data,"2IMG",4U);image->data[8]=64U;image->data[10]=1U;
    store32(image->data+12U,1U);store32(image->data+20U,blocks);store32(image->data+24U,128U);
    const unsigned drive=mt_iigs_hdd_drive(7U),slot=mt_iigs_drive_wire_slot(drive);
    assert(iigs_content_service_mount(drive,"wide.2mg")==0 && mount_sizes[slot]==(uint64_t)blocks*512U);
    request(slot,lba,false);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK && last_read_offset==expected);
    for(size_t i=0U;i<512U;++i)assert(stream_data[i]==sparse_byte(expected+i));
    request(slot,lba,true);fail_disk_write=true;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && last_write_offset==expected);
    assert(regs[MT_REG_MEDIA_REQUEST/4U]==0U && file_find(TEST_MEDIA_ROOT "/wide.2mg.gsj",false)!=NULL);
    /* The next read repairs the durable journal before observing any bytes. */
    fail_disk_write=false;request(slot,lba,false);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK && last_read_offset==expected && last_write_offset==expected);
    for(size_t i=0U;i<512U;++i)assert(stream_data[i]==(uint8_t)(i+0x31U));
    assert(file_find(TEST_MEDIA_ROOT "/wide.2mg.gsj",false)==NULL && memcmp(image->data,"2IMG",4U)==0);
    request(slot,lba,true);fail_disk_write=true;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && iigs_content_service_eject(drive)!=0);
    fail_disk_write=false;assert(iigs_content_service_eject(drive)==0);
    assert(file_find(TEST_MEDIA_ROOT "/wide.2mg.gsj",false)==NULL);
    request(slot,0U,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_INVALID_ARGUMENT);
    assert(iigs_content_service_mount(drive,"wide.2mg")==0);
    request(slot,blocks,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_IO_ERROR);
    request(slot,lba,true);--regs[MT_REG_MEDIA_REQUEST_GENERATION/4U];iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_INVALID_ARGUMENT && reverse_index==0U);
    /* Full 32-bit capacity remains representable without wrapping at4GiB. */
    assert(iigs_content_service_eject(drive)==0);
    image->size=MT_IIGS_MAX_HDD_BYTES+128U;store32(image->data+20U,UINT32_MAX);
    assert(iigs_content_service_mount(drive,"wide.2mg")==0 && mount_sizes[slot]==MT_IIGS_MAX_HDD_BYTES);
    request(slot,UINT32_MAX-1U,false);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK && last_read_offset==(uint64_t)(UINT32_MAX-1U)*512U+128U);
    request(slot,UINT32_MAX,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_IO_ERROR);
    assert(iigs_content_service_eject(drive)==0);
    deny_writes=true;assert(iigs_content_service_mount(drive,"wide.2mg")==0);
    request(slot,lba,true);iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && reverse_index==0U);
    reset();reject_smartport_rom=true;
    assert(iigs_content_service_boot()!=0 && !iigs_content_service_ready() && core_reset_asserted);
    assert(strcmp(iigs_content_service_error(),"Update IIgs core/ROM for SmartPort disks")==0);
    for(unsigned i=0U;i<MT_IIGS_MEDIA_SLOT_COUNT;++i)assert(generations[i]==0U);
    reset();failed_rom=HDD_FILE;rom_failure_status=MT_SD_SIZE_MISMATCH;
    assert(iigs_content_service_boot()!=0 && !iigs_content_service_ready() && core_reset_asserted);
    assert(strcmp(iigs_content_service_error(),"Update IIgs core/ROM for SmartPort disks")==0);
    for(unsigned i=0U;i<MT_IIGS_MEDIA_SLOT_COUNT;++i)assert(generations[i]==0U);
}
static test_file_t *start_busy_media_model(bool boot)
{
    reset();if(boot)assert(iigs_content_service_boot()==0);
    (void)disk("background.hdv");test_file_t *target=raw_disk("target.po",MT_IIGS_RAW_PRODOS35,0U);
    assert(iigs_content_service_mount(0U,"background.hdv")==0);
    assert(iigs_content_service_mount(2U,"target.po")==0);
    mt_iigs_floppy_track_t track;assert(mt_iigs_floppy_track(MT_IIGS_RAW_PRODOS35,0U,&track));
    raw_prepare(target,MT_IIGS_RAW_PRODOS35,0U,0U);
    media_model.enabled=true;media_model.target=target;media_model.hdd_admit=true;
    media_model.pram_admission_delay=32U;
    media_model.hdd_left=100010U;media_model.flush_data=encoded;
    media_model.flush_lba=track.first_block;media_model.flush_blocks=track.block_count;
    dirty_drives=4U;request(0U,0U,false);return target;
}
static void test_target_eject_amid_hdd_traffic(void)
{
    test_file_t *target=start_busy_media_model(false);
    const int result=iigs_content_service_eject(2U);
    if(result!=0) {
        printf("TARGET_EJECT failure commands=%u HDD_completions=%u error=%s\n",
            media_model.ejects,media_model.hdd_completions,iigs_content_service_error());fflush(stdout);
    }
    assert(result==0 && media_model.ejects==1U && media_model.target_closes==1U);
    assert(media_model.hdd_left!=0U && media_model.hdd_completions>16U);
    assert(media_model.target_completions==media_model.flush_blocks && !media_model.eject_wait);
    assert((regs[MT_REG_MEDIA_REQUEST/4U]&MT_MEDIA_REQUEST_VALID)!=0U && request_slot()==0U);
    assert(iigs_content_service_name(2U)==NULL && strcmp(iigs_content_service_name(0U),"background.hdv")==0);
    mt_iigs_floppy_track_t track;assert(mt_iigs_floppy_track(MT_IIGS_RAW_PRODOS35,0U,&track));
    assert(memcmp(target->data,raw_changed,track.raw_bytes)==0 && raw_snapshots==track.block_count);
    assert(file_find(TEST_MEDIA_ROOT "/target.po.gsraw",false)==NULL);
}
static void test_target_drain_failures_preserve_media(void)
{
    test_file_t *target=start_busy_media_model(false);media_model.busy_target=true;
    assert(iigs_content_service_eject(2U)!=0 && media_model.ejects==1U);
    assert(media_model.target_closes==0U && eject_pending_test && (dirty_drives&4U)!=0U);
    assert(iigs_content_service_name(2U)!=NULL && media_model.completion_attempts<=100000U);
    assert(memcmp(target->data,raw_source,MT_IIGS_FLOPPY_RAW_TRACK_BYTES)==0);

    target=start_busy_media_model(false);media_model.fail_target_completion=true;
    assert(iigs_content_service_eject(2U)!=0 && media_model.ejects==1U);
    assert(media_model.target_closes==0U && eject_pending_test && request_slot()==2U);
    assert(media_model.target_completions==0U && media_model.completion_attempts<=100000U);
    assert(raw_snapshots==1U && file_find(TEST_MEDIA_ROOT "/target.po.gsraw",false)!=NULL);
    assert(iigs_content_service_name(2U)!=NULL && memcmp(target->data,raw_source,MT_IIGS_FLOPPY_RAW_TRACK_BYTES)==0);

    (void)start_busy_media_model(false);media_model.legacy_eject_cancels_read=true;
    assert(iigs_content_service_eject(2U)!=0 && media_model.orphaned_read);
    assert(media_model.target_closes==0U && eject_pending_test && media_model.target_completions==0U);
    /* The old shared shell can orphan an unrelated read. The ARM service
     * must leave the target open; the corrected shell is tested in HDL. */
    assert(iigs_content_service_name(2U)!=NULL && (dirty_drives&4U)!=0U);
}
static void test_pram_and_reset_amid_hdd_traffic(void)
{
    (void)start_busy_media_model(true);
    assert(iigs_content_service_pram(true)==0);
    assert(media_model.pram_completions==1U && media_model.hdd_completions>=32U && media_model.hdd_left!=0U);
    assert(file_find(TEST_MEDIA_ROOT "/pram.bin",false)!=NULL && !queued_pram);
    assert(iigs_content_service_prepare_reset()==0);
    /* prepare_reset must require its own new PRAM completion, then finish
     * target media work rather than consume the background HDD burst. */
    assert(media_model.pram_completions==2U && media_model.hdd_left!=0U);
    assert(media_model.target_closes==1U && media_model.target_completions==media_model.flush_blocks);
    assert(!eject_pending_test && dirty_drives==0U);
    for(unsigned i=0U;i<FILES;++i)assert(!handles[i].open);

    (void)start_busy_media_model(true);request(MT_IIGS_PRAM_SLOT,0U,true);
    assert(iigs_content_service_pram(true)==0);
    /* An old visible save must not satisfy a newly issued same-direction
     * pulse while that new request is hidden behind background HDD work. */
    assert(media_model.pram_completions==2U && !queued_pram && media_model.hdd_left!=0U);

    (void)start_busy_media_model(true);media_model.hdd_left=200100U;
    media_model.pram_admission_delay=100050U;
    assert(iigs_content_service_pram(true)!=0 && queued_pram && media_model.pram_completions==0U);
    media_model.pram_admission_delay=32U;
    assert(iigs_content_service_pram(true)==0);
    /* Retire an accepted save that timed out before publishing any slot4
     * snapshot. Its later completion must not satisfy the next save pulse. */
    assert(media_model.pram_completions==2U && !queued_pram && media_model.coalesced_pram_pulses==0U);

    (void)start_busy_media_model(true);
    queued_pram=true;queued_pram_write=true;queued_pram_lba=0U;queued_pram_hdd_delay=32U;
    media_model.prior_pram_eject_pending=true;eject_pending_test=true;
    assert(iigs_content_service_pram(true)==0);
    /* An earlier eject can force a hidden save without a public-pulse marker.
     * Its pending fence must retire before a new pulse is acknowledged. */
    assert(pram_save_pulses==1U && media_model.pram_completions==2U && !eject_pending_test);
    assert(media_model.coalesced_pram_pulses==0U && !queued_pram);

    (void)start_busy_media_model(true);assert(iigs_content_service_pram(true)==0);
    media_model.fail_pram_completion=true;
    assert(iigs_content_service_pram(false)!=0);
    assert(media_model.pram_completions==1U && pram_restore_pending && warm_reset_packets==0U);
    assert(media_model.target_closes==0U && iigs_content_service_name(2U)!=NULL);
}
static void test_fresh_floppy_error_retry(void)
{
    (void)start_busy_media_model(false);request(2U,0U,false);
    const uint32_t old_id=regs[MT_REG_MEDIA_REQUEST_ID/4U];
    fail_sector_transfer=true;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && regs[MT_REG_MEDIA_REQUEST_ID/4U]!=old_id);
    fail_sector_transfer=false;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_OK && request_slot()==0U);
}
static void test_pram_config_transport_boundaries(void)
{
    (void)start_busy_media_model(true);fail_config_end_once=true;
    assert(iigs_content_service_pram(true)!=0);
    assert(pram_save_pulses==1U && queued_pram && media_model.pram_completions==0U);
    /* STREAM_END failed after the final config byte committed the old pulse.
     * A later call must finish it before sending its own fresh save pulse. */
    assert(iigs_content_service_pram(true)==0);
    assert(pram_save_pulses==2U && media_model.pram_completions==2U && !queued_pram);
    assert(media_model.coalesced_pram_pulses==0U && media_model.target_closes==0U);

    (void)start_busy_media_model(true);media_model.hdd_left=300100U;fail_config_begin=true;
    assert(iigs_content_service_pram(true)!=0);
    assert(pram_save_pulses==0U && !queued_pram && media_model.pram_completions==0U);
    fail_config_begin=false;
    /* No request committed. Preserve the ambiguous operation rather than
     * inventing an ACK, issuing another pulse, or discarding open media. */
    assert(iigs_content_service_pram(true)!=0 && iigs_content_service_prepare_reset()!=0);
    assert(pram_save_pulses==0U && media_model.pram_completions==0U && warm_reset_packets==0U);
    assert(media_model.ejects==0U && media_model.target_closes==0U && reset_assertions==1U);
    assert(iigs_content_service_name(0U)!=NULL && iigs_content_service_name(2U)!=NULL);
    assert((dirty_drives&4U)!=0U);
}
static void test_media_capability_guard(void)
{
    static const struct {uint32_t abi,caps;} incompatible[]={
        {MT_ABI_VERSION,MT_IIGS_ACCEL_CAPS_VALID|MT_IIGS_ACCEL_SPEED_MASK},
        {MT_ABI_VERSION,MT_IIGS_MEDIA_TARGET_DRAIN_SUPPORTED|MT_IIGS_ACCEL_SPEED_MASK},
        {21U,TEST_IIGS_CAPS}
    };
    for(unsigned i=0U;i<sizeof(incompatible)/sizeof(incompatible[0]);++i) {
        reset();(void)raw_disk("old-core.po",MT_IIGS_RAW_PRODOS35,0U);
        regs[MT_REG_ABI_VERSION/4U]=incompatible[i].abi;regs[MT_REG_IIGS_ACCEL_CAPS/4U]=incompatible[i].caps;
        iigs_content_service_init(&core,TEST_MEDIA_ROOT,NULL,NULL);
        assert(iigs_content_service_boot()!=0 && !iigs_content_service_ready());
        assert(iigs_content_service_mount(2U,"old-core.po")!=0);
        assert(iigs_content_service_eject(2U)!=0 && iigs_content_service_pram(true)!=0);
        request(2U,0U,false);iigs_content_service_poll();
        assert(iigs_content_service_retry()!=0);
        assert(strcmp(iigs_content_service_error(),"Update IIgs core for safe media changes")==0);
        assert(iigs_content_service_faulted() && file_open_calls==0U && rom_read_count==0U && rom_stream_count==0U);
        assert(reset_assertions==0U && regs[MT_REG_COMMAND_DOORBELL/4U]==0U);
        for(unsigned slot=0U;slot<MT_IIGS_MEDIA_SLOT_COUNT;++slot)assert(generations[slot]==0U);
        /* Reject unsafe media commands without trapping the user in a core
         * that has no open handles or pending state to preserve. */
        assert(iigs_content_service_prepare_reset()==0 && regs[MT_REG_COMMAND_DOORBELL/4U]==0U);
        assert(iigs_content_service_resume_after_reset()!=0 && regs[MT_REG_COMMAND_DOORBELL/4U]==0U);
    }
}
int main(void)
{
    test_target_eject_amid_hdd_traffic();
    test_target_drain_failures_preserve_media();
    test_pram_and_reset_amid_hdd_traffic();
    test_fresh_floppy_error_retry();
    test_pram_config_transport_boundaries();
    test_media_capability_guard();
    test_eight_hdd_and_legacy_settings();
    test_wide_hdd_and_error_recovery();
    test_media_progress_and_completion_retry();
    test_media_request_rejection_errors();
    test_raw_metadata_transport();
    test_raw_floppy_transport();
    test_raw_floppy_recovery();
    test_raw_floppy_protection();
    test_speed_capability();
    test_irq_default_and_saved_choices();
    test_mouse_settings();
    test_card_mode_lifecycle();
    test_fast_floppy_lifecycle();
    test_legacy_none_and_card_rollback();
    test_card_restart_failures();
    test_rom_paths();
    test_nested_media_paths();
    reset();test_file_t *f=disk("test.2mg");memset(f->data,0,64U);memcpy(f->data,"2IMG",4U);f->data[8]=64U;f->data[10]=1U;
    store32(f->data+12U,1U);store32(f->data+20U,3U);store32(f->data+24U,512U);store32(f->data+28U,1536U);
    assert(iigs_content_service_mount(1U,"test.2mg")==0 && mount_sizes[1]==1536U);
    request(1U,1U,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK && stream_data[1]==1U);
    request(1U,1U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_OK && journal_writes==1U && disk_writes==1U);
    assert(f->data[1024U]==0x31U && f->data[1535U]==0x30U && memcmp(f->data,"2IMG",4U)==0);
    assert(file_find("0:/multitini/cores/apple-iigs/media/test.2mg.gsj",false)==NULL);
    request(1U,3U,false);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_IO_ERROR);
    assert(iigs_content_service_eject(1U)==0); /* HDD errors complete instead of hanging the guest. */

    reset();f=disk("write.hdv");assert(iigs_content_service_mount(0U,"write.hdv")==0);
    request(0U,0U,true);fail_disk_write=true;iigs_content_service_poll();
    assert(completion_status==MT_RESPONSE_IO_ERROR && iigs_content_service_faulted());
    assert(file_find("0:/multitini/cores/apple-iigs/media/write.hdv.gsj",false)!=NULL);
    fail_disk_write=false;assert(iigs_content_service_retry()==0 && !iigs_content_service_faulted());
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
    request(0U,0U,true);iigs_content_service_poll();assert(completion_status==MT_RESPONSE_IO_ERROR && disk_writes==0U && reverse_index==0U);

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
    puts("Apple IIgs eight HDDs, wide LBA/2MG, settings migration, durable recovery, floppy CRC and PRAM reset ordering passed");return 0;
}
