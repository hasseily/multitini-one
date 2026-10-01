/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_d64.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define MT_C64_D64_DIRECTORY_TRACK 18U
#define MT_C64_D64_DIRECTORY_FIRST_SECTOR 1U
#define MT_C64_D64_DIRECTORY_LAST_SECTOR 18U
#define MT_C64_D64_DIRECTORY_ENTRIES_PER_SECTOR 8U
#define MT_C64_D64_DIRECTORY_ENTRY_SIZE 32U
#define MT_C64_D64_DIRECTORY_ENTRY_OFFSET 2U
#define MT_C64_D64_FILE_TYPE_PRG 2U
#define MT_C64_D64_FILE_TYPE_MASK 7U
#define MT_C64_D64_FILE_CLOSED 0x80U
#define MT_C64_D64_VISITED_BYTES ((802U + 7U) / 8U)

static uint8_t d64_track_count(size_t sector_count)
{
    switch (sector_count) {
    case 683U: return 35U;
    case 768U: return 40U;
    case 802U: return 42U;
    default: return 0U;
    }
}

static uint8_t d64_sectors_per_track(uint8_t track)
{
    if (track >= 1U && track <= 17U) return 21U;
    if (track <= 24U) return 19U;
    if (track <= 30U) return 18U;
    return 17U;
}

static mt_c64_d64_result_t d64_sector_lba(size_t sector_count,
                                           uint8_t track, uint8_t sector,
                                           size_t *lba)
{
    const uint8_t tracks = d64_track_count(sector_count);
    size_t result = 0U;
    uint8_t current;

    if (lba == NULL || tracks == 0U || track == 0U || track > tracks ||
        sector >= d64_sectors_per_track(track)) {
        return MT_C64_D64_BAD_STRUCTURE;
    }
    for (current = 1U; current < track; ++current)
        result += d64_sectors_per_track(current);
    result += sector;
    if (result >= sector_count) return MT_C64_D64_BAD_STRUCTURE;
    *lba = result;
    return MT_C64_D64_OK;
}

static bool d64_mark_visited(uint8_t *visited, size_t lba)
{
    const uint8_t mask = (uint8_t)(1U << (lba & 7U));
    uint8_t *const byte = &visited[lba >> 3U];
    if ((*byte & mask) != 0U) return false;
    *byte |= mask;
    return true;
}

static void d64_display_name(const uint8_t *raw, char *display)
{
    size_t length = MT_C64_D64_NAME_SIZE;
    size_t index;

    while (length != 0U && raw[length - 1U] == 0xa0U) --length;
    for (index = 0U; index < length; ++index) {
        const uint8_t byte = raw[index];
        if (byte == 0xa0U) display[index] = ' ';
        else if (byte >= 0xc1U && byte <= 0xdaU)
            display[index] = (char)((unsigned)'A' + byte - 0xc1U);
        else if (byte >= 0x20U && byte <= 0x7eU)
            display[index] = (char)byte;
        else
            display[index] = '?';
    }
    display[length] = '\0';
}

static void d64_read_directory_entry(const uint8_t *sector,
                                     uint8_t directory_sector,
                                     uint8_t directory_slot,
                                     mt_c64_d64_prg_entry_t *entry)
{
    const uint8_t *const raw = sector + MT_C64_D64_DIRECTORY_ENTRY_OFFSET +
        (size_t)directory_slot * MT_C64_D64_DIRECTORY_ENTRY_SIZE;
    memset(entry, 0, sizeof(*entry));
    entry->directory_sector = directory_sector;
    entry->directory_slot = directory_slot;
    entry->file_type = raw[0];
    entry->start_track = raw[1];
    entry->start_sector = raw[2];
    memcpy(entry->raw_name, raw + 3U, MT_C64_D64_NAME_SIZE);
    entry->blocks = (uint16_t)((uint16_t)raw[28U] |
                               ((uint16_t)raw[29U] << 8U));
    d64_display_name(entry->raw_name, entry->display_name);
}

