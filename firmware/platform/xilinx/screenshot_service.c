/* SPDX-License-Identifier: GPL-3.0-only */
#include "screenshot_service.h"
#include "sd_storage.h"
#include "video_effect_service.h"
#include "multitini_video_frame.h"
#include "multitini_display.h"
#include <stdio.h>
#include <string.h>

enum { IDLE, SNAPSHOT, OPEN_FILE, WRITE_ROWS, FINISH, CLEANUP };
static mt_screenshot_service_ops_t bus;
static unsigned state, attempts;
static bool full;
static uint32_t started, counter, output_width, output_height, next_row;
static mt_sd_file_t *file;
static mt_screenshot_scene_t scene;
static mt_ui_frame_t captured_ui;
static mt_video_effects_tables_t captured_effects;
static mt_screenshot_png_t png;
static uint16_t captured_pixels[MT_VIDEO_FRAME_MAX_PIXELS];
static uint16_t output_row[MT_SCREENSHOT_MAX_WIDTH];
static char stem[32], temporary_path[128], final_path[128], last_path[128], message[180];

/* The video worker maps the FPGA frame rings as normal noncacheable DDR.
 * This separate buffer is ordinary private CPU0 storage. Tests replace only
 * the physical read, while retaining the sequence checks and state machine. */
#ifndef MT_SCREENSHOT_COPY_PIXELS
#define MT_SCREENSHOT_COPY_PIXELS(destination, address, bytes) \
    memcpy((destination), (const void *)(uintptr_t)(address), (bytes))
