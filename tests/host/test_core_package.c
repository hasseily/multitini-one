/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_core_package.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static const char valid_manifest[] =
    "# Multitini core package\n"
    "schema=1\n"
    "id=c64\n"
    "name=Commodore 64\n"
    "core_id=0x43363420\n"
    "abi=0.5\n"
    "content_root=0:/multitini/cores/c64\n"
    "variant=pal|PAL 50 Hz|c64-pal.bin|4000000|0x1234ABCD\n"
    "variant=ntsc|NTSC 60 Hz|c64-ntsc.bin|4000004|0x89ABCDEF\n";

static mt_core_package_status_t parse_text(const char *text)
{
    mt_core_package_t package;
    return mt_core_package_parse(text, strlen(text), &package);
}

int main(void)
{
    mt_core_package_t package;
    char path[160];
    char long_line[300];
    static const uint8_t check[] = "123456789";
    static const char embedded_nul[] = "schema=1\0junk";

    assert(mt_core_package_parse(valid_manifest, strlen(valid_manifest),
                                 &package) == MT_CORE_PACKAGE_OK);
    assert(package.schema == 1U);
    assert(strcmp(package.id, "c64") == 0);
    assert(strcmp(package.name, "Commodore 64") == 0);
    assert(package.core_id == UINT32_C(0x43363420));
    assert(package.abi_major == 0U && package.abi_minor == 5U);
    assert(strcmp(package.content_root, "0:/multitini/cores/c64") == 0);
    assert(package.variant_count == 2U);
    assert(strcmp(package.variants[1].id, "ntsc") == 0);
    assert(package.variants[1].bitstream_size == 4000004U);
    assert(package.variants[1].bitstream_crc32 == UINT32_C(0x89ABCDEF));
    assert(mt_core_package_bitstream_path(
               "0:/multitini/cores/c64", &package.variants[0], path,
               sizeof(path)) == MT_CORE_PACKAGE_OK);
    assert(strcmp(path, "0:/multitini/cores/c64/c64-pal.bin") == 0);
    assert(mt_core_package_bitstream_path(
               "0:/multitini/cores/c64/", &package.variants[1], path,
               sizeof(path)) == MT_CORE_PACKAGE_OK);
    assert(strcmp(path, "0:/multitini/cores/c64/c64-ntsc.bin") == 0);
    assert(mt_core_package_bitstream_path(
               "0:/multitini/cores/c64", &package.variants[0], path, 8U) ==
           MT_CORE_PACKAGE_PATH_TOO_LONG);
    assert(mt_core_package_crc32(check, sizeof(check) - 1U) ==
           UINT32_C(0xCBF43926));

    assert(parse_text("schema=1\nid=c64\n") == MT_CORE_PACKAGE_MISSING_KEY);
    assert(parse_text("schema=2\n") == MT_CORE_PACKAGE_INVALID_VALUE);
    assert(parse_text("wat=1\n") == MT_CORE_PACKAGE_UNKNOWN_KEY);
    assert(parse_text("schema=1\nschema=1\n") ==
           MT_CORE_PACKAGE_DUPLICATE_KEY);
    assert(parse_text("schema 1\n") == MT_CORE_PACKAGE_SYNTAX);
    assert(mt_core_package_parse(embedded_nul, sizeof(embedded_nul) - 1U,
                                 &package) == MT_CORE_PACKAGE_SYNTAX);
    assert(parse_text(
        "schema=1\nid=C64\nname=C64\ncore_id=0x43363420\nabi=0.5\n"
        "content_root=0:/multitini/cores/c64\n"
        "variant=pal|PAL|c64.bin|4000000|0x12345678\n") ==
        MT_CORE_PACKAGE_INVALID_VALUE);
    assert(parse_text(
        "schema=1\nid=c64\nname=C64\ncore_id=0x43363420\nabi=0.5\n"
        "content_root=0:/multitini/cores/../bad\n"
        "variant=pal|PAL|c64.bin|4000000|0x12345678\n") ==
        MT_CORE_PACKAGE_INVALID_VALUE);
    assert(parse_text(
        "schema=1\nid=c64\nname=C64\ncore_id=0x43363420\nabi=0.5\n"
        "content_root=0:/multitini/cores/c64\n"
        "variant=pal|PAL|../bad.bin|4000000|0x12345678\n") ==
        MT_CORE_PACKAGE_INVALID_VALUE);
    assert(parse_text(
        "schema=1\nid=c64\nname=C64\ncore_id=0x43363420\nabi=0.5\n"
        "content_root=0:/multitini/cores/c64\n"
        "variant=pal|PAL|c64.bin|4000000|0x12345678\n"
        "variant=pal|NTSC|c64n.bin|4000000|0x12345678\n") ==
        MT_CORE_PACKAGE_DUPLICATE_VARIANT);

    memset(long_line, 'x', sizeof(long_line));
    long_line[sizeof(long_line) - 1U] = '\0';
    assert(parse_text(long_line) == MT_CORE_PACKAGE_LINE_TOO_LONG);
    assert(strcmp(mt_core_package_status_name(MT_CORE_PACKAGE_OK), "ok") == 0);
    return 0;
}
