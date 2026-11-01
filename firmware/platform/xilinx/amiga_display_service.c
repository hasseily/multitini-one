/* SPDX-License-Identifier: GPL-3.0-only */
#include "amiga_display_service.h"
#include "sd_storage.h"
#include <stdio.h>
#include <string.h>

static mt_amiga_display_t display;
static mt_amiga_display_apply_fn apply_callback;
static void *apply_context;
static char target[512],journal[512],storage_error[128];
static bool storage_blocked,automatic_request,saved_unavailable;

static bool apply(void *context,uint8_t mode)
{ (void)context;return apply_callback!=NULL && apply_callback(apply_context,mode); }
static bool save(void *context,uint8_t mode)
{
    (void)context;
    if(storage_blocked)return false;
    if(display.saved_valid && display.saved==mode)return true;
    uint8_t wire[MT_AMIGA_DISPLAY_SIDECAR_BYTES];mt_amiga_display_sidecar_encode(mode,wire);
    const mt_sd_result_t result=mt_sd_storage_journal_replace(target,journal,wire,sizeof(wire));
    if(result.status==MT_SD_OK)return true;
    storage_blocked=true;
    (void)snprintf(storage_error,sizeof(storage_error),"Display active; save failed (%s); journal retained",mt_sd_status_name(result.status));
    return false;
}
static void load(void)
{
    uint8_t wire[MT_AMIGA_DISPLAY_SIDECAR_BYTES];bool found=false;uint8_t mode=0U;
    mt_sd_result_t result=mt_sd_storage_journal_recover(target,journal,wire,sizeof(wire));
    if(result.status==MT_SD_OK)result=mt_sd_storage_read_file_optional_exact(target,wire,sizeof(wire),&found);
    if(result.status!=MT_SD_OK || (found && !mt_amiga_display_sidecar_decode(wire,&mode))) {
        storage_blocked=true;
        (void)snprintf(storage_error,sizeof(storage_error),"Display settings invalid or unreadable; retained for recovery");
        return;
    }
    if(found)mt_amiga_display_restore(&display,mode);
}
void amiga_display_service_init(mt_core_link_t *link,const char *media_root,
    mt_amiga_display_apply_fn callback,void *context)
{
    apply_callback=callback;apply_context=context;storage_blocked=false;automatic_request=false;saved_unavailable=false;storage_error[0]='\0';
    target[0]='\0';journal[0]='\0';
    mt_amiga_display_init(&display,link,apply,save,NULL);
    if(!display.capable)return; /* Legacy806 performs no new commands or file writes. */
    if(media_root==NULL || media_root[0]=='\0')storage_blocked=true;
    else {
        const int a=snprintf(target,sizeof(target),"%s/amiga-display.bin",media_root);
        const int b=snprintf(journal,sizeof(journal),"%s/amiga-display.bin.mtj",media_root);
        storage_blocked=a<0 || b<0 || (size_t)a>=sizeof(target) || (size_t)b>=sizeof(journal);
    }
    if(storage_blocked) {
        (void)snprintf(storage_error,sizeof(storage_error),"Display settings path is unavailable");return;
    }
    load();
}
void amiga_display_service_poll(uint32_t now_ms)
{
    if(display.paused)return;
    mt_amiga_display_poll(&display,now_ms);
    if(!automatic_request && display.saved_valid && mt_amiga_display_available(&display) && !display.pending) {
        if(mt_amiga_display_mode_available(&display,display.saved))
            automatic_request=mt_amiga_display_request(&display,display.saved,now_ms);
        else { /* Preserve the saved wide choice when using an older core/helper. */
            saved_unavailable=true;display.requested=0U;
        }
    }
}
void amiga_display_service_guest_restarted(void)
{
    /* A guest boot invalidates its mailbox ACK, heartbeat and screen snapshot.
     * Retain persistence and any blocked journal; only the live session resets. */
    mt_core_link_t *const link=display.link;
    const uint8_t saved=display.saved;
    const bool saved_valid=display.saved_valid;
    mt_amiga_display_init(&display,link,apply,save,NULL);
    if(saved_valid)mt_amiga_display_restore(&display,saved);
    automatic_request=false;saved_unavailable=false;
}
void amiga_display_service_set_paused(bool paused,uint32_t now_ms)
{ mt_amiga_display_set_paused(&display,paused,now_ms); }
bool amiga_display_service_paused(void) { return display.paused; }
bool amiga_display_service_request(uint8_t mode,uint32_t now_ms)
{
    const bool accepted=mt_amiga_display_request(&display,mode,now_ms);
    if(accepted) { automatic_request=true;saved_unavailable=false; }
    return accepted;
}
bool amiga_display_service_available(void) { return mt_amiga_display_available(&display); }
uint8_t amiga_display_service_mode_count(void) { return mt_amiga_display_mode_count(&display); }
bool amiga_display_service_mode_available(uint8_t mode) { return mt_amiga_display_mode_available(&display,mode); }
bool amiga_display_service_pending(void) { return display.pending; }
uint8_t amiga_display_service_requested(void) { return display.requested; }
uint8_t amiga_display_service_applied(void) { return display.applied; }
uint8_t amiga_display_service_saved(void) { return display.saved; }
const char *amiga_display_service_status(void)
{ return storage_blocked && storage_error[0]!='\0' ? storage_error:saved_unavailable ? "Saved SHRES mode unavailable; choose a supported resolution":mt_amiga_display_status_name(&display); }
const mt_amiga_display_info_t *amiga_display_service_info(void) { return &display.info; }
