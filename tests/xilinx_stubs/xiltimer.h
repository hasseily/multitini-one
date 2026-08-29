/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XILINX_XILTIMER_H
#define TEST_XILINX_XILTIMER_H

#include <stdint.h>

typedef uint64_t XTime;
#define COUNTS_PER_SECOND UINT32_C(333333333)
void XTime_GetTime(XTime *value);

#endif
