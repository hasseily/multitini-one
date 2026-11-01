/* SPDX-License-Identifier: GPL-3.0-only */
#include "amiga_display_service.h"
#include "amiga_display_fixture.h"
#include "sd_storage.h"
#include <stdio.h>

static uint8_t disk[MT_AMIGA_DISPLAY_SIDECAR_BYTES];
static const uint8_t old_settings[]={"MTAMSET1 CPU/mount state remains untouched"};
static uint8_t settings_copy[sizeof(old_settings)];
static bool found,recover_fail,write_fail;
static unsigned recoveries,reads,writes;
static display_fixture_t f;
static mt_sd_result_t result(mt_sd_status_t status)
{ const mt_sd_result_t r={status,0U,0U};return r; }
static void paths(const char *path,const char *journal)
{
    assert(strcmp(path,"/media/amiga/amiga-display.bin")==0);
    if(journal!=NULL)assert(strcmp(journal,"/media/amiga/amiga-display.bin.mtj")==0);
}
mt_sd_result_t mt_sd_storage_journal_recover(const char *path,const char *journal,uint8_t *buffer,size_t capacity)
{
    paths(path,journal);assert(buffer!=NULL && capacity==sizeof(disk));++recoveries;
    return result(recover_fail ? MT_SD_JOURNAL_INVALID:MT_SD_OK);
}
mt_sd_result_t mt_sd_storage_read_file_optional_exact(const char *path,uint8_t *dst,size_t size,bool *present)
{
    paths(path,NULL);assert(size==sizeof(disk));++reads;*present=found;
    if(found)memcpy(dst,disk,sizeof(disk));return result(MT_SD_OK);
}
mt_sd_result_t mt_sd_storage_journal_replace(const char *path,const char *journal,const uint8_t *src,size_t size)
{
    paths(path,journal);assert(size==sizeof(disk));assert(f.applies>writes);++writes;
    if(write_fail)return result(MT_SD_SYNC_FAILED);
    memcpy(disk,src,size);found=true;return result(MT_SD_OK);
}
const char *mt_sd_status_name(mt_sd_status_t status)
{ (void)status;return "test storage error"; }
static void reset(bool capability)
{
    found=false;recover_fail=false;write_fail=false;recoveries=0U;reads=0U;writes=0U;
    memset(disk,0,sizeof(disk));memcpy(settings_copy,old_settings,sizeof(old_settings));
    display_fixture_init(&f,capability);
}
static void healthy(void)
{
    amiga_display_service_poll(0U);assert(!amiga_display_service_available());
    ++f.words[5];amiga_display_service_poll(1000U);assert(amiga_display_service_available());
}
static void default_and_success(void)
{
    reset(true);amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);
    assert(recoveries==1U && reads==1U && writes==0U);
    assert(amiga_display_service_requested()==0U && amiga_display_service_applied()==UINT8_MAX);
    healthy();assert(!amiga_display_service_pending() && f.requests==0U);
    assert(amiga_display_service_request(2U,1100U));assert(writes==0U);
    display_metadata(&f,2U,1U);f.words[3]=0U;amiga_display_service_poll(1200U);
    assert(writes==0U && f.applies==0U && amiga_display_service_pending() && strstr(amiga_display_service_status(),"requester")!=NULL);
    display_metadata(&f,2U,0U);amiga_display_service_poll(1300U);
    uint8_t mode=99U;assert(writes==1U && f.applies==1U && mt_amiga_display_sidecar_decode(disk,&mode) && mode==2U);
    assert(amiga_display_service_applied()==2U && amiga_display_service_saved()==2U);
    assert(!memcmp(settings_copy,old_settings,sizeof(old_settings)));
    assert(amiga_display_service_request(2U,1400U));display_metadata(&f,2U,0U);amiga_display_service_poll(1500U);
    assert(f.applies==2U && writes==1U); /* An already-saved profile needs no SD rewrite. */
}
static void restoration_and_failures(void)
{
    reset(true);mt_amiga_display_sidecar_encode(1U,disk);found=true;
    amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);
    assert(amiga_display_service_requested()==1U && amiga_display_service_saved()==1U && f.requests==0U);
    healthy();assert(amiga_display_service_pending() && f.requests==1U && f.applies==0U);
    display_metadata(&f,1U,0U);amiga_display_service_poll(1100U);assert(f.applies==1U && writes==0U);
    assert(amiga_display_service_request(0U,1200U));display_metadata(&f,0U,3U);amiga_display_service_poll(1300U);
    assert(writes==0U && amiga_display_service_applied()==1U && amiga_display_service_saved()==1U);
    f.apply_ok=false;assert(amiga_display_service_request(0U,1400U));display_metadata(&f,0U,0U);amiga_display_service_poll(1500U);
    assert(writes==0U && amiga_display_service_saved()==1U);
    f.apply_ok=true;write_fail=true;assert(amiga_display_service_request(0U,1600U));display_metadata(&f,0U,0U);
    amiga_display_service_poll(1700U);assert(writes==1U && amiga_display_service_applied()==0U && amiga_display_service_saved()==1U);
    assert(strstr(amiga_display_service_status(),"save failed")!=NULL);
    uint8_t mode=99U;assert(mt_amiga_display_sidecar_decode(disk,&mode) && mode==1U);
    assert(amiga_display_service_request(2U,1800U));display_metadata(&f,2U,0U);amiga_display_service_poll(1900U);
    assert(writes==1U && amiga_display_service_saved()==1U); /* Do not overwrite a retained failed journal. */
    assert(!memcmp(settings_copy,old_settings,sizeof(old_settings)));
    const unsigned before=f.commands;f.status|=MT_AMIGA_HOST_ERROR;
    assert(!amiga_display_service_request(0U,2100U));amiga_display_service_poll(2200U);
    assert(f.commands==before && writes==1U && (f.status&MT_AMIGA_HOST_ERROR)!=0U && f.clears==0U);
}
static void invalid_and_paths(void)
{
    for(unsigned failure=0U;failure<2U;++failure) {
        reset(true);found=true;mt_amiga_display_sidecar_encode(2U,disk);
        if(failure==0U)disk[12]^=1U;else recover_fail=true;
        uint8_t retained[sizeof(disk)];memcpy(retained,disk,sizeof(disk));
        amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);healthy();
        assert(!amiga_display_service_pending() && f.requests==0U);
        assert(amiga_display_service_request(0U,1100U));display_metadata(&f,0U,0U);amiga_display_service_poll(1200U);
        assert(amiga_display_service_applied()==0U && writes==0U && !memcmp(retained,disk,sizeof(disk)));
    }
    reset(true);char too_long[600];memset(too_long,'a',sizeof(too_long));too_long[sizeof(too_long)-1U]='\0';
    amiga_display_service_init(&f.link,too_long,display_apply,&f);healthy();assert(recoveries==0U && reads==0U);
    assert(amiga_display_service_request(0U,1100U));display_metadata(&f,0U,0U);amiga_display_service_poll(1200U);
    assert(writes==0U && strstr(amiga_display_service_status(),"path")!=NULL);
}
static void saved_shres_is_not_replayed_on_legacy(void)
{
    reset(true);found=true;mt_amiga_display_sidecar_encode(3U,disk);
    uint8_t retained[sizeof(disk)];memcpy(retained,disk,sizeof(disk));
    amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);healthy();
    assert(!amiga_display_service_pending() && amiga_display_service_requested()==0U && amiga_display_service_saved()==3U);
    assert(f.requests==0U && writes==0U && !memcmp(disk,retained,sizeof(disk)));
    f.cap|=MT_AMIGA_HOST_SHRES;amiga_display_service_poll(1100U);
    assert(f.requests==0U && writes==0U); /* New FPGA plus old helper still cannot request3. */
    f.words[15]|=64U;amiga_display_service_poll(1200U);
    assert(amiga_display_service_pending() && f.requests==1U && writes==0U);
    display_metadata(&f,3U,0U);amiga_display_service_poll(1300U);
    assert(amiga_display_service_applied()==3U && amiga_display_service_saved()==3U && writes==0U);
}
static void paused_service_does_not_restore_or_write(void)
{
    reset(true);found=true;mt_amiga_display_sidecar_encode(1U,disk);
    amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);
    amiga_display_service_set_paused(true,0U);const unsigned before=f.commands;
    amiga_display_service_poll(100000U);
    assert(amiga_display_service_paused() && f.commands==before && writes==0U && f.requests==0U);
    assert(!amiga_display_service_request(1U,100000U));
    amiga_display_service_set_paused(false,100100U);amiga_display_service_poll(100100U);
    ++f.words[5];amiga_display_service_poll(101100U);
    assert(amiga_display_service_pending() && f.requests==1U && writes==0U);
}
static void slow_guest_completion_preserves_pending_and_sidecar(void)
{
    reset(true);amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);healthy();
    assert(amiga_display_service_request(1U,1100U));display_metadata(&f,1U,1U);f.words[3]=0U;
    amiga_display_service_poll(5000U);
    assert(amiga_display_service_pending() && !amiga_display_service_available() && writes==0U && f.applies==0U);
    const unsigned before=f.commands;
    assert(!amiga_display_service_request(2U,5100U) && f.commands==before);
    display_metadata(&f,1U,0U);amiga_display_service_poll(6100U);
    uint8_t mode=99U;
    assert(!amiga_display_service_pending() && amiga_display_service_applied()==1U &&
           amiga_display_service_saved()==1U && writes==1U && f.applies==1U);
    assert(mt_amiga_display_sidecar_decode(disk,&mode) && mode==1U);
    assert(!memcmp(settings_copy,old_settings,sizeof(old_settings)));
}
static void guest_restart_rearms_saved_mode_after_fresh_heartbeat(void)
{
    reset(true);f.cap|=MT_AMIGA_HOST_SHRES;f.words[15]|=64U;
    found=true;mt_amiga_display_sidecar_encode(3U,disk);
    amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);healthy();
    assert(f.requests==1U && amiga_display_service_pending());
    display_metadata(&f,3U,0U);amiga_display_service_poll(1100U);
    assert(amiga_display_service_applied()==3U && f.applies==1U && writes==0U);
    /* A CPU change restarts the guest and clears native mailbox state. An
     * old completion/health must not suppress the saved automatic request. */
    memset(f.words,0,sizeof(f.words));f.words[0]=0x4d54U;f.words[1]=1U;f.words[15]=65U;
    const unsigned commands=f.commands;
    amiga_display_service_guest_restarted();
    assert(f.commands==commands && recoveries==1U && reads==1U && writes==0U);
    assert(!amiga_display_service_available() && !amiga_display_service_pending());
    assert(amiga_display_service_saved()==3U && amiga_display_service_applied()==UINT8_MAX);
    amiga_display_service_poll(2000U);
    assert(f.requests==1U && !amiga_display_service_available());
    ++f.words[5];amiga_display_service_poll(3000U);
    assert(amiga_display_service_available() && amiga_display_service_mode_count()==4U &&
           f.requests==2U && amiga_display_service_pending());
    display_metadata(&f,3U,0U);amiga_display_service_poll(3100U);
    assert(amiga_display_service_applied()==3U && f.applies==2U && writes==0U);
    /* A reset during an accepted request also discards its old pending ACK. */
    assert(amiga_display_service_request(1U,3200U));
    amiga_display_service_guest_restarted();
    assert(!amiga_display_service_pending() && !amiga_display_service_available() &&
           amiga_display_service_requested()==3U && amiga_display_service_saved()==3U);
}
static void guest_restart_preserves_failed_journal_barrier(void)
{
    reset(true);amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);healthy();
    write_fail=true;assert(amiga_display_service_request(1U,1100U));
    display_metadata(&f,1U,0U);amiga_display_service_poll(1200U);
    assert(writes==1U && strstr(amiga_display_service_status(),"save failed")!=NULL);
    amiga_display_service_guest_restarted();
    assert(strstr(amiga_display_service_status(),"save failed")!=NULL && recoveries==1U && reads==1U);
    amiga_display_service_poll(2000U);++f.words[5];amiga_display_service_poll(3000U);
    assert(amiga_display_service_request(2U,3100U));
    display_metadata(&f,2U,0U);amiga_display_service_poll(3200U);
    assert(amiga_display_service_applied()==2U && writes==1U &&
           strstr(amiga_display_service_status(),"save failed")!=NULL);
}
int main(void)
{
    reset(false);amiga_display_service_init(&f.link,"/media/amiga",display_apply,&f);
    amiga_display_service_poll(0U);++f.words[5];amiga_display_service_poll(1000U);
    assert(!amiga_display_service_available() && !amiga_display_service_request(2U,1100U));
    assert(recoveries==0U && reads==0U && writes==0U && f.commands==0U);
    default_and_success();restoration_and_failures();invalid_and_paths();saved_shres_is_not_replayed_on_legacy();paused_service_does_not_restore_or_write();slow_guest_completion_preserves_pending_and_sidecar();
    guest_restart_rearms_saved_mode_after_fresh_heartbeat();guest_restart_preserves_failed_journal_barrier();
    puts("Amiga display service journal/legacy/restore/rollback tests passed");return 0;
}
