/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_NES_CONTENT_H
#define MULTITINI_NES_CONTENT_H
#include "multitini_content.h"
#include "multitini_nes_interface.h"
typedef struct {
    size_t prg_size, chr_size;
    uint16_t mapper;
    bool battery, vertical_mirroring;
} mt_nes_content_info_t;
/* Accept only the implemented iNES 1 cartridge contract. No guessed mapper,
 * truncated payload, trailing payload or obsolete dirty-header heuristic. */
mt_content_validation_t mt_nes_content_inspect(const uint8_t *, size_t,
                                               mt_nes_content_info_t *);
#endif
