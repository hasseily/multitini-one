/* SPDX-License-Identifier: GPL-3.0-only */
#include "amiga_content_service.h"
#include "sd_storage.h"
#include <stdio.h>
#include <string.h>
#define AM_POLLS 100000U
#define AM_PATH 512U
#define AM_NAME (MT_AMIGA_MEDIA_NAME_MAX+1U)
#define AM_SETTINGS (24U+MT_AMIGA_DRIVE_COUNT*AM_NAME)
#define AM_READ_AHEAD_SECTORS 32U
typedef struct {
    mt_sd_file_t *file;
    uint64_t size; /* Physical file size, including in journal records. */
    mt_amiga_hardfile_t hardfile;
    uint16_t heads,sectors;
    bool writable,pending,uncommitted;
    uint8_t retained[544];
    unsigned read_track,read_sector,write_track,write_used;
    uint16_t previous;
    uint32_t reads,writes;
    char name[AM_NAME],path[AM_PATH],journal[AM_PATH],recovery[AM_PATH];
} am_drive_t;
static mt_core_link_t *link;
static mt_link_progress_fn progress;
static void *progress_context;
static mt_amiga_config_t config;
static uint8_t preferred_cpu;
static am_drive_t drives[MT_AMIGA_DRIVE_COUNT];
static char root[256],rom_root[256],error[160],remembered[MT_AMIGA_DRIVE_COUNT][AM_NAME];
static uint8_t rom[MT_AMIGA_ROM_BYTES],sector[512],journal_wire[544],settings_wire[AM_SETTINGS];
static uint8_t pfs_handler[MT_AMIGA_PFS_HANDLER_MAX];
static size_t pfs_handler_bytes;
static uint16_t mfm[MT_AMIGA_MFM_SECTOR_WORDS],write_mfm[2][MT_AMIGA_MFM_SECTOR_WORDS];
static bool ready,faulted,paused,settings_invalid,bulk_failed;
static bool aga_pal;
static mt_amiga_rtc_t rtc;
static mt_clock_time_t cached_clock;
static bool cached_clock_valid;
static uint32_t clock_now_ms;
static struct { uint32_t lba,left;uint8_t command,drive,task_head;bool active,captured,lba_mode; } ata;
/* One command's consecutive HDF sectors. No speculative reads past its ATA
 * count, no floppy/IDENTIFY cache, and no dirty/write-back data. */
