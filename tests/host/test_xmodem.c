/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_xmodem.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TEST_EVENT_CAPACITY 5000U
#define TEST_OUTPUT_CAPACITY 64U
#define TEST_EVENT_TIMEOUT UINT16_C(0x0100)
#define TEST_EVENT_IO_ERROR UINT16_C(0x0101)

#define XMODEM_STX UINT8_C(0x02)
#define XMODEM_EOT UINT8_C(0x04)
#define XMODEM_ACK UINT8_C(0x06)
#define XMODEM_NAK UINT8_C(0x15)
#define XMODEM_CAN UINT8_C(0x18)
#define XMODEM_CRC_REQUEST UINT8_C(0x43)

typedef struct {
    uint16_t events[TEST_EVENT_CAPACITY];
    size_t event_count;
    size_t event_offset;
    uint8_t output[TEST_OUTPUT_CAPACITY];
    size_t output_count;
    uint32_t expected_timeout_ms;
} fake_io_t;

static int fake_read(void *context, uint8_t *value, uint32_t timeout_ms)
{
    fake_io_t *const fake = context;
    uint16_t event;

    assert(value != NULL);
    assert(timeout_ms == fake->expected_timeout_ms);
    if (fake->event_offset >= fake->event_count) {
        return 0;
    }
    event = fake->events[fake->event_offset++];
    if (event == TEST_EVENT_TIMEOUT) return 0;
    if (event == TEST_EVENT_IO_ERROR) return -1;
    assert(event <= UINT8_MAX);
    *value = (uint8_t)event;
    return 1;
}

static int fake_write(void *context, uint8_t value)
{
    fake_io_t *const fake = context;
    assert(fake->output_count < TEST_OUTPUT_CAPACITY);
    fake->output[fake->output_count++] = value;
    return 0;
}

static void append_event(fake_io_t *fake, uint16_t event)
{
    assert(fake->event_count < TEST_EVENT_CAPACITY);
    fake->events[fake->event_count++] = event;
}

static void fill_block(uint8_t block[MT_XMODEM_1K_BLOCK_SIZE], uint8_t seed)
{
    size_t index;
    for (index = 0U; index < MT_XMODEM_1K_BLOCK_SIZE; ++index) {
        block[index] = (uint8_t)(seed + (uint8_t)index);
    }
}

static void append_block(fake_io_t *fake, uint8_t number, uint8_t seed,
                         int corrupt_crc)
{
    uint8_t block[MT_XMODEM_1K_BLOCK_SIZE];
    uint16_t crc;
    size_t index;

    fill_block(block, seed);
    crc = mt_xmodem_crc16_ccitt(block, sizeof(block));
    if (corrupt_crc != 0) crc ^= UINT16_C(1);

    append_event(fake, XMODEM_STX);
    append_event(fake, number);
    append_event(fake, (uint8_t)(UINT8_C(0xFF) - number));
    for (index = 0U; index < sizeof(block); ++index) {
        append_event(fake, block[index]);
    }
    append_event(fake, (uint8_t)(crc >> 8));
    append_event(fake, (uint8_t)crc);
}

static mt_xmodem_io_t make_io(fake_io_t *fake)
{
    mt_xmodem_io_t io;
    io.context = fake;
    io.read_byte = fake_read;
    io.write_byte = fake_write;
    return io;
}

static void check_output(const fake_io_t *fake, const uint8_t *expected,
                         size_t expected_size)
{
    assert(fake->output_count == expected_size);
    assert(memcmp(fake->output, expected, expected_size) == 0);
}

static void test_checksums(void)
{
    static const uint8_t vector[] = "123456789";
    assert(mt_xmodem_crc16_ccitt(vector, sizeof(vector) - 1U) ==
           UINT16_C(0x31C3));
    assert(mt_xmodem_crc32_ieee(vector, sizeof(vector) - 1U) ==
           UINT32_C(0xCBF43926));
    assert(mt_xmodem_crc16_ccitt(NULL, 1U) == 0U);
    assert(mt_xmodem_crc32_ieee(NULL, 1U) == 0U);
}

