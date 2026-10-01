/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VIDEO_EFFECT_SERVICE_H
#define MULTITINI_VIDEO_EFFECT_SERVICE_H
#include "multitini_video_effects.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t (*read)(uint32_t offset);
    void (*write)(uint32_t offset, uint32_t value);
    uint32_t (*milliseconds)(void);
} mt_video_effect_service_ops_t;
void mt_video_effect_service_init(const mt_video_effect_service_ops_t *ops);
/* CPU0 stops servicing before PL replacement; CPU1 only uses shared DDR. */
void mt_video_effect_service_suspend(void);
/* Drain HP0 reads/writes after suspend before resetting the fabric. CPU1
 * only accesses reserved DDR and may finish while PL is being replaced. */
bool mt_video_effect_service_quiescent(void);
void mt_video_effect_service_configure(bool supported,
    const mt_video_effects_settings_t *settings);
void mt_video_effect_service_poll(void);
bool mt_video_effect_service_available(void);
const char *mt_video_effect_service_status(void);
/* Copy the actual committed transform only when its FPGA sequence matches.
 * Capture callers retry later while a new configuration is being committed. */
bool mt_video_effect_service_snapshot(mt_video_effects_tables_t *tables);
#endif
