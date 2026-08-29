/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_hid_input.h"

#include "multitini_c64_interface.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    uint8_t scan;
    uint8_t extended;
} mt_ps2_key_t;

static bool hid_usage_to_set2(uint8_t usage, mt_ps2_key_t *key)
{
    static const uint8_t letters[26] = {
        0x1CU, 0x32U, 0x21U, 0x23U, 0x24U, 0x2BU, 0x34U,
        0x33U, 0x43U, 0x3BU, 0x42U, 0x4BU, 0x3AU, 0x31U,
        0x44U, 0x4DU, 0x15U, 0x2DU, 0x1BU, 0x2CU, 0x3CU,
        0x2AU, 0x1DU, 0x22U, 0x35U, 0x1AU,
    };
    static const uint8_t numbers[10] = {
        0x16U, 0x1EU, 0x26U, 0x25U, 0x2EU,
        0x36U, 0x3DU, 0x3EU, 0x46U, 0x45U,
    };
    static const uint8_t function_keys[12] = {
        0x05U, 0x06U, 0x04U, 0x0CU, 0x03U, 0x0BU,
        0x83U, 0x0AU, 0x01U, 0x09U, 0x78U, 0x07U,
    };
    static const uint8_t keypad_numbers[10] = {
        0x69U, 0x72U, 0x7AU, 0x6BU, 0x73U,
        0x74U, 0x6CU, 0x75U, 0x7DU, 0x70U,
    };

    if (key == NULL) return false;
    key->scan = 0U;
    key->extended = 0U;

    if (usage >= 0x04U && usage <= 0x1DU) {
        key->scan = letters[usage - 0x04U];
        return true;
    }
    if (usage >= 0x1EU && usage <= 0x27U) {
        key->scan = numbers[usage - 0x1EU];
        return true;
    }
    if (usage >= 0x3AU && usage <= 0x45U) {
        key->scan = function_keys[usage - 0x3AU];
        return usage != 0x45U; /* FPGA64 has no F12 assignment. */
    }
    if (usage >= 0x59U && usage <= 0x62U) {
        key->scan = keypad_numbers[usage - 0x59U];
        return true;
    }

    switch (usage) {
    case 0x28U: key->scan = 0x5AU; break; /* Enter */
    case 0x29U: key->scan = 0x76U; break; /* Escape -> Run/Stop */
    case 0x2AU: key->scan = 0x66U; break; /* Backspace -> Delete */
    case 0x2BU: key->scan = 0x0DU; break; /* Tab -> Commodore */
    case 0x2CU: key->scan = 0x29U; break;
    case 0x2DU: key->scan = 0x4EU; break;
    case 0x2EU: key->scan = 0x55U; break;
    case 0x2FU: key->scan = 0x54U; break;
    case 0x30U: key->scan = 0x5BU; break;
    case 0x31U: key->scan = 0x5DU; break;
    case 0x33U: key->scan = 0x4CU; break;
    case 0x34U: key->scan = 0x52U; break;
    case 0x35U: key->scan = 0x0EU; break;
    case 0x36U: key->scan = 0x41U; break;
    case 0x37U: key->scan = 0x49U; break;
    case 0x38U: key->scan = 0x4AU; break;
    case 0x39U: key->scan = 0x58U; break;
    case 0x49U: key->scan = 0x70U; key->extended = 1U; break;
    case 0x4AU: key->scan = 0x6CU; key->extended = 1U; break;
    case 0x4BU: key->scan = 0x7DU; key->extended = 1U; break;
    case 0x4CU: key->scan = 0x71U; key->extended = 1U; break;
    case 0x4DU: key->scan = 0x69U; key->extended = 1U; break;
    case 0x4EU: key->scan = 0x7AU; key->extended = 1U; break;
    case 0x4FU: key->scan = 0x74U; key->extended = 1U; break;
    case 0x50U: key->scan = 0x6BU; key->extended = 1U; break;
    case 0x51U: key->scan = 0x72U; key->extended = 1U; break;
    case 0x52U: key->scan = 0x75U; key->extended = 1U; break;
    case 0x54U: key->scan = 0x4AU; key->extended = 1U; break;
    case 0x55U: key->scan = 0x7CU; break;
    case 0x56U: key->scan = 0x7BU; break;
    case 0x57U: key->scan = 0x79U; break;
    case 0x58U: key->scan = 0x5AU; key->extended = 1U; break;
    case 0x63U: key->scan = 0x49U; break;
    case 0xE0U: key->scan = 0x14U; break;
    case 0xE1U: key->scan = 0x12U; break;
    case 0xE2U: key->scan = 0x11U; break;
    case 0xE3U: key->scan = 0x0DU; break;
    case 0xE4U: key->scan = 0x14U; key->extended = 1U; break;
    case 0xE5U: key->scan = 0x59U; break;
    case 0xE6U: key->scan = 0x11U; key->extended = 1U; break;
    case 0xE7U: key->scan = 0x0DU; key->extended = 1U; break;
    default: return false;
    }
    return true;
}

