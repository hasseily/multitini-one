/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_atari_disk.h"
#include "atari_xex_boot.h"

#include <string.h>

enum {
    ATR_HEADER_BYTES = 16,
    ATR_BOOT_SECTORS = 3,
    ATR_BOOT_BYTES = ATR_BOOT_SECTORS * MT_ATARI_BOOT_SECTOR_BYTES,
    XEX_FIRST_LOADER_PAGE = 0x07,
    XEX_END_LOADER_PAGE = 0x98,
    XEX_LOADER_PAGES = (MT_ATARI_XEX_BOOT_BYTES + MT_ATARI_BOOT_SECTOR_BYTES + 255U) / 256U
};

static uint16_t little_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (uint16_t)bytes[1] << 8U);
}

mt_atari_disk_result_t mt_atari_atr_open(uint32_t file_size, mt_atari_disk_read_fn read,
                                        void *context, mt_atari_atr_t *atr)
{
    uint8_t header[ATR_HEADER_BYTES];
    mt_atari_atr_t disk = {.boot_stride = MT_ATARI_BOOT_SECTOR_BYTES,
                          .first_data_offset = ATR_HEADER_BYTES + ATR_BOOT_BYTES};
    if (atr == NULL || read == NULL || file_size < sizeof(header)) return MT_ATARI_DISK_INVALID;
    if (!read(context, 0U, header, sizeof(header))) return MT_ATARI_DISK_IO;
    if (little_u16(header) != 0x0296U) return MT_ATARI_DISK_INVALID;
    const uint32_t paragraphs = little_u16(header + 2U) | (uint32_t)little_u16(header + 6U) << 16U;
    const uint32_t payload = file_size - ATR_HEADER_BYTES;
    if (payload == 0U || payload % 16U != 0U || paragraphs != payload / 16U)
        return MT_ATARI_DISK_INVALID;
    disk.sector_size = little_u16(header + 4U);
    disk.write_protected = header[15] != 0U;

    uint32_t sector_count;
    if (disk.sector_size == MT_ATARI_BOOT_SECTOR_BYTES ||
        (disk.sector_size == MT_ATARI_MAX_SECTOR_BYTES && payload <= ATR_BOOT_BYTES)) {
        if (payload % MT_ATARI_BOOT_SECTOR_BYTES != 0U) return MT_ATARI_DISK_INVALID;
        sector_count = payload / MT_ATARI_BOOT_SECTOR_BYTES;
    } else if (disk.sector_size == MT_ATARI_MAX_SECTOR_BYTES) {
        if ((payload - ATR_BOOT_BYTES) % MT_ATARI_MAX_SECTOR_BYTES == 0U) {
            sector_count = ATR_BOOT_SECTORS + (payload - ATR_BOOT_BYTES) / MT_ATARI_MAX_SECTOR_BYTES;
        } else if (payload >= 2U * ATR_BOOT_BYTES && payload % MT_ATARI_MAX_SECTOR_BYTES == 0U) {
            /* Legacy padded images have either three packed boot sectors and
             * one padding block, or 128 bytes of padding after each sector. */
            uint8_t padding[ATR_BOOT_BYTES];
            if (!read(context, ATR_HEADER_BYTES + ATR_BOOT_BYTES, padding, sizeof(padding)))
                return MT_ATARI_DISK_IO;
            for (size_t i = 0; i < sizeof(padding); ++i) {
                if (padding[i] == 0U) continue;
                disk.boot_stride = MT_ATARI_MAX_SECTOR_BYTES;
                break;
            }
            disk.first_data_offset = ATR_HEADER_BYTES + 2U * ATR_BOOT_BYTES;
            sector_count = payload / MT_ATARI_MAX_SECTOR_BYTES;
        } else {
            return MT_ATARI_DISK_INVALID;
        }
    } else {
        return MT_ATARI_DISK_UNSUPPORTED;
    }
    if (sector_count > UINT16_MAX) return MT_ATARI_DISK_UNSUPPORTED;
    disk.sector_count = (uint16_t)sector_count;
    *atr = disk;
    return MT_ATARI_DISK_OK;
}

bool mt_atari_atr_sector(const mt_atari_atr_t *atr, uint16_t sector,
                         uint32_t *offset, uint16_t *length)
{
    if (atr == NULL || offset == NULL || length == NULL || sector == 0U || sector > atr->sector_count)
        return false;
    if (sector <= ATR_BOOT_SECTORS) {
        *offset = ATR_HEADER_BYTES + (uint32_t)(sector - 1U) * atr->boot_stride;
        *length = MT_ATARI_BOOT_SECTOR_BYTES;
    } else {
        *offset = atr->first_data_offset + (uint32_t)(sector - ATR_BOOT_SECTORS - 1U) * atr->sector_size;
        *length = atr->sector_size;
    }
    return true;
}

