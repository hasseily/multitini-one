/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_uci_cvt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTORS 600u
typedef struct {
    uint8_t data[SECTORS][256];
    bool used[SECTORS];
    unsigned allocations, writes, visits;
    unsigned fail_allocate, fail_write;
    bool fail_read, duplicate, reject_visit;
} disk_t;

static unsigned address(uint8_t t, uint8_t s)
{
    return t && s < 32 ? (unsigned)(t - 1) * 32 + s : SECTORS;
}
static bool read_sector(void *context, uint8_t t, uint8_t s, uint8_t out[256])
{
    disk_t *d = context;
    unsigned n = address(t, s);
    if (d->fail_read || n >= SECTORS || !d->used[n]) return false;
    memcpy(out, d->data[n], 256);
    return true;
}
static bool allocate_sector(void *context, uint8_t *t, uint8_t *s)
{
    disk_t *d = context;
    ++d->allocations;
    if (d->allocations == d->fail_allocate) return false;
    unsigned i;
    for (i = 0; i < SECTORS && d->used[i]; ++i) {}
    if (i == SECTORS) return false;
    if (d->duplicate && d->allocations > 1) i = 0;
    d->used[i] = true; *t = (uint8_t)(i / 32 + 1); *s = (uint8_t)(i % 32);
    return true;
}
static bool write_sector(void *context, uint8_t t, uint8_t s, const uint8_t in[256])
{
    disk_t *d = context;
    unsigned n = address(t, s);
    ++d->writes;
    if (d->writes == d->fail_write || n >= SECTORS || !d->used[n]) return false;
    memcpy(d->data[n], in, 256);
    return true;
}
static bool visitor(void *context, uint8_t t, uint8_t s)
{
    disk_t *d = context;
    assert(address(t, s) < SECTORS);
    ++d->visits;
    return !d->reject_visit;
}
static mt_c64_uci_cvt_ops_t ops(disk_t *d)
{
    const mt_c64_uci_cvt_ops_t o = {d, read_sector, allocate_sector, write_sector};
    return o;
}
static uint8_t *fixture(size_t size, bool vlir, bool extended, unsigned blocks)
{
    uint8_t *p = calloc(1, size);
    assert(p);
    p[0] = 0x83; memset(p + 3, 0xa0, 16); memcpy(p + 3, "GEOS FIXTURE", 12);
    p[21] = vlir; p[22] = 6; p[23] = 86; p[24] = 7; p[25] = 4;
    p[28] = (uint8_t)blocks; p[29] = (uint8_t)(blocks >> 8);
    strcpy((char *)p + 30, extended ? "PRG GeoConvert98-format V2.0" :
                                    "PRG formatted GEOS file V1.0");
    for (unsigned i = 0; i < 254; ++i) p[254 + i] = (uint8_t)(i * 37);
    return p;
}
static void round_trip(uint8_t *p, size_t size, unsigned sectors)
{
    disk_t *d = calloc(1, sizeof(*d)); assert(d);
    mt_c64_uci_cvt_ops_t o = ops(d);
    uint8_t entry[30] = {0}, *out = NULL; size_t output_size = 0;
    assert(mt_c64_uci_cvt_import(p, size, &o, entry) == MT_C64_UCI_CVT_OK);
    assert(d->allocations == sectors && d->writes == sectors);
    assert(entry[19] && !memcmp(entry + 3, p + 3, 16));
    assert(mt_c64_uci_cvt_walk(entry, &o, visitor, d) == MT_C64_UCI_CVT_OK);
    assert(d->visits == sectors);
    assert(mt_c64_uci_cvt_export(entry, &o, &out, &output_size) == MT_C64_UCI_CVT_OK);
    assert(output_size == size && !memcmp(out, p, size));
    free(out); free(d);
}