static void bitmap_set(uint8_t *bitmap, uint16_t bit)
{
    bitmap[bit >> 3U] |= (uint8_t)(1U << (bit & 7U));
}

static bool bitmap_get(const uint8_t *bitmap, uint16_t bit)
{
    return (bitmap[bit >> 3U] & (uint8_t)(1U << (bit & 7U))) != 0U;
}

static int emit_event(mt_c64_hid_input_t *input, uint16_t code,
                      uint16_t value)
{
    if (input->emit == NULL) return -1;
    return input->emit(input->emit_context, code, value);
}

void mt_c64_hid_input_init(mt_c64_hid_input_t *input,
                           mt_c64_hid_emit_fn emit, void *emit_context)
{
    if (input == NULL) return;
    memset(input, 0, sizeof(*input));
    input->joystick_port = 2U;
    input->emitted_joystick_port = 2U;
    input->emit = emit;
    input->emit_context = emit_context;
}

int mt_c64_hid_keyboard_report(mt_c64_hid_input_t *input, uint8_t slot,
                               uint8_t modifiers, const uint8_t *keys,
                               size_t key_count)
{
    uint8_t next[MT_C64_HID_USAGE_BYTES] = {0};
    size_t i;

    if (input == NULL || slot >= MT_C64_HID_SLOT_COUNT ||
        (keys == NULL && key_count != 0U)) {
        return -1;
    }
    for (i = 0U; i < key_count; ++i) {
        if (keys[i] != 0U && keys[i] <= 3U) {
            return -2; /* HID boot rollover/error: retain the last good set. */
        }
    }
    for (i = 0U; i < 8U; ++i) {
        if ((modifiers & (uint8_t)(1U << i)) != 0U) {
            bitmap_set(next, (uint16_t)(0xE0U + i));
        }
    }
    for (i = 0U; i < key_count; ++i) {
        if (keys[i] > 3U) bitmap_set(next, keys[i]);
    }
    memcpy(input->usages[slot], next, sizeof(next));
    return mt_c64_hid_input_poll(input);
}

int mt_c64_hid_joystick_report(mt_c64_hid_input_t *input, uint8_t slot,
                               uint8_t state)
{
    if (input == NULL || slot >= MT_C64_HID_SLOT_COUNT) return -1;
    input->joystick_active[slot] = 1U;
    input->joysticks[slot] = state & 0x7FU;
    return mt_c64_hid_input_poll(input);
}

int mt_c64_hid_disconnect(mt_c64_hid_input_t *input, uint8_t slot)
{
    if (input == NULL || slot >= MT_C64_HID_SLOT_COUNT) return -1;
    memset(input->usages[slot], 0, MT_C64_HID_USAGE_BYTES);
    input->joystick_active[slot] = 0U;
    input->joysticks[slot] = 0U;
    return mt_c64_hid_input_poll(input);
}

int mt_c64_hid_set_joystick_port(mt_c64_hid_input_t *input, uint8_t port)
{
    if (input == NULL || (port != 1U && port != 2U)) return -1;
    input->joystick_port = port;
    return mt_c64_hid_input_poll(input);
}