mt_atari_disk_result_t mt_atari_xex_open(uint32_t file_size, mt_atari_disk_read_fn read,
                                        void *context, mt_atari_xex_t *xex)
{
    if (xex == NULL || read == NULL || file_size < 7U) return MT_ATARI_DISK_INVALID;
    const uint32_t max_file_size = (UINT16_MAX - MT_ATARI_XEX_BOOT_SECTORS) * MT_ATARI_BOOT_SECTOR_BYTES;
    if (file_size > max_file_size) return MT_ATARI_DISK_UNSUPPORTED;

    bool occupied[XEX_END_LOADER_PAGE] = {false};
    uint8_t word[2];
    if (!read(context, 0U, word, sizeof(word))) return MT_ATARI_DISK_IO;
    if (little_u16(word) != UINT16_MAX) return MT_ATARI_DISK_INVALID;
    uint32_t position = 2U;
    bool unsupported = false;
    bool first_segment = true;
    uint16_t run_address = 0U;
    while (position < file_size) {
        uint16_t start;
        do {
            if (file_size - position < 2U) return MT_ATARI_DISK_INVALID;
            if (!read(context, position, word, sizeof(word))) return MT_ATARI_DISK_IO;
            position += 2U;
            start = little_u16(word);
        } while (start == UINT16_MAX);
        if (file_size - position < 2U) return MT_ATARI_DISK_INVALID;
        if (!read(context, position, word, sizeof(word))) return MT_ATARI_DISK_IO;
        position += 2U;
        const uint16_t end = little_u16(word);
        if (end < start || (uint32_t)end - start + 1U > file_size - position)
            return MT_ATARI_DISK_INVALID;
        if (first_segment) {
            run_address = start;
            first_segment = false;
        }
        /* The stock XL boot and SIO stack grows down from $01FF. Reserve
         * its upper half and the disk control block. Writes to the mapped
         * hardware window are meaningful, unlike writes to ROM. */
        if ((start <= 0x01ffU && end >= 0x0180U) || (start <= 0x030bU && end >= 0x0300U) ||
            (end >= 0xc000U && !(start >= 0xd000U && end < 0xd800U)))
            unsupported = true;
        for (unsigned page = start >> 8U; page <= (end >> 8U) && page < XEX_END_LOADER_PAGE; ++page)
            occupied[page] = true;
        position += (uint32_t)end - start + 1U;
    }
    if (unsupported) return MT_ATARI_DISK_UNSUPPORTED;
    unsigned free_pages = 0U;
    for (unsigned page = XEX_FIRST_LOADER_PAGE; page < XEX_END_LOADER_PAGE; ++page) {
        free_pages = occupied[page] ? 0U : free_pages + 1U;
        if (free_pages < XEX_LOADER_PAGES) continue;
        *xex = (mt_atari_xex_t){.file_size = file_size,
            .sector_count = (uint16_t)(MT_ATARI_XEX_BOOT_SECTORS +
                (file_size + MT_ATARI_BOOT_SECTOR_BYTES - 1U) / MT_ATARI_BOOT_SECTOR_BYTES),
            .load_address = (uint16_t)((page + 1U - XEX_LOADER_PAGES) << 8U),
            .run_address = run_address};
        return MT_ATARI_DISK_OK;
    }
    return MT_ATARI_DISK_UNSUPPORTED;
}

mt_atari_disk_result_t mt_atari_xex_read_sector(const mt_atari_xex_t *xex,
                                               mt_atari_disk_read_fn read, void *context,
                                               uint16_t sector,
                                               uint8_t data[MT_ATARI_BOOT_SECTOR_BYTES])
{
    if (xex == NULL || read == NULL || data == NULL || sector == 0U || sector > xex->sector_count)
        return MT_ATARI_DISK_INVALID;
    const uint32_t sector_offset = (uint32_t)(sector - 1U) * MT_ATARI_BOOT_SECTOR_BYTES;
    if (sector <= MT_ATARI_XEX_BOOT_SECTORS) {
        memcpy(data, mt_atari_xex_boot + sector_offset, MT_ATARI_BOOT_SECTOR_BYTES);
        const uint8_t page_delta = (uint8_t)((xex->load_address - MT_ATARI_XEX_BOOT_ADDRESS) >> 8U);
        for (size_t i = 0; i < sizeof(mt_atari_xex_relocations) / sizeof(mt_atari_xex_relocations[0]); ++i) {
            const uint32_t offset = mt_atari_xex_relocations[i];
            if (offset >= sector_offset && offset - sector_offset < MT_ATARI_BOOT_SECTOR_BYTES)
                data[offset - sector_offset] = (uint8_t)(data[offset - sector_offset] + page_delta);
        }
        if (sector == 1U) {
            for (unsigned i = 0; i < 3U; ++i)
                data[MT_ATARI_XEX_LENGTH_OFFSET + i] = (uint8_t)(xex->file_size >> (i * 8U));
            data[MT_ATARI_XEX_RUN_OFFSET] = (uint8_t)xex->run_address;
            data[MT_ATARI_XEX_RUN_OFFSET + 1U] = (uint8_t)(xex->run_address >> 8U);
        }
        return MT_ATARI_DISK_OK;
    }
    const uint32_t offset = sector_offset - MT_ATARI_XEX_BOOT_BYTES;
    if (offset >= xex->file_size) return MT_ATARI_DISK_INVALID;
    size_t count = xex->file_size - offset;
    if (count > MT_ATARI_BOOT_SECTOR_BYTES) count = MT_ATARI_BOOT_SECTOR_BYTES;
    memset(data + count, 0, MT_ATARI_BOOT_SECTOR_BYTES - count);
    return read(context, offset, data, count) ? MT_ATARI_DISK_OK : MT_ATARI_DISK_IO;
}
