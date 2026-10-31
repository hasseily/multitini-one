/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_CONTENT_SERVICE_H
#define MULTITINI_AMIGA_CONTENT_SERVICE_H
#include "multitini_amiga_boot.h"
#include "multitini_amiga_rtc.h"
void amiga_content_service_init(mt_core_link_t *,const char *media_root,mt_link_progress_fn,void *);
int amiga_content_service_boot(void);
void amiga_content_service_poll(void);
mt_link_result_t amiga_content_service_set_clock(const mt_clock_time_t *,uint32_t now_ms);
mt_link_result_t amiga_content_service_clock_status(void);
int amiga_content_service_mount(unsigned drive,const char *name);
int amiga_content_service_eject(unsigned drive);
int amiga_content_service_prepare_reset(void);
int amiga_content_service_resume_after_reset(void);
int amiga_content_service_shutdown(void);
int amiga_content_service_retry(void);
int amiga_content_service_configure(const mt_amiga_config_t *);
const mt_amiga_config_t *amiga_content_service_config(void);
const char *amiga_content_service_name(unsigned drive);
const char *amiga_content_service_error(void);
bool amiga_content_service_ready(void);
bool amiga_content_service_faulted(void);
void amiga_content_service_activity(unsigned drive,uint32_t *reads,uint32_t *writes);
#endif
