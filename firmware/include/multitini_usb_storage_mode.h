/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_USB_STORAGE_MODE_H
#define MULTITINI_USB_STORAGE_MODE_H
#include <stdbool.h>
typedef enum { MT_USB_SD_IDLE, MT_USB_SD_ACTIVE, MT_USB_SD_STOPPING,
    MT_USB_SD_STOP_FAILED, MT_USB_SD_REMOUNT_FAILED, MT_USB_SD_RESTART_FAILED } mt_usb_sd_state_t;
typedef struct {
    void *context;
    int (*quiesce)(void *); /* flush/eject/close every SD owner and drain DMA */
    int (*acquire)(void *); /* exclusively unregister local filesystem */
    int (*connect)(void *);
    void (*poll)(void *);
    bool (*host_ejected)(void *);
    int (*disconnect)(void *); /* 1 pending, 0 stopped+synced, -1 failure */
    int (*release)(void *); /* remount; failed remount retains exclusivity */
    int (*restart)(void *); /* rescan and restart prior active core */
    void (*cancel_quiesce)(void *);
} mt_usb_sd_ops_t;
typedef struct { mt_usb_sd_ops_t ops; mt_usb_sd_state_t state; bool stop; } mt_usb_sd_mode_t;
void mt_usb_sd_mode_init(mt_usb_sd_mode_t *, const mt_usb_sd_ops_t *);
int mt_usb_sd_mode_enter(mt_usb_sd_mode_t *);
void mt_usb_sd_mode_stop(mt_usb_sd_mode_t *);
void mt_usb_sd_mode_poll(mt_usb_sd_mode_t *);
const char *mt_usb_sd_mode_status(const mt_usb_sd_mode_t *);
#endif
