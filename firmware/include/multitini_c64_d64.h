/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_D64_H
#define MULTITINI_C64_D64_H

#include <stddef.h>
#include <stdint.h>

#define MT_C64_D64_SECTOR_SIZE ((size_t)256)
#define MT_C64_D64_MAX_SIZE ((size_t)206114)

typedef enum {
    MT_C64_D64_OK = 0,
    MT_C64_D64_INVALID_ARGUMENT,
    MT_C64_D64_WRONG_SIZE,
    MT_C64_D64_OUT_OF_RANGE
} mt_c64_d64_result_t;

/* Return the number of 256-byte data sectors, excluding an error-info tail. */
size_t mt_c64_d64_sector_count(size_t image_size);

/*
 * Resolve one FPGA drive request into a byte slice of a validated D64 image.
 * The request uses an actual block count, not MiSTer's count-minus-one form.
 */
mt_c64_d64_result_t mt_c64_d64_resolve_request(
    const uint8_t *image, size_t image_size, uint32_t lba,
    uint32_t block_count, const uint8_t **data, size_t *size);

const char *mt_c64_d64_result_name(mt_c64_d64_result_t result);

#endif
