/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_hid_input.h"
#include <assert.h>
#include <string.h>
static struct { uint16_t code,value; } events[512];static unsigned count;static bool reject;
static int emit(void *ctx,uint16_t code,uint16_t value)
{ (void)ctx;if(reject)return -1;assert(count<512U);events[count].code=code;events[count++].value=value;return 0; }
static void test_cd32_buttons(void)
{
    mt_amiga_hid_input_t input;
    mt_amiga_hid_init(&input,emit,NULL);
    assert(mt_amiga_hid_assigned_report(&input,0U,0U,0U,15U)==0);
    assert(events[count-1U].code==0x6200U && events[count-1U].value==0x30U);
    mt_amiga_hid_set_cd32_buttons(&input,true);
    assert(mt_amiga_hid_poll(&input)==0 && events[count-1U].value==0xf0U);
    assert(mt_amiga_hid_assigned_report(&input,0U,0U,0U,4U)==0 && events[count-1U].value==0x80U);
    assert(mt_amiga_hid_assigned_report(&input,0U,0U,0U,8U)==0 && events[count-1U].value==0x40U);
    assert(mt_amiga_hid_assigned_report(&input,0U,0U,5U,15U)==0);
    assert(mt_amiga_hid_assigned_report(&input,1U,0U,10U,15U)==0 && events[count-1U].value==0xffU);
    const unsigned replay=count;
    mt_amiga_hid_reassert(&input);
    reject=true;assert(mt_amiga_hid_poll(&input)==-1 && count==replay);
    reject=false;assert(mt_amiga_hid_poll(&input)==0);
    assert(events[replay].code==0x6200U && events[replay].value==0xffU);
    assert(events[replay+1U].code==0x6201U && events[replay+1U].value==0U);
    assert(mt_amiga_hid_disconnect(&input,1U)==0 && events[count-1U].value==0xfaU);
    mt_amiga_hid_set_cd32_buttons(&input,false);
    assert(mt_amiga_hid_poll(&input)==0 && events[count-1U].value==0x3aU);
    assert(mt_amiga_hid_release_all(&input)==0 && events[count-1U].value==0U);
}
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
    const uint8_t menu[]={0x65U};
    assert(mt_amiga_hid_keyboard_report(&input,0U,0U,menu,1U)==0 && events[count-1U].value==0x67U);
    assert(mt_amiga_hid_keyboard_report(&input,1U,0x80U,NULL,0U)==0);
    const unsigned held=count;assert(mt_amiga_hid_disconnect(&input,0U)==0 && count==held);
    assert(mt_amiga_hid_disconnect(&input,1U)==0 && events[count-1U].value==0xe7U);
    /* Aggregate modifiers across keyboards, including the Menu alias. */
    assert(mt_amiga_hid_keyboard_report(&input,0U,0x01U,NULL,0U)==0);
    assert(mt_amiga_hid_keyboard_report(&input,1U,0x08U,NULL,0U)==0);
    assert(!mt_amiga_hid_take_reset(&input));
    assert(mt_amiga_hid_keyboard_report(&input,2U,0U,menu,1U)==0 && mt_amiga_hid_take_reset(&input));
    assert(mt_amiga_hid_poll(&input)==0 && !mt_amiga_hid_take_reset(&input));
    mt_amiga_hid_reassert(&input);assert(mt_amiga_hid_poll(&input)==0 && !mt_amiga_hid_take_reset(&input));
    assert(mt_amiga_hid_keyboard_report(&input,2U,0U,NULL,0U)==0);
    assert(mt_amiga_hid_keyboard_report(&input,2U,0x80U,NULL,0U)==0 && mt_amiga_hid_take_reset(&input));
    assert(mt_amiga_hid_release_all(&input)==0);
    assert(mt_amiga_hid_keyboard_report(&input,0U,0x89U,NULL,0U)==0);
    assert(mt_amiga_hid_disconnect(&input,1U)==0 && !mt_amiga_hid_take_reset(&input));
    assert(mt_amiga_hid_release_all(&input)==0);
    mt_amiga_hid_set_reset_enabled(&input,false);
    assert(mt_amiga_hid_keyboard_report(&input,0U,0x98U,NULL,0U)==0 && !mt_amiga_hid_take_reset(&input));
    mt_amiga_hid_set_reset_enabled(&input,true);
    assert(mt_amiga_hid_poll(&input)==0 && !mt_amiga_hid_take_reset(&input));
    assert(mt_amiga_hid_release_all(&input)==0);
    assert(mt_amiga_hid_keyboard_report(&input,0U,0x98U,NULL,0U)==0 && mt_amiga_hid_take_reset(&input));
    test_cd32_buttons();
    return 0;
}
