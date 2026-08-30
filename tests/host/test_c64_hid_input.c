/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_hid_input.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t code[256];
    uint16_t value[256];
    size_t count;
    int fail_once;
} event_log_t;

static int record_event(void *context, uint16_t code, uint16_t value)
{
    event_log_t *const log = context;
    if (log->fail_once != 0) {
        log->fail_once = 0;
        return -1;
    }
    assert(log->count < 256U);
    log->code[log->count] = code;
    log->value[log->count] = value;
    ++log->count;
    return 0;
}

static void clear_log(event_log_t *log)
{
    log->count = 0U;
}

static bool has_event(const event_log_t *log, size_t start, size_t count,
                      uint16_t code, uint16_t value)
{
    for (size_t index = start; index < start + count; ++index) {
        if (log->code[index] == code && log->value[index] == value) {
            return true;
        }
    }
    return false;
}

static void expect_key_mapping(mt_c64_keyboard_layout_t layout,
                               uint8_t modifiers, uint8_t usage,
                               uint16_t code0, uint16_t value0,
                               uint16_t code1, uint16_t value1,
                               size_t expected_count)
{
    mt_c64_hid_input_t input;
    event_log_t log = {0};
    const uint8_t key[6] = {usage, 0U, 0U, 0U, 0U, 0U};
    const uint8_t no_keys[6] = {0};

    assert(expected_count <= 2U);
    mt_c64_hid_input_init(&input, record_event, &log);
    assert(mt_c64_hid_set_keyboard_layout(&input, layout) == 0);
    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, modifiers, key,
                                      sizeof(key)) == 0);
    assert(log.count == expected_count);
    if (expected_count > 0U) {
        assert(has_event(&log, 0U, expected_count, code0, value0));
    }
    if (expected_count > 1U) {
        assert(has_event(&log, 0U, expected_count, code1, value1));
    }

    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(log.count == expected_count * 2U);
    if (expected_count > 0U) {
        assert(has_event(&log, expected_count, expected_count, code0,
                         (uint16_t)(value0 & UINT16_C(2))));
    }
    if (expected_count > 1U) {
        assert(has_event(&log, expected_count, expected_count, code1,
                         (uint16_t)(value1 & UINT16_C(2))));
    }
}

static void expect_alias_aggregation(uint8_t modifiers0, uint8_t usage0,
                                     uint8_t modifiers1, uint8_t usage1,
                                     uint16_t expected_code)
{
    mt_c64_hid_input_t input;
    event_log_t log = {0};
    const uint8_t keys0[6] = {usage0, 0U, 0U, 0U, 0U, 0U};
    const uint8_t keys1[6] = {usage1, 0U, 0U, 0U, 0U, 0U};

    mt_c64_hid_input_init(&input, record_event, &log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, modifiers0, keys0,
                                      sizeof(keys0)) == 0);
    assert(mt_c64_hid_keyboard_report(&input, 1U, modifiers1, keys1,
                                      sizeof(keys1)) == 0);
    assert(log.count == 1U && log.code[0] == expected_code &&
           log.value[0] == 1U);
    assert(mt_c64_hid_disconnect(&input, 0U) == 0);
    assert(log.count == 1U);
    assert(mt_c64_hid_disconnect(&input, 1U) == 0);
    assert(log.count == 2U && log.code[1] == expected_code &&
           log.value[1] == 0U);
}

