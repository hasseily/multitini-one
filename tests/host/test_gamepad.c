/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_gamepad.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static void test_extended_partial_reports(void)
{
    mt_gamepad_input_t input;
    mt_gamepad_state_t mapped;
    mt_gamepad_report_t report = {0};
    mt_gamepad_init(&input);
    assert(mt_gamepad_connect(&input, 0U) == 0);
    report.generic_axes = 1U;
    report.axis_valid_mask = (1U << MT_INPUT_AXIS_SLIDER) | (1U << MT_INPUT_AXIS_WHEEL) | (1U << MT_INPUT_AXIS_PAN);
    report.relative_mask = (1U << MT_INPUT_AXIS_WHEEL) | (1U << MT_INPUT_AXIS_PAN);
    report.axis[MT_INPUT_AXIS_SLIDER] = 0;
    report.logical_max[MT_INPUT_AXIS_SLIDER] = 255;
    report.axis[MT_INPUT_AXIS_WHEEL] = 1;
    report.axis[MT_INPUT_AXIS_PAN] = INT32_MIN;
    mt_gamepad_report(&input, 0U, &report, UINT8_MAX, 0U);
    assert(input.raw[0].generic_axes && input.raw[0].axis_valid_mask == 0x340U);
    assert(input.raw[0].axis[MT_INPUT_AXIS_SLIDER] == INT16_MIN);
    assert(input.raw[0].axis[MT_INPUT_AXIS_WHEEL] == 1);
    assert(input.raw[0].axis[MT_INPUT_AXIS_PAN] == INT16_MIN);
    assert(input.raw[0].relative_mask == 0x300U);
    /* An unrelated button report cannot replay, release or renormalize axes. */
    memset(&report, 0, sizeof(report)); report.generic_axes = 1U;
    report.buttons_valid = 1U; report.buttons_mask = 16U; report.buttons = 16U;
    mt_gamepad_report(&input, 0U, &report, UINT8_MAX, 0U);
    assert(input.raw[0].buttons == 16U && input.raw[0].axis[MT_INPUT_AXIS_WHEEL] == 1);
    assert(input.raw[0].relative_mask == 0x300U);
    report.buttons_valid = 0U;
    report.axis_valid_mask = report.relative_mask = 1U << MT_INPUT_AXIS_WHEEL;
    report.axis[MT_INPUT_AXIS_WHEEL] = 0;
    mt_gamepad_report(&input, 0U, &report, UINT8_MAX, 0U);
    assert(input.raw[0].axis[MT_INPUT_AXIS_WHEEL] == 0 && input.raw[0].buttons == 16U);
    assert(input.raw[0].axis[MT_INPUT_AXIS_PAN] == INT16_MIN);
    mt_gamepad_mapped(&input, 0U, &mapped);
    assert(mapped.axis_valid_mask == 0x340U && mapped.relative_mask == 0x300U);
    assert(mapped.axis[MT_INPUT_AXIS_PAN] == INT16_MIN);
    /* Relative mapped axes bypass the old stick dead zone and remap flags. */
    report.axis_valid_mask = report.relative_mask = 1U << MT_GAMEPAD_AXIS_RX;
    report.axis[MT_GAMEPAD_AXIS_RX] = 1;
    mt_gamepad_report(&input, 0U, &report, UINT8_MAX, 0U);
    input.config[0].axis[0].source = MT_GAMEPAD_AXIS_RX;
    input.config[0].axis[0].deadzone_percent = 50U;
    input.config[0].axis[0].sensitivity_percent = 25U;
    input.config[0].axis[0].invert = 1U;
    mt_gamepad_mapped(&input, 0U, &mapped);
    assert(mapped.axis[0] == -1 && (mapped.relative_mask & 1U) != 0U);
    mt_gamepad_disconnect(&input, 0U);
    assert(!input.raw[0].connected && input.raw[0].relative_mask == 0U &&
           input.raw[0].axis_valid_mask == 0U && input.raw[0].axis[MT_INPUT_AXIS_PAN] == 0);
}

