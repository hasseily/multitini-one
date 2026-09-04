/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_D64_H
#define MULTITINI_C64_D64_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_C64_D64_SECTOR_SIZE ((size_t)256)
#define MT_C64_D64_MAX_SIZE ((size_t)206114)
#define MT_C64_D64_NAME_SIZE ((size_t)16)
#define MT_C64_D64_DISPLAY_NAME_SIZE ((size_t)17)
#define MT_C64_D64_MAX_PRG_SIZE ((size_t)65538)

typedef struct {
    uint8_t directory_sector;
    uint8_t directory_slot;
    uint8_t file_type;
    uint8_t start_track;
    uint8_t start_sector;
    uint16_t blocks;
    uint8_t raw_name[MT_C64_D64_NAME_SIZE];
    char display_name[MT_C64_D64_DISPLAY_NAME_SIZE];
} mt_c64_d64_prg_entry_t;

/* The entry pointer is valid only for the duration of a visitor call. Returning
 * false stops further calls. The directory chain is still validated and
 * entry_count still reports the total number of PRG entries. */
typedef bool (*mt_c64_d64_prg_visitor_fn)(
    void *context, const mt_c64_d64_prg_entry_t *entry);

typedef enum {
    MT_C64_D64_OK = 0,
    MT_C64_D64_INVALID_ARGUMENT,
    MT_C64_D64_WRONG_SIZE,
    MT_C64_D64_OUT_OF_RANGE,
    MT_C64_D64_DATA_SIZE_MISMATCH,
    MT_C64_D64_BAD_STRUCTURE,
    MT_C64_D64_CHAIN_CYCLE,
    MT_C64_D64_NOT_PRG,
    MT_C64_D64_ENTRY_CHANGED,
    MT_C64_D64_BUFFER_TOO_SMALL,
    MT_C64_D64_PRG_TOO_LARGE
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

/*
 * Apply a complete 256-byte-unit write to the data-sector part of a D64 image.
 * All arguments and bounds are checked before the image is changed. Error-info
 * tails, when present, are never addressable through this function.
 */
mt_c64_d64_result_t mt_c64_d64_write_blocks(
    uint8_t *image, size_t image_size, uint32_t lba,
    uint32_t block_count, const uint8_t *data, size_t data_size);

/* Enumerate the closed and unclosed PRG directory entries on a normal 1541
 * directory chain rooted at track 18, sector 1. The raw PETSCII name is kept
 * for stable identity; display_name is a bounded ASCII approximation. */
mt_c64_d64_result_t mt_c64_d64_list_prgs(
    const uint8_t *image, size_t image_size,
    mt_c64_d64_prg_visitor_fn visitor, void *visitor_context,
    size_t *entry_count);

/* Extract one closed PRG by following its sector chain. The directory locator
 * and every identity field are revalidated so a stale menu selection cannot
 * load a different file. The destination is not modified unless the complete
 * chain, final-sector length, maximum size, and caller capacity all pass. */
mt_c64_d64_result_t mt_c64_d64_extract_prg(
    const uint8_t *image, size_t image_size,
    const mt_c64_d64_prg_entry_t *entry,
    uint8_t *destination, size_t capacity, size_t *extracted_size);

const char *mt_c64_d64_result_name(mt_c64_d64_result_t result);

#endif
