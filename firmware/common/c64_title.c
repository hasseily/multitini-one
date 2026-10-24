/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_title.h"
#include "multitini_media_browser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool same(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return false;
    }
    return *a == *b;
}
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
    return s;
}
static const char *section_name(const char *s)
{
    if (same(s, "C64U Specific Settings"))
        return "U64 Specific Settings";
    if (same(s, "Cartridge and ROM Settings"))
        return "C64 and Cartridge Settings";
    return s;
}
static bool known_section(const char *s)
{
    return same(s, "U64 Specific Settings") || same(s, "C64 and Cartridge Settings") ||
           same(s, "SID Addressing") || same(s, "SID Sockets Configuration") ||
           same(s, "UltiSID Configuration") || same(s, "Audio Mixer") || same(s, "Multitini") ||
           same(s, "Multitini SID");
}
static int onoff(const char *s)
{
    if (same(s, "Enabled") || same(s, "On"))
        return 1;
    if (same(s, "Disabled") || same(s, "Off"))
        return 0;
    return -1;
}
static void note(mt_c64_title_report_t *r, unsigned line, const char *key, const char *why)
{
    if (r->entries < MT_C64_CFG_REPORTS)
        (void)snprintf(r->message[r->entries++], 96, "Line %u: %.40s: %.35s", line, key, why);
}
const char *mt_c64_title_io_conflict(const mt_c64_title_values_t *v,
                                     const mt_c64_capabilities_t *caps, bool cartridge)
{
    mt_c64_sid_table_t table;
    unsigned count =
        caps->expansion & MT_C64_EXPANSION_CAPS_MULTI_SID ? ((caps->expansion >> 8) & 7U) + 1U : 2U;
    if (!mt_c64_sid_build(&v->sid, count,
                          (caps->expansion & MT_C64_EXPANSION_CAPS_SID_MODEL_8580) != 0, &table))
        return "Invalid SID map";
    for (unsigned slot = 32; slot < 48; ++slot) {
        if (!table.words[slot])
            continue;
        if (!(caps->expansion & MT_C64_EXPANSION_CAPS_SID_IO_SLOTS))
            return "I/O SID mapping needs a newer C64 image";
        if (cartridge)
            return "Remove I/O SID mappings before attaching a cartridge";
        if (slot >= 40 && v->reu)
            return "Disable REU before mapping a SID in IO2";
        if (slot >= 41 && v->ua)
            return "Disable Ultimate Audio before mapping a SID in its range";
        if (slot == 40 && v->uci)
            return "Disable Command Interface before mapping a SID at $DF00";
    }
    return NULL;
}
void mt_c64_title_defaults(mt_c64_title_values_t *v)
{
    memset(v, 0, sizeof(*v));
    v->detail = (uint8_t)(0x41);
    v->sid_mode = 3;
    v->joystick = 2;
    v->reu = true;
    mt_c64_sid_preset(&v->sid, MT_SID_DUAL500);
}
static void overlay_fields(mt_c64_title_values_t *v, const mt_c64_title_overlay_t *o)
{
    const mt_c64_title_values_t *n = &o->values;
    uint32_t m = o->owned;
    if (m & MT_TITLE_TURBO) {
        v->turbo = n->turbo;
        v->detail = (uint8_t)((v->detail & 0x3f) | (n->detail & 0xc0));
    }
    if (m & MT_TITLE_SPEED)
        v->detail = (uint8_t)((v->detail & 0xf0) | (n->detail & 15));
    if (m & MT_TITLE_BADLINE)
        v->detail = (uint8_t)((v->detail & ~0x20U) | (n->detail & 0x20));
    if (m & MT_TITLE_FOLLOW)
        v->detail = (uint8_t)((v->detail & ~0x10U) | (n->detail & 0x10));
    if (m & MT_TITLE_AUTOSLOW)
        v->flags = (uint8_t)((v->flags & ~1U) | (n->flags & 1));
    if (m & MT_TITLE_CART)
        v->flags = (uint8_t)((v->flags & ~2U) | (n->flags & 2));
    if (m & MT_TITLE_SCPU)
        v->flags = (uint8_t)((v->flags & ~8U) | (n->flags & 8));
    if (m & MT_TITLE_MEMORY) {
        v->reu = n->reu;
        v->ua = n->ua;
    }
    if (m & MT_TITLE_UCI)
        v->uci = n->uci;
    if (m & MT_TITLE_JOYSTICK)
        v->joystick = n->joystick;
    if (m & MT_TITLE_SID) {
        mt_c64_sid_model_t result;
        mt_c64_sid_effective(&result, &v->sid, &n->sid, o->sid_fields, o->sid_classes);
        v->sid = result;
        v->sid_custom = n->sid_custom;
        v->sid_mode = n->sid_mode;
    }
}
void mt_c64_title_effective(mt_c64_title_values_t *requested, mt_c64_title_values_t *applied,
                            const mt_c64_title_values_t *stored,
                            const mt_c64_title_overlay_t *overlay, bool cartridge)
{
    *requested = *stored;
    requested->detail = mt_c64_turbo_detail(requested->detail);
    if (overlay)
        overlay_fields(requested, overlay);
    if (requested->ua)
        requested->reu = true;
    *applied = *requested;
    if (cartridge) {
        applied->reu = false;
        applied->ua = false;
    }
}
void mt_c64_title_merge(mt_c64_title_overlay_t *d, const mt_c64_title_overlay_t *s)
{
    const mt_c64_sid_model_t previous_sid = d->values.sid;
    overlay_fields(&d->values, s);
    if (s->owned & MT_TITLE_SID) {
        d->values.sid = previous_sid;
        mt_c64_sid_merge(&d->values.sid, &s->values.sid, s->sid_fields);
    }
    d->owned |= s->owned;
    d->sid_fields |= s->sid_fields;
    d->sid_classes |= s->sid_classes;
    if (s->source[0])
        (void)snprintf(d->source, sizeof(d->source), "%s", s->source);
    if (d->origin == MT_TITLE_NONE)
        d->origin = s->origin;
}
bool mt_c64_title_sidecar(char *out, size_t capacity, const char *relative, const char *ext)
{
    if (!out || !relative || !ext || relative[0] == '/' || strchr(relative, ':') ||
        strchr(relative, '\\') || strlen(relative) > 255 || strlen(ext) != 4 || ext[0] != '.')
        return false;
    const char *part = relative;
    while (*part) {
        const char *slash = strchr(part, '/');
        size_t n = slash ? (size_t)(slash - part) : strlen(part);
        char component[256];
        if (!n || n >= sizeof(component))
            return false;
        memcpy(component, part, n);
        component[n] = 0;
        if (!mt_media_browser_valid_component(component))
            return false;
        if (!slash)
            break;
        part = slash + 1;
        if (!*part)
            return false;
    }
    const char *last = strrchr(relative, '/');
    last = last ? last + 1 : relative;
    const char *dot = strrchr(last, '.');
    if (dot == last)
        dot = NULL;
    size_t n = dot ? (size_t)(dot - relative) : strlen(relative);
    if (n + 5 > capacity || n + 4 > 255)
        return false;
    memcpy(out, relative, n);
    memcpy(out + n, ext, 5);
    return true;
}

