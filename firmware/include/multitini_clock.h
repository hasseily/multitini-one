/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CLOCK_H
#define MULTITINI_CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} mt_clock_time_t;

typedef enum {
    MT_CLOCK_FIELD_YEAR = 0,
    MT_CLOCK_FIELD_MONTH,
    MT_CLOCK_FIELD_DAY,
    MT_CLOCK_FIELD_HOUR,
    MT_CLOCK_FIELD_MINUTE,
    MT_CLOCK_FIELD_SECOND,
    MT_CLOCK_FIELD_COUNT
} mt_clock_field_t;

/* Hardware snapshots can cover 1900..2099. Decoding uses the established
 * PCF8563 century convention: month bit 7 selects 1900, otherwise 2000.
 * Invalid input, including the voltage-low flag, leaves the output intact. */
bool mt_clock_valid(const mt_clock_time_t *time);
bool mt_clock_decode(const uint8_t registers[7], mt_clock_time_t *time);
/* Editor writes permit 2000..2099 and generate weekday (Sunday = 0).
 * Invalid input leaves all seven output registers intact. */
bool mt_clock_encode(const mt_clock_time_t *time, uint8_t registers[7]);
/* A verified write may read back exactly or one second later. Calendar
 * rollover is checked; 2099 cannot wrap into 1900 or the editor's year 2000. */
bool mt_clock_readback_matches(const mt_clock_time_t *requested,
                               const mt_clock_time_t *actual);
/* Adjust one field by the sign of direction, wrapping its allowed bounds.
 * Month/year changes clamp the day. A valid 1900s snapshot enters the editor
 * range at 2000 (2099 for a backwards year step). Other invalid inputs and
 * a zero direction do nothing. */
void mt_clock_adjust(mt_clock_time_t *time, size_t field, int direction);

#endif
