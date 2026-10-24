/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_reu_host.h"

static bool wait_state(const mt_c64_reu_host_ops_t *o, uint32_t mask,
    uint32_t value, unsigned timeout, bool pause)
{
    const uint64_t start = o->milliseconds(o->context);
    for (;;) {
        if (pause ? o->paused(o->context) :
            ((o->read_host(o->context) & mask) == value)) return true;
        if (o->milliseconds(o->context) - start >= timeout) return false;
        o->progress(o->context);
    }
}
mt_c64_reu_load_result_t mt_c64_reu_load(mt_c64_reu_host_t *s,
    const mt_c64_reu_host_ops_t *o, bool available, bool ua,
    bool cartridge, bool content_busy, uint32_t offset, size_t size)
{
    if (!s || !o || !o->read_host || !o->write_host || !o->pause || !o->paused ||
        !o->milliseconds || !o->progress || !o->copy || !o->drain_writes)
        return MT_REU_LOAD_BAD_ARGUMENT;
    if (s->reload_required) return MT_REU_LOAD_RELOAD_REQUIRED;
    if (!available) return MT_REU_LOAD_UNAVAILABLE;
    if (offset > MT_C64_REU_DDR_SIZE || size > MT_C64_REU_DDR_SIZE - offset)
        return MT_REU_LOAD_RANGE;
    if (cartridge) return MT_REU_LOAD_CARTRIDGE;
    if (content_busy) return MT_REU_LOAD_BUSY;
    mt_c64_reu_load_result_t result = MT_REU_LOAD_OK;
    uint32_t host = 0;
    bool hold_set = false;
    o->pause(o->context, true);
    if (!wait_state(o,0,0,100,true)) {
        result = MT_REU_LOAD_PAUSE_TIMEOUT;
        goto resume;
    }
    host = o->read_host(o->context) & MT_C64_REU_HOST_REQUEST_MASK;
    if (ua) {
        host |= MT_C64_REU_HOST_UA_HOLD;
        o->write_host(o->context,host);
        hold_set = true;
        if (!wait_state(o,MT_C64_REU_HOST_UA_HELD,MT_C64_REU_HOST_UA_HELD,100,false)) {
            result = MT_REU_LOAD_HOLD_TIMEOUT;
            goto resume;
        }
    }
    if (!wait_state(o,MT_C64_REU_HOST_IDLE,MT_C64_REU_HOST_IDLE,1000,false)) {
        result = MT_REU_LOAD_REU_BUSY;
        goto resume;
    }
    for (size_t done = 0; done < size;) {
        const size_t amount = size - done < 4096 ? size - done : 4096;
        if (!o->copy(o->context,offset+(uint32_t)done,amount)) {
            result = MT_REU_LOAD_PARTIAL;
            break;
        }
        done += amount;
        o->progress(o->context);
    }
    /* Partial stores need the same drain and invalidate as a complete file. */
    o->drain_writes(o->context);
    for (unsigned attempt = 0; attempt != 2; ++attempt) {
        const uint32_t counter = ((host & MT_C64_REU_HOST_REQUEST_MASK)+1U) & 15U;
        host = (host & MT_C64_REU_HOST_UA_HOLD) | counter;
        o->write_host(o->context,host);
        if (wait_state(o,MT_C64_REU_HOST_DONE_MASK,counter<<8,100,false)) goto resume;
    }
    /* Guest reset cannot recover stale read lines. Only core reload may clear this. */
    s->reload_required = true;
    return MT_REU_LOAD_RELOAD_REQUIRED;
resume:
    if (hold_set) o->write_host(o->context,host & MT_C64_REU_HOST_REQUEST_MASK);
    o->pause(o->context,false);
    return result;
}
const char *mt_c64_reu_load_message(mt_c64_reu_load_result_t result)
{
    switch (result) {
    case MT_REU_LOAD_OK: return "REU image loaded";
    case MT_REU_LOAD_UNAVAILABLE: return "REU image load not available on this image";
    case MT_REU_LOAD_RANGE: return "REU image exceeds 16 MB";
    case MT_REU_LOAD_CARTRIDGE: return "Eject the cartridge before loading REU memory";
    case MT_REU_LOAD_BUSY: return "Busy; try again";
    case MT_REU_LOAD_PAUSE_TIMEOUT: return "C64 did not pause; try again";
    case MT_REU_LOAD_HOLD_TIMEOUT: return "Ultimate Audio did not stop; try again";
    case MT_REU_LOAD_REU_BUSY: return "REU busy; try again";
    case MT_REU_LOAD_PARTIAL: return "REU image load failed; REU contents are partial";
    case MT_REU_LOAD_RELOAD_REQUIRED: return "REU cache did not clear; reload the C64 core";
    default: return "REU image load failed";
    }
}