_Alignas(32) static uint8_t read_ahead[AM_READ_AHEAD_SECTORS*MT_AMIGA_SECTOR_BYTES];
static struct { am_drive_t *drive;mt_sd_file_t *file;uint32_t lba;unsigned used,count; } read_cache;
static void invalidate_read_cache(void)
{ read_cache.drive=NULL;read_cache.file=NULL;read_cache.used=0U;read_cache.count=0U; }
static mt_amiga_ide_profile_t profile;
static uint64_t (*profile_timer)(void *);
static void *profile_context;
static uint64_t profile_started,profile_last,profile_service_end,profile_drq;
static uint64_t profile_poll_before,profile_last_zero_before;
static bool profile_in_service,profile_ata,profile_drq_pending;
static bool profile_zero_seen;
static bool profile_native_capable,profile_native_active,profile_native_faulted;
static uint16_t profile_native_sequence;
static uint32_t profile_next_lba[2];
static bool profile_lba_seen[2];
static uint64_t profile_now(void)
{
    if(profile.running) {
        profile_last=profile_timer(profile_context);
        profile.elapsed_ticks=profile_last-profile_started; /* unsigned timer wrap */
    }
    return profile_last;
}
static void profile_add(uint64_t *value,uint64_t amount)
{
    if(profile.reason==MT_AMIGA_PROFILE_OVERFLOW)return;
    if(UINT64_MAX-*value<amount) {
        *value=UINT64_MAX;profile.running=false;profile.valid=false;profile.reason=MT_AMIGA_PROFILE_OVERFLOW;
    } else *value+=amount;
}
static void profile_span(uint64_t *total,uint64_t *maximum,uint64_t elapsed)
{ profile_add(total,elapsed);if(elapsed>*maximum)*maximum=elapsed; }
void amiga_content_service_profile_stop(void)
{
    if(profile.running) {
        const uint64_t now=profile_now();
        if(!profile_in_service)profile_span(&profile.outside_ticks,&profile.max_gap_ticks,now-profile_service_end);
        profile.running=false;
        if(profile.valid)profile.reason=MT_AMIGA_PROFILE_STOPPED;
    }
    if(profile_native_active) {
        /* Stop ARM timing before diagnostic commands; no UART or native
         * snapshot transport is charged as measured service/guest delay. */
        profile_native_active=false;
        profile.native_result=mt_amiga_diag_control(link,profile_native_sequence,MT_AMIGA_DIAG_STOP,AM_POLLS);
        if(profile.native_result==MT_LINK_OK)
            profile.native_result=mt_amiga_diag_read(link,profile_native_sequence,AM_POLLS,&profile.native);
        profile.native_valid=profile.native_result==MT_LINK_OK && (profile.native.status&MT_AMIGA_DIAG_VALID)!=0U;
        if(profile.native_result!=MT_LINK_OK)profile_native_faulted=true;
    }
}
static void profile_invalidate(mt_amiga_profile_reason_t reason)
{
    if(!profile.running && !profile_native_active)return;
    amiga_content_service_profile_stop();profile.valid=false;
    if(profile.reason!=MT_AMIGA_PROFILE_OVERFLOW)profile.reason=reason;
}
int amiga_content_service_profile_start(uint64_t (*timer)(void *),void *context)
{
    if(timer==NULL || profile.running || profile_native_active || profile_native_faulted || !ready || paused || faulted)return -1;
    memset(&profile,0,sizeof(profile));profile_timer=timer;profile_context=context;
    profile.native_available=profile_native_capable;profile.native_result=MT_LINK_ABI_MISMATCH;
    if(profile_native_capable) {
        if(++profile_native_sequence==0U)profile_native_sequence=1U;
        profile.native_result=mt_amiga_diag_control(link,profile_native_sequence,MT_AMIGA_DIAG_ARM,AM_POLLS);
        if(profile.native_result!=MT_LINK_OK) {
            profile_native_faulted=true;profile.reason=MT_AMIGA_PROFILE_IO_ERROR;return -1;
        }
        profile_native_active=true;
    }
    profile_started=profile_last=profile_service_end=timer(context);
    profile_in_service=false;profile_ata=false;profile_drq_pending=false;profile_zero_seen=false;
    memset(profile_lba_seen,0,sizeof(profile_lba_seen));
    profile.running=true;profile.valid=true;profile.reason=MT_AMIGA_PROFILE_RUNNING;return 0;
}
void amiga_content_service_profile_snapshot(mt_amiga_ide_profile_t *out)
{
    if(out==NULL)return;
    const uint64_t now=profile_now();*out=profile;
    if(profile.running && !profile_in_service) {
        const uint64_t gap=now-profile_service_end;
        if(UINT64_MAX-out->outside_ticks<gap) {
            profile.running=false;profile.valid=false;profile.reason=MT_AMIGA_PROFILE_OVERFLOW;
            profile.outside_ticks=UINT64_MAX;*out=profile;
        }
        else out->outside_ticks+=gap;
        if(gap>out->max_gap_ticks)out->max_gap_ticks=gap;
    }
}
static uint32_t crc32(const uint8_t *p,size_t n)
{ uint32_t c=UINT32_MAX;for(size_t i=0U;i<n;++i) { c^=p[i];for(unsigned b=0U;b<8U;++b)c=(c>>1U)^((0U-(c&1U))&UINT32_C(0xedb88320)); }return ~c; }
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U); }
static void put32(uint8_t *p,uint32_t v)
{ for(unsigned i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(i*8U)); }
static int fail(const char *message)
{ invalidate_read_cache();profile_invalidate(MT_AMIGA_PROFILE_IO_ERROR);(void)snprintf(error,sizeof(error),"%s",message);faulted=true;return -1; }
static int sd_fail(mt_sd_result_t r)
{ return fail(mt_sd_status_name(r.status)); }
static bool path(char *out,const char *name)
{ const int n=snprintf(out,AM_PATH,"%s/%s",root,name);return n>0 && (size_t)n<AM_PATH; }
static int host(uint8_t op,uint32_t payload,uint16_t *out)
{
    uint32_t value=0U;const mt_link_result_t r=mt_amiga_host_access(link,op,payload,AM_POLLS,&value);
    if(r!=MT_LINK_OK)return fail(mt_link_result_name(r));
    if(out!=NULL)*out=(uint16_t)value;
    return 0;
}
static int select_io(unsigned pins)
{ return host(MT_AMIGA_HOST_SELECT,pins,NULL); }
static int xfer(uint16_t input,uint16_t *output)
{ return host(MT_AMIGA_HOST_TRANSFER,input,output); }
static int uio(uint8_t reg,uint16_t value)
{
    int r=select_io(2U);if(r==0)r=xfer((uint16_t)(0xf0U|reg),NULL);if(r==0)r=xfer(value,NULL);
    const int end=select_io(0U);return r==0 ? end:r;
}
static int reset(bool asserted)
{
    const uint32_t args[4]={asserted ? 1U:0U,0U,0U,0U};mt_command_response_t response;
    const mt_link_result_t r=mt_core_link_command(link,MT_COMMAND_CORE_RESET,args,AM_POLLS,progress,progress_context,&response);
    return r==MT_LINK_OK ? 0:fail(mt_link_result_name(r));
}
static int drive_status(void)
{
    uint16_t status=0x1000U;
    for(unsigned i=0U;i<2U;++i)if(drives[i].file!=NULL)status|=(uint16_t)((1U<<i)|(drives[i].writable ? 16U<<i:0U));
    int r=select_io(1U);if(r==0)r=xfer(status,NULL);const int end=select_io(0U);return r==0 ? end:r;
}
static int ide_read(unsigned address,uint16_t *value)
{ return host(MT_AMIGA_HOST_IDE_READ,address<<16U,value); }
static int ide_write(unsigned address,uint16_t value)
{
    const int result=host(MT_AMIGA_HOST_IDE_WRITE,(address<<16U)|value,NULL);
    if(result==0 && profile.running && profile_ata && address==5U && (value&0x0800U)!=0U) {
        profile_drq=profile_now();profile_drq_pending=true;profile_zero_seen=false;
    }
    return result;
}
static int ide_presence(void)
{
    const uint16_t present=(uint16_t)(0x388U|(drives[2].file!=NULL ? 1U:0U)|(drives[3].file!=NULL ? 16U:0U));
    return ide_write(6U,present);
}
static bool settings_paths(char target[AM_PATH],char journal[AM_PATH])
{
    return path(target,aga_pal ? "amiga-aga-settings.bin":"amiga-settings.bin") &&
        path(journal,aga_pal ? "amiga-aga-settings.bin.mtj":"amiga-settings.bin.mtj");
}
static int settings_save(void)
{
    if(settings_invalid)return fail("Amiga settings invalid; retain file for recovery");
    char target[AM_PATH],journal[AM_PATH];if(!settings_paths(target,journal))return fail("Amiga settings path too long");
    memset(settings_wire,0,sizeof(settings_wire));memcpy(settings_wire,"MTAMSET1",8U);
    mt_amiga_config_t saved=config;saved.cpu=preferred_cpu;
    mt_amiga_config_encode(&saved,settings_wire+8U);
    for(unsigned i=0U;i<MT_AMIGA_DRIVE_COUNT;++i)memcpy(settings_wire+24U+i*AM_NAME,remembered[i],AM_NAME);
    /* AM01 reserved bytes12..15 are checksum storage in the enclosing file. */
    put32(settings_wire+20U,crc32(settings_wire,sizeof(settings_wire)));
    const mt_sd_result_t r=mt_sd_storage_journal_replace(target,journal,settings_wire,sizeof(settings_wire));
    return r.status==MT_SD_OK ? 0:sd_fail(r);
}
static void settings_load(void)
{
    char target[AM_PATH],journal[AM_PATH];bool found=false;
    if(!settings_paths(target,journal)) { settings_invalid=true;(void)fail("Amiga settings path too long");return; }
    mt_sd_result_t r=mt_sd_storage_journal_recover(target,journal,settings_wire,sizeof(settings_wire));
    if(r.status!=MT_SD_OK) { settings_invalid=true;(void)sd_fail(r);return; }
    r=mt_sd_storage_read_file_optional_max(target,settings_wire,sizeof(settings_wire),&found);
    if(r.status!=MT_SD_OK) { settings_invalid=true;(void)sd_fail(r);return; }
    if(!found)return;
    const uint32_t expected=le32(settings_wire+20U);put32(settings_wire+20U,0U);
    mt_amiga_config_t candidate;
    bool valid=r.size==sizeof(settings_wire) && memcmp(settings_wire,"MTAMSET1",8U)==0 &&
        expected==crc32(settings_wire,sizeof(settings_wire)) && mt_amiga_config_decode(settings_wire+8U,&candidate);
    if(valid && aga_pal && candidate.cpu==MT_AMIGA_CPU_68020_14)valid=false;
    for(unsigned i=0U;valid && i<MT_AMIGA_DRIVE_COUNT;++i) {
        const char *name=(const char *)settings_wire+24U+i*AM_NAME;
        valid=memchr(name,0,AM_NAME)!=NULL && (*name=='\0' || mt_amiga_media_path_valid(name));
    }
    if(!valid) { settings_invalid=true;(void)fail("Amiga settings checksum or values invalid");return; }
    config=candidate;for(unsigned i=0U;i<MT_AMIGA_DRIVE_COUNT;++i)memcpy(remembered[i],settings_wire+24U+i*AM_NAME,AM_NAME);
}
static int apply_journal(am_drive_t *d)
{
    invalidate_read_cache();
    bool found=false;d->pending=true;
    if(d->uncommitted) {
        const mt_sd_result_t saved=mt_sd_storage_journal_replace(d->journal,d->recovery,d->retained,sizeof(d->retained));
        if(saved.status!=MT_SD_OK)return sd_fail(saved);
        d->uncommitted=false;
    }
    mt_sd_result_t r=mt_sd_storage_journal_recover(d->journal,d->recovery,journal_wire,sizeof(journal_wire));
    if(r.status!=MT_SD_OK)return sd_fail(r);
    r=mt_sd_storage_read_file_optional_exact(d->journal,journal_wire,sizeof(journal_wire),&found);
    if(r.status!=MT_SD_OK)return sd_fail(r);
    if(!found) { d->pending=false;return 0; }
    const uint32_t expected=le32(journal_wire+28U);put32(journal_wire+28U,0U);
    const uint64_t offset=(uint64_t)le32(journal_wire+8U)|((uint64_t)le32(journal_wire+12U)<<32U);
    const uint64_t bytes=(uint64_t)le32(journal_wire+16U)|((uint64_t)le32(journal_wire+20U)<<32U);
    if(memcmp(journal_wire,"MTAMBLK1",8U)!=0 || crc32(journal_wire,sizeof(journal_wire))!=expected ||
        bytes!=d->size || !d->writable || offset>d->size || d->size-offset<512U || (offset&511U)!=0U)
        return fail("Amiga disk journal invalid; retained for recovery");
    r=mt_sd_file_seek(d->file,(int64_t)offset,SEEK_SET);
    if(r.status==MT_SD_OK)r=mt_sd_file_write(d->file,journal_wire+32U,512U);
    if(r.status!=MT_SD_OK || r.size!=512U)return sd_fail(r.status==MT_SD_OK ? (mt_sd_result_t){MT_SD_WRITE_FAILED,0U,r.size}:r);
    r=mt_sd_file_remove(d->journal);if(r.status!=MT_SD_OK)return sd_fail(r);
    d->pending=false;++d->writes;return 0;
}
static int disk_write(am_drive_t *d,uint32_t lba)
{
    invalidate_read_cache();
    if(lba<d->hardfile.first_lba)return fail("Virtual Amiga partition table is read-only");
    const uint64_t offset=(uint64_t)(lba-d->hardfile.first_lba)*512U;
    if(d->file==NULL || !d->writable || offset>d->size || d->size-offset<512U)return fail("Amiga disk write out of bounds or protected");
    memset(journal_wire,0,sizeof(journal_wire));memcpy(journal_wire,"MTAMBLK1",8U);
    put32(journal_wire+8U,(uint32_t)offset);put32(journal_wire+12U,(uint32_t)(offset>>32U));
    put32(journal_wire+16U,(uint32_t)d->size);put32(journal_wire+20U,(uint32_t)(d->size>>32U));
    memcpy(journal_wire+32U,sector,512U);put32(journal_wire+28U,crc32(journal_wire,sizeof(journal_wire)));
    memcpy(d->retained,journal_wire,sizeof(d->retained));d->pending=true;d->uncommitted=true;
    return apply_journal(d);
}
static int disk_read_backend(am_drive_t *d,uint32_t lba,uint8_t *data,size_t bytes)
{
    const uint64_t offset=(uint64_t)lba*512U;
    if(d->file==NULL || offset>d->size || d->size-offset<bytes)return fail("Amiga disk read out of bounds");
    const bool timed=profile.running && profile_ata;
    const uint64_t began=timed ? profile_now():0U;
    if(timed)profile_add(&profile.sd_read_calls,1U);
    mt_sd_result_t r=mt_sd_file_seek(d->file,(int64_t)offset,SEEK_SET);
    size_t received=0U;
    if(r.status==MT_SD_OK) { r=mt_sd_file_read(d->file,data,bytes);received=r.size; }
    if(timed) {
        profile_span(&profile.sd_read_ticks,&profile.sd_read_max_ticks,profile_now()-began);
        profile_add(&profile.read_bytes,received);
    }
    /* Even a complete prefix of a failed/short batch is not published. A
     * FatFs read error may be sticky; preserve the existing error path. */
    if(r.status!=MT_SD_OK || r.size!=bytes)return sd_fail(r.status==MT_SD_OK ? (mt_sd_result_t){MT_SD_READ_FAILED,0U,r.size}:r);
    return 0;
}
static int disk_read(am_drive_t *d,uint32_t lba)
{
    if(disk_read_backend(d,lba,sector,sizeof(sector))!=0)return -1;
    ++d->reads;return 0;
}
static uint32_t ata_sector_count(const am_drive_t *d)
{
    return (uint32_t)(d->size/512U)+d->hardfile.first_lba;
}

