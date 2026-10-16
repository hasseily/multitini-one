/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "xiltimer.h"

/* Match the selected Zynq BSP's unparenthesized two-level macro, rather
 * than the ordinary host stub's already evaluated constant. */
#undef COUNTS_PER_SECOND
#define TEST_CPU_FREQ UINT32_C(666666687)
#define TEST_SLEEP_FREQ TEST_CPU_FREQ/2
#define COUNTS_PER_SECOND TEST_SLEEP_FREQ
#include "../../firmware/platform/xilinx/cherryusb_baremetal_osal.c"

static XTime now_ticks;
static unsigned sleep_calls, timer_calls;
void XTime_GetTime(XTime *value) { *value=now_ticks; }
void cherryusb_baremetal_poll_irq(void) { }
void usleep(uint32_t microseconds)
{
    const uint64_t frequency=(uint64_t)COUNTS_PER_SECOND;
    now_ticks+=(frequency*microseconds+UINT64_C(999999))/UINT64_C(1000000);
    ++sleep_calls;
}
static uint64_t ticks_for_ms(uint64_t milliseconds)
{
    const uint64_t frequency=(uint64_t)COUNTS_PER_SECOND;
    return (frequency*milliseconds+UINT64_C(999))/UINT64_C(1000);
}
static void timer_callback(void *context)
{ assert(context==&timer_calls);++timer_calls; }
int main(void)
{
    static const uint64_t milliseconds[]={0U,1U,2U,4U,999U,1000U,1999U,2000U,2001U,8000U,3600000U,
        UINT64_C(4294967295),UINT64_C(4294967296),UINT64_C(4294967304)};
    for(size_t i=0U;i<sizeof(milliseconds)/sizeof(milliseconds[0]);++i) {
        now_ticks=ticks_for_ms(milliseconds[i]);
        assert(cherryusb_baremetal_ms()==(uint32_t)milliseconds[i]);
    }
    now_ticks=0U;sleep_calls=0U;
    usb_osal_msleep(3U);
    assert(sleep_calls==3U && cherryusb_baremetal_ms()==3U);
    now_ticks=0U;
    struct usb_osal_timer *timer=usb_osal_timer_create("nominal",5U,timer_callback,&timer_calls,0U);
    assert(timer!=NULL);usb_osal_timer_start(timer);
    now_ticks=ticks_for_ms(4U);cherryusb_baremetal_osal_poll();assert(timer_calls==0U);
    now_ticks=ticks_for_ms(5U);cherryusb_baremetal_osal_poll();assert(timer_calls==1U);
    usb_osal_timer_delete(timer);
    puts("CherryUSB real-BSP macro: 1ms scale, boundary/wrap, sleep and timer deadlines passed");
    return 0;
}
