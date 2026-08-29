/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_ak4493.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t addresses[32];
    uint8_t registers[32];
    uint8_t values[32];
    uint32_t delays[8];
    size_t write_count;
    size_t delay_count;
    size_t fail_write;
} mock_bus_t;

static int mock_write(void *context, uint8_t address, uint8_t reg,
                      uint8_t value)
{
    mock_bus_t *const bus = context;
    const size_t index = bus->write_count++;
    bus->addresses[index] = address;
    bus->registers[index] = reg;
    bus->values[index] = value;
    return index == bus->fail_write ? -7 : 0;
}

static void mock_delay(void *context, uint32_t microseconds)
{
    mock_bus_t *const bus = context;
    bus->delays[bus->delay_count++] = microseconds;
}

int main(void)
{
    mock_bus_t muted = {.fail_write = SIZE_MAX};
    mock_bus_t active = {.fail_write = SIZE_MAX};
    mock_bus_t failed = {.fail_write = 4U};
    const mt_ak4493_bus_t muted_bus = {&muted, mock_write, mock_delay};
    const mt_ak4493_bus_t active_bus = {&active, mock_write, mock_delay};
    const mt_ak4493_bus_t failed_bus = {&failed, mock_write, mock_delay};

    assert(mt_ak4493_initialize(NULL, false) == -1);
    assert(mt_ak4493_initialize(&muted_bus, false) == 0);
    assert(muted.write_count == 17U);
    assert(muted.delay_count == 3U);
    assert(muted.delays[0] == 1000U);
    assert(muted.delays[1] == 1000U);
    assert(muted.delays[2] == 10000U);
    assert(muted.addresses[0] == MT_AK4493_I2C_ADDRESS);
    assert(muted.registers[0] == 0x01U && muted.values[0] == 0x21U);
    assert(muted.registers[16] == 0x00U && muted.values[16] == 0x87U);

    assert(mt_ak4493_initialize(&active_bus, true) == 0);
    assert(active.write_count == 18U);
    assert(active.registers[17] == 0x01U && active.values[17] == 0x20U);

    assert(mt_ak4493_initialize(&failed_bus, false) == -7);
    assert(failed.write_count == 5U);
    assert(failed.delay_count == 1U);
    return 0;
}
