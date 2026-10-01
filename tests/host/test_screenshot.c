/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_screenshot.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_vectrex_interface.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool file_write(void *context,const void *bytes,size_t size)
{ return fwrite(bytes,1U,size,(FILE *)context)==size; }
static bool reject_write(void *context,const void *bytes,size_t size)
{ (void)context;(void)bytes;(void)size;return false; }
static uint16_t source[640U*720U],row[MT_SCREENSHOT_MAX_WIDTH],cropped_row[MT_SCREENSHOT_MAX_WIDTH];
static mt_ui_frame_t ui;
static void renderer(void)
{
    static const uint32_t cores[]={MT_C64_CORE_ID,MT_GBC_CORE_ID,MT_VECTREX_CORE_ID,MT_GENESIS_CORE_ID};
    static const unsigned widths[]={382U,160U,540U,320U},heights[]={270U,144U,720U,240U};
    static const unsigned screen_w[]={1920U,1024U,1200U,1280U,1360U,1680U};
    static const unsigned screen_h[]={1080U,768U,800U,1024U,768U,1050U};
    static const unsigned scale[4][6]={{4U,2U,2U,3U,2U,3U},{6U,5U,5U,6U,5U,6U},
        {3U,1U,1U,1U,1U,1U},{4U,3U,3U,3U,3U,4U}};
    mt_screenshot_scene_t scene={0};uint32_t w,h;
    scene.pixels=source;
    for(unsigned c=0U;c<4U;++c)for(unsigned mode=0U;mode<6U;++mode) {
        scene.core_id=cores[c];scene.width=widths[c];scene.height=heights[c];scene.display_mode=(uint8_t)mode;
        for(unsigned i=0U;i<scene.width*scene.height;++i)source[i]=0xf800U;
        scene.full_output=false;
        const unsigned denominator=c==2U && mode==0U ? 2U:1U;
        assert(mt_screenshot_dimensions(&scene,&w,&h) &&
               w==widths[c]*scale[c][mode]/denominator && h==heights[c]*scale[c][mode]/denominator);
        assert(mt_screenshot_render_row(&scene,h-1U,row) && row[w-1U]==0xf800U);
        scene.full_output=true;
        assert(mt_screenshot_dimensions(&scene,&w,&h) && w==screen_w[mode] && h==screen_h[mode]);
        assert(mt_screenshot_render_row(&scene,h/2U,row) && row[w/2U]==0xf800U);
        assert(row[0]==0U && row[w-1U]==0U);
        assert(!mt_screenshot_render_row(&scene,h,row));
    }
    scene.core_id=MT_GBC_CORE_ID;scene.width=160U;scene.height=144U;scene.display_mode=0U;
    /* At 1080p the GBC viewport is x480..1439, y108..971. */
    scene.border_config=1U|(7U<<4)|(6U<<8);
    assert(mt_screenshot_render_row(&scene,101U,row));
    assert(row[473U]!=0U && row[960U]!=0U && row[1446U]!=0U && row[472U]==0U);
    scene.border_config=1U|(1U<<1)|(7U<<4)|(6U<<8);
    assert(mt_screenshot_render_row(&scene,98U,row) && row[960U]!=0U);
    scene.border_config=1U|(2U<<1)|(7U<<4)|(6U<<8);
    assert(mt_screenshot_render_row(&scene,101U,row) && row[473U]!=0U && row[960U]==0U);
    /* Opaque/transparent text behavior and compact overlay edge clipping. */
    scene.ui=&ui;scene.ui_visible=true;
    for(unsigned i=0U;i<MT_UI_COLUMNS*MT_UI_ROWS;++i)ui.cells[i]=(uint16_t)(' '|(15U<<11));
    assert(mt_screenshot_render_row(&scene,100U,row) && row[0]==0xffffU && row[960U]==0xffffU);
    for(unsigned i=0U;i<MT_UI_COLUMNS*MT_UI_ROWS;++i)ui.cells[i]=0x8020U;
    assert(mt_screenshot_render_row(&scene,500U,row) && row[960U]==0xf800U);
    /* A is 0x08 on glyph row zero: its middle pixel, repeated at 2x.
     * Transparent background must leave pixels outside those strokes alone. */
    ui.cells[0]=(uint16_t)('A'|(15U<<7)|0x8000U);
    assert(mt_screenshot_render_row(&scene,MT_UI_GLYPH_Y,row));
    assert(row[MT_UI_GLYPH_X+5U]==0U && row[MT_UI_GLYPH_X+6U]==0xffffU &&
           row[MT_UI_GLYPH_X+7U]==0xffffU && row[MT_UI_GLYPH_X+8U]==0U);
    scene.display_mode=2U;
    for(unsigned i=0U;i<MT_UI_COLUMNS*MT_UI_ROWS;++i)ui.cells[i]=(uint16_t)(' '|(15U<<11));
    assert(mt_screenshot_render_row(&scene,10U,row));
    assert(row[1151U]==0xffffU && row[1152U]==0U);
    scene.ui_visible=false;scene.border_config=0U;scene.display_mode=0U;
    mt_video_effects_settings_t settings;mt_video_effects_tables_t tables;
    mt_video_effects_defaults(&settings);settings.enabled=1U;settings.negative=1U;
    assert(mt_video_effects_build(&settings,&tables));scene.effects=&tables;
    assert(mt_screenshot_render_row(&scene,500U,row));
    assert(row[960U]==mt_video_effects_reference_rgb565(&tables,0xf800U));
    /* PrtScr preserves output filtering at viewport resolution, without UI. */
    scene.full_output=false;scene.ui_visible=true;
    assert(mt_screenshot_dimensions(&scene,&w,&h) && w==960U && h==864U);
    assert(mt_screenshot_render_row(&scene,0U,row) &&
           row[0]==mt_video_effects_reference_rgb565(&tables,0xf800U));
    settings.scanlines=2U;settings.mask=MT_VIDEO_MASK_SHADOW;settings.softness=MT_VIDEO_FILTER_STRONG;
    assert(mt_video_effects_build(&settings,&tables));
    for(unsigned y=0U;y<scene.height;++y)for(unsigned x=0U;x<scene.width;++x)
        source[y*scene.width+x]=(uint16_t)((x*113U+y*37U)&65535U);
    for(unsigned i=0U;i<MT_UI_COLUMNS*MT_UI_ROWS;++i)ui.cells[i]=(uint16_t)(' '|(15U<<11));
    for(unsigned y=0U;y<12U;++y) {
        scene.full_output=false;scene.ui_visible=true;
        assert(mt_screenshot_render_row(&scene,y,cropped_row));
        scene.full_output=true;scene.ui_visible=false;
        assert(mt_screenshot_render_row(&scene,y+108U,row));
        assert(memcmp(cropped_row,row+480U,960U*sizeof(uint16_t))==0);
    }
    scene.width=641U;assert(!mt_screenshot_dimensions(&scene,&w,&h));
}
int main(int argc,char **argv)
{
    renderer();
    mt_screenshot_png_t png;
    assert(!mt_screenshot_png_begin(&png,0U,1U,reject_write,NULL));
    assert(!mt_screenshot_png_begin(&png,1U,0U,reject_write,NULL));
    assert(!mt_screenshot_png_begin(&png,1921U,1U,reject_write,NULL));
    assert(!mt_screenshot_png_begin(&png,1U,1U,reject_write,NULL));
    assert(!mt_screenshot_png_row(&png,row));
    if(argc>1) {
        FILE *output=fopen(argv[1],"wb");assert(output!=NULL);
        assert(mt_screenshot_png_begin(&png,1920U,3U,file_write,output));
        assert(!mt_screenshot_png_end(&png));
        for(unsigned y=0U;y<3U;++y) {
            for(unsigned x=0U;x<1920U;++x)row[x]=(uint16_t)((x*37U+y*1009U)&65535U);
            assert(mt_screenshot_png_row(&png,row));
        }
        assert(!mt_screenshot_png_row(&png,row));
        assert(mt_screenshot_png_end(&png));assert(!mt_screenshot_png_end(&png));
        assert(fclose(output)==0);
    }
    puts("PASS screenshot geometry, borders, overlay, effects, PNG bounds");return 0;
}
