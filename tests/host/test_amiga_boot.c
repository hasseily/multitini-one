/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_amiga_boot.h"
#include <assert.h>
#include <string.h>
typedef struct { uint32_t status;unsigned selected,reg,transfers;uint16_t configs[16];bool hold,error,no_toggle; } native_t;
static uint32_t rd(void *ctx,uint32_t offset)
{ native_t *n=ctx;assert(offset==MT_AMIGA_HOST_PORT);return n->hold ? MT_AMIGA_HOST_BUSY:n->status; }
static void wr(void *ctx,uint32_t offset,uint32_t value)
{
    native_t *n=ctx;assert(offset==MT_AMIGA_HOST_PORT);
    const unsigned op=value>>24U,payload=value&0xffffffU;
    if(op==MT_AMIGA_HOST_SELECT) { n->selected=payload;n->reg=255U; }
    else if(op==MT_AMIGA_HOST_TRANSFER) {
        assert(n->selected==2U);
        if(n->reg==255U)n->reg=payload&15U;else n->configs[n->reg]=(uint16_t)payload;
        ++n->transfers;
    }
    n->status=(n->status^(n->no_toggle ? 0U:MT_AMIGA_HOST_TOGGLE))|0x1234U;
    if(n->error)n->status|=MT_AMIGA_HOST_ERROR;
}
int main(void)
{
    native_t native={0};mt_core_link_t link;mt_core_link_init(&link,&native,rd,wr);
    mt_amiga_config_t config;mt_amiga_config_defaults(&config);
    assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_OK);
    assert(native.selected==0U && native.transfers==20U);
    assert(native.configs[3]==9U && native.configs[4]==0x1fU && native.configs[5]==0x33U && native.configs[7]==4U && native.configs[8]==0x27U);
    native.status|=MT_AMIGA_HOST_FAST_IDE;
    assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_OK && native.configs[8]==7U);
    config.ide_compat=MT_AMIGA_IDE_CLASSIC;
    assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_OK && native.configs[8]==0x27U);
    config.ide_compat=MT_AMIGA_IDE_AUTO;
    config.cpu=MT_AMIGA_CPU_STOCK;assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_OK);
    assert(native.configs[3]==8U && native.configs[4]==0U && native.configs[8]==0x27U);
    config.cpu=MT_AMIGA_CPU_68020_14;
    const unsigned transfers=native.transfers;
    assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_ABI_MISMATCH && native.transfers==transfers);
    native.status|=MT_AMIGA_HOST_CPU14;
    assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_OK && native.configs[4]==0x5fU && native.configs[8]==7U);
    config.cpu=MT_AMIGA_CPU_68020_TURBO;
    assert(mt_amiga_native_configure(&link,&config,false,5U)==MT_LINK_OK && native.configs[4]==0x1fU);
    assert(mt_amiga_native_configure(&link,&config,true,5U)==MT_LINK_OK);
    assert(native.configs[3]==0x19U && native.configs[4]==0x1fU && native.configs[8]==7U);
    config.cpu=MT_AMIGA_CPU_STOCK;
    assert(mt_amiga_native_configure(&link,&config,true,5U)==MT_LINK_OK);
    assert(native.configs[3]==0x18U && native.configs[4]==0x23U && native.configs[8]==0x27U);
    config.cpu=MT_AMIGA_CPU_68020_14;
    assert(mt_amiga_native_configure(&link,&config,true,5U)==MT_LINK_BAD_ARGUMENT);
    native.hold=true;assert(mt_amiga_host_access(&link,0U,0U,2U,NULL)==MT_LINK_TIMEOUT);native.hold=false;
    native.error=true;assert(mt_amiga_host_access(&link,0U,0U,2U,NULL)==MT_LINK_REMOTE_ERROR);native.error=false;native.status=0U;
    native.no_toggle=true;assert(mt_amiga_host_access(&link,0U,0U,2U,NULL)==MT_LINK_TIMEOUT);
    assert(mt_amiga_host_access(&link,8U,0U,2U,NULL)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_RTC_LOW,0U,2U,NULL)==MT_LINK_ABI_MISMATCH);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_RTC_COMMIT,0x10000U,2U,NULL)==MT_LINK_BAD_ARGUMENT);
    native.no_toggle=false;native.status=MT_AMIGA_HOST_DISPLAY;
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DISPLAY_REQUEST,7U,2U,NULL)==MT_LINK_ABI_MISMATCH);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DISPLAY_APPLY,3U,2U,NULL)==MT_LINK_ABI_MISMATCH);
    native.status|=MT_AMIGA_HOST_SHRES;
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DISPLAY_REQUEST,7U,2U,NULL)==MT_LINK_OK);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DISPLAY_APPLY,3U,2U,NULL)==MT_LINK_OK);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_DISPLAY_APPLY,4U,2U,NULL)==MT_LINK_BAD_ARGUMENT);
    native.status=0U;
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_READ_CONFIG,0U,2U,NULL)==MT_LINK_ABI_MISMATCH);
    native.status=MT_AMIGA_HOST_CPU14;
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_READ_CONFIG,0U,2U,NULL)==MT_LINK_OK);
    native.status=MT_AMIGA_HOST_FAST_IDE; /* Capability is independent of CPU14. */
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_READ_CONFIG,0U,2U,NULL)==MT_LINK_OK);
    assert(mt_amiga_host_access(&link,MT_AMIGA_HOST_READ_CONFIG,1U,2U,NULL)==MT_LINK_BAD_ARGUMENT);
    assert(mt_amiga_host_access(&link,12U,0U,2U,NULL)==MT_LINK_BAD_ARGUMENT);
    return 0;
}
