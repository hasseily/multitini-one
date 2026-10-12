/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "appletini_phosphor_reference.h"

enum { TEST_PIXELS = 65536 };
static uint16_t input[MT_VIDEO_FRAME_MAX_PIXELS], output[MT_VIDEO_FRAME_MAX_PIXELS];
static uint32_t history[MT_VIDEO_FRAME_MAX_PIXELS], base[MT_VIDEO_FRAME_MAX_PIXELS];
static uint32_t scratch[MT_VIDEO_FRAME_MAX_PIXELS];

static void process(uint32_t width, uint32_t height, mt_video_frame_options_t options,
                    uint32_t steps, bool reset)
{
    assert(mt_video_frame_process(input, output, history, base, scratch,
                                   width, height, &options, steps, reset));
}

static void identity(void)
{
    const mt_video_frame_options_t enabled = {1U,0U,0U,0U,0U};
    const mt_video_frame_options_t disabled = {0U,3U,3U,3U,0U};
    for (unsigned i=0U;i<TEST_PIXELS;++i) input[i]=(uint16_t)i;
    process(256U,256U,enabled,1U,true);
    assert(memcmp(input,output,TEST_PIXELS*sizeof(*input))==0);
    process(256U,256U,disabled,1U,false);
    assert(memcmp(input,output,TEST_PIXELS*sizeof(*input))==0);
}

static void uniform_and_edges(void)
{
    for (uint8_t blur=0U;blur<=3U;++blur) {
        for (uint8_t bloom=0U;bloom<=3U;++bloom) {
            const mt_video_frame_options_t options={1U,0U,blur,bloom,0U};
            for (unsigned i=0U;i<35U;++i) input[i]=0x4208U;
            process(7U,5U,options,1U,true);
            for(unsigned i=0U;i<35U;++i) assert(output[i]==output[0]);
            const uint16_t uniform=output[0];
            process(1U,1U,options,1U,true);
            (void)uniform;
            input[0]=0xffffU;
            process(1U,1U,options,1U,true);
            assert(output[0]==0xffffU);
            input[0]=0U;
            process(1U,1U,options,1U,true);
            assert(output[0]==0U);
        }
    }
}

static void blur_preserves_solid_rgb565(void)
{
    enum { W=13, H=3, N=W*H };
    /* Exercise every packed color, including dark odd green codes that the
     * separately truncated Appletini taps could darken. The geometry covers
     * clamped edges, both axes, and vector-sized interiors with scalar tails. */
    for(unsigned color=0U;color<65536U;++color) {
        for(unsigned i=0U;i<N;++i)input[i]=(uint16_t)color;
        for(uint8_t blur=1U;blur<=3U;++blur) {
            process(W,H,(mt_video_frame_options_t){1U,0U,blur,0U,0U},1U,true);
            for(unsigned i=0U;i<N;++i) {
                assert(output[i]==color);
                assert(base[i]==history[i]);
            }
        }
    }
}

static void persistence(void)
{
    const mt_video_frame_options_t strong={1U,3U,0U,0U,0U};
    input[0]=0xffffU;
    process(1U,1U,strong,1U,true);
    assert(history[0]==0x00ffffffU);
    input[0]=0U;
    process(1U,1U,strong,1U,false);
    assert(history[0]==0x005f5f5fU); /* 255 * 24 / 64 */
    history[0]=0x00404040U;
    process(1U,1U,strong,1U,false);
    assert(history[0]==0x00383838U); /* brightness >=32, not bit 0x20 */
    process(1U,1U,strong,0U,false);
    assert(history[0]==0x00383838U);
    process(1U,1U,strong,64U,false);
    assert(history[0]==0U && output[0]==0U);
    history[0]=0x00ffffffU;
    process(1U,1U,strong,1U,true);
    assert(history[0]==0U && output[0]==0U);
    input[0]=0x07e0U;history[0]=0xffU;
    process(1U,1U,strong,0U,false);
    assert(output[0]==0xffe0U); /* independent channel maximum */
}

