/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_REU_HOST_H
#define MULTITINI_C64_REU_HOST_H
#include "multitini_regs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *context;
    uint32_t (*read_host)(void *);
    void (*write_host)(void *, uint32_t);
    void (*pause)(void *, bool);
    bool (*paused)(void *);
    uint64_t (*milliseconds)(void *);
    void (*progress)(void *);
    bool (*copy)(void *, uint32_t offset, size_t size);
    void (*drain_writes)(void *); /* DSB, PL310 Cache Sync, DSB; never a range flush. */
} mt_c64_reu_host_ops_t;
typedef struct { bool reload_required; } mt_c64_reu_host_t;
typedef enum {
    MT_REU_LOAD_OK, MT_REU_LOAD_UNAVAILABLE, MT_REU_LOAD_RANGE, MT_REU_LOAD_CARTRIDGE,
    MT_REU_LOAD_BUSY, MT_REU_LOAD_PAUSE_TIMEOUT, MT_REU_LOAD_HOLD_TIMEOUT,
    MT_REU_LOAD_REU_BUSY, MT_REU_LOAD_PARTIAL, MT_REU_LOAD_RELOAD_REQUIRED,
    MT_REU_LOAD_BAD_ARGUMENT
} mt_c64_reu_load_result_t;
mt_c64_reu_load_result_t mt_c64_reu_load(mt_c64_reu_host_t *state,
    const mt_c64_reu_host_ops_t *ops, bool available, bool ua_present,
    bool cartridge, bool content_busy, uint32_t offset, size_t size);
const char *mt_c64_reu_load_message(mt_c64_reu_load_result_t result);
#endif
