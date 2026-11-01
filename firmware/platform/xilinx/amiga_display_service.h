/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_DISPLAY_SERVICE_H
#define MULTITINI_AMIGA_DISPLAY_SERVICE_H
#include "multitini_amiga_display.h"
void amiga_display_service_init(mt_core_link_t *,const char *media_root,
                                 mt_amiga_display_apply_fn,void *context);
void amiga_display_service_poll(uint32_t now_ms);
void amiga_display_service_guest_restarted(void);
void amiga_display_service_set_paused(bool paused,uint32_t now_ms);
bool amiga_display_service_paused(void);
bool amiga_display_service_request(uint8_t mode,uint32_t now_ms);
bool amiga_display_service_available(void);
uint8_t amiga_display_service_mode_count(void);
bool amiga_display_service_mode_available(uint8_t mode);
bool amiga_display_service_pending(void);
uint8_t amiga_display_service_requested(void);
uint8_t amiga_display_service_applied(void);
uint8_t amiga_display_service_saved(void);
const char *amiga_display_service_status(void);
const mt_amiga_display_info_t *amiga_display_service_info(void);
#endif