int mt_c64_hid_release_all(mt_c64_hid_input_t *input)
{
    if (input == NULL) return -1;
    memset(input->usages, 0, sizeof(input->usages));
    memset(input->joysticks, 0, sizeof(input->joysticks));
    memset(input->joystick_active, 0, sizeof(input->joystick_active));
    return mt_c64_hid_input_poll(input);
}

void mt_c64_hid_reassert(mt_c64_hid_input_t *input)
{
    if (input == NULL) return;
    memset(input->emitted_scans, 0, sizeof(input->emitted_scans));
    input->emitted_joystick = 0U;
    input->emitted_joystick_port = input->joystick_port;
}

int mt_c64_hid_input_poll(mt_c64_hid_input_t *input)
{
    uint8_t desired_scans[MT_C64_HID_SCAN_BYTES] = {0};
    uint8_t desired_joystick = 0U;
    uint16_t scan_bit;
    uint8_t slot;
    uint16_t usage;

    if (input == NULL || input->emit == NULL) return -1;

    for (slot = 0U; slot < MT_C64_HID_SLOT_COUNT; ++slot) {
        for (usage = 0U; usage < 256U; ++usage) {
            mt_ps2_key_t key;
            if (!bitmap_get(input->usages[slot], usage) ||
                !hid_usage_to_set2((uint8_t)usage, &key)) {
                continue;
            }
            scan_bit = (uint16_t)((uint16_t)key.scan +
                       (key.extended != 0U ? UINT16_C(256) : UINT16_C(0)));
            bitmap_set(desired_scans, scan_bit);
        }
        if (input->joystick_active[slot] != 0U) {
            desired_joystick |= input->joysticks[slot];
        }
    }

    for (scan_bit = 0U; scan_bit < 512U; ++scan_bit) {
        const bool desired = bitmap_get(desired_scans, scan_bit);
        const bool emitted = bitmap_get(input->emitted_scans, scan_bit);
        uint16_t value;
        if (desired == emitted) continue;
        value = (uint16_t)(desired ? 1U : 0U);
        if (scan_bit >= 256U) value |= UINT16_C(2);
        if (emit_event(input, (uint16_t)(scan_bit & UINT16_C(0x00FF)),
                       value) != 0) {
            return -1;
        }
        if (desired) {
            bitmap_set(input->emitted_scans, scan_bit);
        } else {
            input->emitted_scans[scan_bit >> 3U] &=
                (uint8_t)~(uint8_t)(1U << (scan_bit & 7U));
        }
    }

    if (input->emitted_joystick_port != input->joystick_port) {
        const uint16_t old_namespace =
            (uint16_t)(input->emitted_joystick_port == 1U ?
                       MT_INPUT_JOYSTICK1 : MT_INPUT_JOYSTICK2);
        uint8_t bit;
        for (bit = 0U; bit < 7U; ++bit) {
            if ((input->emitted_joystick & (uint8_t)(1U << bit)) == 0U) {
                continue;
            }
            if (emit_event(input, (uint16_t)((old_namespace << 8U) | bit),
                           0U) != 0) {
                return -1;
            }
            input->emitted_joystick &= (uint8_t)~(uint8_t)(1U << bit);
        }
        input->emitted_joystick_port = input->joystick_port;
    }

    for (uint8_t bit = 0U; bit < 7U; ++bit) {
        const uint8_t mask = (uint8_t)(1U << bit);
        const bool desired = (desired_joystick & mask) != 0U;
        const bool emitted = (input->emitted_joystick & mask) != 0U;
        const uint16_t joy_namespace =
            (uint16_t)(input->joystick_port == 1U ?
                       MT_INPUT_JOYSTICK1 : MT_INPUT_JOYSTICK2);
        if (desired == emitted) continue;
        if (emit_event(input, (uint16_t)((joy_namespace << 8U) | bit),
                       (uint16_t)(desired ? 1U : 0U)) != 0) {
            return -1;
        }
        if (desired) input->emitted_joystick |= mask;
        else input->emitted_joystick &= (uint8_t)~mask;
    }
    return 0;
}
