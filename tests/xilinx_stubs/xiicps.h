/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TEST_XILINX_XIICPS_H
#define TEST_XILINX_XIICPS_H
#include <stdint.h>
typedef struct { uintptr_t BaseAddress; } XIicPs_Config;
typedef struct { uintptr_t opaque; } XIicPs;
XIicPs_Config *XIicPs_LookupConfig(uintptr_t base_address);
int XIicPs_CfgInitialize(XIicPs *instance, XIicPs_Config *config,
                         uintptr_t effective_address);
int XIicPs_SetSClk(XIicPs *instance, uint32_t rate);
int XIicPs_MasterSendPolled(XIicPs *instance, uint8_t *message, int length,
                            uint8_t address);
int XIicPs_MasterRecvPolled(XIicPs *instance, uint8_t *message, int length,
                            uint8_t address);
int XIicPs_BusIsBusy(XIicPs *instance);
#endif