int main(void)
{
    test_extended_partial_reports();
    mt_gamepad_input_t input;
    mt_gamepad_state_t state;
    mt_gamepad_report_t report = {0};
    mt_gamepad_init(&input);
    assert(mt_gamepad_connect(&input, 0U) == 0); /* HID minor 0 */
    assert(mt_gamepad_connect(&input, 0U) == 1); /* vendor minor 0 */
    assert(mt_gamepad_connect(&input, 8U) == 8); /* existing Xbox 360 */
    for (unsigned i = 2; i < 8; ++i)
        assert(mt_gamepad_connect(&input, 0U) == (int)i);
    assert(mt_gamepad_connect(&input, 0U) == -1);
    mt_gamepad_disconnect(&input, 1U);
    assert(mt_gamepad_connect(&input, 0U) == 1);
    report.axis_valid_mask = 1U;
    report.axis[0] = INT32_MAX;
    report.logical_min[0] = INT32_MIN;
    report.logical_max[0] = INT32_MAX;
    report.buttons_valid = 1U;
    report.buttons = 0xa5U;
    mt_gamepad_report(&input, 1U, &report, 7U, 1U);
    mt_gamepad_mapped(&input, 1U, &state);
    assert(state.axis[0] == INT16_MAX && state.buttons == 0xa5U && state.hat == 7U);
    memset(&report, 0, sizeof(report));
    report.axis_valid_mask = 2U;
    report.axis[1] = 0;
    report.logical_max[1] = 255;
    report.buttons_valid = 1U;
    report.buttons_mask = 2U;
    report.buttons = 2U;
    mt_gamepad_report(&input, 1U, &report, UINT8_MAX, 0U);
    mt_gamepad_mapped(&input, 1U, &state);
    assert(state.axis[0] == INT16_MAX && state.axis[1] == INT16_MIN);
    assert(state.buttons == 0xa7U && state.hat == 7U && state.extra_active == 0U);
    input.config[1].axis[0].source = MT_GAMEPAD_AXIS_Y;
    input.config[1].axis[0].invert = 1U;
    input.config[1].axis[1].source = MT_GAMEPAD_AXIS_OFF;
    input.config[1].button[0] = 7U;
    input.config[1].button[1] = 8U;
    mt_gamepad_mapped(&input, 1U, &state);
    assert(state.axis[0] == INT16_MAX && state.axis[1] == 0);
    assert((state.axis_valid_mask & 3U) == 1U && (state.buttons & 3U) == 1U);
    /* Sweeps cover monotonicity, deadzone, gain, saturation and no overflow. */
    input.config[1].axis[0].invert = 0U;
    input.config[1].axis[0].deadzone_percent = 25U;
    for (unsigned gain = 25; gain <= 200; gain += 25) {
        input.config[1].axis[0].sensitivity_percent = (uint16_t)gain;
        int previous = INT16_MIN;
        for (int value = 0; value <= 255; ++value) {
            report.axis[1] = value;
            mt_gamepad_report(&input, 1U, &report, 8U, 0U);
            mt_gamepad_mapped(&input, 1U, &state);
            assert(state.axis[0] >= previous);
            if (value >= 96 && value <= 159) assert(state.axis[0] == 0);
            previous = state.axis[0];
        }
    }
    assert(mt_gamepad_config_valid(&input.config[1]));
    input.config[1].axis[0].sensitivity_percent = 0U;
    assert(!mt_gamepad_config_valid(&input.config[1]));
    mt_gamepad_disconnect(&input, 1U);
    mt_gamepad_mapped(&input, 1U, &state);
    assert(!state.connected && state.buttons == 0 && state.axis[0] == 0 && state.hat == 8);
    assert(input.allocated == 0x1fdU); /* Other controllers survive disconnect. */
    puts("PASS gamepad slots, partial reports, mapping, bounds, deadzone and gain");
    return 0;
}
