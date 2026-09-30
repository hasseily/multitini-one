/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_GENESIS_CONTENT_H
#define MULTITINI_GENESIS_CONTENT_H
#include "multitini_content.h"
#include "multitini_genesis_interface.h"

typedef enum {
    MT_GENESIS_FORMAT_RAW = 0,
    MT_GENESIS_FORMAT_SMD
} mt_genesis_format_t;

typedef struct {
    size_t rom_size;
    mt_genesis_region_t region;
    uint8_t region_mask; /* Bit 0: Japan, 1: USA, 2: Europe. */
    uint8_t save_initial_value; /* Missing save only; existing files are preserved. */
    bool byte_swapped;
    bool six_button;
    bool save_declared;
    bool checksum_matches;
    uint16_t checksum_header;
    uint16_t checksum_computed;
    char title[49];
} mt_genesis_content_info_t;

/* Inspect without changing data. Header checksum mismatches are advisory:
 * homebrew and patched images may intentionally retain the original checksum.
 * SMD files consist of a 512-byte copier header and whole 16 KiB blocks. */
mt_content_validation_t mt_genesis_content_inspect(
    const uint8_t *data, size_t size, mt_genesis_format_t format,
    mt_genesis_content_info_t *info);
/* Validate first, then normalize in place to 68000 big-endian byte order.
 * On rejection, the buffer and size are unchanged. Calls are serialized by
 * the supervisor; SMD conversion uses one shared 16 KiB scratch block. */
mt_content_validation_t mt_genesis_content_decode(
    uint8_t *data, size_t *size, mt_genesis_format_t format,
    mt_genesis_content_info_t *info);
#endif
