/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_gameboy_hid_input.h"

#include "multitini_gameboy_interface.h"

#include <stdbool.h>
#include <string.h>

enum {
    MT_PAD_UP = 0x01,
    MT_PAD_DOWN = 0x02,
    MT_PAD_LEFT = 0x04,
    MT_PAD_RIGHT = 0x08,
    MT_PAD_A = 0x10,
    MT_PAD_B = 0x20,
    MT_PAD_SELECT = 0x40,
    MT_PAD_START = 0x80
};

static void bitmap_set(uint8_t *bitmap, uint8_t bit)
{
    bitmap[bit >> 3U] |= (uint8_t)(1U << (bit & 7U));
}

static bool bitmap_get(const uint8_t *bitmap, uint8_t bit)
{
    return (bitmap[bit >> 3U] & (uint8_t)(1U << (bit & 7U))) != 0U;
}

static uint8_t normalized_pad_to_buttons(uint8_t state)
{
    uint8_t buttons = 0U;
    if ((state & MT_PAD_RIGHT) != 0U) buttons |= 1U << MT_GB_BUTTON_RIGHT;
    if ((state & MT_PAD_LEFT) != 0U) buttons |= 1U << MT_GB_BUTTON_LEFT;
    if ((state & MT_PAD_DOWN) != 0U) buttons |= 1U << MT_GB_BUTTON_DOWN;
    if ((state & MT_PAD_UP) != 0U) buttons |= 1U << MT_GB_BUTTON_UP;
    if ((state & MT_PAD_A) != 0U) buttons |= 1U << MT_GB_BUTTON_A;
    if ((state & MT_PAD_B) != 0U) buttons |= 1U << MT_GB_BUTTON_B;
    if ((state & MT_PAD_SELECT) != 0U) buttons |= 1U << MT_GB_BUTTON_SELECT;
    if ((state & MT_PAD_START) != 0U) buttons |= 1U << MT_GB_BUTTON_START;
    return buttons;
}

static uint8_t keyboard_buttons(const uint8_t *usages)
{
    uint8_t buttons = 0U;
    /* Arrow keys, Z/X, right Shift, and Enter provide a complete keyboard
     * fallback when no gamepad is connected. */
    if (bitmap_get(usages, 0x4fU)) buttons |= 1U << MT_GB_BUTTON_RIGHT;
    if (bitmap_get(usages, 0x50U)) buttons |= 1U << MT_GB_BUTTON_LEFT;
    if (bitmap_get(usages, 0x51U)) buttons |= 1U << MT_GB_BUTTON_DOWN;
    if (bitmap_get(usages, 0x52U)) buttons |= 1U << MT_GB_BUTTON_UP;
    if (bitmap_get(usages, 0x1dU)) buttons |= 1U << MT_GB_BUTTON_A;
    if (bitmap_get(usages, 0x1bU)) buttons |= 1U << MT_GB_BUTTON_B;
    if (bitmap_get(usages, 0xe5U)) buttons |= 1U << MT_GB_BUTTON_SELECT;
    if (bitmap_get(usages, 0x28U)) buttons |= 1U << MT_GB_BUTTON_START;
    return buttons;
}

void mt_gameboy_hid_input_init(mt_gameboy_hid_input_t *input,
                               mt_gameboy_hid_emit_fn emit,
                               void *emit_context)
{
    if (input == NULL) return;
    memset(input, 0, sizeof(*input));
    input->emit = emit;
    input->emit_context = emit_context;
}

int mt_gameboy_hid_keyboard_report(mt_gameboy_hid_input_t *input,
                                   uint8_t slot, uint8_t modifiers,
                                   const uint8_t *keys, size_t key_count)
{
    uint8_t next[MT_GAMEBOY_HID_USAGE_BYTES] = {0};
    size_t index;
    if (input == NULL || slot >= MT_GAMEBOY_HID_SLOT_COUNT ||
        (keys == NULL && key_count != 0U)) {
        return -1;
    }
    for (index = 0U; index < key_count; ++index) {
        if (keys[index] != 0U && keys[index] <= 3U)
            return -2;
    }
    for (index = 0U; index < 8U; ++index) {
        if ((modifiers & (uint8_t)(1U << index)) != 0U)
            bitmap_set(next, (uint8_t)(0xe0U + index));
    }
    for (index = 0U; index < key_count; ++index) {
        if (keys[index] > 3U) bitmap_set(next, keys[index]);
    }
    memcpy(input->usages[slot], next, sizeof(next));
    return mt_gameboy_hid_input_poll(input);
}

int mt_gameboy_hid_gamepad_report(mt_gameboy_hid_input_t *input,
                                  uint8_t slot, uint8_t state)
{
    if (input == NULL || slot >= MT_GAMEBOY_HID_SLOT_COUNT) return -1;
    input->gamepad_active[slot] = 1U;
    input->gamepads[slot] = state;
    return mt_gameboy_hid_input_poll(input);
}

int mt_gameboy_hid_disconnect(mt_gameboy_hid_input_t *input, uint8_t slot)
{
    if (input == NULL || slot >= MT_GAMEBOY_HID_SLOT_COUNT) return -1;
    memset(input->usages[slot], 0, MT_GAMEBOY_HID_USAGE_BYTES);
    input->gamepads[slot] = 0U;
    input->gamepad_active[slot] = 0U;
    return mt_gameboy_hid_input_poll(input);
}

int mt_gameboy_hid_release_all(mt_gameboy_hid_input_t *input)
{
    if (input == NULL) return -1;
    memset(input->usages, 0, sizeof(input->usages));
    memset(input->gamepads, 0, sizeof(input->gamepads));
    memset(input->gamepad_active, 0, sizeof(input->gamepad_active));
    return mt_gameboy_hid_input_poll(input);
}

void mt_gameboy_hid_reassert(mt_gameboy_hid_input_t *input)
{
    if (input != NULL) input->emitted_buttons = 0U;
}

int mt_gameboy_hid_input_poll(mt_gameboy_hid_input_t *input)
{
    uint8_t desired = 0U;
    uint8_t slot;
    uint8_t button;
    if (input == NULL || input->emit == NULL) return -1;
    for (slot = 0U; slot < MT_GAMEBOY_HID_SLOT_COUNT; ++slot) {
        desired |= keyboard_buttons(input->usages[slot]);
        if (input->gamepad_active[slot] != 0U)
            desired |= normalized_pad_to_buttons(input->gamepads[slot]);
    }
    for (button = 0U; button < 8U; ++button) {
        const uint8_t mask = (uint8_t)(1U << button);
        const bool next = (desired & mask) != 0U;
        const bool emitted = (input->emitted_buttons & mask) != 0U;
        const uint16_t code = (uint16_t)(
            ((uint16_t)MT_INPUT_GB_GAMEPAD << 8U) | button);
        if (next == emitted) continue;
        if (input->emit(input->emit_context, code,
                        (uint16_t)(next ? 1U : 0U)) != 0) {
            return -1;
        }
        if (next) input->emitted_buttons |= mask;
        else input->emitted_buttons &= (uint8_t)~mask;
    }
    return 0;
}
