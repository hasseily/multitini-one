/* SPDX-License-Identifier: GPL-3.0-only */
#include "amiga_content_service.h"
#include "sd_storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define FILE_COUNT 16U
#define ROOT "0:/multitini/cores/amiga/media"
typedef struct { char name[512];uint8_t data[1048576];size_t size;bool used,readonly; } file_t;
struct mt_sd_file { file_t *file;size_t pos;bool open; };
static file_t files[FILE_COUNT];static struct mt_sd_file handles[16];
static uint32_t regs[1024],native_status;static unsigned select_pins,io_phase,uio_reg;
static uint16_t native_config[16],ide_regs[16],fifo[4096],transmitted[2048];
static unsigned fifo_position,fifo_count,transmitted_count,ide_pointer,ide_request,rom_streams;
static bool floppy_read,floppy_write,fail_journal,fail_write,core_reset;
static unsigned floppy_track;
static unsigned pio_ticks,pio_progress;
static bool race_on_halt;
static bool reject_rom_begin,load_failure,native_error_latch;
static unsigned load_error_clears;
static bool rtc_capable=true,rtc_fail_middle,rtc_seed_held;
static uint32_t rtc_low,rtc_middle;
static uint64_t rtc_value;
static unsigned rtc_stage,rtc_commits,rtc_discards;
static bool bulk_capable,bulk_timeout,cpu14_capable,fast_ide_capable;
static uint8_t staged_config[MT_AMIGA_CONFIG_BYTES];
static uint32_t bulk_stage[128],bulk_status;
static unsigned bulk_commands,legacy_data_words;
static bool bench_timed;
static uint64_t bench_clock;
static bool bench_restart_expected;
static unsigned bench_restarts;
static uint16_t expected_restart_fifo[256];
static uint8_t rom_first,rom_middle,rom_last,stream_kind;static size_t stream_offset;
static mt_sd_result_t result(mt_sd_status_t s,size_t n) { return (mt_sd_result_t){s,0U,n}; }
static file_t *find(const char *name,bool create)
{
    for(unsigned i=0U;i<FILE_COUNT;++i)if(files[i].used && strcmp(files[i].name,name)==0)return &files[i];
    if(create)for(unsigned i=0U;i<FILE_COUNT;++i)if(!files[i].used) {
        files[i].used=true;snprintf(files[i].name,sizeof(files[i].name),"%s",name);return &files[i];
    }
    return NULL;
}
static file_t *create(const char *name,size_t size)
{ file_t *f=find(name,true);assert(f!=NULL && size<=sizeof(f->data));f->size=size;for(size_t i=0U;i<size;++i)f->data[i]=(uint8_t)(i^(i>>9U));return f; }
mt_sd_result_t mt_sd_storage_journal_recover(const char *p,const char *j,uint8_t *b,size_t n)
{ (void)p;(void)j;(void)b;(void)n;return result(MT_SD_OK,0U); }
mt_sd_result_t mt_sd_storage_journal_replace(const char *p,const char *j,const uint8_t *b,size_t n)
{
    (void)j;if(fail_journal && strstr(p,".amj")!=NULL)return result(MT_SD_WRITE_FAILED,0U);
    file_t *f=find(p,true);assert(f!=NULL && n<=sizeof(f->data));memcpy(f->data,b,n);f->size=n;return result(MT_SD_OK,n);
}
mt_sd_result_t mt_sd_storage_read_file_optional_max(const char *p,uint8_t *b,size_t n,bool *found)
{ file_t *f=find(p,false);*found=f!=NULL;if(f==NULL)return result(MT_SD_OK,0U);if(f->size>n)return result(MT_SD_TOO_LARGE,0U);memcpy(b,f->data,f->size);return result(MT_SD_OK,f->size); }
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char *p,uint8_t *b,size_t n,bool *found)
{ mt_sd_result_t r=mt_sd_storage_read_file_optional_max(p,b,n,found);if(r.status==MT_SD_OK && *found && r.size!=n)r.status=MT_SD_SIZE_MISMATCH;return r; }
mt_sd_result_t mt_sd_storage_read_file_max(const char *p,uint8_t *b,size_t n)
{ bool found=false;mt_sd_result_t r=mt_sd_storage_read_file_optional_max(p,b,n,&found);if(!found)r.status=MT_SD_OPEN_FAILED;return r; }
mt_sd_result_t mt_sd_file_open(const char *p,const char *mode,mt_sd_file_t **out)
{
    file_t *f=find(p,false);if(f==NULL || (f->readonly && strchr(mode,'+')!=NULL))return result(MT_SD_OPEN_FAILED,0U);
    for(unsigned i=0U;i<16U;++i)if(!handles[i].open) { handles[i]=(struct mt_sd_file){f,0U,true};*out=&handles[i];return result(MT_SD_OK,0U); }
    return result(MT_SD_FILE_LIMIT,0U);
}
mt_sd_result_t mt_sd_file_seek(mt_sd_file_t *f,int64_t offset,int origin)
{
    assert(f->open);int64_t p=origin==SEEK_END ? (int64_t)f->file->size:origin==SEEK_CUR ? (int64_t)f->pos:0;
    p+=offset;if(p<0 || (uint64_t)p>f->file->size)return result(MT_SD_SEEK_FAILED,0U);f->pos=(size_t)p;return result(MT_SD_OK,0U);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *f) { assert(f->open);return (int64_t)f->pos; }
