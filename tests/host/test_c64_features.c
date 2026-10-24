/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_features.h"
#include "multitini_c64_interface.h"
#include <assert.h>
#include <stdio.h>

static uint32_t live_abi = 24, expansion = 0x8000001f;
static unsigned reads, offsets[8];
static uint32_t read32(void *ctx, uint32_t offset) {
    (void)ctx; assert(reads < 8); offsets[reads++] = offset;
    switch (offset) {
    case MT_REG_ABI_VERSION: return live_abi;
    case MT_REG_C64_EXPANSION_CAPS: return expansion;
    case MT_REG_C64_CPU_CAPS: return MT_C64_CPU_CAPS_RELEASE;
    case MT_REG_C64_CPU_CAPS2: return MT_C64_CPU_CAPS2_RELEASE;
    case MT_REG_DEBUG_STATUS: return MT_DEBUG_STATUS_AUDIO_PRESENT;
    default: assert(0); return 0;
    }
}
int main(void) {
    mt_core_link_t link = {.read32=read32};
    mt_c64_capabilities_t c;
    c = mt_c64_probe_capabilities(&link, MT_C64_CORE_ID, 0, 22);
    assert(reads == 0 && !c.turbo);
    c = mt_c64_probe_capabilities(&link, 0, 0, 24);
    assert(reads == 0 && !c.turbo);
    live_abi = 22;
    c = mt_c64_probe_capabilities(&link, MT_C64_CORE_ID, 0, 24);
    assert(reads == 1 && offsets[0] == MT_REG_ABI_VERSION && !c.reu_host);
    reads = 0; live_abi = 24;
    c = mt_c64_probe_capabilities(&link, MT_C64_CORE_ID, 0, 23);
    assert(reads == 2 && c.expansion == 0x80000007 && !c.turbo && !c.reu_host);
    reads = 0;
    c = mt_c64_probe_capabilities(&link, MT_C64_CORE_ID, 0, 24);
    assert(reads == 5 && c.turbo && c.reu_host && c.ram_peek && c.ultimate_audio && c.audio_capture);
    assert(offsets[0] == MT_REG_ABI_VERSION && offsets[1] == MT_REG_C64_EXPANSION_CAPS);
    for (unsigned i=0; i<256; ++i) {
        uint8_t detail = mt_c64_turbo_detail((uint8_t)i);
        assert(detail == (i < 64 ? 0x41 : i));
        uint32_t config = mt_c64_turbo_config((uint8_t)i, 0, true, &c, 0, false);
        assert(!(config & MT_C64_CPU_CONFIG_APPLY_MASK));
        assert((config & MT_C64_CPU_CONFIG_SPEED_MASK) == (detail & 15));
        assert((config & MT_C64_CPU_CONFIG_MODE_MASK) == ((unsigned)(detail >> 6) << 4));
        assert((config & MT_C64_CPU_CONFIG_BADLINES) == ((detail & 32) ? 0 : 64));
        assert(mt_c64_turbo_config((uint8_t)i, 1, true, &c, config, true) ==
               (config & ~MT_C64_CPU_CONFIG_DISK_AUTOSLOW));
        uint32_t off = mt_c64_turbo_config((uint8_t)i, 0, false, &c, config, true);
        assert(!(off & 0x30) && (off & 0xF000) == 0x1000);
        assert(mt_c64_turbo_config((uint8_t)i, 0, false, &c, off, true) == off);
    }
    assert(mt_c64_turbo_config(0x41,0,true,&c,0xF551,true)==0xF551);
    assert(mt_c64_turbo_config(0x42,0,true,&c,0xF551,true)==0x552);
    for (unsigned bit=0; bit<16; ++bit) {
        const uint16_t mask = (uint16_t)(1U << bit);
        for (unsigned i=0; i<16; ++i) {
            assert(mt_c64_clamp_speed(i,mask)==bit);
            assert(mt_c64_next_speed(i,mask)==bit);
            assert(mt_c64_step_speed(i,mask,true)==bit);
            assert(mt_c64_step_speed(i,mask,false)==bit);
        }
    }
    assert(mt_c64_next_speed(15,0xffff)==0);
    assert(mt_c64_step_speed(15,0x1fff,true)==12);
    assert(mt_c64_step_speed(0,0xffff,false)==0);
    assert(mt_c64_clamp_speed(15,0)==0);
    assert(mt_c64_speed_mhz(15)==64 && mt_c64_speed_mhz(14)==48);
    assert(mt_c64_expansion_restore(3,true,true,false,&c)==0x1b);
    assert(mt_c64_expansion_restore(3,false,true,true,&c)==3);
    assert(mt_c64_expansion_restore(3,false,true,false,&c)==0x1b);
    assert(mt_c64_expansion_restore(7,true,false,false,&c)==8);
    puts("PASS C64 firmware capability order, all preferences, apply counter and masks");
}
