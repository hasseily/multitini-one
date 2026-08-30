/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XILINX_C64_D64_SERVICE_H
#define MULTITINI_XILINX_C64_D64_SERVICE_H

#include "multitini_c64_rom_files.h"
#include "multitini_core_link.h"

#define MT_C64_MEDIA_DIRECTORY MT_C64_SD_MEDIA_ROOT
#define MT_C64_DEFAULT_D64_NAME "drive8.d64"

void c64_d64_service_init(mt_core_link_t *link,
                          mt_link_progress_fn progress,
                          void *progress_context);
/*
 * Forget only the in-flight request de-duplication state after a soft core
 * reset.  The mounted image, its generation, and any pending recovery eject
 * remain intact.
 */
void c64_d64_service_notify_core_reset(void);
int c64_d64_service_mount(const char *file_name);
int c64_d64_service_mount_default(void);
int c64_d64_service_eject(void);
void c64_d64_service_poll(void);
void c64_d64_service_list(void);
void c64_d64_service_dump_status(void);

#endif
