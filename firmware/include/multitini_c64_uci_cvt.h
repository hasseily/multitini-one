/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_UCI_CVT_H
#define MULTITINI_C64_UCI_CVT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MT_C64_UCI_CVT_OK,
    MT_C64_UCI_CVT_MALFORMED,
    MT_C64_UCI_CVT_NOMEM,
    MT_C64_UCI_CVT_IO,
    MT_C64_UCI_CVT_NOSPACE
} mt_c64_uci_cvt_result_t;

typedef struct {
    void *context;
    bool (*read_sector)(void *, uint8_t track, uint8_t sector, uint8_t data[256]);
    bool (*allocate_sector)(void *, uint8_t *track, uint8_t *sector);
    bool (*write_sector)(void *, uint8_t track, uint8_t sector, const uint8_t data[256]);
} mt_c64_uci_cvt_ops_t;

typedef bool (*mt_c64_uci_cvt_visitor_t)(void *, uint8_t track, uint8_t sector);

/* Directory entries start at the file-type byte, excluding their two link bytes.
 * Export allocates *data; the caller frees it. Import allocates new sectors and
 * replaces entry only on success. The caller must roll back its image snapshot
 * on failure, and free any previous file separately. No callback writes occur
 * until the complete input has passed format/length validation. */
mt_c64_uci_cvt_result_t mt_c64_uci_cvt_export(const uint8_t entry[30],
    const mt_c64_uci_cvt_ops_t *ops, uint8_t **data, size_t *size);
mt_c64_uci_cvt_result_t mt_c64_uci_cvt_import(const uint8_t *data, size_t size,
    const mt_c64_uci_cvt_ops_t *ops, uint8_t entry[30]);
/* Enumerates info, index and all data sectors, rejecting cycles/cross-links. */
mt_c64_uci_cvt_result_t mt_c64_uci_cvt_walk(const uint8_t entry[30],
    const mt_c64_uci_cvt_ops_t *ops, mt_c64_uci_cvt_visitor_t visitor, void *context);

#endif
