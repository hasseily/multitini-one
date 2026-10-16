/* SPDX-License-Identifier: GPL-3.0-only */
/* Mapping and partial-report behavior adapted from Appletini ONE be2ea4f. */
#include "multitini_gamepad.h"
#include <stddef.h>
#include <string.h>

void mt_gamepad_default_config(mt_gamepad_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    for (uint8_t i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        config->axis[i].source = i;
        config->axis[i].sensitivity_percent = 100U;
    }
    for (uint8_t i = 0U; i < 8U; ++i) config->button[i] = i;
}

bool mt_gamepad_config_valid(const mt_gamepad_config_t *config)
{
    if (config == NULL) return false;
    for (unsigned i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        const mt_gamepad_axis_config_t *axis = &config->axis[i];
        if (axis->source > MT_GAMEPAD_AXIS_OFF || axis->invert > 1U ||
            axis->deadzone_percent > 50U || axis->sensitivity_percent < 25U ||
            axis->sensitivity_percent > 200U) return false;
    }
    for (unsigned i = 0U; i < 8U; ++i)
        if (config->button[i] > 8U) return false;
    return true;
}

void mt_gamepad_init(mt_gamepad_input_t *input)
{
    memset(input, 0, sizeof(*input));
    for (unsigned i = 0U; i < MT_GAMEPAD_SLOTS; ++i) {
        input->raw[i].hat = 8U;
        mt_gamepad_default_config(&input->config[i]);
    }
}

int mt_gamepad_connect(mt_gamepad_input_t *input, uint8_t preferred)
{
    if (input == NULL) return -1;
    unsigned slot = preferred;
    if (slot >= MT_GAMEPAD_SLOTS || (input->allocated & (1U << slot)) != 0U) {
        for (slot = 0U; slot < MT_GAMEPAD_SLOTS; ++slot)
            if ((input->allocated & (1U << slot)) == 0U) break;
    }
    if (slot == MT_GAMEPAD_SLOTS) return -1;
    input->allocated |= (uint16_t)(1U << slot);
    memset(&input->raw[slot], 0, sizeof(input->raw[slot]));
    input->raw[slot].hat = 8U;
    return (int)slot;
}

void mt_gamepad_disconnect(mt_gamepad_input_t *input, uint8_t slot)
{
    if (input == NULL || slot >= MT_GAMEPAD_SLOTS) return;
    input->allocated &= (uint16_t)~(1U << slot);
    memset(&input->raw[slot], 0, sizeof(input->raw[slot]));
    input->raw[slot].hat = 8U;
}

static int16_t normalize(int32_t value, int32_t minimum, int32_t maximum)
{
    if (maximum <= minimum) return 0;
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    /* Wide intermediates handle the full signed HID logical range. */
    const int32_t result = (int32_t)((((int64_t)value - minimum) * 65535 /
                      ((int64_t)maximum - minimum)) - 32768);
    return result >= -256 && result <= 256 ? 0 : (int16_t)result;
}

void mt_gamepad_report(mt_gamepad_input_t *input, uint8_t slot,
                       const mt_gamepad_report_t *report, uint8_t hat,
                       uint8_t extra_active)
{
    if (input == NULL || report == NULL || slot >= MT_GAMEPAD_SLOTS ||
        (input->allocated & (1U << slot)) == 0U) return;
    mt_gamepad_state_t *state = &input->raw[slot];
    state->connected = 1U;
    state->generic_axes = report->generic_axes;
    if (report->buttons_valid != 0U) {
        const uint32_t mask = report->buttons_mask == 0U ? UINT32_MAX : report->buttons_mask;
        state->buttons = (state->buttons & ~mask) | (report->buttons & mask);
    }
    for (unsigned i = 0U; i < MT_INPUT_AXIS_COUNT; ++i) {
        if ((report->axis_valid_mask & (1U << i)) == 0U) continue;
        const uint16_t bit = (uint16_t)(1U << i);
        if ((report->relative_mask & bit) != 0U) {
            const int32_t value = report->axis[i];
            state->axis[i] = value > INT16_MAX ? INT16_MAX :
                value < INT16_MIN ? INT16_MIN : (int16_t)value;
        } else state->axis[i] = normalize(report->axis[i], report->logical_min[i], report->logical_max[i]);
        state->axis_valid_mask |= bit;
        state->relative_mask = (uint16_t)((state->relative_mask & (uint16_t)~bit) |
            (report->relative_mask & bit));
        state->trigger_mask = (uint16_t)((state->trigger_mask & (uint16_t)~bit) |
            (report->trigger_mask & bit & (uint16_t)~report->relative_mask));
    }
    /* 255 means absent in a partial report; 8 means explicitly neutral. */
    if (hat != UINT8_MAX) state->hat = hat < 8U ? hat : 8U;
    state->extra_active = extra_active;
}

