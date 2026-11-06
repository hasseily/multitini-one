/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_ATARI_SIO_H
#define MULTITINI_ATARI_SIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MT_ATARI_SIO_BYTE,
    MT_ATARI_SIO_COMMAND_BEGIN,
    MT_ATARI_SIO_COMMAND_END,
    MT_ATARI_SIO_RESYNC
} mt_atari_sio_event_t;

typedef struct {
    uint16_t sectors, sector_bytes;
} mt_atari_sio_geometry_t;

typedef struct {
    void *context;
    bool (*geometry)(void *context, uint8_t unit, mt_atari_sio_geometry_t *geometry);
    bool (*read)(void *context, uint8_t unit, uint16_t sector, uint8_t *data, uint16_t bytes);
} mt_atari_sio_disk_t;

typedef struct {
    uint8_t epoch, byte;
    bool gap;
} mt_atari_sio_reply_t;

typedef struct {
    uint8_t command[5], command_bytes, epoch, unit;
    bool collecting;
    uint16_t read_sector, read_bytes;
    uint8_t reply[258];
    uint16_t reply_index, reply_bytes;
    bool reply_gap;
} mt_atari_sio_t;

void mt_atari_sio_reset(mt_atari_sio_t *sio);
void mt_atari_sio_receive(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk,
                          uint8_t epoch, mt_atari_sio_event_t event, uint8_t byte);
/* The first reply to a sector read is ACK. A subsequent call may read the media. */
bool mt_atari_sio_next_reply(mt_atari_sio_t *sio, const mt_atari_sio_disk_t *disk,
                             mt_atari_sio_reply_t *reply);

#endif
