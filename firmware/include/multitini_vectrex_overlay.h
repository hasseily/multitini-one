/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_VECTREX_OVERLAY_H
#define MULTITINI_VECTREX_OVERLAY_H

#include <stdbool.h>
#include "multitini_core_link.h"

#define MT_VECTREX_OVERLAY_WIDTH 540U
#define MT_VECTREX_OVERLAY_HEIGHT 720U
#define MT_VECTREX_OVERLAY_BYTES (MT_VECTREX_OVERLAY_WIDTH * MT_VECTREX_OVERLAY_HEIGHT * 2U)
#define MT_VECTREX_OVERLAY_PATH_MAX 255U

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
} mt_vectrex_overlay_rgba_t;

/* OVR is headerless, top-left row-major ABGR4444 in little-endian words.
 * Positive even files up to the full image size are accepted. Short files
 * retain their raw linear addresses; their missing tail is transparent black. */
bool mt_vectrex_overlay_valid_size(size_t size);
/* Expand one pixel to eight-bit channels. Alpha-zero RGB is preserved because
 * the display can use it to tint the beam. Invalid input leaves output intact. */
bool mt_vectrex_overlay_pixel(const uint8_t *data, size_t size,
    unsigned x, unsigned y, mt_vectrex_overlay_rgba_t *output);
/* Validate a confined forward-slash relative path and replace the last
 * filename extension with .ovr. Preserve nesting, stem, and case; FAT resolves
 * extension case. Extensionless names gain .ovr. A leading dot is part of the
 * stem. Paths and results are at most PATH_MAX bytes. Failure preserves output;
 * input and output may overlap. */
bool mt_vectrex_overlay_sidecar_path(const char *relative,
    char *output, size_t capacity);
/* The caller owns a buffer of at least BYTES bytes, containing payload_size
 * file bytes. Validate before modifying it, zero the tail, then stream exactly
 * BYTES bytes without allocating another image. Gate core/ABI support before
 * calling. A failed transfer must not be presented as an installed overlay. */
mt_link_result_t mt_vectrex_overlay_load(mt_core_link_t *link,
    uint8_t *buffer, size_t capacity, size_t payload_size,
    uint32_t max_polls, mt_link_progress_fn progress, void *context);
/* One zero byte is the explicit clear command, not an OVR image. */
mt_link_result_t mt_vectrex_overlay_clear(mt_core_link_t *link,
    uint32_t max_polls, mt_link_progress_fn progress, void *context);

#endif
