/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_GAMEBOY_BOOT_H
#define MULTITINI_GAMEBOY_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include "multitini_core_link.h"

typedef struct {
    const uint8_t *data;
    size_t size;
} mt_gameboy_image_t;

typedef struct {
    mt_gameboy_image_t cartridge;
    /* Optional. A null pointer and zero size select the bundled open boot. */
    mt_gameboy_image_t bootstrap_override;
} mt_gameboy_boot_images_t;

/* Validates both objects before streaming. A failed transfer deliberately
 * leaves core reset asserted. */
mt_link_result_t mt_gameboy_boot(
    mt_core_link_t *link, const mt_gameboy_boot_images_t *images,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context);

#endif