mt_sd_result_t mt_sd_file_read(mt_sd_file_t *f,void *b,size_t n)
{ assert(!bench_timed && f->open);if(n>f->file->size-f->pos)n=f->file->size-f->pos;memcpy(b,f->file->data+f->pos,n);f->pos+=n;return result(MT_SD_OK,n); }
mt_sd_result_t mt_sd_file_write(mt_sd_file_t *f,const void *b,size_t n)
{ assert(!bench_timed && f->open && f->pos+n<=f->file->size);if(fail_write)return result(MT_SD_SYNC_FAILED,0U);memcpy(f->file->data+f->pos,b,n);f->pos+=n;return result(MT_SD_OK,n); }
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *f) { assert(f->open);f->open=false;return result(MT_SD_OK,0U); }
mt_sd_result_t mt_sd_file_remove(const char *p) { file_t *f=find(p,false);if(f!=NULL)f->used=false;return result(MT_SD_OK,0U); }
const char *mt_sd_status_name(mt_sd_status_t s) { (void)s;return "test storage error"; }
static uint32_t rd(void *ctx,uint32_t offset)
{
    (void)ctx;
    if(offset==MT_AMIGA_HOST_PORT)return native_status|((uint32_t)ide_request<<16U)|
        (rtc_capable ? MT_AMIGA_HOST_RTC:0U)|(bulk_capable ? MT_AMIGA_HOST_BULK:0U)|(cpu14_capable ? MT_AMIGA_HOST_CPU14:0U)|
        (fast_ide_capable ? MT_AMIGA_HOST_FAST_IDE:0U);
    if(offset==MT_AMIGA_BULK_STATUS)return bulk_status;
    if(offset==MT_AMIGA_BULK_PROGRESS)return 256U;
    if(offset>=MT_AMIGA_BULK_DATA && offset<MT_AMIGA_BULK_DATA+512U)return bulk_stage[(offset-MT_AMIGA_BULK_DATA)/4U];
    return regs[offset/4U];
}
static void wr(void *ctx,uint32_t offset,uint32_t value)
{
    (void)ctx;
    if(offset>=MT_AMIGA_BULK_DATA && offset<MT_AMIGA_BULK_DATA+512U) {
        assert(!(bulk_status&MT_AMIGA_BULK_BUSY));bulk_stage[(offset-MT_AMIGA_BULK_DATA)/4U]=value;return;
    }
    if(offset==MT_AMIGA_BULK_COMMAND) {
        assert(bulk_capable && (value>>16U)!=0U && (value&0xfffeU)==512U);++bulk_commands;
        if(bulk_timeout) { bulk_status=MT_AMIGA_BULK_BUSY;return; }
        for(unsigned i=0U;i<128U;++i) {
            if(value&1U)bulk_stage[i]=(uint32_t)fifo[2U*i]|((uint32_t)fifo[2U*i+1U]<<16U);
            else { transmitted[2U*i]=fifo[2U*i]=(uint16_t)bulk_stage[i];transmitted[2U*i+1U]=fifo[2U*i+1U]=(uint16_t)(bulk_stage[i]>>16U); }
        }
        bulk_status=MT_AMIGA_BULK_DONE|(value>>16U);return;
    }
    if(offset!=MT_AMIGA_HOST_PORT) {
        if(offset==MT_REG_ERROR && (value&MT_ERROR_CORE_LOAD)!=0U) { load_failure=false;++load_error_clears; }
        regs[offset/4U]=value;return;
    }
    const unsigned op=value>>24U,address=(value>>16U)&31U;const uint16_t data=(uint16_t)value;uint16_t answer=0U;
    if(op==MT_AMIGA_HOST_CLEAR_ERROR) { native_error_latch=false;rtc_stage=0U;++rtc_discards; }
    if(op==MT_AMIGA_HOST_SELECT) { select_pins=data;io_phase=0U;uio_reg=255U; }
    else if(op==MT_AMIGA_HOST_TRANSFER) {
        if(select_pins==2U) {
            if(uio_reg==255U)uio_reg=data&15U;
            else {
                native_config[uio_reg]=data;
                if(uio_reg==1U && data==4U && race_on_halt) {
                    race_on_halt=false;ide_regs[1]=0x0301U;ide_regs[2]=0U;
                    ide_regs[5]=0x30e0U;ide_request=4U;pio_ticks=3U;
                }
            }
        } else if(select_pins==1U) {
            if(io_phase==0U)answer=(uint16_t)(0x1800U|floppy_track|(floppy_read ? 0x100U:0U)|(floppy_write ? 0x200U:0U));
            else if(io_phase==1U)answer=0x4489U;
            else if(io_phase==2U)answer=(uint16_t)(floppy_write ? fifo_count-fifo_position:0U);
            else if(floppy_write) { assert(fifo_position<fifo_count);answer=fifo[fifo_position++];if(fifo_position==fifo_count)floppy_write=false; }
            else if(floppy_read) { assert(transmitted_count<2048U);transmitted[transmitted_count++]=data; }
            ++io_phase;
        } else assert(false);
    } else if(op==MT_AMIGA_HOST_RTC_LOW) { rtc_low=value&0xffffffU;rtc_stage=1U; }
    else if(op==MT_AMIGA_HOST_RTC_MIDDLE) {
        assert(rtc_stage==1U);
        if(rtc_fail_middle) { rtc_fail_middle=false;native_error_latch=true; }
        else { rtc_middle=value&0xffffffU;rtc_stage=3U; }
    } else if(op==MT_AMIGA_HOST_RTC_COMMIT) {
        assert(rtc_stage==3U && (value&0xff0000U)==0U);
        rtc_value=(uint64_t)rtc_low|((uint64_t)rtc_middle<<24U)|((uint64_t)data<<48U);
        rtc_stage=0U;++rtc_commits;if(core_reset)rtc_seed_held=true;
    } else if(op==MT_AMIGA_HOST_IDE_READ) {
        if(address==15U) { ++legacy_data_words;assert(ide_pointer<256U);answer=fifo[ide_pointer++]; }
        else { answer=ide_regs[address];ide_pointer=0U; }
    } else if(op==MT_AMIGA_HOST_IDE_WRITE) {
        if(address==15U) { ++legacy_data_words;assert(ide_pointer<256U);fifo[ide_pointer]=data;transmitted[ide_pointer++]=data; }
        else { ide_regs[address]=data;ide_pointer=0U;if(address==5U)ide_request=0U; }
    }
    native_status=(native_status^MT_AMIGA_HOST_TOGGLE)&MT_AMIGA_HOST_TOGGLE;native_status|=answer;
    if(native_error_latch)native_status|=MT_AMIGA_HOST_ERROR;
}
static void progress(void *ctx)
{
    (void)ctx;
    if(pio_ticks!=0U && native_config[1]==0U) {
        ++pio_progress;
        if(--pio_ticks==0U) { for(unsigned i=0U;i<256U;++i)fifo[i]=0x45a3U;ide_request=5U; }
    }
    const uint32_t seq=regs[MT_REG_COMMAND_SEQ/4U];if(seq==regs[MT_REG_RESPONSE_SEQ/4U])return;
    const uint32_t op=regs[MT_REG_COMMAND_OPCODE/4U],a=regs[MT_REG_COMMAND_ARG0/4U];
    if(op==MT_COMMAND_CORE_RESET) {
        if(a!=0U && bench_restart_expected) {
            assert(!bench_timed && memcmp(fifo,expected_restart_fifo,sizeof(expected_restart_fifo))==0);
            bench_restart_expected=false;++bench_restarts;
        }
        core_reset=a!=0U;
    }
    else if(op==MT_COMMAND_STREAM_BEGIN) {
        assert(core_reset && !load_failure);stream_kind=(uint8_t)a;stream_offset=0U;
        if(stream_kind==MT_LOAD_AMIGA_KICKSTART && reject_rom_begin) {
            reject_rom_begin=false;load_failure=true;
            regs[MT_REG_RESPONSE_SEQ/4U]=seq;regs[MT_REG_RESPONSE_STATUS/4U]=MT_RESPONSE_IO_ERROR;return;
        }
    }
    else if(op==MT_COMMAND_STREAM_DATA) {
        if(stream_kind==MT_LOAD_AMIGA_CONFIG) { assert(stream_offset<sizeof(staged_config));staged_config[stream_offset]=(uint8_t)a; }
        if(stream_kind==MT_LOAD_AMIGA_KICKSTART) {
            if(stream_offset==0U)rom_first=(uint8_t)a;
            if(stream_offset==0x80000U)rom_middle=(uint8_t)a;
            if(stream_offset==0xfffffU)rom_last=(uint8_t)a;
        }
        ++stream_offset;
    } else if(op==MT_COMMAND_STREAM_END) { if(stream_kind==MT_LOAD_AMIGA_KICKSTART) { assert(stream_offset==1048576U);++rom_streams; } }
    else assert(op==MT_COMMAND_STREAM_ABORT);
    regs[MT_REG_RESPONSE_SEQ/4U]=seq;regs[MT_REG_RESPONSE_STATUS/4U]=MT_RESPONSE_OK;
}
static void command(uint8_t op,unsigned drive,uint32_t lba,unsigned count)
{
    ide_regs[1]=(uint16_t)(count|((lba&255U)<<8U));ide_regs[2]=(uint16_t)(lba>>8U);
    ide_regs[5]=(uint16_t)(((uint16_t)op<<8U)|0xe0U|(drive<<4U)|((lba>>24U)&15U));ide_request=4U;
}
static uint64_t bench_timer(void *context)
{ assert(context==NULL && native_config[1]==4U && ide_request==0U);bench_timed=!bench_timed;bench_clock+=10U;return bench_clock; }

