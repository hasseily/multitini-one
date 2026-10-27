/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_title.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static mt_c64_capabilities_t caps;
static mt_c64_title_values_t base;
static mt_c64_title_overlay_t overlay;
static mt_c64_title_report_t report;
static bool parse(const char *text, bool strict, mt_c64_title_origin_t origin)
{
    const mt_c64_title_parse_context_t c = {&caps, &base, false, false, strict, origin};
    return mt_c64_title_parse((const uint8_t *)text, strlen(text), &c, &overlay, &report);
}
int main(void)
{
    mt_c64_title_defaults(&base);
    caps.turbo = caps.ultimate_audio = true;
    caps.cpu = UINT32_MAX;
    caps.cpu2 = UINT32_MAX;
    caps.expansion = UINT32_MAX;
    assert(parse("\xef\xbb\xbf[U64 Specific Settings]\r\nTurbo Control=Manual\rCPU Speed=64\n"
                 "Badline Timing=Disabled\nSuperCPU Detect ($D0BC)=Enabled\n"
                 "[C64 and Cartridge Settings]\nRAM Expansion Unit=Disabled\n"
                 "Map Ultimate Audio $DF20-DFFF=Enabled\nCommand Interface=Enabled",
                 true, MT_TITLE_PRG));
    assert(overlay.values.turbo && (overlay.values.detail & 15) == 15);
    assert(overlay.values.detail & 0x20);
    assert(overlay.values.flags & 8);
    assert(overlay.values.reu && overlay.values.ua && overlay.values.uci);
    assert((overlay.owned &
            (MT_TITLE_TURBO | MT_TITLE_SPEED | MT_TITLE_MEMORY | MT_TITLE_SCPU | MT_TITLE_UCI)) ==
           (MT_TITLE_TURBO | MT_TITLE_SPEED | MT_TITLE_MEMORY | MT_TITLE_SCPU | MT_TITLE_UCI));
    mt_c64_title_values_t requested, applied;
    mt_c64_title_effective(&requested, &applied, &base, &overlay, true);
    assert(requested.reu && requested.ua && !applied.reu && !applied.ua);
    assert(base.reu && !base.ua && !base.turbo && base.flags == 0);
    assert(parse("[U64 Specific Settings]\nCPU Speed=16\nCPU Speed=bad\n", false, MT_TITLE_PRG));
    assert(report.errors == 1 && mt_c64_speed_mhz(overlay.values.detail & 15) == 16);
    assert(!parse("[U64 Specific Settings]\nCPU Speed=16\nCPU Speed=bad\n", true, MT_TITLE_MANUAL));
    assert(overlay.owned == 0);
    assert(parse("[C64U Specific Settings]\nCPU Speed=5\nJoystick Swapper=Swapped\n", true,
                 MT_TITLE_PRG));
    assert(report.adjusted == 1 && mt_c64_speed_mhz(overlay.values.detail & 15) == 4);
    assert(overlay.values.joystick == 1 && base.joystick == 2);
    assert(parse("[C64 and Cartridge Settings]\nCommand Interface=Disabled\n", true,
                 MT_TITLE_UCI_FILE));
    assert(!(overlay.owned & MT_TITLE_UCI));
    assert(parse("[unknown]\nCPU Speed=64\n[U64 Specific Settings]\n#comment\nCPU Speed=8\n", true,
                 MT_TITLE_PRG));
    assert(report.applied == 1 && mt_c64_speed_mhz(overlay.values.detail & 15) == 8);
    assert(report.skipped == 1 && strstr(report.message[0], "unknown section"));
    assert(parse("[C64 and Cartridge Settings]\nCustom Kernal ROM=custom.rom\n"
                 "REU Preload Image=game.reu\n", true, MT_TITLE_UCI_FILE));
    assert(!overlay.owned && !report.applied && report.skipped == 2 && report.entries == 2);
    assert(strstr(report.message[0], "not supported by title settings"));
    assert(!parse("CPU Speed=16\n", true, MT_TITLE_UCI_FILE));
    assert(report.errors && !overlay.owned);
    assert(!parse("[U64 Specific Settings]\nSystem Mode=wrong\n", true, MT_TITLE_UCI_FILE));
    assert(!parse("[U64 Specific Settings]\nSystem Mode=NTSC\n", true, MT_TITLE_UCI_FILE));
    assert(strstr(report.message[0], "different video image required"));
    assert(parse("[U64 Specific Settings]\nSystem Mode=NTSC\nCPU Speed=16\n", false,
                 MT_TITLE_PRG));
    assert(report.skipped == 1 && report.errors == 0 && (overlay.owned & MT_TITLE_SPEED));
    char path[256];
    assert(mt_c64_title_sidecar(path, sizeof(path), "Nested/Some title.PRG", ".cfg"));
    assert(strcmp(path, "Nested/Some title.cfg") == 0);
    assert(!mt_c64_title_sidecar(path, sizeof(path), "../escape.prg", ".cfg"));
    assert(!mt_c64_title_sidecar(path, sizeof(path), "bad./title.prg", ".cfg"));
    assert(!mt_c64_title_sidecar(path, sizeof(path), "/absolute.prg", ".cfg"));
    assert(!mt_c64_title_sidecar(path, sizeof(path), "0:/absolute.prg", ".cfg"));
    const char original[] = ";keep me\r\n[Future Section]\r\nFuture Key=untouched\r\n"
                            "[U64 Specific Settings]\r\nCPU Speed=16\r\n";
    assert(parse(original, true, MT_TITLE_PRG));
    static uint8_t output[MT_C64_CFG_BYTES + 1];
    size_t written;
    base.turbo = true;
    base.detail = 0x4f;
    assert(mt_c64_title_write((const uint8_t *)original, strlen(original), &base, &caps, &report,
                              output, MT_C64_CFG_BYTES, &written));
    output[written] = 0;
    assert(strstr((char *)output, ";keep me\r\n[Future Section]\r\nFuture Key=untouched\r\n"));
    assert(parse((char *)output, true, MT_TITLE_PRG));
    assert(overlay.values.turbo && (overlay.values.detail & 15) == 15);
    assert(overlay.values.reu && overlay.values.joystick == base.joystick);
    assert(!mt_c64_title_write((const uint8_t *)original, strlen(original), &base, &caps, &report,
                               output, 16, &written));
    const uint8_t binary[] = {'a', 0, 'b'};
    assert(!parse("[Multitini SID]\nSID 2 Address=$DF00\n", true, MT_TITLE_MANUAL));
    assert(report.errors && !overlay.owned);
    base.reu = false;
    assert(parse("[Multitini SID]\nSID 2 Address=$DF00\n", true, MT_TITLE_MANUAL));
    mt_c64_title_effective(&requested, &applied, &base, &overlay, false);
    assert(!mt_c64_title_io_conflict(&applied, &caps, false));
    applied.uci = true;
    assert(mt_c64_title_io_conflict(&applied, &caps, false));
    applied.uci = false;
    assert(mt_c64_title_io_conflict(&applied, &caps, true));
    /* Per-SID fields from an earlier file survive a later U64 class overlay. */
    assert(parse("[Multitini SID]\nSID 3 Address=$D600\n", true, MT_TITLE_MANUAL));
    mt_c64_title_overlay_t merged = overlay;
    assert(parse("[SID Addressing]\nUltiSID 2 Address=$D420\n", true, MT_TITLE_MANUAL));
    mt_c64_title_merge(&merged, &overlay);
    mt_c64_title_effective(&requested, &applied, &base, &merged, false);
    assert(applied.sid.sid_address[2] == 17 && applied.sid.sid_address[3] == 255);
    caps.expansion &= ~MT_C64_EXPANSION_CAPS_SID_IO_SLOTS;
    assert(parse("[Multitini SID]\nSID 2 Address=$DF00\n", true, MT_TITLE_MANUAL));
    assert((report.unavailable & MT_TITLE_SID) && !(overlay.owned & MT_TITLE_SID));
    const mt_c64_title_parse_context_t c = {&caps, &base, false, false, false, MT_TITLE_PRG};
    assert(!mt_c64_title_parse(binary, sizeof(binary), &c, &overlay, &report));
    /* Coexistence images retain explicit requested settings in title files;
     * legacy images still reject them while a cartridge is attached. */
    mt_c64_title_defaults(&base);
    caps.expansion = MT_C64_EXPANSION_CAPS_VALID | MT_C64_EXPANSION_CAPS_REU_16MB |
        MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO | MT_C64_EXPANSION_CAPS_CART_COEXIST;
    const mt_c64_title_parse_context_t cart_context = {&caps, &base, false, true, true, MT_TITLE_CRT};
    const char memory_cfg[] = "[C64 and Cartridge Settings]\nRAM Expansion Unit=Enabled\nMap Ultimate Audio $DF20-DFFF=Enabled\n";
    assert(mt_c64_title_parse((const uint8_t *)memory_cfg, sizeof memory_cfg - 1,
        &cart_context, &overlay, &report));
    assert(overlay.values.reu && overlay.values.ua && (overlay.owned & MT_TITLE_MEMORY));
    caps.expansion &= ~MT_C64_EXPANSION_CAPS_CART_COEXIST;
    assert(!mt_c64_title_parse((const uint8_t *)memory_cfg, sizeof memory_cfg - 1,
        &cart_context, &overlay, &report));
    puts("C64 title parser, ownership, capability clamps, path confinement and merge writer PASS");
    return 0;
}
