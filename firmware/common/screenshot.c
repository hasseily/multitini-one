/* SPDX-License-Identifier: GPL-3.0-only */
/* Software capture matches the existing display geometry, border/text
 * overlays and color/spatial reference pipeline. No FPGA writes occur. */
#include "multitini_screenshot.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_vectrex_interface.h"
#include "multitini_video_frame.h"
#include "screenshot_font.h"
#include <string.h>

static void big32(uint8_t *p, uint32_t n)
{ p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n; }
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc=UINT32_MAX;
    while(n-- != 0U) {
        crc^=*p++;
        for(unsigned bit=0U;bit<8U;++bit) crc=(crc>>1) ^ (UINT32_C(0xedb88320) & (0U-(crc&1U)));
    }
    return ~crc;
}
static bool emit(mt_screenshot_png_t *png, const void *data, size_t size)
{
    if(png->failed || !png->write(png->context,data,size)) { png->failed=true; return false; }
    return true;
}
bool mt_screenshot_png_begin(mt_screenshot_png_t *png,uint32_t width,uint32_t height,
                            mt_screenshot_write_fn write,void *context)
{
    static const uint8_t signature[8]={137U,80U,78U,71U,13U,10U,26U,10U};
    if(png==NULL || write==NULL || width==0U || width>MT_SCREENSHOT_MAX_WIDTH || height==0U || height>1080U) return false;
    memset(png,0,sizeof(*png)); png->write=write; png->context=context;
    png->width=width; png->height=height; png->adler_a=1U;
    uint8_t *p=png->chunk;
    memcpy(p,signature,8U); big32(p+8U,13U); memcpy(p+12U,"IHDR",4U);
    big32(p+16U,width); big32(p+20U,height); p[24U]=8U; p[25U]=2U;
    big32(p+29U,crc32(p+12U,17U));
    return emit(png,p,33U);
}
bool mt_screenshot_png_row(mt_screenshot_png_t *png,const uint16_t *rgb565)
{
    if(png==NULL || rgb565==NULL || png->write==NULL || png->row>=png->height || png->failed) return false;
    uint8_t *p=png->chunk; size_t cursor=8U;
    memcpy(p+4U,"IDAT",4U);
    if(png->row==0U) { p[cursor++]=0x78U; p[cursor++]=0x01U; }
    const uint16_t bytes=(uint16_t)(1U+png->width*3U);
    const uint16_t complement=(uint16_t)~bytes;
    p[cursor++]=png->row+1U==png->height ? 1U : 0U;
    p[cursor++]=(uint8_t)bytes; p[cursor++]=(uint8_t)(bytes>>8);
    p[cursor++]=(uint8_t)complement; p[cursor++]=(uint8_t)(complement>>8);
    const size_t data=cursor;
    p[cursor++]=0U; /* PNG filter None. */
    for(uint32_t x=0U;x<png->width;++x) {
        const unsigned pixel=rgb565[x],r=pixel>>11,g=(pixel>>5)&63U,b=pixel&31U;
        p[cursor++]=(uint8_t)((r<<3)|(r>>2));
        p[cursor++]=(uint8_t)((g<<2)|(g>>4));
        p[cursor++]=(uint8_t)((b<<3)|(b>>2));
    }
    for(size_t i=data;i<cursor;++i) {
        png->adler_a=(png->adler_a+p[i])%65521U;
        png->adler_b=(png->adler_b+png->adler_a)%65521U;
    }
    if(png->row+1U==png->height) { big32(p+cursor,(png->adler_b<<16)|png->adler_a); cursor+=4U; }
    big32(p,(uint32_t)(cursor-8U)); big32(p+cursor,crc32(p+4U,cursor-4U));
    if(!emit(png,p,cursor+4U)) return false;
    ++png->row; return true;
}
bool mt_screenshot_png_end(mt_screenshot_png_t *png)
{
    if(png==NULL || png->write==NULL || png->failed || png->row!=png->height) return false;
    uint8_t end[12]={0}; memcpy(end+4U,"IEND",4U); big32(end+8U,crc32(end+4U,4U));
    if(!emit(png,end,sizeof(end))) return false;
    png->write=NULL; return true;
}