void mt_gamepad_mapped(const mt_gamepad_input_t *input, uint8_t slot,
                       mt_gamepad_state_t *state)
{
    if (state == NULL) return;
    memset(state, 0, sizeof(*state));
    state->hat = 8U;
    if (input == NULL || slot >= MT_GAMEPAD_SLOTS) return;
    const mt_gamepad_state_t *raw = &input->raw[slot];
    const mt_gamepad_config_t *config = &input->config[slot];
    if (!raw->connected) return;
    *state = *raw;
    state->buttons = 0U;
    const uint16_t extra_mask = (uint16_t)(((1U << MT_INPUT_AXIS_COUNT) - 1U) &
                                         ~((1U << MT_GAMEPAD_AXIS_COUNT) - 1U));
    state->axis_valid_mask &= extra_mask;
    state->trigger_mask &= extra_mask;
    state->relative_mask &= extra_mask;
    for (unsigned i = 0U; i < 8U; ++i)
        if (config->button[i] < 8U && (raw->buttons & (1U << config->button[i])) != 0U)
            state->buttons |= (uint8_t)(1U << i);
    for (unsigned i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        const mt_gamepad_axis_config_t *axis = &config->axis[i];
        state->axis[i] = 0;
        if (axis->source >= MT_GAMEPAD_AXIS_COUNT ||
            (raw->axis_valid_mask & (1U << axis->source)) == 0U) continue;
        int32_t value = raw->axis[axis->source];
        const int32_t sign = value < 0 ? -1 : 1;
        int32_t magnitude = value < 0 ? -value : value;
        const bool relative = (raw->relative_mask & (1U << axis->source)) != 0U;
        const int32_t deadzone = relative ? 0 : (int32_t)axis->deadzone_percent * 32768 / 100;
        magnitude = magnitude <= deadzone ? 0 :
            (magnitude - deadzone) * 32768 / (32768 - deadzone);
        magnitude = magnitude * axis->sensitivity_percent / 100;
        if (relative && value != 0 && magnitude == 0) magnitude = 1;
        value = magnitude * sign * (axis->invert != 0U ? -1 : 1);
        if (value > INT16_MAX) value = INT16_MAX;
        if (value < INT16_MIN) value = INT16_MIN;
        state->axis[i] = (int16_t)value;
        const uint16_t bit = (uint16_t)(1U << i);
        state->axis_valid_mask |= bit;
        if (relative) state->relative_mask |= bit;
        else if ((raw->trigger_mask & (1U << axis->source)) != 0U) state->trigger_mask |= bit;
    }
}

uint8_t mt_gamepad_hat_bits(uint8_t hat)
{
    static const uint8_t bits[9] = {1U, 9U, 8U, 10U, 2U, 6U, 4U, 5U, 0U};
    return bits[hat < 8U ? hat : 8U];
}

uint8_t mt_gamepad_digital(const mt_gamepad_state_t *state)
{
    uint8_t bits = mt_gamepad_hat_bits(state->hat);
    if (state->axis[MT_GAMEPAD_AXIS_X] < -10922) bits |= 4U;
    if (state->axis[MT_GAMEPAD_AXIS_X] > 10922) bits |= 8U;
    if (state->axis[MT_GAMEPAD_AXIS_Y] < -10922) bits |= 1U;
    if (state->axis[MT_GAMEPAD_AXIS_Y] > 10922) bits |= 2U;
    /* Preserve the existing C64/GB four-action encoding. */
    bits = (uint8_t)((unsigned)bits | ((state->buttons & 15U) << 4U));
    bits = (uint8_t)((unsigned)bits | (state->buttons & 0xc0U));
    return bits;
}
