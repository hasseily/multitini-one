/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_gamepad.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
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
