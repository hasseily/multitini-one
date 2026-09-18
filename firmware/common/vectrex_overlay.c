/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_vectrex_overlay.h"
#include "multitini_vectrex_interface.h"

#include <string.h>

bool mt_vectrex_overlay_valid_size(size_t size)
{
    return size != 0U && size <= MT_VECTREX_OVERLAY_BYTES && (size & 1U) == 0U;
}

bool mt_vectrex_overlay_pixel(const uint8_t *data, size_t size,
    unsigned x, unsigned y, mt_vectrex_overlay_rgba_t *output)
{
    size_t offset;
    mt_vectrex_overlay_rgba_t pixel = {0U, 0U, 0U, 0U};
    if (data == NULL || output == NULL || !mt_vectrex_overlay_valid_size(size) ||
        x >= MT_VECTREX_OVERLAY_WIDTH || y >= MT_VECTREX_OVERLAY_HEIGHT)
        return false;
    offset = ((size_t)y * MT_VECTREX_OVERLAY_WIDTH + x) * 2U;
    if (offset < size) {
        pixel.red = (uint8_t)((data[offset] & 15U) * 17U);
        pixel.green = (uint8_t)((data[offset] >> 4U) * 17U);
        pixel.blue = (uint8_t)((data[offset + 1U] & 15U) * 17U);
        pixel.alpha = (uint8_t)((data[offset + 1U] >> 4U) * 17U);
    }
    *output = pixel;
    return true;
}

bool mt_vectrex_overlay_sidecar_path(const char *relative,
    char *output, size_t capacity)
{
    size_t length, component = 0U, extension = SIZE_MAX;
    if (relative == NULL || output == NULL) return false;
    for (length = 0U; length <= MT_VECTREX_OVERLAY_PATH_MAX; ++length) {
        const unsigned char ch = (unsigned char)relative[length];
        if (ch == '\0' || ch == '/') {
            if (length == component || relative[length - 1U] == '.' ||
                relative[length - 1U] == ' ') return false;
            if (ch == '\0') break;
            component = length + 1U;
            extension = SIZE_MAX;
        } else {
            if (ch < 32U || ch == 127U || strchr("\\:*?\"<>|", (int)ch) != NULL)
                return false;
            if (ch == '.' && length != component) extension = length;
        }
    }
    if (length > MT_VECTREX_OVERLAY_PATH_MAX) return false;
    if (extension != SIZE_MAX) length = extension;
    if (length > MT_VECTREX_OVERLAY_PATH_MAX - 4U || capacity <= length + 4U)
        return false;
    memmove(output, relative, length);
    memcpy(output + length, ".ovr", 5U);
    return true;
}

mt_link_result_t mt_vectrex_overlay_load(mt_core_link_t *link,
    uint8_t *buffer, size_t capacity, size_t payload_size,
    uint32_t max_polls, mt_link_progress_fn progress, void *context)
{
    if (link == NULL || link->read32 == NULL || link->write32 == NULL ||
        buffer == NULL || capacity < MT_VECTREX_OVERLAY_BYTES ||
        !mt_vectrex_overlay_valid_size(payload_size) || max_polls == 0U)
        return MT_LINK_BAD_ARGUMENT;
    memset(buffer + payload_size, 0, MT_VECTREX_OVERLAY_BYTES - payload_size);
    return mt_core_link_stream(link, MT_LOAD_VECTREX_OVERLAY,
        buffer, MT_VECTREX_OVERLAY_BYTES, max_polls, progress, context);
}

mt_link_result_t mt_vectrex_overlay_clear(mt_core_link_t *link,
    uint32_t max_polls, mt_link_progress_fn progress, void *context)
{
    const uint8_t clear = 0U;
    return mt_core_link_stream(link, MT_LOAD_VECTREX_OVERLAY,
        &clear, 1U, max_polls, progress, context);
}
