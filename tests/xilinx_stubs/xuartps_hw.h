/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XILINX_XUARTPS_HW_H
#define TEST_XILINX_XUARTPS_HW_H

#include <stdint.h>

#define XUARTPS_SR_OFFSET UINT32_C(0x2C)
#define XUARTPS_FIFO_OFFSET UINT32_C(0x30)
#define XUARTPS_SR_RXEMPTY UINT32_C(0x00000002)
#define XUARTPS_SR_TXFULL UINT32_C(0x00000010)
#define XUARTPS_SR_TXEMPTY UINT32_C(0x00000008)
#define XUARTPS_SR_TACTIVE UINT32_C(0x00000800)

uint32_t XUartPs_ReadReg(uintptr_t base_address, uint32_t offset);
void XUartPs_WriteReg(uintptr_t base_address, uint32_t offset, uint32_t value);

#endif
