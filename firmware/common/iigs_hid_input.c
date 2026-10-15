/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_iigs_hid_input.h"
#include <string.h>
void mt_iigs_hid_init(mt_iigs_hid_input_t *in,mt_c64_hid_emit_fn emit,void *context)
{
    memset(in,0,sizeof(*in)); in->emit=emit; in->context=context;
    mt_c64_hid_input_init(&in->keyboard,emit,context);
    in->keyboard.keyboard_layout=MT_C64_KEYBOARD_LAYOUT_PS2_PC;
    in->emitted_buttons=UINT16_MAX; in->emitted_xy=0U;
    for(unsigned i=0U;i<MT_C64_HID_SLOT_COUNT;++i)in->xy[i]=0x8080U;
}
int mt_iigs_hid_keyboard_report(mt_iigs_hid_input_t *in,uint8_t slot,uint8_t modifiers,const uint8_t *keys,size_t count)
{ return mt_c64_hid_keyboard_report(&in->keyboard,slot,modifiers,keys,count); }
static uint8_t analog(int16_t value)
{ return (uint8_t)(((int32_t)value+32768)/256); }
int mt_iigs_hid_assigned_report(mt_iigs_hid_input_t *in,uint8_t slot,uint8_t player,uint8_t buttons,int16_t x,int16_t y,uint8_t directions)
{
    if(in==NULL || slot>=MT_C64_HID_SLOT_COUNT)return -1;
    in->active[slot]=player==0U ? 1U:0U;
    in->buttons[slot]=(uint16_t)((buttons&7U)<<4U);
    uint8_t ax=analog(x),ay=analog(y);
    if((directions&12U)!=0U)ax=(directions&12U)==12U ? 128U:(directions&4U)!=0U ? 0U:255U;
    if((directions&3U)!=0U)ay=(directions&3U)==3U ? 128U:(directions&1U)!=0U ? 0U:255U;
    in->xy[slot]=(uint16_t)(ax|((uint16_t)ay<<8U));
    return mt_iigs_hid_poll(in);
}
void mt_iigs_hid_mouse_report(mt_iigs_hid_input_t *in,int16_t x,int16_t y,uint8_t buttons)
{
    if(in==NULL)return;
    in->mouse_x+=x; in->mouse_y-=y; /* USB down-positive to PS/2 up-positive */
    if(in->mouse_x>32767)in->mouse_x=32767;
    if(in->mouse_x< -32767)in->mouse_x= -32767;
    if(in->mouse_y>32767)in->mouse_y=32767;
    if(in->mouse_y< -32767)in->mouse_y= -32767;
    in->mouse_buttons=buttons&7U;
}
void mt_iigs_hid_reassert(mt_iigs_hid_input_t *in)
{ mt_c64_hid_reassert(&in->keyboard); in->emitted_buttons=UINT16_MAX; in->emitted_xy=0U; in->mouse_emitted_buttons=UINT8_MAX; }
int mt_iigs_hid_disconnect(mt_iigs_hid_input_t *in,uint8_t slot)
{
    if(slot>=MT_C64_HID_SLOT_COUNT)return -1;
    in->active[slot]=0U; (void)mt_c64_hid_disconnect(&in->keyboard,slot);
    return mt_iigs_hid_poll(in);
}
int mt_iigs_hid_release_all(mt_iigs_hid_input_t *in)
{
    memset(in->active,0,sizeof(in->active)); in->mouse_buttons=0U;
    in->mouse_x=0; in->mouse_y=0; (void)mt_c64_hid_release_all(&in->keyboard);
    return mt_iigs_hid_poll(in);
}
int mt_iigs_hid_poll(mt_iigs_hid_input_t *in)
{
    if(in==NULL || in->emit==NULL)return -1;
    if(mt_c64_hid_input_poll(&in->keyboard)!=0)return -1;
    uint16_t buttons=0U,xy=0x8080U;
    for(unsigned i=0U;i<MT_C64_HID_SLOT_COUNT;++i)if(in->active[i]) { buttons|=in->buttons[i]; xy=in->xy[i]; }
    if(buttons!=in->emitted_buttons) {
        if(in->emit(in->context,0x4100U,buttons)!=0)return -1;
        in->emitted_buttons=buttons;
    }
    if(xy!=in->emitted_xy) {
        if(in->emit(in->context,0x4101U,xy)!=0)return -1;
        in->emitted_xy=xy;
    }
    if(!in->mouse_pending && (in->mouse_x!=0 || in->mouse_y!=0 || in->mouse_buttons!=in->mouse_emitted_buttons)) {
        const int32_t x=in->mouse_x>127 ? 127:in->mouse_x< -127 ? -127:in->mouse_x;
        const int32_t y=in->mouse_y>127 ? 127:in->mouse_y< -127 ? -127:in->mouse_y;
        const uint8_t flags=(uint8_t)(8U|in->mouse_buttons|(x<0 ? 16U:0U)|(y<0 ? 32U:0U));
        in->mouse_toggle^=1U;
        in->mouse_packet=((uint32_t)in->mouse_toggle<<24U)|((uint32_t)(uint8_t)y<<16U)|((uint32_t)(uint8_t)x<<8U)|flags;
        in->mouse_x-=x; in->mouse_y-=y; in->mouse_pending=true;
    }
    if(in->mouse_pending) {
        if(in->emit(in->context,0x4000U,(uint16_t)in->mouse_packet)!=0 ||
            in->emit(in->context,0x4001U,(uint16_t)(in->mouse_packet>>16U))!=0)return -1;
        in->mouse_emitted_buttons=(uint8_t)(in->mouse_packet&7U); in->mouse_pending=false;
    }
    return 0;
}
