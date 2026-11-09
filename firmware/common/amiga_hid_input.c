/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_hid_input.h"
#include <string.h>
static uint8_t amiga_key(uint8_t hid)
{
    static const uint8_t letters[]={0x20,0x35,0x33,0x22,0x12,0x23,0x24,0x25,0x17,0x26,0x27,0x28,0x37,
        0x36,0x18,0x19,0x10,0x13,0x21,0x14,0x16,0x34,0x11,0x32,0x15,0x31};
    if(hid>=4U && hid<=0x1dU)return letters[hid-4U];
    if(hid>=0x1eU && hid<=0x27U)return (uint8_t)(hid-0x1dU);
    if(hid>=0x3aU && hid<=0x43U)return (uint8_t)(hid+0x16U);
    switch(hid) {
    case 0x28:return 0x44;case 0x29:return 0x45;case 0x2a:return 0x41;
    case 0x2b:return 0x42;case 0x2c:return 0x40;case 0x2d:return 0x0b;
    case 0x2e:return 0x0c;case 0x2f:return 0x1a;case 0x30:return 0x1b;
    case 0x31:return 0x0d;case 0x32:return 0x2b;case 0x33:return 0x29;
    case 0x34:return 0x2a;case 0x35:return 0x00;case 0x36:return 0x38;
    case 0x37:return 0x39;case 0x38:return 0x3a;case 0x39:return 0x62;
    case 0x44:case 0x46:case 0x49:return 0x5f;case 0x4c:return 0x46;
    case 0x4f:return 0x4e;case 0x50:return 0x4f;case 0x51:return 0x4d;case 0x52:return 0x4c;
    case 0x54:return 0x5c;case 0x55:return 0x5d;case 0x56:return 0x4a;
    case 0x57:return 0x5e;case 0x58:return 0x43;
    case 0x59:return 0x1d;case 0x5a:return 0x1e;case 0x5b:return 0x1f;
    case 0x5c:return 0x2d;case 0x5d:return 0x2e;case 0x5e:return 0x2f;
    case 0x5f:return 0x3d;case 0x60:return 0x3e;case 0x61:return 0x3f;
    case 0x62:return 0x0f;case 0x63:return 0x3c;case 0x64:return 0x30;
    case 0x65:return 0x67; /* Application/Menu aliases Right Amiga. */
    default:return 0xff;
    }
}
void mt_amiga_hid_init(mt_amiga_hid_input_t *in,mt_c64_hid_emit_fn emit,void *context)
{
    memset(in,0,sizeof(*in));memset(in->player,0xff,sizeof(in->player));
    in->emit=emit;in->context=context;in->reset_enabled=true;in->joystick_emitted_valid=3U;
}
void mt_amiga_hid_set_cd32_buttons(mt_amiga_hid_input_t *in,bool enabled)
{ if(in!=NULL)in->cd32_buttons=enabled; }
static bool reset_chord(const uint8_t keys[16])
{ return (keys[0x63U>>3U]&(1U<<(0x63U&7U)))!=0U &&
    (keys[0x66U>>3U]&(1U<<(0x66U&7U)))!=0U &&
    (keys[0x67U>>3U]&(1U<<(0x67U&7U)))!=0U; }
