/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_ATARI_DISK_H
#define MULTITINI_ATARI_DISK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MT_ATARI_BOOT_SECTOR_BYTES 128U
#define MT_ATARI_MAX_SECTOR_BYTES 256U

typedef bool (*mt_atari_disk_read_fn)(void *context, uint32_t offset,
                                      uint8_t *data, size_t length);

typedef enum {
    MT_ATARI_DISK_OK = 0,
    MT_ATARI_DISK_INVALID,
    MT_ATARI_DISK_IO,
    MT_ATARI_DISK_UNSUPPORTED
} mt_atari_disk_result_t;

typedef struct {
    uint16_t sector_count;
    uint16_t sector_size;
    uint16_t boot_stride;
    uint16_t first_data_offset;
    bool write_protected;
} mt_atari_atr_t;

typedef struct {
    uint32_t file_size;
    uint16_t sector_count;
    uint16_t load_address;
    uint16_t run_address;
} mt_atari_xex_t;

/* The callback must return true only after reading all requested bytes. */
mt_atari_disk_result_t mt_atari_atr_open(uint32_t file_size, mt_atari_disk_read_fn read,
                                        void *context, mt_atari_atr_t *atr);
/* Sectors are one-based. The first three always transfer 128 bytes. */
bool mt_atari_atr_sector(const mt_atari_atr_t *atr, uint16_t sector,
                         uint32_t *offset, uint16_t *length);

mt_atari_disk_result_t mt_atari_xex_open(uint32_t file_size, mt_atari_disk_read_fn read,
                                        void *context, mt_atari_xex_t *xex);
/* XEX presents a read-only D1: boot image of 128-byte sectors. */
mt_atari_disk_result_t mt_atari_xex_read_sector(const mt_atari_xex_t *xex,
                                               mt_atari_disk_read_fn read, void *context,
                                               uint16_t sector,
                                               uint8_t data[MT_ATARI_BOOT_SECTOR_BYTES]);

#endif
