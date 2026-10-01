/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_genesis_hid_input.h"
#include "multitini_genesis_interface.h"
#include <string.h>

#define BUTTON(name) ((uint16_t)(1U << MT_GENESIS_BUTTON_##name))

void mt_genesis_hid_input_init(mt_genesis_hid_input_t *input,
    mt_genesis_hid_emit_fn emit, void *context)
{
    if (input == NULL) return;
    memset(input, 0, sizeof(*input));
    memset(input->player, 0xff, sizeof(input->player));
    input->emit = emit;
    input->context = context;
}

static uint16_t neutral_opposites(uint16_t state)
{
    if ((state & (BUTTON(LEFT) | BUTTON(RIGHT))) ==
        (BUTTON(LEFT) | BUTTON(RIGHT)))
        state &= (uint16_t)~(BUTTON(LEFT) | BUTTON(RIGHT));
    if ((state & (BUTTON(UP) | BUTTON(DOWN))) ==
        (BUTTON(UP) | BUTTON(DOWN)))
        state &= (uint16_t)~(BUTTON(UP) | BUTTON(DOWN));
    return state;
}

static void assign_players(mt_genesis_hid_input_t *input)
{
    unsigned slot;
    uint8_t occupied = 0U;
    for (slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot)
        if (input->connected[slot] != 0U && input->player[slot] < 2U)
            occupied |= (uint8_t)(1U << input->player[slot]);
    for (slot = 0U; slot < MT_GAMEPAD_SLOTS && occupied != 3U; ++slot) {
        if (input->connected[slot] == 0U || input->player[slot] < 2U ||
            input->assignment_explicit[slot] != 0U) continue;
        input->player[slot] = (uint8_t)((occupied & 1U) == 0U ? 0U : 1U);
        occupied |= (uint8_t)(1U << input->player[slot]);
    }
}

int mt_genesis_hid_input_poll(mt_genesis_hid_input_t *input)
{
    uint16_t state[2] = {0U, 0U};
    unsigned slot;
    unsigned player;
    if (input == NULL || input->emit == NULL) return -1;
    assign_players(input);
    for (slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot) {
        state[0] |= input->keyboard[slot];
        if (input->connected[slot] != 0U && input->player[slot] < 2U)
            state[input->player[slot]] |= input->gamepad[slot];
    }
    for (player = 0U; player < 2U; ++player) {
        const uint8_t bit = (uint8_t)(1U << player);
        state[player] = neutral_opposites(state[player]);
        if ((input->emitted_valid & bit) != 0U && input->emitted[player] == state[player])
            continue;
        if (input->emit(input->context, (uint16_t)(((uint16_t)MT_INPUT_GENESIS_GAMEPAD << 8U) | player), state[player]) != 0)
            return -1;
        input->emitted[player] = state[player];
        input->emitted_valid |= bit;
    }
    return 0;
}

int mt_genesis_hid_keyboard_report(mt_genesis_hid_input_t *input, uint8_t slot,
    uint8_t modifiers, const uint8_t *keys, size_t count)
{
    uint16_t state = (modifiers & 0x20U) != 0U ? BUTTON(MODE) : 0U;
    size_t index;
    if (input == NULL || slot >= MT_GAMEPAD_SLOTS || (keys == NULL && count != 0U))
        return -1;
    for (index = 0U; index < count; ++index) {
        if (keys[index] != 0U && keys[index] <= 3U) return -2;
        switch (keys[index]) {
        case 0x4fU: state |= BUTTON(RIGHT); break;
        case 0x50U: state |= BUTTON(LEFT); break;
        case 0x51U: state |= BUTTON(DOWN); break;
        case 0x52U: state |= BUTTON(UP); break;
        case 0x1dU: state |= BUTTON(A); break; /* Z */
        case 0x1bU: state |= BUTTON(B); break; /* X */
        case 0x06U: state |= BUTTON(C); break; /* C */
        case 0x04U: state |= BUTTON(X); break; /* A */
        case 0x16U: state |= BUTTON(Y); break; /* S */
        case 0x07U: state |= BUTTON(Z); break; /* D */
        case 0x28U: state |= BUTTON(START); break;
        default: break;
        }
    }
    input->keyboard[slot] = state;
    return mt_genesis_hid_input_poll(input);
}

int mt_genesis_hid_gamepad_report(mt_genesis_hid_input_t *input, uint8_t slot,
    const mt_gamepad_state_t *pad)
{
    static const uint16_t button_map[8] = {
        BUTTON(A), BUTTON(B), BUTTON(C), BUTTON(X),
        BUTTON(Y), BUTTON(Z), BUTTON(MODE), BUTTON(START)
    };
    uint16_t state = 0U;
    uint8_t directions;
    unsigned button;
    if (input == NULL || pad == NULL || slot >= MT_GAMEPAD_SLOTS) return -1;
    if (pad->connected == 0U) return mt_genesis_hid_disconnect(input, slot);
    directions = mt_gamepad_hat_bits(pad->hat);
    if ((pad->axis_valid_mask & (1U << MT_GAMEPAD_AXIS_X)) != 0U) {
        if (pad->axis[MT_GAMEPAD_AXIS_X] < -8192) directions |= 4U;
        if (pad->axis[MT_GAMEPAD_AXIS_X] > 8192) directions |= 8U;
    }
    if ((pad->axis_valid_mask & (1U << MT_GAMEPAD_AXIS_Y)) != 0U) {
        if (pad->axis[MT_GAMEPAD_AXIS_Y] < -8192) directions |= 1U;
        if (pad->axis[MT_GAMEPAD_AXIS_Y] > 8192) directions |= 2U;
    }
    if ((directions & 1U) != 0U) state |= BUTTON(UP);
    if ((directions & 2U) != 0U) state |= BUTTON(DOWN);
    if ((directions & 4U) != 0U) state |= BUTTON(LEFT);
    if ((directions & 8U) != 0U) state |= BUTTON(RIGHT);
    for (button = 0U; button < 8U; ++button)
        if ((pad->buttons & (1U << button)) != 0U) state |= button_map[button];
    input->connected[slot] = 1U;
    input->gamepad[slot] = state;
    return mt_genesis_hid_input_poll(input);
}

int mt_genesis_hid_disconnect(mt_genesis_hid_input_t *input, uint8_t slot)
{
    if (input == NULL || slot >= MT_GAMEPAD_SLOTS) return -1;
    input->keyboard[slot] = 0U;
    input->gamepad[slot] = 0U;
    input->connected[slot] = 0U;
    input->player[slot] = 0xffU;
    return mt_genesis_hid_input_poll(input);
}

int mt_genesis_hid_assigned_report(mt_genesis_hid_input_t *input, uint8_t slot,
    uint8_t player, const mt_gamepad_state_t *pad)
{
    if (input == NULL || slot >= MT_GAMEPAD_SLOTS ||
        (player >= 2U && player != UINT8_MAX)) return -1;
    input->assignment_explicit[slot] = 1U;
    input->player[slot] = player;
    return mt_genesis_hid_gamepad_report(input, slot, pad);
}

int mt_genesis_hid_release_all(mt_genesis_hid_input_t *input)
{
    if (input == NULL) return -1;
    memset(input->keyboard, 0, sizeof(input->keyboard));
    memset(input->gamepad, 0, sizeof(input->gamepad));
    /* Menu capture releases keys without changing which pad owns each port. */
    return mt_genesis_hid_input_poll(input);
}

void mt_genesis_hid_reassert(mt_genesis_hid_input_t *input)
{
    if (input != NULL) input->emitted_valid = 0U;
}
