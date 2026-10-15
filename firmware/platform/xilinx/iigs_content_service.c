/* SPDX-License-Identifier: GPL-3.0-only */
#include "iigs_content_service.h"
#include "sd_storage.h"
#include <stdio.h>
#include <string.h>
#define GS_POLLS UINT32_C(100000)
#define GS_PATH 256U
#define GS_NAME (MT_IIGS_MEDIA_NAME_MAX+1U)
#define GS_JOURNAL_BYTES (32U+MT_IIGS_SECTOR_BYTES)
#define GS_SETTINGS_BYTES (24U+MT_IIGS_DRIVE_COUNT*GS_NAME)
typedef struct {
    mt_sd_file_t *file;
    mt_iigs_media_info_t info;
    uint64_t file_size;
    uint32_t generation;
    bool mounted;
    bool woz_crc_disabled;
    char name[GS_NAME], path[GS_PATH], journal[GS_PATH], recovery[GS_PATH];
} gs_drive_t;
static mt_core_link_t *link;
static mt_link_progress_fn progress;
static void *progress_context;
static gs_drive_t drives[MT_IIGS_DRIVE_COUNT];
static char root[128], rom_root[128], error[128];
static uint8_t rom_buffer[MT_SIZE_IIGS_ROM3], pram[1024], sector[512];
static uint8_t journal_wire[GS_JOURNAL_BYTES], settings_wire[GS_SETTINGS_BYTES];
static char remembered[MT_IIGS_DRIVE_COUNT][GS_NAME];
static mt_iigs_config_t config;
static uint32_t speed_mask;
static uint32_t next_generation, pram_generation, seen_id, seen_generation;
static uint32_t completion_id, completion_status, completion_blocks, completion_generation;
static bool ready, faulted, seen, completion_pending, pram_mounted, pram_exists, reset_pending;
static bool staged_write;
static uint32_t staged_id, staged_generation;
static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0U;i<n;++i) {
        c^=p[i];
        for(unsigned b=0U;b<8U;++b)c=(c>>1)^((0U-(c&1U))&UINT32_C(0xedb88320));
    }
    return ~c;
}
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void store32(uint8_t *p,uint32_t v)
{ for(unsigned i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(i*8U)); }
static void tick(void) { if(progress!=NULL)progress(progress_context); }
static int fail(const char *text)
{ (void)snprintf(error,sizeof(error),"%s",text); faulted=true; return -1; }
static int sd_fail(mt_sd_result_t result)
{ return fail(mt_sd_status_name(result.status)); }
static bool safe_name(const char *name)
{
    return mt_iigs_media_path_valid(name);
}
static bool path(char *out,const char *name)
{ return snprintf(out,GS_PATH,"%s/%s",root,name)>0 && strlen(root)+strlen(name)+2U<=GS_PATH; }
static mt_link_result_t command(uint32_t opcode,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    const uint32_t args[4]={a,b,c,d};
    for(unsigned attempt=0U;attempt<16U;++attempt) {
        mt_command_response_t response={0};
        mt_link_result_t r=mt_core_link_command(link,opcode,args,GS_POLLS,progress,progress_context,&response);
        if(r!=MT_LINK_REMOTE_ERROR || response.status!=MT_RESPONSE_BUSY)return r;
    }
    return MT_LINK_TIMEOUT;
}
static uint32_t generation(void)
{ uint32_t g=next_generation++; if(g==0U)g=next_generation++; return g; }
static int send_config(const mt_iigs_config_t *c)
{
    uint8_t wire[MT_IIGS_CONFIG_BYTES];
    mt_iigs_config_encode(c,wire);
    const mt_link_result_t r=mt_core_link_stream(link,MT_LOAD_IIGS_CONFIG,wire,sizeof(wire),GS_POLLS,progress,progress_context);
    return r==MT_LINK_OK ? 0:fail(mt_link_result_name(r));
}
static int settings_save(void)
{
    char target[GS_PATH], journal[GS_PATH];
    if(!path(target,"gs-settings.bin") || !path(journal,"gs-settings.bin.mtj"))return fail("Settings path too long");
    memset(settings_wire,0,sizeof(settings_wire));
    memcpy(settings_wire,"MTGSSET1",8U);
    mt_iigs_config_t saved=config; saved.timestamp=0U; saved.pulses&=1U;
    mt_iigs_config_encode(&saved,settings_wire+8U);
    for(unsigned i=0U;i<MT_IIGS_DRIVE_COUNT;++i)
        memcpy(settings_wire+24U+i*GS_NAME,remembered[i],GS_NAME);
    /* Reserved config bytes12..15 carry the enclosing record checksum. */
    store32(settings_wire+20U,crc32(settings_wire,sizeof(settings_wire)));
    mt_sd_result_t r=mt_sd_storage_journal_replace(target,journal,settings_wire,sizeof(settings_wire));
    return r.status==MT_SD_OK ? 0:sd_fail(r);
}
static void settings_load(void)
{
    char target[GS_PATH], journal[GS_PATH]; bool found=false;
    if(!path(target,"gs-settings.bin") || !path(journal,"gs-settings.bin.mtj"))return;
    mt_sd_result_t r=mt_sd_storage_journal_recover(target,journal,settings_wire,sizeof(settings_wire));
    if(r.status!=MT_SD_OK) { (void)sd_fail(r); return; }
    r=mt_sd_storage_read_file_optional_exact(target,settings_wire,sizeof(settings_wire),&found);
    if(r.status!=MT_SD_OK) { (void)sd_fail(r); return; }
    if(!found)return;
    uint32_t saved_crc=le32(settings_wire+20U); store32(settings_wire+20U,0U);
    mt_iigs_config_t candidate={settings_wire[8],settings_wire[9],settings_wire[10],settings_wire[11],0U,settings_wire[16]};
    if(memcmp(settings_wire,"MTGSSET1",8U)!=0 || crc32(settings_wire,sizeof(settings_wire))!=saved_crc ||
        !mt_iigs_config_valid(&candidate) || candidate.card==1U || (candidate.pulses&~1U)!=0U) {
        (void)fail("Apple IIgs settings checksum or values invalid"); return;
    }
    for(unsigned i=0U;i<MT_IIGS_DRIVE_COUNT;++i) {
        const char *name=(const char *)settings_wire+24U+i*GS_NAME;
        if(memchr(name,0,GS_NAME)==NULL || (name[0]!='\0' && !safe_name(name))) {
            (void)fail("Apple IIgs remembered media name invalid"); return;
        }
    }
    config=candidate;
    for(unsigned i=0U;i<MT_IIGS_DRIVE_COUNT;++i)memcpy(remembered[i],settings_wire+24U+i*GS_NAME,GS_NAME);
}
/* One durable write-ahead sector record per image. This avoids duplicating
 * large hard disks while keeping interrupted writes replayable at mount. */
