/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_clock.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static bool equal_time(const mt_clock_time_t *left, const mt_clock_time_t *right)
{
    return left->year == right->year && left->month == right->month &&
           left->day == right->day && left->hour == right->hour &&
           left->minute == right->minute && left->second == right->second;
}

static void test_calendar_roundtrip(void)
{
    mt_clock_time_t time = {2000U, 1U, 1U, 23U, 59U, 58U};
    mt_clock_time_t decoded;
    uint8_t registers[9];
    unsigned expected_weekday = 6U;
    unsigned dates = 0U;
    memset(registers, 0xA5, sizeof(registers));
    for (time.year = 2000U; time.year <= 2099U; ++time.year) {
        for (time.month = 1U; time.month <= 12U; ++time.month) {
            for (time.day = 1U; time.day <= 31U; ++time.day) {
                if (!mt_clock_valid(&time)) continue;
                check(mt_clock_encode(&time, registers + 1U), "valid date encodes");
                check(registers[5] == expected_weekday, "weekdays advance across all editor dates");
                expected_weekday = (expected_weekday + 1U) % 7U;
                check(mt_clock_decode(registers + 1U, &decoded), "encoded date decodes");
                check(equal_time(&time, &decoded), "date and time roundtrip exactly");
                check(registers[0] == 0xA5U && registers[8] == 0xA5U,
                      "encoding stays within seven registers");
                ++dates;
            }
        }
    }
    check(dates == 36525U, "editor century includes exactly 25 leap days");
    time = (mt_clock_time_t){1900U, 2U, 29U, 0U, 0U, 0U};
    check(!mt_clock_valid(&time), "1900 is not a leap year");
    time.year = 2000U;
    check(mt_clock_valid(&time), "2000 is a leap year");
    time.year = 2100U;
    check(!mt_clock_valid(&time), "year beyond hardware convention rejected");
}

static void test_invalid_snapshots(void)
{
    const uint8_t good[7] = {0x56U, 0x34U, 0x12U, 0x18U, 0x05U, 0x09U, 0x26U};
    const size_t bcd_fields[6] = {0U, 1U, 2U, 3U, 5U, 6U};
    mt_clock_time_t time = {2026U, 9U, 18U, 12U, 34U, 56U};
    const mt_clock_time_t saved = time;
    uint8_t registers[7];
    size_t index;
    check(mt_clock_decode(good, &time) && equal_time(&time, &saved),
          "normal PCF8563 snapshot decodes");
    for (index = 0U; index < 6U; ++index) {
        memcpy(registers, good, sizeof(registers));
        registers[bcd_fields[index]] = 0x1AU;
        check(!mt_clock_decode(registers, &time) && equal_time(&time, &saved),
              "bad BCD rejects snapshot without replacing prior time");
    }
    memcpy(registers, good, sizeof(registers));
    registers[0] |= 0x80U;
    check(!mt_clock_decode(registers, &time), "voltage-low RTC time is invalid");
    memcpy(registers, good, sizeof(registers));
    registers[4] = 7U;
    check(!mt_clock_decode(registers, &time), "weekday outside zero through six rejected");
    memcpy(registers, good, sizeof(registers));
    registers[3] = 0x31U;
    check(!mt_clock_decode(registers, &time), "September 31 rejects snapshot");
    registers[3] = 0x29U;
    registers[5] = 0x02U;
    check(!mt_clock_decode(registers, &time), "non-leap February 29 rejected");
    registers[6] = 0x24U;
    check(mt_clock_decode(registers, &time) && time.year == 2024U,
          "leap February 29 accepted");
    registers[5] = 0x82U;
    check(mt_clock_decode(registers, &time) && time.year == 1924U,
          "month century bit selects 1900s");
    registers[6] = 0x00U;
    check(!mt_clock_decode(registers, &time), "1900 leap-day snapshot rejected");
    check(!mt_clock_decode(NULL, &time) && !mt_clock_decode(good, NULL),
          "NULL decode arguments rejected");
    check(!mt_clock_valid(NULL), "NULL time is invalid");

    time = saved;
    time.hour = 24U;
    memset(registers, 0xA5, sizeof(registers));
    check(!mt_clock_encode(&time, registers), "invalid hour does not encode");
    for (index = 0U; index < sizeof(registers); ++index)
        check(registers[index] == 0xA5U, "invalid encode preserves all destination bytes");
    time = saved;
    time.year = 1999U;
    check(!mt_clock_encode(&time, registers), "editor cannot write 1900s dates");
    check(!mt_clock_encode(NULL, registers) && !mt_clock_encode(&saved, NULL),
          "NULL encode arguments rejected");
}

