/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_UCI_GEOS_H
#define MULTITINI_C64_UCI_GEOS_H
#include "multitini_c64_uci.h"
/* Serialized by the UCI engine. The owning target routes poll/abort until done. */
mt_uci_result_t c64_uci_geos_begin(const uint8_t command[896], size_t length, const char *cwd,
                                   mt_uci_reply_t *);
mt_uci_result_t c64_uci_geos_poll(mt_uci_reply_t *);
mt_uci_result_t c64_uci_geos_abort(void);
void c64_uci_geos_reset(void);
#endif