static void ata_default_geometry(am_drive_t *d)
{
    d->heads=d->hardfile.first_lba!=0U ? d->hardfile.heads:16U;
    d->sectors=d->hardfile.first_lba!=0U ? d->hardfile.sectors:63U;
}

static int ata_read_sector(am_drive_t *d,uint32_t lba)
{
    const bool timed=profile.running && profile_ata;
    if(timed && !profile.read_window_started) {
        profile.read_window_started=true;profile.first_read_ticks=profile_now()-profile_started;
    }
    if(read_cache.drive!=d || read_cache.file!=d->file || read_cache.used>=read_cache.count ||
       lba<read_cache.lba || lba-read_cache.lba!=read_cache.used) {
        invalidate_read_cache();
        const uint32_t capacity=ata_sector_count(d);
        if(d->file==NULL || lba>=capacity || ata.left==0U)
            return fail("Amiga disk read out of bounds");
        unsigned count=ata.left<AM_READ_AHEAD_SECTORS ? (unsigned)ata.left:AM_READ_AHEAD_SECTORS;
        if(capacity-lba<count)count=(unsigned)(capacity-lba);
        const uint32_t first=d->hardfile.first_lba;
        unsigned prefix=0U;
        while(prefix<count && lba+prefix<first) {
            mt_amiga_hardfile_sector(&d->hardfile,ata.drive-2U,pfs_handler,lba+prefix,
                read_ahead+(size_t)prefix*512U);
            ++prefix;
        }
        if(prefix<count && disk_read_backend(d,lba+prefix-first,read_ahead+(size_t)prefix*512U,
            (size_t)(count-prefix)*512U)!=0)return -1;
        read_cache.drive=d;read_cache.file=d->file;read_cache.lba=lba;read_cache.count=count;
    }
    memcpy(sector,read_ahead+(size_t)read_cache.used*512U,sizeof(sector));
    if(++read_cache.used==read_cache.count)invalidate_read_cache();
    if(timed) {
        profile_add(&profile.read_sectors,1U);
        const unsigned disk=(unsigned)(d-&drives[2]);
        if(disk<2U) {
            profile_add(profile_lba_seen[disk] && profile_next_lba[disk]==lba ?
                &profile.sequential_reads:&profile.nonsequential_reads,1U);
            profile_lba_seen[disk]=true;profile_next_lba[disk]=lba+1U;
        }
    }
    ++d->reads;return 0;
}
static int floppy_poll(bool *writing)
{
    uint16_t status=0U,sync=0U,count=0U;
    int r=select_io(1U);if(r==0)r=xfer(0U,&status);if(r==0)r=xfer(0U,&sync);if(r==0)r=xfer(0U,&count);
    if(r!=0) { (void)select_io(0U);return r; }
    const unsigned drive=status>>14U,track=status&255U;
    *writing=(status&0x200U)!=0U;
    if(drive>=2U || track>=160U || drives[drive].file==NULL) { (void)select_io(0U);return 0; }
    am_drive_t *const d=&drives[drive];
    if(status&0x100U) {
        /* SD access cannot run while the FIFO transaction is selected. */
        (void)select_io(0U);
        if(d->read_track!=track) { d->read_track=track;d->read_sector=0U;d->previous=0U; }
        if(disk_read(d,track*11U+d->read_sector)!=0)return -1;
        if(sync==0U || sync==0x8914U || sync==0xa144U)sync=0x4489U;
        if(!mt_amiga_mfm_encode(track,d->read_sector,sync,sector,mfm,&d->previous))return fail("Amiga MFM encode failed");
        r=select_io(1U);uint16_t current=0U;
        if(r==0)r=xfer(0U,&current);
        if(r==0)r=xfer(0U,NULL);
        if(r==0)r=xfer(0U,NULL);
        if(r==0 && (current&0xc1ffU)==(status&0xc1ffU)) {
            for(unsigned i=0U;r==0 && i<MT_AMIGA_MFM_SECTOR_WORDS;++i)r=xfer(mfm[i],NULL);
            if(r==0 && d->read_sector==10U)for(unsigned i=0U;r==0 && i<MT_AMIGA_MFM_GAP_WORDS;++i)r=xfer(mt_amiga_mfm_clock(0U,&d->previous),NULL);
            if(r==0)d->read_sector=(d->read_sector+1U)%11U;
        }
    } else if(*writing) {
        if(d->write_track!=track) { d->write_track=track;d->write_used=0U; }
        unsigned available=count&0x3fffU;if(available>1024U)available=1024U;
        for(unsigned i=0U;r==0 && i<available;++i) {
            uint16_t value=0U;r=xfer(0U,&value);if(r!=0)break;
            uint16_t *const wire=write_mfm[drive];
            if(d->write_used<4U) {
                if(value==0x4489U && d->write_used==3U) { wire[2]=value;wire[3]=value;d->write_used=4U; }
                else d->write_used=value==0x4489U ? 3U:0U;
            } else {
                wire[d->write_used++]=value;
                if(d->write_used==MT_AMIGA_MFM_SECTOR_WORDS) {
                    unsigned s=0U;d->write_used=0U;
                    if(mt_amiga_mfm_decode(wire,track,&s,sector)) {
                        (void)select_io(0U);
                        if(disk_write(d,track*11U+s)!=0)return -1;
                        /* Finish this poll after durable commit. The next
                         * transaction takes a fresh FIFO count. */
                        return 0;
                    }
                }
            }
        }
    }
    const int end=select_io(0U);return r==0 ? end:r;
}
static void identify(am_drive_t *d)
{
    memset(sector,0,sizeof(sector));
    const uint32_t count=ata_sector_count(d);
    const unsigned heads=d->hardfile.first_lba!=0U ? d->hardfile.heads:16U;
    const unsigned sectors=d->hardfile.first_lba!=0U ? d->hardfile.sectors:63U;
#define IDW(index,value) do { const uint16_t v=(uint16_t)(value);sector[(index)*2U]=(uint8_t)v;sector[(index)*2U+1U]=(uint8_t)(v>>8U); } while(0)
    uint32_t cylinders=count/(heads*sectors);if(cylinders>16383U)cylinders=16383U;if(cylinders==0U)cylinders=1U;
    IDW(0U,0x0040U);IDW(1U,cylinders);IDW(3U,heads);IDW(6U,sectors);
    /* READ/WRITE MULTIPLE is not implemented: do not advertise it. */
    IDW(47U,0U);IDW(49U,0x0200U);IDW(53U,1U);
    uint32_t current_cylinders=count/((uint32_t)d->heads*d->sectors);
    if(current_cylinders>65535U)current_cylinders=65535U;
    const uint32_t current_capacity=current_cylinders*d->heads*d->sectors;
    IDW(54U,current_cylinders);IDW(55U,d->heads);IDW(56U,d->sectors);
    IDW(57U,current_capacity);IDW(58U,current_capacity>>16U);IDW(60U,count);IDW(61U,count>>16U);
    static const char model[]="MULTITINI AMIGA VIRTUAL IDE DISK          ";
    for(unsigned i=0U;i<40U;i+=2U) { sector[54U+i]=(uint8_t)model[i+1U];sector[55U+i]=(uint8_t)model[i]; }
#undef IDW
}
static int ata_status(uint16_t status,uint8_t err)
{
    if(status&0x0100U)invalidate_read_cache();
    if(status&0x0100U)status|=0x0400U; /* ATA errors complete with IRQ too. */
    int r=ide_write(0U,(uint16_t)(1U|((uint16_t)err<<8U)));
    if(r==0)r=ide_write(5U,(uint16_t)(status|ata.task_head));
    if((status&0x0100U)!=0U)profile_invalidate(MT_AMIGA_PROFILE_IO_ERROR);
    return r;
}
static int ata_data_legacy(bool writing)
{
    if(ide_write(14U,0U)!=0)return -1; /* Reset management FIFO pointer. */
    for(unsigned i=0U;i<256U;++i) {
        uint16_t value=0U;
        if(writing) {
            if(ide_read(15U,&value)!=0)return -1;
            sector[2U*i]=(uint8_t)value;sector[2U*i+1U]=(uint8_t)(value>>8U);
        } else if(ide_write(15U,(uint16_t)((uint16_t)sector[2U*i]|((uint16_t)sector[2U*i+1U]<<8U)))!=0)return -1;
    }
    return 0;
}
static int ata_data_bulk(bool writing)
{
    bool submitted=false;
    const mt_link_result_t result=mt_amiga_bulk_transfer(link,writing,sector,AM_POLLS,&submitted);
    if(result==MT_LINK_OK)return 0;
    bulk_failed=true;
    return fail(submitted ? "Amiga IDE transfer uncertain; reload core":"Amiga IDE bulk unavailable; reload core");
}
static int ata_data(bool writing)
{
    if(bulk_failed)return fail("Amiga IDE transfer uncertain; reload core");
    const bool timed=profile.running && profile_ata;
    const uint64_t began=timed ? profile_now():0U;
    if(timed)profile_add(&profile.fifo_calls,1U);
    const int result=(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_BULK)!=0U ?
        ata_data_bulk(writing):ata_data_legacy(writing);
    if(timed) {
        profile_span(&profile.fifo_ticks,&profile.fifo_max_ticks,profile_now()-began);
        if(result==0)profile_add(&profile.fifo_bytes,512U);
    }
    return result;
}
static int ata_poll(void)
{
    const uint32_t snapshot=link->read32(link->context,MT_AMIGA_HOST_PORT);
    const unsigned request=(snapshot>>16U)&7U;
    if(profile.running) {
        if(request==0U) {
            profile_add(&profile.idle_ata_polls,1U);
            if(profile_drq_pending) {
                /* Reuse the existing pre-ATA timer sample. It precedes this
                 * zero MMIO observation; it is not the assertion timestamp. */
                profile_last_zero_before=profile_poll_before;profile_zero_seen=true;
            }
        }
        else if(request==4U)profile_add(&profile.req4,1U);
        else if(request==5U)profile_add(&profile.req5,1U);
        else if(request==6U)profile_add(&profile.req6,1U);
        if(profile_drq_pending && (request==4U || request==5U)) {
            const uint64_t observed_after=profile_now();
            const uint64_t elapsed=observed_after-profile_drq;
            profile_add(&profile.drq_intervals,1U);
            profile_span(&profile.drq_to_request_ticks,&profile.max_drq_to_request_ticks,elapsed);
            if(profile_zero_seen) {
                profile_add(&profile.poll_bracketed_intervals,1U);
                profile_span(&profile.poll_tail_upper_ticks,&profile.max_poll_tail_upper_ticks,
                    observed_after-profile_last_zero_before);
            } else profile_add(&profile.poll_unbracketed_intervals,1U);
            profile_drq_pending=false;profile_zero_seen=false;
        } else if(request==6U) { profile_drq_pending=false;profile_zero_seen=false; }
    }
    if(request==0U)return 0;
    if(request==6U) {
        invalidate_read_cache();
        profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);
        ata.active=false;ata.captured=false;ata.task_head=0xa0U;
        for(unsigned d=2U;d<MT_AMIGA_DRIVE_COUNT;++d)ata_default_geometry(&drives[d]);
        if(ide_write(1U,0x0101U)!=0 || ide_write(2U,0U)!=0)return -1;
        return ata_status(0x5000U,1U);
    }
    if(request==4U) {
        invalidate_read_cache();
        uint16_t regs[6];for(unsigned i=0U;i<6U;++i)if(ide_read(i,&regs[i])!=0)return -1;
        ata.drive=(uint8_t)(2U+((regs[5]>>4U)&1U));ata.command=(uint8_t)(regs[5]>>8U);
        ata.task_head=(uint8_t)regs[5];
        ata.lba_mode=(regs[5]&0x40U)!=0U;
        ata.left=regs[1]&255U;if(ata.left==0U)ata.left=256U;
        if(profile.running && (ata.command==0x20U || ata.command==0x21U)) {
            profile_add(&profile.read_commands,1U);profile_add(&profile.requested_read_sectors,ata.left);
            unsigned bin=0U,limit=1U;
            while(ata.left>limit && bin<8U) { ++bin;limit<<=1U; }
            profile_add(&profile.read_command_hist[bin],1U);
        }
        if(profile.running && (ata.command==0x30U || ata.command==0x31U))profile_add(&profile.write_commands,1U);
        am_drive_t *const d=&drives[ata.drive];ata.active=false;ata.captured=false;
        if(d->file==NULL)return ata_status(0x5100U,4U);
        if(ata.command==0xecU) { identify(d);if(ata_data(false)!=0)return -1;return ata_status(0x5e00U,0U); }
        if(ata.command==0x91U) {
            const uint16_t sectors=(uint16_t)(regs[1]&255U);
            /* This target supports CHS sectors 1..255. Reject the optional
             * 256-sector encoding rather than acknowledging unusable geometry. */
            if(sectors==0U)return ata_status(0x5100U,4U);
            d->heads=(uint16_t)((regs[5]&15U)+1U);d->sectors=sectors;
            return ata_status(0x5400U,0U);
        }
        if(ata.command==0x20U || ata.command==0x21U || ata.command==0x30U || ata.command==0x31U) {
            if(ata.lba_mode)ata.lba=((uint32_t)(regs[5]&15U)<<24U)|((uint32_t)regs[2]<<8U)|(regs[1]>>8U);
            else {
                const unsigned s=regs[1]>>8U,h=regs[5]&15U;
                if(s==0U || s>d->sectors || h>=d->heads)return ata_status(0x5100U,0x10U);
                ata.lba=((uint32_t)regs[2]*d->heads+h)*d->sectors+s-1U;
            }
            const uint32_t capacity=ata_sector_count(d);
            if(ata.lba>=capacity || ata.left>capacity-ata.lba)return ata_status(0x5100U,0x10U);
            if(ata.command>=0x30U && (!d->writable || ata.lba<d->hardfile.first_lba))
                return ata_status(0x5100U,4U);
            ata.active=true;
            if(ide_write(0U,1U)!=0)return -1;
        } else if(ata.command==0xe7U || ata.command==0x10U || ata.command==0x70U || ata.command==0xefU)
            return ata_status(0x5400U,0U);
        else return ata_status(0x5100U,4U);
    }
    if(!ata.active)return ata_status(0x5100U,4U);
    am_drive_t *const d=&drives[ata.drive];
    if(ata.command>=0x30U) {
        if(request==4U)return ata_status(0x5800U,0U);
        if(!ata.captured) {
            if(ata_data(true)!=0)return -1;
            ata.captured=true;
            if(disk_write(d,ata.lba)!=0)return -1;
        }
        if(d->pending)return -1;
    } else {
        if(ata_read_sector(d,ata.lba)!=0)return -1;
        /* A guest reset can arrive while a synchronous SD batch is in
         * progress. Do not overwrite its pending reset with old data/ACK.
         * This also checks a cache hit before taking native FIFO ownership. */
        if(((link->read32(link->context,MT_AMIGA_HOST_PORT)>>16U)&7U)==6U) {
            invalidate_read_cache();ata.active=false;ata.captured=false;
            profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);return 0;
        }
        if(ata_data(false)!=0)return -1;
    }
    ++ata.lba;--ata.left;
    const uint16_t next_sector=(uint16_t)(ata.lba_mode ? ata.lba&255U:ata.lba%d->sectors+1U);
    const uint16_t next_cylinder=(uint16_t)(ata.lba_mode ? ata.lba>>8U:ata.lba/((uint32_t)d->sectors*d->heads));
    const uint16_t next_head=(uint16_t)(ata.lba_mode ? (ata.lba>>24U)&15U:(ata.lba/d->sectors)%d->heads);
    int r=ide_write(1U,(uint16_t)((ata.left&255U)|(next_sector<<8U)));
    if(r==0)r=ide_write(2U,next_cylinder);
    const uint16_t status=(uint16_t)(ata.command>=0x30U ? (ata.left==0U ? 0x5400U:0x5c00U):(ata.left==0U ? 0x5e00U:0x5c00U));
    if(r==0)r=ide_write(5U,(uint16_t)(status|((ata.lba_mode ? 0xe0U:0xa0U)|((ata.drive-2U)<<4U)|next_head)));
    if(r==0 && profile.running && (ata.command==0x20U || ata.command==0x21U)) {
        profile.last_read_ticks=profile_now()-profile_started;
        profile.active_read_ticks=profile.last_read_ticks-profile.first_read_ticks;
        profile_add(&profile.read_window_bytes,512U);
    }
    ata.captured=false;
    if(ata.left==0U)ata.active=false;
    return r;
}
void amiga_content_service_init(mt_core_link_t *l,const char *media_root,bool use_aga,mt_link_progress_fn p,void *context)
{
    invalidate_read_cache();
    profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);
    profile_native_capable=false;profile_native_faulted=false;
    link=l;progress=p;progress_context=context;memset(drives,0,sizeof(drives));memset(remembered,0,sizeof(remembered));
    root[0]='\0';rom_root[0]='\0';error[0]='\0';ready=false;faulted=false;paused=true;settings_invalid=false;bulk_failed=false;ata.active=false;
    pfs_handler_bytes=0U;
    mt_amiga_rtc_init(&rtc);cached_clock_valid=false;clock_now_ms=0U;
    mt_amiga_config_defaults(&config);
    aga_pal=use_aga;
    if(aga_pal)config.cpu=MT_AMIGA_CPU_STOCK;
    preferred_cpu=config.cpu;
    const size_t n=media_root!=NULL ? strlen(media_root):0U;
    if(n<6U || n>=sizeof(root) || strcmp(media_root+n-6U,"/media")!=0) { (void)fail("Amiga package requires /media content root");return; }
    memcpy(root,media_root,n+1U);memcpy(rom_root,media_root,n-5U);memcpy(rom_root+n-5U,"roms",5U);
    for(unsigned i=0U;i<MT_AMIGA_DRIVE_COUNT;++i) { drives[i].read_track=160U;drives[i].write_track=160U; }
    settings_load();
    preferred_cpu=config.cpu;
    if(config.cpu==MT_AMIGA_CPU_68020_14 && !amiga_content_service_cpu14_available()) {
        config.cpu=MT_AMIGA_CPU_68020_TURBO;
        (void)snprintf(error,sizeof(error),"14 MHz unavailable on this core; using 28 MHz; preference retained");
    }
}
int amiga_content_service_boot(void)
{
    invalidate_read_cache();
    profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);
    if(settings_invalid)return -1;
    if(link==NULL || root[0]=='\0')return fail("Amiga service not initialized");
    if(amiga_content_service_prepare_reset()!=0 || reset(true)!=0)return -1;
    const char *const filename=aga_pal ? "kick-a1200.rom":"kick.rom";
    char name[AM_PATH];const int n=snprintf(name,sizeof(name),"%s/%s",rom_root,filename);
    if(n<=0 || (size_t)n>=sizeof(name))return fail("Kickstart path too long");
    const mt_sd_result_t r=mt_sd_storage_read_file_max(name,rom,sizeof(rom));
    if(r.status==MT_SD_OPEN_FAILED)return fail(aga_pal ?
        "Cannot open roms/kick-a1200.rom":"Cannot open roms/kick.rom");
    if(r.status!=MT_SD_OK)return sd_fail(r);
    if(!mt_amiga_rom_size_valid(r.size))return fail(aga_pal ?
        "kick-a1200.rom must be 256 KiB, 512 KiB or 1 MiB":"kick.rom must be 256 KiB, 512 KiB or 1 MiB");
    for(size_t i=r.size;i<sizeof(rom);++i)rom[i]=rom[i%r.size];
    const mt_link_result_t staged=mt_amiga_stage(link,&config,aga_pal,rom,sizeof(rom),AM_POLLS,progress,progress_context);
    if(staged!=MT_LINK_OK)return fail(mt_link_result_name(staged));
    for(unsigned i=0U;i<MT_AMIGA_DRIVE_COUNT;++i)if(drives[i].file==NULL && remembered[i][0]!='\0') {
        char saved[AM_NAME];memcpy(saved,remembered[i],AM_NAME);
        if(amiga_content_service_mount(i,saved)!=0)return -1;
    }
    (void)mt_amiga_rtc_update(&rtc,link,cached_clock_valid ? &cached_clock:NULL,clock_now_ms,true,AM_POLLS);
    if(drive_status()!=0 || ide_presence()!=0 || uio(1U,0U)!=0 || reset(false)!=0)return -1;
    profile_native_capable=(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_DIAGNOSTICS)!=0U;
    profile_native_faulted=false;
    paused=false;ready=true;faulted=false;error[0]='\0';return 0;
}
void amiga_content_service_poll(void)
{
    if(!ready || paused || faulted)return;
    const bool timed=profile.running;
    const uint64_t began=timed ? profile_now():0U;
    if(timed) {
        profile_span(&profile.outside_ticks,&profile.max_gap_ticks,began-profile_service_end);
        profile_in_service=true;profile_add(&profile.polls,1U);
    }
    bool writing=false;const int floppy_result=floppy_poll(&writing);
    const uint64_t fdd_end=timed ? profile_now():0U;
    if(timed)profile_span(&profile.fdd_ticks,&profile.fdd_max_ticks,fdd_end-began);
    const uint64_t sd_before=profile.sd_read_ticks,fifo_before=profile.fifo_ticks;
    profile_poll_before=fdd_end;
    profile_ata=timed;
    if(floppy_result==0)(void)ata_poll();
    profile_ata=false;
    if(timed) {
        const uint64_t end=profile_now(),ata_ticks=end-fdd_end;
        const uint64_t sd_ticks=profile.sd_read_ticks-sd_before,fifo_ticks=profile.fifo_ticks-fifo_before;
        if(sd_ticks>ata_ticks || fifo_ticks>ata_ticks-sd_ticks) {
            profile.running=false;profile.valid=false;profile.reason=MT_AMIGA_PROFILE_OVERFLOW;
        } else profile_span(&profile.ata_other_ticks,&profile.ata_other_max_ticks,ata_ticks-sd_ticks-fifo_ticks);
        profile_span(&profile.service_ticks,&profile.service_max_ticks,end-began);
        profile_service_end=end;profile_in_service=false;
    }
}
mt_link_result_t amiga_content_service_set_clock(const mt_clock_time_t *time,uint32_t now_ms)
{
    cached_clock_valid=time!=NULL && mt_clock_valid(time);
    if(cached_clock_valid)cached_clock=*time;
    clock_now_ms=now_ms;
    /* Live wall-clock refresh is independent of media errors and guest pause.
     * Before first boot only cache it; boot publishes while CPU reset is held. */
    if(ready)return mt_amiga_rtc_update(&rtc,link,cached_clock_valid ? &cached_clock:NULL,now_ms,false,AM_POLLS);
    return MT_LINK_OK;
}
mt_link_result_t amiga_content_service_clock_status(void) { return rtc.result; }
int amiga_content_service_prepare_reset(void)
{
    invalidate_read_cache();
    profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);
    if(bulk_failed)return fail("Amiga IDE transfer uncertain; reload core");
    if(link==NULL)return -1;
    if(!ready) { paused=true;return 0; }
    if(paused)return 0;
    if(faulted)return -1;
    /* Let a guest finish an in-progress PIO block before halting its CPU.
     * After halt, sample again: a command may have arrived between the idle
     * sample and the halt. Such a command needs the guest running to finish
     * PIO, so release the halt and retry. A refusal always releases our halt. */
    for(unsigned i=0U;i<4096U;++i) {
        bool writing=false;if(floppy_poll(&writing)!=0 || ata_poll()!=0)return -1;
        bool pending=ata.active || ((link->read32(link->context,MT_AMIGA_HOST_PORT)>>16U)&7U)!=0U;
        for(unsigned d=0U;d<MT_AMIGA_DRIVE_COUNT;++d)pending|=drives[d].pending;
        if(!writing && !pending) {
            if(uio(1U,4U)!=0) { (void)uio(1U,0U);return -1; }
            if(floppy_poll(&writing)!=0 || ata_poll()!=0) { (void)uio(1U,0U);return -1; }
            pending=ata.active || ((link->read32(link->context,MT_AMIGA_HOST_PORT)>>16U)&7U)!=0U;
            for(unsigned d=0U;d<MT_AMIGA_DRIVE_COUNT;++d)pending|=drives[d].pending;
            if(!writing && !pending) { invalidate_read_cache();paused=true;return 0; }
            if(uio(1U,0U)!=0)return -1;
        }
        if(progress!=NULL)progress(progress_context);
    }
    (void)uio(1U,0U);
    return fail("Amiga media busy; reset refused");
}
int amiga_content_service_resume_after_reset(void)
{
    invalidate_read_cache();
    if(!ready)return amiga_content_service_boot();
    if(drive_status()!=0 || ide_presence()!=0 || uio(1U,0U)!=0)return -1;
    paused=false;ata.active=false;return 0;
}
static int load_pfs_handler(void)
{
    if(pfs_handler_bytes!=0U)return 0;
    char name[AM_PATH];
    const int length=snprintf(name,sizeof(name),"%s/pfs3aio",rom_root);
    if(length<=0 || (size_t)length>=sizeof(name))return fail("PFS3 handler path too long");
    bool found=false;
    const mt_sd_result_t r=mt_sd_storage_read_file_optional_max(name,pfs_handler,sizeof(pfs_handler),&found);
    if(r.status!=MT_SD_OK)return sd_fail(r);
    if(!found)return 0; /* A handler can also be installed by another RDB disk. */
    static const uint8_t hunk_header[4]={0U,0U,3U,0xf3U};
    if(r.size<sizeof(hunk_header) || (r.size&3U)!=0U || memcmp(pfs_handler,hunk_header,sizeof(hunk_header))!=0)
        return fail("roms/pfs3aio must be an Amiga Hunk filesystem handler");
    pfs_handler_bytes=r.size;
    return 0;
}

