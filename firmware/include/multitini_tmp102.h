/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_TMP102_H
#define MULTITINI_TMP102_H

#include <stdint.h>

#define MT_TMP102_I2C_ADDRESS UINT8_C(0x48)
#define MT_TMP102_TEMPERATURE_REGISTER UINT8_C(0x00)

/* Decode default 12-bit mode exactly in millionths of a degree Celsius. */
int mt_tmp102_decode_micro_c(const uint8_t raw[2], int32_t *micro_c);

#endif
