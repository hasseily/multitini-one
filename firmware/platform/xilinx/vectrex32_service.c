/* SPDX-License-Identifier: GPL-3.0-only */
#include "vectrex32_service.h"
#include "multitini_vectrex_boot.h"
#include "multitini_vectrex_interface.h"
#include "sd_storage.h"
#include "vectrex_content_service.h"
#include "../../vectrex32/vectrex32.h"
#include <string.h>

static mt_core_link_t *core;
static mt_vectrex32_platform_ops_t platform;
static const char *asset_root;
static mt_sd_file_t *files[16];
static char error_text[256];
static uint64_t next_ms;
static unsigned frame_remainder;
static unsigned previous_hz;
static int transfer_failed;
static int stream_cancelled;
static int cleanup;
static const char *backend_error;

static int service(void *context)
{
    (void)context;
    return !transfer_failed && !stream_cancelled && backend_error == NULL &&
        platform.service != NULL && platform.service(platform.context);
}

static int stream_continue(void *context)
{
    (void)context;
    if (platform.stream_service != NULL && !platform.stream_service(platform.context))
        stream_cancelled = 1;
    return !stream_cancelled;
}

static int stream(uint8_t kind, const uint8_t *data, size_t size)
{
    const mt_link_result_t result = mt_core_link_stream_until(core, kind, data, size,
        cleanup ? UINT32_C(1000) : UINT32_C(100000), NULL, NULL,
        cleanup ? NULL : stream_continue, NULL);
    if (result != MT_LINK_OK) {
        if (cleanup || !stream_cancelled) transfer_failed = 1;
        return 0;
    }
    return 1;
}

static void put16(uint8_t *target, int32_t value)
{
    uint16_t bits;
    if (value < INT16_MIN) value = INT16_MIN;
    if (value > INT16_MAX) value = INT16_MAX;
    bits = (uint16_t)value;
    target[0] = (uint8_t)bits; target[1] = (uint8_t)(bits >> 8U);
}

static int solid_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t intensity)
{
    uint8_t packet[9];
    put16(packet, x0); put16(packet + 2U, y0);
    put16(packet + 4U, x1); put16(packet + 6U, y1);
    if (intensity > 127U) intensity = 127U;
    packet[8] = (uint8_t)((intensity << 1U) | (intensity >> 6U));
    return stream(MT_LOAD_VECTREX_LINE, packet, sizeof(packet));
}

static int line(void *context, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
    uint8_t intensity, uint8_t pattern)
{
    unsigned bit;
    (void)context;
    if (!service(NULL)) return 0;
    if (pattern == 255U) return solid_line(x0, y0, x1, y1, intensity);
    for (bit = 0U; bit < 8U; ++bit) {
        if ((pattern & (uint8_t)(0x80U >> bit)) != 0U &&
            !solid_line((int32_t)(x0 + ((int64_t)x1 - x0) * bit / 8),
                (int32_t)(y0 + ((int64_t)y1 - y0) * bit / 8),
                (int32_t)(x0 + ((int64_t)x1 - x0) * (bit + 1U) / 8),
                (int32_t)(y0 + ((int64_t)y1 - y0) * (bit + 1U) / 8), intensity)) return 0;
    }
    return 1;
}

static int frame(void *context, unsigned hz, int controls[2][6])
{
    unsigned player;
    uint64_t now;
    (void)context;
    if (hz == 0U || hz > 150U || platform.milliseconds == NULL) {
        backend_error = "Invalid frame cadence"; return 0;
    }
    if (previous_hz != hz) { frame_remainder = 0U; previous_hz = hz; }
    frame_remainder += 1000U;
    next_ms += frame_remainder / hz;
    frame_remainder %= hz;
    do {
        if (!service(NULL)) return 0;
        now = platform.milliseconds(platform.context);
    } while (now < next_ms);
    /* A late frame does not trigger an unbounded catch-up burst. */
    if (now - next_ms > 1000U / hz) next_ms = now;
    memset(controls, 0, sizeof(int) * 12U);
    if (platform.controls != NULL) platform.controls(platform.context, controls);
    for (player = 0U; player < 2U; ++player) {
        controls[player][0] /= 256;
        controls[player][1] /= 256;
    }
    return 1;
}

