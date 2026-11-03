/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production service through real file/native fixture boundaries. */
#define main amiga_service_existing_regressions
#define mt_sd_file_read fixture_file_read
#include "test_amiga_service.c"
#undef mt_sd_file_read
#undef main

static struct { file_t *file;size_t offset,bytes; } reads[2048];
static unsigned read_count,drain_left;
static bool fail_read,short_fill;
static bool reset_on_fill,reset_on_recheck,after_floppy_deselect;
static unsigned reads_after_deselect;
static uint64_t read_clock;
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *f,void *data,size_t bytes)
{
    assert(read_count<2048U);
    reads[read_count].file=f->file;reads[read_count].offset=f->pos;reads[read_count++].bytes=bytes;
    read_clock+=100U;
    if(fail_read || short_fill) {
        const size_t prefix=bytes>512U ? 512U:128U;
        memcpy(data,f->file->data+f->pos,prefix);f->pos+=prefix;
        return result(fail_read ? MT_SD_READ_FAILED:MT_SD_OK,prefix);
    }
    const mt_sd_result_t r=fixture_file_read(f,data,bytes);
    if(reset_on_fill) { reset_on_fill=false;ide_request=6U; }
    return r;
}
static uint64_t timer(void *context) { assert(context==&read_clock);return read_clock; }
static uint32_t timed_rd(void *context,uint32_t address)
{
    ++read_clock;
    /* The fixture completes SELECT0 with one read, then ATA takes its initial
     * request snapshot; the third read is the shared pre-FIFO resample. */
    if(reset_on_recheck && after_floppy_deselect && address==MT_AMIGA_HOST_PORT && ++reads_after_deselect==3U) {
        reset_on_recheck=false;after_floppy_deselect=false;ide_request=6U;
    }
    return rd(context,address);
}
static void timed_wr(void *context,uint32_t address,uint32_t value)
{
    read_clock+=2U;
    if(reset_on_recheck && address==MT_AMIGA_HOST_PORT && value==0U && select_pins==1U) {
        after_floppy_deselect=true;reads_after_deselect=0U;
    }
    wr(context,address,value);
}
static void progress_reads(void *context)
{
    if(drain_left!=0U && ide_request==0U && native_config[1]==0U) { ide_request=5U;--drain_left; }
    progress(context);
}
static void check_sector(file_t *f,unsigned lba)
{
    assert(!amiga_content_service_faulted() && ide_request==0U);
    for(unsigned i=0U;i<256U;++i)assert(transmitted[i]==(uint16_t)((uint16_t)f->data[lba*512U+2U*i]|((uint16_t)f->data[lba*512U+2U*i+1U]<<8U)));
}
static void read_command(unsigned drive,file_t *f,unsigned lba,unsigned count)
{
    command(0x20U,drive,lba,count==256U ? 0U:count);
    for(unsigned i=0U;i<count;++i) { if(i!=0U)ide_request=5U;amiga_content_service_poll();check_sector(f,lba+i); }
}
int main(void)
{
    (void)create("0:/multitini/cores/amiga/roms/kick.rom",524288U);
    file_t *a=create(ROOT "/ReadAhead.hdf",260U*512U),*b=create(ROOT "/Other.hdf",260U*512U);
    file_t *adf=create(ROOT "/ReadAhead.adf",901120U);
    for(unsigned i=0U;i<a->size;++i) { a->data[i]^=0x17U;b->data[i]^=0xc3U; }
    mt_core_link_t link;mt_core_link_init(&link,NULL,timed_rd,timed_wr);bulk_capable=true;fast_ide_capable=true;
    amiga_content_service_init(&link,ROOT,false,progress_reads,NULL);assert(amiga_content_service_boot()==0);
    assert(amiga_content_service_mount(2U,"ReadAhead.hdf")==0 && amiga_content_service_mount(3U,"Other.hdf")==0);
    assert(amiga_content_service_mount(0U,"ReadAhead.adf")==0);
    const unsigned counts[]={1U,31U,32U,33U,128U,256U};
    for(unsigned c=0U;c<sizeof(counts)/sizeof(counts[0]);++c) {
        const unsigned count=counts[c],before=read_count,blocks=bulk_commands;
        uint32_t logical_before,logical_after;amiga_content_service_activity(2U,&logical_before,NULL);
        assert(amiga_content_service_profile_start(timer,&read_clock)==0);read_command(0U,a,0U,count);
        amiga_content_service_profile_stop();mt_amiga_ide_profile_t p;amiga_content_service_profile_snapshot(&p);
        const unsigned fills=(count+31U)/32U;
        assert(read_count-before==fills && bulk_commands-blocks==count);
        for(unsigned n=0U;n<fills;++n) {
            const unsigned expected=count-n*32U<32U ? count-n*32U:32U;
            assert(reads[before+n].file==a && reads[before+n].offset==n*32U*512U && reads[before+n].bytes==expected*512U);
        }
        amiga_content_service_activity(2U,&logical_after,NULL);assert(logical_after-logical_before==count);
        assert(p.valid && p.sd_read_calls==fills && p.read_bytes==(uint64_t)count*512U && p.sd_read_ticks==(uint64_t)fills*100U);
        assert(p.read_sectors==count && p.read_window_bytes==(uint64_t)count*512U && p.fifo_calls==count);
        assert(p.service_ticks==p.sd_read_ticks+p.fifo_ticks+p.ata_other_ticks+p.fdd_ticks);
    }
    /* A fresh command must neither reuse a prior command's cache nor leak
     * another drive's data. Tail fills stop at the explicit ATA count/EOF. */
    unsigned before=read_count;command(0x20U,0U,0U,33U);amiga_content_service_poll();check_sector(a,0U);
    a->data[512U]^=0x55U;read_command(0U,a,1U,1U);assert(read_count==before+2U && reads[before+1U].bytes==512U);
    read_command(1U,b,1U,1U);assert(reads[read_count-1U].file==b);
    before=read_count;read_command(0U,a,257U,3U);assert(read_count==before+1U && reads[before].bytes==1536U);
    const unsigned blocks=bulk_commands;before=read_count;command(0x20U,0U,259U,2U);amiga_content_service_poll();
    assert(read_count==before && bulk_commands==blocks && (ide_regs[5]&0xff00U)==0x5500U);
    /* IDENTIFY reads no disk bytes. FDD stays a single-sector MFM read and
     * cannot consume or replace cached HDF data at the same LBA. */
    command(0xecU,0U,0U,1U);amiga_content_service_poll();assert(read_count==before);
    command(0x20U,0U,0U,3U);amiga_content_service_poll();check_sector(a,0U);before=read_count;
    floppy_read=true;floppy_track=0U;transmitted_count=0U;amiga_content_service_poll();floppy_read=false;
    uint8_t decoded[512];unsigned sector_number=99U;
    assert(mt_amiga_mfm_decode(transmitted,0U,&sector_number,decoded) && memcmp(decoded,adf->data,512U)==0);
    assert(read_count==before+1U && reads[before].file==adf && reads[before].bytes==512U);
    ide_request=5U;amiga_content_service_poll();check_sector(a,1U);assert(read_count==before+1U);
    /* Starting profiling during a cache hit still has a first READ timestamp
     * and logical bytes, with no invented physical backend reads. */
    assert(amiga_content_service_profile_start(timer,&read_clock)==0);
    ide_request=5U;amiga_content_service_poll();check_sector(a,2U);amiga_content_service_profile_stop();
    mt_amiga_ide_profile_t p;amiga_content_service_profile_snapshot(&p);
    assert(p.valid && p.read_window_started && p.read_window_bytes==512U && p.read_sectors==1U && p.sd_read_calls==0U && p.read_bytes==0U);
    /* Quiesce invalidates the outstanding cache before draining the command.
     * Remaining32 sectors are refilled from LBA1, rather than using cached31. */
    command(0x20U,0U,0U,33U);amiga_content_service_poll();before=read_count;drain_left=32U;
    assert(amiga_content_service_prepare_reset()==0 && drain_left==0U);
    assert(read_count==before+1U && reads[before].offset==512U && reads[before].bytes==16384U);
    assert(amiga_content_service_resume_after_reset()==0);
    /* Writes retain durable journal replay and cannot expose stale reads. */
    command(0x20U,0U,0U,33U);amiga_content_service_poll();
    command(0x30U,0U,1U,1U);amiga_content_service_poll();
    for(unsigned i=0U;i<256U;++i)fifo[i]=0xabcdU;ide_request=5U;fail_write=true;amiga_content_service_poll();
    const unsigned captured=bulk_commands;assert(amiga_content_service_faulted() && ide_request==5U);
    fail_write=false;assert(amiga_content_service_retry()==0);amiga_content_service_poll();assert(bulk_commands==captured && ide_request==0U);
    read_command(0U,a,1U,1U);assert(a->data[512U]==0xcdU && a->data[513U]==0xabU);
    /* A short or failed batch aborts before the current sector/FIFO/ACK, even
     * when its returned prefix happens to contain a full current sector. */
    for(unsigned failure=0U;failure<2U;++failure) {
        fail_read=failure==0U;short_fill=failure==1U;before=read_count;const unsigned old_blocks=bulk_commands;
        assert(amiga_content_service_profile_start(timer,&read_clock)==0);command(0x20U,0U,0U,33U);amiga_content_service_poll();
        amiga_content_service_profile_snapshot(&p);
        assert(amiga_content_service_faulted() && ide_request==4U && read_count==before+1U && bulk_commands==old_blocks);
        assert(!p.valid && p.reason==MT_AMIGA_PROFILE_IO_ERROR && p.read_bytes==512U && p.read_sectors==0U && p.read_window_bytes==0U && p.fifo_calls==0U);
        fail_read=false;short_fill=false;assert(amiga_content_service_retry()==0);amiga_content_service_poll();check_sector(a,0U);
        assert(read_count==before+2U && reads[before+1U].offset==0U && reads[before+1U].bytes==16384U);
        command(0xecU,0U,0U,1U);amiga_content_service_poll();
    }
    /* Reset during a batch or at the cache-hit resample must remain pending
     * without any old sector FIFO, status ACK or ATA progression. */
    before=read_count;const unsigned before_reset_blocks=bulk_commands;
    assert(amiga_content_service_profile_start(timer,&read_clock)==0);reset_on_fill=true;
    command(0x20U,0U,0U,33U);amiga_content_service_poll();amiga_content_service_profile_snapshot(&p);
    assert(!reset_on_fill && ide_request==6U && ide_regs[5]==0x20e0U && bulk_commands==before_reset_blocks && read_count==before+1U);
    assert(!p.valid && p.reason==MT_AMIGA_PROFILE_LIFECYCLE && p.fifo_calls==0U && p.read_window_bytes==0U);
    amiga_content_service_poll();assert(ide_request==0U && (ide_regs[5]&0xff00U)==0x5000U);
    command(0x20U,0U,0U,33U);amiga_content_service_poll();before=read_count;
    const unsigned cached_blocks=bulk_commands;const uint16_t old_status=ide_regs[5];
    reset_on_recheck=true;ide_request=5U;amiga_content_service_poll();
    assert(!reset_on_recheck && ide_request==6U && ide_regs[5]==old_status && bulk_commands==cached_blocks && read_count==before);
    amiga_content_service_poll();assert(ide_request==0U && (ide_regs[5]&0xff00U)==0x5000U);
    read_command(1U,b,0U,1U);
    assert(amiga_content_service_eject(2U)==0);a->data[0]^=0x4aU;
    assert(amiga_content_service_mount(2U,"ReadAhead.hdf")==0);read_command(0U,a,0U,1U);
    assert(amiga_content_service_shutdown()==0 && amiga_content_service_resume_after_reset()==0);read_command(0U,a,0U,1U);
    puts("PASS bounded ATA read-ahead:32-sector fills, logical/physical profiling, drive/command/lifecycle isolation, FDD/IDENTIFY exclusion, journal replay and fail-closed partial batches");return 0;
}
