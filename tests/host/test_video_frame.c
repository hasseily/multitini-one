/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    const mt_video_frame_options_t enabled = {1U,0U,0U,0U};
    const mt_video_frame_options_t disabled = {0U,3U,3U,3U};
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
            const mt_video_frame_options_t options={1U,0U,blur,bloom};
            for (unsigned i=0U;i<35U;++i) input[i]=0x4208U;
            process(7U,5U,options,1U,true);
            for(unsigned i=0U;i<35U;++i) assert(output[i]==output[0]);
            const uint16_t uniform=output[0];
            process(1U,1U,options,1U,true);
            assert(output[0]==uniform);
            input[0]=0xffffU;
            process(1U,1U,options,1U,true);
            assert(output[0]==0xffffU);
            input[0]=0U;
            process(1U,1U,options,1U,true);
            assert(output[0]==0U);
        }
    }
}

static void persistence(void)
{
    const mt_video_frame_options_t strong={1U,3U,0U,0U};
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
    const unsigned center=4U*9U+4U;
    memset(input,0,81U*sizeof(*input)); input[center]=0xf800U;
    process(9U,9U,(mt_video_frame_options_t){1U,0U,1U,0U},1U,true);
    assert(output[center]==0x7800U);
    assert(output[center-1U]==0x3800U && output[center+1U]==0x3800U);
    assert(output[center-9U]==0U && output[center+9U]==0U);
    process(9U,9U,(mt_video_frame_options_t){1U,0U,2U,0U},1U,true);
    assert(output[center]==0x3800U && output[center-9U]==0x1800U);
    input[center]=0xffffU;
    process(9U,9U,(mt_video_frame_options_t){1U,0U,0U,3U},1U,true);
    assert(output[center]==0xffffU && output[center-9U]!=0U);
    assert(output[center-27U]==0U);
    input[center]=0xf800U;
    process(9U,9U,(mt_video_frame_options_t){1U,0U,0U,3U},1U,true);
    for (unsigned i=0U;i<81U;++i) assert((output[i]&0x07ffU)==0U);
    assert(history[center]==255U && history[center-1U]==0U);
    memset(input,0,81U*sizeof(*input));
    process(9U,9U,(mt_video_frame_options_t){1U,3U,3U,3U},1U,false);
    assert(history[center]==95U && history[center-1U]==0U);
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
        const mt_video_frame_options_t options={1U,(uint8_t)(strength+1U),0U,0U};
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
    const mt_video_frame_options_t bad={1U,4U,0U,0U};
    assert(!mt_video_frame_process(input,output,history,base,scratch,1U,1U,&bad,1U,true));
    const mt_video_frame_options_t good={1U,0U,0U,0U};
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
    const mt_video_frame_options_t options={1U,persistence_mode,blur,bloom};
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

/* A frozen pre-normalization result, not a second copy of the new blend.
 * Recorded from video_frame.c SHA256 94ad73aac094e669e57f309bbc8c594a571a325d80005350915e2174e904a053.
 * Covers all 64 option combinations, non-vector-aligned rows, edges and history. */
static void legacy_baseline(void)
{
    uint32_t hash=UINT32_C(2166136261);
    for(unsigned mode=0U;mode<64U;++mode) {
        for(unsigned i=0U;i<153U;++i) {
            input[i]=(uint16_t)(i*733U+mode*977U);
            history[i]=(i*UINT32_C(7919)+mode*UINT32_C(104729))&UINT32_C(0xffffff);
        }
        const mt_video_frame_options_t options={1U,(uint8_t)(mode&3U),(uint8_t)((mode>>2U)&3U),(uint8_t)(mode>>4U)};
        process(17U,9U,options,mode%3U,mode%5U==0U);
        for(unsigned i=0U;i<153U;++i) {
            hash=(hash^output[i])*UINT32_C(16777619);
            hash=(hash^history[i])*UINT32_C(16777619);
        }
    }
    assert(hash==UINT32_C(0xd180bb5d));
}

static void unaffected_bloom_and_history(void)
{
    uint16_t original[153];uint32_t prior[153], original_history[153];
    const uint32_t scales[][2]={{3U,2U},{6U,4U},{4U,1U},{6U,1U},{1U,64U},{64U,1U}};
    for(unsigned mode=0U;mode<16U;++mode) {
        const mt_video_frame_options_t options={1U,(uint8_t)(mode&3U),0U,(uint8_t)(mode>>2U)};
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

/* Acceptance uses the displayed RGB565 codes directly: no extra precision,
 * synthetic history values, or quantizer-phase averaging can conceal a blur
 * level which disappears on the display. These broad bounds describe the
 * desired detail/visibility tradeoff; they do not reproduce filter weights. */
static unsigned channel_code(uint16_t pixel, unsigned channel)
{
    const unsigned shifts[3]={11U,5U,0U}, maxima[3]={31U,63U,31U};
    return (pixel>>shifts[channel])&maxima[channel];
}

static void quantized_level_visibility(void)
{
    const unsigned shifts[3]={11U,5U,0U}, maxima[3]={31U,63U,31U};
    const geometry_t cases[]={{17U,9U,6U,1U},{17U,9U,4U,1U}};
    /* Light keeps fine details, Medium visibly softens them, and Strong may
     * disperse an isolated dot. Keep the handheld Light peak above 85%. */
    const unsigned minimum_peak_percent[2][3]={{85U,50U,15U},{76U,30U,3U}};
    const unsigned maximum_peak_percent[2][3]={{90U,62U,25U},{85U,45U,10U}};
    for(unsigned scale=0U;scale<2U;++scale) {
        const geometry_t *const g=&cases[scale];
        const unsigned center=4U*17U+8U;
        for(unsigned channel=0U;channel<3U;++channel) {
            const unsigned maximum=maxima[channel], shift=shifts[channel];
            const uint16_t full=(uint16_t)(maximum<<shift);
            unsigned prior_peak=maximum, prior_contrast=maximum;
            unsigned prior_edge_change=0U;
            for(uint8_t blur=1U;blur<=3U;++blur) {
                memset(input,0,153U*sizeof(*input));input[center]=full;
                scaled(g,0U,blur,0U,true);
                const unsigned peak=channel_code(output[center],channel);
                assert(peak*100U>=maximum*minimum_peak_percent[scale][blur-1U]);
                assert(peak*100U<=maximum*maximum_peak_percent[scale][blur-1U]);
                assert(peak<prior_peak && (prior_peak-peak)*100U>=maximum*10U);
                prior_peak=peak;
                assert(output[center-1U]==output[center+1U]);
                assert(output[center-g->width]==output[center+g->width]);
                assert(channel_code(output[center-1U],channel)>=maximum/20U);
                if(blur==1U)assert(output[center-g->width]==0U);
                else assert(output[center-g->width]!=0U);
                for(unsigned i=0U;i<153U;++i)assert((output[i]&~full)==0U);
                /* Even one-pixel stripes retain useful contrast on Light.
                 * Every subsequent mode must reduce it by visible RGB codes. */
                for(unsigned y=0U;y<g->height;++y)for(unsigned x=0U;x<g->width;++x)
                    input[y*g->width+x]=(x&1U)!=0U ? full : 0U;
                scaled(g,0U,blur,0U,true);
                const unsigned high=channel_code(output[center+1U],channel);
                const unsigned low=channel_code(output[center],channel);
                assert(high>=low);
                const unsigned contrast=high-low;
                assert(contrast<prior_contrast && (prior_contrast-contrast)*100U>=maximum*10U);
                prior_contrast=contrast;
                if(blur==1U) {
                    const unsigned detail_floor=scale==0U ? 70U : 55U;
                    assert(contrast*100U>=maximum*detail_floor);
                    assert(contrast*100U<=maximum*80U);
                }
                /* Midtone edges test quantization without white clipping.
                 * Each mode must make a distinct stored-code change, stay
                 * monotone, and preserve the flat regions beyond its radius. */
                const unsigned dark=maximum/4U, light=maximum*3U/4U;
                for(unsigned y=0U;y<g->height;++y)for(unsigned x=0U;x<g->width;++x)
                    input[y*g->width+x]=(uint16_t)((x<8U ? dark : light)<<shift);
                scaled(g,0U,blur,0U,true);
                const unsigned shoulder=channel_code(output[center-1U],channel);
                const unsigned top=channel_code(output[center],channel);
                assert(shoulder>dark && top<light);
                const unsigned edge_change=(shoulder-dark)+(light-top);
                assert(edge_change>prior_edge_change);
                prior_edge_change=edge_change;
                for(unsigned y=0U;y<g->height;++y) {
                    const unsigned row=y*g->width;
                    assert(channel_code(output[row+4U],channel)==dark);
                    assert(channel_code(output[row+11U],channel)==light);
                    for(unsigned x=1U;x<g->width;++x)
                        assert(channel_code(output[row+x],channel)>=channel_code(output[row+x-1U],channel));
                }
                printf("RGB565 scale%u channel%u blur%u: peak %u/%u, stripe contrast %u/%u, midtone edge change %u codes\n",
                    g->num,channel,(unsigned)blur,peak,maximum,contrast,maximum,edge_change);
            }
        }
    }
}

int main(void)
{
    identity(); uniform_and_edges(); persistence(); persistence_extinction();
    blur_and_bloom(); invalid();
    legacy_baseline();scaled_identity();unaffected_bloom_and_history();
    quantized_level_visibility();
    puts("CPU1 video frame effects passed");
    return 0;
}