static void blur_and_bloom(void)
{
    const uint32_t scales[][2]={{1U,1U},{3U,2U},{4U,1U},{6U,1U}};
    const uint16_t centers[3]={0xef7dU,0xce79U,0x9492U};
    const uint16_t horizontal_near[3]={0x0841U,0x0841U,0x1082U};
    const uint16_t vertical_near[3]={0U,0x0841U,0x0841U};
    const uint16_t diagonal[3]={0U,0x0020U,0x0841U};
    memset(input,0,81U*sizeof(*input));input[40]=0xffffU;
    for(unsigned scale=0U;scale<4U;++scale)for(uint8_t blur=1U;blur<=3U;++blur) {
        const mt_video_frame_options_t options={1U,0U,blur,0U,0U};
        assert(mt_video_frame_process_scaled(input,output,history,base,scratch,9U,9U,
            scales[scale][0],scales[scale][1],&options,1U,true));
        /* These packed values make the strength and support visible after
         * RGB565 quantization rather than only in intermediate RGB888. */
        assert(output[40]==centers[blur-1U]);
        assert(output[39]==horizontal_near[blur-1U] && output[41]==output[39]);
        assert(output[31]==vertical_near[blur-1U] && output[49]==output[31]);
        assert(output[30]==diagonal[blur-1U] && output[32]==output[30]);
        assert(output[38]==(blur==3U ? 0x0841U : 0U) && output[42]==output[38]);
        assert(output[29]==(blur==3U ? 0x0020U : 0U) && output[33]==output[29]);
        assert(output[37]==0U && output[43]==0U && output[22]==0U && output[58]==0U);
        for(unsigned i=0U;i<81U;++i)assert(history[i]==(i==40U ? 0xffffffU : 0U));
    }
}

/* Independent pre-optimization recurrence: always run the requested steps,
 * with no expiration table or call into the production implementation. */
static uint32_t reference_decay(uint32_t pixel, unsigned strength, unsigned steps)
{
    const unsigned bright[3]={14U,18U,24U}, knee[3]={40U,48U,56U};
    const unsigned tail[3]={58U,60U,61U};
    unsigned rgb[3]={pixel&255U,(pixel>>8)&255U,(pixel>>16)&255U};
    for (unsigned step=0U;step<steps;++step) {
        unsigned maximum=0U;
        for (unsigned channel=0U;channel<3U;++channel)
            if(rgb[channel]>maximum)maximum=rgb[channel];
        const unsigned factor=maximum>=128U ? bright[strength] :
            maximum>=32U ? knee[strength] : tail[strength];
        bool visible=false;
        for (unsigned channel=0U;channel<3U;++channel) {
            rgb[channel]=rgb[channel]*factor/64U;
            visible=visible || rgb[channel]>=4U;
        }
        if(!visible)rgb[0]=rgb[1]=rgb[2]=0U;
    }
    return rgb[0] | (rgb[1]<<8) | (rgb[2]<<16);
}

static void persistence_extinction(void)
{
    const unsigned bright[3]={14U,18U,24U}, knee[3]={40U,48U,56U};
    const unsigned tail[3]={58U,60U,61U}, bounds[3]={18U,24U,33U};
    for (unsigned strength=0U;strength<3U;++strength) {
        unsigned longest=0U;
        /* Since all channels share a coefficient selected by their maximum,
         * monotonic floor multiplication makes this exhaustive over every
         * possible RGB maximum, not just a white test pixel. */
        for (unsigned maximum=0U;maximum<256U;++maximum) {
            unsigned value=maximum, steps=0U;
            do {
                value=value*(value>=128U ? bright[strength] :
                    value>=32U ? knee[strength] : tail[strength])/64U;
                ++steps;
                if(value<4U)value=0U;
            } while(value!=0U);
            if(steps>longest)longest=steps;
            input[maximum]=(uint16_t)(maximum*257U);
            history[maximum]=maximum | ((maximum/2U)<<8) | ((maximum/3U)<<16);
        }
        assert(longest==bounds[strength]);
        const mt_video_frame_options_t options={1U,(uint8_t)(strength+1U),0U,0U,0U};
        process(256U,1U,options,bounds[strength],false);
        assert(memcmp(input,output,256U*sizeof(*input))==0);
        /* 127 lies below the bright-decay knee and survives longer than
         * white, so checking only 255 would give unsafe shorter bounds. */
        input[0]=0U;history[0]=0x007f7f7fU;
        process(1U,1U,options,bounds[strength]-1U,false);
        assert(history[0]!=0U); /* The bound is tight. */
        history[0]=0x007f7f7fU;
        process(1U,1U,options,bounds[strength],false);
        assert(history[0]==0U);
        for (unsigned which=0U;which<3U;++which) {
            const unsigned steps=which==0U ? bounds[strength]-1U :
                which==1U ? bounds[strength] : 64U;
            uint32_t expected[256];
            for(unsigned maximum=0U;maximum<256U;++maximum) {
                const unsigned shift=(maximum%3U)*8U;
                history[maximum]=(maximum<<shift) |
                    ((maximum/2U)<<((shift+8U)%24U)) |
                    ((maximum/3U)<<((shift+16U)%24U));
                expected[maximum]=reference_decay(history[maximum],strength,steps);
                input[maximum]=0U;
            }
            process(256U,1U,options,steps,false);
            assert(memcmp(history,expected,sizeof(expected))==0);
        }
    }
}

