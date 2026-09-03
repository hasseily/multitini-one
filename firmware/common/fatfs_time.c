/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_fatfs_time.h"

#include <stddef.h>

#define MT_FAT_YEAR_MIN 1980U
#define MT_FAT_YEAR_MAX 2107U

static uint32_t cached_fattime;
static bool cached_fattime_valid;

static uint8_t bcd_to_binary(uint8_t value)
{
    return (uint8_t)((value & UINT8_C(0x0F)) +
                     ((value >> 4U) & UINT8_C(0x0F)) * 10U);
}

static uint32_t fallback_fattime(void)
{
    return ((uint32_t)(2010U - MT_FAT_YEAR_MIN) << 25U) |
           (UINT32_C(1) << 21U) | (UINT32_C(1) << 16U);
}

static uint32_t encode_fattime(uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < MT_FAT_YEAR_MIN) {
        year = MT_FAT_YEAR_MIN;
    } else if (year > MT_FAT_YEAR_MAX) {
        year = MT_FAT_YEAR_MAX;
    }
    return ((uint32_t)(year - MT_FAT_YEAR_MIN) << 25U) |
           ((uint32_t)month << 21U) |
           ((uint32_t)day << 16U) |
           ((uint32_t)hour << 11U) |
           ((uint32_t)minute << 5U) |
           (uint32_t)(second / 2U);
}

void mt_fatfs_time_reset(void)
{
    cached_fattime = 0U;
    cached_fattime_valid = false;
}

bool mt_fatfs_time_update_from_pcf8563(
    const uint8_t registers[MT_PCF8563_TIME_REGISTER_COUNT])
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;

    if (registers == NULL ||
        (registers[0] & UINT8_C(0x80)) != 0U) {
        return false;
    }

    second = bcd_to_binary((uint8_t)(registers[0] & UINT8_C(0x7F)));
    minute = bcd_to_binary((uint8_t)(registers[1] & UINT8_C(0x7F)));
    hour = bcd_to_binary((uint8_t)(registers[2] & UINT8_C(0x3F)));
    day = bcd_to_binary((uint8_t)(registers[3] & UINT8_C(0x3F)));
    month = bcd_to_binary((uint8_t)(registers[5] & UINT8_C(0x1F)));
    year = (uint16_t)(((registers[5] & UINT8_C(0x80)) != 0U ?
                       1900U : 2000U) + bcd_to_binary(registers[6]));

    if (month < 1U || month > 12U || day < 1U || day > 31U ||
        hour > 23U || minute > 59U || second > 59U) {
        return false;
    }

    cached_fattime = encode_fattime(year, month, day, hour, minute, second);
    cached_fattime_valid = true;
    return true;
}

uint32_t mt_fatfs_time_current(void)
{
    return cached_fattime_valid ? cached_fattime : fallback_fattime();
}
