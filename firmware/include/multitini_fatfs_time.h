/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_FATFS_TIME_H
#define MULTITINI_FATFS_TIME_H

#include <stdbool.h>
#include <stdint.h>

#define MT_PCF8563_I2C_ADDRESS UINT8_C(0x51)
#define MT_PCF8563_SECONDS_REGISTER UINT8_C(0x02)
#define MT_PCF8563_TIME_REGISTER_COUNT 7U

/*
 * Cache a PCF8563 seconds-through-year register snapshot for FatFs. Invalid
 * snapshots leave the last good value in place. The getter returns FatFs'
 * fixed 2010-01-01 fallback until the first valid RTC sample arrives.
 */
void mt_fatfs_time_reset(void);
bool mt_fatfs_time_update_from_pcf8563(
    const uint8_t registers[MT_PCF8563_TIME_REGISTER_COUNT]);
uint32_t mt_fatfs_time_current(void);

#endif