static void invalid(void)
{
    const mt_video_frame_options_t bad={1U,4U,0U,0U,0U};
    assert(!mt_video_frame_process(input,output,history,base,scratch,1U,1U,&bad,1U,true));
    const mt_video_frame_options_t bad_horizontal={1U,0U,0U,0U,4U};
    assert(!mt_video_frame_process(input,output,history,base,scratch,1U,1U,&bad_horizontal,1U,true));
    const mt_video_frame_options_t good={1U,0U,0U,0U,0U};
    assert(!mt_video_frame_process(input,output,history,base,scratch,0U,1U,&good,1U,true));
    assert(!mt_video_frame_process(input,output,history,base,scratch,641U,1U,&good,1U,true));
    assert(!mt_video_frame_process(input,output,history,base,scratch,1U,721U,&good,1U,true));
    assert(!mt_video_frame_process(NULL,output,history,base,scratch,1U,1U,&good,1U,true));
    const uint32_t invalid_scales[][2]={{0U,1U},{1U,0U},{65U,1U},{1U,65U},{UINT32_MAX,1U}};
    for(unsigned i=0U;i<sizeof(invalid_scales)/sizeof(invalid_scales[0]);++i) {
        output[0]=0x1234U;history[0]=0x123456U;base[0]=0xabcdefU;scratch[0]=0x987654U;
        assert(!mt_video_frame_process_scaled(input,output,history,base,scratch,1U,1U,
            invalid_scales[i][0],invalid_scales[i][1],&good,1U,true));
        assert(output[0]==0x1234U && history[0]==0x123456U && base[0]==0xabcdefU && scratch[0]==0x987654U);
    }
}

typedef struct { uint32_t width,height,num,den; } geometry_t;
static const geometry_t geometries[]={
    {160U,144U,6U,1U},   /* Current GBC: 960x864 viewport. */
    {240U,160U,6U,1U},   /* Generic GBA example, not a shipped core. */
    {382U,270U,4U,1U},
    {404U,250U,4U,1U},
    {540U,720U,3U,2U}
};

static void scaled(const geometry_t *g, uint8_t persistence_mode, uint8_t blur,
                   uint8_t bloom, bool reset)
{
    const mt_video_frame_options_t options={1U,persistence_mode,blur,bloom,0U};
    assert(mt_video_frame_process_scaled(input,output,history,base,scratch,
        g->width,g->height,g->num,g->den,&options,0U,reset));
}

static void scaled_identity(void)
{
    for(unsigned kind=0U;kind<sizeof(geometries)/sizeof(geometries[0]);++kind) {
        const geometry_t *const g=&geometries[kind];
        const unsigned pixels=g->width*g->height;
        for(unsigned i=0U;i<pixels;++i)input[i]=(uint16_t)(i*733U);
        scaled(g,0U,0U,0U,true);
        assert(memcmp(input,output,pixels*sizeof(*input))==0);
        for(uint8_t blur=1U;blur<=3U;++blur) {
            for(unsigned i=0U;i<pixels;++i)input[i]=0x5b8eU;
            scaled(g,0U,blur,0U,true);
            for(unsigned i=0U;i<pixels;++i)assert(output[i]==input[i]);
        }
    }
}

static uint16_t reference_pack(uint32_t p)
{
    return (uint16_t)(((p&0xf8U)<<8)|((p&0xfc00U)>>5)|((p&0xf80000U)>>19));
}

/* Independent normalized spatial oracle. Keep the imported Appletini
 * functions unchanged for Glow and the prior Blur comparison below. */
