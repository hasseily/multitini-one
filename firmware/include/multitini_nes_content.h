/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_NES_CONTENT_H
#define MULTITINI_NES_CONTENT_H
#include "multitini_content.h"
#include "multitini_nes_interface.h"
typedef struct {
    size_t prg_size, chr_size;
    uint16_t mapper;
    bool battery, vertical_mirroring;
    uint8_t canonical_header[16];
    const char *rejection_reason;
} mt_nes_content_info_t;
/* Validate the implemented iNES 1 and bounded NES 2.0 board contracts. The
 * canonical iNES 1 header describes the same supported hardware to the retained
 * FPGA loader. Inspection never changes the caller's cartridge bytes. */
mt_content_validation_t mt_nes_content_inspect(const uint8_t *, size_t,
                                               mt_nes_content_info_t *);
#endif
