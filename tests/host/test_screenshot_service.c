/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the real state machine and ownership checks, replacing only DDR,
 * the already-tested SD owner, and the committed-effects service. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void copy_pixels(void *destination,uint32_t address,size_t size);
#define MT_SCREENSHOT_COPY_PIXELS(destination,address,bytes) copy_pixels(destination,address,bytes)
#include "../../firmware/platform/xilinx/screenshot_service.c"
#include "multitini_gameboy_interface.h"
#include "multitini_iigs_interface.h"

struct mt_sd_file { bool open;size_t position; };
static struct mt_sd_file test_file;
static uint32_t registers[256],now_ms;
static unsigned copies,progress_calls,open_calls,close_calls,remove_calls,rename_calls,write_calls;
static unsigned open_collisions,rename_collisions,close_failures,write_failure_at,race;
static mt_sd_status_t directory_failure,open_failure,rename_failure;
static bool effects_ready;
static uint32_t copied_base;
static uint32_t copied_last;
static uint8_t written[MT_SCREENSHOT_MAX_WIDTH*1080U*3U+1080U*32U+1024U];
static char opened_name[128],renamed_from[128],renamed_to[128],removed_name[128];

static mt_sd_result_t result(mt_sd_status_t status,size_t size)
{ const mt_sd_result_t r={status,0U,size};return r; }
static uint32_t read_register(uint32_t offset)
{ assert(offset/4U<256U);return registers[offset/4U]; }
static uint32_t milliseconds(void) { return now_ms; }
static void progress(void) { ++progress_calls; }
static void copy_pixels(void *destination,uint32_t address,size_t size)
{
    uint16_t *pixels=(uint16_t *)destination;
    const uint32_t width=registers[MT_REG_VIDEO_FRAME_GEOMETRY/4U]&0xffffU;
    assert((address&127U)==0U && size==width*sizeof(uint16_t));
    if(copies==0U)copied_base=address;
    copied_last=address;
    for(size_t x=0U;x<size/2U;++x)pixels[x]=(uint16_t)(0x1000U+copies*width+x);
    ++copies;
    if(race==1U)registers[MT_REG_VIDEO_FRAME_CAPTURE_PRODUCER/4U]+=4U;
    if(race==2U)registers[MT_REG_VIDEO_FRAME_DISPLAY_ACTIVE/4U]+=4U;
    if(race==3U)++registers[MT_REG_VIDEO_EFFECT_SEQUENCE/4U];
    if(race==4U)registers[MT_REG_VIDEO_FRAME_CONTROL/4U]=0U;
    if(race==5U)registers[MT_REG_VIDEO_FRAME_STATUS/4U]&=~MT_VIDEO_FRAME_STATUS_DISPLAY_ACTIVE;
    if(race==6U && (copies&1U)!=0U)registers[MT_REG_DISPLAY_STATUS/4U]^=16U;
}
bool mt_video_effect_service_snapshot(mt_video_effects_tables_t *out)
{ memset(out,0,sizeof(*out));return effects_ready; }
mt_sd_result_t mt_sd_storage_ensure_directory(const char *path)
{ assert(strcmp(path,"0:/screenshots")==0);return result(directory_failure,0U); }
mt_sd_result_t mt_sd_file_open(const char *path,const char *mode,mt_sd_file_t **out)
{
    ++open_calls;assert(strcmp(mode,"xb")==0);*out=NULL;
    if(open_collisions!=0U) { --open_collisions;return result(MT_SD_FILE_EXISTS,0U); }
    if(open_failure!=MT_SD_OK)return result(open_failure,0U);
    assert(!test_file.open);test_file.open=true;test_file.position=0U;*out=&test_file;
    (void)snprintf(opened_name,sizeof(opened_name),"%s",path);
    return result(MT_SD_OK,0U);
}
mt_sd_result_t mt_sd_file_write_buffered(mt_sd_file_t *handle,const void *data,size_t size)
{
    assert(handle==&test_file && handle->open);++write_calls;
    if(write_failure_at==write_calls)return result(MT_SD_WRITE_FAILED,0U);
    assert(handle->position+size<=sizeof(written));
    memcpy(written+handle->position,data,size);handle->position+=size;
    return result(MT_SD_OK,size);
}
mt_sd_result_t mt_sd_file_close(mt_sd_file_t *handle)
{
    assert(handle==&test_file && handle->open);++close_calls;
    if(close_failures!=0U) { --close_failures;return result(MT_SD_CLOSE_FAILED,0U); }
    handle->open=false;return result(MT_SD_OK,0U);
}
int64_t mt_sd_file_tell(const mt_sd_file_t *handle)
{ return handle==&test_file && handle->open ? (int64_t)handle->position:-1; }
mt_sd_result_t mt_sd_file_remove(const char *path)
{
    assert(!test_file.open);++remove_calls;
    (void)snprintf(removed_name,sizeof(removed_name),"%s",path);return result(MT_SD_OK,0U);
}
mt_sd_result_t mt_sd_file_rename(const char *old_path,const char *new_path)
{
    assert(!test_file.open);++rename_calls;
    if(rename_collisions!=0U) { --rename_collisions;return result(MT_SD_FILE_EXISTS,0U); }
    if(rename_failure!=MT_SD_OK)return result(rename_failure,0U);
    (void)snprintf(renamed_from,sizeof(renamed_from),"%s",old_path);
    (void)snprintf(renamed_to,sizeof(renamed_to),"%s",new_path);return result(MT_SD_OK,0U);
}
const char *mt_sd_status_name(mt_sd_status_t status)
{ return status==MT_SD_OK ? "OK":"stub SD error"; }
static void reset_test(void)
{
    const mt_screenshot_service_ops_t ops={read_register,milliseconds,progress};
    assert(!test_file.open);memset(registers,0,sizeof(registers));
    registers[MT_REG_ABI_VERSION/4U]=MT_VIDEO_PHOSPHOR_MIN_ABI_MINOR;
    registers[MT_REG_CORE_ID/4U]=MT_GBC_CORE_ID;
    registers[MT_REG_VIDEO_FRAME_CONTROL/4U]=MT_VIDEO_FRAME_CAPTURE_ENABLE;
    registers[MT_REG_VIDEO_FRAME_STATUS/4U]=MT_VIDEO_FRAME_STATUS_DISPLAY_ACTIVE;
    registers[MT_REG_VIDEO_FRAME_GEOMETRY/4U]=(2U<<16)|4U;
    registers[MT_REG_VIDEO_FRAME_STRIDE/4U]=128U;
    registers[MT_REG_VIDEO_FRAME_CAPTURE_PRODUCER/4U]=7U;
    registers[MT_REG_VIDEO_FRAME_DISPLAY_ACTIVE/4U]=(9U<<2)|1U;
    registers[MT_REG_VIDEO_EFFECT_SEQUENCE/4U]=6U;
    registers[MT_REG_VIDEO_EFFECT_STATUS/4U]=MT_VIDEO_EFFECT_STATUS_VALID;
    now_ms=123U;copies=progress_calls=open_calls=close_calls=remove_calls=rename_calls=write_calls=0U;
    open_collisions=rename_collisions=close_failures=write_failure_at=race=0U;
    directory_failure=open_failure=rename_failure=MT_SD_OK;effects_ready=true;
    opened_name[0]=renamed_from[0]=renamed_to[0]=removed_name[0]='\0';
    mt_screenshot_service_init(&ops);
}
static void poll(void)
{ mt_screenshot_service_poll(MT_GBC_CORE_ID,"Game Boy/Color",0U,0U,NULL,false); }
static void finish(void)
{
    for(unsigned i=0U;i<1200U && mt_screenshot_service_busy();++i)poll();
    assert(!mt_screenshot_service_busy());
}
static uint32_t big32_at(size_t offset)
{ return (uint32_t)written[offset]<<24|(uint32_t)written[offset+1U]<<16|
    (uint32_t)written[offset+2U]<<8|written[offset+3U]; }