static uint32_t simple_key(const char *section, const char *key)
{
    if (same(section, "U64 Specific Settings")) {
        if (same(key, "Turbo Control"))
            return MT_TITLE_TURBO;
        if (same(key, "CPU Speed"))
            return MT_TITLE_SPEED;
        if (same(key, "Badline Timing"))
            return MT_TITLE_BADLINE;
        if (same(key, "SuperCPU Detect (D0BC)") || same(key, "SuperCPU Detect ($D0BC)"))
            return MT_TITLE_SCPU;
        if (same(key, "Joystick Swapper"))
            return MT_TITLE_JOYSTICK;
    }
    if (same(section, "C64 and Cartridge Settings")) {
        if (same(key, "RAM Expansion Unit") || same(key, "Map Ultimate Audio $DF20-DFFF"))
            return MT_TITLE_MEMORY;
        if (same(key, "Command Interface"))
            return MT_TITLE_UCI;
    }
    if (same(section, "Multitini")) {
        if (same(key, "Disk auto-slow"))
            return MT_TITLE_AUTOSLOW;
        if (same(key, "Cartridge turbo"))
            return MT_TITLE_CART;
        if (same(key, "Expansion memory speed"))
            return MT_TITLE_FOLLOW;
        if (same(key, "Joystick port"))
            return MT_TITLE_JOYSTICK;
        if (same(key, "SID audio"))
            return MT_TITLE_SID;
    }
    if (same(section, "SID Addressing") || same(section, "Multitini SID") ||
        same(section, "UltiSID Configuration") || same(section, "Audio Mixer") ||
        same(section, "SID Sockets Configuration"))
        return MT_TITLE_SID;
    return 0;
}
static void line_value(mt_c64_title_overlay_t *o, mt_c64_title_report_t *r,
                       const mt_c64_title_parse_context_t *c, const char *sec, const char *key,
                       const char *val, unsigned line, int *reu, int *ua)
{
    uint32_t g = simple_key(sec, key);
    int b = onoff(val);
    bool valid = true, available = true, adjusted = false;
    const mt_c64_capabilities_t *caps = c->caps;
    mt_c64_title_values_t *v = &o->values;
    const mt_c64_title_values_t before = *v;
    const uint64_t before_fields = o->sid_fields;
    const uint8_t before_classes = o->sid_classes;
    if (!g) {
        if (same(sec, "U64 Specific Settings") && same(key, "System Mode")) {
            if (!(same(val, "PAL") || same(val, "NTSC")) || same(val, "NTSC") != c->ntsc) {
                ++r->skipped;
                note(r, line, key, "different video image required");
            }
        } else if (same(sec, "C64 and Cartridge Settings") && same(key, "REU Size")) {
            if (!same(val, "128 KB") && !same(val, "256 KB") && !same(val, "512 KB") &&
                !same(val, "1 MB") && !same(val, "2 MB") && !same(val, "4 MB") &&
                !same(val, "8 MB") && !same(val, "16 MB")) {
                ++r->errors;
                ++r->skipped;
                note(r, line, key, "invalid REU size");
            } else if (!same(val, "16 MB")) {
                ++r->adjusted;
                note(r, line, key, "hardware remains 16 MB");
            }
        } else if (same(sec, "C64 and Cartridge Settings") &&
                   (strstr(key, "ROM") || strstr(key, "Cartridge") || strstr(key, "REU Preload") ||
                    strstr(key, "Bus ") || strstr(key, "DMA "))) { /* content paths never applied */
        } else {
            ++r->skipped;
            note(r, line, key, "unknown item; ignored");
        }
        return;
    }
    r->mentioned |= g;
    if (g == MT_TITLE_SID) {
        uint64_t fields = 0;
        uint8_t classes = 0;
        const mt_c64_sid_key_result_t result =
            mt_c64_sid_apply_key(&v->sid, sec, key, val, &fields, &classes);
        if (result == MT_SID_UNKNOWN || result == MT_SID_UNSUPPORTED) {
            ++r->skipped;
            r->unavailable |= g;
            note(r, line, key,
                 result == MT_SID_UNKNOWN ? "unknown SID item"
                                          : "SID engine does not implement this");
            return;
        }
        valid = result == MT_SID_APPLIED;
        if (valid) {
            o->sid_fields |= fields;
            o->sid_classes |= classes;
            v->sid_custom = true;
            if (fields & ((UINT64_C(15) << 14) | (UINT64_C(1) << 9))) {
                ++r->adjusted;
                note(r, line, key, "stored; engine behavior unchanged");
            }
        }
    } else if (g == MT_TITLE_TURBO) {
        if (same(val, "Off"))
            v->turbo = false;
        else {
            unsigned mode =
                same(val, "Manual")                                                       ? 1
                : (same(val, "U64 Turbo Registers") || same(val, "C64U Turbo Registers")) ? 2
                : same(val, "TurboEnable Bit")                                            ? 3
                                                                                          : 0;
            valid = mode != 0;
            v->turbo = true;
            if (mode == 2 && !(caps->cpu & MT_C64_CPU_CAPS_REGISTERS)) {
                mode = 1;
                adjusted = true;
            }
            if (mode == 3 && !(caps->cpu & MT_C64_CPU_CAPS_TURBO_ENABLE)) {
                mode = 1;
                adjusted = true;
            }
            v->detail = (uint8_t)((v->detail & 0x3f) | (mode << 6));
        }
        available = caps->turbo;
        if (!available && !v->turbo && valid)
            return;
    } else if (g == MT_TITLE_SPEED) {
        char *end;
        unsigned long mhz = strtoul(val, &end, 10);
        while (*end == ' ')
            ++end;
        valid = end != val && (!*end || same(end, "MHz"));
        unsigned index = 16;
        for (unsigned i = 0; i < 16; ++i)
            if (mt_c64_speed_mhz(i) == mhz)
                index = i;
        if (mhz == 5) {
            index = 3;
            adjusted = true;
        }
        valid = valid && index < 16;
        if (valid) {
            unsigned limited = mt_c64_clamp_speed(index, (uint16_t)caps->cpu);
            adjusted |= limited != index;
            v->detail = (uint8_t)((v->detail & 0xf0) | limited);
        }
        available = caps->turbo;
    } else if (g == MT_TITLE_BADLINE) {
        valid = b >= 0;
        available = caps->turbo;
        v->detail = (uint8_t)((v->detail & ~0x20U) | (b == 0 ? 0x20 : 0));
    } else if (g == MT_TITLE_SCPU) {
        valid = b >= 0;
        available = (caps->cpu2 & MT_C64_CPU_CAPS2_SCPU_DETECT) != 0;
        v->flags = (uint8_t)((v->flags & ~8U) | (b == 1 ? 8 : 0));
        if (!available && b == 0)
            return;
    } else if (g == MT_TITLE_UCI) {
        valid = b >= 0;
        available = (caps->expansion & MT_C64_EXPANSION_CAPS_UCI) != 0;
        if (c->origin == MT_TITLE_UCI_FILE) {
            note(r, line, key, "interface remains enabled");
            return;
        }
        v->uci = b == 1;
        if (!available && b == 0)
            return;
    } else if (g == MT_TITLE_MEMORY) {
        valid = b >= 0;
        bool audio = same(key, "Map Ultimate Audio $DF20-DFFF");
        available =
            audio ? caps->ultimate_audio : (caps->expansion & MT_C64_EXPANSION_CAPS_REU_16MB) != 0;
        if (c->cartridge && b == 1) {
            ++r->skipped;
            if (c->strict)
                ++r->errors;
            note(r, line, key, "blocked by cartridge");
            r->unavailable |= g;
            return;
        }
        if (valid && available) {
            if (audio)
                *ua = b;
            else
                *reu = b;
        }
    } else if (g == MT_TITLE_JOYSTICK) {
        if (same(key, "Joystick port")) {
            valid = same(val, "1") || same(val, "2");
            v->joystick = same(val, "1") ? 1 : 2;
        } else {
            valid = same(val, "Normal") || same(val, "Swapped");
            v->joystick = same(val, "Swapped") ? 3 - c->base->joystick : c->base->joystick;
        }
    } else if (g == MT_TITLE_AUTOSLOW) {
        valid = b >= 0;
        available = caps->turbo;
        v->flags = (uint8_t)((v->flags & ~1U) | (b == 0 ? 1 : 0));
    } else if (g == MT_TITLE_CART) {
        valid = same(val, "Allowed") || same(val, "Forces 1 MHz");
        available = caps->turbo && (caps->cpu & MT_C64_CPU_CAPS_CARTRIDGE_TURBO);
        v->flags = (uint8_t)((v->flags & ~2U) | (same(val, "Forces 1 MHz") ? 2 : 0));
    } else if (g == MT_TITLE_FOLLOW) {
        valid = same(val, "Follow CPU") || same(val, "C64U (1 byte per cycle)");
        available = caps->turbo && (caps->cpu2 & MT_C64_CPU_CAPS2_FOLLOW_CPU);
        v->detail = (uint8_t)((v->detail & ~0x10U) | (same(val, "Follow CPU") ? 0x10 : 0));
    }
    if (!valid) {
        *v = before;
        o->sid_fields = before_fields;
        o->sid_classes = before_classes;
        ++r->errors;
        ++r->skipped;
        note(r, line, key, "invalid value");
        return;
    }
    if (!available) {
        *v = before;
        o->sid_fields = before_fields;
        o->sid_classes = before_classes;
        ++r->skipped;
        r->unavailable |= g;
        note(r, line, key, "not supported by this image");
        return;
    }
    o->owned |= g;
    ++r->applied;
    if (adjusted) {
        ++r->adjusted;
        note(r, line, key, "adjusted to image capabilities");
    }
}

