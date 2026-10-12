/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_VIDEO_WORKER_XIL_CACHE_H
#define TEST_VIDEO_WORKER_XIL_CACHE_H
#include <stdint.h>
typedef intptr_t INTPTR;
typedef uint32_t u32;
void Xil_DCacheFlushRange(INTPTR address, u32 size);
#endif
