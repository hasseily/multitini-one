/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_NES_BOOT_H
#define MULTITINI_NES_BOOT_H
#include "multitini_core_link.h"
#include "multitini_nes_interface.h"
typedef struct { const uint8_t *data; size_t size; } mt_nes_image_t;
typedef struct { mt_nes_image_t cartridge, save; } mt_nes_boot_images_t;
typedef enum {
    MT_NES_TRANSFER_NONE = 0, MT_NES_TRANSFER_RESET,
    MT_NES_TRANSFER_CARTRIDGE, MT_NES_TRANSFER_SAVE, MT_NES_TRANSFER_MOUNT,
    MT_NES_TRANSFER_RELEASE, MT_NES_TRANSFER_READY
} mt_nes_transfer_phase_t;
mt_link_result_t mt_nes_stage_traced(mt_core_link_t *, const mt_nes_boot_images_t *,
    uint32_t, mt_link_progress_fn, void *, mt_nes_transfer_phase_t *);
mt_link_result_t mt_nes_stage(mt_core_link_t *, const mt_nes_boot_images_t *,
    uint32_t, mt_link_progress_fn, void *);
mt_link_result_t mt_nes_release(mt_core_link_t *, uint32_t, mt_link_progress_fn, void *);
mt_link_result_t mt_nes_boot(mt_core_link_t *, const mt_nes_boot_images_t *,
    uint32_t, mt_link_progress_fn, void *);
#endif
