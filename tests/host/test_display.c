/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_display.h"
#include "multitini_regs.h"
#include <assert.h>
#include <string.h>
static uint32_t version,mask,now,status,requested;
static unsigned writes,polls,finish_at;
static bool fail,held;
static uint32_t read_reg(uint32_t offset) {
    if(offset==MT_REG_ABI_VERSION)return version;
    assert(version>=16);
    if(offset==MT_REG_DISPLAY_MODES)return mask;
    assert(offset==MT_REG_DISPLAY_STATUS);
    if(writes && ++polls==finish_at)status=fail ? MT_DISPLAY_ERROR : requested;
    return status|(held ? MT_DISPLAY_HELD : 0U);
}
static void write_reg(uint32_t offset,uint32_t value) {
    assert(offset==MT_REG_DISPLAY_REQUEST && value<6);++writes;requested=value;status=MT_DISPLAY_BUSY;
}
static uint32_t millis(void){return now++;}
static const mt_display_ops_t ops={read_reg,write_reg,millis};
static void clear(void){version=16;mask=63;now=status=requested=0;writes=polls=0;finish_at=10;fail=held=false;}
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
    assert(!strcmp(mt_display_mode_name(0),"1920 x 1080"));
    assert(!strcmp(mt_display_mode_name(6),"Unknown"));
    return 0;
}
