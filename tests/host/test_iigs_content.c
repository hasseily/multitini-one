/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_iigs_content.h"
#include "multitini_core_frontend.h"
#include "multitini_media_browser.h"
#include "multitini_regs.h"
#include "multitini_c64_interface.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void store32(uint8_t *p,uint32_t v) { for(unsigned i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(i*8U)); }
static void test_nested_browser(void)
{
    mt_media_browser_t browser;
    char relative[MT_IIGS_MEDIA_NAME_MAX+1U], full[256];
    assert(mt_core_frontend_browser_folders(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8));
    assert(mt_core_frontend_browser_folders(0U,MT_MENU_BROWSER_DOCUMENTS));
    assert(!mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_DRIVE8));
    assert(mt_media_browser_init(&browser,"0:/multitini/cores/apple-iigs/media"));
    assert(mt_media_browser_enter(&browser,"Operating Systems"));
    assert(mt_media_browser_enter(&browser,"GS OS"));
    assert(mt_media_browser_resolve(&browser,"System.2mg",relative,sizeof(relative)));
    assert(strcmp(relative,"Operating Systems/GS OS/System.2mg")==0 && mt_iigs_media_path_valid(relative));
    assert(mt_media_browser_path(&browser,full,sizeof(full)) && strstr(full,"/media/Operating Systems/GS OS")!=NULL);
    assert(mt_media_browser_up(&browser) && strcmp(browser.relative,"Operating Systems")==0);
    assert(mt_media_browser_up(&browser) && !mt_media_browser_can_up(&browser));
    assert(!mt_media_browser_up(&browser));
    for(unsigned drive=0U;drive<MT_IIGS_DRIVE_COUNT;++drive) {
        assert(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_IIGS_HDD)==(drive<=2U));
        assert(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_IIGS_FLOPPY)==(drive==3U));
        assert(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_IIGS_WOZ)==(drive>=2U));
        assert(!mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_PRG));
    }
    assert(!mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,MT_IIGS_DRIVE_COUNT,MT_CONTENT_IIGS_HDD));
    static const char *const invalid[]={"","../disk.hdv","A/../disk.hdv","./disk.hdv","A//disk.hdv","/disk.hdv","0:/disk.hdv","A\\disk.hdv","A./disk.hdv","A /disk.hdv","A/disk.hdv/"};
    for(size_t i=0U;i<sizeof(invalid)/sizeof(invalid[0]);++i)assert(!mt_iigs_media_path_valid(invalid[i]));
    char boundary[MT_IIGS_MEDIA_NAME_MAX+2U];memset(boundary,'a',sizeof(boundary));
    boundary[MT_IIGS_MEDIA_NAME_MAX]='\0';assert(mt_iigs_media_path_valid(boundary));
    boundary[MT_IIGS_MEDIA_NAME_MAX]='a';boundary[MT_IIGS_MEDIA_NAME_MAX+1U]='\0';assert(!mt_iigs_media_path_valid(boundary));
}
int main(void)
{
    uint8_t h[512]={0}, wire[16]; mt_iigs_media_info_t media;
    assert(mt_iigs_media_inspect(0U,"disk.hdv",h,sizeof(h),1024U,&media)==MT_CONTENT_VALID);
    assert(media.offset==0U && media.size==1024U && media.writable);
    assert(mt_iigs_media_inspect(0U,"disk.po",h,sizeof(h),33554432U,&media)==MT_CONTENT_VALID);
    assert(mt_iigs_media_inspect(0U,"disk.po",h,sizeof(h),33554944U,&media)==MT_CONTENT_WRONG_SIZE);
    assert(mt_iigs_media_inspect(0U,"disk.hdv",h,sizeof(h),513U,&media)==MT_CONTENT_WRONG_SIZE);
    memcpy(h,"2IMGTEST",8U); h[8]=64U; h[10]=1U;
    store32(h+12U,1U); store32(h+20U,2U); store32(h+24U,128U); store32(h+28U,1024U);
    assert(mt_iigs_media_inspect(1U,"disk.2MG",h,sizeof(h),1152U,&media)==MT_CONTENT_VALID);
    assert(media.offset==128U && media.size==1024U);
    store32(h+16U,UINT32_C(0x80000000));
    assert(mt_iigs_media_inspect(0U,"disk.2mg",h,sizeof(h),1152U,&media)==MT_CONTENT_VALID && !media.writable);
    store32(h+24U,UINT32_MAX);
    assert(mt_iigs_media_inspect(0U,"disk.2mg",h,sizeof(h),1152U,&media)==MT_CONTENT_BAD_STRUCTURE);
    store32(h+24U,128U); store32(h+12U,0U);
    assert(mt_iigs_media_inspect(0U,"disk.2mg",h,sizeof(h),1152U,&media)==MT_CONTENT_UNSUPPORTED_VERSION);
    memset(h,0,sizeof(h)); memcpy(h,"WOZ2\xff\x0a\x0d\x0a",8U); memcpy(h+12U,"INFO",4U); store32(h+16U,60U); h[20]=2U; h[21]=2U;
    assert(mt_iigs_media_inspect(2U,"disk.woz",h,sizeof(h),16000U,&media)==MT_CONTENT_VALID);
    assert(media.kind==MT_IIGS_MEDIA_WOZ);
    assert(mt_iigs_media_inspect(3U,"disk.woz",h,sizeof(h),16000U,&media)==MT_CONTENT_BAD_STRUCTURE);
    h[21]=1U; h[22]=1U;
    assert(mt_iigs_media_inspect(3U,"disk.WOZ",h,sizeof(h),16000U,&media)==MT_CONTENT_VALID && !media.writable);
    assert(mt_iigs_media_inspect(3U,"disk.dsk",h,sizeof(h),143360U,&media)==MT_CONTENT_VALID);
    assert(media.raw_format==MT_IIGS_RAW_DOS525 && media.kind==MT_IIGS_MEDIA_WOZ);
    assert(mt_iigs_media_inspect(3U,"disk.dsk",h,sizeof(h),143361U,&media)==MT_CONTENT_WRONG_SIZE);
    assert(mt_iigs_media_inspect(2U,"disk.dsk",h,sizeof(h),143360U,&media)==MT_CONTENT_UNSUPPORTED_VERSION);
    assert(mt_iigs_media_inspect(0U,"disk.dsk",h,sizeof(h),143360U,&media)==MT_CONTENT_UNSUPPORTED_VERSION);
    assert(mt_iigs_media_inspect(3U,"disk.nib",h,sizeof(h),232960U,&media)==MT_CONTENT_UNSUPPORTED_VERSION);
    assert(mt_iigs_media_inspect(2U,"disk.po",h,sizeof(h),819200U,&media)==MT_CONTENT_VALID);
    assert(media.raw_format==MT_IIGS_RAW_PRODOS35 && media.offset==0U && media.size==819200U);
    assert(mt_iigs_media_inspect(2U,"disk.po",h,sizeof(h),819712U,&media)==MT_CONTENT_WRONG_SIZE);
    assert(mt_iigs_media_inspect(2U,"disk.hdv",h,sizeof(h),819200U,&media)==MT_CONTENT_UNSUPPORTED_VERSION);
    memset(h,0,sizeof(h));memcpy(h,"2IMG",4U);h[8]=64U;h[10]=1U;
    store32(h+12U,1U);store32(h+20U,1600U);store32(h+24U,128U);store32(h+28U,819200U);
    assert(mt_iigs_media_inspect(2U,"disk.2mg",h,sizeof(h),819352U,&media)==MT_CONTENT_VALID);
    assert(media.raw_format==MT_IIGS_RAW_PRODOS35 && media.offset==128U && media.size==819200U && media.writable);
    store32(h+28U,0U);
    assert(mt_iigs_media_inspect(2U,"WOOF.2mg",h,sizeof(h),819352U,&media)==MT_CONTENT_VALID);
    assert(media.size==819200U && media.offset==128U);
    assert(mt_iigs_media_inspect(2U,"WOOF.2mg",h,sizeof(h),819327U,&media)==MT_CONTENT_BAD_STRUCTURE);
    store32(h+20U,UINT32_MAX);
    assert(mt_iigs_media_inspect(2U,"overflow.2mg",h,sizeof(h),819352U,&media)==MT_CONTENT_BAD_STRUCTURE);
    store32(h+20U,1600U);store32(h+28U,819200U);
    store32(h+16U,UINT32_C(0x80000000));
    assert(mt_iigs_media_inspect(2U,"disk.2mg",h,sizeof(h),819352U,&media)==MT_CONTENT_VALID && !media.writable);
    assert(mt_iigs_media_inspect(3U,"disk.2mg",h,sizeof(h),819352U,&media)==MT_CONTENT_UNSUPPORTED_VERSION);
    store32(h+28U,818688U);store32(h+20U,1599U);
    assert(mt_iigs_media_inspect(2U,"disk.2mg",h,sizeof(h),819352U,&media)==MT_CONTENT_WRONG_SIZE);
    mt_iigs_config_t c={1U,4U,0U,8U,UINT32_C(0x12345678),1U};
    assert(mt_iigs_config_valid(&c)); mt_iigs_config_encode(&c,wire);
    assert(wire[0]==1U && wire[1]==4U && wire[4]==0x78U && wire[7]==0x12U && wire[8]==1U && wire[15]==0U);
    c.speed=5U; assert(mt_iigs_config_valid(&c));c.speed=6U;assert(mt_iigs_config_valid(&c));
    c.speed=7U; assert(!mt_iigs_config_valid(&c));
    assert(mt_iigs_next_speed(4U,MT_IIGS_ACCEL_LEGACY_SPEED_MASK)==0U);
    assert(mt_iigs_next_speed(4U,MT_IIGS_ACCEL_SPEED_MASK)==5U);
    assert(mt_iigs_next_speed(5U,MT_IIGS_ACCEL_SPEED_MASK)==6U);
    assert(mt_iigs_next_speed(6U,MT_IIGS_ACCEL_SPEED_MASK)==0U);
    assert(mt_iigs_next_speed(0U,UINT32_C(0x21))==5U && mt_iigs_next_speed(5U,UINT32_C(0x21))==0U);
    assert(mt_iigs_next_speed(4U,0U)==0U);
    mt_core_frontend_t frontend; mt_menu_view_t view={0};
    mt_core_frontend_state_t state={0}; state.core_id=MT_IIGS_CORE_ID; state.services_ready=true; state.iigs_config.speed=4U;
    mt_core_frontend_apply(&frontend,&state,&view);
    assert(view.media_item_count==8U && view.core_option_count==9U);
    assert(strstr(view.core_options[0].value,"14.3")!=NULL);
    state.iigs_speed_mask=MT_IIGS_ACCEL_SPEED_MASK;state.iigs_config.speed=5U;
    mt_core_frontend_apply(&frontend,&state,&view);assert(strcmp(view.core_options[0].value,"28.6 MHz")==0);
    state.iigs_config.speed=6U;mt_core_frontend_apply(&frontend,&state,&view);assert(strcmp(view.core_options[0].value,"57.3 MHz")==0);
    state.iigs_speed_mask=MT_IIGS_ACCEL_LEGACY_SPEED_MASK;
    mt_core_frontend_apply(&frontend,&state,&view);assert(strcmp(view.core_options[0].value,"Unavailable")==0);
    assert(view.media_items[3].argument==3U && view.media_items[3].action==MT_MENU_ACTION_BROWSE_IIGS_DRIVE);
    assert(mt_core_frontend_accepts_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,MT_CONTENT_IIGS_HDD));
    assert(mt_content_classify("HDISK.2MG")==MT_CONTENT_IIGS_HDD);
    test_nested_browser();
    puts("Apple IIgs content, config and menu checks passed"); return 0;
}
