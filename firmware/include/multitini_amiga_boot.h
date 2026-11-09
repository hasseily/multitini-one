/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_BOOT_H
#define MULTITINI_AMIGA_BOOT_H
#include "multitini_core_link.h"
#include "multitini_amiga_content.h"
mt_link_result_t mt_amiga_host_access(mt_core_link_t *,uint8_t opcode,uint32_t payload,
    uint32_t polls,uint32_t *response);
typedef struct {
    uint16_t sequence,status,config_begin,config_end;
    uint32_t service_hz,epoch;
    uint64_t counters[MT_AMIGA_DIAG_COUNTERS];
} mt_amiga_diag_snapshot_t;
/* Optional read-only instrumentation. Control ACK must echo sequence. Read
 * publishes output only after a complete, coherent, validated frozen bank.
 * An invalid native capture remains readable with its status reason bits. */
mt_link_result_t mt_amiga_diag_control(mt_core_link_t *,uint16_t sequence,
    uint8_t action,uint32_t polls);
mt_link_result_t mt_amiga_diag_read(mt_core_link_t *,uint16_t sequence,
    uint32_t polls,mt_amiga_diag_snapshot_t *);
/* Single main-loop owner. Stage is ARM-owned only while idle. Capture means
 * IDE -> ARM; false means ARM -> IDE. On failure after submission the caller
 * must not replay this sector or fall back to the word transport. */
mt_link_result_t mt_amiga_bulk_transfer(mt_core_link_t *,bool capture,
    uint8_t sector[MT_AMIGA_SECTOR_BYTES],uint32_t polls,bool *submitted);
bool mt_amiga_fast_ram_16_available(const mt_core_link_t *);
mt_link_result_t mt_amiga_stage(mt_core_link_t *,const mt_amiga_config_t *,bool aga_pal,const uint8_t *rom,
    size_t bytes,uint32_t polls,mt_link_progress_fn,void *);
mt_link_result_t mt_amiga_native_configure(mt_core_link_t *,const mt_amiga_config_t *,bool aga_pal,uint32_t polls);
#endif
