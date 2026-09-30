/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XQSPIPS_H
#define TEST_XQSPIPS_H
#include <stdint.h>
typedef uint8_t u8;
typedef struct { unsigned placeholder; } XQspiPs;
typedef struct { uintptr_t BaseAddress; uint32_t InputClockHz; } XQspiPs_Config;
#define XQSPIPS_CLK_PRESCALE_32 4U
#define XQSPIPS_FORCE_SSELECT_OPTION 0x10U
int XQspiPs_SetSlaveSelect(XQspiPs *qspi);
int XQspiPs_PolledTransfer(XQspiPs *qspi, u8 *tx, u8 *rx, uint32_t length);
int XQspiPs_CfgInitialize(XQspiPs *qspi, XQspiPs_Config *config, uintptr_t base);
void XQspiPs_SetClkPrescaler(XQspiPs *qspi, uint8_t prescaler);
int XQspiPs_SetOptions(XQspiPs *qspi, uint32_t options);
#endif
