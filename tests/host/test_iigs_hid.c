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
    puts("Apple IIgs keyboard, joystick, mouse retry and release checks passed"); return 0;
}