static int psg(void *context, unsigned reg, uint8_t value)
{
    uint8_t packet[2];
    (void)context;
    if (reg >= 16U) return 0;
    packet[0] = (uint8_t)reg; packet[1] = value;
    return stream(MT_LOAD_VECTREX_PSG, packet, sizeof(packet));
}

static void text(void *context, const char *message)
{
    (void)context;
    if (platform.text != NULL) platform.text(platform.context, message);
}

static int rooted_path(const char *name, char path[512])
{
    size_t prefix, length;
    size_t offset;
    if (asset_root == NULL || !mt_v32_valid_relative_path(name)) return -1;
    prefix = strlen(asset_root); length = strlen(name);
    if (prefix + length + 2U > 512U) return -1;
    memcpy(path, asset_root, prefix); path[prefix] = '/';
    memcpy(path + prefix + 1U, name, length + 1U);
    for (offset = prefix + 1U; path[offset] != '\0'; ++offset)
        if (path[offset] == '\\') path[offset] = '/';
    return 0;
}

static void *file_open(void *context, const char *name, const char *mode)
{
    char path[512];
    unsigned slot;
    (void)context;
    if (rooted_path(name, path) != 0) return NULL;
    for (slot = 0U; slot < 16U && files[slot] != NULL; ++slot) { }
    if (slot == 16U || mt_sd_file_open(path, mode, &files[slot]).status != MT_SD_OK) return NULL;
    return files[slot];
}

static int file_read(void *context, void *handle, void *data, size_t capacity, size_t *received)
{
    mt_sd_result_t result;
    (void)context;
    result = mt_sd_file_read(handle, data, capacity); *received = result.size;
    return result.status == MT_SD_OK ? 0 : -1;
}
static int file_write(void *context, void *handle, const void *data, size_t size, size_t *written)
{
    mt_sd_result_t result;
    (void)context;
    result = mt_sd_file_write(handle, data, size); *written = result.size;
    return result.status == MT_SD_OK ? 0 : -1;
}
static int file_seek(void *context, void *handle, int64_t offset, int origin)
{
    (void)context;
    return mt_sd_file_seek(handle, offset, origin).status == MT_SD_OK ? 0 : -1;
}
static int64_t file_tell(void *context, void *handle)
{ (void)context; return mt_sd_file_tell(handle); }
static int file_eof(void *context, void *handle)
{ (void)context; return mt_sd_file_eof(handle); }
static int file_close(void *context, void *handle)
{
    unsigned slot;
    mt_sd_result_t result;
    (void)context;
    result = mt_sd_file_close(handle);
    if (result.status == MT_SD_OK || mt_sd_file_tell(handle) < 0)
        for (slot = 0U; slot < 16U; ++slot) if (files[slot] == handle) files[slot] = NULL;
    return result.status == MT_SD_OK ? 0 : -1;
}
static int file_remove(void *context, const char *name)
{
    char path[512];
    (void)context;
    return rooted_path(name, path) == 0 && mt_sd_file_remove(path).status == MT_SD_OK ? 0 : -1;
}
static int file_rename(void *context, const char *old_name, const char *new_name)
{
    char old_path[512], new_path[512];
    (void)context;
    return rooted_path(old_name, old_path) == 0 && rooted_path(new_name, new_path) == 0 &&
        mt_sd_file_rename(old_path, new_path).status == MT_SD_OK ? 0 : -1;
}
static int read_bios(void *context, uint16_t address, uint8_t *data, size_t count)
{
    (void)context;
    return vectrex_content_service_read_bios(address, data, count);
}

static uint64_t monotonic_milliseconds(void *context)
{ (void)context; return platform.milliseconds(platform.context); }

