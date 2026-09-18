/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_g64.h"

#include <string.h>

#define MT_C64_G64_FIXED_HEADER_SIZE ((size_t)12)

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static size_t table_end(uint8_t track_count)
{
    return MT_C64_G64_FIXED_HEADER_SIZE + (size_t)track_count * 8U;
}

mt_c64_g64_result_t mt_c64_g64_open(const uint8_t *data, size_t size,
                                    mt_c64_g64_image_t *image)
{
    size_t index;
    size_t header_size;
    uint8_t track_count;
    uint16_t maximum_track_size;

    if (data == NULL || image == NULL) return MT_C64_G64_INVALID_ARGUMENT;
    if (size < MT_C64_G64_FIXED_HEADER_SIZE) return MT_C64_G64_TOO_SMALL;
    if (memcmp(data, "GCR-1541", 8U) != 0) return MT_C64_G64_BAD_MAGIC;
    if (data[8] != 0U) return MT_C64_G64_UNSUPPORTED_VERSION;

    track_count = data[9];
    if (track_count == 0U || (size_t)track_count > MT_C64_G64_MAX_TRACKS) {
        return MT_C64_G64_BAD_TRACK_COUNT;
    }
    maximum_track_size = read_le16(data + 10U);
    if (maximum_track_size == 0U ||
        (size_t)maximum_track_size > MT_C64_G64_MAX_TRACK_DATA_SIZE) {
        return MT_C64_G64_BAD_MAX_TRACK_SIZE;
    }

    header_size = table_end(track_count);
    if (size < header_size) return MT_C64_G64_TOO_SMALL;

    for (index = 0U; index < (size_t)track_count; ++index) {
        const uint32_t offset =
            read_le32(data + MT_C64_G64_FIXED_HEADER_SIZE + index * 4U);
        const uint32_t speed = read_le32(
            data + MT_C64_G64_FIXED_HEADER_SIZE +
            (size_t)track_count * 4U + index * 4U);
        uint16_t track_size;
        if (speed > 3U) return MT_C64_G64_UNSUPPORTED_SPEED_MAP;
        if (offset == 0U) continue;
        if ((size_t)offset < header_size || (size_t)offset > size - 2U) {
            return MT_C64_G64_BAD_TRACK_OFFSET;
        }
        track_size = read_le16(data + (size_t)offset);
        if (track_size == 0U || track_size > maximum_track_size ||
            (size_t)track_size > MT_C64_G64_MAX_TRACK_DATA_SIZE ||
            (size_t)track_size > size - (size_t)offset - 2U) {
            return MT_C64_G64_BAD_TRACK_SIZE;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            const uint32_t previous_offset = read_le32(
                data + MT_C64_G64_FIXED_HEADER_SIZE + previous * 4U);
            uint16_t previous_size;
            size_t extent_end;
            size_t previous_end;
            if (previous_offset == 0U) continue;
            previous_size = read_le16(data + (size_t)previous_offset);
            extent_end = (size_t)offset + 2U + (size_t)track_size;
            previous_end = (size_t)previous_offset + 2U +
                           (size_t)previous_size;
            if ((size_t)offset < previous_end &&
                (size_t)previous_offset < extent_end) {
                return MT_C64_G64_OVERLAPPING_TRACKS;
            }
        }
    }

    image->data = data;
    image->size = size;
    image->track_count = track_count;
    image->maximum_track_size = maximum_track_size;
    return MT_C64_G64_OK;
}

mt_c64_g64_result_t mt_c64_g64_read_track(
    const mt_c64_g64_image_t *image, uint8_t half_track,
    uint8_t *destination, size_t capacity, size_t *meaningful_size)
{
    uint32_t offset;
    uint32_t speed;
    uint16_t track_size;
    uint16_t encoded_length;

    if (image == NULL || image->data == NULL || destination == NULL ||
        meaningful_size == NULL) {
        return MT_C64_G64_INVALID_ARGUMENT;
    }
    if ((size_t)half_track >= MT_C64_G64_MAX_TRACKS) {
        return MT_C64_G64_TRACK_OUT_OF_RANGE;
    }
    if (capacity < MT_C64_G64_TRACK_BUFFER_SIZE) {
        return MT_C64_G64_BUFFER_TOO_SMALL;
    }

    memset(destination, 0xFF, MT_C64_G64_TRACK_BUFFER_SIZE);
    if (half_track >= image->track_count) {
        destination[0] = 0U;
        destination[1] = 0U;
        *meaningful_size = 2U;
        return MT_C64_G64_OK;
    }
    offset = read_le32(image->data + MT_C64_G64_FIXED_HEADER_SIZE +
                       (size_t)half_track * 4U);
    speed = read_le32(image->data + MT_C64_G64_FIXED_HEADER_SIZE +
                      (size_t)image->track_count * 4U +
                      (size_t)half_track * 4U);
    if (offset == 0U) {
        destination[0] = 0U;
        destination[1] = 0U;
        *meaningful_size = 2U;
        return MT_C64_G64_OK;
    }

    track_size = read_le16(image->data + (size_t)offset);
    encoded_length = (uint16_t)(track_size | (uint16_t)(speed << 13));
    destination[0] = (uint8_t)encoded_length;
    destination[1] = (uint8_t)(encoded_length >> 8);
    memcpy(destination + 2U, image->data + (size_t)offset + 2U,
           (size_t)track_size);
    *meaningful_size = (size_t)track_size + 2U;
    return MT_C64_G64_OK;
}

const char *mt_c64_g64_result_name(mt_c64_g64_result_t result)
{
    switch (result) {
    case MT_C64_G64_OK: return "ok";
    case MT_C64_G64_INVALID_ARGUMENT: return "invalid argument";
    case MT_C64_G64_TOO_SMALL: return "too small";
    case MT_C64_G64_BAD_MAGIC: return "bad magic";
    case MT_C64_G64_UNSUPPORTED_VERSION: return "unsupported version";
    case MT_C64_G64_BAD_TRACK_COUNT: return "bad track count";
    case MT_C64_G64_BAD_MAX_TRACK_SIZE: return "bad maximum track size";
    case MT_C64_G64_UNSUPPORTED_SPEED_MAP:
        return "unsupported variable speed map";
    case MT_C64_G64_BAD_TRACK_OFFSET: return "bad track offset";
    case MT_C64_G64_BAD_TRACK_SIZE: return "bad track size";
    case MT_C64_G64_OVERLAPPING_TRACKS: return "overlapping tracks";
    case MT_C64_G64_TRACK_OUT_OF_RANGE: return "track out of range";
    case MT_C64_G64_BUFFER_TOO_SMALL: return "buffer too small";
    }
    return "unknown";
}
