/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_AK4493_H
#define MULTITINI_AK4493_H

#include <stdbool.h>
#include <stdint.h>

#define MT_AK4493_I2C_ADDRESS UINT8_C(0x10)

typedef int (*mt_ak4493_write_fn)(void *context, uint8_t address,
                                  uint8_t reg, uint8_t value);
typedef void (*mt_ak4493_delay_us_fn)(void *context, uint32_t microseconds);

typedef struct {
    void *context;
    mt_ak4493_write_fn write;
    mt_ak4493_delay_us_fn delay_us;
} mt_ak4493_bus_t;

/* Initialize 48 kHz, 24-bit I2S. The safe default leaves soft mute asserted. */
int mt_ak4493_initialize(const mt_ak4493_bus_t *bus, bool unmute);
int mt_ak4493_set_mute(const mt_ak4493_bus_t *bus, bool mute);

#endif
