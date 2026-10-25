/* SPDX-License-Identifier: GPL-3.0-only */
/* GEOS CVT wire format follows Ultimate 3.15 filesystem_d64.cc at
 * d43546fe25b26cb332f43f00219d33153dfe44f8: 254-byte header/info blocks,
 * VLIR block counts plus last-sector end bytes, and G98 high counts. */
#include "multitini_c64_uci_cvt.h"
#include <stdlib.h>
#include <string.h>

#define PAYLOAD 254u
#define RECORDS 127u
static const char signature[] = "PRG formatted GEOS file V1.0";
static const char signature_long[] = "PRG GeoConvert98-format V2.0";

typedef struct {
    uint16_t blocks;
    uint8_t track, sector, last;
} record_t;
typedef struct {
    uint8_t visited[8192];
    uint8_t info[256], index[256];
    record_t records[RECORDS];
    unsigned count;
    bool extended;
    mt_c64_uci_cvt_visitor_t visitor;
    void *visitor_context;
} layout_t;

static bool valid_entry(const uint8_t *e)
{
    unsigned type = e[0] & 7u;
    return type >= 1 && type <= 3 && e[21] <= 1;
}

static bool mark(layout_t *l, uint8_t t, uint8_t s)
{
    unsigned n = (unsigned)t * 256 + s;
    uint8_t bit = (uint8_t)(1u << (n & 7));
    if (!t || (l->visited[n >> 3] & bit)) return false;
    l->visited[n >> 3] |= bit;
    return !l->visitor || l->visitor(l->visitor_context, t, s);
}

static mt_c64_uci_cvt_result_t read_owned(layout_t *l, const mt_c64_uci_cvt_ops_t *o,
                                        uint8_t t, uint8_t s, uint8_t *data)
{
    if (!mark(l, t, s)) return MT_C64_UCI_CVT_MALFORMED;
    return o->read_sector(o->context, t, s, data) ? MT_C64_UCI_CVT_OK : MT_C64_UCI_CVT_IO;
}

static mt_c64_uci_cvt_result_t chain(layout_t *l, const mt_c64_uci_cvt_ops_t *o,
                                   record_t *r)
{
    uint8_t t = r->track, s = r->sector, data[256];
    while (t) {
        mt_c64_uci_cvt_result_t result = read_owned(l, o, t, s, data);
        if (result != MT_C64_UCI_CVT_OK) return result;
        if (r->blocks == UINT16_MAX) return MT_C64_UCI_CVT_MALFORMED;
        r->blocks++;
        t = data[0]; s = data[1];
        if (!t) {
            if (!s) return MT_C64_UCI_CVT_MALFORMED;
            r->last = s;
        }
    }
    if (r->blocks > 255) l->extended = true;
    return MT_C64_UCI_CVT_OK;
}

static mt_c64_uci_cvt_result_t inspect(const uint8_t *e,
    const mt_c64_uci_cvt_ops_t *o, layout_t *l)
{
    if (!e || !o || !o->read_sector || !valid_entry(e) || !e[19])
        return MT_C64_UCI_CVT_MALFORMED;
    mt_c64_uci_cvt_result_t result = read_owned(l, o, e[19], e[20], l->info);
    if (result != MT_C64_UCI_CVT_OK) return result;
    if (!e[21]) {
        l->count = 1;
        l->records[0].track = e[1]; l->records[0].sector = e[2];
        return chain(l, o, &l->records[0]);
    }
    result = read_owned(l, o, e[1], e[2], l->index);
    if (result != MT_C64_UCI_CVT_OK) return result;
    for (unsigned i = 0; i < RECORDS; ++i) {
        record_t *r = &l->records[i];
        r->track = l->index[2 + i * 2]; r->sector = l->index[3 + i * 2];
        if (!r->track && !r->sector) break;
        l->count = i + 1;
        if (r->track) {
            result = chain(l, o, r);
            if (result != MT_C64_UCI_CVT_OK) return result;
        } else r->last = r->sector;
    }
    /* Ultimate omits trailing empty records once a data record exists. */
    for (unsigned i = l->count; i; --i) {
        if (l->records[i - 1].blocks) { l->count = i; break; }
    }
    return MT_C64_UCI_CVT_OK;
}