static uint32_t disk_word(unsigned index)
{
    return ((uint32_t)(transmitted[2U*index]&255U)<<24U) |
        ((uint32_t)(transmitted[2U*index]>>8U)<<16U) |
        ((uint32_t)(transmitted[2U*index+1U]&255U)<<8U) |
        (transmitted[2U*index+1U]>>8U);
}
static void check_metadata(uint32_t magic)
{
    assert(disk_word(0U)==magic && disk_word(1U)<=128U);
    uint32_t sum=0U;
    for(unsigned i=0U;i<disk_word(1U);++i)sum+=disk_word(i);
    assert(sum==0U);
}
static void test_partition_hardfiles(void)
{
    /* Match the exact geometry of the two reported files without a copy of
     * any private media. */
    mt_amiga_hardfile_t layout;
    assert(mt_amiga_hardfile_layout(&layout,409600U,0x444f5301U,0U));
    assert(layout.first_lba==512U && layout.cylinders==801U);
    assert(mt_amiga_hardfile_layout(&layout,4112640U,MT_AMIGA_PFS3_DOSTYPE,59120U));
    assert(layout.first_lba==1008U && layout.cylinders==4081U);

    file_t *ffs=create(ROOT "/Partition.hdf",262144U);
    memcpy(ffs->data,"DOS\1",4U);
    assert(amiga_content_service_mount(3U,"Partition.hdf")==0);
    command(0xecU,1U,0U,1U);amiga_content_service_poll();
    assert(transmitted[55]==16U && transmitted[56]==32U && transmitted[60]==1024U);
    command(0x20U,1U,0U,2U);amiga_content_service_poll();
    check_metadata(0x5244534bU);
    assert(disk_word(7U)==1U && disk_word(8U)==UINT32_MAX && disk_word(34U)==1U);
    ide_request=5U;amiga_content_service_poll();
    check_metadata(0x50415254U);
    assert(disk_word(5U)==0U && disk_word(9U)==0x03444831U && disk_word(48U)==0x444f5301U);
    command(0x20U,1U,511U,2U);amiga_content_service_poll();
    assert(disk_word(0U)==0U);
    ide_request=5U;amiga_content_service_poll();
    assert(disk_word(0U)==0x444f5301U);

    command(0x30U,1U,511U,2U);amiga_content_service_poll();
    assert(ide_request==0U && (ide_regs[0]>>8U)==4U && !amiga_content_service_faulted());
    command(0x30U,1U,514U,1U);amiga_content_service_poll();
    for(unsigned i=0U;i<256U;++i)fifo[i]=0x3412U;
    ide_request=5U;fail_write=true;amiga_content_service_poll();
    assert(amiga_content_service_faulted());
    const file_t *journal=find(ROOT "/Partition.hdf.amj",false);
    assert(journal!=NULL && journal->data[8]==0U && journal->data[9]==4U);
    fail_write=false;assert(amiga_content_service_retry()==0);
    amiga_content_service_poll();
    assert(ide_request==0U && memcmp(ffs->data,"DOS\1",4U)==0);
    for(unsigned i=1024U;i<1536U;i+=2U)assert(ffs->data[i]==0x12U && ffs->data[i+1U]==0x34U);
    command(0x20U,1U,1024U,1U);amiga_content_service_poll();
    assert(ide_request==0U && (ide_regs[0]>>8U)==0x10U);
    assert(amiga_content_service_eject(3U)==0);

    file_t *pfs=create(ROOT "/Apps.hdf",262144U);
    memcpy(pfs->data,"PFS\1",4U);
    memcpy(pfs->data+1024U,"PFS\1",4U);
    file_t *handler=create("0:/multitini/cores/amiga/roms/pfs3aio",500U);
    memcpy(handler->data,"\0\0\3\xf3",4U);
    assert(amiga_content_service_mount(3U,"Apps.hdf")==0);
    command(0x20U,1U,0U,1U);amiga_content_service_poll();
    check_metadata(0x5244534bU);
    assert(disk_word(8U)==2U);
    command(0x20U,1U,1U,1U);amiga_content_service_poll();
    check_metadata(0x50415254U);
    assert(disk_word(48U)==MT_AMIGA_PFS3_DOSTYPE);
    command(0x20U,1U,2U,1U);amiga_content_service_poll();
    check_metadata(0x46534844U);
    assert(disk_word(8U)==MT_AMIGA_PFS3_DOSTYPE && disk_word(18U)==3U);
    command(0x20U,1U,3U,2U);amiga_content_service_poll();
    check_metadata(0x4c534547U);
    assert(disk_word(4U)==4U && disk_word(5U)==0x000003f3U);
    ide_request=5U;amiga_content_service_poll();
    check_metadata(0x4c534547U);
    assert(disk_word(1U)==7U && disk_word(4U)==UINT32_MAX);
    command(0x20U,1U,512U,1U);amiga_content_service_poll();
    assert(disk_word(0U)==0x50465301U);
    assert(amiga_content_service_eject(3U)==0);
}

