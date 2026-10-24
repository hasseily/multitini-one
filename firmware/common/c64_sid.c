/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_sid.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t split_bits[8] = {0, 2, 4, 8, 16, 6, 18, 24};
static const uint8_t volume_gain[31] = {0,   1,   2,   4,   6,   8,   16,  18,  20, 23, 26,
                                        29,  32,  36,  40,  45,  51,  57,  64,  72, 81, 91,
                                        102, 114, 128, 144, 161, 181, 203, 228, 255};
static const uint16_t pan_gain[11] = {0, 40, 79, 116, 150, 181, 207, 228, 243, 253, 256};
static const char *const volumes[31] = {
    "OFF",    "-42 dB", "-36 dB", "-30 dB", "-27 dB", "-24 dB", "-18 dB", "-17 dB",
    "-16 dB", "-15 dB", "-14 dB", "-13 dB", "-12 dB", "-11 dB", "-10 dB", "-9 dB",
    "-8 dB",  "-7 dB",  "-6 dB",  "-5 dB",  "-4 dB",  "-3 dB",  "-2 dB",  "-1 dB",
    "0 dB",   "+1 dB",  "+2 dB",  "+3 dB",  "+4 dB",  "+5 dB",  "+6 dB"};
static const char *const pans[11] = {"Left 5",  "Left 4",  "Left 3",  "Left 2",  "Left 1", "Center",
                                     "Right 1", "Right 2", "Right 3", "Right 4", "Right 5"};
static const char *const splits[8] = {"Off",      "1/2 (A5)",    "1/2 (A6)",    "1/2 (A7)",
                                      "1/2 (A8)", "1/4 (A5,A6)", "1/4 (A5,A8)", "1/4 (A7,A8)"};
static const char *const curves[7] = {"8580 Lo", "8580 Hi", "6581",   "6581 Alt",
                                      "U2 Low",  "U2 Mid",  "U2 High"};
static const char *const digis[4] = {"Off", "Low", "Medium", "High"};
static const char *const models[2] = {"6581", "8580"};
static const char *const presets[7] = {"Custom", "Single", "Dual $D420", "Dual $D500",
                                       "Triple", "Quad",   "Eight"};
