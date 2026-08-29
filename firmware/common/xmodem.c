/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_xmodem.h"

#include <stddef.h>
#include <stdint.h>

#define XMODEM_STX UINT8_C(0x02)
#define XMODEM_EOT UINT8_C(0x04)
#define XMODEM_ACK UINT8_C(0x06)
#define XMODEM_NAK UINT8_C(0x15)
#define XMODEM_CAN UINT8_C(0x18)
#define XMODEM_CRC_REQUEST UINT8_C(0x43)

uint16_t mt_xmodem_crc16_ccitt(const uint8_t *data, size_t size)
{
    uint16_t crc = 0U;
    size_t index;

    if (data == NULL && size != 0U) {
        return 0U;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t bit;
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & UINT16_C(0x8000)) != 0U) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ UINT16_C(0x1021));
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

uint32_t mt_xmodem_crc32_ieee(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t index;

    if (data == NULL && size != 0U) {
        return 0U;
    }
    for (index = 0U; index < size; ++index) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask =
                UINT32_C(0) - (crc & UINT32_C(1));
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xFFFFFFFF);
}

static int write_byte(const mt_xmodem_io_t *io, uint8_t value)
{
    return io->write_byte(io->context, value);
}

static void send_cancel(const mt_xmodem_io_t *io)
{
    (void)write_byte(io, XMODEM_CAN);
    (void)write_byte(io, XMODEM_CAN);
    (void)write_byte(io, XMODEM_CAN);
}

static int read_byte(const mt_xmodem_io_t *io, uint8_t *value,
                     uint32_t timeout_ms)
{
    return io->read_byte(io->context, value, timeout_ms);
}

static int receive_packet(const mt_xmodem_io_t *io, uint8_t *block_number,
                          uint8_t data[MT_XMODEM_1K_BLOCK_SIZE],
                          uint32_t timeout_ms)
{
    uint8_t inverse;
    uint8_t crc_high;
    uint8_t crc_low;
    uint16_t received_crc;
    uint16_t calculated_crc;
    size_t index;
    int status;

    status = read_byte(io, block_number, timeout_ms);
    if (status != 1) return status;
    status = read_byte(io, &inverse, timeout_ms);
    if (status != 1) return status;
    for (index = 0U; index < MT_XMODEM_1K_BLOCK_SIZE; ++index) {
        status = read_byte(io, &data[index], timeout_ms);
        if (status != 1) return status;
    }
    status = read_byte(io, &crc_high, timeout_ms);
    if (status != 1) return status;
    status = read_byte(io, &crc_low, timeout_ms);
    if (status != 1) return status;

    if ((uint8_t)(*block_number + inverse) != UINT8_C(0xFF)) {
        return 2;
    }
    received_crc = (uint16_t)(((uint16_t)crc_high << 8) | crc_low);
    calculated_crc = mt_xmodem_crc16_ccitt(data, MT_XMODEM_1K_BLOCK_SIZE);
    return received_crc == calculated_crc ? 1 : 2;
}

static mt_xmodem_result_t retry_or_abort(const mt_xmodem_io_t *io,
                                         uint32_t *retries,
                                         uint32_t max_retries)
{
    ++*retries;
    if (*retries > max_retries) {
        send_cancel(io);
        return MT_XMODEM_RETRY_LIMIT;
    }
    if (write_byte(io, XMODEM_NAK) != 0) {
        return MT_XMODEM_IO_ERROR;
    }
    return MT_XMODEM_OK;
}

