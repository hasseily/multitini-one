/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AMIGA_RTC_H
#define MULTITINI_AMIGA_RTC_H
#include "multitini_amiga_boot.h"
#include "multitini_clock.h"

typedef struct {
    uint64_t value;
    uint32_t last_attempt_ms;
    mt_link_result_t result;
    bool attempted,sent,supported;
} mt_amiga_rtc_t;

/* MSM6242B bytes: BCD seconds/minutes/hours/day/month/year, Sunday-zero
 * weekday and0x40 (24-hour mode). Uses mt_clock_encode's2000..2099 range;
 * invalid or unsupported input leaves the output intact. */
bool mt_amiga_rtc_encode(const mt_clock_time_t *,uint64_t *);
/* Old cores without MT_AMIGA_HOST_RTC are a successful no-op. A failed
 * attempted transfer discards its partial staging through CLEAR_ERROR. */
mt_link_result_t mt_amiga_rtc_write(mt_core_link_t *,const mt_clock_time_t *,uint32_t polls);
void mt_amiga_rtc_init(mt_amiga_rtc_t *);
/* Force is used before CPU release. Periodic attempts are limited to1Hz;
 * invalid/unavailable time is skipped and never becomes a media fault. */
mt_link_result_t mt_amiga_rtc_update(mt_amiga_rtc_t *,mt_core_link_t *,
    const mt_clock_time_t *,uint32_t now_ms,bool force,uint32_t polls);
#endif
