/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_hid_input.h"
#include <assert.h>
#include <string.h>
static struct { uint16_t code,value; } events[512];static unsigned count;static bool reject;
static int emit(void *ctx,uint16_t code,uint16_t value)
{ (void)ctx;if(reject)return -1;assert(count<512U);events[count].code=code;events[count++].value=value;return 0; }
int main(void)
{
    mt_amiga_hid_input_t input;mt_amiga_hid_init(&input,emit,NULL);
    const uint8_t keys[]={4U,0x1eU};assert(mt_amiga_hid_keyboard_report(&input,0U,2U,keys,2U)==0);
    assert(count==3U && events[0].code==0x6000U && events[0].value==1U && events[1].value==0x20U && events[2].value==0x60U);
    assert(mt_amiga_hid_keyboard_report(&input,1U,2U,keys,2U)==0);assert(count==3U);
    assert(mt_amiga_hid_disconnect(&input,0U)==0);assert(count==3U);
    assert(mt_amiga_hid_disconnect(&input,1U)==0);assert(count==6U && events[3].value==0x81U);
    assert(mt_amiga_hid_assigned_report(&input,0U,0U,9U,1U)==0);
    assert(events[count-1U].code==0x6200U && events[count-1U].value==0x19U);
    mt_amiga_hid_mouse_report(&input,300,-300,3U);reject=true;assert(mt_amiga_hid_poll(&input)!=0);
    assert(input.mouse_x==300 && input.mouse_y== -300);reject=false;
    assert(mt_amiga_hid_poll(&input)==0);assert(input.mouse_x==173 && input.mouse_y== -173);
    assert(events[count-1U].code==0x6102U && events[count-1U].value==3U);
    assert(mt_amiga_hid_release_all(&input)==0);assert(input.mouse_x==0 && input.mouse_y==0 && input.emitted_joystick[0]==0U);
    return 0;
}