static void test_readback(void)
{
    static const mt_clock_time_t pairs[][2] = {
        {{2026U, 9U, 18U, 12U, 34U, 56U}, {2026U, 9U, 18U, 12U, 34U, 57U}},
        {{2026U, 9U, 18U, 12U, 34U, 59U}, {2026U, 9U, 18U, 12U, 35U, 0U}},
        {{2026U, 9U, 18U, 12U, 59U, 59U}, {2026U, 9U, 18U, 13U, 0U, 0U}},
        {{2026U, 9U, 18U, 23U, 59U, 59U}, {2026U, 9U, 19U, 0U, 0U, 0U}},
        {{2026U, 9U, 30U, 23U, 59U, 59U}, {2026U, 10U, 1U, 0U, 0U, 0U}},
        {{2026U, 2U, 28U, 23U, 59U, 59U}, {2026U, 3U, 1U, 0U, 0U, 0U}},
        {{2024U, 2U, 28U, 23U, 59U, 59U}, {2024U, 2U, 29U, 0U, 0U, 0U}},
        {{2024U, 2U, 29U, 23U, 59U, 59U}, {2024U, 3U, 1U, 0U, 0U, 0U}},
        {{2026U, 12U, 31U, 23U, 59U, 59U}, {2027U, 1U, 1U, 0U, 0U, 0U}},
        {{1999U, 12U, 31U, 23U, 59U, 59U}, {2000U, 1U, 1U, 0U, 0U, 0U}}
    };
    const mt_clock_time_t last = {2099U, 12U, 31U, 23U, 59U, 59U};
    mt_clock_time_t actual = {2100U, 1U, 1U, 0U, 0U, 0U};
    size_t index;
    for (index = 0U; index < sizeof(pairs) / sizeof(pairs[0]); ++index) {
        check(mt_clock_readback_matches(&pairs[index][0], &pairs[index][0]),
              "exact readback matches");
        check(mt_clock_readback_matches(&pairs[index][0], &pairs[index][1]),
              "readback accepts one elapsed second across calendar boundaries");
        check(!mt_clock_readback_matches(&pairs[index][1], &pairs[index][0]),
              "readback rejects a clock that moved backwards");
    }
    check(mt_clock_readback_matches(&last, &last), "last hardware instant accepts exact readback");
    check(!mt_clock_readback_matches(&last, &actual), "year 2100 is outside the hardware convention");
    actual.year = 2000U;
    check(!mt_clock_readback_matches(&last, &actual), "readback does not use editor year wrapping");
    actual.year = 1900U;
    check(!mt_clock_readback_matches(&last, &actual), "century-bit overflow cannot pass verification");
    actual = pairs[0][1];
    ++actual.second;
    check(!mt_clock_readback_matches(&pairs[0][0], &actual), "two elapsed seconds do not match");
    actual.day = 0U;
    check(!mt_clock_readback_matches(&pairs[0][0], &actual) &&
          !mt_clock_readback_matches(&actual, &pairs[0][0]), "invalid timestamps do not match");
    check(!mt_clock_readback_matches(NULL, &last) &&
          !mt_clock_readback_matches(&last, NULL), "NULL timestamps do not match");
}

static void test_adjustments(void)
{
    mt_clock_time_t time = {2024U, 2U, 29U, 23U, 59U, 59U};
    mt_clock_time_t saved;
    mt_clock_adjust(&time, MT_CLOCK_FIELD_YEAR, 1);
    check(time.year == 2025U && time.day == 28U, "year change clamps leap day");
    time = (mt_clock_time_t){2024U, 1U, 31U, 23U, 59U, 59U};
    mt_clock_adjust(&time, MT_CLOCK_FIELD_MONTH, 1);
    check(time.month == 2U && time.day == 29U, "month change clamps day to leap month");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_DAY, 1);
    check(time.day == 1U, "day wraps within its month");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_DAY, -1);
    check(time.day == 29U, "day wraps backwards within its month");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_HOUR, INT_MAX);
    mt_clock_adjust(&time, MT_CLOCK_FIELD_MINUTE, INT_MAX);
    mt_clock_adjust(&time, MT_CLOCK_FIELD_SECOND, INT_MAX);
    check(time.hour == 0U && time.minute == 0U && time.second == 0U,
          "positive directions step once and wrap clock fields");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_HOUR, INT_MIN);
    mt_clock_adjust(&time, MT_CLOCK_FIELD_MINUTE, INT_MIN);
    mt_clock_adjust(&time, MT_CLOCK_FIELD_SECOND, INT_MIN);
    check(time.hour == 23U && time.minute == 59U && time.second == 59U,
          "INT_MIN directions safely step backwards");
    time = (mt_clock_time_t){2099U, 12U, 31U, 0U, 0U, 0U};
    mt_clock_adjust(&time, MT_CLOCK_FIELD_YEAR, 1);
    check(time.year == 2000U, "editor year wraps forward");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_YEAR, -1);
    check(time.year == 2099U, "editor year wraps backwards");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_MONTH, 1);
    check(time.month == 1U && time.year == 2099U, "month wraps without changing other field");
    mt_clock_adjust(&time, MT_CLOCK_FIELD_MONTH, -1);
    check(time.month == 12U, "month wraps backwards");
    time = (mt_clock_time_t){1996U, 2U, 29U, 0U, 0U, 0U};
    mt_clock_adjust(&time, MT_CLOCK_FIELD_YEAR, -1);
    check(time.year == 2099U && time.day == 28U,
          "old-century year adjustment enters editor range and clamps date");
    time = (mt_clock_time_t){1996U, 2U, 29U, 0U, 0U, 0U};
    mt_clock_adjust(&time, MT_CLOCK_FIELD_MINUTE, 1);
    check(time.year == 2000U && time.day == 29U && time.minute == 1U,
          "editing an old-century time enters supported editor range");
    saved = time;
    mt_clock_adjust(&time, SIZE_MAX, 1);
    mt_clock_adjust(&time, MT_CLOCK_FIELD_YEAR, 0);
    mt_clock_adjust(NULL, MT_CLOCK_FIELD_YEAR, 1);
    check(equal_time(&time, &saved), "invalid field and zero direction do nothing");
    time.month = 0U;
    saved = time;
    mt_clock_adjust(&time, MT_CLOCK_FIELD_YEAR, 1);
    check(equal_time(&time, &saved), "invalid input date remains untouched");
}

int main(void)
{
    test_calendar_roundtrip();
    test_invalid_snapshots();
    test_readback();
    test_adjustments();
    puts("PASS: RTC calendar, BCD validation, weekdays, write verification, and bounded editor adjustments");
    return 0;
}
