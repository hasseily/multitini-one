/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_XMODEM_H
#define MULTITINI_XMODEM_H

#include <stddef.h>
#include <stdint.h>

#define MT_XMODEM_1K_BLOCK_SIZE ((size_t)1024)

typedef int (*mt_xmodem_read_byte_fn)(void *context, uint8_t *value,
                                      uint32_t timeout_ms);
typedef int (*mt_xmodem_write_byte_fn)(void *context, uint8_t value);

typedef struct {
    void *context;
    mt_xmodem_read_byte_fn read_byte;
    mt_xmodem_write_byte_fn write_byte;
} mt_xmodem_io_t;

typedef enum {
    MT_XMODEM_OK = 0,
    MT_XMODEM_BAD_ARGUMENT = -1,
    MT_XMODEM_TIMEOUT = -2,
    MT_XMODEM_IO_ERROR = -3,
    MT_XMODEM_CANCELLED = -4,
    MT_XMODEM_PROTOCOL_ERROR = -5,
    MT_XMODEM_SIZE_MISMATCH = -6,
    MT_XMODEM_RETRY_LIMIT = -7
} mt_xmodem_result_t;

uint16_t mt_xmodem_crc16_ccitt(const uint8_t *data, size_t size);
uint32_t mt_xmodem_crc32_ieee(const uint8_t *data, size_t size);

/*
 * Receive an exact number of bytes using 1024-byte XMODEM-CRC packets.
 * Capacity must be a nonzero multiple of 1024, so no padding is accepted or
 * discarded. The callback returns 1 for a byte, 0 for timeout, and a negative
 * value for an I/O error. Local protocol failures send CAN three times.
 */
mt_xmodem_result_t mt_xmodem_receive_1k(
    const mt_xmodem_io_t *io, uint8_t *destination, size_t capacity,
    uint32_t byte_timeout_ms, uint32_t max_retries, size_t *received_size);

const char *mt_xmodem_result_name(mt_xmodem_result_t result);

#endif