int main(void)
{
    /* Linear payload straddles the 254-byte CBM sector boundary. */
    size_t linear_size = 508 + 255;
    uint8_t *linear = fixture(linear_size, false, false, 3);
    for (size_t i = 508; i < linear_size; ++i) linear[i] = (uint8_t)(i * 13);
    round_trip(linear, linear_size, 3);

    /* Sparse VLIR: skip, 257-byte record, skip, five-byte record. Intermediate
     * records are padded to full sectors; their last-length bytes remain exact. */
    size_t vlir_size = 762 + 508 + 5;
    uint8_t *vlir = fixture(vlir_size, true, false, 5);
    vlir[509] = 255; vlir[510] = 2; vlir[511] = 4;
    vlir[513] = 255; vlir[514] = 1; vlir[515] = 6;
    for (size_t i = 762; i < vlir_size; ++i) vlir[i] = (uint8_t)(i * 19);
    round_trip(vlir, vlir_size, 5);

    uint8_t *empty = fixture(762, true, false, 2);
    empty[509] = 255;
    round_trip(empty, 762, 2);
    free(empty);

    /* A >255-sector VLIR record uses GeoConvert98's fourth header block. */
    size_t long_size = 1016 + 256 * 254 + 2;
    uint8_t *long_file = fixture(long_size, true, true, 259);
    long_file[509] = 255; long_file[510] = 1; long_file[511] = 3;
    long_file[762] = 1;
    for (size_t i = 1016; i < long_size; ++i) long_file[i] = (uint8_t)(i * 23);
    round_trip(long_file, long_size, 259);
    free(long_file);

    disk_t *d = calloc(1, sizeof(*d)); assert(d);
    mt_c64_uci_cvt_ops_t o = ops(d);
    uint8_t entry[30], old_entry[30]; memset(old_entry, 0xa5, 30);
    memcpy(entry, old_entry, 30);
    assert(mt_c64_uci_cvt_import(vlir, vlir_size - 1, &o, entry) == MT_C64_UCI_CVT_MALFORMED);
    assert(!d->allocations && !d->writes && !memcmp(entry, old_entry, 30));
    vlir[33] ^= 1;
    assert(mt_c64_uci_cvt_import(vlir, vlir_size, &o, entry) == MT_C64_UCI_CVT_MALFORMED);
    vlir[33] ^= 1;
    assert(mt_c64_uci_cvt_import(vlir, 507, &o, entry) == MT_C64_UCI_CVT_MALFORMED);
    assert(mt_c64_uci_cvt_import(linear, SIZE_MAX, &o, entry) == MT_C64_UCI_CVT_MALFORMED);
    vlir[511] = 0;
    assert(mt_c64_uci_cvt_import(vlir, vlir_size, &o, entry) == MT_C64_UCI_CVT_MALFORMED);
    vlir[511] = 4;
    assert(!d->allocations && !d->writes);

    d->fail_allocate = 3;
    assert(mt_c64_uci_cvt_import(vlir, vlir_size, &o, entry) == MT_C64_UCI_CVT_NOSPACE);
    assert(!memcmp(entry, old_entry, 30));
    memset(d, 0, sizeof(*d)); d->fail_write = 2;
    assert(mt_c64_uci_cvt_import(vlir, vlir_size, &o, entry) == MT_C64_UCI_CVT_IO);
    assert(!memcmp(entry, old_entry, 30));
    memset(d, 0, sizeof(*d)); d->duplicate = true;
    assert(mt_c64_uci_cvt_import(vlir, vlir_size, &o, entry) == MT_C64_UCI_CVT_MALFORMED);
    memset(d, 0, sizeof(*d));
    assert(mt_c64_uci_cvt_import(vlir, vlir_size, &o, entry) == MT_C64_UCI_CVT_OK);
    uint8_t *index = d->data[address(entry[1], entry[2])];
    uint8_t record_t = index[4], record_s = index[5];
    /* A shared info/data block must never be freed twice by a directory walker. */
    index[4] = entry[19]; index[5] = entry[20];
    assert(mt_c64_uci_cvt_walk(entry, &o, NULL, NULL) == MT_C64_UCI_CVT_MALFORMED);
    index[4] = record_t; index[5] = record_s;
    uint8_t *record = d->data[address(record_t, record_s)];
    uint8_t next_t = record[0], next_s = record[1];
    record[0] = record_t; record[1] = record_s;
    assert(mt_c64_uci_cvt_walk(entry, &o, NULL, NULL) == MT_C64_UCI_CVT_MALFORMED);
    record[0] = next_t; record[1] = next_s;
    d->reject_visit = true;
    assert(mt_c64_uci_cvt_walk(entry, &o, visitor, d) == MT_C64_UCI_CVT_MALFORMED);
    d->reject_visit = false; d->fail_read = true;
    uint8_t *out = (uint8_t *)1; size_t output_size = 123;
    assert(mt_c64_uci_cvt_export(entry, &o, &out, &output_size) == MT_C64_UCI_CVT_IO);
    assert(out == NULL && output_size == 0);
    free(d); free(vlir); free(linear);
    puts("PASS: GEOS CVT linear/VLIR/G98 round trips, sparse records, ownership, malformed and I/O rollback contracts");
    return 0;
}
