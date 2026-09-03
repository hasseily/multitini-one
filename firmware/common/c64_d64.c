/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_d64.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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

const char *mt_c64_d64_result_name(mt_c64_d64_result_t result)
{
    switch (result) {
    case MT_C64_D64_OK: return "ok";
    case MT_C64_D64_INVALID_ARGUMENT: return "invalid argument";
    case MT_C64_D64_WRONG_SIZE: return "wrong size";
    case MT_C64_D64_OUT_OF_RANGE: return "out of range";
    case MT_C64_D64_DATA_SIZE_MISMATCH: return "data size mismatch";
    }
    return "unknown";
}