static void test_success_and_duplicate(void)
{
    fake_io_t fake = {0};
    mt_xmodem_io_t io;
    uint8_t destination[2U * MT_XMODEM_1K_BLOCK_SIZE];
    uint8_t expected_block[MT_XMODEM_1K_BLOCK_SIZE];
    size_t received = 0U;
    static const uint8_t expected_output[] = {
        XMODEM_CRC_REQUEST, XMODEM_ACK, XMODEM_ACK, XMODEM_ACK, XMODEM_ACK
    };

    fake.expected_timeout_ms = 1000U;
    append_block(&fake, 1U, 0x10U, 0);
    append_block(&fake, 1U, 0x10U, 0);
    append_block(&fake, 2U, 0x80U, 0);
    append_event(&fake, XMODEM_EOT);
    io = make_io(&fake);

    assert(mt_xmodem_receive_1k(&io, destination, sizeof(destination),
                                1000U, 3U, &received) == MT_XMODEM_OK);
    assert(received == sizeof(destination));
    fill_block(expected_block, 0x10U);
    assert(memcmp(destination, expected_block, sizeof(expected_block)) == 0);
    fill_block(expected_block, 0x80U);
    assert(memcmp(destination + MT_XMODEM_1K_BLOCK_SIZE, expected_block,
                  sizeof(expected_block)) == 0);
    check_output(&fake, expected_output, sizeof(expected_output));
}

static void test_crc_retry_and_start_timeout(void)
{
    fake_io_t fake = {0};
    mt_xmodem_io_t io;
    uint8_t destination[MT_XMODEM_1K_BLOCK_SIZE];
    size_t received = 0U;
    static const uint8_t expected_output[] = {
        XMODEM_CRC_REQUEST, XMODEM_CRC_REQUEST, XMODEM_NAK,
        XMODEM_ACK, XMODEM_ACK
    };

    fake.expected_timeout_ms = 250U;
    append_event(&fake, TEST_EVENT_TIMEOUT);
    append_block(&fake, 1U, 0x44U, 1);
    append_block(&fake, 1U, 0x44U, 0);
    append_event(&fake, XMODEM_EOT);
    io = make_io(&fake);

    assert(mt_xmodem_receive_1k(&io, destination, sizeof(destination),
                                250U, 3U, &received) == MT_XMODEM_OK);
    assert(received == sizeof(destination));
    check_output(&fake, expected_output, sizeof(expected_output));
}

static void test_cancel_and_size_errors(void)
{
    uint8_t destination[2U * MT_XMODEM_1K_BLOCK_SIZE];
    size_t received;
    mt_xmodem_io_t io;

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {XMODEM_CRC_REQUEST};
        fake.expected_timeout_ms = 1000U;
        append_event(&fake, XMODEM_CAN);
        io = make_io(&fake);
        received = 99U;
        assert(mt_xmodem_receive_1k(&io, destination,
                                    MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                    &received) == MT_XMODEM_CANCELLED);
        assert(received == 0U);
        check_output(&fake, expected, sizeof(expected));
    }

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {
            XMODEM_CRC_REQUEST, XMODEM_ACK,
            XMODEM_CAN, XMODEM_CAN, XMODEM_CAN
        };
        fake.expected_timeout_ms = 1000U;
        append_block(&fake, 1U, 0x11U, 0);
        append_event(&fake, XMODEM_EOT);
        io = make_io(&fake);
        assert(mt_xmodem_receive_1k(&io, destination, sizeof(destination),
                                    1000U, 2U, &received) ==
               MT_XMODEM_SIZE_MISMATCH);
        assert(received == MT_XMODEM_1K_BLOCK_SIZE);
        check_output(&fake, expected, sizeof(expected));
    }

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {
            XMODEM_CRC_REQUEST, XMODEM_ACK,
            XMODEM_CAN, XMODEM_CAN, XMODEM_CAN
        };
        fake.expected_timeout_ms = 1000U;
        append_block(&fake, 1U, 0x22U, 0);
        append_block(&fake, 2U, 0x33U, 0);
        io = make_io(&fake);
        assert(mt_xmodem_receive_1k(&io, destination,
                                    MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                    &received) == MT_XMODEM_SIZE_MISMATCH);
        assert(received == MT_XMODEM_1K_BLOCK_SIZE);
        check_output(&fake, expected, sizeof(expected));
    }
}