static void reference_blur_h(const uint32_t *row,uint32_t *result,
                             unsigned width,unsigned mode)
{
    static const unsigned narrow[3]={1U,2U,1U},wide[5]={1U,2U,2U,2U,1U};
    const unsigned count=mode==3U ? 5U:3U,denominator=mode==3U ? 8U:4U;
    const unsigned *const weights=mode==3U ? wide:narrow;
    if(mode==0U || width<4U) {
        memcpy(result,row,width*sizeof(*result));return;
    }
    for(unsigned x=0U;x<width;++x) {
        uint32_t value=0U;
        for(unsigned channel=0U;channel<24U;channel+=8U) {
            unsigned sum=0U;
            for(unsigned tap=0U;tap<count;++tap) {
                const int position=(int)x+(int)tap-(int)(count/2U);
                const unsigned clamped=position<0 ? 0U:
                    (unsigned)position>=width ? width-1U:(unsigned)position;
                sum+=weights[tap]*((row[clamped]>>channel)&255U);
            }
            value|=((sum+denominator/2U)/denominator)<<channel;
        }
        result[x]=value;
    }
}

static void reference_blur_v(const uint32_t *up,const uint32_t *mid,
                             const uint32_t *dn,uint32_t *result,
                             unsigned width,unsigned mode)
{
    for(unsigned x=0U;x<width;++x) {
        uint32_t value=mid[x];
        if(mode>=2U) {
            value=0U;
            for(unsigned channel=0U;channel<24U;channel+=8U) {
                const unsigned sum=((up[x]>>channel)&255U)+
                    2U*((mid[x]>>channel)&255U)+((dn[x]>>channel)&255U);
                value|=((sum+2U)/4U)<<channel;
            }
        }
        result[x]=value;
    }
}

static uint32_t reference_blur_mix(uint32_t original,uint32_t blurred,unsigned mode)
{
    if(mode==0U)return original;
    const unsigned denominator=mode==1U ? 8U:mode==2U ? 4U:2U;
    uint32_t result=0U;
    for(unsigned channel=0U;channel<24U;channel+=8U) {
        const unsigned source=(original>>channel)&255U;
        const unsigned smooth=(blurred>>channel)&255U;
        result|=(((denominator-1U)*source+smooth+denominator/2U)/denominator)<<channel;
    }
    return result;
}

static void blur_step_strengths(void)
{
    enum { W=9,H=9,N=W*H };
    const uint16_t vertical_step[3][W]={
        {0U,0U,0U,0x0841U,0xf7beU,0xffffU,0xffffU,0xffffU,0xffffU},
        {0U,0U,0U,0x1082U,0xef7dU,0xffffU,0xffffU,0xffffU,0xffffU},
        {0U,0U,0x1082U,0x3186U,0xce79U,0xef7dU,0xffffU,0xffffU,0xffffU}
    };
    const uint16_t horizontal_dark[3]={0U,0x1082U,0x2104U};
    const uint16_t horizontal_bright[3]={0xffffU,0xef7dU,0xdefbU};
    uint32_t appletini_h[N],appletini_base[N];
    for(unsigned axis=0U;axis<2U;++axis) {
        for(unsigned y=0U;y<H;++y)for(unsigned x=0U;x<W;++x)
            input[y*W+x]=(axis==0U ? x>=W/2U:y>=H/2U) ? 0xffffU:0U;
        for(uint8_t blur=1U;blur<=3U;++blur) {
            process(W,H,(mt_video_frame_options_t){1U,0U,blur,0U,0U},1U,true);
            for(unsigned y=0U;y<H;++y)for(unsigned x=0U;x<W;++x) {
                const uint16_t expected=axis==0U ? vertical_step[blur-1U][x]:
                    y==H/2U-1U ? horizontal_dark[blur-1U]:
                    y==H/2U ? horizontal_bright[blur-1U]:
                    y<H/2U ? 0U:0xffffU;
                assert(output[y*W+x]==expected);
            }
            /* Prove the new strength moves less light across this edge
             * than the previous Appletini kernel at the same menu level. */
            for(unsigned y=0U;y<H;++y)
                effect_blur_h_row(history+y*W,appletini_h+y*W,W,blur);
            for(unsigned y=0U;y<H;++y) {
                const unsigned up=y ? y-1U:0U,dn=y+1U<H ? y+1U:y;
                effect_blur_v_row(appletini_h+up*W,appletini_h+y*W,appletini_h+dn*W,
                    appletini_base+y*W,W,blur>=2U);
            }
            if(axis==0U || blur>=2U) {
                const unsigned dark=axis==0U ? 4U*W+3U:3U*W+4U;
                const unsigned bright=4U*W+4U;
                assert(output[dark]<reference_pack(appletini_base[dark]));
                assert(output[bright]>reference_pack(appletini_base[bright]));
            }
        }
    }
}

