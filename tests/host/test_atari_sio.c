/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_atari_sio.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    mt_atari_sio_geometry_t geometry;
    unsigned reads;
    uint16_t last_sector, last_bytes;
    uint8_t unit, last_unit;
    bool read_error;
} disk_fixture_t;

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static uint8_t checksum(const uint8_t *data, size_t length)
{
    unsigned sum = 0U;
    for (size_t i = 0U; i < length; ++i) sum += data[i];
    /* End-around carry reduces a nonzero sum to the range 1..255. */
    return sum == 0U ? 0U : (uint8_t)((sum - 1U) % 255U + 1U);
}

static uint8_t sector_byte(size_t index)
{
    return (uint8_t)(index * 7U + 0x91U);
}

static bool disk_geometry(void *context, uint8_t unit, mt_atari_sio_geometry_t *geometry)
{
    const disk_fixture_t *disk = context;
    if (unit != disk->unit) return false;
    *geometry = disk->geometry;
    return true;
}

static bool disk_read(void *context, uint8_t unit, uint16_t sector, uint8_t *data, uint16_t bytes)
{
    disk_fixture_t *disk = context;
    ++disk->reads;
    disk->last_unit = unit;
    disk->last_sector = sector;
    disk->last_bytes = bytes;
    for (size_t i = 0U; i < bytes; ++i) data[i] = sector_byte(i);
    return !disk->read_error;
}

static void make_frame(uint8_t frame[5], uint8_t device, uint8_t operation, uint16_t sector)
{
    frame[0] = device;
    frame[1] = operation;
    frame[2] = (uint8_t)sector;
    frame[3] = (uint8_t)(sector >> 8U);
    frame[4] = checksum(frame, 4U);
}

static void receive_bytes(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk, uint8_t epoch,
                          const uint8_t *data, size_t length)
{
    for (size_t i = 0U; i < length; ++i)
        mt_atari_sio_receive(sio, disk, epoch, MT_ATARI_SIO_BYTE, data[i]);
}

static void receive_frame(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk, uint8_t epoch,
                          const uint8_t *frame, size_t length)
{
    mt_atari_sio_receive(sio, disk, epoch, MT_ATARI_SIO_COMMAND_BEGIN, 0U);
    receive_bytes(sio, disk, epoch, frame, length);
    mt_atari_sio_receive(sio, disk, epoch, MT_ATARI_SIO_COMMAND_END, 0U);
}

static void command(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk, uint8_t epoch,
                    uint8_t device, uint8_t operation, uint16_t sector)
{
    uint8_t frame[5];
    make_frame(frame, device, operation, sector);
    receive_frame(sio, disk, epoch, frame, sizeof(frame));
}

static void expect_reply(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk, uint8_t epoch,
                         uint8_t byte, bool gap)
{
    mt_atari_sio_reply_t reply;
    check(mt_atari_sio_next_reply(sio, disk, &reply), "reply byte is available");
    if (reply.byte != byte || reply.epoch != epoch || reply.gap != gap) {
        fprintf(stderr, "FAIL: expected reply %02x epoch %u gap %u, got %02x epoch %u gap %u\n",
                (unsigned)byte, (unsigned)epoch, (unsigned)gap,
                (unsigned)reply.byte, (unsigned)reply.epoch, (unsigned)reply.gap);
        exit(EXIT_FAILURE);
    }
}

static void expect_idle(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk)
{
    mt_atari_sio_reply_t reply;
    check(!mt_atari_sio_next_reply(sio, disk, &reply), "no unexpected reply bytes");
}

static void expect_data(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk, uint8_t epoch,
                        const uint8_t *data, size_t length)
{
    for (size_t i = 0U; i < length; ++i) expect_reply(sio, disk, epoch, data[i], false);
    expect_reply(sio, disk, epoch, checksum(data, length), false);
    expect_idle(sio, disk);
}