#endif
static void memory_barrier(void)
{
#if defined(__arm__) || defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#elif defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

static void fail(const char *reason)
{
    (void)snprintf(message,sizeof(message),"Screenshot failed: %s",reason);
    state=CLEANUP;
}
static bool write_bytes(void *context,const void *bytes,size_t size)
{
    (void)context;
    const mt_sd_result_t result=mt_sd_file_write_buffered(file,bytes,size);
    if(result.status!=MT_SD_OK || result.size!=size) {
        fail(result.status==MT_SD_OK ? "short SD write" : mt_sd_status_name(result.status));
        return false;
    }
    return true;
}
void mt_screenshot_service_init(const mt_screenshot_service_ops_t *ops)
{
    memset(&bus,0,sizeof(bus));if(ops!=NULL)bus=*ops;
    state=IDLE;file=NULL;counter=0U;temporary_path[0]=final_path[0]=last_path[0]=message[0]='\0';
}
bool mt_screenshot_service_request(bool full_output)
{
    if(state!=IDLE || bus.read==NULL || bus.milliseconds==NULL)return false;
    full=full_output;attempts=0U;started=bus.milliseconds();state=SNAPSHOT;
    temporary_path[0]=final_path[0]='\0';
    (void)snprintf(message,sizeof(message),"Capturing %s screenshot...",full ? "full screen":"filtered video");
    return true;
}
bool mt_screenshot_service_busy(void) { return state!=IDLE; }
const char *mt_screenshot_service_status(void) { return message[0]=='\0' ? NULL:message; }
const char *mt_screenshot_service_last_path(void) { return last_path; }

static bool cleanup(void)
{
    if(file!=NULL) {
        (void)mt_sd_file_close(file);
        if(mt_sd_file_tell(file)>=0)return false;
        file=NULL;
    }
    if(temporary_path[0]!='\0') {
        const mt_sd_result_t removed=mt_sd_file_remove(temporary_path);
        if(removed.status!=MT_SD_OK)
            (void)snprintf(message,sizeof(message),"Screenshot cleanup failed: %s",mt_sd_status_name(removed.status));
    }
    temporary_path[0]='\0';state=IDLE;return true;
}
bool mt_screenshot_service_cancel(void)
{
    if(state==IDLE)return true;
    if(state!=CLEANUP)(void)snprintf(message,sizeof(message),"Screenshot cancelled.");
    state=CLEANUP;
    return cleanup();
}

static bool snapshot(uint32_t core_id,uint8_t mode,uint32_t border,const mt_ui_frame_t *ui,bool visible)
{
    const uint32_t abi=bus.read(MT_REG_ABI_VERSION);
    if((abi>>16)!=MT_ABI_MAJOR || (abi&0xffffU)<MT_VIDEO_PHOSPHOR_MIN_ABI_MINOR ||
       bus.read(MT_REG_CORE_ID)!=core_id) { fail("update the core package to capture this video");return false; }
    const bool has_multiplier=(abi&0xffffU)>=MT_DISPLAY_SCALE_MIN_ABI_MINOR;
    const uint32_t display=has_multiplier ? bus.read(MT_REG_DISPLAY_STATUS):mode;
    if(has_multiplier && ((display&(MT_DISPLAY_BUSY|MT_DISPLAY_ERROR|MT_DISPLAY_HELD))!=0U ||
                          (display&15U)!=mode)) return false;
    const uint32_t status=bus.read(MT_REG_VIDEO_FRAME_STATUS);
    if((status&MT_VIDEO_FRAME_STATUS_ERROR)!=0U) { fail("video transport error");return false; }
    if((bus.read(MT_REG_VIDEO_FRAME_CONTROL)&MT_VIDEO_FRAME_CAPTURE_ENABLE)==0U)return false;
    const uint32_t geometry=bus.read(MT_REG_VIDEO_FRAME_GEOMETRY),stride=bus.read(MT_REG_VIDEO_FRAME_STRIDE);
    const uint32_t width=geometry&0xffffU,height=geometry>>16;
    if(width==0U || width>MT_VIDEO_FRAME_MAX_WIDTH || height==0U || height>MT_VIDEO_FRAME_MAX_HEIGHT ||
       stride!=((width*2U+127U)&~UINT32_C(127)) || stride*height>MT_VIDEO_FRAME_SLOT_BYTES) {
        fail("invalid capture geometry");return false;
    }
    const uint32_t selector=MT_REG_VIDEO_FRAME_DISPLAY_ACTIVE;
    const uint32_t sequence=bus.read(selector);
    if((status&MT_VIDEO_FRAME_STATUS_DISPLAY_ACTIVE)==0U)return false;
    const uint32_t effect_sequence=bus.read(MT_REG_VIDEO_EFFECT_SEQUENCE);
    if(!mt_video_effect_service_snapshot(&captured_effects))return false;
    const uint32_t slot=sequence&3U;
    const uint32_t base=MT_VIDEO_FRAME_OUTPUT_BASE+slot*MT_VIDEO_FRAME_SLOT_BYTES;
    memory_barrier();
    for(uint32_t y=0U;y<height;++y)
        MT_SCREENSHOT_COPY_PIXELS(captured_pixels+y*width,base+y*stride,width*sizeof(uint16_t));
    memory_barrier();
    /* A slot is accepted only if its publication sequence stayed unchanged
     * throughout the copy. No ring is reserved, stopped, or made writable. */
    if(bus.read(selector)!=sequence || bus.read(MT_REG_VIDEO_FRAME_GEOMETRY)!=geometry ||
       bus.read(MT_REG_VIDEO_FRAME_STRIDE)!=stride || bus.read(MT_REG_CORE_ID)!=core_id ||
       (bus.read(MT_REG_VIDEO_FRAME_STATUS)&MT_VIDEO_FRAME_STATUS_ERROR)!=0U ||
       (bus.read(MT_REG_VIDEO_FRAME_CONTROL)&MT_VIDEO_FRAME_CAPTURE_ENABLE)==0U)return false;
    if(has_multiplier && bus.read(MT_REG_DISPLAY_STATUS)!=display)return false;
    if((bus.read(MT_REG_VIDEO_FRAME_STATUS)&MT_VIDEO_FRAME_STATUS_DISPLAY_ACTIVE)==0U ||
       bus.read(MT_REG_VIDEO_EFFECT_SEQUENCE)!=effect_sequence ||
       (bus.read(MT_REG_VIDEO_EFFECT_STATUS)&(MT_VIDEO_EFFECT_STATUS_BUSY|MT_VIDEO_EFFECT_STATUS_VALID))!=
           MT_VIDEO_EFFECT_STATUS_VALID)return false;
    memset(&scene,0,sizeof(scene));scene.pixels=captured_pixels;scene.width=width;scene.height=height;
    scene.core_id=core_id;scene.display_mode=mode;scene.border_config=border;scene.full_output=full;
    scene.video_multiplier=has_multiplier ? (uint8_t)((display&MT_DISPLAY_SCALE_MASK)>>MT_DISPLAY_SCALE_SHIFT):0U;
    scene.ui_visible=full && visible && ui!=NULL;
    if(scene.ui_visible) { captured_ui=*ui;scene.ui=&captured_ui; }
    scene.effects=&captured_effects;
    if(!mt_screenshot_dimensions(&scene,&output_width,&output_height)) { fail("unsupported display geometry");return false; }
    return true;
}
static void make_paths(void)
{
    ++counter;
    (void)snprintf(final_path,sizeof(final_path),"0:/screenshots/%s-%08lx-%08lx-%s.png",
        stem,(unsigned long)started,(unsigned long)counter,full ? "screen":"video");
    (void)snprintf(temporary_path,sizeof(temporary_path),"0:/screenshots/%s-%08lx-%08lx.part",
        stem,(unsigned long)started,(unsigned long)counter);
}
void mt_screenshot_service_poll(uint32_t core_id,const char *core_name,uint8_t mode,
    uint32_t border,const mt_ui_frame_t *ui,bool visible)
{
    if(state==IDLE)return;
    if(state==CLEANUP) {
        (void)cleanup();return;
    }
    if(state==SNAPSHOT) {
        if(!snapshot(core_id,mode,border,ui,visible)) {
            if(state==SNAPSHOT && (++attempts>=8U || bus.milliseconds()-started>=1000U))
                fail("could not copy a stable video frame; try again");
            return;
        }
        size_t i=0U;
        if(core_name!=NULL)for(;i<sizeof(stem)-1U && core_name[i]!='\0';++i) {
            const char c=core_name[i];
            stem[i]=((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-') ? c:'_';
        }
        if(i==0U) { memcpy(stem,"core",4U);i=4U; }stem[i]='\0';
        const mt_sd_result_t directory=mt_sd_storage_ensure_directory("0:/screenshots");
        if(directory.status!=MT_SD_OK) { fail(mt_sd_status_name(directory.status));return; }
        attempts=0U;state=OPEN_FILE;return;
    }
    if(state==OPEN_FILE) {
        make_paths();
        const mt_sd_result_t opened=mt_sd_file_open(temporary_path,"xb",&file);
        if(opened.status==MT_SD_FILE_EXISTS) {
            temporary_path[0]='\0';
            if(++attempts>=64U)fail("too many existing capture filenames");
            return;
        }
        if(opened.status!=MT_SD_OK) { temporary_path[0]='\0';fail(mt_sd_status_name(opened.status));return; }
        if(!mt_screenshot_png_begin(&png,output_width,output_height,write_bytes,NULL)) {
            if(state!=CLEANUP)fail("PNG header failed");
            return;
        }
        next_row=0U;state=WRITE_ROWS;return;
    }
    if(state==WRITE_ROWS) {
        /* One row per service call keeps file and input polling cooperative. */
        if(!mt_screenshot_render_row(&scene,next_row,output_row) || !mt_screenshot_png_row(&png,output_row)) {
            if(state!=CLEANUP)fail("PNG encoding failed");
            return;
        }
        ++next_row;
        if(bus.progress!=NULL)bus.progress();
        if(next_row==output_height)state=FINISH;
        return;
    }
    if(state==FINISH) {
        if(file!=NULL) {
            if(!mt_screenshot_png_end(&png))return;
            const mt_sd_result_t closed=mt_sd_file_close(file);
            if(mt_sd_file_tell(file)<0)file=NULL;
            if(closed.status!=MT_SD_OK) { fail(mt_sd_status_name(closed.status));return; }
        }
        const mt_sd_result_t renamed=mt_sd_file_rename(temporary_path,final_path);
        if(renamed.status==MT_SD_FILE_EXISTS) {
            ++counter;
            (void)snprintf(final_path,sizeof(final_path),"0:/screenshots/%s-%08lx-%08lx-%s.png",
                stem,(unsigned long)started,(unsigned long)counter,full ? "screen":"video");
            if(++attempts>=64U)fail("too many existing capture filenames");
            return;
        }
        if(renamed.status!=MT_SD_OK) { fail(mt_sd_status_name(renamed.status));return; }
        (void)snprintf(last_path,sizeof(last_path),"%s",final_path);
        (void)snprintf(message,sizeof(message),"Screenshot saved: %s",final_path+3U);
        temporary_path[0]='\0';state=IDLE;
    }
}
