/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_nes_content.h"
#include "multitini_nes_boot.h"
#include "multitini_nes_hid_input.h"
#include "multitini_controls.h"
#include "multitini_core_frontend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t cartridge[MT_SIZE_NES_CART_MAX];
static uint8_t wire_cartridge[MT_SIZE_NES_CART_MAX];
/* Header bytes supplied by the owner. Payload below is generated test data. */
static const uint8_t reported_headers[5][16] = {
    {0x4e,0x45,0x53,0x1a,0x08,0x00,0x21,0x08,0x20,0,0,7,0,0,0,1},
    {0x4e,0x45,0x53,0x1a,0x08,0x00,0x12,0x08,0,0,0x70,7,0,0,0,1},
    {0x4e,0x45,0x53,0x1a,0x02,0x01,0x01,0x08,0,0,0,0,2,0,0,1},
    {0x4e,0x45,0x53,0x1a,0x08,0x10,0x40,0x08,0,0,7,0,0,0,0,1},
    {0x4e,0x45,0x53,0x1a,0x10,0x10,0x40,0x08,0,0,7,0,0,0,0,1}
};
static size_t make_reported(size_t index)
{
    assert(index < 5U);
    memcpy(cartridge, reported_headers[index], 16U);
    const size_t size = 16U + (size_t)cartridge[4] * 16384U + (size_t)cartridge[5] * 8192U;
    for (size_t i = 16U; i < size; ++i) cartridge[i] = (uint8_t)(i ^ (i >> 8U));
    return size;
}
static size_t make_cart(uint16_t mapper, uint8_t prg, uint8_t chr)
{
    const size_t size = 16U + (size_t)prg * 16384U + (size_t)chr * 8192U;
    memset(cartridge, 0, sizeof(cartridge));
    memcpy(cartridge, "NES\032", 4U);
    cartridge[4] = prg; cartridge[5] = chr;
    cartridge[6] = (uint8_t)((mapper & 15U) << 4U) | 2U;
    cartridge[7] = (uint8_t)(mapper & 0xf0U);
    return size;
}
static void test_content(void)
{
    mt_nes_content_info_t info;
    static const uint16_t mappers[] = {0,1,2,3,4,7,9,10,11,66,71};
    for (size_t i = 0U; i < sizeof(mappers) / sizeof(mappers[0]); ++i) {
        const uint8_t prg = mappers[i] == 9U ? 8U : 2U;
        const uint8_t chr = mappers[i] == 2U || mappers[i] == 7U || mappers[i] == 71U ? 0U : 1U;
        const size_t size = make_cart(mappers[i], prg, chr);
        assert(mt_nes_content_inspect(cartridge, size, &info) == MT_CONTENT_VALID);
        assert(info.mapper == mappers[i] && info.battery);
    }
    size_t size = make_cart(4,32,32);
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_VALID);
    size = make_cart(2,4,0);
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_VALID && info.chr_size==0U);
    assert(mt_nes_content_inspect(cartridge,size-1U,&info)==MT_CONTENT_WRONG_SIZE);
    assert(mt_nes_content_inspect(cartridge,size+1U,&info)==MT_CONTENT_WRONG_SIZE);
    assert(mt_nes_content_inspect(NULL,size,&info)==MT_CONTENT_INVALID_ARGUMENT);
    assert(mt_nes_content_inspect(cartridge,15U,&info)==MT_CONTENT_TOO_SMALL);
    cartridge[0]=0;
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_BAD_MAGIC);
    size=make_cart(5,2,1);
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_UNSUPPORTED_VERSION);
    size=make_cart(0,2,1);
    const unsigned offsets[]={6,6,7,7,7,9,10,11,15};
    const unsigned flags[]={4,8,8,1,2,1,1,1,1};
    for (unsigned i=0U;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        const uint8_t old=cartridge[offsets[i]];
        cartridge[offsets[i]] |= (uint8_t)flags[i];
        assert(mt_nes_content_inspect(cartridge,size,&info)!=MT_CONTENT_VALID);
        cartridge[offsets[i]]=old;
    }
    cartridge[8]=2U;
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_WRONG_SIZE);
    size=make_cart(4,3,1);
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_WRONG_SIZE);
    size=make_cart(1,32,0);
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_WRONG_SIZE);
    assert(mt_content_classify("USER.NES")==MT_CONTENT_NES_CART);
    assert(mt_core_frontend_accepts_content(MT_NES_CORE_ID,MT_MENU_BROWSER_CARTRIDGE,MT_CONTENT_NES_CART));
    assert(!mt_core_frontend_accepts_content(MT_NES_CORE_ID,MT_MENU_BROWSER_CARTRIDGE,MT_CONTENT_GB_CART));
    mt_content_info_t generic;
    size=make_cart(1,2,0);
    assert(mt_content_inspect(MT_CONTENT_NES_CART,cartridge,size,&generic)==MT_CONTENT_VALID);
    assert(generic.payload_offset==16U && generic.hardware_type==1U && generic.writable);
}
static void test_nes20_content(void)
{
    mt_nes_content_info_t info;
    for (size_t i=0U;i<5U;++i) {
        const size_t size=make_reported(i);
        assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_VALID);
        assert(info.rejection_reason==NULL && info.canonical_header[7]==0U);
        for(size_t b=8U;b<16U;++b)
            assert(info.canonical_header[b]==(i==0U && b==10U ? 0x20U:0U));
        mt_nes_content_info_t canonical;
        uint8_t original[16];memcpy(original,cartridge,16U);
        memcpy(cartridge,info.canonical_header,16U);
        assert(mt_nes_content_inspect(cartridge,size,&canonical)==MT_CONTENT_VALID);
        memcpy(cartridge,original,16U);
        assert(memcmp(cartridge,reported_headers[i],16U)==0);
        assert(info.battery==(i==1U));
    }
    size_t size=make_reported(0U);
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_VALID);
    assert(info.canonical_header[10]==0x20U);
    cartridge[8]=0x10U;
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_VALID && info.canonical_header[10]==0U);
    cartridge[8]=0x30U;
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_UNSUPPORTED_VERSION);
    assert(strcmp(info.rejection_reason,"NES cartridge board variant is unsupported")==0);
    static const struct {unsigned offset;uint8_t value;} unsupported[]={
        {6U,0x05U},{6U,0x09U},{7U,0x09U},{8U,0x10U},{8U,0x01U},
        {9U,0x01U},{9U,0x0fU},{10U,0x08U},{10U,0x77U},{11U,0x07U},
        {11U,0x70U},{12U,0x01U},{12U,0x03U},{12U,0x80U},
        {13U,1U},{14U,1U},{15U,2U}
    };
    for(size_t i=0U;i<sizeof(unsupported)/sizeof(unsupported[0]);++i) {
        size=make_reported(2U);cartridge[unsupported[i].offset]=unsupported[i].value;
        assert(mt_nes_content_inspect(cartridge,size,&info)!=MT_CONTENT_VALID);
        assert(info.rejection_reason!=NULL);
    }
    size=make_reported(1U);cartridge[10]=7U;
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_UNSUPPORTED_VERSION);
    size=make_reported(1U);cartridge[11]=0U;
    assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_UNSUPPORTED_VERSION);
    size=make_reported(2U);
    assert(mt_nes_content_inspect(cartridge,size-1U,&info)==MT_CONTENT_WRONG_SIZE);
    assert(mt_nes_content_inspect(cartridge,size+1U,&info)==MT_CONTENT_WRONG_SIZE);
}
typedef struct {
    uint32_t regs[MT_REGISTER_SPAN/4U], sequence;
    size_t size, offset;
    uint8_t kind, reject_kind, order[2];
    unsigned completed, resets;
    bool reset;
} shell_t;
static uint32_t read32(void *context,uint32_t offset)
{
    shell_t *s=context;
    if(offset==MT_REG_STREAM_STATUS)return s->offset==s->size ? MT_STREAM_STATUS_DRAINED:MT_STREAM_STATUS_READY;
    return s->regs[offset/4U];
}
static void write32(void *context,uint32_t offset,uint32_t value)
{
    shell_t *s=context;s->regs[offset/4U]=value;
    if(offset==MT_REG_STREAM_DATA_BURST) {
        for(unsigned i=0U;i<4U && s->offset<s->size;++i) {
            if(s->kind==MT_LOAD_NES_CART)wire_cartridge[s->offset]=(uint8_t)(value>>(8U*i));
            ++s->offset;
        }
    }
}
static void progress(void *context)
{
    shell_t *s=context;
    uint32_t sequence=s->regs[MT_REG_COMMAND_SEQ/4U];
    if(sequence==s->sequence)return;
    s->sequence=sequence;
    uint32_t op=s->regs[MT_REG_COMMAND_OPCODE/4U],arg=s->regs[MT_REG_COMMAND_ARG0/4U];
    uint32_t status=MT_RESPONSE_OK;
    switch(op) {
    case MT_COMMAND_CORE_RESET:s->reset=arg!=0U;++s->resets;break;
    case MT_COMMAND_STREAM_BEGIN:
        assert(s->reset);s->kind=(uint8_t)arg;s->offset=0U;s->size=s->regs[MT_REG_COMMAND_ARG1/4U];
        if(s->kind==s->reject_kind)status=MT_RESPONSE_IO_ERROR;
        break;
    case MT_COMMAND_STREAM_DATA:
        assert(s->offset<s->size);
        if(s->kind==MT_LOAD_NES_CART)wire_cartridge[s->offset]=(uint8_t)arg;
        ++s->offset;break;
    case MT_COMMAND_STREAM_END:assert(s->offset==s->size && s->completed<2U);s->order[s->completed++]=s->kind;break;
    case MT_COMMAND_STREAM_ABORT:break;
    default:assert(0);
    }
    s->regs[MT_REG_RESPONSE_STATUS/4U]=status;s->regs[MT_REG_RESPONSE_SEQ/4U]=sequence;
}
static void test_boot(void)
{
    static uint8_t save[MT_SIZE_NES_SAVE];
    mt_nes_boot_images_t images={{cartridge,make_cart(1,2,0)},{save,sizeof(save)}};
    shell_t shell={0};mt_core_link_t link;
    mt_core_link_init(&link,&shell,read32,write32);
    assert(mt_nes_stage(&link,&images,2U,progress,&shell)==MT_LINK_OK);
    assert(shell.reset && shell.resets==1U && shell.completed==2U);
    assert(shell.order[0]==MT_LOAD_NES_CART && shell.order[1]==MT_LOAD_NES_SAVE);
    assert(mt_nes_release(&link,2U,progress,&shell)==MT_LINK_OK && !shell.reset);
    images.save.size--;
    assert(mt_nes_boot(&link,&images,2U,progress,&shell)==MT_LINK_BAD_ARGUMENT && shell.resets==2U);
    images.save.size++;
    cartridge[6] &= (uint8_t)~2U;
    assert(mt_nes_boot(&link,&images,2U,progress,&shell)==MT_LINK_BAD_ARGUMENT);
    images.save=(mt_nes_image_t){NULL,0U};shell.completed=0U;
    assert(mt_nes_boot(&link,&images,2U,progress,&shell)==MT_LINK_OK && shell.completed==1U && !shell.reset);
    shell.completed=0U;shell.reject_kind=MT_LOAD_NES_CART;
    mt_nes_transfer_phase_t phase;
    assert(mt_nes_stage_traced(&link,&images,2U,progress,&shell,&phase)==MT_LINK_REMOTE_ERROR);
    assert(shell.reset && shell.completed==0U && phase==MT_NES_TRANSFER_CARTRIDGE);
}
static void test_nes20_boot(void)
{
    for(unsigned burst=0U;burst<2U;++burst) {
        for(size_t i=0U;i<5U;++i) {
            shell_t shell={0};mt_core_link_t link;
            if(burst)shell.regs[MT_REG_ABI_VERSION/4U]=MT_ABI_VERSION;
            mt_core_link_init(&link,&shell,read32,write32);
            const size_t size=make_reported(i);
            mt_nes_boot_images_t images={{cartridge,size},{NULL,0U}};
            mt_nes_content_info_t info;
            assert(mt_nes_content_inspect(cartridge,size,&info)==MT_CONTENT_VALID);
            assert(mt_nes_boot(&link,&images,2U,progress,&shell)==MT_LINK_OK);
            assert(!shell.reset && shell.completed==1U);
            assert(memcmp(wire_cartridge,info.canonical_header,16U)==0);
            assert(memcmp(wire_cartridge+16U,cartridge+16U,size-16U)==0);
            assert(memcmp(cartridge,reported_headers[i],16U)==0);
            for(size_t b=16U;b<size;++b)assert(cartridge[b]==(uint8_t)(b^(b>>8U)));
        }
    }
}
typedef struct {uint16_t state[2];unsigned writes;bool fail;} pads_t;
static int emit(void *context,uint16_t code,uint16_t value)
{
    pads_t *p=context;assert((code>>8U)==MT_INPUT_NES_GAMEPAD && (code&255U)<2U);
    if(p->fail)return -1;
    p->state[code&255U]=value;++p->writes;return 0;
}
static void test_input(void)
{
    pads_t pads={0};mt_nes_hid_input_t input;
    mt_nes_hid_input_init(&input,emit,&pads);
    mt_gamepad_state_t a={0};a.connected=1U;a.hat=2U;a.buttons=1U;
    assert(mt_nes_hid_assigned_report(&input,0,1,&a)==0);
    assert(pads.state[0]==0U && pads.state[1]==((1U<<MT_NES_BUTTON_RIGHT)|(1U<<MT_NES_BUTTON_A)));
    const uint8_t keys[]={0x1bU,0x28U};
    assert(mt_nes_hid_keyboard_report(&input,2,0x20U,keys,sizeof(keys))==0);
    assert(pads.state[0]==((1U<<MT_NES_BUTTON_B)|(1U<<MT_NES_BUTTON_START)|(1U<<MT_NES_BUTTON_SELECT)));
    const uint8_t rollover[]={0x1dU,1U};
    unsigned writes=pads.writes;
    assert(mt_nes_hid_keyboard_report(&input,2,0,rollover,sizeof(rollover))==-2 && pads.writes==writes);
    assert(mt_nes_hid_release_all(&input)==0 && pads.state[0]==0U && pads.state[1]==0U);
    assert(mt_nes_hid_assigned_report(&input,0,1,&a)==0 && pads.state[1]!=0U);
    assert(mt_nes_hid_disconnect(&input,0)==0 && pads.state[1]==0U);
    pads.fail=true;
    assert(mt_nes_hid_assigned_report(&input,0,0,&a)==-1);
    pads.fail=false;
    assert(mt_nes_hid_input_poll(&input)==0 && pads.state[0]!=0U);
    mt_nes_hid_reassert(&input);writes=pads.writes;
    assert(mt_nes_hid_input_poll(&input)==0 && pads.writes==writes+2U);
    mt_controller_config_t config;mt_controls_default(MT_NES_CORE_ID,NULL,&config);
    config.assignment=MT_CONTROLLER_PORT2;
    assert(mt_controls_config_valid(MT_NES_CORE_ID,&config));
    assert((mt_controls_action_mask(MT_NES_CORE_ID)&(1U<<MT_CONTROL_SELECT))!=0U);
    assert((mt_controls_action_mask(MT_NES_CORE_ID)&(1U<<MT_CONTROL_C))==0U);
}
int main(void)
{
    test_content();test_nes20_content();test_boot();test_nes20_boot();test_input();
    puts("PASS: NES iNES/NES2 header bounds, immutable canonical command/burst boot and two-player controls");
    return 0;
}