int main(void)
{
    mt_c64_hid_input_t input;
    event_log_t log = {0};
    const uint8_t key_a[6] = {0x04U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t key_equal[6] = {0x2EU, 0U, 0U, 0U, 0U, 0U};
    const uint8_t key_up[6] = {0x52U, 0U, 0U, 0U, 0U, 0U};
    const uint8_t rollover[6] = {0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U};
    const uint8_t no_keys[6] = {0};

    mt_c64_hid_input_init(&input, record_event, &log);
    assert(input.joystick_port == 2U);

    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_a,
                                      sizeof(key_a)) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x001C) &&
           log.value[0] == UINT16_C(1));
    assert(mt_c64_hid_keyboard_report(&input, 1U, 0U, key_a,
                                      sizeof(key_a)) == 0);
    assert(log.count == 1U);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(log.count == 1U);
    assert(mt_c64_hid_disconnect(&input, 1U) == 0);
    assert(log.count == 2U && log.code[1] == UINT16_C(0x001C) &&
           log.value[1] == UINT16_C(0));

    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0x02U, key_a,
                                      sizeof(key_a)) == 0);
    assert(log.count == 2U);
    assert(log.code[0] == UINT16_C(0x0012) && log.value[0] == UINT16_C(1));
    assert(log.code[1] == UINT16_C(0x001C) && log.value[1] == UINT16_C(1));
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, rollover,
                                      sizeof(rollover)) == -2);
    assert(log.count == 2U);
    assert(mt_c64_hid_disconnect(&input, 0U) == 0);
    assert(log.count == 4U);

    assert(mt_c64_hid_get_keyboard_layout(&input) ==
           MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC);

    /* US symbolic punctuation and its explicit C64/PETSCII fallbacks. */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x1FU, 0x1EU, 1U, 0U, 0U, 1U); /* 2 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x1FU, 0x54U, 1U, 0U, 0U, 1U); /* @ */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x23U, 0x36U, 1U, 0U, 0U, 1U); /* 6 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x23U, 0x01U, 1U, 0U, 0U, 1U); /* ^ */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x25U, 0x3EU, 1U, 0U, 0U, 1U); /* 8 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x25U, 0x7CU, 1U, 0U, 0U, 1U); /* * */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x2DU, 0x4EU, 1U, 0U, 0U, 1U); /* - */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x2DU, 0x0DU, 1U, 0x54U, 1U, 2U); /* _ */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x2EU, 0x09U, 1U, 0U, 0U, 1U); /* = */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x2EU, 0x55U, 1U, 0U, 0U, 1U); /* + */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x20U, 0x2EU, 0x55U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x2FU, 0x12U, 1U, 0x4CU, 1U, 2U); /* [ */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x2FU, 0x12U, 1U, 0x4CU, 1U, 2U); /* { -> [ */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x30U, 0x12U, 1U, 0x52U, 1U, 2U); /* ] */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x30U, 0x12U, 1U, 0x52U, 1U, 2U); /* } -> ] */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x33U, 0x52U, 1U, 0U, 0U, 1U); /* ; */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x33U, 0x4CU, 1U, 0U, 0U, 1U); /* : */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x34U, 0x12U, 1U, 0x36U, 1U, 2U); /* ' */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x34U, 0x12U, 1U, 0x1EU, 1U, 2U); /* \" */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x31U, 0x5DU, 1U, 0U, 0U, 1U); /* pound */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x31U, 0x12U, 1U, 0x5DU, 1U, 2U); /* | fallback */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x35U, 0x0EU, 1U, 0U, 0U, 1U); /* left-arrow */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x35U, 0x0EU, 1U, 0x12U, 1U, 2U); /* ~ fallback */

    /* Ordinary shifted keys remain physical Set-2 chords for FPGA64's map. */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x04U, 0x12U, 1U, 0x1CU, 1U, 2U); /* A */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x24U, 0x12U, 1U, 0x3DU, 1U, 2U); /* & */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x26U, 0x12U, 1U, 0x46U, 1U, 2U); /* ( */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x27U, 0x12U, 1U, 0x45U, 1U, 2U); /* ) */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x36U, 0x41U, 1U, 0U, 0U, 1U); /* , */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x36U, 0x12U, 1U, 0x41U, 1U, 2U); /* < */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x37U, 0x49U, 1U, 0U, 0U, 1U); /* . */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x37U, 0x12U, 1U, 0x49U, 1U, 2U); /* > */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x38U, 0x4AU, 1U, 0U, 0U, 1U); /* / */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0x02U, 0x38U, 0x12U, 1U, 0x4AU, 1U, 2U); /* ? */

    /* Symbolic function-key raw access and unchanged keypad operators. */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x42U, 0x01U, 1U, 0U, 0U, 1U); /* F9 up */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x43U, 0x55U, 1U, 0U, 0U, 1U); /* F10 plus */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x44U, 0x78U, 1U, 0U, 0U, 1U); /* F11 restore */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x45U, 0x09U, 1U, 0U, 0U, 1U); /* F12 equals */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x54U, 0x4AU, 1U, 0U, 0U, 1U); /* keypad / */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x55U, 0x7CU, 1U, 0U, 0U, 1U); /* keypad * */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x56U, 0x4EU, 1U, 0U, 0U, 1U); /* keypad - */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x57U, 0x55U, 1U, 0U, 0U, 1U); /* keypad + */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x58U, 0x5AU, 1U, 0U, 0U, 1U); /* keypad Enter */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x59U, 0x16U, 1U, 0U, 0U, 1U); /* keypad 1 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x5AU, 0x1EU, 1U, 0U, 0U, 1U); /* keypad 2 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x5BU, 0x26U, 1U, 0U, 0U, 1U); /* keypad 3 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x5CU, 0x25U, 1U, 0U, 0U, 1U); /* keypad 4 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x5DU, 0x2EU, 1U, 0U, 0U, 1U); /* keypad 5 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x5EU, 0x36U, 1U, 0U, 0U, 1U); /* keypad 6 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x5FU, 0x3DU, 1U, 0U, 0U, 1U); /* keypad 7 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x60U, 0x3EU, 1U, 0U, 0U, 1U); /* keypad 8 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x61U, 0x46U, 1U, 0U, 0U, 1U); /* keypad 9 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x62U, 0x45U, 1U, 0U, 0U, 1U); /* keypad 0 */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x63U, 0x49U, 1U, 0U, 0U, 1U); /* keypad . */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x67U, 0x09U, 1U, 0U, 0U, 1U); /* keypad = */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x4CU, 0x66U, 1U, 0U, 0U, 1U); /* Delete */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x4DU, 0x09U, 1U, 0U, 0U, 1U); /* End */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC,
                       0U, 0x4EU, 0x01U, 1U, 0U, 0U, 1U); /* Page Down */

    /* Positional mode is the original translator, including raw F10/F12. */
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x1FU, 0x12U, 1U, 0x1EU, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x23U, 0x12U, 1U, 0x36U, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x25U, 0x12U, 1U, 0x3EU, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x2DU, 0x12U, 1U, 0x4EU, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x2EU, 0x55U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x2EU, 0x12U, 1U, 0x55U, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x2FU, 0x54U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x2FU, 0x12U, 1U, 0x54U, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x30U, 0x5BU, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x30U, 0x12U, 1U, 0x5BU, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x33U, 0x4CU, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x33U, 0x12U, 1U, 0x4CU, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x34U, 0x52U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0x02U, 0x34U, 0x12U, 1U, 0x52U, 1U, 2U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x42U, 0x01U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x43U, 0x09U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x44U, 0x78U, 1U, 0U, 0U, 1U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x45U, 0U, 0U, 0U, 0U, 0U);
    expect_key_mapping(MT_C64_KEYBOARD_LAYOUT_POSITIONAL,
                       0U, 0x67U, 0U, 0U, 0U, 0U, 0U);

    /* Shift consumption is limited to a sole transformed nonmodifier. */
    {
        const uint8_t shifted_a_plus[6] = {
            0x04U, 0x2EU, 0U, 0U, 0U, 0U,
        };
        const uint8_t bracket_a[6] = {
            0x2FU, 0x04U, 0U, 0U, 0U, 0U,
        };
        mt_c64_hid_input_t chord_input;
        event_log_t chord_log = {0};

        mt_c64_hid_input_init(&chord_input, record_event, &chord_log);
        assert(mt_c64_hid_keyboard_report(&chord_input, 0U, 0x02U,
                                          shifted_a_plus,
                                          sizeof(shifted_a_plus)) == 0);
        assert(chord_log.count == 3U);
        assert(has_event(&chord_log, 0U, 3U, 0x12U, 1U));
        assert(has_event(&chord_log, 0U, 3U, 0x1CU, 1U));
        assert(has_event(&chord_log, 0U, 3U, 0x55U, 1U));
        assert(mt_c64_hid_release_all(&chord_input) == 0);
        clear_log(&chord_log);
        assert(mt_c64_hid_keyboard_report(&chord_input, 0U, 0U,
                                          bracket_a,
                                          sizeof(bracket_a)) == 0);
        assert(chord_log.count == 2U);
        assert(has_event(&chord_log, 0U, 2U, 0x1CU, 1U));
        assert(has_event(&chord_log, 0U, 2U, 0x54U, 1U));
        assert(!has_event(&chord_log, 0U, 2U, 0x12U, 1U));
        assert(mt_c64_hid_release_all(&chord_input) == 0);
    }

    /* Switching layouts remaps held state with releases before presses. */
    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_equal,
                                      sizeof(key_equal)) == 0);
    assert(log.count == 1U && log.code[0] == 0x09U && log.value[0] == 1U);
    assert(mt_c64_hid_set_keyboard_layout(
               &input, MT_C64_KEYBOARD_LAYOUT_POSITIONAL) == 0);
    assert(log.count == 3U);
    assert(log.code[1] == 0x09U && log.value[1] == 0U);
    assert(log.code[2] == 0x55U && log.value[2] == 1U);
    assert(mt_c64_hid_get_keyboard_layout(&input) ==
           MT_C64_KEYBOARD_LAYOUT_POSITIONAL);
    assert(mt_c64_hid_set_keyboard_layout(
               &input, MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC) == 0);
    assert(log.count == 5U);
    assert(log.code[3] == 0x55U && log.value[3] == 0U);
    assert(log.code[4] == 0x09U && log.value[4] == 1U);
    assert(mt_c64_hid_set_keyboard_layout(
               &input, (mt_c64_keyboard_layout_t)99) == -1);
    assert(mt_c64_hid_get_keyboard_layout(&input) ==
           MT_C64_KEYBOARD_LAYOUT_US_SYMBOLIC);
    assert(log.count == 5U);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);

    /* Synthetic modifiers press before and release after their symbol. */
    {
        const uint8_t left_bracket[6] = {
            0x2FU, 0U, 0U, 0U, 0U, 0U,
        };
        mt_c64_hid_input_t bracket_input;
        event_log_t bracket_log = {0};

        mt_c64_hid_input_init(&bracket_input, record_event, &bracket_log);
        assert(mt_c64_hid_keyboard_report(&bracket_input, 0U, 0U,
                                          left_bracket,
                                          sizeof(left_bracket)) == 0);
        assert(bracket_log.count == 2U);
        assert(bracket_log.code[0] == 0x12U &&
               bracket_log.value[0] == 1U);
        assert(bracket_log.code[1] == 0x4CU &&
               bracket_log.value[1] == 1U);
        assert(mt_c64_hid_keyboard_report(&bracket_input, 0U, 0U, no_keys,
                                          sizeof(no_keys)) == 0);
        assert(bracket_log.count == 4U);
        assert(bracket_log.code[2] == 0x4CU &&
               bracket_log.value[2] == 0U);
        assert(bracket_log.code[3] == 0x12U &&
               bracket_log.value[3] == 0U);
    }

    /* Caps Lock is a C64 Shift source, so it brackets ordinary keys. */
    {
        const uint8_t caps_a[6] = {
            0x39U, 0x04U, 0U, 0U, 0U, 0U,
        };
        mt_c64_hid_input_t caps_input;
        event_log_t caps_log = {0};

        mt_c64_hid_input_init(&caps_input, record_event, &caps_log);
        assert(mt_c64_hid_keyboard_report(&caps_input, 0U, 0U, caps_a,
                                          sizeof(caps_a)) == 0);
        assert(caps_log.count == 2U);
        assert(caps_log.code[0] == 0x58U && caps_log.value[0] == 1U);
        assert(caps_log.code[1] == 0x1CU && caps_log.value[1] == 1U);
        assert(mt_c64_hid_keyboard_report(&caps_input, 0U, 0U, no_keys,
                                          sizeof(no_keys)) == 0);
        assert(caps_log.count == 4U);
        assert(caps_log.code[2] == 0x1CU && caps_log.value[2] == 0U);
        assert(caps_log.code[3] == 0x58U && caps_log.value[3] == 0U);
    }

    /* Shifted symbol transitions release keys before restoring host Shift. */
    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0x02U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0x02U, key_equal,
                                      sizeof(key_equal)) == 0);
    assert(log.count == 3U);
    assert(log.code[0] == 0x12U && log.value[0] == 1U);
    assert(log.code[1] == 0x12U && log.value[1] == 0U);
    assert(log.code[2] == 0x55U && log.value[2] == 1U);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0x02U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(log.count == 5U);
    assert(log.code[3] == 0x55U && log.value[3] == 0U);
    assert(log.code[4] == 0x12U && log.value[4] == 1U);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);

    /* Duplicate symbolic chords aggregate until the final slot disconnects. */
    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0x02U, key_equal,
                                      sizeof(key_equal)) == 0);
    assert(mt_c64_hid_keyboard_report(&input, 1U, 0x20U, key_equal,
                                      sizeof(key_equal)) == 0);
    assert(log.count == 1U && log.code[0] == 0x55U);
    assert(mt_c64_hid_disconnect(&input, 0U) == 0);
    assert(log.count == 1U);
    assert(mt_c64_hid_disconnect(&input, 1U) == 0);
    assert(log.count == 2U && log.code[1] == 0x55U && log.value[1] == 0U);

    /* Distinct PC aliases use one Set-2 identity in symbolic mode. */
    expect_alias_aggregation(0U, 0x43U, 0U, 0x57U, 0x55U); /* plus */
    expect_alias_aggregation(0U, 0x28U, 0U, 0x58U, 0x5AU); /* Return */
    expect_alias_aggregation(0U, 0x2AU, 0U, 0x4CU, 0x66U); /* Delete */
    expect_alias_aggregation(0U, 0x38U, 0U, 0x54U, 0x4AU); /* slash */
    expect_alias_aggregation(0U, 0x2DU, 0U, 0x56U, 0x4EU); /* minus */
    expect_alias_aggregation(0x02U, 0x25U, 0U, 0x55U, 0x7CU); /* star */
    expect_alias_aggregation(0U, 0x42U, 0U, 0x4EU, 0x01U); /* up-arrow */
    expect_alias_aggregation(0U, 0x45U, 0U, 0x4DU, 0x09U); /* equals */
    expect_alias_aggregation(0U, 0x1EU, 0U, 0x59U, 0x16U); /* digit 1 */
    expect_alias_aggregation(0U, 0x24U, 0U, 0x5FU, 0x3DU); /* digit 7 */
    expect_alias_aggregation(0U, 0x2BU, 0x04U, 0U, 0x0DU); /* Commodore */
    expect_alias_aggregation(0U, 0x2BU, 0x80U, 0U, 0x0DU); /* right GUI */
    expect_alias_aggregation(0x01U, 0U, 0x10U, 0U, 0x14U); /* Control */

    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_up,
                                      sizeof(key_up)) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x0075) &&
           log.value[0] == UINT16_C(3));
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, no_keys,
                                      sizeof(no_keys)) == 0);
    assert(log.count == 2U && log.value[1] == UINT16_C(2));

    clear_log(&log);
    assert(mt_c64_hid_joystick_report(&input, 2U, 0x51U) == 0);
    assert(log.count == 3U);
    assert(log.code[0] == UINT16_C(0x0200) && log.value[0] == 1U);
    assert(log.code[1] == UINT16_C(0x0204) && log.value[1] == 1U);
    assert(log.code[2] == UINT16_C(0x0206) && log.value[2] == 1U);
    assert(mt_c64_hid_set_joystick_port(&input, 1U) == 0);
    assert(log.count == 9U);
    assert(log.code[3] == UINT16_C(0x0200) && log.value[3] == 0U);
    assert(log.code[6] == UINT16_C(0x0100) && log.value[6] == 1U);
    assert(mt_c64_hid_disconnect(&input, 2U) == 0);
    assert(log.count == 12U);

    /* Slot 8 is reserved for the Xbox aggregate and remains independent. */
    assert(mt_c64_hid_release_all(&input) == 0);
    assert(mt_c64_hid_set_joystick_port(&input, 2U) == 0);
    clear_log(&log);
    assert(mt_c64_hid_joystick_report(&input, 8U, 0x10U) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x0204) &&
           log.value[0] == 1U);
    assert(mt_c64_hid_disconnect(&input, 8U) == 0);
    assert(log.count == 2U && log.code[1] == UINT16_C(0x0204) &&
           log.value[1] == 0U);
    assert(mt_c64_hid_joystick_report(&input, 9U, 0x10U) == -1);

    clear_log(&log);
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_a,
                                      sizeof(key_a)) == 0);
    assert(mt_c64_hid_joystick_report(&input, 0U, 0x08U) == 0);
    clear_log(&log);
    mt_c64_hid_reassert(&input);
    assert(mt_c64_hid_input_poll(&input) == 0);
    assert(log.count == 2U);
    assert(log.code[0] == UINT16_C(0x001C));
    assert(log.code[1] == UINT16_C(0x0203));

    assert(mt_c64_hid_release_all(&input) == 0);
    clear_log(&log);
    log.fail_once = 1;
    assert(mt_c64_hid_keyboard_report(&input, 0U, 0U, key_a,
                                      sizeof(key_a)) == -1);
    assert(log.count == 0U);
    assert(mt_c64_hid_input_poll(&input) == 0);
    assert(log.count == 1U && log.code[0] == UINT16_C(0x001C));

    return 0;
}