static int apply_journal(gs_drive_t *d)
{
    bool found=false;
    mt_sd_result_t r=mt_sd_storage_journal_recover(d->journal,d->recovery,journal_wire,sizeof(journal_wire));
    if(r.status!=MT_SD_OK)return sd_fail(r);
    r=mt_sd_storage_read_file_optional_exact(d->journal,journal_wire,sizeof(journal_wire),&found);
    if(r.status!=MT_SD_OK)return sd_fail(r);
    if(!found)return 0;
    const uint32_t checksum=le32(journal_wire+28U); store32(journal_wire+28U,0U);
    const uint64_t offset=(uint64_t)le32(journal_wire+8U)|((uint64_t)le32(journal_wire+12U)<<32);
    const uint64_t image_size=(uint64_t)le32(journal_wire+16U)|((uint64_t)le32(journal_wire+20U)<<32);
    const uint32_t count=le32(journal_wire+24U);
    if(memcmp(journal_wire,"MTGSBLK1",8U)!=0 || crc32(journal_wire,sizeof(journal_wire))!=checksum ||
        image_size!=d->file_size || count==0U || count>512U || offset>d->file_size ||
        count>d->file_size-offset)return fail("Disk sector journal invalid; retained for recovery");
    r=mt_sd_file_seek(d->file,(int64_t)offset,SEEK_SET); if(r.status!=MT_SD_OK)return sd_fail(r);
    r=mt_sd_file_write(d->file,journal_wire+32U,count); if(r.status!=MT_SD_OK)return sd_fail(r);
    r=mt_sd_file_remove(d->journal); return r.status==MT_SD_OK ? 0:sd_fail(r);
}
static int write_sector(gs_drive_t *d,uint64_t offset,size_t count)
{
    memset(journal_wire,0,sizeof(journal_wire)); memcpy(journal_wire,"MTGSBLK1",8U);
    store32(journal_wire+8U,(uint32_t)offset); store32(journal_wire+12U,(uint32_t)(offset>>32));
    store32(journal_wire+16U,(uint32_t)d->file_size); store32(journal_wire+20U,(uint32_t)(d->file_size>>32));
    store32(journal_wire+24U,(uint32_t)count); memcpy(journal_wire+32U,sector,count);
    store32(journal_wire+28U,crc32(journal_wire,sizeof(journal_wire)));
    mt_sd_result_t r=mt_sd_storage_journal_replace(d->journal,d->recovery,journal_wire,sizeof(journal_wire));
    if(r.status!=MT_SD_OK)return sd_fail(r);
    return apply_journal(d);
}
static int disable_woz_crc(gs_drive_t *d)
{
    if(d->info.kind!=MT_IIGS_MEDIA_WOZ || d->woz_crc_disabled)return 0;
    uint8_t request_data[512]; memcpy(request_data,sector,sizeof(request_data));
    const size_t count=d->file_size<512U ? (size_t)d->file_size:512U;
    mt_sd_result_t sd=mt_sd_file_seek(d->file,0,SEEK_SET);
    if(sd.status==MT_SD_OK)sd=mt_sd_file_read(d->file,sector,count);
    int result;
    if(sd.status!=MT_SD_OK || sd.size!=count)result=sd_fail(sd);
    else {
        memset(sector+8U,0,4U);
        result=write_sector(d,0U,count);
        if(result==0)d->woz_crc_disabled=true;
    }
    memcpy(sector,request_data,sizeof(request_data)); return result;
}
static int pram_save(void)
{
    char target[GS_PATH], journal[GS_PATH];
    if(!path(target,"pram.bin") || !path(journal,"pram.bin.mtj"))return fail("PRAM path too long");
    mt_sd_result_t r=mt_sd_storage_journal_replace(target,journal,pram,sizeof(pram));
    if(r.status!=MT_SD_OK)return sd_fail(r);
    pram_exists=true;
    return 0;
}
static int complete_pending(void)
{
    if(!completion_pending)return 0;
    const mt_link_result_t r=command(MT_COMMAND_MEDIA_COMPLETE,completion_id,completion_status,completion_blocks,completion_generation);
    if(r==MT_LINK_OK) { completion_pending=false; return 0; }
    if((link->read32(link->context,MT_REG_MEDIA_REQUEST)&MT_MEDIA_REQUEST_VALID)==0U ||
        link->read32(link->context,MT_REG_MEDIA_REQUEST_ID)!=completion_id ||
        link->read32(link->context,MT_REG_MEDIA_REQUEST_GENERATION)!=completion_generation) {
        completion_pending=false; return 0;
    }
    return fail(mt_link_result_name(r));
}
static int transfer(bool writing)
{
    if(!writing) {
        const mt_link_result_t r=mt_core_link_stream(link,0x48U,sector,sizeof(sector),GS_POLLS,progress,progress_context);
        return r==MT_LINK_OK ? 0:fail(mt_link_result_name(r));
    }
    for(unsigned i=0U;i<512U;++i) {
        uint32_t polls=0U;
        if(writing) {
            uint32_t word;
            do { word=link->read32(link->context,MT_REG_MEDIA_DATA); tick(); }
            while((word&MT_MEDIA_DATA_VALID)==0U && ++polls<GS_POLLS);
            if((word&MT_MEDIA_DATA_VALID)==0U || ((word&MT_MEDIA_DATA_LAST)!=0U)!=(i==511U))return fail("Disk write stream length or timeout");
            sector[i]=(uint8_t)(word>>MT_MEDIA_DATA_BYTE_SHIFT);
        }
    }
    return 0;
}
void iigs_content_service_poll(void)
{
    if(link==NULL)return;
    if(completion_pending) { (void)complete_pending(); return; }
    const uint32_t word=link->read32(link->context,MT_REG_MEDIA_REQUEST);
    if((word&MT_MEDIA_REQUEST_VALID)==0U) { seen=false; return; }
    const uint32_t id=link->read32(link->context,MT_REG_MEDIA_REQUEST_ID);
    const uint32_t gen=link->read32(link->context,MT_REG_MEDIA_REQUEST_GENERATION);
    if(seen && id==seen_id && gen==seen_generation)return;
    const uint32_t blocks=link->read32(link->context,MT_REG_MEDIA_REQUEST_BLOCK_COUNT);
    const unsigned slot=(blocks>>8U)&255U;
    const uint32_t lba=link->read32(link->context,MT_REG_MEDIA_REQUEST_LBA);
    const bool writing=(word&MT_MEDIA_REQUEST_WRITE)!=0U;
    seen=true; seen_id=id; seen_generation=gen;
    uint32_t status=MT_RESPONSE_OK;
    const bool valid_slot=slot<MT_IIGS_DRIVE_COUNT ? drives[slot].mounted && drives[slot].generation==gen:
        slot==MT_IIGS_PRAM_SLOT && pram_mounted && pram_generation==gen;
    if(!valid_slot || (blocks&255U)!=2U || (blocks>>16U)!=0U ||
        (slot==MT_IIGS_PRAM_SLOT && lba>=2U))status=MT_RESPONSE_INVALID_ARGUMENT;
    gs_drive_t *d=slot<MT_IIGS_DRIVE_COUNT ? &drives[slot]:NULL;
    const uint64_t offset=(uint64_t)lba*512U;
    size_t count=512U;
    if(status==MT_RESPONSE_OK && d!=NULL) {
        if(offset>=d->info.size || (writing && !d->info.writable))status=MT_RESPONSE_IO_ERROR;
        else if(d->info.size-offset<512U)count=(size_t)(d->info.size-offset);
    }
    if(status==MT_RESPONSE_OK && !writing) {
        memset(sector,0,sizeof(sector));
        if(d==NULL)memcpy(sector,pram+offset,512U);
        else {
            mt_sd_result_t r=mt_sd_file_seek(d->file,(int64_t)(d->info.offset+offset),SEEK_SET);
            if(r.status==MT_SD_OK)r=mt_sd_file_read(d->file,sector,count);
            if(r.status!=MT_SD_OK || r.size!=count) { (void)sd_fail(r); status=MT_RESPONSE_IO_ERROR; }
        }
    }
    const bool staged=writing && staged_write && staged_id==id && staged_generation==gen;
    if(status==MT_RESPONSE_OK && !staged && transfer(writing)!=0)status=MT_RESPONSE_IO_ERROR;
    if(status==MT_RESPONSE_OK && writing) { staged_write=true; staged_id=id; staged_generation=gen; }
    if(status==MT_RESPONSE_OK && writing) {
        if(d==NULL) { memcpy(pram+offset,sector,512U); if(pram_save()!=0)status=MT_RESPONSE_IO_ERROR; }
        else if(disable_woz_crc(d)!=0)status=MT_RESPONSE_IO_ERROR;
        else {
            if(d->info.kind==MT_IIGS_MEDIA_WOZ && lba==0U)memset(sector+8U,0,4U);
            if(write_sector(d,d->info.offset+offset,count)!=0)status=MT_RESPONSE_IO_ERROR;
        }
        if(status==MT_RESPONSE_OK)staged_write=false;
    }
    completion_id=id; completion_status=status; completion_blocks=status==MT_RESPONSE_OK ? blocks:0U;
    completion_generation=gen; completion_pending=true;
    if(status!=MT_RESPONSE_OK)faulted=true;
    (void)complete_pending();
}
static int drain_state(bool flush)
{
    unsigned idle=0U;
    for(uint32_t poll=0U;poll<GS_POLLS;++poll) {
        iigs_content_service_poll(); tick();
        if(!completion_pending && (link->read32(link->context,MT_REG_MEDIA_REQUEST)&MT_MEDIA_REQUEST_VALID)==0U &&
            (!flush || (link->read32(link->context,MT_REG_MEDIA_STATE)&MT_MEDIA_STATE_EJECT_PENDING)==0U)) {
            /* Allow the config/eject request to cross into the machine clock
             * and back before treating an initially idle mailbox as drained. */
            if(++idle>=16U)return 0;
        } else idle=0U;
    }
    return fail("Apple IIgs media did not finish flushing");
}
static int drain(void) { return drain_state(true); }
static int mount_wire(unsigned slot,uint8_t kind,uint32_t bytes,uint32_t gen,bool ro)
{
    const uint32_t flags=slot|((uint32_t)kind<<MT_MEDIA_KIND_SHIFT)|(ro ? MT_MEDIA_MOUNT_READ_ONLY:0U);
    const mt_link_result_t r=command(MT_COMMAND_MEDIA_MOUNT,flags,bytes,gen,0U);
    return r==MT_LINK_OK ? 0:fail(mt_link_result_name(r));
}
static int mount_internal(unsigned drive,const char *name,bool remember)
{
    if(drive>=MT_IIGS_DRIVE_COUNT || !safe_name(name))return fail("Invalid Apple IIgs disk name");
    gs_drive_t *d=&drives[drive];
    if(d->mounted && iigs_content_service_eject(drive)!=0)return -1;
    if(!path(d->path,name) || strlen(d->path)+9U>GS_PATH)
        return fail("Disk path too long");
    const size_t path_length=strlen(d->path);
    memcpy(d->journal,d->path,path_length); memcpy(d->journal+path_length,".gsj",5U);
    memcpy(d->recovery,d->journal,path_length+4U); memcpy(d->recovery+path_length+4U,".mtj",5U);
    mt_sd_result_t r=mt_sd_file_open(d->path,"r+b",&d->file);
    bool ro=false;
    if(r.status!=MT_SD_OK) { ro=true; r=mt_sd_file_open(d->path,"rb",&d->file); }
    if(r.status!=MT_SD_OK)return sd_fail(r);
    r=mt_sd_file_seek(d->file,0,SEEK_END);
    const int64_t end=mt_sd_file_tell(d->file);
    if(r.status!=MT_SD_OK || end<=0)goto failed;
    d->file_size=(uint64_t)end;
    r=mt_sd_file_seek(d->file,0,SEEK_SET); if(r.status!=MT_SD_OK)goto failed;
    r=mt_sd_file_read(d->file,sector,sizeof(sector)); if(r.status!=MT_SD_OK)goto failed;
    const mt_content_validation_t v=mt_iigs_media_inspect(drive,name,sector,r.size,d->file_size,&d->info);
    if(v!=MT_CONTENT_VALID) { (void)fail(mt_content_validation_name(v)); goto close_failed; }
    if(ro)d->info.writable=false;
    if(!ro && apply_journal(d)!=0)goto close_failed;
    d->woz_crc_disabled=d->info.kind!=MT_IIGS_MEDIA_WOZ || le32(sector+8U)==0U;
    if(ro) {
        const char *const journals[2]={d->journal,d->recovery};
        for(unsigned i=0U;i<2U;++i) {
            bool journal_found=false;
            r=mt_sd_storage_read_file_optional_exact(journals[i],journal_wire,sizeof(journal_wire),&journal_found);
            if(r.status!=MT_SD_OK || journal_found) { (void)fail("Disk requires writable journal recovery"); goto close_failed; }
        }
    }
    d->generation=generation();
    if(mount_wire(drive,d->info.kind,(uint32_t)d->info.size,d->generation,!d->info.writable)!=0)goto close_failed;
    d->mounted=true; (void)snprintf(d->name,sizeof(d->name),"%s",name);
    if(remember) { (void)snprintf(remembered[drive],GS_NAME,"%s",name); return settings_save(); }
    return 0;
failed:
    (void)sd_fail(r);
close_failed:
    (void)mt_sd_file_close(d->file); d->file=NULL; return -1;
}
int iigs_content_service_mount(unsigned drive,const char *name)
{ faulted=false; error[0]='\0'; return mount_internal(drive,name,true); }
static int eject_internal(unsigned drive,bool forget)
{
    gs_drive_t *d=&drives[drive];
    if(d->mounted) {
        /* A WOZ track can stay dirty while its motor runs. Finish only the
         * current transfer before requesting eject, which forces its flush. */
        if(drain_state(false)!=0)return -1;
        const mt_link_result_t r=command(MT_COMMAND_MEDIA_EJECT,drive,generation(),0U,0U);
        if(r!=MT_LINK_OK)return fail(mt_link_result_name(r));
        if(drain()!=0)return -1;
        mt_sd_result_t sd=mt_sd_file_close(d->file); if(sd.status!=MT_SD_OK)return sd_fail(sd);
        d->file=NULL; d->mounted=false; d->name[0]='\0';
    }
    if(forget) { remembered[drive][0]='\0'; return settings_save(); }
    return 0;
}
int iigs_content_service_eject(unsigned drive)
{ return drive<MT_IIGS_DRIVE_COUNT ? eject_internal(drive,true):fail("Invalid disk drive"); }
int iigs_content_service_pram(bool save)
{
    if(!ready)return fail("System ROMs required");
    if(!save && !pram_exists)return fail("No saved Apple IIgs PRAM yet");
    mt_iigs_config_t pulse=config; pulse.pulses|=save ? 8U:16U;
    if(send_config(&pulse)!=0)return -1;
    if(drain_state(false)!=0)return -1;
    if(!save) {
        /* Reset only after the selected PRAM sector has reached the machine. */
        pulse=config; pulse.pulses|=2U;
        if(send_config(&pulse)!=0)return -1;
    }
    return 0;
}
int iigs_content_service_prepare_reset(void)
{
    if(link==NULL)return 0;
    if(ready && iigs_content_service_pram(true)!=0)return -1;
    if(drain_state(false)!=0)return -1;
    for(unsigned i=0U;i<MT_IIGS_DRIVE_COUNT;++i)if(eject_internal(i,false)!=0)return -1;
    if(pram_mounted) {
        if(command(MT_COMMAND_MEDIA_EJECT,MT_IIGS_PRAM_SLOT,generation(),0U,0U)!=MT_LINK_OK)
            return fail("PRAM could not eject");
        if(drain()!=0)return -1;
        pram_mounted=false;
    }
    reset_pending=true; return 0;
}
int iigs_content_service_resume_after_reset(void)
{
    if(!reset_pending)return 0;
    faulted=false;
    pram_generation=generation();
    if(mount_wire(MT_IIGS_PRAM_SLOT,MT_IIGS_MEDIA_PRAM,sizeof(pram),pram_generation,false)!=0)return -1;
    pram_mounted=true;
    for(unsigned i=0U;i<MT_IIGS_DRIVE_COUNT;++i)
        if(remembered[i][0]!='\0' && mount_internal(i,remembered[i],false)!=0)return -1;
    reset_pending=false; return 0;
}
int iigs_content_service_shutdown(void)
{ const int r=iigs_content_service_prepare_reset(); if(r==0)ready=false; return r; }
void iigs_content_service_init(mt_core_link_t *l,const char *content_root,mt_link_progress_fn p,void *ctx)
{
    link=l; progress=p; progress_context=ctx;
    memset(drives,0,sizeof(drives)); memset(remembered,0,sizeof(remembered)); memset(pram,0,sizeof(pram));
    root[0]='\0'; rom_root[0]='\0'; error[0]='\0'; ready=false; faulted=false; seen=false;
    completion_pending=false; pram_mounted=false; pram_exists=false; reset_pending=false; next_generation=1U;
    staged_write=false;
    config=(mt_iigs_config_t){1U,0U,0U,0U,0U,0U};
    speed_mask=MT_IIGS_ACCEL_LEGACY_SPEED_MASK;
    if(link!=NULL && link->read32!=NULL) {
        const uint32_t abi=link->read32(link->context,MT_REG_ABI_VERSION);
        if((abi>>16U)==MT_ABI_MAJOR && (abi&UINT32_C(0xffff))>=MT_IIGS_ACCEL_MIN_ABI_MINOR) {
            const uint32_t caps=link->read32(link->context,MT_REG_IIGS_ACCEL_CAPS);
            if((caps&MT_IIGS_ACCEL_CAPS_VALID)!=0U)
                speed_mask=(caps&MT_IIGS_ACCEL_SPEED_MASK)|UINT32_C(1);
        }
    }
    if(content_root==NULL || strlen(content_root)>=sizeof(root) || strlen(content_root)<3U ||
        content_root[1]!=':' || content_root[2]!='/' || strstr(content_root,"..")!=NULL) {
        (void)fail("Invalid Apple IIgs content folder"); return;
    }
    const char *const media_folder=strrchr(content_root,'/');
    if(media_folder==NULL || strcmp(media_folder,"/media")!=0) {
        (void)fail("Apple IIgs content folder must end in /media"); return;
    }
    const size_t parent_length=(size_t)(media_folder-content_root);
    memcpy(rom_root,content_root,parent_length);
    memcpy(rom_root+parent_length,"/roms",6U);
    memcpy(root,content_root,strlen(content_root)+1U); settings_load();
    /* A saved faster setting must still boot an explicitly reused older image. */
    while(config.speed!=0U && (speed_mask&(UINT32_C(1)<<config.speed))==0U)--config.speed;
}
static int load_rom(const char *name,uint8_t kind,size_t bytes)
{
    char filename[GS_PATH];
    const int length=snprintf(filename,sizeof(filename),"%s/%s",rom_root,name);
    if(length<0 || (size_t)length>=sizeof(filename))return fail("ROM path too long");
    mt_sd_result_t sd=mt_sd_storage_read_file_exact(filename,rom_buffer,bytes);
    if(sd.status!=MT_SD_OK) {
        char message[128];
        (void)snprintf(message,sizeof(message),"ROM %s: %s",name,mt_sd_status_name(sd.status));
        return fail(message);
    }
    uint8_t difference=0U;
    for(size_t i=1U;i<bytes;++i)difference|=(uint8_t)(rom_buffer[i]^rom_buffer[0]);
    if(difference==0U)return fail("System ROM is blank");
    mt_link_result_t r=mt_core_link_stream(link,kind,rom_buffer,bytes,GS_POLLS,progress,progress_context);
    return r==MT_LINK_OK ? 0:fail(mt_link_result_name(r));
}
int iigs_content_service_boot(void)
{
    if(link==NULL || root[0]=='\0')return fail("Apple IIgs service unavailable");
    bool mounted=pram_mounted;
    for(unsigned i=0U;i<MT_IIGS_DRIVE_COUNT;++i)mounted=mounted||drives[i].mounted;
    if((ready || mounted) && iigs_content_service_prepare_reset()!=0)return -1;
    ready=false; faulted=false;
    if(command(MT_COMMAND_CORE_RESET,1U,0U,0U,0U)!=MT_LINK_OK)return fail("Apple IIgs reset failed");
    link->write32(link->context,MT_REG_ERROR,MT_ERROR_CORE_LOAD);
    if(send_config(&config)!=0 ||
        load_rom(config.rom3 ? "rom3.rom":"rom1.rom",config.rom3 ? MT_LOAD_IIGS_ROM3:MT_LOAD_IIGS_ROM1,
            config.rom3 ? MT_SIZE_IIGS_ROM3:MT_SIZE_IIGS_ROM1)!=0 ||
        load_rom("char.rom",MT_LOAD_IIGS_CHARACTER,MT_SIZE_IIGS_CHARACTER)!=0 ||
        load_rom(config.rom3 ? "adb3.rom":"adb1.rom",config.rom3 ? MT_LOAD_IIGS_ADB3:MT_LOAD_IIGS_ADB1,MT_SIZE_IIGS_ADB)!=0 ||
        load_rom("hdd.rom",MT_LOAD_IIGS_HDD_ROM,MT_SIZE_IIGS_HDD_ROM)!=0)return -1;
    char target[GS_PATH], journal[GS_PATH]; bool found=false;
    if(!path(target,"pram.bin") || !path(journal,"pram.bin.mtj"))return fail("PRAM path too long");
    mt_sd_result_t sd=mt_sd_storage_journal_recover(target,journal,pram,sizeof(pram));
    if(sd.status==MT_SD_OK)sd=mt_sd_storage_read_file_optional_exact(target,pram,sizeof(pram),&found);
    if(sd.status!=MT_SD_OK)return sd_fail(sd);
    pram_exists=found;
    if(found && mt_core_link_stream(link,MT_LOAD_IIGS_PRAM,pram+(config.rom3 ? 0U:512U),512U,GS_POLLS,progress,progress_context)!=MT_LINK_OK)
        return fail("PRAM restore transfer failed");
    reset_pending=true;
    if(iigs_content_service_resume_after_reset()!=0)return -1;
    if(command(MT_COMMAND_CORE_RESET,0U,0U,0U,0U)!=MT_LINK_OK)return fail("Apple IIgs reset release failed");
    for(uint32_t poll=0U;poll<GS_POLLS;++poll) {
        tick(); const uint32_t status=link->read32(link->context,MT_REG_STATUS);
        if((status&MT_STATUS_CORE_LOAD_ERROR)!=0U)return fail("Apple IIgs ROM load rejected");
        if((status&MT_STATUS_CORE_READY)!=0U) { ready=true; error[0]='\0'; return 0; }
    }
    return fail("Apple IIgs ready timeout");
}
int iigs_content_service_configure(const mt_iigs_config_t *candidate)
{
    if(!mt_iigs_config_valid(candidate) || candidate->card==1U || (candidate->pulses&~1U)!=0U ||
        (speed_mask&(UINT32_C(1)<<candidate->speed))==0U)
        return fail("Unsupported Apple IIgs configuration");
    const mt_iigs_config_t previous=config; config=*candidate;
    const int r=previous.rom3!=config.rom3 ? iigs_content_service_boot():send_config(&config);
    if(r!=0) { config=previous; return -1; }
    if(settings_save()!=0) {
        config=previous;
        if(previous.rom3!=candidate->rom3)(void)iigs_content_service_boot();
        else (void)send_config(&config);
        return -1;
    }
    return 0;
}
const mt_iigs_config_t *iigs_content_service_config(void) { return &config; }
uint32_t iigs_content_service_speed_mask(void) { return speed_mask; }
void iigs_content_service_set_timestamp(uint32_t timestamp) { config.timestamp=timestamp; }
const char *iigs_content_service_name(unsigned d) { return d<MT_IIGS_DRIVE_COUNT && drives[d].mounted ? drives[d].name:NULL; }
const char *iigs_content_service_error(void) { return error; }
bool iigs_content_service_ready(void) { return ready; }
bool iigs_content_service_faulted(void) { return faulted; }
int iigs_content_service_retry(void)
{
    faulted=false; seen=false; error[0]='\0';
    iigs_content_service_poll();
    const int r=drain();
    return r==0 && faulted ? -1:r;
}
