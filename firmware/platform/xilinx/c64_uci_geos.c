/* SPDX-License-Identifier: GPL-3.0-only */
/* Original bounded implementation of Ultimate's REU utility commands.
 * Wire/memory layout reference: GideonZ/1541ultimate
 * d43546fe25b26cb332f43f00219d33153dfe44f8 control_target.cc, dos.cc,
 * c64.cc and disk_image.cc. No upstream source is copied. */
#include "c64_uci_geos.h"
#include "c64_uci_internal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum { IDLE, HOLD, FILE_COPY, GCR_READ, GCR_SCAN, FENCE };
static struct {
    unsigned state, command, drive, track, max_sector, type;
    uint32_t address, destination, length, offset, scan, ones;
    unsigned decoded, records, sector;
    bool save, what_if, changed, pending, canceling, close_failed;
    uint8_t pairs[510], scratch[4096], *gcr;
    mt_sd_file_t *file;
    char path[C64_UCI_PATH];
    const char *failure;
} g;
static uint32_t le24(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static mt_uci_result_t answer(mt_uci_reply_t *r, const char *s) {
    mt_uci_reply_clear(r);
    mt_uci_reply_status(r, s);
    return MT_UCI_REPLY;
}
static uint8_t byte_at(uint32_t address) {
    uint8_t v;
    c64_uci_reu_read(address, &v, 1);
    return v;
}
static unsigned drive_info(unsigned d, uint32_t *base, uint32_t *size) {
    uint8_t signature[4];
    c64_uci_reu_read(0xbb0e, signature, 4);
    uint8_t type = signature[d], bank = byte_at(0x7dc7 + d);
    if (!memcmp(signature, "\x03\xa9\x06\x8d", 4)) {
        type = byte_at(0x798e + d);
        if (type > 0x83)
            type = 0;
    }
    *base = (uint32_t)bank << 16;
    *size = 0;
    if (bank > 0x40)
        return 0;
    switch (type) {
    case 0x81:
        *size = 174848;
        return 0x41;
    case 0x82:
        *size = 349696;
        return 0x71;
    case 0x83:
        *size = 819200;
        return 0x81;
    case 0x84:
    case 0xa4: {
        uint32_t pointer = byte_at(0xbafe + d) | ((uint32_t)byte_at(0xbb02 + d) << 8);
        *size = (uint32_t)byte_at(pointer + 0x84) << 16;
        return 0xdd;
    }
    default:
        return 0;
    }
}
static bool extension(const char *path, const char *expected) {
    const char *dot = strrchr(path, '.');
    if (!dot)
        return false;
    dot++;
    while (*dot && *expected)
        if (toupper((unsigned char)*dot++) != *expected++)
            return false;
    return !*dot && !*expected;
}
static void close_file(void) {
    if (g.file) {
        mt_sd_result_t s = mt_sd_file_close(g.file);
        if(s.status==MT_SD_OK || mt_sd_file_tell(g.file)<0)g.file=NULL;
        if (s.status != MT_SD_OK) {
            g.close_failed = true;
            if (!g.failure) g.failure = c64_uci_sd_status(s);
        }
    }
}
static mt_uci_result_t finish(mt_uci_reply_t *r) {
    close_file();
    if(g.close_failed){c64_uci_service_hold_fault();return MT_UCI_FAILED;}
    free(g.gcr);
    g.gcr = NULL;
    c64_uci_service_hold_end();
    g.state = IDLE;
    mt_uci_reply_clear(r);
    if (g.command == 0x11) {
        r->data[0] = (uint8_t)g.decoded;
        r->data_length = 1 + 2 * g.decoded;
        memcpy(r->data + 1, g.pairs, 2 * g.decoded);
        if (!g.failure) {
            if (g.decoded != g.max_sector)
                g.failure = "82,ERRORS ON TRACK";
            for (unsigned i = 0; i < g.decoded; i++)
                if (g.pairs[2 * i + 1])
                    g.failure = "82,ERRORS ON TRACK";
        }
    }
    mt_uci_reply_status(r, g.failure ? g.failure : "00,OK");
    return MT_UCI_REPLY;
}
static mt_uci_result_t complete(mt_uci_reply_t *r) {
    if (g.changed) {
        g.state = FENCE;
        return MT_UCI_WAIT;
    }
    return finish(r);
}
/* GCR's 5-bit codes are numeric format constants, not an imported table. */
static int nibble(unsigned code) {
    static const uint8_t codes[16] = {10, 11, 18, 19, 14, 15, 22, 23,
                                      9,  25, 26, 27, 13, 29, 30, 21};
    for (unsigned i = 0; i < 16; i++)
        if (codes[i] == code)
            return (int)i;
    return -1;
}
static bool decode(uint32_t bit, uint8_t *out, unsigned count) {
    bool valid = true;
    uint32_t bits = g.length * 8;
    for (unsigned i = 0; i < count; i++) {
        unsigned value = 0;
        for (unsigned half = 0; half < 2; half++) {
            unsigned code = 0;
            for (unsigned j = 0; j < 5; j++) {
                uint32_t at = bit++ % bits;
                code = (code << 1) | ((g.gcr[at / 8] >> (7 - at % 8)) & 1U);
            }
            int n = nibble(code);
            if (n < 0) {
                valid = false;
                n = 0;
            }
            value = (value << 4) | (unsigned)n;
        }
        out[i] = (uint8_t)value;
    }
    return valid;
}
static mt_uci_result_t scan(mt_uci_reply_t *r) {
    /* At most512 scanned bytes or one decoded sector per foreground pass.
     * Two circular revolutions cover a header/data pair straddling byte0. */
    for (unsigned work = 0; work < 512 && g.scan < g.length * 2; work++) {
        uint32_t at = g.scan++;
        uint8_t v = g.gcr[at % g.length];
        if (v == 255) {
            g.ones++;
            continue;
        }
        unsigned ones = g.ones;
        g.ones = 0;
        if (ones < 2)
            continue;
        unsigned shift = 0;
        while (v & 128) {
            shift++;
            v = (uint8_t)(v << 1);
        }
        uint32_t bit = (at % g.length) * 8 + shift;
        uint8_t h[8];
        bool header_ok = decode(bit, h, 8);
        if (h[0] == 8) {
            if (g.pending && g.records)
                g.pairs[2 * (g.records - 1) + 1] = 0xe4;
            g.pending = false;
            if (g.records < g.max_sector) {
                g.sector = h[2];
                g.pairs[2 * g.records] = (uint8_t)g.sector;
                uint8_t code = h[3] != g.track            ? 0xe1
                               : g.sector >= g.max_sector ? 0xe2
                               : !header_ok               ? 0xe3
                                                          : 0;
                g.pairs[2 * g.records + 1] = code;
                g.records++;
                g.pending = code == 0;
                if (g.pending)
                    g.scan = at + 10;
            }
        } else if (h[0] == 7 && g.pending) {
            uint8_t sector[260];
            bool valid = decode(bit, sector, 260);
            uint8_t check = 0;
            for (unsigned i = 1; i <= 256; i++)
                check ^= sector[i];
            c64_uci_reu_write(g.destination + (uint32_t)g.sector * 256, sector + 1, 256);
            g.changed = true;
            g.pairs[2 * (g.records - 1) + 1] = valid && check == sector[257] ? 0 : 0xe3;
            g.decoded++;
            g.pending = false;
            g.scan = at + 325;
            if (g.decoded == g.max_sector)
                return complete(r);
            return MT_UCI_WAIT;
        }
    }
    if (g.scan >= g.length * 2)
        return complete(r);
    return MT_UCI_WAIT;
}
mt_uci_result_t c64_uci_geos_poll(mt_uci_reply_t *r) {
    if (g.state == IDLE)
        return answer(r, "83,NOT IN DATA MODE");
    if (g.state == HOLD) {
        int held = c64_uci_service_hold_begin();
        if (held > 0)
            return MT_UCI_WAIT;
        if (held < 0) {
            g.failure = "87,INTERNAL ERROR";
            return finish(r);
        }
        if (g.command == 0x40) {
            mt_uci_reply_clear(r);
            for (unsigned i = 0; i < 4; i++) {
                uint32_t base, size;
                unsigned type = drive_info(i, &base, &size);
                r->data[2 * i] = (uint8_t)type;
                r->data[2 * i + 1] = type == 0xdd ? (uint8_t)(size >> 16) : 0;
            }
            r->data_length = 8;
            mt_uci_reply_status(r, "00,OK");
            g.state = IDLE;
            c64_uci_service_hold_end();
            return MT_UCI_REPLY;
        }
        if (g.command == 0x11) {
            g.gcr = malloc(g.length);
            if (!g.gcr) {
                g.failure = "87,INTERNAL ERROR";
                return finish(r);
            }
            g.state = GCR_READ;
            return MT_UCI_WAIT;
        }
        g.type = drive_info(g.drive, &g.address, &g.length);
        const char *ext = g.type == 0x41   ? "D64"
                          : g.type == 0x71 ? "D71"
                          : g.type == 0x81 ? "D81"
                                           : "DNP";
        if (!g.type || !g.length || g.length > 12U * 1024U * 1024U || !extension(g.path, ext)) {
            g.failure = "91,INCOMPATIBLE IMAGE";
            return finish(r);
        }
        if (!g.save) {
            mt_sd_info_t info;
            mt_sd_result_t s = mt_sd_file_stat(g.path, &info);
            if (s.status != MT_SD_OK) {
                g.failure = "82,FILE NOT FOUND";
                return finish(r);
            }
            if (info.size < g.length || (g.type == 0xdd && info.size != g.length)) {
                g.failure = "91,INCOMPATIBLE IMAGE";
                return finish(r);
            }
        }
        if (g.what_if)
            return finish(r);
        mt_sd_result_t s = mt_sd_file_open(g.path, g.save ? "wb" : "rb", &g.file);
        if (s.status != MT_SD_OK) {
            g.failure = c64_uci_sd_status(s);
            return finish(r);
        }
        g.state = FILE_COPY;
        return MT_UCI_WAIT;
    }
    if (g.state == FILE_COPY) {
        size_t n = g.length - g.offset;
        if (n > sizeof(g.scratch))
            n = sizeof(g.scratch);
        uint32_t where = g.address + g.offset;
        if (g.type == 0x71) {
            if (g.offset < 174848 && n > 174848 - g.offset)
                n = 174848 - g.offset;
            if (g.offset >= 174848)
                where += 700U * 256U - 174848U;
        }
        if (g.save) {
            c64_uci_reu_read(where, g.scratch, n);
            mt_sd_result_t s = mt_sd_file_write_buffered(g.file, g.scratch, n);
            if (s.status != MT_SD_OK || s.size != n)
                g.failure = "87,INTERNAL ERROR";
        } else {
            mt_sd_result_t s = mt_sd_file_read(g.file, g.scratch, n);
            if (s.size) {
                c64_uci_reu_write(where, g.scratch, s.size);
                g.changed = true;
            }
            if (s.status != MT_SD_OK || s.size != n)
                g.failure = "87,INTERNAL ERROR";
        }
        g.offset += (uint32_t)n;
        if (g.failure || g.offset == g.length)
            return complete(r);
        return MT_UCI_WAIT;
    }
    if (g.state == GCR_READ) {
        size_t n = g.length - g.offset;
        if (n > 4096)
            n = 4096;
        c64_uci_reu_read(g.address + g.offset, g.gcr + g.offset, n);
        g.offset += (uint32_t)n;
        if (g.offset == g.length)
            g.state = GCR_SCAN;
        return MT_UCI_WAIT;
    }
    if (g.state == GCR_SCAN)
        return scan(r);
    if (g.state == FENCE) {
        int fence = c64_uci_reu_fence();
        if (fence > 0)
            return MT_UCI_WAIT;
        if (fence < 0)
            return MT_UCI_FAILED;
        g.changed = false;
        return finish(r);
    }
    return MT_UCI_FAILED;
}
mt_uci_result_t c64_uci_geos_begin(const uint8_t c[896], size_t length, const char *cwd,
                                   mt_uci_reply_t *r) {
    if (g.state != IDLE)
        return MT_UCI_FAILED;
    memset(&g, 0, sizeof(g));
    g.command = length >= 2 ? c[1] : 0;
    if (!(c64_uci_read(MT_REG_C64_EXPANSION_CONFIG) & MT_C64_EXPANSION_REU_ENABLE))
        return answer(r, "84,REU NOT ENABLED");
    if (g.command == 0x11) {
        if (length != 14 || !c[3])
            return answer(r, "81,INVALID PARAMS");
        g.track = c[2];
        g.max_sector = c[3];
        g.address = le24(c + 4);
        g.destination = le24(c + 8);
        g.length = c[12] | ((uint32_t)c[13] << 8);
        if (g.length < 10 || g.length > MT_C64_REU_DDR_SIZE - g.address ||
            (uint32_t)g.max_sector * 256 > MT_C64_REU_DDR_SIZE - g.destination)
            return answer(r, "81,INVALID PARAMS");
    } else if (g.command == 0x41 || g.command == 0x42) {
        if (length < 4)
            return answer(r, "81,INVALID PARAMS");
        g.save = g.command == 0x42;
        g.what_if = !g.save && (c[2] & 128) != 0;
        g.drive = c[2] & 127;
        if (g.drive >= 4 || !c64_uci_path(cwd, c + 3, length - 3, g.path))
            return answer(r, "81,INVALID PARAMS");
        if (c64_uci_protected(g.path, g.save))
            return answer(r, "98,FUNCTION PROHIBITED");
    } else if (g.command != 0x40)
        return answer(r, "21,UNKNOWN COMMAND");
    g.state = HOLD;
    return c64_uci_geos_poll(r);
}
mt_uci_result_t c64_uci_geos_abort(void) {
    if (g.state == IDLE)
        return MT_UCI_REPLY;
    close_file();
    if (g.changed || g.state == FENCE) {
        int f = c64_uci_reu_fence();
        if (f > 0)
            return MT_UCI_WAIT;
        if (f < 0)
            return MT_UCI_FAILED;
        g.changed = false;
    }
    if (g.close_failed) {
        c64_uci_service_hold_fault();
        return MT_UCI_FAILED;
    }
    free(g.gcr);
    g.gcr = NULL;
    g.state = IDLE;
    c64_uci_service_hold_end();
    return MT_UCI_REPLY;
}
void c64_uci_geos_reset(void) {
    if (c64_uci_geos_abort() == MT_UCI_REPLY)
        memset(&g, 0, sizeof(g));
}
