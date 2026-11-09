/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_SPECTRUM_CONTENT_SERVICE_H
#define MULTITINI_SPECTRUM_CONTENT_SERVICE_H

#include "multitini_core_link.h"

/* Return NULL on success or a message suitable for the core menu. */
const char *spectrum_content_boot(mt_core_link_t *link, const char *core_directory,
                                 mt_link_progress_fn progress);
const char *spectrum_content_insert_tape(mt_core_link_t *link, const char *media_root,
                                        const char *relative, mt_link_progress_fn progress);
uint32_t spectrum_content_tape_config(const mt_core_link_t *link);
const char *spectrum_content_next_tape_mode(mt_core_link_t *link);
const char *spectrum_content_tape_name(void);
void spectrum_content_reset_selection(void);

#endif
