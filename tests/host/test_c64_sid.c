/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_sid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static bool export_key(void *context, const char *section, const char *key, const char *value) {
    uint64_t fields = 0;
    uint8_t classes = 0;
    CHECK(mt_c64_sid_apply_key(context, section, key, value, &fields, &classes) == MT_SID_APPLIED);
    CHECK(fields != 0);
    return true;
}
int main(int argc, char **argv) {
    mt_c64_sid_model_t m, copy, overlay;
    mt_c64_sid_table_t t;
    uint8_t record[128], corrupt[128], hw[64];
    char id[24];
    for (unsigned p = 1; p <= 6; ++p) {
        CHECK(mt_c64_sid_preset(&m, p));
        CHECK(mt_c64_sid_valid(&m));
        CHECK(mt_c64_sid_build(&m, 8, true, &t));
        CHECK(t.mapped == (p <= 3 ? 2 : p == 4 ? 3 : p == 5 ? 4 : 8));
        CHECK(t.legacy_mode == (p <= 3 ? (p == 1 ? 0 : p == 2 ? 2 : 3) : 255));
        CHECK(mt_c64_sid_encode(&m, "c64-pal", record));
        CHECK(mt_c64_sid_decode(&copy, id, record));
        CHECK(!strcmp(id, "c64-pal"));
        CHECK(!memcmp(&m, &copy, sizeof(m)));
        memset(&copy, 0, sizeof(copy));
        CHECK(mt_c64_sid_export(&m, export_key, &copy));
        CHECK(!memcmp(&m, &copy, sizeof(m)));
        for (unsigned b = 96; b < 128; ++b) {
            memcpy(corrupt, record, 128);
            corrupt[b] = 1;
            CHECK(!mt_c64_sid_decode(&copy, id, corrupt));
        }
    }
    mt_c64_sid_preset(&m, MT_SID_EIGHT);
    CHECK(mt_c64_sid_build(&m, 8, true, &t));
    const uint16_t expected[] = {0xd400, 0xd420, 0xd480, 0xd4a0, 0xd500, 0xd520, 0xd580, 0xd5a0};
    for (unsigned n = 0; n < 8; ++n)
        CHECK(t.base[n] == expected[n]);
    for (unsigned s = 0; s < 32; ++s)
        CHECK(t.words[s] == (1u << ((s & 1) + ((s & 4) >> 1) + ((s & 8) >> 1))));
    CHECK(mt_c64_sid_hwinfo(&t, 1, hw, sizeof(hw)) == 11 && hw[0] == 2);
    CHECK(mt_c64_sid_hwinfo(&t, 2, hw, sizeof(hw)) == 41 && hw[0] == 8);
    CHECK(mt_c64_sid_hwinfo(&t, 2, hw, 40) == 0);
    CHECK(mt_c64_sid_build(&m, 4, true, &t) && t.dropped == 4);
    for (unsigned s = 0; s < 32; ++s)
        CHECK(t.words[s] == ((s & 8) ? 0x101u : (1u << ((s & 1) + ((s & 4) >> 1)))));
    /* Explicit same-valued keys own their field; an address overlay removes
       old unowned per-chip address overrides without modifying stored state. */
    mt_c64_sid_default(&m);
    m.sid_address[2] = 17;
    overlay = m;
    uint64_t fields = 0;
    uint8_t classes = 0;
    CHECK(mt_c64_sid_apply_key(&overlay, "SID Addressing", "UltiSID 1 Address", "$D400", &fields,
                               &classes) == MT_SID_APPLIED);
    CHECK(fields == 1 && classes == 1);
    mt_c64_sid_effective(&copy, &m, &overlay, fields, classes);
    CHECK(copy.sid_address[2] == 255 && m.sid_address[2] == 17);
    CHECK(mt_c64_sid_apply_key(&overlay, "Multitini SID", "SID 3 Address", "$D600", &fields,
                               &classes) == MT_SID_APPLIED);
    mt_c64_sid_effective(&copy, &m, &overlay, fields, classes);
    CHECK(copy.sid_address[2] == 17);
    CHECK(mt_c64_sid_apply_key(&copy, "Speaker Mixer", "Vol UltiSid 1", "-7 dB", 0, 0) ==
          MT_SID_UNKNOWN);
    CHECK(mt_c64_sid_apply_key(&copy, "Audio Mixer", "Vol UltiSid 1", " 0 dB ", 0, 0) ==
              MT_SID_APPLIED &&
          copy.volume[0] == 24);
    CHECK(mt_c64_sid_apply_key(&copy, "Multitini SID", "SID 9 Address", "$D400", 0, 0) ==
          MT_SID_UNKNOWN);
    CHECK(mt_c64_sid_apply_key(&copy, "SID Addressing", "UltiSID 1 Address", "$D401", 0, 0) ==
          MT_SID_BAD_VALUE);
    mt_c64_sid_default(&m);
    m.mirror = 0;
    CHECK(mt_c64_sid_build(&m, 8, true, &t));
    CHECK(t.words[1] == 0x101);
    CHECK(t.words[8] == 2);
    m.address[1] = 33;
    CHECK(mt_c64_sid_build(&m, 8, true, &t));
    CHECK(t.words[32] == 2 && t.words[33] == 0 && t.base[1] == 0xde00);
    m.model[0] = 1;
    CHECK(mt_c64_sid_build(&m, 8, false, &t) && t.unsupported_models > 0);
    CHECK(!(t.words[48] & (1u << 16)));
    CHECK(mt_c64_sid_build(&m, 8, true, &t));
    CHECK((t.words[48] & 0x30000) == 0x30000);
    m.socket_enable[0] = 1;
    m.socket_address[0] = 49;
    CHECK(!mt_c64_sid_build(&m, 8, true, &t));
    m.socket_address[0] = 17;
    CHECK(mt_c64_sid_build(&m, 8, true, &t) && t.socket_chip[0] == 2 && t.base[2] == 0xd600);
    CHECK(mt_c64_sid_hwinfo(&t, 1, hw, sizeof(hw)) == 16 && hw[15] == 0x84);
    /* Socket allocation must not make an originally unmapped UltiSID group
       appear mapped in GET_HW_INFO command 1. */
    mt_c64_sid_default(&m);
    m.address[0] = 0;
    m.socket_enable[0] = 1;
    m.socket_address[0] = 17;
    CHECK(mt_c64_sid_build(&m, 8, true, &t));
    CHECK(t.socket_chip[0] == 0 && t.base[0] == 0xd600 && t.group_base[0] == 0);
    CHECK(mt_c64_sid_hwinfo(&t, 1, hw, sizeof(hw)) == 16 && hw[1] == 0 && hw[2] == 0);
    /* A single unity preset-panned chip takes the historical mono path;
       explicit panning or reduced gain requires the programmable mixer. */
    mt_c64_sid_default(&m);
    m.address[1] = 0;
    CHECK(mt_c64_sid_build(&m, 8, true, &t) && t.legacy_mode == 0);
    CHECK(mt_c64_sid_hwinfo(&t, 1, hw, sizeof(hw)) == 11 && hw[0] == 2);
    m.pan_from_preset[0] = 0;
    CHECK(mt_c64_sid_build(&m, 8, true, &t) && t.legacy_mode == 255);
    m.pan_from_preset[0] = 1;
    m.volume[0] = 23;
    CHECK(mt_c64_sid_build(&m, 8, true, &t) && t.legacy_mode == 255);
    /* The whole supported address/split space must produce a legal table and
       deterministic lower-owner fallback, including absent 4-chip parts. */
    for (unsigned a = 0; a < 49; ++a)
        for (unsigned b = 0; b < 49; ++b)
            for (unsigned split = 0; split < 8; ++split) {
                mt_c64_sid_default(&m);
                m.address[0] = (uint8_t)a;
                m.address[1] = (uint8_t)b;
                m.split = (uint8_t)split;
                CHECK(mt_c64_sid_build(&m, 8, true, &t));
                for (unsigned s = 0; s < 32; ++s) {
                    uint32_t v = t.words[s];
                    CHECK((v & 255) != 0 && (v & ~511u) == 0);
                    CHECK(!(v & 256) || ((v & 255) & ((v & 255) - 1)) == 0);
                }
            }
    mt_c64_sid_preset(&m, MT_SID_EIGHT);
    CHECK(mt_c64_sid_build(&m, 8, true, &t));
    if (argc == 2) {
        FILE *f = fopen(argv[1], "w");
        CHECK(f != NULL);
        for (unsigned n = 0; n < 57; ++n)
            CHECK(fprintf(f, "%08lx\n", (unsigned long)t.words[n]) > 0);
        CHECK(fclose(f) == 0);
    }
    printf("PASS SID firmware checks=%u\n", checks);
    return 0;
}
