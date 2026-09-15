/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX32_LOWLEVEL_H
#define MULTITINI_VECTREX32_LOWLEVEL_H
#include "vectrex32.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct mt_v32_lowlevel mt_v32_lowlevel;
typedef enum mt_v32_lowlevel_status {
    MT_V32_LL_OK=0, MT_V32_LL_CANCELLED=1, MT_V32_LL_BUDGET=2,
    MT_V32_LL_MEMORY=3, MT_V32_LL_OPCODE=4, MT_V32_LL_ARGUMENT=5,
    MT_V32_LL_REENTRANT=6
} mt_v32_lowlevel_status;
typedef struct mt_v32_lowlevel_pen {
    int32_t x,y;
    uint8_t scale,intensity,pattern;
} mt_v32_lowlevel_pen;
typedef struct mt_v32_lowlevel_registers {
    uint16_t x,y,u,s,pc;
    uint8_t a,b,dp,cc;
} mt_v32_lowlevel_registers;

/* Instruction-cycle approximation, not a bus-cycle-accurate 6522/Vectrex.
 * Callback lifetime must cover the object. Call this API from one host thread;
 * callbacks may service the host but must not throw or re-enter the backend.
 * Deferred BIOS Peek is guarded like execute. A callback's attempted mutation
 * or destruction is rejected; the owner must destroy the object after return.
 * CPU registers, RAM, DPRAM and VIA state persist until reset/destroy.
 */
mt_v32_lowlevel *mt_v32_lowlevel_create(const mt_v32_callbacks *callbacks);
void mt_v32_lowlevel_destroy(mt_v32_lowlevel *machine);
void mt_v32_lowlevel_reset(mt_v32_lowlevel *machine);
void mt_v32_lowlevel_begin_frame(mt_v32_lowlevel *machine);
void mt_v32_lowlevel_set_pen(mt_v32_lowlevel *machine,const mt_v32_lowlevel_pen *pen);
void mt_v32_lowlevel_get_pen(const mt_v32_lowlevel *machine,mt_v32_lowlevel_pen *pen);
void mt_v32_lowlevel_set_controls(mt_v32_lowlevel *machine,const int controls[2][6]);
void mt_v32_lowlevel_get_registers(const mt_v32_lowlevel *machine,mt_v32_lowlevel_registers *registers);
void mt_v32_lowlevel_set_registers(mt_v32_lowlevel *machine,const mt_v32_lowlevel_registers *registers);

/* Each frame places live code arrays consecutively in a 2KiB DPRAM arena.
 * This allocator is an explicit adapter layout, not the original Vectrex32
 * compiler's unavailable layout. Stop at fallthrough immediately after the
 * supplied bytes; JSR/RTS into the external BIOS executes normally.
 */
mt_v32_lowlevel_status mt_v32_lowlevel_execute(mt_v32_lowlevel *machine,
    const uint8_t *bytes,size_t count,uint64_t cycle_budget);
mt_v32_lowlevel_status mt_v32_lowlevel_peek_dpram(mt_v32_lowlevel *machine,
    unsigned offset,uint8_t *bytes,size_t count);
mt_v32_lowlevel_status mt_v32_lowlevel_poke_dpram(mt_v32_lowlevel *machine,
    unsigned offset,const uint8_t *bytes,size_t count);
/* CPU-address read for deferred Peek: 1..16 bytes, completed by the caller
 * after rendering. VIA reads retain normal read side effects. */
mt_v32_lowlevel_status mt_v32_lowlevel_read_memory(mt_v32_lowlevel *machine,
    uint16_t address,uint8_t *bytes,size_t count);
const char *mt_v32_lowlevel_error(const mt_v32_lowlevel *machine);
uint64_t mt_v32_lowlevel_cycles(const mt_v32_lowlevel *machine);

#ifdef __cplusplus
}
#endif
#endif
