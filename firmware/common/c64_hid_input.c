/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_hid_input.h"

#include "multitini_c64_interface.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    uint8_t scan;
    uint8_t extended;
} mt_ps2_key_t;

typedef struct {
    mt_ps2_key_t keys[2];
    uint8_t count;
    uint8_t consume_shift;
} mt_key_mapping_t;

static bool hid_usage_to_set2_positional(uint8_t usage, mt_ps2_key_t *key)
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

static void mapping_clear(mt_key_mapping_t *mapping)
{
    memset(mapping, 0, sizeof(*mapping));
}

static void mapping_add(mt_key_mapping_t *mapping, uint8_t scan,
                        uint8_t extended)
{
    if (mapping->count >= 2U) return;
    mapping->keys[mapping->count].scan = scan;
    mapping->keys[mapping->count].extended = extended;
    ++mapping->count;
}

static bool positional_mapping(uint8_t usage, mt_key_mapping_t *mapping)
{
    mt_ps2_key_t key;

    mapping_clear(mapping);
    if (!hid_usage_to_set2_positional(usage, &key)) return false;
    mapping_add(mapping, key.scan, key.extended);
    return true;
}

static bool pc_mapping(uint8_t usage, mt_key_mapping_t *mapping)
{
    if (usage == 0x53U || usage == 0x32U || usage == 0x64U) {
        mapping_clear(mapping);
        mapping_add(mapping, usage == 0x53U ? 0x77U:0x5dU,0U);
        return true;
    }
    if (usage == 0xe3U || usage == 0xe7U) {
        mapping_clear(mapping);
        mapping_add(mapping, usage == 0xe3U ? 0x1fU:0x27U, 1U);
        return true;
    }
    return positional_mapping(usage,mapping);
}