static bool d64_entry_identity_matches(
    const mt_c64_d64_prg_entry_t *expected,
    const mt_c64_d64_prg_entry_t *actual)
{
    return expected->directory_sector == actual->directory_sector &&
           expected->directory_slot == actual->directory_slot &&
           expected->file_type == actual->file_type &&
           expected->start_track == actual->start_track &&
           expected->start_sector == actual->start_sector &&
           expected->blocks == actual->blocks &&
           memcmp(expected->raw_name, actual->raw_name,
                  MT_C64_D64_NAME_SIZE) == 0;
}

static mt_c64_d64_result_t d64_find_directory_entry(
    const uint8_t *image, size_t sector_count,
    const mt_c64_d64_prg_entry_t *expected,
    mt_c64_d64_prg_entry_t *actual)
{
    uint8_t visited[MT_C64_D64_VISITED_BYTES] = {0};
    uint8_t track = MT_C64_D64_DIRECTORY_TRACK;
    uint8_t sector = MT_C64_D64_DIRECTORY_FIRST_SECTOR;

    while (track != 0U) {
        const uint8_t *directory;
        size_t lba;
        mt_c64_d64_result_t status;

        if (track != MT_C64_D64_DIRECTORY_TRACK ||
            sector < MT_C64_D64_DIRECTORY_FIRST_SECTOR ||
            sector > MT_C64_D64_DIRECTORY_LAST_SECTOR) {
            return MT_C64_D64_BAD_STRUCTURE;
        }
        status = d64_sector_lba(sector_count, track, sector, &lba);
        if (status != MT_C64_D64_OK) return status;
        if (!d64_mark_visited(visited, lba))
            return MT_C64_D64_CHAIN_CYCLE;
        directory = image + lba * MT_C64_D64_SECTOR_SIZE;
        if (sector == expected->directory_sector) {
            d64_read_directory_entry(directory, sector,
                                     expected->directory_slot, actual);
            return d64_entry_identity_matches(expected, actual) ?
                MT_C64_D64_OK : MT_C64_D64_ENTRY_CHANGED;
        }
        track = directory[0];
        sector = directory[1];
    }
    return MT_C64_D64_ENTRY_CHANGED;
}

static mt_c64_d64_result_t d64_scan_file_chain(
    const uint8_t *image, size_t sector_count, uint8_t start_track,
    uint8_t start_sector, size_t *file_size)
{
    uint8_t visited[MT_C64_D64_VISITED_BYTES] = {0};
    uint8_t track = start_track;
    uint8_t sector = start_sector;
    size_t total = 0U;

    if (file_size == NULL || track == 0U)
        return MT_C64_D64_BAD_STRUCTURE;
    while (track != 0U) {
        const uint8_t *block;
        size_t lba;
        size_t payload_size;
        mt_c64_d64_result_t status = d64_sector_lba(
            sector_count, track, sector, &lba);
        if (status != MT_C64_D64_OK) return status;
        if (!d64_mark_visited(visited, lba))
            return MT_C64_D64_CHAIN_CYCLE;
        block = image + lba * MT_C64_D64_SECTOR_SIZE;
        if (block[0] == 0U) {
            if (block[1] == 0U) return MT_C64_D64_BAD_STRUCTURE;
            payload_size = (size_t)block[1] - 1U;
        } else {
            payload_size = MT_C64_D64_SECTOR_SIZE - 2U;
        }
        if (total > MT_C64_D64_MAX_PRG_SIZE - payload_size)
            return MT_C64_D64_PRG_TOO_LARGE;
        total += payload_size;
        track = block[0];
        sector = block[1];
    }
    *file_size = total;
    return MT_C64_D64_OK;
}

