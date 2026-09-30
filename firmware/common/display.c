/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_display.h"
#include "multitini_regs.h"
#include <stddef.h>
const char *mt_display_mode_name(uint8_t mode)
{
    static const char *const names[MT_DISPLAY_MODE_COUNT]={
        "1920 x 1080", "1024 x 768", "1200 x 800", "1280 x 1024",
        "1360 x 768", "1680 x 1050"};
    return mode<MT_DISPLAY_MODE_COUNT ? names[mode] : "Unknown";
}
bool mt_display_supported(const mt_display_ops_t *ops)
{
    if(ops==NULL || ops->read==NULL || ops->write==NULL || ops->milliseconds==NULL) return false;
    const uint32_t version=ops->read(MT_REG_ABI_VERSION);
    return (version>>16)==MT_ABI_MAJOR && (version & UINT32_C(0xffff))>=MT_DISPLAY_MIN_ABI_MINOR &&
           ops->read(MT_REG_DISPLAY_MODES)==UINT32_C(0x3f);
}
mt_display_result_t mt_display_apply(const mt_display_ops_t *ops,uint8_t mode)
{
    if(mode>=MT_DISPLAY_MODE_COUNT) return MT_DISPLAY_INVALID;
    if(!mt_display_supported(ops)) return MT_DISPLAY_UNSUPPORTED;
    if((ops->read(MT_REG_DISPLAY_STATUS)&MT_DISPLAY_BUSY)!=0U) return MT_DISPLAY_IN_PROGRESS;
    const uint32_t start=ops->milliseconds();
    ops->write(MT_REG_DISPLAY_REQUEST,mode);
    for(uint32_t polls=0;polls<UINT32_C(10000000);++polls) {
        const uint32_t status=ops->read(MT_REG_DISPLAY_STATUS);
        if((status&MT_DISPLAY_BUSY)==0U) {
            if((status&(MT_DISPLAY_ERROR|MT_DISPLAY_HELD))!=0U || (status&15U)!=mode) return MT_DISPLAY_FAILED;
            return MT_DISPLAY_OK;
        }
        if((uint32_t)(ops->milliseconds()-start)>=1500U) return MT_DISPLAY_TIMEOUT;
    }
    return MT_DISPLAY_TIMEOUT;
}
