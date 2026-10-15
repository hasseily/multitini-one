/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_iigs_hid_input.h"
#include "multitini_controls.h"
#include <assert.h>
#include <stdio.h>
static uint16_t codes[512],values[512]; static unsigned count; static int reject;
static int emit(void *ctx,uint16_t code,uint16_t value)
{ (void)ctx; if(reject)return -1; assert(count<512U); codes[count]=code; values[count++]=value; return 0; }
static bool find(uint16_t code,uint16_t value)
{ for(unsigned i=0U;i<count;++i)if(codes[i]==code && values[i]==value)return true; return false; }
static void test_mouse_sensitivity(void)
{
    const uint16_t steps[]={25U,50U,75U,100U,150U,200U,300U,400U};
    mt_iigs_hid_input_t in;
    assert(!mt_iigs_mouse_sensitivity_valid(0U) && !mt_iigs_mouse_sensitivity_valid(125U));
    assert(mt_iigs_mouse_next_sensitivity(400U)==25U && mt_iigs_mouse_next_sensitivity(0U)==100U);
    for(unsigned s=0U;s<8U;++s) {
        mt_iigs_hid_init(&in,emit,NULL);count=0U;assert(mt_iigs_hid_poll(&in)==0);
        assert(in.mouse_scale_quarters==4U && mt_iigs_mouse_sensitivity_valid(steps[s]));
        assert(mt_iigs_mouse_next_sensitivity(steps[s])==steps[(s+1U)%8U]);
        assert(mt_iigs_hid_set_mouse_sensitivity(&in,steps[s]));
        mt_iigs_hid_mouse_report(&in,32,32,5U);
        int32_t x=0,y=0;
        do {
            count=0U;assert(mt_iigs_hid_poll(&in)==0);
            for(unsigned i=0U;i<count;++i) {
                if(codes[i]==0x4000U) { x+=(int8_t)(values[i]>>8U);assert((values[i]&7U)==5U); }
                if(codes[i]==0x4001U)y+=(int8_t)(values[i]&255U);
            }
        } while(in.mouse_x!=0 || in.mouse_y!=0 || in.mouse_pending);
        assert(x==(int32_t)steps[s]*32/100 && y== -x);
    }
    mt_iigs_hid_init(&in,emit,NULL);count=0U;assert(mt_iigs_hid_poll(&in)==0);
    assert(mt_iigs_hid_set_mouse_sensitivity(&in,25U));
    for(unsigned i=0U;i<3U;++i) {
        mt_iigs_hid_mouse_report(&in,1,1,0U);count=0U;
        assert(mt_iigs_hid_poll(&in)==0 && count==0U);
    }
    assert(in.mouse_fraction_x==3 && in.mouse_fraction_y== -3);
    mt_iigs_hid_mouse_report(&in,-1,-1,0U);
    assert(in.mouse_x==0 && in.mouse_y==0 && in.mouse_fraction_x==2 && in.mouse_fraction_y== -2);
    mt_iigs_hid_mouse_report(&in,1,1,1U);count=0U;
    assert(mt_iigs_hid_poll(&in)==0 && find(0x4000U,0x0009U)); /* Buttons do not wait for a whole pixel. */
    mt_iigs_hid_mouse_report(&in,1,1,1U);count=0U;
    assert(mt_iigs_hid_poll(&in)==0 && find(0x4000U,0x0129U));
    assert(in.mouse_fraction_x==0 && in.mouse_fraction_y==0);
    mt_iigs_hid_mouse_report(&in,1,1,1U);
    assert(mt_iigs_hid_set_mouse_sensitivity(&in,400U));
    assert(in.mouse_fraction_x==0 && in.mouse_fraction_y==0);
    assert(!mt_iigs_hid_set_mouse_sensitivity(&in,125U) && in.mouse_scale_quarters==16U);
    mt_iigs_hid_mouse_report(&in,INT16_MAX,INT16_MIN,7U);
    assert(in.mouse_x==32767 && in.mouse_y==32767);
    mt_iigs_hid_mouse_report(&in,INT16_MIN,INT16_MAX,0U);
    assert(in.mouse_x== -32767 && in.mouse_y== -32767);
    mt_iigs_hid_clear_mouse_motion(&in);assert(in.mouse_x==0 && in.mouse_y==0);
    assert(mt_iigs_hid_set_mouse_sensitivity(&in,25U));mt_iigs_hid_mouse_report(&in,1,1,0U);
    count=0U;assert(mt_iigs_hid_release_all(&in)==0);
    assert(in.mouse_fraction_x==0 && in.mouse_fraction_y==0 && in.mouse_scale_quarters==1U);
    /* A failed packet remains exact when sensitivity changes between retries. */
    mt_iigs_hid_mouse_report(&in,16,-16,1U);reject=1;assert(mt_iigs_hid_poll(&in)==-1);
    const uint32_t packet=in.mouse_packet;
    assert(mt_iigs_hid_set_mouse_sensitivity(&in,400U));
    reject=0;count=0U;assert(mt_iigs_hid_poll(&in)==0 && in.mouse_packet==packet);
    assert(find(0x4000U,(uint16_t)packet) && find(0x4001U,(uint16_t)(packet>>16U)));
}
int main(void)
{
    mt_iigs_hid_input_t in; mt_iigs_hid_init(&in,emit,NULL);
    mt_controller_config_t pad;mt_controls_default(MT_IIGS_CORE_ID,NULL,&pad);
    assert(pad.binding[MT_CONTROL_UP]==MT_CONTROL_SOURCE_HAT);
    assert(pad.binding[MT_CONTROL_AXIS_X]==MT_CONTROL_SOURCE_AXIS+MT_GAMEPAD_AXIS_X);
    assert(mt_iigs_hid_poll(&in)==0); count=0U;
    const uint8_t keys[]={0x04U,0x44U};
    assert(mt_iigs_hid_keyboard_report(&in,0U,0x08U,keys,2U)==0);
    assert(find(0x1fU,3U) && find(0x1cU,1U) && find(0x78U,1U));
    const unsigned before=count; const uint8_t rollover[]={1U};
    assert(mt_iigs_hid_keyboard_report(&in,0U,0U,rollover,1U)==-2 && count==before);
    assert(mt_iigs_hid_disconnect(&in,0U)==0 && find(0x1cU,0U) && find(0x1fU,2U));
    count=0U;
    assert(mt_iigs_hid_assigned_report(&in,0U,0U,7U,INT16_MIN,INT16_MAX,0U)==0);
    assert(find(0x4100U,0x70U) && find(0x4101U,0xff00U));
    count=0U; assert(mt_iigs_hid_disconnect(&in,0U)==0 && find(0x4100U,0U) && find(0x4101U,0x8080U));
    count=0U; mt_iigs_hid_mouse_report(&in,4,-5,1U); reject=1;
    assert(mt_iigs_hid_poll(&in)==-1); reject=0;
    assert(mt_iigs_hid_poll(&in)==0 && find(0x4000U,0x0409U) && find(0x4001U,0x0105U));
    count=0U; assert(mt_iigs_hid_poll(&in)==0 && count==0U);
    mt_iigs_hid_mouse_report(&in,300,0,0U);
    assert(mt_iigs_hid_poll(&in)==0 && find(0x4000U,0x7f08U));
    assert(mt_iigs_hid_poll(&in)==0); assert(mt_iigs_hid_poll(&in)==0);
    count=0U; assert(mt_iigs_hid_poll(&in)==0 && count==0U);
    test_mouse_sensitivity();
    puts("Apple IIgs keyboard, joystick, mouse sensitivity, retry and release checks passed"); return 0;
}