typedef struct { uint32_t w,h,num,den,vw,vh,cw,ch,cx,cy; int32_t vx,vy; } geometry_t;
static int32_t centered_origin(uint32_t screen,uint32_t image)
{
    return image<=screen ? (int32_t)((screen-image)/2U) :
        -(int32_t)((image-screen+1U)/2U);
}
static bool geometry(const mt_screenshot_scene_t *s,geometry_t *g)
{
    static const uint16_t widths[6]={1920U,1024U,1200U,1280U,1360U,1680U};
    static const uint16_t heights[6]={1080U,768U,800U,1024U,768U,1050U};
    if(s==NULL || s->pixels==NULL || s->width==0U || s->height==0U ||
       s->width>MT_VIDEO_FRAME_MAX_WIDTH || s->height>MT_VIDEO_FRAME_MAX_HEIGHT ||
       s->display_mode>=6U || s->video_multiplier>4U) return false;
    const uint8_t m=s->display_mode;
    g->w=widths[m]; g->h=heights[m]; g->num=g->den=1U;
    if(s->core_id==MT_VECTREX_CORE_ID) { g->num=m==0U ? 3U:1U; g->den=m==0U ? 2U:1U; }
    else if(s->core_id==MT_GENESIS_CORE_ID) g->num=m==0U || m==5U ? 4U:3U;
    else if(s->core_id==MT_GBC_CORE_ID) g->num=m==1U || m==2U || m==4U ? 5U:6U;
    else if(s->core_id==MT_C64_CORE_ID) g->num=m==0U ? 4U : m==1U || m==2U || m==4U ? 2U:3U;
    else return false;
    if(s->video_multiplier!=0U) { g->num=s->video_multiplier;g->den=1U; }
    g->vw=s->width*g->num/g->den; g->vh=s->height*g->num/g->den;
    if(s->video_multiplier==0U && (g->vw>g->w || g->vh>g->h)) return false;
    g->vx=centered_origin(g->w,g->vw);g->vy=centered_origin(g->h,g->vh);
    g->cw=g->vw<g->w ? g->vw:g->w;g->ch=g->vh<g->h ? g->vh:g->h;
    g->cx=g->vx>0 ? (uint32_t)g->vx:0U;g->cy=g->vy>0 ? (uint32_t)g->vy:0U;
    return true;
}
bool mt_screenshot_dimensions(const mt_screenshot_scene_t *s,uint32_t *w,uint32_t *h)
{
    geometry_t g;
    if(w==NULL || h==NULL || !geometry(s,&g)) return false;
    *w=s->full_output ? g.w:g.cw;*h=s->full_output ? g.h:g.ch;return true;
}
static bool ui_pixel(const mt_screenshot_scene_t *s,const geometry_t *g,uint32_t x,uint32_t y,uint16_t *pixel)
{
    static const uint16_t palette[16]={0x0841U,0x10a3U,0x18e4U,0x10c3U,0x2166U,0xe75eU,0xa596U,0x39d7U,
        0x6cfbU,0xf5abU,0x8ed1U,0x3186U,0x4208U,0xf800U,0x07e0U,0xffffU};
    if(!s->full_output || !s->ui_visible || s->ui==NULL) return false;
    const unsigned cw=g->w/MT_UI_COLUMNS,ch=g->h/MT_UI_ROWS,scale=g->w==1920U ? MT_UI_FONT_SCALE:1U;
    const unsigned gx=g->w==1920U ? MT_UI_GLYPH_X:(cw-7U)/2U;
    const unsigned gy=g->w==1920U ? MT_UI_GLYPH_Y:(ch-8U)/2U;
    const bool inside_grid=x<cw*MT_UI_COLUMNS && y<ch*MT_UI_ROWS;
    const unsigned column=x/cw<MT_UI_COLUMNS ? x/cw:MT_UI_COLUMNS-1U;
    const unsigned row=y/ch<MT_UI_ROWS ? y/ch:MT_UI_ROWS-1U;
    const uint16_t cell=s->ui->cells[row*MT_UI_COLUMNS+column];
    const unsigned lx=x%cw,ly=y%ch,c=cell&127U;
    bool ink=false;
    if(inside_grid && c>=32U && lx>=gx && lx<gx+7U*scale && ly>=gy && ly<gy+8U*scale)
        ink=((mt_screenshot_font[c-32U] >> (((ly-gy)/scale)*8U+(lx-gx)/scale)) & 1U)!=0U;
    if(!ink && (cell&0x8000U)!=0U) return false;
    *pixel=palette[((unsigned)cell>>(ink ? 7U:11U))&15U]; return true;
}
static int maximum(int a,int b) { return a>b ? a:b; }
static bool border_pixel(const mt_screenshot_scene_t *s,const geometry_t *g,uint32_t x,uint32_t y,uint16_t *pixel)
{
    static const uint32_t colors[16]={0x30343bU,0x424d88U,0x4b7455U,0x497c80U,0x8c5050U,0x815a86U,0x8b7150U,0x9b9da2U,
        0x575c65U,0x758ec2U,0x86ab83U,0x7cacb0U,0xba8181U,0xae87b3U,0xbbb184U,0xc7c8caU};
    if((s->border_config&1U)==0U) return false;
    int pad=(int)((s->border_config>>8)&31U); if(pad<1)pad=1;if(pad>20)pad=20;
    const int dl=g->vx-pad-1-(int)x,dr=(int)x-(g->vx+(int)g->vw)-pad;
    const int dt=g->vy-pad-1-(int)y,db=(int)y-(g->vy+(int)g->vh)-pad;
    const int distance=maximum(maximum(dl,dr),maximum(dt,db));
    const unsigned style=(s->border_config>>1)&7U;
    bool draw=distance==0;
    if(style==1U)draw=distance==0 || distance==3;
    if(style==2U)draw=draw && (((dt==0 || db==0) && (dl>=-23 || dr>=-23)) || ((dl==0 || dr==0) && (dt>=-23 || db>=-23)));
    if(style==3U)draw=draw && (((unsigned)(dt==0 || db==0 ? dl:dt)&8U)==0U);
    if(style==4U)draw=distance>=0 && distance<=3;
    if(!draw)return false;
    const uint32_t color=colors[(s->border_config>>4)&15U];
    const unsigned dim=style==4U ? (unsigned)distance:0U;
    const unsigned r=((color>>16)&255U)>>dim,gc=((color>>8)&255U)>>dim,b=(color&255U)>>dim;
    *pixel=(uint16_t)(((r>>3)<<11)|((gc>>2)<<5)|(b>>3));return true;
}
static bool scanline_dim(const geometry_t *g,uint32_t y)
{
    if(g->den==2U)return y%3U==1U;
    if(g->num==1U)return false;
    return y%g->num>=(g->num+1U)/2U;
}
bool mt_screenshot_render_row(const mt_screenshot_scene_t *s,uint32_t y,uint16_t *row)
{
    geometry_t g;
    if(row==NULL || !geometry(s,&g) || y>=(s->full_output ? g.h:g.ch))return false;
    const uint32_t output_width=s->full_output ? g.w:g.cw;
    if(!s->full_output)y+=g.cy;
    for(uint32_t output_x=0U;output_x<output_width;++output_x) {
        const uint32_t x=s->full_output ? output_x:output_x+g.cx;
        uint16_t pixel=0U;
        if(s->full_output && (ui_pixel(s,&g,x,y,&pixel) || border_pixel(s,&g,x,y,&pixel))) {
            row[output_x]=pixel;continue;
        }
        if((int32_t)x>=g.vx && (int32_t)x<g.vx+(int32_t)g.vw &&
           (int32_t)y>=g.vy && (int32_t)y<g.vy+(int32_t)g.vh) {
            const uint32_t xx=(uint32_t)((int32_t)x-g.vx),yy=(uint32_t)((int32_t)y-g.vy),sy=yy*g.den/g.num;
            const uint16_t *source=s->pixels+sy*s->width;
            const uint32_t sx=xx*g.den/g.num;
            const uint16_t center=source[sx];
            const bool first=sx==0U,last=sx+1U==s->width;
            const uint16_t left=first ? center:source[sx-1U];
            const uint16_t right=last ? center:source[sx+1U];
            pixel=mt_video_effects_reference_scaled_rgb565(s->effects,left,center,right,
                first,last,scanline_dim(&g,yy),xx,yy,g.num,g.den);
        }
        row[output_x]=pixel;
    }
    return true;
}
