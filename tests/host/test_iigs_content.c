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
    assert(mt_core_frontend_browser_folders(MT_C64_CORE_ID,MT_MENU_BROWSER_DRIVE8));
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
        assert(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_IIGS_HDD)==
            (mt_iigs_drive_hdd_unit(drive)<MT_IIGS_HDD_COUNT || drive==MT_IIGS_FLOPPY35_DRIVE));
        assert(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_IIGS_FLOPPY)==(drive==3U));
        assert(mt_core_frontend_accepts_drive_content(MT_IIGS_CORE_ID,MT_MENU_BROWSER_DRIVE8,drive,MT_CONTENT_IIGS_WOZ)==(drive==2U || drive==3U));
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
    assert(mt_iigs_media_inspect(0U,"disk.po",h,sizeof(h),33554944U,&media)==MT_CONTENT_VALID);
    assert(mt_iigs_media_inspect(9U,"large.hdv",h,sizeof(h),UINT64_C(0x100000000),&media)==MT_CONTENT_VALID);
    assert(media.kind==MT_IIGS_MEDIA_BLOCK32 && media.size==UINT64_C(0x100000000));
    assert(mt_iigs_media_inspect(9U,"limit.hdv",h,sizeof(h),MT_IIGS_MAX_HDD_BYTES,&media)==MT_CONTENT_VALID);
    assert(mt_iigs_media_inspect(9U,"overflow.hdv",h,sizeof(h),MT_IIGS_MAX_HDD_BYTES+512U,&media)==MT_CONTENT_WRONG_SIZE);
    assert(mt_iigs_media_inspect(9U,"overflow.hdv",h,sizeof(h),UINT64_MAX,&media)==MT_CONTENT_WRONG_SIZE);
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
    store32(h+12U,1U);store32(h+24U,64U);store32(h+20U,UINT32_MAX);store32(h+28U,0U);
    assert(mt_iigs_media_inspect(9U,"large.2mg",h,sizeof(h),MT_IIGS_MAX_HDD_BYTES+64U,&media)==MT_CONTENT_VALID);
    assert(media.offset==64U && media.size==MT_IIGS_MAX_HDD_BYTES && !media.writable);
    assert(mt_iigs_media_inspect(9U,"short.2mg",h,sizeof(h),MT_IIGS_MAX_HDD_BYTES+63U,&media)==MT_CONTENT_BAD_STRUCTURE);
    bool slots[MT_IIGS_MEDIA_SLOT_COUNT]={false};
    for(unsigned unit=0U;unit<MT_IIGS_HDD_COUNT;++unit) {
        const unsigned drive=mt_iigs_hdd_drive(unit),slot=mt_iigs_drive_wire_slot(drive);
        assert(drive<MT_IIGS_DRIVE_COUNT && mt_iigs_drive_hdd_unit(drive)==unit);
        assert(slot<MT_IIGS_MEDIA_SLOT_COUNT && slot!=MT_IIGS_PRAM_SLOT && !slots[slot]);
        slots[slot]=true;assert(mt_iigs_wire_slot_drive(slot)==drive);
    }
    assert(mt_iigs_drive_hdd_unit(2U)==MT_IIGS_HDD_COUNT && mt_iigs_drive_hdd_unit(3U)==MT_IIGS_HDD_COUNT);
    assert(mt_iigs_wire_slot_drive(MT_IIGS_PRAM_SLOT)==MT_IIGS_DRIVE_COUNT);
    assert(mt_iigs_wire_slot_drive(MT_IIGS_MEDIA_SLOT_COUNT)==MT_IIGS_DRIVE_COUNT);
    assert(mt_iigs_hdd_drive(MT_IIGS_HDD_COUNT)==MT_IIGS_DRIVE_COUNT);
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
    c.flags|=MT_IIGS_FLAG_FAST_FLOPPY;
    assert(mt_iigs_config_valid(&c));mt_iigs_config_encode(&c,wire);
    assert(wire[3]==UINT8_C(0x88));
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
    assert(view.media_item_count==MT_IIGS_DRIVE_COUNT+4U && view.core_option_count==11U);
    for(unsigned drive=0U;drive<MT_IIGS_DRIVE_COUNT;++drive)
        assert(view.media_items[drive].argument==drive && view.media_items[drive].action==MT_MENU_ACTION_BROWSE_IIGS_DRIVE);
    assert(strcmp(view.media_items[9].label,"Hard disk 8")==0);
    assert(strcmp(view.core_options[9].label,"Mouse sensitivity")==0 && strcmp(view.core_options[9].value,"100%")==0);
    assert(view.core_options[9].argument==9U && view.core_options[9].action==MT_MENU_ACTION_TOGGLE_CORE_OPTION);
    assert(strcmp(view.core_options[10].label,"Floppy speed")==0 && !view.core_options[10].enabled &&
        strcmp(view.core_options[10].value,"Unavailable")==0 && view.core_options[10].argument==10U);
    state.iigs_fast_floppy_supported=true;mt_core_frontend_apply(&frontend,&state,&view);
    assert(view.core_options[10].enabled && strcmp(view.core_options[10].value,"Normal")==0 &&
        strstr(view.core_options[10].help,"restarts")!=NULL);
    state.iigs_config.flags|=MT_IIGS_FLAG_FAST_FLOPPY;mt_core_frontend_apply(&frontend,&state,&view);
    assert(strcmp(view.core_options[10].value,"Fast")==0);
    state.iigs_fast_floppy_supported=false;mt_core_frontend_apply(&frontend,&state,&view);
    assert(!view.core_options[10].enabled && strcmp(view.core_options[10].value,"Unavailable")==0);
    state.iigs_config.flags=0U;
    state.iigs_mouse_sensitivity=25U;mt_core_frontend_apply(&frontend,&state,&view);
    assert(strcmp(view.core_options[9].value,"25%")==0);
    state.iigs_mouse_sensitivity=400U;mt_core_frontend_apply(&frontend,&state,&view);
    assert(strcmp(view.core_options[9].value,"400%")==0);
    assert(strstr(view.core_options[0].value,"14.3")!=NULL);
    assert(strcmp(view.core_options[8].label,"IRQ delay")==0 && strcmp(view.core_options[8].value,"On")==0);
    assert(strstr(view.core_options[8].help,"AppleTalk")!=NULL &&
        strstr(view.core_options[8].help,"interrupts are disabled")!=NULL);
    state.iigs_config.flags=UINT8_C(0x10);
    mt_core_frontend_apply(&frontend,&state,&view);
    assert(strcmp(view.core_options[8].value,"Off")==0);
    assert(strcmp(view.core_options[4].value,"On")==0 && strcmp(view.core_options[5].value,"On")==0 &&
        strcmp(view.core_options[6].value,"On")==0 && strcmp(view.core_options[7].value,"Off")==0);
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