mt_c64_uci_cvt_result_t mt_c64_uci_cvt_walk(const uint8_t entry[30],
    const mt_c64_uci_cvt_ops_t *ops, mt_c64_uci_cvt_visitor_t visitor, void *context)
{
    layout_t *l = calloc(1, sizeof(*l));
    if (!l) return MT_C64_UCI_CVT_NOMEM;
    l->visitor = visitor; l->visitor_context = context;
    mt_c64_uci_cvt_result_t result = inspect(entry, ops, l);
    free(l);
    return result;
}

mt_c64_uci_cvt_result_t mt_c64_uci_cvt_export(const uint8_t entry[30],
    const mt_c64_uci_cvt_ops_t *ops, uint8_t **data, size_t *size)
{
    if (!data || !size) return MT_C64_UCI_CVT_MALFORMED;
    *data = NULL; *size = 0;
    layout_t *l = calloc(1, sizeof(*l));
    if (!l) return MT_C64_UCI_CVT_NOMEM;
    mt_c64_uci_cvt_result_t result = inspect(entry, ops, l);
    if (result != MT_C64_UCI_CVT_OK) { free(l); return result; }
    size_t header = PAYLOAD * (entry[21] ? (l->extended ? 4u : 3u) : 2u);
    size_t length = header;
    for (unsigned i = 0; i < l->count; ++i) {
        record_t *r = &l->records[i];
        if (r->blocks) length += (size_t)r->blocks * PAYLOAD -
            ((i + 1 == l->count) ? 255u - r->last : 0u);
    }
    uint8_t *out = calloc(1, length);
    if (!out) { free(l); return MT_C64_UCI_CVT_NOMEM; }
    memcpy(out, entry, 30);
    out[1] = out[2] = out[19] = out[20] = 0;
    memcpy(out + 30, l->extended && entry[21] ? signature_long : signature,
           sizeof(signature));
    memcpy(out + PAYLOAD, l->info + 2, PAYLOAD);
    size_t offset = header;
    for (unsigned i = 0; i < l->count; ++i) {
        record_t *r = &l->records[i];
        if (entry[21]) {
            out[2 * PAYLOAD + 2 * i] = (uint8_t)r->blocks;
            out[2 * PAYLOAD + 2 * i + 1] = r->last;
            if (l->extended) out[3 * PAYLOAD + 2 * i] = (uint8_t)(r->blocks >> 8);
        }
        uint8_t t = r->track, s = r->sector, sector[256];
        for (unsigned b = 0; b < r->blocks; ++b) {
            if (!t || !ops->read_sector(ops->context, t, s, sector)) {
                result = MT_C64_UCI_CVT_IO; goto done;
            }
            size_t n = PAYLOAD;
            if (i + 1 == l->count && b + 1 == r->blocks) n = r->last - 1u;
            memcpy(out + offset, sector + 2, n); offset += n;
            t = sector[0]; s = sector[1];
        }
    }
    *data = out; *size = length; out = NULL;
done:
    free(out); free(l);
    return result;
}

static mt_c64_uci_cvt_result_t allocate(layout_t *l, const mt_c64_uci_cvt_ops_t *o,
                                       uint8_t *t, uint8_t *s)
{
    if (!o->allocate_sector(o->context, t, s)) return MT_C64_UCI_CVT_NOSPACE;
    return mark(l, *t, *s) ? MT_C64_UCI_CVT_OK : MT_C64_UCI_CVT_MALFORMED;
}

