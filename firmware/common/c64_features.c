/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_features.h"
#include "multitini_c64_interface.h"

static const uint8_t speeds[16] = {1,2,3,4,6,8,10,12,14,16,20,24,32,40,48,64};

mt_c64_capabilities_t mt_c64_probe_capabilities(mt_core_link_t *link,
    uint32_t core_id, uint16_t package_major, uint16_t package_minor)
{
    mt_c64_capabilities_t c = {0};
    if (core_id != MT_C64_CORE_ID || package_major != MT_ABI_MAJOR ||
        package_minor < MT_C64_EXPANSION_MIN_ABI_MINOR || link == NULL ||
        link->read32 == NULL) return c;
    const uint32_t abi = link->read32(link->context, MT_REG_ABI_VERSION);
    if ((abi >> 16) != MT_ABI_MAJOR || (uint16_t)abi < MT_C64_EXPANSION_MIN_ABI_MINOR)
        return c;
    c.expansion = link->read32(link->context, MT_REG_C64_EXPANSION_CAPS);
    if (!(c.expansion & MT_C64_EXPANSION_CAPS_VALID)) c.expansion = 0;
    const unsigned minor = package_minor < (uint16_t)abi ? package_minor : (uint16_t)abi;
    if (minor < MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR)
        c.expansion &= ~MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO;
    if (minor < MT_C64_REU_HOST_MIN_ABI_MINOR)
        c.expansion &= ~MT_C64_EXPANSION_CAPS_REU_HOST_LOAD;
    c.ultimate_audio = minor >= MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR &&
        (c.expansion & (MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO | MT_C64_EXPANSION_CAPS_REU_16MB)) ==
        (MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO | MT_C64_EXPANSION_CAPS_REU_16MB);
    c.reu_host = minor >= MT_C64_REU_HOST_MIN_ABI_MINOR &&
        (c.expansion & MT_C64_EXPANSION_CAPS_REU_HOST_LOAD) != 0;
    if (minor >= MT_C64_TURBO_MIN_ABI_MINOR) {
        c.cpu = link->read32(link->context, MT_REG_C64_CPU_CAPS);
        c.cpu2 = link->read32(link->context, MT_REG_C64_CPU_CAPS2);
        if (!(c.cpu & MT_C64_CPU_CAPS_VALID)) c.cpu = 0;
        if (!(c.cpu2 & MT_C64_CPU_CAPS2_VALID)) c.cpu2 = 0;
        c.turbo = (c.cpu & MT_C64_CPU_CAPS_INDEX_MASK) != 0 && c.cpu2 != 0;
    }
    if (minor >= MT_DEBUG_MIN_ABI_MINOR) {
        c.ram_peek = (c.cpu2 & MT_C64_CPU_CAPS2_RAM_PEEK) != 0;
        c.audio_capture = (link->read32(link->context, MT_REG_DEBUG_STATUS) & MT_DEBUG_STATUS_AUDIO_PRESENT) != 0;
    }
    return c;
}