static void test_protocol_and_argument_errors(void)
{
    fake_io_t valid_fake = {0};
    uint8_t destination[MT_XMODEM_1K_BLOCK_SIZE + 1U];
    size_t received = 0U;
    mt_xmodem_io_t io;

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {
            XMODEM_CRC_REQUEST, XMODEM_CAN, XMODEM_CAN, XMODEM_CAN
        };
        fake.expected_timeout_ms = 1000U;
        append_block(&fake, 2U, 0x55U, 0);
        io = make_io(&fake);
        assert(mt_xmodem_receive_1k(&io, destination,
                                    MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                    &received) == MT_XMODEM_PROTOCOL_ERROR);
        check_output(&fake, expected, sizeof(expected));
    }

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {
            XMODEM_CRC_REQUEST, XMODEM_CAN, XMODEM_CAN, XMODEM_CAN
        };
        fake.expected_timeout_ms = 1000U;
        append_block(&fake, 0U, 0x66U, 0);
        io = make_io(&fake);
        assert(mt_xmodem_receive_1k(&io, destination,
                                    MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                    &received) == MT_XMODEM_PROTOCOL_ERROR);
        assert(received == 0U);
        check_output(&fake, expected, sizeof(expected));
    }

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {
            XMODEM_CRC_REQUEST, XMODEM_CAN, XMODEM_CAN, XMODEM_CAN
        };
        fake.expected_timeout_ms = 1000U;
        append_event(&fake, TEST_EVENT_IO_ERROR);
        io = make_io(&fake);
        assert(mt_xmodem_receive_1k(&io, destination,
                                    MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                    &received) == MT_XMODEM_IO_ERROR);
        assert(received == 0U);
        check_output(&fake, expected, sizeof(expected));
    }

    {
        fake_io_t fake = {0};
        static const uint8_t expected[] = {
            XMODEM_CRC_REQUEST, XMODEM_NAK, XMODEM_NAK,
            XMODEM_CAN, XMODEM_CAN, XMODEM_CAN
        };
        fake.expected_timeout_ms = 1000U;
        append_event(&fake, UINT16_C(0x007F));
        append_event(&fake, UINT16_C(0x007E));
        append_event(&fake, UINT16_C(0x007D));
        io = make_io(&fake);
        assert(mt_xmodem_receive_1k(&io, destination,
                                    MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                    &received) == MT_XMODEM_RETRY_LIMIT);
        assert(received == 0U);
        check_output(&fake, expected, sizeof(expected));
    }

    assert(mt_xmodem_receive_1k(NULL, destination,
                                MT_XMODEM_1K_BLOCK_SIZE, 1000U, 2U,
                                &received) == MT_XMODEM_BAD_ARGUMENT);
    valid_fake.expected_timeout_ms = 1000U;
    io = make_io(&valid_fake);
    assert(mt_xmodem_receive_1k(&io, destination, sizeof(destination),
                                1000U, 2U, &received) ==
           MT_XMODEM_BAD_ARGUMENT);
    assert(strcmp(mt_xmodem_result_name(MT_XMODEM_RETRY_LIMIT),
                  "retry limit") == 0);
}

int main(void)
{
    test_checksums();
    test_success_and_duplicate();
    test_crc_retry_and_start_timeout();
    test_cancel_and_size_errors();
    test_protocol_and_argument_errors();
    return 0;
}