static bool same(const char *a, const char *b) {
    if (!a || !b)
        return false;
    while (isspace((unsigned char)*a))
        ++a;
    while (isspace((unsigned char)*b))
        ++b;
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    while (isspace((unsigned char)*a))
        ++a;
    while (isspace((unsigned char)*b))
        ++b;
    return !*a && !*b;
}
static int choice(const char *v, const char *const *names, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        if (same(v, names[i]))
            return (int)i;
    return -1;
}
static uint8_t offset(unsigned index) {
    return index == 0    ? 1
           : index <= 32 ? (uint8_t)(0x40 + 2 * (index - 1))
                         : (uint8_t)(0xe0 + 2 * (index - 33));
}
static int address(const char *v) {
    if (same(v, "Unmapped"))
        return 0;
    while (isspace((unsigned char)*v))
        ++v;
    if (*v == '$')
        ++v;
    char *end;
    unsigned long a = strtoul(v, &end, 16);
    while (isspace((unsigned char)*end))
        ++end;
    if (end == v || *end || (a & 31))
        return -1;
    if (a >= 0xd400 && a <= 0xd7e0)
        return (int)((a - 0xd400) / 32 + 1);
    if (a >= 0xde00 && a <= 0xdfe0)
        return (int)((a - 0xde00) / 32 + 33);
    return -1;
}
static uint8_t *field(mt_c64_sid_model_t *m, unsigned i) {
    if (i < 2)
        return &m->address[i];
    if (i == 2)
        return &m->split;
    if (i == 3)
        return &m->mirror;
    if (i < 6)
        return &m->socket_address[i - 4];
    if (i < 8)
        return &m->socket_enable[i - 6];
    if (i == 8)
        return &m->ext_split;
    if (i == 9)
        return &m->paddle_override;
    if (i < 12)
        return &m->model[i - 10];
    if (i < 14)
        return &m->digi[i - 12];
    if (i < 16)
        return &m->curve[i - 14];
    if (i < 18)
        return &m->resonance[i - 16];
    if (i < 20)
        return &m->volume[i - 18];
    if (i < 22)
        return &m->pan[i - 20];
    if (i < 24)
        return &m->pan_from_preset[i - 22];
    if (i == 24)
        return &m->master;
    if (i < 57) {
        unsigned n = (i - 25) / 4;
        switch ((i - 25) % 4) {
        case 0:
            return &m->sid_address[n];
        case 1:
            return &m->sid_model[n];
        case 2:
            return &m->sid_volume[n];
        default:
            return &m->sid_pan[n];
        }
    }
    if (i == 57)
        return &m->headroom;
    if (i == 58)
        return &m->dc_remove;
    if (i == 59)
        return &m->resample;
    if (i < 62)
        return &m->socket_volume[i - 60];
    return &m->socket_pan[i - 62];
}
bool mt_c64_sid_valid(const mt_c64_sid_model_t *m) {
    if (!m || m->split > 7 || m->mirror > 1 || m->ext_split > 5 || m->paddle_override > 1 ||
        m->master > 30 || m->headroom > 3 || m->dc_remove > 2 || m->resample > 1)
        return false;
    for (unsigned g = 0; g < 2; ++g)
        if (m->address[g] > 48 || m->socket_address[g] > 48 || m->socket_enable[g] > 1 ||
            m->model[g] > 1 || m->digi[g] > 3 || m->curve[g] > 6 || m->resonance[g] > 1 ||
            m->volume[g] > 30 || m->pan[g] > 10 || m->pan_from_preset[g] > 1 ||
            m->socket_volume[g] > 30 || m->socket_pan[g] > 10)
            return false;
    for (unsigned n = 0; n < 8; ++n)
        if ((m->sid_address[n] != 255 && m->sid_address[n] > 48) ||
            (m->sid_model[n] != 255 && m->sid_model[n] > 1) ||
            (m->sid_volume[n] != 255 && m->sid_volume[n] > 30) ||
            (m->sid_pan[n] != 255 && m->sid_pan[n] > 10))
            return false;
    return true;
}
bool mt_c64_sid_preset(mt_c64_sid_model_t *m, unsigned p) {
    if (!m || p < 1 || p > 6)
        return false;
    memset(m, 0, sizeof(*m));
    m->mirror = 1;
    m->paddle_override = 1;
    m->master = 24;
    for (unsigned n = 0; n < 8; ++n)
        m->sid_address[n] = m->sid_model[n] = m->sid_volume[n] = m->sid_pan[n] = 255;
    for (unsigned g = 0; g < 2; ++g) {
        m->socket_address[g] = 1;
        m->volume[g] = 24;
        m->digi[g] = 2;
        m->pan_from_preset[g] = 1;
        m->socket_volume[g] = 24;
        m->socket_pan[g] = 5;
    }
    m->address[0] = 1;
    m->address[1] = p == MT_SID_SINGLE ? 1 : p == MT_SID_DUAL500 ? 9 : 2;
    m->pan[1] = 10;
    if (p == MT_SID_TRIPLE || p == MT_SID_QUAD)
        m->split = 2;
    if (p == MT_SID_TRIPLE) {
        m->pan[0] = 5;
        m->pan[1] = 0;
        m->sid_pan[2] = 10;
        m->sid_address[3] = 0;
    }
    if (p == MT_SID_EIGHT) {
        m->split = 7;
        m->pan[0] = 3;
        m->pan[1] = 7;
    }
    return true;
}
void mt_c64_sid_default(mt_c64_sid_model_t *m) { (void)mt_c64_sid_preset(m, MT_SID_DUAL500); }
unsigned mt_c64_sid_preset_id(const mt_c64_sid_model_t *m) {
    if (!m)
        return 0;
    for (unsigned p = 1; p < 7; ++p) {
        mt_c64_sid_model_t candidate;
        mt_c64_sid_preset(&candidate, p);
        if (!memcmp(m, &candidate, sizeof(*m)))
            return p;
    }
    return 0;
}
const char *mt_c64_sid_preset_name(unsigned p) { return p < 7 ? presets[p] : "Custom"; }
const char *mt_c64_sid_volume_name(unsigned i) {
    return i == 255 ? "Follow" : i < 31 ? volumes[i] : "Invalid";
}
const char *mt_c64_sid_pan_name(unsigned i) {
    return i == 255 ? "Follow" : i < 11 ? pans[i] : "Invalid";
}
uint16_t mt_c64_sid_address_value(unsigned i) {
    return i > 0 && i <= 48 ? (uint16_t)(0xd000u | ((unsigned)offset(i) << 4)) : 0;
}
void mt_c64_sid_merge(mt_c64_sid_model_t *d, const mt_c64_sid_model_t *s, uint64_t fields) {
    if (!d || !s)
        return;
    mt_c64_sid_model_t copy = *s;
    for (unsigned i = 0; i < 64; ++i)
        if (fields & (UINT64_C(1) << i))
            *field(d, i) = *field(&copy, i);
}
void mt_c64_sid_effective(mt_c64_sid_model_t *r, const mt_c64_sid_model_t *stored,
                          const mt_c64_sid_model_t *overlay, uint64_t fields, uint8_t classes) {
    if (!r || !stored || !overlay)
        return;
    *r = *stored;
    mt_c64_sid_merge(r, overlay, fields);
    for (unsigned n = 0; n < 8; ++n) {
        unsigned b = 25 + 4 * n;
        if ((classes & 1) && !(fields & (UINT64_C(1) << b)))
            r->sid_address[n] = 255;
        if ((classes & 4) && !(fields & (UINT64_C(1) << (b + 1))))
            r->sid_model[n] = 255;
        if ((classes & 2) && !(fields & (UINT64_C(1) << (b + 2))))
            r->sid_volume[n] = 255;
        if ((classes & 2) && !(fields & (UINT64_C(1) << (b + 3))))
            r->sid_pan[n] = 255;
    }
}
static unsigned gain(unsigned v, unsigned master) {
    unsigned g = ((unsigned)volume_gain[v] * (unsigned)volume_gain[master] + 64U) >> 7;
    return g > 255 ? 255 : g;
}
static uint8_t part_bits(unsigned p, unsigned bits) {
    unsigned out = 0, n = 0;
    for (unsigned b = 1; b < 6; ++b)
        if (bits & (1u << b)) {
            if (p & (1u << n))
                out |= 1u << b;
            ++n;
        }
    return (uint8_t)out;
}
bool mt_c64_sid_build(const mt_c64_sid_model_t *m, unsigned count, bool allow8580,
                      mt_c64_sid_table_t *t) {
    if (!t || !mt_c64_sid_valid(m) || (count != 2 && count != 4 && count != 8))
        return false;
    memset(t, 0, sizeof(*t));
    t->legacy_mode = 255;
    t->socket_chip[0] = t->socket_chip[1] = 255;
    uint8_t base[10], mask[10], original[8], socket_for[8];
    memset(socket_for, 255, sizeof(socket_for));
    unsigned bits = split_bits[m->split], parts = m->split == 0 ? 1 : m->split < 5 ? 2 : 4;
    for (unsigned n = 0; n < 8; ++n) {
        unsigned g = n & 1, p = n / 2;
        mask[n] = 0xfe;
        base[n] = m->address[g] && p < parts
                      ? (uint8_t)((offset(m->address[g]) & ~bits) | part_bits(p, bits))
                      : 1;
        if (m->sid_address[n] != 255)
            base[n] = offset(m->sid_address[n]);
        original[n] = base[n];
    }
    unsigned ext = m->ext_split ? 1u << m->ext_split : 0;
    for (unsigned g = 0; g < 2; ++g) {
        base[8 + g] = (uint8_t)(offset(m->socket_address[g]) & ~ext);
        mask[8 + g] = (uint8_t)(0xfe & ~ext);
    }
    if (m->mirror)
        for (unsigned b = 1; b < 6; ++b) {
            unsigned bit = 1u << b;
            int found = -1;
            bool common = true;
            for (unsigned n = 0; n < 10; ++n)
                if (base[n] >= 0x40 && base[n] < 0x80) {
                    int v = (base[n] & bit) ? 1 : 0;
                    if (!(mask[n] & bit))
                        common = false;
                    if (found < 0)
                        found = v;
                    else if (found != v)
                        common = false;
                }
            if (found >= 0 && common)
                for (unsigned n = 0; n < 10; ++n)
                    if (base[n] >= 0x40 && base[n] < 0x80) {
                        base[n] &= (uint8_t)~bit;
                        mask[n] &= (uint8_t)~bit;
                    }
        }
    for (unsigned n = count; n < 8; ++n)
        if (original[n] != 1)
            ++t->dropped;
    for (unsigned g = 0; g < 2; ++g)
        t->group_base[g] =
            original[g] == 1 ? 0 : (uint16_t)(0xd000u | ((unsigned)original[g] << 4));
    /* A socket consumes an otherwise unused physical SID. It does not silently
       replace an UltiSID part when all eight engines are assigned. */
    for (unsigned g = 0; g < 2; ++g)
        if (m->socket_enable[g] && m->socket_address[g]) {
            unsigned n;
            for (n = 0; n < count; ++n)
                if (original[n] == 1)
                    break;
            if (n == count) {
                ++t->dropped;
                continue;
            }
            base[n] = base[8 + g];
            mask[n] = mask[8 + g];
            original[n] = offset(m->socket_address[g]);
            socket_for[n] = (uint8_t)g;
            t->socket_chip[g] = (uint8_t)n;
        }
    unsigned fallback = 0;
    bool have = false;
    for (unsigned n = 0; n < count; ++n) {
        if (original[n] != 1) {
            t->base[n] = (uint16_t)(0xd000u | ((unsigned)original[n] << 4));
            ++t->mapped;
        }
        if (!have && original[n] >= 0x40 && original[n] < 0x80) {
            fallback = n;
            have = true;
        }
    }
    unsigned slots[8] = {0};
    for (unsigned s = 0; s < 48; ++s) {
        unsigned a = s < 32 ? 0x40 + 2 * s : 0xe0 + 2 * (s - 32);
        uint32_t v = 0;
        for (unsigned n = 0; n < count; ++n)
            if (original[n] != 1 && (a & mask[n]) == base[n]) {
                v |= 1u << n;
                ++slots[n];
            }
        t->words[s] = v ? v : s < 32 ? (0x100u | (1u << fallback)) : 0;
    }
    bool groups[2] = {false, false};
    for (unsigned n = 0; n < count; ++n)
        if (slots[n] && socket_for[n] == 255)
            groups[n & 1] = true;
    unsigned total_l = 0, total_r = 0;
    for (unsigned n = 0; n < count; ++n) {
        unsigned g = n & 1, sock = socket_for[n];
        unsigned v = m->sid_volume[n] == 255 ? (sock == 255 ? m->volume[g] : m->socket_volume[sock])
                                             : m->sid_volume[n];
        unsigned p =
            m->sid_pan[n] == 255 ? (sock == 255 ? m->pan[g] : m->socket_pan[sock]) : m->sid_pan[n];
        if (sock == 255 && m->sid_pan[n] == 255 && m->pan_from_preset[g] && !groups[g ^ 1])
            p = 5;
        unsigned model = m->sid_model[n] == 255 ? m->model[g] : m->sid_model[n];
        if (model && !allow8580) {
            model = 0;
            ++t->unsupported_models;
        }
        unsigned raw = slots[n] ? gain(v, m->master) : 0, l = (pan_gain[10 - p] * raw) >> 8,
                 r = (pan_gain[p] * raw) >> 8;
        t->words[48 + n] =
            l | (r << 8) | (model << 16) | ((model && m->digi[g] != 0) ? 1u << 17 : 0);
        total_l += l;
        total_r += r;
    }
    unsigned total = total_l > total_r ? total_l : total_r;
    unsigned h = m->headroom ? m->headroom - 1 : total <= 192 ? 0 : total <= 384 ? 1 : 2;
    unsigned ua = gain(24, m->master);
    bool dc = m->dc_remove == 1 || (m->dc_remove == 0 && t->mapped >= 3);
    t->words[56] =
        ua | (ua << 8) | ((uint32_t)m->resample << 16) | ((uint32_t)dc << 17) | (h << 18);
    bool canonical = t->words[56] == 0x8080 && t->words[48] == 0x80 && t->words[49] == 0x8000;
    for (unsigned n = 2; n < 8; ++n)
        if (t->words[48 + n])
            canonical = false;
    for (unsigned s = 32; s < 48; ++s)
        if (t->words[s])
            canonical = false;
    if (canonical)
        for (unsigned mode = 0; mode < 3; ++mode) {
            bool match = true;
            for (unsigned s = 0; s < 32; ++s) {
                unsigned want = mode == 0 ? 3 : mode == 1 ? (s & 1 ? 2 : 1) : (s & 8 ? 2 : 1);
                if (t->words[s] != want)
                    match = false;
            }
            if (match)
                t->legacy_mode = (uint8_t)(mode == 0 ? 0 : mode == 1 ? 2 : 3);
        }
    if (t->mapped == 1 && t->words[56] == 0x8080 && m->master == 24) {
        for (unsigned n = 0; n < count; ++n)
            if (slots[n] == 32 && socket_for[n] == 255 && m->pan_from_preset[n & 1] &&
                m->sid_pan[n] == 255 &&
                (m->sid_volume[n] == 24 || (m->sid_volume[n] == 255 && m->volume[n & 1] == 24)) &&
                !(t->words[48 + n] & 0x10000))
                t->legacy_mode = 0;
    }
    return true;
}