static bool symbolic_mapping(uint8_t usage, bool shifted,
                             bool allow_transform,
                             mt_key_mapping_t *mapping)
{
    mapping_clear(mapping);

    /* F9-F12 retain direct C64 keys so PETSCII chords remain reachable. */
    switch (usage) {
    case 0x42U: mapping_add(mapping, 0x01U, 0U); return true;
    case 0x43U: mapping_add(mapping, 0x55U, 0U); return true;
    case 0x44U: mapping_add(mapping, 0x78U, 0U); return true;
    case 0x45U: mapping_add(mapping, 0x09U, 0U); return true;
    case 0x67U: mapping_add(mapping, 0x09U, 0U); return true; /* keypad = */
    default: break;
    }

    /* These one-key substitutions need no synthetic modifier and remain sane
     * when another ordinary key is held in the same boot report. */
    if (!shifted && usage == 0x2EU) {
        mapping_add(mapping, 0x09U, 0U); /* = */
        return true;
    }
    if (!shifted && usage == 0x33U) {
        mapping_add(mapping, 0x52U, 0U); /* ; */
        return true;
    }

    if (allow_transform) {
        if (shifted) {
            switch (usage) {
            case 0x1FU: /* Shift+2 -> @ */
                mapping_add(mapping, 0x54U, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x23U: /* Shift+6 -> up-arrow */
                mapping_add(mapping, 0x01U, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x25U: /* Shift+8 -> * */
                mapping_add(mapping, 0x7CU, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x2DU: /* Shift+- -> Commodore+@ underscore */
                mapping_add(mapping, 0x0DU, 0U);
                mapping_add(mapping, 0x54U, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x2EU: /* Shift+= -> + */
                mapping_add(mapping, 0x55U, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x2FU: /* { fallback -> closest [ */
                mapping_add(mapping, 0x12U, 0U);
                mapping_add(mapping, 0x4CU, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x30U: /* } fallback -> closest ] */
                mapping_add(mapping, 0x12U, 0U);
                mapping_add(mapping, 0x52U, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x33U: /* Shift+; -> : */
                mapping_add(mapping, 0x4CU, 0U);
                mapping->consume_shift = 1U;
                return true;
            case 0x34U: /* Shift+' -> Shift+2 quote */
                mapping_add(mapping, 0x12U, 0U);
                mapping_add(mapping, 0x1EU, 0U);
                mapping->consume_shift = 1U;
                return true;
            default: break;
            }
        } else {
            switch (usage) {
            case 0x2FU: /* [ -> Shift+colon */
                mapping_add(mapping, 0x12U, 0U);
                mapping_add(mapping, 0x4CU, 0U);
                return true;
            case 0x30U: /* ] -> Shift+semicolon */
                mapping_add(mapping, 0x12U, 0U);
                mapping_add(mapping, 0x52U, 0U);
                return true;
            case 0x34U: /* ' -> C64 Shift+7 */
                mapping_add(mapping, 0x12U, 0U);
                mapping_add(mapping, 0x36U, 0U);
                return true;
            default: break;
            }
        }
    }

    /* Backslash/pipe and grave/tilde deliberately retain the C64
     * pound/shifted-pound and left-arrow/PETSCII positional fallbacks. */
    return positional_mapping(usage, mapping);
}

static void canonicalize_symbolic_mapping(mt_key_mapping_t *mapping)
{
    /* FPGA64 assigns several Set-2 aliases to one C64 key signal. Emit one
     * identity per logical key so releasing one host alias cannot clear
     * another that is still held. Its later top-row remap also overrides the
     * apparent keypad 6..0 assignments, so route every keypad digit through
     * the working physical top-row path in symbolic mode. */
    for (uint8_t index = 0U; index < mapping->count; ++index) {
        mt_ps2_key_t *const key = &mapping->keys[index];

        if (key->extended != 0U) {
            switch (key->scan) {
            case 0x0DU: key->extended = 0U; break; /* GUI -> Commodore */
            case 0x11U: key->scan = 0x0DU; key->extended = 0U; break;
            case 0x14U: key->extended = 0U; break; /* right Control */
            case 0x4AU: key->extended = 0U; break; /* keypad slash */
            case 0x5AU: key->extended = 0U; break; /* keypad Enter */
            case 0x69U: key->scan = 0x09U; key->extended = 0U; break;
            case 0x71U: key->scan = 0x66U; key->extended = 0U; break;
            case 0x7AU: key->scan = 0x01U; key->extended = 0U; break;
            default: break;
            }
            continue;
        }

        switch (key->scan) {
        case 0x11U: key->scan = 0x0DU; break; /* Alt -> Commodore */
        case 0x5BU: key->scan = 0x7CU; break; /* star */
        case 0x69U: key->scan = 0x16U; break; /* keypad 1 */
        case 0x6BU: key->scan = 0x25U; break; /* keypad 4 */
        case 0x6CU: key->scan = 0x3DU; break; /* keypad 7 */
        case 0x70U: key->scan = 0x45U; break; /* keypad 0 */
        case 0x72U: key->scan = 0x1EU; break; /* keypad 2 */
        case 0x73U: key->scan = 0x2EU; break; /* keypad 5 */
        case 0x74U: key->scan = 0x36U; break; /* keypad 6 */
        case 0x75U: key->scan = 0x3EU; break; /* keypad 8 */
        case 0x7DU: key->scan = 0x46U; break; /* keypad 9 */
        case 0x7AU: key->scan = 0x26U; break; /* keypad 3 */
        case 0x7BU: key->scan = 0x4EU; break; /* keypad minus */
        case 0x79U: key->scan = 0x55U; break; /* keypad plus */
        default: break;
        }
    }
}

static void bitmap_set(uint8_t *bitmap, uint16_t bit)
{
    bitmap[bit >> 3U] |= (uint8_t)(1U << (bit & 7U));
}

static bool bitmap_get(const uint8_t *bitmap, uint16_t bit)
{
    return (bitmap[bit >> 3U] & (uint8_t)(1U << (bit & 7U))) != 0U;
}

static bool slot_shifted(const uint8_t *usages)
{
    return bitmap_get(usages, UINT16_C(0xE1)) ||
           bitmap_get(usages, UINT16_C(0xE5));
}

static uint8_t slot_mapped_nonmodifier_count(
    const uint8_t *usages, mt_c64_keyboard_layout_t layout)
{
    uint8_t count = 0U;

    for (uint16_t usage = UINT16_C(0x04); usage < UINT16_C(0xE0);
         ++usage) {
        mt_key_mapping_t mapping;
        bool mapped;

        if (!bitmap_get(usages, usage)) continue;
        mapped = layout == MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC
                     ? symbolic_mapping((uint8_t)usage,
                                        slot_shifted(usages), false, &mapping)
                     : layout == MT_C64_KEYBOARD_LAYOUT_PS2_PC ?
                         pc_mapping((uint8_t)usage,&mapping):positional_mapping((uint8_t)usage, &mapping);
        if (mapped) {
            ++count;
            if (count > 1U) return count;
        }
    }
    return count;
}

static bool scan_is_modifier(uint16_t scan_bit)
{
    switch ((uint8_t)(scan_bit & UINT16_C(0x00FF))) {
    case 0x0DU: /* Commodore */
    case 0x11U: /* Alt -> Commodore */
    case 0x12U: /* left Shift */
    case 0x14U: /* Control */
    case 0x58U: /* Caps Lock participates in FPGA64 Shift */
    case 0x59U: /* right Shift */
        return true;
    default:
        return false;
    }
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
    memset(input->player, 2, sizeof(input->player));
    input->keyboard_layout = MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC;
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

int mt_c64_hid_assigned_report(mt_c64_hid_input_t *input, uint8_t slot,
                               uint8_t player, uint8_t state)
{
    if (input == NULL || slot >= MT_C64_HID_SLOT_COUNT ||
        (player >= 2U && player != UINT8_MAX)) return -1;
    input->player[slot] = player;
    return mt_c64_hid_joystick_report(input, slot, state);
}

int mt_c64_hid_set_joystick_port(mt_c64_hid_input_t *input, uint8_t port)
{
    if (input == NULL || (port != 1U && port != 2U)) return -1;
    input->joystick_port = port;
    return mt_c64_hid_input_poll(input);
}

int mt_c64_hid_set_keyboard_layout(mt_c64_hid_input_t *input,
                                   mt_c64_keyboard_layout_t layout)
{
    if (input == NULL ||
        (layout != MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC &&
         layout != MT_C64_KEYBOARD_LAYOUT_POSITIONAL &&
         layout != MT_C64_KEYBOARD_LAYOUT_PS2_PC)) {
        return -1;
    }
    input->keyboard_layout = layout;
    return mt_c64_hid_input_poll(input);
}

mt_c64_keyboard_layout_t mt_c64_hid_get_keyboard_layout(
    const mt_c64_hid_input_t *input)
{
    if (input == NULL) return MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC;
    return input->keyboard_layout;
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
    memset(input->emitted_joystick, 0, sizeof(input->emitted_joystick));
}

int mt_c64_hid_input_poll(mt_c64_hid_input_t *input)
{
    uint8_t desired_scans[MT_C64_HID_SCAN_BYTES] = {0};
    uint8_t desired_joystick[2] = {0U, 0U};
    uint16_t scan_bit;
    uint8_t slot;
    uint16_t usage;

    if (input == NULL || input->emit == NULL) return -1;

    for (slot = 0U; slot < MT_C64_HID_SLOT_COUNT; ++slot) {
        const bool shifted = slot_shifted(input->usages[slot]);
        const bool allow_transform =
            input->keyboard_layout == MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC &&
            slot_mapped_nonmodifier_count(input->usages[slot],
                                          input->keyboard_layout) == 1U;
        bool consume_shift = false;

        if (allow_transform) {
            for (usage = UINT16_C(0x04); usage < UINT16_C(0xE0); ++usage) {
                mt_key_mapping_t mapping;
                if (bitmap_get(input->usages[slot], usage) &&
                    symbolic_mapping((uint8_t)usage, shifted, true,
                                     &mapping) &&
                    mapping.consume_shift != 0U) {
                    consume_shift = true;
                    break;
                }
            }
        }

        for (usage = 0U; usage < 256U; ++usage) {
            mt_key_mapping_t mapping;
            bool mapped;
            if (!bitmap_get(input->usages[slot], usage) ||
                (consume_shift &&
                 (usage == UINT16_C(0xE1) || usage == UINT16_C(0xE5)))) {
                continue;
            }
            mapped = input->keyboard_layout ==
                             MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC
                         ? symbolic_mapping((uint8_t)usage, shifted,
                                            allow_transform, &mapping)
                         : input->keyboard_layout == MT_C64_KEYBOARD_LAYOUT_PS2_PC ?
                             pc_mapping((uint8_t)usage,&mapping):positional_mapping((uint8_t)usage, &mapping);
            if (!mapped) continue;
            if (input->keyboard_layout ==
                MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC) {
                canonicalize_symbolic_mapping(&mapping);
            }
            for (uint8_t index = 0U; index < mapping.count; ++index) {
                scan_bit = (uint16_t)(
                    (uint16_t)mapping.keys[index].scan +
                    (mapping.keys[index].extended != 0U
                         ? UINT16_C(256) : UINT16_C(0)));
                bitmap_set(desired_scans, scan_bit);
            }
        }
        if (input->joystick_active[slot] != 0U) {
            const uint8_t player = input->player[slot] == 2U ?
                (uint8_t)(input->joystick_port - 1U) : input->player[slot];
            if (player < 2U) desired_joystick[player] |= input->joysticks[slot];
        }
    }

    /* Release old non-modifiers and then modifiers before pressing replacement
     * modifiers and keys. This prevents transient Shift+symbol chords while a
     * semantic mapping changes under a still-held host modifier. */
    for (uint8_t phase = 0U; phase < 4U; ++phase) {
        const bool desired_state = phase >= 2U;
        const bool modifier_phase = phase == 1U || phase == 2U;
        for (scan_bit = 0U; scan_bit < 512U; ++scan_bit) {
            const bool desired = bitmap_get(desired_scans, scan_bit);
            const bool emitted = bitmap_get(input->emitted_scans, scan_bit);
            uint16_t value;
            if (desired == emitted || desired != desired_state ||
                scan_is_modifier(scan_bit) != modifier_phase) {
                continue;
            }
            value = (uint16_t)(desired ? 1U : 0U);
            if (scan_bit >= 256U) value |= UINT16_C(2);
            if (emit_event(input,
                           (uint16_t)(scan_bit & UINT16_C(0x00FF)),
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
    }

    /* Release both old ports before pressing newly assigned inputs. */
    for (uint8_t phase = 0U; phase < 2U; ++phase)
    for (uint8_t player = 0U; player < 2U; ++player)
    for (uint8_t bit = 0U; bit < 7U; ++bit) {
        const uint8_t mask = (uint8_t)(1U << bit);
        const bool desired = (desired_joystick[player] & mask) != 0U;
        const bool emitted = (input->emitted_joystick[player] & mask) != 0U;
        const uint16_t joy_namespace =
            (uint16_t)(player == 0U ?
                       MT_INPUT_JOYSTICK1 : MT_INPUT_JOYSTICK2);
        if (desired == emitted || desired != (phase != 0U)) continue;
        if (emit_event(input, (uint16_t)((joy_namespace << 8U) | bit),
                       (uint16_t)(desired ? 1U : 0U)) != 0) {
            return -1;
        }
        if (desired) input->emitted_joystick[player] |= mask;
        else input->emitted_joystick[player] &= (uint8_t)~mask;
    }
    return 0;
}