bool mt_c64_title_parse(const uint8_t *data, size_t size, const mt_c64_title_parse_context_t *c,
                        mt_c64_title_overlay_t *o, mt_c64_title_report_t *r)
{
    if (!data || !c || !c->caps || !c->base || !o || !r)
        return false;
    memset(o, 0, sizeof(*o));
    memset(r, 0, sizeof(*r));
    o->values = *c->base;
    o->values.detail = mt_c64_turbo_detail(o->values.detail);
    o->origin = c->origin;
    if (size > MT_C64_CFG_BYTES) {
        r->errors = 1;
        note(r, 0, "file", "larger than 16 KiB");
        return false;
    }
    for (size_t i = 0; i < size; ++i)
        if ((data[i] < 32 && data[i] != '\t' && data[i] != '\r' && data[i] != '\n') ||
            data[i] == 127) {
            r->errors = 1;
            note(r, 0, "file", "not a text configuration");
            return false;
        }
    size_t p = size >= 3 && data[0] == 239 && data[1] == 187 && data[2] == 191 ? 3 : 0;
    char section[80] = "";
    unsigned lineno = 0;
    int reu = -1, ua = -1;
    while (p < size) {
        size_t start = p;
        while (p < size && data[p] != '\r' && data[p] != '\n')
            ++p;
        size_t n = p - start;
        if (p < size && data[p++] == '\r' && p < size && data[p] == '\n')
            ++p;
        ++lineno;
        if (n > MT_C64_CFG_LINE) {
            ++r->errors;
            ++r->skipped;
            note(r, lineno, "line", "longer than 319 bytes");
            if (data[start] == '[')
                section[0] = 0;
            continue;
        }
        char line[MT_C64_CFG_LINE + 1];
        memcpy(line, data + start, n);
        line[n] = 0;
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';')
            continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            section[0] = 0;
            if (!end) {
                ++r->errors;
                note(r, lineno, "header", "missing closing bracket");
                continue;
            }
            *end = 0;
            s = trim(s + 1);
            if (strlen(s) >= sizeof(section)) {
                ++r->errors;
                continue;
            }
            (void)snprintf(section, sizeof(section), "%s", section_name(s));
            if (known_section(section))
                ++r->known_sections;
            continue;
        }
        if (!known_section(section))
            continue;
        char *eq = strchr(s, '=');
        if (!eq || (size_t)(eq - s) > 40) {
            ++r->errors;
            ++r->skipped;
            note(r, lineno, "line", "missing or oversized item name");
            continue;
        }
        *eq = 0;
        line_value(o, r, c, section, trim(s), trim(eq + 1), lineno, &reu, &ua);
    }
    if (o->owned & MT_TITLE_MEMORY) {
        if (reu >= 0)
            o->values.reu = reu != 0;
        if (ua >= 0)
            o->values.ua = ua != 0;
        if (ua == 1)
            o->values.reu = true;
        else if (reu == 0)
            o->values.ua = false;
    }
    if (o->owned & MT_TITLE_SID) {
        mt_c64_sid_table_t table;
        unsigned count = (c->caps->expansion & MT_C64_EXPANSION_CAPS_MULTI_SID)
                             ? (((c->caps->expansion >> 8) & 7) + 1)
                             : 2;
        if (!mt_c64_sid_build(&o->values.sid, count,
                              (c->caps->expansion & MT_C64_EXPANSION_CAPS_SID_MODEL_8580) != 0,
                              &table)) {
            ++r->errors;
            o->owned &= ~(uint32_t)MT_TITLE_SID;
        } else if (!(c->caps->expansion & MT_C64_EXPANSION_CAPS_MULTI_SID) &&
                   table.legacy_mode == 255) {
            o->owned &= ~(uint32_t)MT_TITLE_SID;
            r->unavailable |= MT_TITLE_SID;
            ++r->skipped;
            note(r, 0, "SID layout", "requires multi-SID image");
        } else {
            bool io_unavailable = false;
            for (unsigned slot = 32; slot < 48; ++slot)
                io_unavailable |= table.words[slot] != 0 &&
                                  !(c->caps->expansion & MT_C64_EXPANSION_CAPS_SID_IO_SLOTS);
            if (io_unavailable) {
                o->owned &= ~(uint32_t)MT_TITLE_SID;
                r->unavailable |= MT_TITLE_SID;
                ++r->skipped;
                note(r, 0, "SID layout", "I/O SID slots unavailable");
            }
            o->values.sid_mode = table.legacy_mode == 255 ? 3 : table.legacy_mode;
            o->values.sid_custom = table.legacy_mode == 255;
            if (table.dropped || table.unsupported_models) {
                ++r->adjusted;
                note(r, 0, "SID layout", "image cannot reproduce all settings");
            }
        }
    }
    mt_c64_title_values_t requested, applied;
    mt_c64_title_effective(&requested, &applied, c->base, o, c->cartridge);
    const char *conflict = mt_c64_title_io_conflict(&applied, c->caps, c->cartridge);
    if (conflict) {
        ++r->errors;
        note(r, 0, "SID ownership", conflict);
        o->owned = 0;
        return false;
    }
    if (c->strict && r->errors) {
        o->owned = 0;
        return false;
    }
    return true;
}