static int console_read_byte(void *context)
{ (void)context; return platform.console_read_byte(platform.context); }
static int console_write(void *context, const void *data, size_t size)
{ (void)context; return platform.console_write(platform.context, data, size); }

void vectrex32_service_init(mt_core_link_t *link, const mt_vectrex32_platform_ops_t *ops)
{
    core = link;
    memset(&platform, 0, sizeof(platform));
    if (ops != NULL) platform = *ops;
}

int vectrex32_service_run(void *context, const char *root, const char *name, const char *source)
{
    mt_v32_callbacks callbacks;
    mt_v32_result result;
    mt_command_response_t response;
    uint32_t args[4] = {1U, 0U, 0U, 0U};
    int status;
    unsigned slot;
    (void)context;
    error_text[0] = '\0';
    if (core == NULL || core->read32 == NULL || core->write32 == NULL ||
        platform.milliseconds == NULL || platform.service == NULL) {
        memcpy(error_text, "BASIC platform is not initialized", 34U);
        return -1;
    }
    if (root == NULL || name == NULL || source == NULL) {
        memcpy(error_text, "Invalid BASIC program arguments", 32U); return -1;
    }
    for (slot = 0U; slot < 16U; ++slot)
        if (files[slot] != NULL && file_close(NULL, files[slot]) != 0) {
            memcpy(error_text, "File close failed", 18U); return -1;
        }
    core->write32(core->context, MT_REG_ERROR, MT_ERROR_CORE_LOAD);
    if (mt_core_link_command(core, MT_COMMAND_CORE_RESET, args, 100000U, NULL, NULL,
        &response) != MT_LINK_OK || mt_vectrex_set_mode(core, 1U, 100000U, NULL, NULL) != MT_LINK_OK) {
        memcpy(error_text, "Unable to enter BASIC mode", 27U);
        return -1;
    }
    args[0] = 0U;
    if (mt_core_link_command(core, MT_COMMAND_CORE_RESET, args, 100000U, NULL, NULL,
        &response) != MT_LINK_OK) {
        memcpy(error_text, "Unable to release BASIC reset", 30U); return -1;
    }
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.service = service; callbacks.begin_frame = service;
    callbacks.line = line; callbacks.frame = frame; callbacks.psg = psg;
    callbacks.text = text; callbacks.read_bios = read_bios;
    callbacks.file_open = file_open; callbacks.file_read = file_read;
    callbacks.file_write = file_write; callbacks.file_seek = file_seek;
    callbacks.file_tell = file_tell; callbacks.file_eof = file_eof;
    callbacks.file_close = file_close; callbacks.file_remove = file_remove;
    callbacks.file_rename = file_rename;
    callbacks.monotonic_milliseconds = monotonic_milliseconds;
    if (platform.console_read_byte != NULL) callbacks.console_read_byte = console_read_byte;
    if (platform.console_write != NULL) callbacks.console_write = console_write;
    asset_root = root; transfer_failed = 0; stream_cancelled = 0; cleanup = 0;
    backend_error = NULL; frame_remainder = 0U; previous_hz = 0U;
    next_ms = platform.milliseconds(platform.context);
    status = mt_v32_run(source, name, &callbacks, &result);
    for (slot = 0U; slot < 16U; ++slot)
        if (files[slot] != NULL && file_close(NULL, files[slot]) != 0)
            backend_error = "File close failed";
    asset_root = NULL;
    memcpy(error_text, result.error, sizeof(error_text)); error_text[255] = '\0';
    /* Silence all three channels after end, error, or deliberate F12 exit. */
    cleanup = 1;
    for (slot = 8U; slot <= 10U; ++slot) (void)psg(NULL, slot, 0U);
    cleanup = 0;
    if (backend_error != NULL) {
        strncpy(error_text, backend_error, sizeof(error_text) - 1U); status = -1;
    }
    if (transfer_failed) {
        memcpy(error_text, "Core stream transfer failed", 28U); status = -1;
    }
    return status;
}

const char *vectrex32_service_error(void) { return error_text; }