static void description(unsigned f, char *section, char *key) {
    strcpy(section, "SID Addressing");
    if (f < 2)
        sprintf(key, "UltiSID %u Address", f + 1);
    else if (f == 2)
        strcpy(key, "UltiSID Range Split");
    else if (f == 3)
        strcpy(key, "Auto Address Mirroring");
    else if (f < 6)
        sprintf(key, "SID Socket %u Address", f - 3);
    else if (f < 8) {
        strcpy(section, "SID Sockets Configuration");
        sprintf(key, "SID Socket %u", f - 5);
    } else if (f == 8)
        strcpy(key, "Ext DualSID Range Split");
    else if (f == 9)
        strcpy(key, "Paddle Override");
    else if (f < 18) {
        const char *suffix = f < 12   ? "Combined Waveforms"
                             : f < 14 ? "Digis Level"
                             : f < 16 ? "Filter Curve"
                                      : "Filter Resonance";
        strcpy(section, "UltiSID Configuration");
        sprintf(key, "UltiSID %u %s", (f & 1) + 1, suffix);
    } else if (f < 22) {
        strcpy(section, "Audio Mixer");
        sprintf(key, "%s UltiSID %u", f < 20 ? "Vol" : "Pan", (f & 1) + 1);
    } else if (f < 24) {
        strcpy(section, "Multitini SID");
        sprintf(key, "UltiSID %u Pan From Preset", f - 21);
    } else if (f == 24) {
        strcpy(section, "Audio Mixer");
        strcpy(key, "Vol Master");
    } else if (f < 57) {
        static const char *const suffix[4] = {"Address", "Model", "Volume", "Pan"};
        strcpy(section, "Multitini SID");
        sprintf(key, "SID %u %s", (f - 25) / 4 + 1, suffix[(f - 25) % 4]);
    } else if (f < 60) {
        strcpy(section, "Multitini SID");
        strcpy(key, f == 57 ? "SID Headroom" : f == 58 ? "SID DC Removal" : "SID Resampling");
    } else {
        strcpy(section, "Audio Mixer");
        sprintf(key, "%s Socket %u", f < 62 ? "Vol" : "Pan", (f & 1) + 1);
    }
}
static int parse_field(unsigned f, const char *v) {
    static const char *const headroom[] = {"Auto", "0 dB", "-6 dB", "-12 dB"};
    static const char *const dc[] = {"Auto", "On", "Off"};
    static const char *const resample[] = {"Point", "Average"};
    static const char *const external[] = {"Off", "A5", "A6", "A7", "A8", "A9"};
    static const char *const resonance[] = {"Low", "High"};
    if (f < 2 || (f >= 4 && f < 6))
        return address(v);
    if (f == 2)
        return choice(v, splits, 8);
    if (f == 8)
        return choice(v, external, 6);
    if (f == 3 || (f >= 6 && f < 8) || f == 9 || (f >= 22 && f < 24)) {
        if (same(v, "Enabled") || same(v, "On") || same(v, "Yes") || same(v, "1"))
            return 1;
        if (same(v, "Disabled") || same(v, "Off") || same(v, "No") || same(v, "0"))
            return 0;
        return -1;
    }
    if (f < 12)
        return choice(v, models, 2);
    if (f < 14)
        return choice(v, digis, 4);
    if (f < 16)
        return choice(v, curves, 7);
    if (f < 18)
        return choice(v, resonance, 2);
    if (f < 20 || f == 24 || (f >= 60 && f < 62))
        return choice(v, volumes, 31);
    if (f < 22 || f >= 62)
        return choice(v, pans, 11);
    if (f < 57) {
        if (same(v, "Follow"))
            return 255;
        switch ((f - 25) % 4) {
        case 0:
            return address(v);
        case 1:
            return choice(v, models, 2);
        case 2:
            return choice(v, volumes, 31);
        default:
            return choice(v, pans, 11);
        }
    }
    if (f == 57)
        return choice(v, headroom, 4);
    if (f == 58)
        return choice(v, dc, 3);
    return choice(v, resample, 2);
}
mt_c64_sid_key_result_t mt_c64_sid_apply_key(mt_c64_sid_model_t *m, const char *section,
                                             const char *key, const char *v, uint64_t *fields,
                                             uint8_t *classes) {
    if (!m || !section || !key || !v)
        return MT_SID_BAD_VALUE;
    if (same(section, "Multitini") && same(key, "SID audio")) {
        int p = choice(v, presets, 7);
        if (same(v, "Tuneful Eight"))
            p = MT_SID_EIGHT;
        if (p < 1)
            return MT_SID_BAD_VALUE;
        mt_c64_sid_preset(m, (unsigned)p);
        if (fields)
            *fields = UINT64_MAX;
        if (classes)
            *classes |= 7;
        return MT_SID_APPLIED;
    }
    char expected_section[40], expected_key[64];
    for (unsigned f = 0; f < 64; ++f) {
        description(f, expected_section, expected_key);
        if (!same(section, expected_section) || !same(key, expected_key))
            continue;
        int value = parse_field(f, v);
        if (value < 0)
            return MT_SID_BAD_VALUE;
        *field(m, f) = (uint8_t)value;
        if (fields)
            *fields |= UINT64_C(1) << f;
        if (f == 20 || f == 21) {
            m->pan_from_preset[f - 20] = 0;
            if (fields)
                *fields |= UINT64_C(1) << (f + 2);
        }
        if (classes) {
            if (f < 9)
                *classes |= 1;
            if (f >= 18 && f < 22)
                *classes |= 2;
            if (f == 10 || f == 11)
                *classes |= 4;
        }
        return MT_SID_APPLIED;
    }
    return MT_SID_UNKNOWN;
}
static void format_field(unsigned f, unsigned v, char *out) {
    if (f < 2 || (f >= 4 && f < 6) || (f >= 25 && f < 57 && (f - 25) % 4 == 0)) {
        if (v == 255)
            strcpy(out, "Follow");
        else if (!v)
            strcpy(out, "Unmapped");
        else
            sprintf(out, "$%04X", 0xd000u | ((unsigned)offset(v) << 4));
        return;
    }
    if (f >= 25 && f < 57) {
        if (v == 255) {
            strcpy(out, "Follow");
            return;
        }
        unsigned kind = (f - 25) % 4;
        strcpy(out, kind == 1 ? models[v] : kind == 2 ? volumes[v] : pans[v]);
        return;
    }
    if (f == 2) {
        strcpy(out, splits[v]);
        return;
    }
    if (f == 8) {
        if (v)
            sprintf(out, "A%u", v + 4);
        else
            strcpy(out, "Off");
        return;
    }
    if (f == 3 || (f >= 6 && f < 8) || f == 9 || (f >= 22 && f < 24)) {
        strcpy(out, v ? "Enabled" : "Disabled");
        return;
    }
    if (f < 12) {
        strcpy(out, models[v]);
        return;
    }
    if (f < 14) {
        strcpy(out, digis[v]);
        return;
    }
    if (f < 16) {
        strcpy(out, curves[v]);
        return;
    }
    if (f < 18) {
        strcpy(out, v ? "High" : "Low");
        return;
    }
    if (f < 20 || f == 24 || (f >= 60 && f < 62)) {
        strcpy(out, volumes[v]);
        return;
    }
    if (f < 22 || f >= 62) {
        strcpy(out, pans[v]);
        return;
    }
    if (f == 57) {
        static const char *const labels[] = {"Auto", "0 dB", "-6 dB", "-12 dB"};
        strcpy(out, labels[v]);
        return;
    }
    if (f == 58) {
        static const char *const labels[] = {"Auto", "On", "Off"};
        strcpy(out, labels[v]);
        return;
    }
    strcpy(out, v ? "Average" : "Point");
}
bool mt_c64_sid_export(const mt_c64_sid_model_t *m, mt_c64_sid_emit_fn emit, void *context) {
    if (!emit || !mt_c64_sid_valid(m))
        return false;
    mt_c64_sid_model_t copy = *m;
    char section[40], key[64], value[40];
    for (unsigned f = 0; f < 64; ++f) {
        description(f, section, key);
        format_field(f, *field(&copy, f), value);
        if (!emit(context, section, key, value))
            return false;
    }
    return true;
}
bool mt_c64_sid_encode(const mt_c64_sid_model_t *m, const char *id, uint8_t record[128]) {
    if (!record || !id || !mt_c64_sid_valid(m) || strlen(id) > 23)
        return false;
    mt_c64_sid_table_t t;
    if (!mt_c64_sid_build(m, 8, true, &t))
        return false;
    memset(record, 0, 128);
    memcpy(record, id, strlen(id));
    record[24] = t.legacy_mode != 255  ? t.legacy_mode
                 : t.mapped == 1       ? 0
                 : t.base[1] == 0xd420 ? 2
                                       : 3;
    record[25] = (uint8_t)((t.legacy_mode == 255 ? 1 : 0) | (m->mirror << 1) |
                           (m->resample << 2) | (m->pan_from_preset[0] << 3) |
                           (m->pan_from_preset[1] << 4));
    for (unsigned p = 1; p < 7; ++p) {
        mt_c64_sid_model_t candidate;
        mt_c64_sid_preset(&candidate, p);
        if (!memcmp(m, &candidate, sizeof(*m)))
            record[26] = (uint8_t)p;
    }
    record[27] = m->split;
    memcpy(record + 28, m->address, 2);
    memcpy(record + 30, m->socket_address, 2);
    record[32] = (uint8_t)(m->socket_enable[0] | (m->socket_enable[1] << 1));
    record[33] = m->ext_split;
    record[34] = m->paddle_override;
    record[35] = m->master;
    record[36] = m->headroom;
    record[37] = m->dc_remove;
    memcpy(record + 38, m->model, 2);
    memcpy(record + 40, m->digi, 2);
    memcpy(record + 42, m->curve, 2);
    memcpy(record + 44, m->resonance, 2);
    memcpy(record + 46, m->volume, 2);
    memcpy(record + 48, m->pan, 2);
    memcpy(record + 50, m->socket_volume, 2);
    memcpy(record + 52, m->socket_pan, 2);
    for (unsigned n = 0; n < 8; ++n) {
        record[56 + 5 * n] = m->sid_address[n];
        record[57 + 5 * n] = m->sid_model[n];
        record[58 + 5 * n] = m->sid_volume[n];
        record[59 + 5 * n] = m->sid_pan[n];
    }
    return true;
}
bool mt_c64_sid_decode(mt_c64_sid_model_t *m, char id[24], const uint8_t r[128]) {
    if (!m || !id || !r || !memchr(r, 0, 24) || (r[24] != 0 && r[24] != 2 && r[24] != 3) ||
        (r[25] & 0xe0) || r[26] > 6 || (r[32] & 0xfc) || r[54] || r[55])
        return false;
    for (unsigned n = 96; n < 128; ++n)
        if (r[n])
            return false;
    for (unsigned n = 0; n < 8; ++n)
        if (r[60 + 5 * n])
            return false;
    mt_c64_sid_model_t candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.mirror = (r[25] >> 1) & 1;
    candidate.resample = (r[25] >> 2) & 1;
    candidate.pan_from_preset[0] = (r[25] >> 3) & 1;
    candidate.pan_from_preset[1] = (r[25] >> 4) & 1;
    candidate.split = r[27];
    memcpy(candidate.address, r + 28, 2);
    memcpy(candidate.socket_address, r + 30, 2);
    candidate.socket_enable[0] = r[32] & 1;
    candidate.socket_enable[1] = (r[32] >> 1) & 1;
    candidate.ext_split = r[33];
    candidate.paddle_override = r[34];
    candidate.master = r[35];
    candidate.headroom = r[36];
    candidate.dc_remove = r[37];
    memcpy(candidate.model, r + 38, 2);
    memcpy(candidate.digi, r + 40, 2);
    memcpy(candidate.curve, r + 42, 2);
    memcpy(candidate.resonance, r + 44, 2);
    memcpy(candidate.volume, r + 46, 2);
    memcpy(candidate.pan, r + 48, 2);
    memcpy(candidate.socket_volume, r + 50, 2);
    memcpy(candidate.socket_pan, r + 52, 2);
    for (unsigned n = 0; n < 8; ++n) {
        candidate.sid_address[n] = r[56 + 5 * n];
        candidate.sid_model[n] = r[57 + 5 * n];
        candidate.sid_volume[n] = r[58 + 5 * n];
        candidate.sid_pan[n] = r[59 + 5 * n];
    }
    if (!mt_c64_sid_valid(&candidate))
        return false;
    *m = candidate;
    memcpy(id, r, 24);
    return true;
}
size_t mt_c64_sid_hwinfo(const mt_c64_sid_table_t *t, unsigned command, uint8_t *out, size_t cap) {
    if (!t || !out || (command != 1 && command != 2))
        return 0;
    if (t->legacy_mode != 255) {
        if (cap < 11)
            return 0;
        const unsigned second = t->legacy_mode == 0   ? 0xd400
                                : t->legacy_mode == 2 ? 0xd420
                                                      : 0xd500;
        const uint8_t legacy[11] = {2, 0, 0xd4, 0, 0, 0x83, (uint8_t)second, (uint8_t)(second >> 8),
                                    0, 0, 0x83};
        memcpy(out, legacy, 11);
        return 11;
    }
    uint8_t chips[8];
    unsigned count = 0;
    if (command == 1) {
        chips[count++] = 0;
        chips[count++] = 1;
        for (unsigned g = 0; g < 2; ++g)
            if (t->socket_chip[g] != 255)
                chips[count++] = t->socket_chip[g];
    } else
        for (unsigned n = 0; n < 8; ++n)
            if (t->base[n])
                chips[count++] = (uint8_t)n;
    if (cap < 1 + count * 5)
        return 0;
    out[0] = (uint8_t)count;
    for (unsigned f = 0; f < count; ++f) {
        unsigned n = chips[f], slots = 0;
        for (unsigned s = 0; s < 48; ++s)
            if (t->words[s] & (1u << n))
                ++slots;
        unsigned type = slots > 1 ? 3 : slots ? 2 : 0;
        for (unsigned g = 0; g < 2; ++g)
            if (t->socket_chip[g] == n)
                type = 4 + g;
        unsigned base = command == 1 && f < 2 ? t->group_base[f] : t->base[n];
        if (!base)
            type = 0;
        out[1 + 5 * f] = (uint8_t)base;
        out[2 + 5 * f] = (uint8_t)(base >> 8);
        out[3 + 5 * f] = 0;
        out[4 + 5 * f] = 0;
        out[5 + 5 * f] = (uint8_t)(0x80 | type);
    }
    return 1 + count * 5;
}
