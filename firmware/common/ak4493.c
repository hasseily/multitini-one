/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ak4493.h"

#include <stddef.h>

enum {
    AK4493_REG_CONTROL_1 = 0x00,
    AK4493_REG_CONTROL_2 = 0x01,
    AK4493_REG_CONTROL_6 = 0x0A,
    AK4493_CONTROL_1_RESET_ASSERTED = 0x86,
    AK4493_CONTROL_1_RESET_RELEASED = 0x87,
    AK4493_CONTROL_2_MUTED = 0x21,
    AK4493_CONTROL_2_UNMUTED = 0x20,
    AK4493_CONTROL_6_POWER_OFF = 0x00,
    AK4493_CONTROL_6_POWER_ON = 0x04
};

typedef struct {
    uint8_t reg;
    uint8_t value;
} register_write_t;

static int write_register(const mt_ak4493_bus_t *bus, uint8_t reg,
                          uint8_t value)
{
    return bus->write(bus->context, MT_AK4493_I2C_ADDRESS, reg, value);
}

int mt_ak4493_set_mute(const mt_ak4493_bus_t *bus, bool mute)
{
    if (bus == NULL || bus->write == NULL) {
        return -1;
    }
    return write_register(bus, AK4493_REG_CONTROL_2,
                          mute ? AK4493_CONTROL_2_MUTED :
                                 AK4493_CONTROL_2_UNMUTED);
}

int mt_ak4493_initialize(const mt_ak4493_bus_t *bus, bool unmute)
{
    static const register_write_t image[] = {
        {0x00, AK4493_CONTROL_1_RESET_ASSERTED},
        {0x01, AK4493_CONTROL_2_MUTED},
        {0x02, 0x00},
        {0x03, 0xFF},
        {0x04, 0xFF},
        {0x05, 0x00},
        {0x06, 0x00},
        {0x07, 0x00},
        {0x08, 0x00},
        {0x09, 0x00},
        {0x0A, AK4493_CONTROL_6_POWER_ON},
        {0x0B, 0x00},
        {0x15, 0x00}
    };
    size_t index;
    int status;

    if (bus == NULL || bus->write == NULL || bus->delay_us == NULL) {
        return -1;
    }

    status = mt_ak4493_set_mute(bus, true);
    if (status != 0) return status;
    status = write_register(bus, AK4493_REG_CONTROL_6,
                            AK4493_CONTROL_6_POWER_OFF);
    if (status != 0) return status;
    status = write_register(bus, AK4493_REG_CONTROL_1,
                            AK4493_CONTROL_1_RESET_ASSERTED);
    if (status != 0) return status;
    bus->delay_us(bus->context, UINT32_C(1000));

    for (index = 0U; index < sizeof(image) / sizeof(image[0]); ++index) {
        status = write_register(bus, image[index].reg, image[index].value);
        if (status != 0) return status;
    }

    bus->delay_us(bus->context, UINT32_C(1000));
    status = write_register(bus, AK4493_REG_CONTROL_1,
                            AK4493_CONTROL_1_RESET_RELEASED);
    if (status != 0) return status;
    bus->delay_us(bus->context, UINT32_C(10000));

    return unmute ? mt_ak4493_set_mute(bus, false) : 0;
}
