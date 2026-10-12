/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_display.h"
#include "multitini_regs.h"
#include <assert.h>
#include <string.h>
static uint32_t version,mask,now,status,requested;
static unsigned writes,polls,finish_at;
static bool fail,held,wrong_scale,wrong_mode;
static uint32_t read_reg(uint32_t offset) {
    if(offset==MT_REG_ABI_VERSION)return version;
    assert(version>=16);
    if(offset==MT_REG_DISPLAY_MODES)return mask;
    assert(offset==MT_REG_DISPLAY_STATUS);
    if(writes && ++polls==finish_at)status=fail ? MT_DISPLAY_ERROR :
        requested ^ (wrong_scale ? 16U:0U) ^ (wrong_mode ? 1U:0U);
    return status|(held ? MT_DISPLAY_HELD : 0U);
}
static void write_reg(uint32_t offset,uint32_t value) {
    assert(offset==MT_REG_DISPLAY_REQUEST && (value&15U)<6U && (value>>4U)<=4U);
    if(version<MT_DISPLAY_SCALE_MIN_ABI_MINOR)assert((value&MT_DISPLAY_SCALE_MASK)==0U);
    ++writes;requested=value;status=MT_DISPLAY_BUSY;
}
static uint32_t millis(void){return now++;}
static const mt_display_ops_t ops={read_reg,write_reg,millis};
static void clear(void){version=16;mask=63;now=status=requested=0;writes=polls=0;finish_at=10;fail=held=wrong_scale=wrong_mode=false;}
int main(void) {
    for(uint8_t mode=0;mode<6;++mode){clear();assert(mt_display_apply(&ops,mode)==MT_DISPLAY_OK);assert(writes==1 && status==mode);}
    clear();version=15;assert(!mt_display_supported(&ops));assert(mt_display_apply(&ops,1)==MT_DISPLAY_UNSUPPORTED && writes==0);
    clear();version=0x10010;assert(!mt_display_supported(&ops));
    clear();mask=0;assert(!mt_display_supported(&ops));
    clear();assert(mt_display_apply(&ops,6)==MT_DISPLAY_INVALID && writes==0);
    clear();status=MT_DISPLAY_BUSY;assert(mt_display_apply(&ops,1)==MT_DISPLAY_IN_PROGRESS && writes==0);
    clear();fail=true;assert(mt_display_apply(&ops,5)==MT_DISPLAY_FAILED);
    clear();held=true;assert(mt_display_apply(&ops,5)==MT_DISPLAY_FAILED);
    clear();finish_at=10000;assert(mt_display_apply(&ops,1)==MT_DISPLAY_TIMEOUT);
    clear();now=UINT32_MAX-3;assert(mt_display_apply(&ops,1)==MT_DISPLAY_OK);
    for(uint32_t abi=16U;abi<21U;++abi) {
        clear();version=abi;assert(!mt_display_scale_supported(&ops));
        for(uint8_t scale=0U;scale<=4U;++scale)
            assert(mt_display_apply_scale(&ops,scale)==MT_DISPLAY_UNSUPPORTED && writes==0U);
        /* Reserved bits on an old image must never be re-emitted as scale. */
        status=64U;assert(mt_display_apply(&ops,2U)==MT_DISPLAY_OK && requested==2U);
    }
    for(uint8_t mode=0U;mode<6U;++mode)for(uint8_t scale=0U;scale<=4U;++scale) {
        clear();version=21U;status=((uint32_t)scale<<4U)|mode;
        assert(mt_display_apply(&ops,(uint8_t)((mode+1U)%6U))==MT_DISPLAY_OK);
        assert(requested==(((uint32_t)scale<<4U)|((mode+1U)%6U)));
        clear();version=21U;status=mode;
        assert(mt_display_apply_scale(&ops,scale)==MT_DISPLAY_OK);
        assert(requested==(((uint32_t)scale<<4U)|mode));
    }
    clear();version=21U;assert(mt_display_apply_scale(&ops,5U)==MT_DISPLAY_INVALID && writes==0U);
    clear();version=21U;status=MT_DISPLAY_BUSY;assert(mt_display_apply_scale(&ops,2U)==MT_DISPLAY_IN_PROGRESS && writes==0U);
    clear();version=21U;status=7U;assert(mt_display_apply_scale(&ops,2U)==MT_DISPLAY_FAILED && writes==0U);
    clear();version=21U;status=80U;assert(mt_display_apply(&ops,2U)==MT_DISPLAY_FAILED && writes==0U);
    clear();version=21U;wrong_scale=true;assert(mt_display_apply_scale(&ops,2U)==MT_DISPLAY_FAILED);
    clear();version=21U;wrong_mode=true;assert(mt_display_apply_scale(&ops,2U)==MT_DISPLAY_FAILED);
    clear();version=21U;finish_at=10000U;assert(mt_display_apply_scale(&ops,2U)==MT_DISPLAY_TIMEOUT);
    clear();version=21U;held=true;assert(mt_display_apply_scale(&ops,2U)==MT_DISPLAY_FAILED);
    assert(!strcmp(mt_display_mode_name(0),"1920 x 1080"));
    assert(!strcmp(mt_display_mode_name(6),"Unknown"));
    assert(!strcmp(mt_display_scale_name(0),"Auto") && !strcmp(mt_display_scale_name(4),"4x"));
    assert(!strcmp(mt_display_scale_name(5),"Unknown"));
    return 0;
}
