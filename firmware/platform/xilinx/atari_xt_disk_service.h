/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_ATARI_XT_DISK_SERVICE_H
#define MULTITINI_ATARI_XT_DISK_SERVICE_H

#include "multitini_core_link.h"
#include <stdbool.h>

bool atari_xt_disk_available(const mt_core_link_t *link);
const char *atari_xt_disk_name(void);
const char *atari_xt_disk_mount(const mt_core_link_t *link, const char *media_root,
                               const char *relative);
bool atari_xt_disk_eject(void);
bool atari_xt_disk_quiesce(void);
void atari_xt_disk_poll(const mt_core_link_t *link);

#endif
