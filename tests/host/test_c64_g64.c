/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_g64.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void make_image(uint8_t *data, size_t size)
{
    const size_t track_offset = 12U + 2U * 8U;
    assert(size >= track_offset + 12U);
    memset(data, 0U, size);
    memcpy(data, "GCR-1541", 8U);
    data[8] = 0U;
    data[9] = 2U;
    write_le16(data + 10U, 16U);
    write_le32(data + 12U, (uint32_t)track_offset);
    write_le32(data + 20U, 2U);
    write_le16(data + track_offset, 10U);
    for (size_t index = 0U; index < 10U; ++index) {
        data[track_offset + 2U + index] = (uint8_t)(0x80U + index);
    }
}

int main(void)
{
    uint8_t data[64U];
    uint8_t track[MT_C64_G64_TRACK_BUFFER_SIZE];
    mt_c64_g64_image_t image;
    size_t meaningful_size = 0U;

    make_image(data, sizeof(data));
    assert(mt_c64_g64_open(data, sizeof(data), &image) == MT_C64_G64_OK);
    assert(image.track_count == 2U && image.maximum_track_size == 16U);
    assert(mt_c64_g64_read_track(&image, 0U, track, sizeof(track),
                                 &meaningful_size) == MT_C64_G64_OK);
    assert(meaningful_size == 12U);
    assert(track[0] == 10U && track[1] == 0x40U);
    assert(memcmp(track + 2U, data + 30U, 10U) == 0);
    assert(track[12] == 0xFFU && track[sizeof(track) - 1U] == 0xFFU);

    assert(mt_c64_g64_read_track(&image, 1U, track, sizeof(track),
                                 &meaningful_size) == MT_C64_G64_OK);
    assert(meaningful_size == 2U && track[0] == 0U && track[1] == 0U);
    assert(track[2] == 0xFFU);
    assert(mt_c64_g64_read_track(&image, 2U, track, sizeof(track),
                                 &meaningful_size) == MT_C64_G64_OK);
    assert(meaningful_size == 2U && track[0] == 0U && track[1] == 0U);
    assert(mt_c64_g64_read_track(&image, 84U, track, sizeof(track),
                                 &meaningful_size) ==
           MT_C64_G64_TRACK_OUT_OF_RANGE);
    assert(mt_c64_g64_read_track(&image, 0U, track, sizeof(track) - 1U,
                                 &meaningful_size) ==
           MT_C64_G64_BUFFER_TOO_SMALL);

    make_image(data, sizeof(data));
    data[8] = 1U;
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_UNSUPPORTED_VERSION);
    make_image(data, sizeof(data));
    write_le32(data + 20U, 32U);
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_UNSUPPORTED_SPEED_MAP);
    make_image(data, sizeof(data));
    write_le32(data + 12U, 8U);
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_BAD_TRACK_OFFSET);
    make_image(data, sizeof(data));
    write_le16(data + 28U, 17U);
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_BAD_TRACK_SIZE);
    make_image(data, sizeof(data));
    write_le16(data + 10U, 8191U);
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_BAD_MAX_TRACK_SIZE);
    make_image(data, sizeof(data));
    write_le32(data + 16U, 30U);
    write_le16(data + 30U, 2U);
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_OVERLAPPING_TRACKS);
    make_image(data, sizeof(data));
    data[0] = 'X';
    assert(mt_c64_g64_open(data, sizeof(data), &image) ==
           MT_C64_G64_BAD_MAGIC);
    assert(strcmp(mt_c64_g64_result_name(MT_C64_G64_OK), "ok") == 0);
    return 0;
}
