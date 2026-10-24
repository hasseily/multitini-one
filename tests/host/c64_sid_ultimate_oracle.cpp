// SPDX-License-Identifier: GPL-3.0-only
// Original test scaffold. The included decoder bodies are verbatim GPL
// Ultimate firmware, extracted from the separately pinned source at runtime.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "multitini_c64_sid.h"
}
class U64Config {
  public:
    static void auto_mirror(uint8_t *, uint8_t *, uint8_t *, int);
    static void fix_splits(uint8_t *, uint8_t *, uint8_t *);
};
#include "ultimate_sid.inc"
static unsigned offset(unsigned i) {
    return !i ? 1 : i <= 32 ? 0x40 + 2 * (i - 1) : 0xe0 + 2 * (i - 33);
}
int main() {
    const uint8_t splits[] = {0, 2, 4, 8, 16, 6, 18, 24};
    unsigned long checks = 0;
    for (unsigned a = 0; a < 49; ++a)
        for (unsigned b = 0; b < 49; ++b)
            for (unsigned sp = 0; sp < 8; ++sp)
                for (unsigned mirror = 0; mirror < 2; ++mirror) {
                    mt_c64_sid_model_t m;
                    mt_c64_sid_default(&m);
                    m.address[0] = a;
                    m.address[1] = b;
                    m.split = sp;
                    m.mirror = mirror;
                    mt_c64_sid_table_t t;
                    if (!mt_c64_sid_build(&m, 8, true, &t))
                        return 1;
                    uint8_t base[] = {0x40, 0x40, (uint8_t)offset(a), (uint8_t)offset(b)},
                            mask[] = {0xfe, 0xfe, 0xfe, 0xfe},
                            split[] = {0, 0, splits[sp], splits[sp]};
                    if (mirror)
                        U64Config::auto_mirror(base, mask, split, 4);
                    else
                        U64Config::fix_splits(base, mask, split);
                    for (unsigned s = 0; s < 48; ++s) {
                        unsigned address = s < 32 ? 0x40 + 2 * s : 0xe0 + 2 * (s - 32), want = 0;
                        for (unsigned g = 0; g < 2; ++g)
                            if ((address & mask[g + 2]) == base[g + 2]) {
                                unsigned bits = address & split[g + 2], part = 0;
                                if (sp == 5)
                                    part = bits >> 1;
                                else if (sp == 6)
                                    part = ((bits & 2) >> 1) + ((bits & 16) >> 3);
                                else if (sp == 7)
                                    part = bits >> 3;
                                else if (bits)
                                    part = 1;
                                want |= 1u << (2 * part + g);
                            }
                        unsigned got = (t.words[s] & 256) ? 0 : t.words[s];
                        ++checks;
                        if (got != want) {
                            std::fprintf(stderr,
                                         "FAIL U64 address a=%u b=%u split=%u mirror=%u slot=%u "
                                         "got=%x want=%x\n",
                                         a, b, sp, mirror, s, got, want);
                            return 1;
                        }
                    }
                }
    std::printf("PASS U64 SID decode checks=%lu\n", checks);
    return 0;
}
