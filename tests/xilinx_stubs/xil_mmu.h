/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XILINX_XIL_MMU_H
#define TEST_XILINX_XIL_MMU_H
#include <stdint.h>
#define DEVICE_MEMORY UINT32_C(0xC02)
void Xil_SetTlbAttributes(uintptr_t address, uint32_t attributes);
#endif
