/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_GENESIS_BOOT_H
#define MULTITINI_GENESIS_BOOT_H
#include <stdbool.h>
#include "multitini_core_link.h"
#include "multitini_genesis_interface.h"

typedef struct {
    mt_genesis_region_t region;
    uint8_t six_button_mask; /* Port 1 bit 0, port 2 bit 1. */
    uint8_t fm_chip; /* 0: YM2612; 1: YM3438. */
    uint8_t lowpass; /* Upstream audio filter mode 0..3. */
} mt_genesis_config_t;
typedef struct { const uint8_t *data; size_t size; } mt_genesis_image_t;
typedef struct {
    mt_genesis_image_t cartridge;
    mt_genesis_image_t save;
    mt_genesis_config_t config;
} mt_genesis_boot_images_t;
typedef enum {
    MT_GENESIS_TRANSFER_NONE = 0,
    MT_GENESIS_TRANSFER_RESET,
    MT_GENESIS_TRANSFER_CONFIG,
    MT_GENESIS_TRANSFER_CARTRIDGE,
    MT_GENESIS_TRANSFER_SAVE,
    MT_GENESIS_TRANSFER_MOUNT,
    MT_GENESIS_TRANSFER_RELEASE,
    MT_GENESIS_TRANSFER_READY
} mt_genesis_transfer_phase_t;

bool mt_genesis_config_valid(const mt_genesis_config_t *config, bool allow_auto);
/* Caller resolves AUTO against the cartridge and FPGA PAL/NTSC variant before
 * staging. Failed transfers retain reset; mounting save media precedes release. */
mt_link_result_t mt_genesis_stage(mt_core_link_t *, const mt_genesis_boot_images_t *,
    uint32_t max_polls, mt_link_progress_fn, void *);
/* Records the last attempted phase without additional MMIO, retries or writes.
 * Stream failure may already have performed the existing bounded abort. */
mt_link_result_t mt_genesis_stage_traced(mt_core_link_t *, const mt_genesis_boot_images_t *,
    uint32_t max_polls, mt_link_progress_fn, void *, mt_genesis_transfer_phase_t *);
mt_link_result_t mt_genesis_release(mt_core_link_t *, uint32_t max_polls,
    mt_link_progress_fn, void *);
mt_link_result_t mt_genesis_boot(mt_core_link_t *, const mt_genesis_boot_images_t *,
    uint32_t max_polls, mt_link_progress_fn, void *);
#endif
