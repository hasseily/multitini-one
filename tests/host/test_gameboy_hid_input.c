/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_gameboy_hid_input.h"

#include "multitini_gameboy_interface.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t code[64];
    uint16_t value[64];
    size_t count;
    int fail;
} events_t;

static int emit(void *context, uint16_t code, uint16_t value)
{
    events_t *const events = context;
    if (events->fail != 0) return -1;
    assert(events->count < 64U);
    events->code[events->count] = code;
    events->value[events->count++] = value;
    return 0;
}

static uint16_t button_code(uint8_t button)
{
    return (uint16_t)(((uint16_t)MT_INPUT_GB_GAMEPAD << 8U) | button);
}

int main(void)
{
    mt_gameboy_hid_input_t input;
    events_t events = {0};
    const uint8_t arrows_and_keys[] = {0x4fU, 0x52U, 0x1dU, 0x1bU, 0x28U};

    mt_gameboy_hid_input_init(&input, emit, &events);
    assert(mt_gameboy_hid_keyboard_report(
               &input, 0U, 0x20U, arrows_and_keys,
               sizeof(arrows_and_keys)) == 0);
    assert(events.count == 6U);
    assert(events.code[0] == button_code(MT_GB_BUTTON_RIGHT));
    assert(events.code[1] == button_code(MT_GB_BUTTON_UP));
    assert(events.code[2] == button_code(MT_GB_BUTTON_A));
    assert(events.code[3] == button_code(MT_GB_BUTTON_B));
    assert(events.code[4] == button_code(MT_GB_BUTTON_SELECT));
    assert(events.code[5] == button_code(MT_GB_BUTTON_START));

    events.count = 0U;
    assert(mt_gameboy_hid_keyboard_report(&input, 0U, 0U, NULL, 0U) == 0);
    assert(events.count == 6U);
    for (size_t index = 0U; index < events.count; ++index)
        assert(events.value[index] == 0U);

    events.count = 0U;
    assert(mt_gameboy_hid_gamepad_report(&input, 1U, 0xffU) == 0);
    assert(events.count == 8U);
    events.count = 0U;
    assert(mt_gameboy_hid_disconnect(&input, 1U) == 0);
    assert(events.count == 8U);

    events.fail = 1;
    assert(mt_gameboy_hid_gamepad_report(&input, 2U, 0x10U) == -1);
    events.fail = 0;
    assert(mt_gameboy_hid_input_poll(&input) == 0);
    assert(events.code[events.count - 1U] == button_code(MT_GB_BUTTON_A));
    return 0;
}
