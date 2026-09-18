/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_clock.h"

#include <string.h>

static uint8_t month_days(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && year % 4U == 0U &&
        (year % 100U != 0U || year % 400U == 0U)) return 29U;
    return days[month - 1U];
}

bool mt_clock_valid(const mt_clock_time_t *time)
{
    return time != NULL && time->year >= 1900U && time->year <= 2099U &&
           time->month >= 1U && time->month <= 12U && time->day >= 1U &&
           time->day <= month_days(time->year, time->month) &&
           time->hour <= 23U && time->minute <= 59U && time->second <= 59U;
}

static bool decode_bcd(uint8_t value, uint8_t *result)
{
    const uint8_t low = value & UINT8_C(0x0F);
    const uint8_t high = value >> 4U;
    if (low > 9U || high > 9U) return false;
    *result = (uint8_t)(high * 10U + low);
    return true;
}

bool mt_clock_decode(const uint8_t registers[7], mt_clock_time_t *time)
{
    mt_clock_time_t decoded;
    uint8_t year;
    if (registers == NULL || time == NULL ||
        (registers[0] & UINT8_C(0x80)) != 0U ||
        (registers[4] & UINT8_C(0x07)) > 6U) return false;
    if (!decode_bcd(registers[0], &decoded.second) ||
        !decode_bcd(registers[1] & UINT8_C(0x7F), &decoded.minute) ||
        !decode_bcd(registers[2] & UINT8_C(0x3F), &decoded.hour) ||
        !decode_bcd(registers[3] & UINT8_C(0x3F), &decoded.day) ||
        !decode_bcd(registers[5] & UINT8_C(0x1F), &decoded.month) ||
        !decode_bcd(registers[6], &year)) return false;
    decoded.year = (uint16_t)(year +
        ((registers[5] & UINT8_C(0x80)) != 0U ? 1900U : 2000U));
    if (!mt_clock_valid(&decoded)) return false;
    *time = decoded;
    return true;
}

static uint8_t encode_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

bool mt_clock_encode(const mt_clock_time_t *time, uint8_t registers[7])
{
    uint8_t encoded[7];
    uint16_t year;
    uint8_t month;
    uint32_t elapsed_days = 0U;
    if (registers == NULL || !mt_clock_valid(time) || time->year < 2000U)
        return false;
    for (year = 2000U; year < time->year; ++year)
        elapsed_days += (uint32_t)337U + month_days(year, 2U);
    for (month = 1U; month < time->month; ++month)
        elapsed_days += month_days(time->year, month);
    elapsed_days += (uint32_t)time->day - 1U;
    encoded[0] = encode_bcd(time->second);
    encoded[1] = encode_bcd(time->minute);
    encoded[2] = encode_bcd(time->hour);
    encoded[3] = encode_bcd(time->day);
    /* 2000-01-01 was Saturday (6 in the PCF8563 Sunday-zero convention). */
    encoded[4] = (uint8_t)((elapsed_days + 6U) % 7U);
    encoded[5] = encode_bcd(time->month);
    encoded[6] = encode_bcd((uint8_t)(time->year - 2000U));
    memcpy(registers, encoded, sizeof(encoded));
    return true;
}

static bool equal_time(const mt_clock_time_t *left, const mt_clock_time_t *right)
{
    return left->year == right->year && left->month == right->month &&
           left->day == right->day && left->hour == right->hour &&
           left->minute == right->minute && left->second == right->second;
}

bool mt_clock_readback_matches(const mt_clock_time_t *requested,
                               const mt_clock_time_t *actual)
{
    mt_clock_time_t next;
    if (!mt_clock_valid(requested) || !mt_clock_valid(actual)) return false;
    if (equal_time(requested, actual)) return true;
    next = *requested;
    if (++next.second == 60U) {
        next.second = 0U;
        if (++next.minute == 60U) {
            next.minute = 0U;
            if (++next.hour == 24U) {
                next.hour = 0U;
                if (++next.day > month_days(next.year, next.month)) {
                    next.day = 1U;
                    if (++next.month == 13U) {
                        next.month = 1U;
                        ++next.year;
                    }
                }
            }
        }
    }
    return mt_clock_valid(&next) && equal_time(&next, actual);
}

static uint8_t adjust_field(uint8_t value, uint8_t minimum, uint8_t maximum,
                            int direction)
{
    if (direction > 0) return value == maximum ? minimum : (uint8_t)(value + 1U);
    return value == minimum ? maximum : (uint8_t)(value - 1U);
}

void mt_clock_adjust(mt_clock_time_t *time, size_t field, int direction)
{
    if (!mt_clock_valid(time) || field >= MT_CLOCK_FIELD_COUNT || direction == 0)
        return;
    if (time->year < 2000U) {
        time->year = field == MT_CLOCK_FIELD_YEAR && direction < 0 ? 2099U : 2000U;
        if (field == MT_CLOCK_FIELD_YEAR) {
            if (time->day > month_days(time->year, time->month))
                time->day = month_days(time->year, time->month);
            return;
        }
    }
    switch (field) {
    case MT_CLOCK_FIELD_YEAR:
        if (direction > 0)
            time->year = time->year == 2099U ? 2000U : (uint16_t)(time->year + 1U);
        else
            time->year = time->year == 2000U ? 2099U : (uint16_t)(time->year - 1U);
        break;
    case MT_CLOCK_FIELD_MONTH:
        time->month = adjust_field(time->month, 1U, 12U, direction);
        break;
    case MT_CLOCK_FIELD_DAY:
        time->day = adjust_field(time->day, 1U, month_days(time->year, time->month), direction);
        break;
    case MT_CLOCK_FIELD_HOUR:
        time->hour = adjust_field(time->hour, 0U, 23U, direction);
        break;
    case MT_CLOCK_FIELD_MINUTE:
        time->minute = adjust_field(time->minute, 0U, 59U, direction);
        break;
    case MT_CLOCK_FIELD_SECOND:
        time->second = adjust_field(time->second, 0U, 59U, direction);
        break;
    default:
        return;
    }
    if (time->day > month_days(time->year, time->month))
        time->day = month_days(time->year, time->month);
}