static void test_status(void)
{
    const struct {
        mt_atari_sio_geometry_t geometry;
        uint8_t flags;
    } cases[] = {{{720U, 128U}, 0x18U}, {{720U, 256U}, 0x38U}, {{1040U, 128U}, 0x98U}};
    disk_fixture_t fixture = {.unit = 1U};
    const mt_atari_sio_disk_t disk = {&fixture, disk_geometry, disk_read};
    mt_atari_sio_t sio = {0};
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        fixture.geometry = cases[i].geometry;
        const uint8_t status[] = {cases[i].flags, 0xffU, 1U, 0U};
        command(&sio, &disk, (uint8_t)i, 0x31U, 'S', 0U);
        expect_reply(&sio, &disk, (uint8_t)i, 'A', false);
        expect_reply(&sio, &disk, (uint8_t)i, 'C', true);
        expect_data(&sio, &disk, (uint8_t)i, status, sizeof(status));
    }
    const struct {
        mt_atari_sio_geometry_t geometry;
        uint8_t percom[12];
    } percom_cases[] = {
        {{1440U, 256U}, {40U, 1U, 0U, 18U, 1U, 4U, 1U, 0U, 1U, 0U, 0U, 0U}},
        {{1040U, 128U}, {40U, 1U, 0U, 26U, 0U, 4U, 0U, 128U, 1U, 0U, 0U, 0U}}
    };
    for (size_t i = 0U; i < sizeof(percom_cases) / sizeof(percom_cases[0]); ++i) {
        const uint8_t epoch = (uint8_t)(3U + i);
        fixture.geometry = percom_cases[i].geometry;
        command(&sio, &disk, epoch, 0x31U, 'N', 0U);
        expect_reply(&sio, &disk, epoch, 'A', false);
        expect_reply(&sio, &disk, epoch, 'C', true);
        expect_data(&sio, &disk, epoch, percom_cases[i].percom, sizeof(percom_cases[i].percom));
    }
    check(fixture.reads == 0U, "status and geometry do not read disk sectors");
}

static void test_reads(void)
{
    disk_fixture_t fixture = {.geometry = {720U, 256U}, .unit = 2U};
    const mt_atari_sio_disk_t disk = {&fixture, disk_geometry, disk_read};
    mt_atari_sio_t sio = {0};
    const uint16_t sectors[] = {3U, 500U, 720U};
    for (size_t i = 0U; i < sizeof(sectors) / sizeof(sectors[0]); ++i) {
        uint8_t data[256];
        const uint16_t bytes = i == 0U ? 128U : 256U;
        fixture.read_error = i == 2U;
        for (size_t j = 0U; j < bytes; ++j) data[j] = fixture.read_error ? 0U : sector_byte(j);
        const uint8_t epoch = (uint8_t)(0x80U + i);
        command(&sio, &disk, epoch, 0x32U, 'R', sectors[i]);
        check(fixture.reads == i, "sector I/O is deferred when the command completes");
        expect_reply(&sio, &disk, epoch, 'A', false);
        check(fixture.reads == i, "ACK is returned before backend I/O");
        expect_reply(&sio, &disk, epoch, fixture.read_error ? 'E' : 'C', true);
        check(fixture.reads == i + 1U && fixture.last_unit == 2U &&
              fixture.last_sector == sectors[i] && fixture.last_bytes == bytes,
              "sector I/O uses the command unit, little-endian sector, and density length");
        expect_data(&sio, &disk, epoch, data, bytes);
        check(fixture.reads == i + 1U, "draining the response reads the sector once");
    }
}

