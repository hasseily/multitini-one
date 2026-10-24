/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_FEATURES_H
#define MULTITINI_C64_FEATURES_H
#include "multitini_regs.h"
#include "multitini_core_link.h"
#include <stdbool.h>

typedef struct {
    uint32_t expansion, cpu, cpu2;
    bool turbo, ultimate_audio, reu_host, ram_peek, audio_capture;
} mt_c64_capabilities_t;

/* Package ABI is checked before live ABI and any capability access. */
mt_c64_capabilities_t mt_c64_probe_capabilities(mt_core_link_t *link,
    uint32_t core_id, uint16_t package_major, uint16_t package_minor);
unsigned mt_c64_speed_mhz(unsigned index);
const char *mt_c64_turbo_reason(uint32_t status);
unsigned mt_c64_clamp_speed(unsigned index, uint16_t mask);
unsigned mt_c64_next_speed(unsigned index, uint16_t mask);
unsigned mt_c64_step_speed(unsigned index, uint16_t mask, bool faster);
uint8_t mt_c64_turbo_detail(uint8_t stored);
uint32_t mt_c64_turbo_config(uint8_t detail, uint8_t flags, bool enabled,
    const mt_c64_capabilities_t *caps, uint32_t previous, bool apply_change);
uint32_t mt_c64_expansion_restore(uint8_t sid, bool reu, bool ua,
    bool cartridge, const mt_c64_capabilities_t *caps);
#endif
