/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_BOOT_H
#define MULTITINI_C64_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include "multitini_core_link.h"

typedef struct {
    const uint8_t *data;
    size_t size;
} mt_c64_rom_image_t;

typedef struct {
    mt_c64_rom_image_t basic;
    mt_c64_rom_image_t kernal;
    mt_c64_rom_image_t character;
} mt_c64_system_roms_t;

/*
 * Load all three selected system ROMs while core reset is asserted.
 * A failed transfer leaves core reset asserted. File-system ownership and ROM
 * redistribution policy remain outside this portable helper.
 */
mt_link_result_t mt_c64_load_system_roms(
    mt_core_link_t *link, const mt_c64_system_roms_t *roms,
    uint32_t max_polls, mt_link_progress_fn progress,
    void *progress_context);

#endif