static void d64_copy_file_chain(const uint8_t *image, size_t sector_count,
                                uint8_t start_track, uint8_t start_sector,
                                uint8_t *destination)
{
    uint8_t track = start_track;
    uint8_t sector = start_sector;
    size_t offset = 0U;

    while (track != 0U) {
        const uint8_t *block;
        size_t lba = 0U;
        size_t payload_size;
        (void)d64_sector_lba(sector_count, track, sector, &lba);
        block = image + lba * MT_C64_D64_SECTOR_SIZE;
        payload_size = block[0] == 0U ? (size_t)block[1] - 1U :
                       MT_C64_D64_SECTOR_SIZE - 2U;
        memcpy(destination + offset, block + 2U, payload_size);
        offset += payload_size;
        track = block[0];
        sector = block[1];
    }
}

size_t mt_c64_d64_sector_count(size_t image_size)
{
    switch (image_size) {
    case 174848U:
    case 175531U:
        return 683U;
    case 196608U:
    case 197376U:
        return 768U;
    case 205312U:
    case 206114U:
        return 802U;
    default:
        return 0U;
    }
}

mt_c64_d64_result_t mt_c64_d64_resolve_request(
    const uint8_t *image, size_t image_size, uint32_t lba,
    uint32_t block_count, const uint8_t **data, size_t *size)
{
    const size_t sectors = mt_c64_d64_sector_count(image_size);
    size_t start;
    size_t count;

    if (image == NULL || data == NULL || size == NULL || block_count == 0U) {
        return MT_C64_D64_INVALID_ARGUMENT;
    }
    if (sectors == 0U) {
        return MT_C64_D64_WRONG_SIZE;
    }
    if ((size_t)lba >= sectors || (size_t)block_count > sectors - (size_t)lba) {
        return MT_C64_D64_OUT_OF_RANGE;
    }

    start = (size_t)lba * MT_C64_D64_SECTOR_SIZE;
    count = (size_t)block_count * MT_C64_D64_SECTOR_SIZE;
    *data = image + start;
    *size = count;
    return MT_C64_D64_OK;
}

mt_c64_d64_result_t mt_c64_d64_write_blocks(
    uint8_t *image, size_t image_size, uint32_t lba,
    uint32_t block_count, const uint8_t *data, size_t data_size)
{
    const uint8_t *resolved;
    size_t resolved_size;
    mt_c64_d64_result_t status;

    if (image == NULL || data == NULL) {
        return MT_C64_D64_INVALID_ARGUMENT;
    }
    status = mt_c64_d64_resolve_request(image, image_size, lba, block_count,
                                        &resolved, &resolved_size);
    if (status != MT_C64_D64_OK) return status;
    if (data_size != resolved_size) {
        return MT_C64_D64_DATA_SIZE_MISMATCH;
    }

    memmove(image + (size_t)lba * MT_C64_D64_SECTOR_SIZE,
            data, resolved_size);
    return MT_C64_D64_OK;
}

mt_c64_d64_result_t mt_c64_d64_list_prgs(
    const uint8_t *image, size_t image_size,
    mt_c64_d64_prg_visitor_fn visitor, void *visitor_context,
    size_t *entry_count)
{
    const size_t sector_count = mt_c64_d64_sector_count(image_size);
    uint8_t visited[MT_C64_D64_VISITED_BYTES] = {0};
    uint8_t track = MT_C64_D64_DIRECTORY_TRACK;
    uint8_t sector = MT_C64_D64_DIRECTORY_FIRST_SECTOR;
    size_t count = 0U;
    bool visit = true;

    if (image == NULL || entry_count == NULL)
        return MT_C64_D64_INVALID_ARGUMENT;
    *entry_count = 0U;
    if (sector_count == 0U) return MT_C64_D64_WRONG_SIZE;

    while (track != 0U) {
        const uint8_t *directory;
        size_t lba;
        uint8_t slot;
        mt_c64_d64_result_t status;

        if (track != MT_C64_D64_DIRECTORY_TRACK ||
            sector < MT_C64_D64_DIRECTORY_FIRST_SECTOR ||
            sector > MT_C64_D64_DIRECTORY_LAST_SECTOR) {
            return MT_C64_D64_BAD_STRUCTURE;
        }
        status = d64_sector_lba(sector_count, track, sector, &lba);
        if (status != MT_C64_D64_OK) return status;
        if (!d64_mark_visited(visited, lba))
            return MT_C64_D64_CHAIN_CYCLE;
        directory = image + lba * MT_C64_D64_SECTOR_SIZE;
        for (slot = 0U; slot < MT_C64_D64_DIRECTORY_ENTRIES_PER_SECTOR;
             ++slot) {
            mt_c64_d64_prg_entry_t entry;
            d64_read_directory_entry(directory, sector, slot, &entry);
            if ((entry.file_type & MT_C64_D64_FILE_TYPE_MASK) !=
                MT_C64_D64_FILE_TYPE_PRG) {
                continue;
            }
            ++count;
            if (visit && visitor != NULL &&
                !visitor(visitor_context, &entry)) {
                visit = false;
            }
        }
        track = directory[0];
        sector = directory[1];
    }
    *entry_count = count;
    return MT_C64_D64_OK;
}

