/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_ATARI_XT_CONTENT_SERVICE_H
#define MULTITINI_ATARI_XT_CONTENT_SERVICE_H

#include "multitini_core_link.h"
#include <stdbool.h>

/* Return NULL on success or a message suitable for the core menu. */
const char *atari_xt_content_boot(mt_core_link_t *link, const char *core_directory,
                                  mt_link_progress_fn progress);
const char *atari_xt_content_restart(mt_core_link_t *link, mt_link_progress_fn progress);
const char *atari_xt_content_insert(mt_core_link_t *link, const char *media_root,
                                    const char *relative, mt_link_progress_fn progress);
const char *atari_xt_content_eject(mt_core_link_t *link, mt_link_progress_fn progress);
const char *atari_xt_content_cart_name(void);
bool atari_xt_content_roms_loaded(void);
bool atari_xt_content_cart_uncertain(void);
void atari_xt_content_reset_selection(void);

#endif