void mt_amiga_hid_set_reset_enabled(mt_amiga_hid_input_t *in,bool enabled)
{
    if(in==NULL)return;
    if(!enabled) {
        uint8_t keys[16]={0};
        for(unsigned s=0U;s<MT_C64_HID_SLOT_COUNT;++s)for(unsigned b=0U;b<16U;++b)keys[b]|=in->keys[s][b];
        in->reset_pending=false;in->reset_latched=reset_chord(keys);
    }
    in->reset_enabled=enabled;
}
bool mt_amiga_hid_take_reset(mt_amiga_hid_input_t *in)
{ if(in==NULL || !in->reset_enabled || !in->reset_pending)return false;in->reset_pending=false;return true; }
static void set_key(uint8_t *keys,uint8_t key)
{ if(key<128U)keys[key>>3U]|=(uint8_t)(1U<<(key&7U)); }
int mt_amiga_hid_keyboard_report(mt_amiga_hid_input_t *in,uint8_t slot,uint8_t modifiers,const uint8_t *keys,size_t count)
{
    static const uint8_t mods[]={0x63,0x60,0x64,0x66,0x63,0x61,0x65,0x67};
    if(in==NULL || slot>=MT_C64_HID_SLOT_COUNT || (keys==NULL && count!=0U))return -1;
    for(size_t i=0U;i<count;++i)if(keys[i]>0U && keys[i]<4U)return -2;
    memset(in->keys[slot],0,16U);
    for(size_t i=0U;i<count;++i)set_key(in->keys[slot],amiga_key(keys[i]));
    for(unsigned i=0U;i<8U;++i)if(modifiers&(1U<<i))set_key(in->keys[slot],mods[i]);
    return mt_amiga_hid_poll(in);
}
int mt_amiga_hid_assigned_report(mt_amiga_hid_input_t *in,uint8_t slot,uint8_t player,uint8_t directions,uint8_t buttons)
{
    if(in==NULL || slot>=MT_C64_HID_SLOT_COUNT || (player>=2U && player!=255U))return -1;
    if((directions&3U)==3U)directions&=(uint8_t)~3U;
    if((directions&12U)==12U)directions&=(uint8_t)~12U;
    in->player[slot]=player;
    /* Controls orders red, blue, green, yellow. Minimig's native bits are
     * red, blue, yellow, green at 4 through 7. */
    in->joystick[slot]=(uint8_t)(((directions&8U)>>3U)|((directions&4U)>>1U)|
        ((directions&2U)<<1U)|((directions&1U)<<3U)|((buttons&3U)<<4U)|
        ((buttons&4U)<<5U)|((buttons&8U)<<3U));
    return mt_amiga_hid_poll(in);
}
void mt_amiga_hid_mouse_report(mt_amiga_hid_input_t *in,int16_t x,int16_t y,uint8_t buttons)
{
    if(in==NULL)return;
    in->mouse_x+=x;in->mouse_y+=y;in->mouse_buttons=buttons&7U;
    if(in->mouse_x>32767)in->mouse_x=32767;
    if(in->mouse_x< -32767)in->mouse_x= -32767;
    if(in->mouse_y>32767)in->mouse_y=32767;
    if(in->mouse_y< -32767)in->mouse_y= -32767;
}
void mt_amiga_hid_clear_mouse_motion(mt_amiga_hid_input_t *in)
{ if(in!=NULL) { in->mouse_x=0;in->mouse_y=0; } }
int mt_amiga_hid_poll(mt_amiga_hid_input_t *in)
{
    if(in==NULL || in->emit==NULL)return -1;
    uint8_t keys[16]={0},joystick[2]={0};
    for(unsigned i=0U;i<MT_C64_HID_SLOT_COUNT;++i) {
        for(unsigned b=0U;b<16U;++b)keys[b]|=in->keys[i][b];
        if(in->player[i]<2U)joystick[in->player[i]]|=in->joystick[i];
    }
    const bool chord=reset_chord(keys);
    if(!chord)in->reset_latched=false;
    else if(!in->reset_latched) {
        in->reset_latched=true;
        if(in->reset_enabled)in->reset_pending=true;
    }
    /* Releases first so a new chord cannot inherit an old modifier. */
    for(unsigned down=0U;down<2U;++down)for(unsigned k=0U;k<128U;++k) {
        const uint8_t mask=(uint8_t)(1U<<(k&7U)); const unsigned byte=k>>3U;
        const bool pressed=(keys[byte]&mask)!=0U;
        if(pressed!=(down!=0U) || !((keys[byte]^in->emitted_keys[byte])&mask))continue;
        if(in->emit(in->context,MT_AMIGA_INPUT_KEYBOARD,(uint16_t)(k|(pressed ? 0U:128U)))!=0)return -1;
        in->emitted_keys[byte]^=mask;
    }
    for(unsigned p=0U;p<2U;++p) {
        const uint8_t mask=(uint8_t)(1U<<p);
        const uint8_t value=joystick[p]&(in->cd32_buttons ? 0xffU:0x3fU);
        if((in->joystick_emitted_valid&mask)!=0U && in->emitted_joystick[p]==value)continue;
        if(in->emit(in->context,(uint16_t)(MT_AMIGA_INPUT_JOYSTICK|p),value)!=0)return -1;
        in->emitted_joystick[p]=value;in->joystick_emitted_valid|=mask;
    }
    int32_t *motion[2]={&in->mouse_x,&in->mouse_y};
    for(unsigned a=0U;a<2U;++a)if(*motion[a]!=0) {
        const int32_t delta=*motion[a]>127 ? 127:*motion[a]< -127 ? -127:*motion[a];
        if(in->emit(in->context,(uint16_t)(MT_AMIGA_INPUT_MOUSE_X+a),(uint8_t)delta)!=0)return -1;
        *motion[a]-=delta;
    }
    if(in->mouse_buttons!=in->emitted_mouse_buttons) {
        if(in->emit(in->context,MT_AMIGA_INPUT_MOUSE_BUTTONS,in->mouse_buttons)!=0)return -1;
        in->emitted_mouse_buttons=in->mouse_buttons;
    }
    return 0;
}
int mt_amiga_hid_disconnect(mt_amiga_hid_input_t *in,uint8_t slot)
{ if(in==NULL || slot>=MT_C64_HID_SLOT_COUNT)return -1;in->reset_pending=false;memset(in->keys[slot],0,16U);in->joystick[slot]=0U;return mt_amiga_hid_poll(in); }
int mt_amiga_hid_release_all(mt_amiga_hid_input_t *in)
{ if(in==NULL)return -1;in->reset_pending=false;in->reset_latched=false;memset(in->keys,0,sizeof(in->keys));memset(in->joystick,0,sizeof(in->joystick));
    in->mouse_buttons=0U;mt_amiga_hid_clear_mouse_motion(in);return mt_amiga_hid_poll(in); }
void mt_amiga_hid_reassert(mt_amiga_hid_input_t *in)
{ if(in!=NULL) { memset(in->emitted_keys,0,sizeof(in->emitted_keys));in->joystick_emitted_valid=0U;in->emitted_mouse_buttons=255U; } }
