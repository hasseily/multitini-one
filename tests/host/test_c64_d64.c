/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_d64.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    static uint8_t image[MT_C64_D64_MAX_SIZE];
    static uint8_t write_data[MT_C64_D64_SECTOR_SIZE * 2U];
    const uint8_t *data = NULL;
    size_t size = 0U;

    memset(image, 0xA5, sizeof(image));
    assert(mt_c64_d64_sector_count(174848U) == 683U);
    assert(mt_c64_d64_sector_count(175531U) == 683U);
    assert(mt_c64_d64_sector_count(196608U) == 768U);
    assert(mt_c64_d64_sector_count(197376U) == 768U);
    assert(mt_c64_d64_sector_count(205312U) == 802U);
    assert(mt_c64_d64_sector_count(206114U) == 802U);
    assert(mt_c64_d64_sector_count(1U) == 0U);

    assert(mt_c64_d64_resolve_request(image, 175531U, 357U, 19U,
                                      &data, &size) == MT_C64_D64_OK);
    assert(data == image + 357U * MT_C64_D64_SECTOR_SIZE);
    assert(size == 19U * MT_C64_D64_SECTOR_SIZE);

    assert(mt_c64_d64_resolve_request(image, 175531U, 682U, 1U,
                                      &data, &size) == MT_C64_D64_OK);
    assert(mt_c64_d64_resolve_request(image, 175531U, 683U, 1U,
                                      &data, &size) ==
           MT_C64_D64_OUT_OF_RANGE);
    assert(mt_c64_d64_resolve_request(image, 175531U, 682U, 2U,
                                      &data, &size) ==
           MT_C64_D64_OUT_OF_RANGE);
    assert(mt_c64_d64_resolve_request(image, 123U, 0U, 1U,
                                      &data, &size) == MT_C64_D64_WRONG_SIZE);
    assert(mt_c64_d64_resolve_request(NULL, 174848U, 0U, 1U,
                                      &data, &size) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(mt_c64_d64_resolve_request(image, 174848U, 0U, 0U,
                                      &data, &size) ==
           MT_C64_D64_INVALID_ARGUMENT);
    assert(strcmp(mt_c64_d64_result_name(MT_C64_D64_OK), "ok") == 0);

    memset(write_data, 0x3C, sizeof(write_data));
    image[174848U] = 0x5AU;
    assert(mt_c64_d64_write_blocks(image, 175531U, 681U, 2U,
                                   write_data, sizeof(write_data)) ==
           MT_C64_D64_OK);
    assert(memcmp(image + 681U * MT_C64_D64_SECTOR_SIZE,
                  write_data, sizeof(write_data)) == 0);
    assert(image[174848U] == 0x5AU);

    image[0] = 0xA5U;
    assert(mt_c64_d64_write_blocks(image, 174848U, 0U, 1U,
                                   write_data,
                                   MT_C64_D64_SECTOR_SIZE - 1U) ==
           MT_C64_D64_DATA_SIZE_MISMATCH);
    assert(image[0] == 0xA5U);
    assert(mt_c64_d64_write_blocks(image, 174848U, 682U, 2U,
                                   write_data, sizeof(write_data)) ==
           MT_C64_D64_OUT_OF_RANGE);
    assert(image[0] == 0xA5U);
    assert(mt_c64_d64_write_blocks(NULL, 174848U, 0U, 1U,
                                   write_data, MT_C64_D64_SECTOR_SIZE) ==
           MT_C64_D64_INVALID_ARGUMENT);
    return 0;
}