int main(void)
{
    file_t *kick=create("0:/multitini/cores/amiga/roms/kick.rom",524288U);
    file_t *adf=create(ROOT "/Workbench.adf",901120U),*hdf=create(ROOT "/System.hdf",4096U);
    mt_core_link_t link;mt_core_link_init(&link,NULL,rd,wr);
    amiga_content_service_init(&link,ROOT,false,progress,NULL);
    mt_clock_time_t clock={2026U,10U,31U,12U,34U,56U};
    assert(amiga_content_service_set_clock(&clock,0U)==MT_LINK_OK && rtc_commits==0U);
    assert(amiga_content_service_boot()==0);
    assert(rtc_seed_held && rtc_commits==1U && rtc_value==UINT64_C(0x4006261031123456));
    ++clock.second;rtc_fail_middle=true;const unsigned discards=rtc_discards;
    assert(amiga_content_service_set_clock(&clock,1000U)==MT_LINK_REMOTE_ERROR);
    assert(!amiga_content_service_faulted() && rtc_stage==0U && rtc_discards==discards+1U && !native_error_latch);
    assert(amiga_content_service_set_clock(&clock,2000U)==MT_LINK_OK && rtc_commits==2U);
    assert(rtc_value==UINT64_C(0x4006261031123457));
    assert(amiga_content_service_set_clock(NULL,3000U)==MT_LINK_OK && rtc_commits==2U);
    assert(rom_streams==1U && !core_reset && amiga_content_service_ready());
    assert(rom_first==kick->data[0] && rom_middle==kick->data[0] && rom_last==kick->data[524287U]);
    reject_rom_begin=true;
    assert(amiga_content_service_boot()!=0 && core_reset && load_failure);
    const unsigned old_clears=load_error_clears;native_error_latch=true;
    assert(amiga_content_service_boot()==0 && !core_reset && !load_failure && !native_error_latch);
    assert(load_error_clears==old_clears+1U);
    assert(amiga_content_service_mount(0U,"../bad.adf")!=0);assert(amiga_content_service_retry()==0);
    assert(amiga_content_service_mount(0U,"Workbench.adf")==0);assert(amiga_content_service_mount(2U,"System.hdf")==0);
    floppy_read=true;floppy_track=7U;transmitted_count=0U;amiga_content_service_poll();floppy_read=false;
    assert(transmitted_count==544U);uint8_t decoded[512];unsigned s=99U;
    assert(mt_amiga_mfm_decode(transmitted,7U,&s,decoded) && s==0U && memcmp(decoded,adf->data+7U*11U*512U,512U)==0);
    uint16_t previous=0U;uint8_t written[512];memset(written,0xa7,sizeof(written));
    assert(mt_amiga_mfm_encode(7U,4U,0x4489U,written,fifo,&previous));fifo_count=544U;fifo_position=0U;floppy_write=true;
    fail_journal=true;amiga_content_service_poll();assert(amiga_content_service_faulted());
    ++clock.second;const unsigned before_fault_clock=rtc_commits;
    assert(amiga_content_service_set_clock(&clock,4000U)==MT_LINK_OK && rtc_commits==before_fault_clock+1U);
    assert(amiga_content_service_faulted()); /* RTC did not consume or clear the media error. */
    assert(memcmp(adf->data+(7U*11U+4U)*512U,written,512U)!=0);
    fail_journal=false;assert(amiga_content_service_retry()==0);
    assert(memcmp(adf->data+(7U*11U+4U)*512U,written,512U)==0);
    command(0x20U,0U,1U,1U);amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5e00U);
    for(unsigned i=0U;i<256U;++i)assert(transmitted[i]==(uint16_t)((uint16_t)hdf->data[512U+2U*i]|((uint16_t)hdf->data[513U+2U*i]<<8U)));
    assert(bulk_commands==0U && legacy_data_words==256U);
    bulk_capable=true;command(0x20U,0U,1U,1U);amiga_content_service_poll();
    assert(bulk_commands==1U && legacy_data_words==256U && (ide_regs[5]&0xff00U)==0x5e00U);
    for(unsigned i=0U;i<256U;++i)assert(transmitted[i]==(uint16_t)((uint16_t)hdf->data[512U+2U*i]|((uint16_t)hdf->data[513U+2U*i]<<8U)));
    command(0x30U,0U,2U,1U);amiga_content_service_poll();assert((ide_regs[5]&0xff00U)==0x5800U);
    for(unsigned i=0U;i<256U;++i)fifo[i]=0xbeefU;ide_request=5U;fail_write=true;
    amiga_content_service_poll();assert(amiga_content_service_faulted() && ide_request==5U);
    const unsigned captured_bulk=bulk_commands;
    fail_write=false;assert(amiga_content_service_retry()==0);amiga_content_service_poll();
    assert(bulk_commands==captured_bulk); /* Journal retry never consumes the FIFO twice. */
    assert(ide_request==0U && (ide_regs[5]&0xff00U)==0x5400U);
    for(unsigned i=1024U;i<1536U;i+=2U)assert(hdf->data[i]==0xefU && hdf->data[i+1U]==0xbeU);
    command(0x20U,0U,8U,1U);amiga_content_service_poll();assert((ide_regs[5]&0xff00U)==0x5500U && (ide_regs[0]>>8U)==0x10U && ide_request==0U);
    command(0xecU,1U,0U,1U);amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5500U && (ide_regs[0]>>8U)==4U && ide_request==0U); /* Absent slave completes with IRQ. */
    command(0xffU,0U,0U,1U);amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5500U && (ide_regs[0]>>8U)==4U && ide_request==0U);
    command(0xecU,0U,0U,1U);amiga_content_service_poll();assert(transmitted[0]==0x0040U && transmitted[60]==8U);
    assert(transmitted[47]==0U); /* Unsupported multiple mode is not advertised. */
    /* INITIALIZE DEVICE PARAMETERS uses count/head, not the CHS sector or
     * cylinder registers. A non-default geometry must change both reads and
     * writes, while IDENTIFY reports that exact current translation. */
    command(0x91U,0U,0U,2U);ide_regs[5]=0x91a1U;
    amiga_content_service_poll();assert((ide_regs[5]&0xff00U)==0x5400U);
    command(0x20U,0U,0U,1U);ide_regs[1]=0x0201U;ide_regs[2]=1U;ide_regs[5]=0x20a0U;
    amiga_content_service_poll();
    for(unsigned i=0U;i<256U;++i)assert(transmitted[i]==(uint16_t)((uint16_t)hdf->data[2560U+2U*i]|((uint16_t)hdf->data[2561U+2U*i]<<8U)));
    command(0x30U,0U,0U,1U);ide_regs[1]=0x0201U;ide_regs[2]=0U;ide_regs[5]=0x30a1U;
    amiga_content_service_poll();assert((ide_regs[5]&0xff00U)==0x5800U);
    for(unsigned i=0U;i<256U;++i)fifo[i]=0x8899U;ide_request=5U;amiga_content_service_poll();
    assert(hdf->data[1536U]==0x99U && hdf->data[1537U]==0x88U);
    command(0xecU,0U,0U,0U);ide_regs[5]=0xeca0U;amiga_content_service_poll();
    assert(transmitted[54]==2U && transmitted[55]==2U && transmitted[56]==2U && transmitted[57]==8U && transmitted[60]==8U);
    command(0x91U,0U,0U,0U);ide_regs[5]=0x91a3U;amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5500U && (ide_regs[0]>>8U)==4U);
    command(0xecU,0U,0U,0U);amiga_content_service_poll();assert(transmitted[55]==2U && transmitted[56]==2U);
    command(0x20U,0U,0U,1U);ide_regs[1]=0x0101U;ide_regs[5]=0x20a2U;amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5500U && (ide_regs[0]>>8U)==0x10U); /* Head 2 is invalid for two heads. */
    command(0xc6U,0U,0U,1U);amiga_content_service_poll();assert((ide_regs[5]&0xff00U)==0x5500U && (ide_regs[0]>>8U)==4U);
    test_partition_hardfiles();
    file_t *second=create(ROOT "/Second.hdf",4096U);
    memcpy(second->data,"DOS\1",4U);
    memcpy(second->data+512U,"RDSK",4U);
    assert(amiga_content_service_mount(3U,"Second.hdf")==0);
    command(0x20U,1U,0U,1U);amiga_content_service_poll();
    assert(disk_word(0U)==0x444f5301U); /* Existing RDB keeps its physical layout. */
    command(0xecU,1U,0U,1U);amiga_content_service_poll();assert(transmitted[55]==16U && transmitted[56]==63U);
    command(0xecU,0U,0U,1U);amiga_content_service_poll();assert(transmitted[55]==2U && transmitted[56]==2U);
    /* A partially written PIO block must complete while the guest runs. */
    command(0x30U,0U,3U,1U);amiga_content_service_poll();pio_ticks=5U;pio_progress=0U;
    assert(amiga_content_service_prepare_reset()==0 && pio_progress==5U && native_config[1]==4U);
    assert(hdf->data[1536U]==0xa3U && hdf->data[1537U]==0x45U);
    assert(amiga_content_service_resume_after_reset()==0 && native_config[1]==0U);
    /* A command racing the halt must be allowed to complete, then sampled
     * again under a fresh halt. */
    race_on_halt=true;pio_progress=0U;
    assert(amiga_content_service_prepare_reset()==0 && !race_on_halt && pio_progress==3U && native_config[1]==4U);
    assert(amiga_content_service_resume_after_reset()==0);
    /* A guest which never finishes PIO gets a bounded refusal with its CPU
     * running, and explicit retry can finish the same captured transaction. */
    command(0x30U,0U,4U,1U);amiga_content_service_poll();pio_ticks=100000U;
    assert(amiga_content_service_shutdown()!=0 && native_config[1]==0U);
    assert(strcmp(amiga_content_service_name(0U),"Workbench.adf")==0 && strcmp(amiga_content_service_name(2U),"System.hdf")==0);
    assert(amiga_content_service_faulted());
    /* Refusing an IDE-mode change cannot publish settings or restart while
     * the guest still owns an unfinished write. Mounts remain retryable. */
    mt_amiga_config_t refused=*amiga_content_service_config();refused.ide_compat=MT_AMIGA_IDE_CLASSIC;
    const unsigned before_refusal_roms=rom_streams;pio_ticks=100000U;
    assert(amiga_content_service_configure(&refused)!=0 && rom_streams==before_refusal_roms &&
        amiga_content_service_config()->ide_compat==MT_AMIGA_IDE_AUTO && native_config[1]==0U);
    assert(find(ROOT "/amiga-settings.bin",false)->data[18]==0U && strcmp(amiga_content_service_name(2U),"System.hdf")==0);
    pio_ticks=0U;ide_request=5U;
    for(unsigned i=0U;i<256U;++i)fifo[i]=0x1357U;
    assert(amiga_content_service_retry()==0);amiga_content_service_poll();assert(ide_request==0U);
    assert(hdf->data[2048U]==0x57U && hdf->data[2049U]==0x13U);
    assert(amiga_content_service_shutdown()==0);
    for(unsigned i=0U;i<16U;++i)assert(!handles[i].open);
    assert(amiga_content_service_resume_after_reset()==0);
    assert(strcmp(amiga_content_service_name(0U),"Workbench.adf")==0 && strcmp(amiga_content_service_name(2U),"System.hdf")==0);
    assert(amiga_content_service_shutdown()==0);
    amiga_content_service_init(&link,ROOT,false,progress,NULL);assert(amiga_content_service_boot()==0);
    assert(strcmp(amiga_content_service_name(0U),"Workbench.adf")==0 && strcmp(amiga_content_service_name(2U),"System.hdf")==0);
    assert(amiga_content_service_eject(0U)==0 && amiga_content_service_name(0U)==NULL);
    assert(amiga_content_service_shutdown()==0);
    /* Invalid time and an old core remain bootable without any RTC opcode. */
    const unsigned old_commits=rtc_commits;rtc_capable=false;
    amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_set_clock(&clock,5000U)==MT_LINK_OK);
    assert(amiga_content_service_boot()==0 && rtc_commits==old_commits);
    assert(amiga_content_service_shutdown()==0);rtc_capable=true;clock.month=13U;
    amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_set_clock(&clock,6000U)==MT_LINK_OK);
    assert(amiga_content_service_boot()==0 && rtc_commits==old_commits && !amiga_content_service_faulted());
    ide_request=6U;amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5000U && (ide_regs[0]>>8U)==1U); /* Reset signature has no completion IRQ. */
    assert(amiga_content_service_shutdown()==0);
    /* Existing zero-byte settings choose Auto, but an old FPGA stays classic.
     * The same preference activates fast IDE only on a capable 68020 core. */
    fast_ide_capable=true;amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_boot()==0 && native_config[8]==7U && amiga_content_service_fast_ide_expected());
    /* Fast guest interface retains the same bulk FIFO and durable journal
     * protocol. A failed sync must not ACK or capture the FIFO a second time. */
    command(0x30U,0U,6U,1U);amiga_content_service_poll();
    for(unsigned i=0U;i<256U;++i)fifo[i]=0x2468U;ide_request=5U;fail_write=true;
    amiga_content_service_poll();const unsigned fast_captured=bulk_commands;
    assert(amiga_content_service_faulted() && ide_request==5U);
    fail_write=false;assert(amiga_content_service_retry()==0);amiga_content_service_poll();
    assert(bulk_commands==fast_captured && ide_request==0U && (ide_regs[5]&0xff00U)==0x5400U);
    assert(hdf->data[3072U]==0x68U && hdf->data[3073U]==0x24U);
    command(0x20U,0U,6U,1U);amiga_content_service_poll();
    assert((ide_regs[5]&0xff00U)==0x5e00U);
    for(unsigned i=0U;i<256U;++i)assert(transmitted[i]==0x2468U);
    mt_amiga_config_t ide_config=*amiga_content_service_config();ide_config.ide_compat=MT_AMIGA_IDE_CLASSIC;
    const unsigned before_ide_roms=rom_streams;
    assert(amiga_content_service_configure(&ide_config)==0 && rom_streams==before_ide_roms+1U &&
        native_config[8]==0x27U && staged_config[10]==1U && !amiga_content_service_fast_ide_expected());
    assert(find(ROOT "/amiga-settings.bin",false)->data[18]==1U && strcmp(amiga_content_service_name(2U),"System.hdf")==0);
    assert(amiga_content_service_shutdown()==0);fast_ide_capable=false;
    amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_config()->ide_compat==MT_AMIGA_IDE_CLASSIC && !amiga_content_service_fast_ide_available());
    assert(amiga_content_service_boot()==0 && staged_config[10]==0U && native_config[8]==0x27U);
    assert(find(ROOT "/amiga-settings.bin",false)->data[18]==1U); /* Sanitizing PL wire cannot erase saved policy. */
    ide_config=*amiga_content_service_config();ide_config.ide_compat=MT_AMIGA_IDE_AUTO;
    assert(amiga_content_service_configure(&ide_config)==0 && native_config[8]==0x27U && staged_config[10]==0U);
    fast_ide_capable=true;ide_config.cpu=MT_AMIGA_CPU_STOCK;
    assert(amiga_content_service_configure(&ide_config)==0 && native_config[8]==0x27U && !amiga_content_service_fast_ide_expected());
    ide_config.cpu=MT_AMIGA_CPU_68020_TURBO;
    assert(amiga_content_service_configure(&ide_config)==0 && native_config[8]==7U && amiga_content_service_fast_ide_expected());
    assert(amiga_content_service_shutdown()==0);
    cpu14_capable=true;amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_boot()==0);
    mt_amiga_config_t cpu_config=*amiga_content_service_config();cpu_config.cpu=MT_AMIGA_CPU_68020_14;
    assert(amiga_content_service_configure(&cpu_config)==0 && native_config[4]==0x5fU && native_config[8]==7U);
    assert(amiga_content_service_shutdown()==0);cpu14_capable=false;
    amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_cpu14_fallback() && amiga_content_service_config()->cpu==MT_AMIGA_CPU_68020_TURBO);
    assert(amiga_content_service_boot()==0 && native_config[4]==0x1fU);
    file_t *settings=find(ROOT "/amiga-settings.bin",false);assert(settings!=NULL && settings->data[12]==3U);
    cpu_config=*amiga_content_service_config();cpu_config.fast_mib=4U;
    assert(amiga_content_service_configure(&cpu_config)==0 && settings->data[12]==3U);
    cpu_config=*amiga_content_service_config();cpu_config.ide_compat=MT_AMIGA_IDE_CLASSIC;
    assert(amiga_content_service_configure(&cpu_config)==0 && settings->data[12]==3U && settings->data[18]==1U);
    cpu_config.ide_compat=MT_AMIGA_IDE_AUTO;
    assert(amiga_content_service_configure(&cpu_config)==0 && settings->data[12]==3U && settings->data[18]==0U);
    cpu_config.cpu=MT_AMIGA_CPU_68020_14;
    assert(amiga_content_service_configure(&cpu_config)!=0 && native_config[4]==0x1fU);
    assert(amiga_content_service_retry()==0);
    cpu_config=*amiga_content_service_config();
    assert(amiga_content_service_configure(&cpu_config)==0 && settings->data[12]==2U && !amiga_content_service_cpu14_fallback());
    cpu14_capable=true;cpu_config.cpu=MT_AMIGA_CPU_68020_14;
    assert(amiga_content_service_configure(&cpu_config)==0 && settings->data[12]==3U);
    assert(amiga_content_service_shutdown()==0);
    amiga_content_service_init(&link,ROOT,true,progress,NULL);
    assert(amiga_content_service_aga_pal() && !amiga_content_service_cpu14_available());
    assert(amiga_content_service_boot()!=0 && core_reset && !amiga_content_service_ready());
    assert(strcmp(amiga_content_service_error(),"Cannot open roms/kick-a1200.rom")==0);
    file_t *kick1200=create("0:/multitini/cores/amiga/roms/kick-a1200.rom",524288U);
    kick1200->data[0]=0xa2U;
    assert(amiga_content_service_boot()==0 && rom_first==0xa2U && rom_middle==0xa2U);
    assert(staged_config[4]==MT_AMIGA_CPU_STOCK && staged_config[11]==1U &&
           native_config[3]==0x18U && native_config[4]==0x23U && native_config[8]==0x27U);
    mt_amiga_config_t aga_config=*amiga_content_service_config();
    aga_config.cpu=MT_AMIGA_CPU_68020_TURBO;
    assert(amiga_content_service_configure(&aga_config)==0 &&
           native_config[3]==0x19U && native_config[4]==0x1fU && native_config[8]==7U);
    file_t *aga_settings=find(ROOT "/amiga-aga-settings.bin",false);
    assert(aga_settings!=NULL && aga_settings->data[12]==MT_AMIGA_CPU_68020_TURBO && settings->data[12]==3U);
    assert(amiga_content_service_shutdown()==0);
    amiga_content_service_init(&link,ROOT,true,progress,NULL);
    assert(amiga_content_service_config()->cpu==MT_AMIGA_CPU_68020_TURBO);
    assert(amiga_content_service_shutdown()==0);
    cpu14_capable=true;amiga_content_service_init(&link,ROOT,false,progress,NULL);
    assert(amiga_content_service_boot()==0 && native_config[4]==0x5fU && !amiga_content_service_cpu14_fallback());
    mt_amiga_ide_bench_t benchmark;uint16_t saved_fifo[256];memcpy(saved_fifo,fifo,sizeof(saved_fifo));
    uint32_t reads_before,writes_before,reads_after,writes_after;
    amiga_content_service_activity(2U,&reads_before,&writes_before);const uint16_t saved_status=ide_regs[5];
    assert(amiga_content_service_ide_bench(0U,bench_timer,NULL,&benchmark)!=0);
    assert(amiga_content_service_ide_bench(257U,bench_timer,NULL,&benchmark)!=0);
    memcpy(expected_restart_fifo,saved_fifo,sizeof(saved_fifo));bench_restart_expected=true;
    const unsigned before_bench_roms=rom_streams;
    assert(amiga_content_service_ide_bench(2U,bench_timer,NULL,&benchmark)==0);
    assert(!bench_restart_expected && bench_restarts==1U && rom_streams==before_bench_roms+1U && !core_reset);
    assert(amiga_content_service_config()->cpu==MT_AMIGA_CPU_68020_14 && native_config[4]==0x5fU);
    assert(strcmp(amiga_content_service_name(2U),"System.hdf")==0 && settings->data[12]==3U);
    assert(!bench_timed && native_config[1]==0U && benchmark.bytes_per_leg==1024U);
    for(unsigned b=0U;b<2U;++b)for(unsigned direction=0U;direction<2U;++direction)assert(benchmark.ticks[b][direction]==20U);
    assert(memcmp(saved_fifo,fifo,sizeof(saved_fifo))==0 && ide_regs[5]==saved_status);
    amiga_content_service_activity(2U,&reads_after,&writes_after);assert(reads_before==reads_after && writes_before==writes_after);
    /* Active guest PIO is drained before the first timer invocation. */
    command(0x30U,0U,3U,1U);pio_ticks=3U;pio_progress=0U;
    for(unsigned i=0U;i<256U;++i)expected_restart_fifo[i]=0x45a3U;
    bench_restart_expected=true;
    assert(amiga_content_service_ide_bench(1U,bench_timer,NULL,&benchmark)==0 && pio_progress==3U);
    assert(!bench_restart_expected && bench_restarts==2U && rom_streams==before_bench_roms+2U && !core_reset);
    assert(native_config[1]==0U && ide_request==0U && !bench_timed);
    amiga_content_service_activity(2U,&reads_after,&writes_after);assert(writes_after==writes_before+1U);
    /* Ambiguous transport completion cannot ACK, fall back, replay or close
     * mounted images. Recovery requires an explicit core reload. */
    bulk_timeout=true;const unsigned before_uncertain=bulk_commands;
    assert(amiga_content_service_ide_bench(1U,bench_timer,NULL,&benchmark)!=0);
    assert(bench_restarts==2U && rom_streams==before_bench_roms+2U);
    const unsigned old_words=legacy_data_words;
    assert(amiga_content_service_faulted() && bulk_commands==before_uncertain+1U && ide_request==0U && native_config[1]==4U);
    assert(amiga_content_service_retry()!=0 && amiga_content_service_prepare_reset()!=0 && amiga_content_service_shutdown()!=0);
    assert(bulk_commands==before_uncertain+1U && legacy_data_words==old_words);
    assert(strcmp(amiga_content_service_name(2U),"System.hdf")==0 && settings->data[12]==3U);
    return 0;
}
