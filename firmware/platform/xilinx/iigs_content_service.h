/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_IIGS_CONTENT_SERVICE_H
#define MULTITINI_IIGS_CONTENT_SERVICE_H
#include "multitini_core_link.h"
#include "multitini_iigs_content.h"
/* content_root is the package's /media folder; ROMs use sibling /roms. */
void iigs_content_service_init(mt_core_link_t *, const char *content_root, mt_link_progress_fn, void *);
int iigs_content_service_boot(void);
void iigs_content_service_set_timestamp(uint32_t timestamp);
void iigs_content_service_poll(void);
int iigs_content_service_mount(unsigned drive, const char *name);
int iigs_content_service_eject(unsigned drive);
int iigs_content_service_prepare_reset(void);
int iigs_content_service_resume_after_reset(void);
int iigs_content_service_shutdown(void);
int iigs_content_service_retry(void);
int iigs_content_service_configure(const mt_iigs_config_t *config);
int iigs_content_service_pram(bool save);
const mt_iigs_config_t *iigs_content_service_config(void);
uint32_t iigs_content_service_speed_mask(void);
uint16_t iigs_content_service_mouse_sensitivity(void);
int iigs_content_service_set_mouse_sensitivity(uint16_t percent);
const char *iigs_content_service_name(unsigned drive);
const char *iigs_content_service_error(void);
bool iigs_content_service_ready(void);
bool iigs_content_service_faulted(void);
#endif
