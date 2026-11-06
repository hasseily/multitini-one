/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_atari_sio.h"

#include <string.h>

enum { ACK = 'A', NAK = 'N', COMPLETE = 'C', ERROR = 'E' };

static uint8_t checksum(const uint8_t *data, size_t bytes)
{
    unsigned sum = 0U;
    for (size_t i = 0U; i < bytes; ++i) {
        sum += data[i];
        sum = (sum & 0xffU) + (sum >> 8U);
    }
    return (uint8_t)sum;
}

void mt_atari_sio_reset(mt_atari_sio_t *sio)
{
    sio->collecting = false;
    sio->command_bytes = 0U;
    sio->read_bytes = 0U;
    sio->reply_index = sio->reply_bytes = 0U;
}

static void queue_reply(mt_atari_sio_t *sio, uint8_t code)
{
    sio->reply[0] = code;
    sio->reply_index = 0U;
    sio->reply_bytes = 1U;
    sio->reply_gap = false;
}

static void status_reply(mt_atari_sio_t *sio, const mt_atari_sio_geometry_t *geometry)
{
    uint8_t flags = 0x18U; /* Drive active and write-protected. */
    if (geometry->sector_bytes == 256U) flags |= 0x20U;
    if (geometry->sectors == 1040U && geometry->sector_bytes == 128U) flags |= 0x80U;
    sio->reply[2] = flags;
    sio->reply[3] = 0xffU;
    sio->reply[4] = 1U;
    sio->reply[5] = 0U;
}

static void percom_reply(mt_atari_sio_t *sio, const mt_atari_sio_geometry_t *geometry)
{
    uint8_t *const data = sio->reply + 2U;
    uint16_t sectors_per_track = geometry->sectors;
    uint8_t tracks = 1U;
    uint8_t sides = 1U;
    switch (geometry->sectors) {
    case 720U: tracks = 40U; sectors_per_track = 18U; break;
    case 1040U: tracks = 40U; sectors_per_track = 26U; break;
    case 1440U: tracks = 40U; sectors_per_track = 18U; sides = 2U; break;
    default: break;
    }
    memset(data, 0, 12U);
    data[0] = tracks;
    data[1] = 1U;
    data[2] = (uint8_t)(sectors_per_track >> 8U);
    data[3] = (uint8_t)sectors_per_track;
    data[4] = (uint8_t)(sides - 1U);
    data[5] = geometry->sector_bytes == 256U || geometry->sectors == 1040U ? 4U : 0U;
    data[6] = (uint8_t)(geometry->sector_bytes >> 8U);
    data[7] = (uint8_t)geometry->sector_bytes;
    data[8] = 1U; /* Drive online. */
}

static void command_end(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk)
{
    const uint8_t device = sio->command[0];
    mt_atari_sio_geometry_t geometry;
    if (sio->command_bytes != sizeof(sio->command) || device < 0x31U || device > 0x38U ||
        checksum(sio->command, 4U) != sio->command[4] ||
        !disk->geometry(disk->context, (uint8_t)(device - 0x30U), &geometry) ||
        (geometry.sector_bytes != 128U && geometry.sector_bytes != 256U))
        return;

    const uint16_t sector = (uint16_t)((uint16_t)sio->command[3] << 8U | sio->command[2]);
    queue_reply(sio, ACK);
    switch (sio->command[1]) {
    case 'R':
        if (sector == 0U || sector > geometry.sectors) {
            queue_reply(sio, NAK);
            return;
        }
        sio->unit = (uint8_t)(device - 0x30U);
        sio->read_sector = sector;
        sio->read_bytes = sector <= 3U ? 128U : geometry.sector_bytes;
        return;
    case 'S':
        status_reply(sio, &geometry);
        sio->reply_bytes = 7U;
        break;
    case 'N':
        percom_reply(sio, &geometry);
        sio->reply_bytes = 15U;
        break;
    default:
        queue_reply(sio, NAK);
        return;
    }
    sio->reply[1] = COMPLETE;
    sio->reply[sio->reply_bytes - 1U] = checksum(sio->reply + 2U, sio->reply_bytes - 3U);
    sio->reply_gap = true;
}

void mt_atari_sio_receive(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk,
                          uint8_t epoch, mt_atari_sio_event_t event, uint8_t byte)
{
    if (event == MT_ATARI_SIO_RESYNC || event == MT_ATARI_SIO_COMMAND_BEGIN) {
        mt_atari_sio_reset(sio);
        sio->epoch = epoch;
        sio->collecting = event == MT_ATARI_SIO_COMMAND_BEGIN;
        return;
    }
    if (!sio->collecting || epoch != sio->epoch) return;
    if (event == MT_ATARI_SIO_COMMAND_END) {
        sio->collecting = false;
        command_end(sio, disk);
    } else if (sio->command_bytes < sizeof(sio->command)) {
        sio->command[sio->command_bytes++] = byte;
    } else {
        sio->collecting = false; /* Reject extra bytes before COMMAND goes high. */
    }
}

bool mt_atari_sio_next_reply(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk,
                             mt_atari_sio_reply_t *reply)
{
    if (sio->reply_index == sio->reply_bytes && sio->read_bytes != 0U) {
        const uint16_t bytes = sio->read_bytes;
        sio->read_bytes = 0U;
        const bool ok = disk->read(disk->context, sio->unit, sio->read_sector,
                                   sio->reply + 1U, bytes);
        if (!ok) memset(sio->reply + 1U, 0, bytes);
        sio->reply[0] = ok ? COMPLETE : ERROR;
        sio->reply[bytes + 1U] = checksum(sio->reply + 1U, bytes);
        sio->reply_bytes = (uint16_t)(bytes + 2U);
        sio->reply_index = 0U;
        *reply = (mt_atari_sio_reply_t){sio->epoch, sio->reply[sio->reply_index++], true};
        return true;
    }
    if (sio->reply_index == sio->reply_bytes) return false;
    const bool gap = sio->reply_gap && sio->reply_index == 1U;
    *reply = (mt_atari_sio_reply_t){sio->epoch, sio->reply[sio->reply_index++], gap};
    return true;
}