static int configure_hardfile(am_drive_t *d)
{
    /* RDB may live anywhere in the first 16 sectors. Leave a real disk's
     * sector numbering alone, even if the first sector has a DOS signature. */
    const size_t scan=d->size<8192U ? (size_t)d->size:8192U;
    if(disk_read_backend(d,0U,read_ahead,scan)!=0)return -1;
    const uint32_t type=mt_amiga_partition_type(read_ahead);
    if(type==0U)return 0;
    for(size_t i=512U;i<scan;i+=512U) {
        if(memcmp(read_ahead+i,"RDSK",4U)==0 || memcmp(read_ahead+i,"DRKS",4U)==0)return 0;
    }
    size_t handler_bytes=0U;
    if(type==MT_AMIGA_PFS3_DOSTYPE) {
        if(load_pfs_handler()!=0)return -1;
        handler_bytes=pfs_handler_bytes;
    }
    if(!mt_amiga_hardfile_layout(&d->hardfile,(uint32_t)(d->size/512U),type,handler_bytes))
        return fail("Partition HDF size has no supported exact IDE geometry");
    return 0;
}

int amiga_content_service_mount(unsigned drive,const char *name)
{
    invalidate_read_cache();
    profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);
    if(drive>=MT_AMIGA_DRIVE_COUNT || !mt_amiga_media_path_valid(name))return fail("Amiga media path invalid");
    am_drive_t *const d=&drives[drive];if(d->file!=NULL)return fail("Eject Amiga disk before mounting another");
    if(!path(d->path,name))return fail("Amiga media path too long");
    const size_t path_size=strlen(d->path);
    if(path_size+9U>sizeof(d->recovery))return fail("Amiga journal path too long");
    memcpy(d->journal,d->path,path_size);memcpy(d->journal+path_size,".amj",5U);
    memcpy(d->recovery,d->journal,path_size+4U);memcpy(d->recovery+path_size+4U,".mtj",5U);
    mt_sd_result_t r=mt_sd_file_open(d->path,"r+b",&d->file);d->writable=r.status==MT_SD_OK;
    if(!d->writable)r=mt_sd_file_open(d->path,"rb",&d->file);
    if(r.status!=MT_SD_OK) { d->file=NULL;return sd_fail(r); }
    r=mt_sd_file_seek(d->file,0,SEEK_END);const int64_t bytes=mt_sd_file_tell(d->file);
    if(r.status!=MT_SD_OK || bytes<0 || mt_amiga_media_validate(drive,(uint64_t)bytes)!=MT_CONTENT_VALID) {
        (void)mt_sd_file_close(d->file);d->file=NULL;return fail("ADF must be 880 KiB; HDF must contain 512-byte sectors");
    }
    d->size=(uint64_t)bytes;
    memset(&d->hardfile,0,sizeof(d->hardfile));
    if(drive>=2U && configure_hardfile(d)!=0) {
        (void)mt_sd_file_close(d->file);d->file=NULL;return -1;
    }
    ata_default_geometry(d);
    if(apply_journal(d)!=0)return -1;
    memcpy(d->name,name,strlen(name)+1U);memcpy(remembered[drive],d->name,AM_NAME);
    d->read_track=160U;d->write_track=160U;d->write_used=0U;
    if(settings_save()!=0 || drive_status()!=0 || ide_presence()!=0)return -1;
    return 0;
}
int amiga_content_service_eject(unsigned drive)
{
    invalidate_read_cache();
    profile_invalidate(MT_AMIGA_PROFILE_LIFECYCLE);
    if(drive>=MT_AMIGA_DRIVE_COUNT)return fail("Amiga drive invalid");
    const bool running=ready && !paused;
    if(running && amiga_content_service_prepare_reset()!=0)return -1;
    am_drive_t *const d=&drives[drive];
    if(d->pending && apply_journal(d)!=0)return -1;
    if(d->file!=NULL) { const mt_sd_result_t r=mt_sd_file_close(d->file);if(r.status!=MT_SD_OK)return sd_fail(r);d->file=NULL; }
    d->name[0]='\0';remembered[drive][0]='\0';
    if(settings_save()!=0 || drive_status()!=0 || ide_presence()!=0)return -1;
    return running ? amiga_content_service_resume_after_reset():0;
}
int amiga_content_service_shutdown(void)
{
    invalidate_read_cache();
    if(amiga_content_service_prepare_reset()!=0)return -1;
    /* A partial close failure must reopen earlier handles on rollback too. */
    ready=false;
    for(unsigned i=0U;i<MT_AMIGA_DRIVE_COUNT;++i) {
        if(drives[i].pending && apply_journal(&drives[i])!=0)return -1;
        if(drives[i].file!=NULL) { const mt_sd_result_t r=mt_sd_file_close(drives[i].file);if(r.status!=MT_SD_OK)return sd_fail(r);drives[i].file=NULL; }
    }
    return 0;
}
int amiga_content_service_retry(void)
{
    invalidate_read_cache();
    if(bulk_failed)return fail("Amiga IDE transfer uncertain; reload core");
    if(settings_invalid)return fail("Amiga settings invalid; retain file for recovery");
    for(unsigned i=0U;i<MT_AMIGA_DRIVE_COUNT;++i)if(drives[i].pending && apply_journal(&drives[i])!=0)return -1;
    (void)mt_amiga_host_access(link,MT_AMIGA_HOST_CLEAR_ERROR,0U,AM_POLLS,NULL);
    faulted=false;error[0]='\0';return 0;
}
int amiga_content_service_configure(const mt_amiga_config_t *candidate)
{
    if(!mt_amiga_config_valid(candidate) || (aga_pal && candidate->cpu==MT_AMIGA_CPU_68020_14))
        return fail("Amiga configuration invalid");
    if(candidate->cpu==MT_AMIGA_CPU_68020_14 && !amiga_content_service_cpu14_available())
        return fail("14 MHz requires an updated Amiga core");
    if(amiga_content_service_prepare_reset()!=0)return -1;
    const mt_amiga_config_t previous=config;const uint8_t previous_cpu=preferred_cpu;
    /* Other option changes retain an unavailable saved14 preference. An
     * explicit reapply of the current CPU/profile accepts its actual speed. */
    if(candidate->cpu!=config.cpu || (candidate->ntsc==config.ntsc &&
        candidate->fast_blitter==config.fast_blitter && candidate->floppy_turbo==config.floppy_turbo &&
        candidate->fast_mib==config.fast_mib && candidate->ide_compat==config.ide_compat))preferred_cpu=candidate->cpu;
    config=*candidate;
    if(settings_save()!=0) { config=previous;preferred_cpu=previous_cpu;return -1; }
    return amiga_content_service_boot();
}
const mt_amiga_config_t *amiga_content_service_config(void) { return &config; }
bool amiga_content_service_aga_pal(void) { return aga_pal; }
int amiga_content_service_ide_bench(unsigned iterations,uint64_t (*timer)(void *),
    void *context,mt_amiga_ide_bench_t *result)
{
    if(result==NULL || timer==NULL || iterations==0U || iterations>256U || !ready || paused ||
        faulted || link==NULL || link->read32==NULL ||
        !(link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_BULK))return -1;
    memset(result,0,sizeof(*result));
    if(amiga_content_service_prepare_reset()!=0)return -1;
    uint8_t original[512],expected[512];
    /* A final READ may still have DRQ data even when ARM has no active job.
     * Management accesses preserve the guest pointer; restore its exact RAM. */
    if(ata_data_legacy(true)!=0)goto uncertain;
    memcpy(original,sector,sizeof(original));
    for(unsigned backend=0U;backend<2U;++backend)for(unsigned iteration=0U;iteration<iterations;++iteration) {
        for(unsigned i=0U;i<512U;++i)expected[i]=(uint8_t)(i^(i>>3U)^(iteration*37U));
        memcpy(sector,expected,sizeof(expected));
        for(unsigned direction=0U;direction<2U;++direction) {
            const uint64_t began=timer(context);
            const int transferred=backend==0U ? ata_data_legacy(direction!=0U):ata_data_bulk(direction!=0U);
            const uint64_t ended=timer(context);
            if(transferred!=0 || ended<began)goto uncertain;
            result->ticks[backend][direction]+=ended-began;
        }
        if(memcmp(sector,expected,sizeof(expected))!=0)goto uncertain;
        /* Cross-read outside the timer so symmetric packing mistakes cannot
         * make either backend's loopback falsely verify itself. */
        if((backend==0U ? ata_data_bulk(true):ata_data_legacy(true))!=0 ||
            memcmp(sector,expected,sizeof(expected))!=0)goto uncertain;
        if(progress!=NULL)progress(progress_context);
    }
    memcpy(sector,original,sizeof(original));
    if(ata_data_legacy(false)!=0)goto uncertain;
    if(ata_data_legacy(true)!=0 || memcmp(sector,original,sizeof(original))!=0)goto uncertain;
    result->bytes_per_leg=(uint32_t)iterations*512U;
    /* The native chip-bus halt cannot safely resume a CPU cycle that began
     * while the bridge was halted. This diagnostic always performs a full
     * guest boot after verified restoration; it does not resume the session. */
    if(amiga_content_service_boot()!=0)return -1;
    return 0;
uncertain:
    bulk_failed=true;
    return fail("Amiga IDE benchmark uncertain; reload core; guest remains halted");
}
bool amiga_content_service_cpu14_available(void)
{ return !aga_pal && link!=NULL && link->read32!=NULL && (link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_CPU14)!=0U; }
bool amiga_content_service_cpu14_fallback(void)
{ return preferred_cpu==MT_AMIGA_CPU_68020_14 && config.cpu!=preferred_cpu; }
bool amiga_content_service_fast_ide_available(void)
{ return link!=NULL && link->read32!=NULL && (link->read32(link->context,MT_AMIGA_HOST_PORT)&MT_AMIGA_HOST_FAST_IDE)!=0U; }
bool amiga_content_service_fast_ide_expected(void)
{ return mt_amiga_fast_ide_enabled(&config,amiga_content_service_fast_ide_available() ? MT_AMIGA_HOST_FAST_IDE:0U); }
const char *amiga_content_service_name(unsigned drive) { return drive<MT_AMIGA_DRIVE_COUNT && drives[drive].file!=NULL ? drives[drive].name:NULL; }
const char *amiga_content_service_error(void) { return error; }
bool amiga_content_service_ready(void) { return ready; }
bool amiga_content_service_faulted(void) { return faulted; }
void amiga_content_service_activity(unsigned drive,uint32_t *reads,uint32_t *writes)
{ if(reads!=NULL)*reads=drive<MT_AMIGA_DRIVE_COUNT ? drives[drive].reads:0U;if(writes!=NULL)*writes=drive<MT_AMIGA_DRIVE_COUNT ? drives[drive].writes:0U; }
