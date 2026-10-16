/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_screenshot.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_iigs_interface.h"
#include "multitini_video_frame.h"
#include "multitini_vectrex_interface.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool file_write(void *context,const void *bytes,size_t size)
{ return fwrite(bytes,1U,size,(FILE *)context)==size; }
static bool reject_write(void *context,const void *bytes,size_t size)
{ (void)context;(void)bytes;(void)size;return false; }
static uint16_t source[MT_VIDEO_FRAME_MAX_PIXELS],row[MT_SCREENSHOT_MAX_WIDTH],cropped_row[MT_SCREENSHOT_MAX_WIDTH];
static mt_ui_frame_t ui;
static void full_screen_ui(void)
{
    static const unsigned screen_w[]={1920U,1024U,1200U,1280U,1360U,1680U};
    static const unsigned screen_h[]={1080U,768U,800U,1024U,768U,1050U};
    static const uint8_t color_index[]={8U,13U,14U};
    static const uint16_t colors[]={0x6cfbU,0xf800U,0x07e0U};
    mt_screenshot_scene_t scene={0};scene.pixels=source;
    scene.core_id=MT_GBC_CORE_ID;scene.width=160U;scene.height=144U;
    scene.full_output=true;scene.ui_visible=true;scene.ui=&ui;
    for(unsigned i=0U;i<160U*144U;++i)source[i]=0x18e3U;
    for(unsigned mode=0U;mode<6U;++mode) {
        scene.display_mode=(uint8_t)mode;
        const unsigned cw=screen_w[mode]/MT_UI_COLUMNS,ch=screen_h[mode]/MT_UI_ROWS;
        const unsigned grid_w=cw*MT_UI_COLUMNS,grid_h=ch*MT_UI_ROWS;
        /* Different backgrounds in every adjacent row and column detect
         * incorrect edge addressing and a fixed-color remainder fallback. */
        for(unsigned r=0U;r<MT_UI_ROWS;++r)for(unsigned c=0U;c<MT_UI_COLUMNS;++c)
            ui.cells[r*MT_UI_COLUMNS+c]=mt_ui_cell(' ',MT_UI_COLOR_WHITE,
                (mt_ui_color_t)color_index[(r+c)%3U],false);
        for(unsigned y=0U;y<screen_h[mode];++y) {
            assert(mt_screenshot_render_row(&scene,y,row));
            const unsigned r=y/ch<MT_UI_ROWS ? y/ch:MT_UI_ROWS-1U;
            for(unsigned x=0U;x<screen_w[mode];++x) {
                const unsigned c=x/cw<MT_UI_COLUMNS ? x/cw:MT_UI_COLUMNS-1U;
                assert(row[x]==colors[(r+c)%3U]);
            }
        }
        /* Glyphs must stop at the true grid. Transparent edge cells pass
         * the actual core/letterbox image through their inherited strips. */
        for(unsigned r=0U;r<MT_UI_ROWS;++r)for(unsigned c=0U;c<MT_UI_COLUMNS;++c) {
            const bool transparent=(r==MT_UI_ROWS-1U || c==MT_UI_COLUMNS-1U) && ((r+c)&1U)==0U;
            ui.cells[r*MT_UI_COLUMNS+c]=mt_ui_cell('W',MT_UI_COLOR_WHITE,
                (mt_ui_color_t)color_index[(r+c)%3U],transparent);
        }
        for(unsigned y=0U;y<screen_h[mode];++y) {
            scene.ui_visible=false;assert(mt_screenshot_render_row(&scene,y,cropped_row));
            scene.ui_visible=true;assert(mt_screenshot_render_row(&scene,y,row));
            const unsigned r=y/ch<MT_UI_ROWS ? y/ch:MT_UI_ROWS-1U;
            for(unsigned x=0U;x<screen_w[mode];++x) {
                const bool corner=(x==0U || x+1U==screen_w[mode]) && (y==0U || y+1U==screen_h[mode]);
                if(x<grid_w && y<grid_h && !corner)continue;
                const unsigned c=x/cw<MT_UI_COLUMNS ? x/cw:MT_UI_COLUMNS-1U;
                const uint16_t cell=ui.cells[r*MT_UI_COLUMNS+c];
                const uint16_t expected=(cell&0x8000U)!=0U ? cropped_row[x]:colors[(r+c)%3U];
                assert(row[x]==expected);
            }
        }
    }
}
static void manual_geometry(void)
{
    static const uint32_t cores[]={MT_C64_CORE_ID,MT_GBC_CORE_ID,MT_VECTREX_CORE_ID,MT_GENESIS_CORE_ID,MT_IIGS_CORE_ID,MT_IIGS_CORE_ID};
    static const unsigned widths[]={382U,160U,540U,320U,640U,724U},heights[]={270U,144U,720U,240U,400U,496U};
    static const unsigned screen_w[]={1920U,1024U,1200U,1280U,1360U,1680U};
    static const unsigned screen_h[]={1080U,768U,800U,1024U,768U,1050U};
    mt_screenshot_scene_t scene={0};scene.pixels=source;
    for(unsigned c=0U;c<sizeof(cores)/sizeof(cores[0]);++c)for(unsigned mode=0U;mode<6U;++mode)for(unsigned scale=1U;scale<=4U;++scale) {
        scene.core_id=cores[c];scene.width=widths[c];scene.height=heights[c];
        scene.display_mode=(uint8_t)mode;scene.video_multiplier=(uint8_t)scale;
        for(unsigned y=0U;y<scene.height;++y)for(unsigned x=0U;x<scene.width;++x)
            source[y*scene.width+x]=(uint16_t)((y*37U+x*113U)&65535U);
        const unsigned vw=widths[c]*scale,vh=heights[c]*scale;
        const unsigned cw=vw<screen_w[mode] ? vw:screen_w[mode],ch=vh<screen_h[mode] ? vh:screen_h[mode];
        /* Independent half-open centered rectangle, using floor for odd
         * negative offsets as the FPGA's signed arithmetic shift does. */
        const int dx=(int)screen_w[mode]-(int)vw,dy=(int)screen_h[mode]-(int)vh;
        const int vx=dx>=0 ? dx/2:-((-dx+1)/2),vy=dy>=0 ? dy/2:-((-dy+1)/2);
        const unsigned cx=vx>0 ? (unsigned)vx:0U,cy=vy>0 ? (unsigned)vy:0U;
        uint32_t w,h;scene.full_output=false;
        assert(mt_screenshot_dimensions(&scene,&w,&h) && w==cw && h==ch);
        const unsigned rows[]={0U,ch/2U,ch-1U};
        for(unsigned r=0U;r<3U;++r) {
            scene.full_output=false;assert(mt_screenshot_render_row(&scene,rows[r],cropped_row));
            scene.full_output=true;assert(mt_screenshot_dimensions(&scene,&w,&h) && w==screen_w[mode] && h==screen_h[mode]);
            assert(mt_screenshot_render_row(&scene,rows[r]+cy,row));
            assert(memcmp(cropped_row,row+cx,cw*sizeof(uint16_t))==0);
            for(unsigned x=0U;x<screen_w[mode];++x) {
                const int xx=(int)x-vx,yy=(int)(rows[r]+cy)-vy;
                const uint16_t expected=xx>=0 && (unsigned)xx<vw && yy>=0 && (unsigned)yy<vh ?
                    source[((unsigned)yy/scale)*widths[c]+(unsigned)xx/scale]:0U;
                assert(row[x]==expected);
            }
        }
        scene.full_output=false;assert(!mt_screenshot_render_row(&scene,ch,row));
    }
    /* Odd excess must clip one extra output pixel at the left/top edge. */
    scene.core_id=MT_C64_CORE_ID;scene.display_mode=1U;scene.width=343U;scene.height=257U;scene.video_multiplier=3U;
    for(unsigned y=0U;y<257U;++y)for(unsigned x=0U;x<343U;++x)source[y*343U+x]=(uint16_t)(y*343U+x);
    scene.full_output=false;uint32_t w,h;
    assert(mt_screenshot_dimensions(&scene,&w,&h) && w==1024U && h==768U);
    assert(mt_screenshot_render_row(&scene,0U,row) && row[0]==source[1U] && row[1023U]==source[342U]);
    assert(mt_screenshot_render_row(&scene,767U,row) && row[0]==source[256U*343U+1U]);
    scene.video_multiplier=5U;assert(!mt_screenshot_dimensions(&scene,&w,&h));
}
static void iigs_geometry(void)
{
    /* Literal FPGA MACHINE=5 Auto viewports, independent of the C decoder. */
    static const unsigned screen_w[]={1920U,1024U,1200U,1280U,1360U,1680U};
    static const unsigned screen_h[]={1080U,768U,800U,1024U,768U,1050U};
    static const unsigned view_w[]={1280U,640U,640U,1280U,640U,1280U};
    static const unsigned view_h[]={800U,400U,400U,800U,400U,800U};
    static const unsigned left[]={320U,192U,280U,0U,360U,200U};
    static const unsigned top[]={140U,184U,200U,112U,184U,125U};
    mt_screenshot_scene_t scene={0};
    scene.pixels=source;scene.width=640U;scene.height=400U;scene.core_id=MT_IIGS_CORE_ID;
    for(unsigned y=0U;y<400U;++y)for(unsigned x=0U;x<640U;++x)
        source[y*640U+x]=(uint16_t)((y*37U+x*113U)&65535U);
    for(unsigned mode=0U;mode<6U;++mode) {
        uint32_t w,h;scene.display_mode=(uint8_t)mode;scene.full_output=false;
        assert(mt_screenshot_dimensions(&scene,&w,&h) && w==view_w[mode] && h==view_h[mode]);
        const unsigned rows[]={0U,view_h[mode]/2U,view_h[mode]-1U};
        for(unsigned r=0U;r<3U;++r) {
            const unsigned y=rows[r];
            scene.full_output=false;assert(mt_screenshot_render_row(&scene,y,cropped_row));
            scene.full_output=true;assert(mt_screenshot_dimensions(&scene,&w,&h) && w==screen_w[mode] && h==screen_h[mode]);
            assert(mt_screenshot_render_row(&scene,y+top[mode],row));
            assert(memcmp(cropped_row,row+left[mode],view_w[mode]*sizeof(uint16_t))==0);
            for(unsigned x=0U;x<screen_w[mode];++x) {
                const uint16_t expected=x>=left[mode] && x<left[mode]+view_w[mode] ?
                    source[(y*400U/view_h[mode])*640U+(x-left[mode])*640U/view_w[mode]]:0U;
                assert(row[x]==expected);
            }
        }
        assert(mt_screenshot_render_row(&scene,0U,row));
        for(unsigned x=0U;x<screen_w[mode];++x)assert(row[x]==0U);
    }
    scene.core_id=UINT32_C(0xDEADBEEF);scene.display_mode=0U;
    uint32_t w,h;
    assert(!mt_screenshot_dimensions(&scene,&w,&h));
    assert(!mt_screenshot_render_row(&scene,0U,row));
}
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
    assert(row[1151U]==0xffffU && row[1152U]==0xffffU && row[1199U]==0xffffU);
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
    settings.scanlines=2U;settings.mask=MT_VIDEO_MASK_SHADOW;settings.softness=MT_VIDEO_SMOOTH_STRONG;
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
    mt_video_effects_defaults(&settings);settings.enabled=1U;settings.softness=MT_VIDEO_SMOOTH_STRONG;
    scene.width=2U;scene.height=1U;scene.full_output=false;source[0]=0xffffU;source[1]=0U;
    /* Appletini spot halves repeat three times at this 6:1 enlargement.
     * Strong alone carries a small contribution into the far gap half. */
    const uint16_t spots[3][4]={{0xffffU,0xbdf7U,0x4208U,0U},
        {0xffffU,0x9cf3U,0x630cU,0U},{0xf7beU,0x8c71U,0x738eU,0x0841U}};
    for(unsigned level=1U;level<=3U;++level) {
        settings.softness=(uint8_t)level;assert(mt_video_effects_build(&settings,&tables));
        assert(mt_screenshot_render_row(&scene,0U,row));
        assert(row[0]==spots[level-1U][0] && row[3]==spots[level-1U][1] &&
               row[5]==spots[level-1U][1] && row[6]==spots[level-1U][2] && row[11]==spots[level-1U][3]);
    }
    source[0]=0xc618U;source[1]=0x39e7U;
    assert(mt_screenshot_render_row(&scene,0U,row) && row[0]==0xc5f8U && row[11]==0x3a07U);
    /* A neighboring opaque menu cell does not truncate source interpolation.
     * At x509 the first source pixel before the menu blends toward white. */
    scene.width=160U;scene.height=144U;scene.full_output=true;scene.ui_visible=true;
    memset(source,0,160U*144U*sizeof(uint16_t));source[72U*160U+5U]=0xffffU;
    for(unsigned i=0U;i<MT_UI_COLUMNS*MT_UI_ROWS;++i)ui.cells[i]=0x8020U;
    ui.cells[30U*MT_UI_COLUMNS+34U]=(uint16_t)(' '|(15U<<11));
    assert(mt_screenshot_render_row(&scene,540U,row));
    assert(row[509U]==0x738eU && row[510U]==0xffffU);
    settings.negative=1U;assert(mt_video_effects_build(&settings,&tables));
    assert(mt_screenshot_render_row(&scene,540U,row) && row[510U]==0xffffU);
    scene.width=MT_VIDEO_FRAME_MAX_WIDTH+1U;assert(!mt_screenshot_dimensions(&scene,&w,&h));
}
static void iigs_border_geometry(void)
{
    const unsigned sw[]={1920U,1024U,1200U,1280U,1360U,1680U};
    const unsigned sh[]={1080U,768U,800U,1024U,768U,1050U};
    const unsigned vw[]={1448U,724U,724U,724U,724U,1448U};
    const unsigned vh[]={992U,496U,496U,496U,496U,992U};
    const unsigned left[]={236U,150U,238U,278U,318U,116U};
    const unsigned top[]={44U,136U,152U,264U,136U,29U};
    mt_screenshot_scene_t scene={0};scene.pixels=source;
    scene.width=724U;scene.height=496U;scene.core_id=MT_IIGS_CORE_ID;
    for(unsigned i=0U;i<724U*496U;++i)source[i]=(uint16_t)(i*113U+37U);
    for(unsigned mode=0U;mode<6U;++mode) {
        uint32_t w,h;scene.display_mode=(uint8_t)mode;scene.full_output=false;
        assert(mt_screenshot_dimensions(&scene,&w,&h) && w==vw[mode] && h==vh[mode]);
        const unsigned rows[]={0U,vh[mode]/2U,vh[mode]-1U};
        for(unsigned r=0U;r<3U;++r) {
            const unsigned y=rows[r];scene.full_output=false;
            assert(mt_screenshot_render_row(&scene,y,cropped_row));
            scene.full_output=true;
            assert(mt_screenshot_dimensions(&scene,&w,&h) && w==sw[mode] && h==sh[mode]);
            assert(mt_screenshot_render_row(&scene,y+top[mode],row));
            assert(memcmp(cropped_row,row+left[mode],vw[mode]*sizeof(uint16_t))==0);
            for(unsigned x=0U;x<sw[mode];++x) {
                const uint16_t expected=x>=left[mode] && x<left[mode]+vw[mode] ?
                    source[(y*496U/vh[mode])*724U+(x-left[mode])*724U/vw[mode]]:0U;
                assert(row[x]==expected);
            }
        }
    }
    scene.display_mode=0U;scene.height=637U;
    uint32_t w,h;assert(!mt_screenshot_dimensions(&scene,&w,&h));
    assert(!mt_screenshot_render_row(&scene,0U,row));
    scene.height=1U;scene.width=MT_VIDEO_FRAME_MAX_WIDTH+1U;
    assert(!mt_screenshot_dimensions(&scene,&w,&h));
}
int main(int argc,char **argv)
{
    renderer();
    iigs_geometry();
    iigs_border_geometry();
    manual_geometry();
    full_screen_ui();
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