static void phosphor_reference(void)
{
    const unsigned widths[]={1U,2U,3U,4U,7U,17U},heights[]={1U,2U,3U,9U};
    const unsigned scales[][2]={{1U,1U},{3U,2U},{4U,1U},{6U,1U}};
    uint32_t expected_h[153],expected_base[153],halo_h[153],halo[153],expected_history[153];
    for(unsigned wi=0;wi<6U;++wi)for(unsigned hi=0;hi<4U;++hi)
    for(unsigned scale=0;scale<4U;++scale)for(uint8_t blur=0;blur<4U;++blur)
    for(uint8_t glow=0;glow<4U;++glow)for(uint8_t dot=0;dot<4U;++dot) {
        const unsigned w=widths[wi],h=heights[hi];
        for(unsigned i=0;i<w*h;++i) {
            input[i]=0U;history[i]=(i*7919U+blur*104729U+glow*733U)&0xffffffU;
            expected_history[i]=history[i];
        }
        const mt_video_frame_options_t options={1U,1U,blur,glow,dot};
        assert(mt_video_frame_process_scaled(input,output,history,base,scratch,w,h,
            scales[scale][0],scales[scale][1],&options,0U,false));
        assert(memcmp(history,expected_history,w*h*sizeof(*history))==0);
        for(unsigned y=0;y<h;++y) {
            reference_blur_h(history+y*w,expected_h+y*w,w,blur);
            effect_glow_h_row(history+y*w,halo_h+y*w,(int)w);
        }
        for(unsigned y=0;y<h;++y) {
            const unsigned up=y ? y-1U:0U,dn=y+1U<h ? y+1U:y;
            reference_blur_v(expected_h+up*w,expected_h+y*w,expected_h+dn*w,
                expected_base+y*w,w,blur);
            effect_glow_v_row(halo_h+up*w,halo_h+y*w,halo_h+dn*w,halo+y*w,(int)w);
        }
        for(unsigned i=0;i<w*h;++i) {
            expected_base[i]=reference_blur_mix(history[i],expected_base[i],blur);
            assert(base[i]==expected_base[i]);
            uint32_t p=0U;
            for(unsigned shift=0;shift<24U;shift+=8U) {
                unsigned value=(expected_base[i]>>shift)&255U;
                if(glow)value+=((halo[i]>>shift)&255U)>>(4U-glow);
                if(value>255U)value=255U;
                p|=value<<shift;
            }
            assert(output[i]==reference_pack(p));
        }
    }
}

static void unaffected_bloom_and_history(void)
{
    uint16_t original[153];uint32_t prior[153], original_history[153];
    const uint32_t scales[][2]={{3U,2U},{6U,4U},{4U,1U},{6U,1U},{1U,64U},{64U,1U}};
    for(unsigned mode=0U;mode<16U;++mode) {
        const mt_video_frame_options_t options={1U,(uint8_t)(mode&3U),0U,(uint8_t)(mode>>2U),0U};
        for(unsigned i=0U;i<153U;++i) {
            input[i]=(uint16_t)(i*733U+mode*977U);
            prior[i]=history[i]=(i*UINT32_C(7919)+mode*UINT32_C(104729))&UINT32_C(0xffffff);
        }
        process(17U,9U,options,1U,false);
        memcpy(original,output,sizeof(original));memcpy(original_history,history,sizeof(original_history));
        for(unsigned scale=0U;scale<sizeof(scales)/sizeof(scales[0]);++scale) {
            for(uint8_t blur=0U;blur<=3U;++blur) {
                mt_video_frame_options_t changed=options;changed.blur=blur;
                memcpy(history,prior,sizeof(prior));
                assert(mt_video_frame_process_scaled(input,output,history,base,scratch,17U,9U,
                    scales[scale][0],scales[scale][1],&changed,1U,false));
                assert(memcmp(history,original_history,sizeof(original_history))==0);
                if(blur==0U)assert(memcmp(output,original,sizeof(original))==0);
            }
        }
    }
}