typedef struct
{
    char section[40], key[41], value[64];
    bool used;
} cfg_emit_entry_t;
typedef struct
{
    cfg_emit_entry_t entry[96];
    size_t count;
    bool failed;
} cfg_emit_t;
/* CPU0 owns configuration I/O. This bounded scratch stays off the ARM stack. */
static cfg_emit_t emit_scratch;
static bool collect(void *context, const char *section, const char *key, const char *value)
{
    cfg_emit_t *e = context;
    if (e->count >= 96 || strlen(section) >= 40 || strlen(key) >= 41 || strlen(value) >= 64) {
        e->failed = true;
        return false;
    }
    cfg_emit_entry_t *n = &e->entry[e->count++];
    (void)snprintf(n->section, sizeof(n->section), "%s", section);
    (void)snprintf(n->key, sizeof(n->key), "%s", key);
    (void)snprintf(n->value, sizeof(n->value), "%s", value);
    n->used = false;
    return true;
}
static bool append(uint8_t *out, size_t cap, size_t *used, const void *src, size_t n)
{
    if (n > cap - *used)
        return false;
    memcpy(out + *used, src, n);
    *used += n;
    return true;
}
bool mt_c64_title_write(const uint8_t *existing, size_t size, const mt_c64_title_values_t *v,
                        const mt_c64_capabilities_t *caps, const mt_c64_title_report_t *parsed,
                        uint8_t *out, size_t cap, size_t *written)
{
    if (!v || !caps || !out || !written || (size && !existing) || size > MT_C64_CFG_BYTES ||
        (parsed && parsed->errors))
        return false;
    if (cap > MT_C64_CFG_BYTES)
        cap = MT_C64_CFG_BYTES;
    cfg_emit_t *e = &emit_scratch;
    memset(e, 0, sizeof(*e));
    const uint32_t blocked = parsed ? parsed->unavailable : 0;
    uint8_t detail = mt_c64_turbo_detail(v->detail);
    char number[32];
#define ADD(s, k, value, g)                                                                        \
    do {                                                                                           \
        if (!(blocked & (g)) && !collect(e, s, k, value))                                          \
            return false;                                                                          \
    } while (0)
    if (caps->turbo) {
        const char *mode = !v->turbo            ? "Off"
                           : (detail >> 6) == 2 ? "U64 Turbo Registers"
                           : (detail >> 6) == 3 ? "TurboEnable Bit"
                                                : "Manual";
        ADD("U64 Specific Settings", "Turbo Control", mode, MT_TITLE_TURBO);
        (void)snprintf(number, sizeof(number), "%2u", mt_c64_speed_mhz(detail & 15));
        ADD("U64 Specific Settings", "CPU Speed", number, MT_TITLE_SPEED);
        ADD("U64 Specific Settings", "Badline Timing", detail & 0x20 ? "Disabled" : "Enabled",
            MT_TITLE_BADLINE);
        ADD("Multitini", "Disk auto-slow", v->flags & 1 ? "Off" : "On", MT_TITLE_AUTOSLOW);
        if (caps->cpu & MT_C64_CPU_CAPS_CARTRIDGE_TURBO)
            ADD("Multitini", "Cartridge turbo", v->flags & 2 ? "Forces 1 MHz" : "Allowed",
                MT_TITLE_CART);
        if (caps->cpu2 & MT_C64_CPU_CAPS2_FOLLOW_CPU)
            ADD("Multitini", "Expansion memory speed",
                detail & 0x10 ? "Follow CPU" : "C64U (1 byte per cycle)", MT_TITLE_FOLLOW);
    }
    if (caps->cpu2 & MT_C64_CPU_CAPS2_SCPU_DETECT)
        ADD("U64 Specific Settings", "SuperCPU Detect (D0BC)",
            v->flags & 8 ? "Enabled" : "Disabled", MT_TITLE_SCPU);
    if (caps->expansion & MT_C64_EXPANSION_CAPS_REU_16MB)
        ADD("C64 and Cartridge Settings", "RAM Expansion Unit", v->reu ? "Enabled" : "Disabled",
            MT_TITLE_MEMORY);
    if (caps->ultimate_audio)
        ADD("C64 and Cartridge Settings", "Map Ultimate Audio $DF20-DFFF",
            v->ua ? "Enabled" : "Disabled", MT_TITLE_MEMORY);
    if (caps->expansion & MT_C64_EXPANSION_CAPS_UCI)
        ADD("C64 and Cartridge Settings", "Command Interface", v->uci ? "Enabled" : "Disabled",
            MT_TITLE_UCI);
    ADD("Multitini", "Joystick port", v->joystick == 1 ? "1" : "2", MT_TITLE_JOYSTICK);
    if (!(blocked & MT_TITLE_SID)) {
        if (caps->expansion & MT_C64_EXPANSION_CAPS_MULTI_SID) {
            if (!mt_c64_sid_export(&v->sid, collect, e))
                return false;
        } else {
            ADD("Multitini", "SID audio",
                v->sid_mode == 2   ? "Dual $D420"
                : v->sid_mode == 3 ? "Dual $D500"
                                   : "Single",
                MT_TITLE_SID);
        }
    }
#undef ADD
    if (e->failed)
        return false;
    size_t used = 0, p = 0;
    char section[80] = "";
    while (p < size) {
        size_t start = p;
        while (p < size && existing[p] != '\r' && existing[p] != '\n')
            ++p;
        size_t n = p - start;
        if (p < size && existing[p++] == '\r' && p < size && existing[p] == '\n')
            ++p;
        if (n > MT_C64_CFG_LINE)
            return false;
        char line[MT_C64_CFG_LINE + 1];
        memcpy(line, existing + start, n);
        line[n] = 0;
        char *s = trim(line);
        if (start == 0 && n >= 3 && (uint8_t)s[0] == 239 && (uint8_t)s[1] == 187 &&
            (uint8_t)s[2] == 191)
            s += 3;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end)
                return false;
            *end = 0;
            if (strlen(s + 1) >= sizeof(section))
                return false;
            (void)snprintf(section, sizeof(section), "%s", section_name(trim(s + 1)));
        }
        char *eq = (*s == '[' || *s == '#' || *s == ';') ? NULL : strchr(s, '=');
        cfg_emit_entry_t *replacement = NULL;
        size_t eq_position = eq ? (size_t)(eq - line) : 0;
        if (eq) {
            *eq = 0;
            const char *key = trim(s);
            for (size_t i = 0; i < e->count; ++i)
                if (same(section, e->entry[i].section) && same(key, e->entry[i].key)) {
                    replacement = &e->entry[i];
                    break;
                }
        }
        if (replacement) {
            if (!append(out, cap, &used, existing + start, eq_position + 1) ||
                !append(out, cap, &used, replacement->value, strlen(replacement->value)) ||
                !append(out, cap, &used, existing + start + n, p - start - n))
                return false;
            replacement->used = true;
        } else if (!append(out, cap, &used, existing + start, p - start))
            return false;
    }
    /* Append missing supported keys after the preserved document. A repeated
     * section is legal, and later explicit values win on both machines. */
    if (used && out[used - 1] != '\n' && out[used - 1] != '\r' && !append(out, cap, &used, "\n", 1))
        return false;
    section[0] = 0;
    for (size_t i = 0; i < e->count; ++i) {
        cfg_emit_entry_t *n = &e->entry[i];
        if (n->used)
            continue;
        char line[192];
        int length;
        if (!same(section, n->section)) {
            length = snprintf(line, sizeof(line), "\n[%s]\n", n->section);
            if (length < 0 || (size_t)length >= sizeof(line) ||
                !append(out, cap, &used, line, (size_t)length))
                return false;
            (void)snprintf(section, sizeof(section), "%s", n->section);
        }
        length = snprintf(line, sizeof(line), "%s=%s\n", n->key, n->value);
        if (length < 0 || (size_t)length >= sizeof(line) ||
            !append(out, cap, &used, line, (size_t)length))
            return false;
    }
    *written = used;
    return true;
}
