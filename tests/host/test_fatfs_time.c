/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_fatfs_time.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static uint32_t fat_datetime(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second)
{
    return ((uint32_t)(year - 1980U) << 25U) |
           ((uint32_t)month << 21U) | ((uint32_t)day << 16U) |
           ((uint32_t)hour << 11U) | ((uint32_t)minute << 5U) |
           (uint32_t)(second / 2U);
}

int main(void)
{
    uint8_t registers[MT_PCF8563_TIME_REGISTER_COUNT] = {
        0x58U, 0x05U, 0x14U, 0x03U, 0x04U, 0x09U, 0x26U
    };

    mt_fatfs_time_reset();
    assert(mt_fatfs_time_current() ==
           fat_datetime(2010U, 1U, 1U, 0U, 0U, 0U));
    assert(!mt_fatfs_time_update_from_pcf8563(NULL));
    assert(mt_fatfs_time_update_from_pcf8563(registers));
    assert(mt_fatfs_time_current() ==
           fat_datetime(2026U, 9U, 3U, 14U, 5U, 58U));

    /* Voltage-low and malformed readings retain the last good timestamp. */
    registers[0] |= UINT8_C(0x80);
    assert(!mt_fatfs_time_update_from_pcf8563(registers));
    assert(mt_fatfs_time_current() ==
           fat_datetime(2026U, 9U, 3U, 14U, 5U, 58U));
    registers[0] &= UINT8_C(0x7F);
    registers[5] = 0x13U;
    assert(!mt_fatfs_time_update_from_pcf8563(registers));
    assert(mt_fatfs_time_current() ==
           fat_datetime(2026U, 9U, 3U, 14U, 5U, 58U));

    /* The PCF8563 century bit selects 1900; FAT dates clamp to 1980. */
    registers[5] = 0x89U;
    assert(mt_fatfs_time_update_from_pcf8563(registers));
    assert(mt_fatfs_time_current() ==
           fat_datetime(1980U, 9U, 3U, 14U, 5U, 58U));
    return 0;
}