mt_c64_uci_cvt_result_t mt_c64_uci_cvt_import(const uint8_t *data, size_t size,
    const mt_c64_uci_cvt_ops_t *ops, uint8_t entry[30])
{
    if (!data || !entry || !ops || !ops->allocate_sector || !ops->write_sector ||
        size < 2 * PAYLOAD || !valid_entry(data)) return MT_C64_UCI_CVT_MALFORMED;
    bool extended = !memcmp(data + 33, signature_long + 3, sizeof(signature_long) - 3);
    if (!extended && memcmp(data + 33, signature + 3, sizeof(signature) - 3))
        return MT_C64_UCI_CVT_MALFORMED;
    bool vlir = data[21] != 0;
    size_t header = PAYLOAD * (vlir ? (extended ? 4u : 3u) : 2u);
    if (size < header) return MT_C64_UCI_CVT_MALFORMED;
    layout_t *l = calloc(1, sizeof(*l));
    if (!l) return MT_C64_UCI_CVT_NOMEM;
    mt_c64_uci_cvt_result_t result = MT_C64_UCI_CVT_MALFORMED;
    size_t expected = header;
    unsigned total = vlir ? 2 : 1;
    if (vlir) {
        for (unsigned i = 0; i < RECORDS; ++i) {
            record_t *r = &l->records[i];
            r->blocks = data[2 * PAYLOAD + 2 * i];
            r->last = data[2 * PAYLOAD + 2 * i + 1];
            if (extended) r->blocks |= (uint16_t)data[3 * PAYLOAD + 2 * i] << 8;
            if (!r->blocks && !r->last) break;
            if (r->blocks && !r->last) goto done;
            l->count = i + 1;
            total += r->blocks;
        }
        unsigned last = l->count;
        while (last && !l->records[last - 1].blocks) --last;
        for (unsigned i = 0; i < last; ++i) {
            record_t *r = &l->records[i];
            if (r->blocks) expected += (size_t)r->blocks * PAYLOAD -
                ((i + 1 == last) ? 255u - r->last : 0u);
        }
        /* The final record may be sector padded by other CVT writers. */
        size_t padding = last ? 255u - l->records[last - 1].last : 0u;
        if (size != expected && size != expected + padding) goto done;
    } else {
        size_t payload = size - header;
        if (payload > ((size_t)UINT16_MAX - 1u) * PAYLOAD) goto done;
        size_t blocks = (payload + PAYLOAD - 1) / PAYLOAD;
        if (blocks > UINT16_MAX) goto done;
        l->count = 1;
        l->records[0].blocks = (uint16_t)blocks;
        l->records[0].last = payload ? (uint8_t)((payload - 1) % PAYLOAD + 2) : 0;
        total += (unsigned)blocks;
    }
    if (total > UINT16_MAX) goto done;
    uint8_t next_entry[30], sector[256], info_t, info_s, index_t = 0, index_s = 0;
    memcpy(next_entry, data, 30);
    result = allocate(l, ops, &info_t, &info_s);
    if (result != MT_C64_UCI_CVT_OK) goto done;
    sector[0] = 0; sector[1] = 255;
    memcpy(sector + 2, data + PAYLOAD, PAYLOAD);
    if (!ops->write_sector(ops->context, info_t, info_s, sector)) {
        result = MT_C64_UCI_CVT_IO; goto done;
    }
    next_entry[19] = info_t; next_entry[20] = info_s;
    next_entry[1] = next_entry[2] = 0;
    if (vlir) {
        result = allocate(l, ops, &index_t, &index_s);
        if (result != MT_C64_UCI_CVT_OK) goto done;
        next_entry[1] = index_t; next_entry[2] = index_s;
        l->index[1] = 255;
    }
    size_t offset = header;
    for (unsigned i = 0; i < l->count; ++i) {
        record_t *r = &l->records[i];
        uint8_t t = 0, s = r->last;
        if (r->blocks) {
            result = allocate(l, ops, &t, &s);
            if (result != MT_C64_UCI_CVT_OK) goto done;
        }
        if (vlir) { l->index[2 + 2 * i] = t; l->index[3 + 2 * i] = s; }
        else { next_entry[1] = t; next_entry[2] = s; }
        for (unsigned b = 0; b < r->blocks; ++b) {
            bool final = b + 1 == r->blocks;
            size_t n = final ? r->last - 1u : PAYLOAD;
            uint8_t nt = 0, ns = r->last;
            if (!final) {
                result = allocate(l, ops, &nt, &ns);
                if (result != MT_C64_UCI_CVT_OK) goto done;
            }
            memset(sector, 0, sizeof(sector)); sector[0] = nt; sector[1] = ns;
            /* Preserve stored padding between records, as Ultimate exports it. */
            size_t available = size - offset;
            size_t copied = available < PAYLOAD ? available : PAYLOAD;
            if (copied < n) { result = MT_C64_UCI_CVT_MALFORMED; goto done; }
            memcpy(sector + 2, data + offset, copied);
            if (!ops->write_sector(ops->context, t, s, sector)) {
                result = MT_C64_UCI_CVT_IO; goto done;
            }
            offset += copied; t = nt; s = ns;
        }
    }
    if (vlir && !ops->write_sector(ops->context, index_t, index_s, l->index)) {
        result = MT_C64_UCI_CVT_IO; goto done;
    }
    next_entry[0] |= 0x80;
    next_entry[28] = (uint8_t)total; next_entry[29] = (uint8_t)(total >> 8);
    memcpy(entry, next_entry, sizeof(next_entry));
    result = MT_C64_UCI_CVT_OK;
done:
    free(l);
    return result;
}