static void test_invalid_commands(void)
{
    disk_fixture_t fixture = {.geometry = {720U, 128U}, .unit = 1U};
    const mt_atari_sio_disk_t disk = {&fixture, disk_geometry, disk_read};
    mt_atari_sio_t sio = {0};
    command(&sio, &disk, 1U, 0x31U, 'R', 0U);
    expect_reply(&sio, &disk, 1U, 'N', false);
    expect_idle(&sio, &disk);
    command(&sio, &disk, 2U, 0x31U, 'R', 721U);
    expect_reply(&sio, &disk, 2U, 'N', false);
    expect_idle(&sio, &disk);
    command(&sio, &disk, 3U, 0x31U, 'W', 1U);
    expect_reply(&sio, &disk, 3U, 'N', false);
    expect_idle(&sio, &disk);
    command(&sio, &disk, 4U, 0x32U, 'S', 0U);
    expect_idle(&sio, &disk);
    command(&sio, &disk, 5U, 0x39U, 'S', 0U);
    expect_idle(&sio, &disk);

    uint8_t frame[6] = {0};
    make_frame(frame, 0x31U, 'R', 500U);
    receive_frame(&sio, &disk, 6U, frame, 4U);
    expect_idle(&sio, &disk);
    receive_frame(&sio, &disk, 7U, frame, 6U);
    expect_idle(&sio, &disk);
    frame[4] = (uint8_t)(frame[4] - 1U); /* Ordinary truncation lacks end-around carry. */
    receive_frame(&sio, &disk, 8U, frame, 5U);
    expect_idle(&sio, &disk);
    fixture.geometry.sector_bytes = 512U;
    command(&sio, &disk, 9U, 0x31U, 'R', 4U);
    expect_idle(&sio, &disk);
    check(fixture.reads == 0U, "bad frames, invalid sectors, and writes never reach sector I/O");
}

static void test_epoch_and_reset(void)
{
    disk_fixture_t fixture = {.geometry = {720U, 128U}, .unit = 1U};
    const mt_atari_sio_disk_t disk = {&fixture, disk_geometry, disk_read};
    mt_atari_sio_t sio = {0};
    uint8_t frame[5];
    make_frame(frame, 0x31U, 'R', 1U);

    mt_atari_sio_receive(&sio, &disk, 0xfeU, MT_ATARI_SIO_COMMAND_BEGIN, 0U);
    receive_bytes(&sio, &disk, 0xfeU, frame, 3U);
    mt_atari_sio_receive(&sio, &disk, 0xffU, MT_ATARI_SIO_RESYNC, 0U);
    receive_bytes(&sio, &disk, 0xfeU, frame + 3U, 2U);
    mt_atari_sio_receive(&sio, &disk, 0xfeU, MT_ATARI_SIO_COMMAND_END, 0U);
    expect_idle(&sio, &disk);

    mt_atari_sio_receive(&sio, &disk, 0U, MT_ATARI_SIO_COMMAND_BEGIN, 0U);
    receive_bytes(&sio, &disk, 0xffU, frame, sizeof(frame));
    mt_atari_sio_receive(&sio, &disk, 0xffU, MT_ATARI_SIO_COMMAND_END, 0U);
    expect_idle(&sio, &disk);
    receive_bytes(&sio, &disk, 0U, frame, sizeof(frame));
    expect_idle(&sio, &disk);
    mt_atari_sio_receive(&sio, &disk, 0U, MT_ATARI_SIO_COMMAND_END, 0U);
    expect_reply(&sio, &disk, 0U, 'A', false);
    mt_atari_sio_reset(&sio);
    expect_idle(&sio, &disk);
    check(fixture.reads == 0U, "reset cancels a deferred read even after ACK");

    const uint8_t status[] = {0x18U, 0xffU, 1U, 0U};
    command(&sio, &disk, 1U, 0x31U, 'S', 0U);
    expect_reply(&sio, &disk, 1U, 'A', false);
    mt_atari_sio_receive(&sio, &disk, 2U, MT_ATARI_SIO_RESYNC, 0U);
    expect_idle(&sio, &disk);
    command(&sio, &disk, 3U, 0x31U, 'S', 0U);
    expect_reply(&sio, &disk, 3U, 'A', false);
    expect_reply(&sio, &disk, 3U, 'C', true);
    expect_data(&sio, &disk, 3U, status, sizeof(status));
}

int main(void)
{
    test_status();
    test_reads();
    test_invalid_commands();
    test_epoch_and_reset();
    puts("PASS: Atari SIO read-only command frames, checksums, reply ordering, and epoch resets");
    return EXIT_SUCCESS;
}