unsigned mt_c64_speed_mhz(unsigned index) { return speeds[index & 15U]; }
const char *mt_c64_turbo_reason(uint32_t status)
{
    if (status & MT_C64_CPU_STATUS_CARTRIDGE) return "cartridge";
    if (status & MT_C64_CPU_STATUS_DISK) {
        const unsigned cause = (status >> 6) & 3U;
        return cause == 1U ? "disk: line held" : cause == 2U ?
            "disk: cable held" : "disk: holdoff";
    }
    if (status & MT_C64_CPU_STATUS_LOADER) return "loader";
    if (status & MT_C64_CPU_STATUS_RESET_WINDOW) return "reset window";
    if (status & MT_C64_CPU_STATUS_PAUSE) return "paused";
    return "running";
}
unsigned mt_c64_clamp_speed(unsigned index, uint16_t mask)
{
    if (!mask) return 0;
    if (index > 15U) index = 15U;
    for (unsigned i = index + 1U; i != 0; --i)
        if (mask & (1U << (i - 1U))) return i - 1U;
    for (unsigned i = 0; i < 16; ++i) if (mask & (1U << i)) return i;
    return 0;
}
unsigned mt_c64_next_speed(unsigned index, uint16_t mask)
{
    if (!mask) return 0;
    for (unsigned n = 1; n <= 16; ++n) {
        const unsigned i = (index + n) & 15U;
        if (mask & (1U << i)) return i;
    }
    return 0;
}
unsigned mt_c64_step_speed(unsigned index, uint16_t mask, bool faster)
{
    index = mt_c64_clamp_speed(index, mask);
    if (faster) {
        for (unsigned i = index + 1; i < 16; ++i) if (mask & (1U << i)) return i;
    } else {
        for (unsigned i = index; i != 0; --i) if (mask & (1U << (i-1))) return i-1;
    }
    return index;
}
uint8_t mt_c64_turbo_detail(uint8_t stored)
{
    return (stored & 0xC0U) ? stored : 0x41U;
}
uint32_t mt_c64_turbo_config(uint8_t detail, uint8_t flags, bool enabled,
    const mt_c64_capabilities_t *caps, uint32_t previous, bool apply_change)
{
    if (caps == NULL || !caps->turbo) return 0;
    detail = mt_c64_turbo_detail(detail);
    uint32_t config = MT_C64_CPU_CONFIG_AUTHORITATIVE |
        mt_c64_clamp_speed(detail & 15U, (uint16_t)caps->cpu);
    unsigned mode = enabled ? (detail >> 6) : 0;
    if (mode == 2 && !(caps->cpu & MT_C64_CPU_CAPS_REGISTERS)) mode = 1;
    if (mode == 3 && !(caps->cpu & MT_C64_CPU_CAPS_TURBO_ENABLE)) mode = 1;
    config |= mode << 4;
    if (!(detail & 0x20U) || !(caps->cpu & MT_C64_CPU_CAPS_BADLINES))
        config |= MT_C64_CPU_CONFIG_BADLINES;
    if (!(flags & 1U)) config |= MT_C64_CPU_CONFIG_DISK_AUTOSLOW;
    if ((flags & 2U) && (caps->cpu & MT_C64_CPU_CAPS_CARTRIDGE_TURBO))
        config |= MT_C64_CPU_CONFIG_CARTRIDGE_NATIVE;
    if ((detail & 0x10U) && (caps->cpu2 & MT_C64_CPU_CAPS2_FOLLOW_CPU))
        config |= MT_C64_CPU_CONFIG_FOLLOW_CPU;
    config |= previous & MT_C64_CPU_CONFIG_DEBUG_MASK;
    unsigned counter = (previous >> 12) & 15U;
    if (apply_change && ((config ^ previous) & 0x7FU)) counter = (counter + 1U) & 15U;
    return config | (counter << 12);
}
uint32_t mt_c64_expansion_restore(uint8_t sid, bool reu, bool ua,
    bool cartridge, const mt_c64_capabilities_t *caps)
{
    if (caps == NULL || !(caps->expansion & MT_C64_EXPANSION_CAPS_VALID)) return 0;
    uint32_t config = (sid == 2 || sid == 3) &&
        (caps->expansion & MT_C64_EXPANSION_CAPS_DUAL_SID) ? sid : 0;
    /* Restore never changes the global UA preference in persistent storage. */
    const bool effective_ua = ua && caps->ultimate_audio && !cartridge;
    if ((reu || effective_ua) && (caps->expansion & MT_C64_EXPANSION_CAPS_REU_16MB)) {
        config |= MT_C64_EXPANSION_REU_ENABLE;
        if (effective_ua) config |= MT_C64_EXPANSION_ULTIMATE_AUDIO_ENABLE;
    }
    return config;
}