static void fixed_radius_bloom(void)
{
    enum { W=17, H=9, N=W*H };
    const unsigned shifts[3]={11U,5U,0U}, rgb_shifts[3]={0U,8U,16U};
    const unsigned coefficients[3]={1U,2U,1U};
    const uint16_t visible_halo[3]={0x0020U,0x0861U,0x18e3U};
    const uint32_t scales[][2]={{3U,2U},{4U,1U},{6U,1U}};
    uint32_t persisted[N], smoothed[N];
    for(unsigned scale=0U;scale<3U;++scale) {
        /* A white line makes even Light's halo visible after RGB565 packing.
         * Strong must brighten the same neighboring row/column, never reach
         * a second one. Test both axes without relying on a C/HDL oracle. */
        for(unsigned axis=0U;axis<2U;++axis) {
            for(unsigned y=0U;y<H;++y)for(unsigned x=0U;x<W;++x)
                input[y*W+x]=(axis==0U ? x==W/2U : y==H/2U) ? 0xffffU : 0U;
            for(uint8_t strength=1U;strength<=3U;++strength) {
                const mt_video_frame_options_t options={1U,0U,0U,strength,0U};
                assert(mt_video_frame_process_scaled(input,output,history,base,scratch,W,H,
                    scales[scale][0],scales[scale][1],&options,1U,true));
                const unsigned center=(H/2U)*W+W/2U, step=axis==0U ? 1U : W;
                assert(output[center]==0xffffU);
                assert(output[center-step]==visible_halo[strength-1U]);
                assert(output[center+step]==visible_halo[strength-1U]);
                assert(output[center-2U*step]==0U && output[center+2U*step]==0U);
            }
        }
        /* Check the halo against an independent 3x3 stencil, including
         * clamped edges, RGB channels, saturation and every smoothing pair.
         * The halo comes from persisted pixels, never the smoothed base. */
        for(unsigned y=0U;y<H;++y)for(unsigned x=0U;x<W;++x)
            input[y*W+x]=(uint16_t)((y*W+x)*733U);
        for(uint8_t horizontal=0U;horizontal<=3U;++horizontal)
            for(uint8_t vertical=0U;vertical<=3U;++vertical) {
                mt_video_frame_options_t options={1U,3U,vertical,0U,horizontal};
                for(unsigned i=0U;i<N;++i)history[i]=(i*7919U)&0xffffffU;
                assert(mt_video_frame_process_scaled(input,output,history,base,scratch,W,H,
                    scales[scale][0],scales[scale][1],&options,1U,false));
                memcpy(persisted,history,sizeof(persisted));memcpy(smoothed,base,sizeof(smoothed));
                for(uint8_t strength=1U;strength<=3U;++strength) {
                    options.bloom=strength;
                    /* Zero elapsed decay reuses exactly the same persisted
                     * image while checking that bloom never enters history. */
                    memcpy(history,persisted,sizeof(persisted));
                    assert(mt_video_frame_process_scaled(input,output,history,base,scratch,W,H,
                        scales[scale][0],scales[scale][1],&options,0U,false));
                    assert(memcmp(history,persisted,sizeof(persisted))==0);
                    assert(memcmp(base,smoothed,sizeof(smoothed))==0);
                    for(unsigned y=0U;y<H;++y)for(unsigned x=0U;x<W;++x) {
                        uint16_t expected=0U;
                        for(unsigned channel=0U;channel<3U;++channel) {
                            unsigned halo=0U;
                            for(int dy=-1;dy<=1;++dy) {
                                const int sy=(int)y+dy;
                                const unsigned cy=sy<0 ? 0U : sy>=H ? H-1U : (unsigned)sy;
                                unsigned row=0U;
                                for(int dx=-1;dx<=1;++dx) {
                                    const int sx=(int)x+dx;
                                    const unsigned cx=sx<0 ? 0U : sx>=W ? W-1U : (unsigned)sx;
                                    row+=coefficients[dx+1]*((persisted[cy*W+cx]>>rgb_shifts[channel])&255U);
                                }
                                halo+=coefficients[dy+1]*(row/4U);
                            }
                            unsigned value=((smoothed[y*W+x]>>rgb_shifts[channel])&255U)+
                                ((halo/4U)>>(4U-strength));
                            if(value>255U)value=255U;
                            expected|=(uint16_t)((value>>(channel==1U ? 2U : 3U))<<shifts[channel]);
                        }
                        assert(output[y*W+x]==expected);
                    }
                }
            }
    }
}

int main(void)
{
    identity(); uniform_and_edges(); blur_preserves_solid_rgb565(); persistence(); persistence_extinction();
    blur_and_bloom(); blur_step_strengths(); invalid();
    phosphor_reference();scaled_identity();unaffected_bloom_and_history();fixed_radius_bloom();

    puts("CPU1 video frame effects passed");
    return 0;
}
