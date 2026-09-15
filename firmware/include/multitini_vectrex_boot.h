/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX_BOOT_H
#define MULTITINI_VECTREX_BOOT_H
#include "multitini_core_link.h"
/* Validate before reset. A transfer failure leaves the machine in reset.
 * A NULL/zero cart leaves its loaded contents unchanged (initial BIOS boot). */
mt_link_result_t mt_vectrex_boot(mt_core_link_t *link,
    const uint8_t *bios, size_t bios_size, const uint8_t *cart, size_t cart_size,
    uint32_t max_polls, mt_link_progress_fn progress, void *context);
mt_link_result_t mt_vectrex_set_mode(mt_core_link_t *link, uint8_t mode,
    uint32_t max_polls, mt_link_progress_fn progress, void *context);
#endif