mt_c64_d64_result_t mt_c64_d64_extract_prg(
    const uint8_t *image, size_t image_size,
    const mt_c64_d64_prg_entry_t *entry,
    uint8_t *destination, size_t capacity, size_t *extracted_size)
{
    const size_t sector_count = mt_c64_d64_sector_count(image_size);
    mt_c64_d64_prg_entry_t actual;
    mt_c64_d64_result_t status;
    size_t file_size;

    if (extracted_size != NULL) *extracted_size = 0U;
    if (image == NULL || entry == NULL || destination == NULL ||
        extracted_size == NULL || entry->directory_slot >=
            MT_C64_D64_DIRECTORY_ENTRIES_PER_SECTOR ||
        entry->directory_sector < MT_C64_D64_DIRECTORY_FIRST_SECTOR ||
        entry->directory_sector > MT_C64_D64_DIRECTORY_LAST_SECTOR) {
        return MT_C64_D64_INVALID_ARGUMENT;
    }
    if (sector_count == 0U) return MT_C64_D64_WRONG_SIZE;
    status = d64_find_directory_entry(image, sector_count, entry, &actual);
    if (status != MT_C64_D64_OK) return status;
    if ((actual.file_type & MT_C64_D64_FILE_TYPE_MASK) !=
        MT_C64_D64_FILE_TYPE_PRG) {
        return MT_C64_D64_NOT_PRG;
    }
    if ((actual.file_type & MT_C64_D64_FILE_CLOSED) == 0U)
        return MT_C64_D64_BAD_STRUCTURE;
    status = d64_scan_file_chain(image, sector_count, actual.start_track,
                                 actual.start_sector, &file_size);
    if (status != MT_C64_D64_OK) return status;
    if (file_size > capacity) return MT_C64_D64_BUFFER_TOO_SMALL;
    d64_copy_file_chain(image, sector_count, actual.start_track,
                        actual.start_sector, destination);
    *extracted_size = file_size;
    return MT_C64_D64_OK;
}

const char *mt_c64_d64_result_name(mt_c64_d64_result_t result)
{
    switch (result) {
    case MT_C64_D64_OK: return "ok";
    case MT_C64_D64_INVALID_ARGUMENT: return "invalid argument";
    case MT_C64_D64_WRONG_SIZE: return "wrong size";
    case MT_C64_D64_OUT_OF_RANGE: return "out of range";
    case MT_C64_D64_DATA_SIZE_MISMATCH: return "data size mismatch";
    case MT_C64_D64_BAD_STRUCTURE: return "bad structure";
    case MT_C64_D64_CHAIN_CYCLE: return "chain cycle";
    case MT_C64_D64_NOT_PRG: return "not a PRG";
    case MT_C64_D64_ENTRY_CHANGED: return "directory entry changed";
    case MT_C64_D64_BUFFER_TOO_SMALL: return "buffer too small";
    case MT_C64_D64_PRG_TOO_LARGE: return "PRG too large";
    }
    return "unknown";
}
