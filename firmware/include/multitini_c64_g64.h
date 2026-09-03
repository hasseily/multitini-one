/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_G64_H
#define MULTITINI_C64_G64_H

#include <stddef.h>
#include <stdint.h>

#define MT_C64_G64_MAX_TRACKS ((size_t)84)
#define MT_C64_G64_TRACK_BUFFER_SIZE ((size_t)8192)
#define MT_C64_G64_MAX_TRACK_DATA_SIZE \
    (MT_C64_G64_TRACK_BUFFER_SIZE - (size_t)2)

typedef enum {
    MT_C64_G64_OK = 0,
    MT_C64_G64_INVALID_ARGUMENT,
    MT_C64_G64_TOO_SMALL,
    MT_C64_G64_BAD_MAGIC,
    MT_C64_G64_UNSUPPORTED_VERSION,
    MT_C64_G64_BAD_TRACK_COUNT,
    MT_C64_G64_BAD_MAX_TRACK_SIZE,
    MT_C64_G64_UNSUPPORTED_SPEED_MAP,
    MT_C64_G64_BAD_TRACK_OFFSET,
    MT_C64_G64_BAD_TRACK_SIZE,
    MT_C64_G64_OVERLAPPING_TRACKS,
    MT_C64_G64_TRACK_OUT_OF_RANGE,
    MT_C64_G64_BUFFER_TOO_SMALL
} mt_c64_g64_result_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint8_t track_count;
    uint16_t maximum_track_size;
} mt_c64_g64_image_t;

/* Validate a version-zero G64 image for the 8 KiB raw-track buffer. */
mt_c64_g64_result_t mt_c64_g64_open(const uint8_t *data, size_t size,
                                    mt_c64_g64_image_t *image);

/*
 * Produce the raw-track layout consumed by c1541_direct_gcr: a little-endian
 * 16-bit track length with the fixed G64 speed zone in bits 14:13, followed
 * by that many GCR bytes, then 0xFF padding. The direct-GCR reader consumes
 * only length bits 12:0; the media wrapper uses the two speed bits.
 * A zero track-table entry produces an empty track with length zero.
 */
mt_c64_g64_result_t mt_c64_g64_read_track(
    const mt_c64_g64_image_t *image, uint8_t half_track,
    uint8_t *destination, size_t capacity, size_t *meaningful_size);

const char *mt_c64_g64_result_name(mt_c64_g64_result_t result);

#endif
