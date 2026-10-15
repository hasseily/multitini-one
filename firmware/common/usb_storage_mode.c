/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_usb_storage_mode.h"
#include <string.h>
void mt_usb_sd_mode_init(mt_usb_sd_mode_t *m,const mt_usb_sd_ops_t *ops)
{ memset(m,0,sizeof(*m)); m->ops=*ops; }
int mt_usb_sd_mode_enter(mt_usb_sd_mode_t *m)
{
    if(m->state!=MT_USB_SD_IDLE)return -1;
    m->stop=false;
    if(m->ops.quiesce(m->ops.context)!=0) { m->ops.cancel_quiesce(m->ops.context);return -1; }
    if(m->ops.acquire(m->ops.context)!=0) { m->ops.cancel_quiesce(m->ops.context);return -1; }
    m->state=MT_USB_SD_ACTIVE;
    if(m->ops.connect(m->ops.context)!=0) {
        m->stop=true;m->state=MT_USB_SD_STOPPING;return -1;
    }
    return 0;
}
void mt_usb_sd_mode_stop(mt_usb_sd_mode_t *m)
{
    if(m->state==MT_USB_SD_IDLE)return;
    m->stop=true;
    if(m->state==MT_USB_SD_STOP_FAILED)m->state=MT_USB_SD_STOPPING;
}
void mt_usb_sd_mode_poll(mt_usb_sd_mode_t *m)
{
    if(m->state==MT_USB_SD_IDLE)return;
    if(m->state==MT_USB_SD_ACTIVE || m->state==MT_USB_SD_STOPPING || m->state==MT_USB_SD_STOP_FAILED) {
        m->ops.poll(m->ops.context);
        if(m->ops.host_ejected(m->ops.context))m->stop=true;
        if(!m->stop)return;
        m->state=MT_USB_SD_STOPPING;
        const int stopped=m->ops.disconnect(m->ops.context);
        if(stopped>0)return;
        if(stopped<0) { m->state=MT_USB_SD_STOP_FAILED;return; }
        m->state=MT_USB_SD_REMOUNT_FAILED;
    }
    if(m->state==MT_USB_SD_REMOUNT_FAILED) {
        if(m->ops.release(m->ops.context)!=0)return;
        m->state=MT_USB_SD_RESTART_FAILED;
    }
    if(m->state==MT_USB_SD_RESTART_FAILED && m->ops.restart(m->ops.context)==0) {
        m->state=MT_USB_SD_IDLE;m->stop=false;
    }
}
const char *mt_usb_sd_mode_status(const mt_usb_sd_mode_t *m)
{
    switch(m->state) {
    case MT_USB_SD_ACTIVE:return "USB SD mounted: eject on PC, then Enter or Esc to stop";
    case MT_USB_SD_STOPPING:return "Waiting for USB transfers; eject the SD volume on the PC";
    case MT_USB_SD_STOP_FAILED:return "USB SD sync failed; card remains exclusive; retrying";
    case MT_USB_SD_REMOUNT_FAILED:return "USB stopped; SD remount failed; insert card to retry";
    case MT_USB_SD_RESTART_FAILED:return "SD remounted; core restart failed; retrying";
    default:return "USB SD stopped; active core restarted";
    }
}
