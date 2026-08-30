/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_d64.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    static uint8_t image[MT_C64_D64_MAX_SIZE];
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
    return 0;
}
