/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_genesis_hid_input.h"
#include "multitini_genesis_interface.h"
#include <assert.h>
#include <string.h>
#define B(name) ((uint16_t)(1U << MT_GENESIS_BUTTON_##name))

typedef struct { uint16_t state[2]; unsigned count; int fail; } events_t;
static int emit(void *context, uint16_t code, uint16_t value)
{
    events_t *events = context;
    assert(code == 0x3000U || code == 0x3001U);
    if (events->fail) return -1;
    events->state[code & 1U] = value;
    ++events->count;
    return 0;
}

int main(void)
{
    mt_genesis_hid_input_t input;
    mt_gamepad_state_t pad = {0};
    events_t events = {0};
    const uint8_t keys[] = {0x4fU, 0x52U, 0x1dU, 0x1bU, 0x06U, 0x04U,
                            0x16U, 0x07U, 0x28U};
    const uint8_t rollover[] = {1U};
    mt_genesis_hid_input_init(&input, emit, &events);
    assert(mt_genesis_hid_keyboard_report(&input, 0U, 0x20U, keys, sizeof(keys)) == 0);
    assert(events.state[0] == (B(RIGHT)|B(UP)|B(A)|B(B)|B(C)|B(X)|B(Y)|B(Z)|B(START)|B(MODE)));
    assert(events.state[1] == 0U);
    assert(mt_genesis_hid_keyboard_report(&input, 0U, 0U, rollover, sizeof(rollover)) == -2);
    assert(events.state[0] != 0U);
    assert(mt_genesis_hid_keyboard_report(&input, 0U, 0U, NULL, 0U) == 0);
    assert(events.state[0] == 0U);
    pad.connected = 1U; pad.hat = 8U; pad.buttons = 0xffU;
    assert(mt_genesis_hid_gamepad_report(&input, 5U, &pad) == 0);
    assert(events.state[0] == 0xff0U && input.player[5] == 0U);
    pad.buttons = 2U; pad.hat = 4U;
    assert(mt_genesis_hid_gamepad_report(&input, 1U, &pad) == 0);
    assert(events.state[1] == (B(B)|B(DOWN)) && input.player[1] == 1U);
    /* Disconnecting port 1 does not move the second player's controller. */
    assert(mt_genesis_hid_disconnect(&input, 5U) == 0);
    assert(events.state[0] == 0U && events.state[1] == (B(B)|B(DOWN)));
    pad.buttons = 4U; pad.hat = 8U;
    assert(mt_genesis_hid_gamepad_report(&input, 7U, &pad) == 0);
    assert(events.state[0] == B(C) && input.player[7] == 0U);
    assert(mt_genesis_hid_release_all(&input) == 0);
    assert(events.state[0] == 0U && events.state[1] == 0U);
    assert(input.player[7] == 0U && input.player[1] == 1U);
    pad.buttons = 0U; pad.hat = 6U;
    pad.axis_valid_mask = 3U;
    pad.axis[0] = INT16_MAX; pad.axis[1] = INT16_MIN;
    assert(mt_genesis_hid_gamepad_report(&input, 7U, &pad) == 0);
    assert(events.state[0] == B(UP)); /* left + right neutralize */
    pad.hat = 8U; pad.axis[0] = -9000; pad.axis[1] = 9000;
    events.fail = 1;
    assert(mt_genesis_hid_gamepad_report(&input, 7U, &pad) == -1);
    events.fail = 0;
    assert(mt_genesis_hid_input_poll(&input) == 0);
    assert(events.state[0] == (B(LEFT)|B(DOWN)));
    {
        const unsigned before = events.count;
        mt_genesis_hid_reassert(&input);
        assert(mt_genesis_hid_input_poll(&input) == 0 && events.count == before + 2U);
    }
    pad.connected = 0U;
    assert(mt_genesis_hid_gamepad_report(&input, 7U, &pad) == 0);
    assert(events.state[0] == 0U && input.player[7] == 0xffU);
    assert(mt_genesis_hid_gamepad_report(&input, 9U, &pad) == -1);
    return 0;
}
