/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XQSPIPS_H
#define TEST_XQSPIPS_H
#include <stdint.h>
typedef uint8_t u8;
typedef struct { uintptr_t BaseAddress; uint32_t InputClockHz; uint8_t ConnectionMode; } XQspiPs_Config;
typedef struct {
    XQspiPs_Config Config;
    uint32_t options, linear_config;
    uint8_t prescaler, selected, IsBusy;
} XQspiPs;
#define XQSPIPS_CLK_PRESCALE_8 2U
#define XQSPIPS_CLK_PRESCALE_32 4U
#define XQSPIPS_FORCE_SSELECT_OPTION 0x10U
#define XQSPIPS_HOLD_B_DRIVE_OPTION 0x100U
#define XQSPIPS_LQSPI_MODE_OPTION 0x80U
int XQspiPs_SetSlaveSelect(XQspiPs *qspi);
int XQspiPs_PolledTransfer(XQspiPs *qspi, u8 *tx, u8 *rx, uint32_t length);
int XQspiPs_CfgInitialize(XQspiPs *qspi, XQspiPs_Config *config, uintptr_t base);
int XQspiPs_SetClkPrescaler(XQspiPs *qspi, uint8_t prescaler);
uint8_t XQspiPs_GetClkPrescaler(XQspiPs *qspi);
int XQspiPs_SetOptions(XQspiPs *qspi, uint32_t options);
uint32_t XQspiPs_GetOptions(XQspiPs *qspi);
void XQspiPs_SetLqspiConfigReg(XQspiPs *qspi, uint32_t value);
uint32_t XQspiPs_GetLqspiConfigReg(XQspiPs *qspi);
#endif
