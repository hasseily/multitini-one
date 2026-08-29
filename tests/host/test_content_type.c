/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_content.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    uint8_t prg[] = {0x01U, 0x08U, 0xA9U, 0x00U, 0x60U};
    uint8_t g64[12U + 84U * 8U] = {0};
    uint8_t crt[0x40U + 0x10U + 4U] = {0};
    uint8_t basic_rom[8192U] = {0};
    const uint8_t dummy = 0U;
    mt_content_info_t info = {0};

    assert(mt_content_classify(NULL) == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify("") == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify("no-extension") == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify("folder.with.dot/file") == MT_CONTENT_UNKNOWN);
    assert(mt_content_classify("KERNAL.ROM") == MT_CONTENT_SYSTEM_ROM);
    assert(mt_content_classify("demo.PrG") == MT_CONTENT_PRG);
    assert(mt_content_classify("disk.d64") == MT_CONTENT_D64);
    assert(mt_content_classify("flux.G64") == MT_CONTENT_G64);
    assert(mt_content_classify("cart.crt") == MT_CONTENT_CRT);
    assert(mt_content_classify("future.tap") == MT_CONTENT_TAP_FUTURE);
    assert(mt_content_classify("future.d81") == MT_CONTENT_D81_FUTURE);
    assert(mt_content_supported_first_release(MT_CONTENT_D64));
    assert(!mt_content_supported_first_release(MT_CONTENT_TAP_FUTURE));
    assert(mt_content_may_write_first_release(MT_CONTENT_D64));
    assert(!mt_content_may_write_first_release(MT_CONTENT_G64));
    assert(strcmp(mt_content_type_name(MT_CONTENT_CRT), "CRT") == 0);

    assert(mt_content_inspect(MT_CONTENT_PRG, prg, sizeof(prg), &info) ==
           MT_CONTENT_VALID);
    assert(info.payload_offset == 2U && info.payload_size == 3U);
    assert(info.load_address == 0x0801U);
    prg[0] = 0xFFU;
    prg[1] = 0xFFU;
    assert(mt_content_inspect(MT_CONTENT_PRG, prg, sizeof(prg), &info) ==
           MT_CONTENT_BAD_STRUCTURE);

    assert(mt_content_inspect(MT_CONTENT_D64, &dummy, 174848U, &info) ==
           MT_CONTENT_VALID);
    assert(info.writable);
    assert(mt_content_inspect(MT_CONTENT_D64, &dummy, 174847U, &info) ==
           MT_CONTENT_WRONG_SIZE);

    memcpy(g64, "GCR-1541", 8U);
    g64[9] = 84U;
    g64[10] = 0x00U;
    g64[11] = 0x20U;
    assert(mt_content_inspect(MT_CONTENT_G64, g64, sizeof(g64), &info) ==
           MT_CONTENT_VALID);
    g64[8] = 1U;
    assert(mt_content_inspect(MT_CONTENT_G64, g64, sizeof(g64), &info) ==
           MT_CONTENT_UNSUPPORTED_VERSION);

    memcpy(crt, "C64 CARTRIDGE   ", 16U);
    crt[0x13] = 0x40U;
    crt[0x14] = 0x01U;
    memcpy(crt + 0x40U, "CHIP", 4U);
    crt[0x47] = 0x14U;
    crt[0x4EU] = 0x00U;
    crt[0x4FU] = 0x04U;
    assert(mt_content_inspect(MT_CONTENT_CRT, crt, sizeof(crt), &info) ==
           MT_CONTENT_VALID);
    crt[0x47] = 0x15U;
    assert(mt_content_inspect(MT_CONTENT_CRT, crt, sizeof(crt), &info) ==
           MT_CONTENT_BAD_STRUCTURE);

    assert(mt_c64_rom_validate(MT_C64_ROM_BASIC, basic_rom,
                               sizeof(basic_rom)) == MT_CONTENT_BAD_STRUCTURE);
    basic_rom[sizeof(basic_rom) - 1U] = 1U;
    assert(mt_c64_rom_validate(MT_C64_ROM_BASIC, basic_rom,
                               sizeof(basic_rom)) == MT_CONTENT_VALID);
    assert(mt_c64_rom_validate(MT_C64_ROM_CHARACTER, basic_rom,
                               sizeof(basic_rom)) == MT_CONTENT_WRONG_SIZE);
    assert(strcmp(mt_content_validation_name(MT_CONTENT_BAD_MAGIC),
                  "bad magic") == 0);
    return 0;
}