static void iigs_capture(void)
{
    for(unsigned geometry=0U;geometry<2U;++geometry)for(unsigned mode=0U;mode<2U;++mode) {
        const unsigned width=geometry ? 724U:640U,height=geometry ? 496U:400U;
        const unsigned stride=geometry ? 1536U:1280U;
        reset_test();
        registers[MT_REG_ABI_VERSION/4U]=22U;
        registers[MT_REG_CORE_ID/4U]=MT_IIGS_CORE_ID;
        registers[MT_REG_VIDEO_FRAME_GEOMETRY/4U]=(height<<16)|width;
        registers[MT_REG_VIDEO_FRAME_STRIDE/4U]=stride;
        assert(mt_screenshot_service_request(mode!=0U));
        for(unsigned i=0U;i<1200U && mt_screenshot_service_busy();++i)
            mt_screenshot_service_poll(MT_IIGS_CORE_ID,"Apple IIgs",0U,0U,NULL,false);
        assert(!mt_screenshot_service_busy());
        assert(strstr(mt_screenshot_service_status(),"Screenshot saved")!=NULL);
        assert(copies==height && copied_base==MT_VIDEO_FRAME_OUTPUT_BASE+MT_VIDEO_FRAME_SLOT_BYTES);
        assert(copied_last==copied_base+(height-1U)*stride);
        assert(open_calls==1U && close_calls==1U && rename_calls==1U && remove_calls==0U);
        assert(memcmp(written,"\211PNG\r\n\032\n",8U)==0);
        assert(big32_at(16U)==(mode!=0U ? 1920U:width*2U));
        assert(big32_at(20U)==(mode!=0U ? 1080U:height*2U));
        assert(progress_calls==(mode!=0U ? 1080U:height*2U) && write_calls==progress_calls+2U);
        assert(strstr(renamed_to,mode!=0U ? "-screen.png":"-video.png")!=NULL);
        assert(test_file.position>3U*big32_at(16U)*big32_at(20U));
        assert(memcmp(written+test_file.position-12U,"\0\0\0\0IEND",8U)==0);
    }
    reset_test();registers[MT_REG_VIDEO_FRAME_GEOMETRY/4U]=(637U<<16)|724U;
    registers[MT_REG_VIDEO_FRAME_STRIDE/4U]=1536U;
    assert(mt_screenshot_service_request(false));finish();
    assert(copies==0U && open_calls==0U && strstr(mt_screenshot_service_status(),"invalid capture geometry")!=NULL);
    reset_test();registers[MT_REG_CORE_ID/4U]=UINT32_C(0xDEADBEEF);
    assert(mt_screenshot_service_request(false));
    for(unsigned i=0U;i<10U && mt_screenshot_service_busy();++i)
        mt_screenshot_service_poll(UINT32_C(0xDEADBEEF),"Unknown",0U,0U,NULL,false);
    assert(!mt_screenshot_service_busy() && open_calls==0U && write_calls==0U);
    assert(strstr(mt_screenshot_service_status(),"unsupported display geometry")!=NULL);
}
static void successful_capture(void)
{
    reset_test();open_collisions=2U;rename_collisions=1U;race=1U;
    assert(mt_screenshot_service_request(false));assert(!mt_screenshot_service_request(true));
    finish();
    /* Advancing raw capture cannot affect a snapshot of processed output. */
    assert(copies==2U && copied_base==MT_VIDEO_FRAME_OUTPUT_BASE+MT_VIDEO_FRAME_SLOT_BYTES);
    assert(open_calls==3U && close_calls==1U && remove_calls==0U && rename_calls==2U);
    assert(progress_calls==12U && write_calls==14U);
    assert(memcmp(written,"\211PNG\r\n\032\n",8U)==0);
    assert(strcmp(opened_name,renamed_from)==0 && strstr(renamed_to,"Game_Boy_Color-")!=NULL);
    assert(strstr(renamed_to,"-video.png")!=NULL);
    assert(strcmp(mt_screenshot_service_last_path(),renamed_to)==0);
    assert(strstr(mt_screenshot_service_status(),"Screenshot saved")!=NULL);
}
static void snapshot_ownership(void)
{
    for(unsigned mode=0U;mode<2U;++mode)for(unsigned r=2U;r<=5U;++r) {
        reset_test();race=r;assert(mt_screenshot_service_request(mode!=0U));
        finish();assert(open_calls==0U && remove_calls==0U);
        assert(strstr(mt_screenshot_service_status(),"stable video frame")!=NULL);
    }
    for(unsigned mode=0U;mode<2U;++mode) {
        reset_test();effects_ready=false;assert(mt_screenshot_service_request(mode!=0U));
        finish();assert(open_calls==0U && copies==0U);
    }
    reset_test();assert(mt_screenshot_service_request(true));poll();
    assert(copied_base==MT_VIDEO_FRAME_OUTPUT_BASE+MT_VIDEO_FRAME_SLOT_BYTES);
    assert(mt_screenshot_service_cancel() && open_calls==0U);
    reset_test();registers[MT_REG_VIDEO_FRAME_GEOMETRY/4U]=0U;
    assert(mt_screenshot_service_request(false));finish();assert(copies==0U && open_calls==0U);
    reset_test();registers[MT_REG_ABI_VERSION/4U]=1U;
    assert(mt_screenshot_service_request(false));finish();assert(copies==0U && open_calls==0U);
    reset_test();registers[MT_REG_ABI_VERSION/4U]=16U;
    assert(mt_screenshot_service_request(false));finish();assert(copies==0U && open_calls==0U);
    reset_test();registers[MT_REG_VIDEO_FRAME_STATUS/4U]=0U;
    assert(mt_screenshot_service_request(false));now_ms+=1000U;finish();assert(copies==0U && open_calls==0U);
    reset_test();registers[MT_REG_ABI_VERSION/4U]=21U;registers[MT_REG_DISPLAY_STATUS/4U]=32U;
    assert(mt_screenshot_service_request(false));finish();
    assert(scene.video_multiplier==2U && progress_calls==4U);
    assert(written[19U]==8U && written[23U]==4U); /* PNG 8 x 4 integer size. */
    reset_test();registers[MT_REG_ABI_VERSION/4U]=21U;race=6U;
    assert(mt_screenshot_service_request(false));finish();assert(open_calls==0U);
    const uint32_t bad_display[]={MT_DISPLAY_BUSY,MT_DISPLAY_ERROR,MT_DISPLAY_HELD,1U};
    for(unsigned i=0U;i<sizeof(bad_display)/sizeof(bad_display[0]);++i) {
        reset_test();registers[MT_REG_ABI_VERSION/4U]=21U;registers[MT_REG_DISPLAY_STATUS/4U]=bad_display[i];
        assert(mt_screenshot_service_request(false));finish();assert(open_calls==0U && copies==0U);
    }
}
static void failure_and_cancel(void)
{
    reset_test();directory_failure=MT_SD_CREATE_DIR_FAILED;
    assert(mt_screenshot_service_request(false));finish();assert(open_calls==0U && remove_calls==0U);
    reset_test();open_failure=MT_SD_OPEN_FAILED;
    assert(mt_screenshot_service_request(false));finish();assert(open_calls==1U && remove_calls==0U);
    for(unsigned at=1U;at<=14U;++at) {
        reset_test();write_failure_at=at;
        assert(mt_screenshot_service_request(false));finish();
        assert(close_calls==1U && remove_calls==1U && rename_calls==0U);
        assert(strcmp(opened_name,removed_name)==0 && mt_screenshot_service_last_path()[0]=='\0');
    }
    reset_test();rename_failure=MT_SD_RENAME_FAILED;
    assert(mt_screenshot_service_request(false));finish();assert(remove_calls==1U && close_calls==1U);
    reset_test();assert(mt_screenshot_service_request(false));poll();poll();
    assert(test_file.open);close_failures=1U;
    assert(!mt_screenshot_service_cancel() && test_file.open && remove_calls==0U);
    assert(mt_screenshot_service_cancel() && !test_file.open && remove_calls==1U);
    assert(!mt_screenshot_service_busy() && mt_screenshot_service_cancel());
    reset_test();close_failures=1U;assert(mt_screenshot_service_request(false));finish();
    assert(close_calls==2U && remove_calls==1U && rename_calls==0U);
}
int main(void)
{
    successful_capture();snapshot_ownership();failure_and_cancel();iigs_capture();
    puts("screenshot service tests passed");return 0;
}
