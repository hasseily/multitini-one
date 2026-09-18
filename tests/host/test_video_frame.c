/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_video_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { TEST_PIXELS = 65536 };
static uint16_t input[TEST_PIXELS], output[TEST_PIXELS];
static uint32_t history[TEST_PIXELS], base[TEST_PIXELS], scratch[TEST_PIXELS];

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
    assert(memcmp(input,output,sizeof(input))==0);
    process(256U,256U,disabled,1U,false);
    assert(memcmp(input,output,sizeof(input))==0);
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
}

int main(void)
{
    identity(); uniform_and_edges(); persistence(); persistence_extinction();
    blur_and_bloom(); invalid();
    puts("CPU1 video frame effects passed");
    return 0;
}