mt_xmodem_result_t mt_xmodem_receive_1k(
    const mt_xmodem_io_t *io, uint8_t *destination, size_t capacity,
    uint32_t byte_timeout_ms, uint32_t max_retries, size_t *received_size)
{
    uint8_t block[MT_XMODEM_1K_BLOCK_SIZE];
    uint8_t expected_block = 1U;
    size_t received = 0U;
    uint32_t retries = 0U;
    int started = 0;

    if (received_size != NULL) {
        *received_size = 0U;
    }
    if (io == NULL || io->read_byte == NULL || io->write_byte == NULL ||
        destination == NULL || received_size == NULL || capacity == 0U ||
        (capacity % MT_XMODEM_1K_BLOCK_SIZE) != 0U ||
        byte_timeout_ms == 0U || max_retries == 0U) {
        return MT_XMODEM_BAD_ARGUMENT;
    }

    if (write_byte(io, XMODEM_CRC_REQUEST) != 0) {
        return MT_XMODEM_IO_ERROR;
    }

    for (;;) {
        uint8_t header;
        int status = read_byte(io, &header, byte_timeout_ms);

        if (status < 0) {
            send_cancel(io);
            *received_size = received;
            return MT_XMODEM_IO_ERROR;
        }
        if (status == 0) {
            ++retries;
            if (retries > max_retries) {
                send_cancel(io);
                *received_size = received;
                return MT_XMODEM_TIMEOUT;
            }
            if (write_byte(io, started != 0 ? XMODEM_NAK :
                           XMODEM_CRC_REQUEST) != 0) {
                *received_size = received;
                return MT_XMODEM_IO_ERROR;
            }
            continue;
        }

        if (header == XMODEM_CAN) {
            *received_size = received;
            return MT_XMODEM_CANCELLED;
        }
        if (header == XMODEM_EOT) {
            if (received != capacity) {
                send_cancel(io);
                *received_size = received;
                return MT_XMODEM_SIZE_MISMATCH;
            }
            if (write_byte(io, XMODEM_ACK) != 0) {
                *received_size = received;
                return MT_XMODEM_IO_ERROR;
            }
            *received_size = received;
            return MT_XMODEM_OK;
        }
        if (header != XMODEM_STX) {
            const mt_xmodem_result_t retry =
                retry_or_abort(io, &retries, max_retries);
            if (retry != MT_XMODEM_OK) {
                *received_size = received;
                return retry;
            }
            continue;
        }

        started = 1;
        {
            uint8_t block_number;
            const int packet_status = receive_packet(
                io, &block_number, block, byte_timeout_ms);

            if (packet_status < 0) {
                send_cancel(io);
                *received_size = received;
                return MT_XMODEM_IO_ERROR;
            }
            if (packet_status != 1) {
                const mt_xmodem_result_t retry =
                    retry_or_abort(io, &retries, max_retries);
                if (retry != MT_XMODEM_OK) {
                    *received_size = received;
                    return retry;
                }
                continue;
            }

            if (received != 0U &&
                block_number == (uint8_t)(expected_block - 1U)) {
                retries = 0U;
                if (write_byte(io, XMODEM_ACK) != 0) {
                    *received_size = received;
                    return MT_XMODEM_IO_ERROR;
                }
                continue;
            }
            if (block_number != expected_block) {
                send_cancel(io);
                *received_size = received;
                return MT_XMODEM_PROTOCOL_ERROR;
            }
            if (capacity - received < MT_XMODEM_1K_BLOCK_SIZE) {
                send_cancel(io);
                *received_size = received;
                return MT_XMODEM_SIZE_MISMATCH;
            }

            for (size_t index = 0U; index < MT_XMODEM_1K_BLOCK_SIZE;
                 ++index) {
                destination[received + index] = block[index];
            }
            received += MT_XMODEM_1K_BLOCK_SIZE;
            ++expected_block;
            retries = 0U;
            if (write_byte(io, XMODEM_ACK) != 0) {
                *received_size = received;
                return MT_XMODEM_IO_ERROR;
            }
        }
    }
}

const char *mt_xmodem_result_name(mt_xmodem_result_t result)
{
    switch (result) {
    case MT_XMODEM_OK: return "ok";
    case MT_XMODEM_BAD_ARGUMENT: return "bad argument";
    case MT_XMODEM_TIMEOUT: return "timeout";
    case MT_XMODEM_IO_ERROR: return "I/O error";
    case MT_XMODEM_CANCELLED: return "cancelled";
    case MT_XMODEM_PROTOCOL_ERROR: return "protocol error";
    case MT_XMODEM_SIZE_MISMATCH: return "size mismatch";
    case MT_XMODEM_RETRY_LIMIT: return "retry limit";
    }
    return "unknown";
}
