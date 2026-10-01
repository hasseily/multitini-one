/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_controls.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_vectrex_interface.h"
#include <stdio.h>
#include <string.h>

#define BIT(n) (UINT32_C(1) << (n))

uint32_t mt_controls_action_mask(uint32_t core)
{
    const uint32_t directions = 15U, menu = BIT(MT_CONTROL_MENU) | BIT(MT_CONTROL_SCREENSHOT) | BIT(MT_CONTROL_SCREENSHOT_OUTPUT);
    switch (core) {
    case MT_C64_CORE_ID: return directions | BIT(MT_CONTROL_A) | BIT(MT_CONTROL_B) | BIT(MT_CONTROL_C) | menu;
    case MT_GBC_CORE_ID: return directions | BIT(MT_CONTROL_A) | BIT(MT_CONTROL_B) | BIT(MT_CONTROL_START) | BIT(MT_CONTROL_SELECT) | menu;
    case MT_GENESIS_CORE_ID: return UINT16_C(0x0fff) | menu;
    case MT_VECTREX_CORE_ID: return UINT16_C(0x00ff) | BIT(MT_CONTROL_AXIS_X) | BIT(MT_CONTROL_AXIS_Y) | menu;
    default: return core == 0U ? menu : 0U;
    }
}

const char *mt_controls_action_name(uint32_t core, unsigned action)
{
    static const char *const names[MT_CONTROL_COUNT] = {
        "Up", "Down", "Left", "Right", "A", "B", "C", "X", "Y", "Z",
        "Start", "Select", "Stick horizontal", "Stick vertical", "Open Multitini menu",
        "Screenshot: filtered video", "Screenshot: full screen"
    };
    if (action >= MT_CONTROL_COUNT) return "Unknown";
    if (core == MT_C64_CORE_ID && action >= MT_CONTROL_A && action <= MT_CONTROL_C) {
        static const char *const c64[] = { "Fire", "Space", "Control" };
        return c64[action - MT_CONTROL_A];
    }
    if (core == MT_VECTREX_CORE_ID && action >= MT_CONTROL_A && action <= MT_CONTROL_X) {
        static const char *const vec[] = { "Button 1", "Button 2", "Button 3", "Button 4" };
        return vec[action - MT_CONTROL_A];
    }
    if (core == MT_GENESIS_CORE_ID && action == MT_CONTROL_SELECT) return "Mode";
    return names[action];
}

void mt_controls_source_name(const mt_controller_device_t *device, uint16_t source,
                            char *text, size_t capacity)
{
    static const char *const buttons[] = { "A / south", "B / east", "X / west", "Y / north",
        "Left shoulder", "Right shoulder", "Select / Back", "Start / Options",
        "Left stick click", "Right stick click", "Home / Guide", "Left trigger button",
        "Right trigger button", "Touchpad click" };
    static const char *const sony[] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1",
        "Share", "Options", "L3", "R3", "PS", "L2 button", "R2 button", "Touchpad click" };
    static const char *const hats[] = { "D-pad up", "D-pad down", "D-pad left", "D-pad right" };
    static const char *const axes[] = { "X", "Y", "Z", "Rx", "Ry", "Rz" };
    static const char *const vendor_axes[] = { "Left stick X", "Left stick Y", "Left trigger", "Right stick X", "Right stick Y", "Right trigger" };
    const bool vendor = device != NULL && device->identity.transport != MT_CONTROLLER_TRANSPORT_HID;
    if (text == NULL || capacity == 0U) return;
    if (source == 0U) (void)snprintf(text, capacity, "Unassigned");
    else if (source < MT_CONTROL_SOURCE_HAT) {
        const unsigned button = (unsigned)source - MT_CONTROL_SOURCE_BUTTON;
        if (vendor && button < sizeof(buttons) / sizeof(buttons[0]))
            (void)snprintf(text, capacity, "%s", device->identity.transport == MT_CONTROLLER_TRANSPORT_PS4 ? sony[button] : buttons[button]);
        else (void)snprintf(text, capacity, "Button %u", button + 1U);
    } else if (source < MT_CONTROL_SOURCE_AXIS_NEG)
        (void)snprintf(text, capacity, "%s", hats[source - MT_CONTROL_SOURCE_HAT]);
    else if (source < MT_CONTROL_SOURCE_AXIS) {
        const unsigned axis = ((unsigned)source - MT_CONTROL_SOURCE_AXIS_NEG) / 2U;
        const bool positive = (((unsigned)source - MT_CONTROL_SOURCE_AXIS_NEG) & 1U) != 0U;
        (void)snprintf(text, capacity, "%s %s", vendor ? vendor_axes[axis] : axes[axis], positive ? "+" : "-");
    } else if (source < MT_CONTROL_SOURCE_DPAD_STICK)
        (void)snprintf(text, capacity, "%s", vendor ? vendor_axes[source - MT_CONTROL_SOURCE_AXIS] : axes[source - MT_CONTROL_SOURCE_AXIS]);
    else if (source < MT_CONTROL_SOURCE_COUNT) {
        static const char *const directions[] = {"up", "down", "left", "right"};
        (void)snprintf(text, capacity, "D-pad / stick %s", directions[source - MT_CONTROL_SOURCE_DPAD_STICK]);
    }
    else (void)snprintf(text, capacity, "Unavailable");
}

static uint16_t default_button(const mt_controller_device_t *device, unsigned button)
{
    if (button >= 32U) return 0U;
    if (device != NULL && device->buttons_present != 0U &&
        (device->buttons_present & (UINT32_C(1) << button)) == 0U) return 0U;
    return (uint16_t)(MT_CONTROL_SOURCE_BUTTON + button);
}

void mt_controls_default(uint32_t core, const mt_controller_device_t *device,
                         mt_controller_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    for (unsigned i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        config->axis[i].source = (uint8_t)i;
        config->axis[i].deadzone_percent = 10U;
        config->axis[i].sensitivity_percent = 100U;
    }
    const uint32_t actions = mt_controls_action_mask(core);
    for (unsigned i = 0U; i < 4U; ++i)
        if ((actions & BIT(i)) != 0U)
            config->binding[i] = (uint16_t)((core == MT_VECTREX_CORE_ID ? MT_CONTROL_SOURCE_HAT : MT_CONTROL_SOURCE_DPAD_STICK) + i);
    for (unsigned i = MT_CONTROL_A; i <= MT_CONTROL_Z; ++i)
        if ((actions & BIT(i)) != 0U) config->binding[i] = default_button(device, i - MT_CONTROL_A);
    const bool generic = device == NULL || device->identity.transport == MT_CONTROLLER_TRANSPORT_HID;
    if ((actions & BIT(MT_CONTROL_START)) != 0U)
        config->binding[MT_CONTROL_START] = default_button(device, generic ? 9U : 7U);
    if ((actions & BIT(MT_CONTROL_SELECT)) != 0U)
        config->binding[MT_CONTROL_SELECT] = default_button(device, generic ? 8U : 6U);
    if ((actions & BIT(MT_CONTROL_AXIS_X)) != 0U) {
        config->binding[MT_CONTROL_AXIS_X] = MT_CONTROL_SOURCE_AXIS + MT_GAMEPAD_AXIS_X;
        config->binding[MT_CONTROL_AXIS_Y] = MT_CONTROL_SOURCE_AXIS + MT_GAMEPAD_AXIS_Y;
    }
    if ((actions & BIT(MT_CONTROL_MENU)) != 0U && device != NULL && device->home_button < 32U)
        config->binding[MT_CONTROL_MENU] = (uint16_t)(MT_CONTROL_SOURCE_BUTTON + device->home_button);
}

bool mt_controls_config_valid(uint32_t core, const mt_controller_config_t *config)
{
    if (config == NULL || config->assignment > MT_CONTROLLER_OFF || mt_controls_action_mask(core) == 0U ||
        (core == MT_GBC_CORE_ID && config->assignment == MT_CONTROLLER_PORT2)) return false;
    const uint32_t actions = mt_controls_action_mask(core);
    for (unsigned i = 0U; i < MT_CONTROL_COUNT; ++i) {
        const unsigned source = config->binding[i];
        const bool analog = i == MT_CONTROL_AXIS_X || i == MT_CONTROL_AXIS_Y;
        if (source >= MT_CONTROL_SOURCE_COUNT || ((actions & BIT(i)) == 0U && source != 0U)) return false;
        if (source != 0U && (analog != (source >= MT_CONTROL_SOURCE_AXIS && source < MT_CONTROL_SOURCE_DPAD_STICK))) return false;
    }
    for (unsigned i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        const mt_gamepad_axis_config_t *a = &config->axis[i];
        if (a->source > MT_GAMEPAD_AXIS_OFF || a->invert > 1U || a->deadzone_percent > 50U ||
            a->sensitivity_percent < 25U || a->sensitivity_percent > 200U) return false;
    }
    return true;
}

bool mt_controls_identity_model_equal(const mt_controller_identity_t *a, const mt_controller_identity_t *b)
{
    return a != NULL && b != NULL && a->vendor_id == b->vendor_id && a->product_id == b->product_id &&
        a->descriptor_hash == b->descriptor_hash && a->transport == b->transport && a->interface_id == b->interface_id;
}

bool mt_controls_identity_equal(const mt_controller_identity_t *a, const mt_controller_identity_t *b)
{
    if (!mt_controls_identity_model_equal(a, b)) return false;
    if (a->serial_hash != 0U || b->serial_hash != 0U) return a->serial_hash != 0U && a->serial_hash == b->serial_hash;
    return a->port_path == b->port_path;
}

void mt_controls_migrate_legacy(uint32_t core, const mt_controller_device_t *device,
                               const mt_gamepad_config_t *legacy, mt_controller_config_t *config)
{
    mt_controls_default(core, device, config);
    if (config == NULL || !mt_gamepad_config_valid(legacy)) return;
    /* Tuning now belongs to a physical axis. Prioritize the old horizontal
     * and vertical controls when several old logical axes shared a source. */
    for (unsigned i = MT_GAMEPAD_AXIS_COUNT; i-- > 0U;) {
        const unsigned physical = legacy->axis[i].source;
        if (physical < MT_GAMEPAD_AXIS_COUNT) {
            config->axis[physical].invert = legacy->axis[i].invert;
            config->axis[physical].deadzone_percent = legacy->axis[i].deadzone_percent;
            config->axis[physical].sensitivity_percent = legacy->axis[i].sensitivity_percent;
        }
    }
    config->axis[MT_GAMEPAD_AXIS_X].source = legacy->axis[MT_GAMEPAD_AXIS_X].source;
    config->axis[MT_GAMEPAD_AXIS_Y].source = legacy->axis[MT_GAMEPAD_AXIS_Y].source;
    for (unsigned a = MT_CONTROL_A; a <= MT_CONTROL_SELECT; ++a) {
        if ((mt_controls_action_mask(core) & BIT(a)) == 0U) continue;
        unsigned logical = a <= MT_CONTROL_Z ? a - MT_CONTROL_A : a == MT_CONTROL_START ? 7U : 6U;
        unsigned physical = legacy->button[logical];
        if (device != NULL && device->identity.transport == MT_CONTROLLER_TRANSPORT_HID && physical >= 6U && physical < 8U)
            physical = (device->buttons_present & (UINT32_C(1) << (physical + 2U))) != 0U ? physical + 2U : physical;
        config->binding[a] = legacy->button[logical] == 8U ? 0U : default_button(device, physical);
    }
    for (unsigned a = MT_CONTROL_AXIS_X; a <= MT_CONTROL_AXIS_Y; ++a) {
        if ((mt_controls_action_mask(core) & BIT(a)) == 0U) continue;
        const unsigned source = legacy->axis[a - MT_CONTROL_AXIS_X].source;
        config->binding[a] = source < MT_GAMEPAD_AXIS_COUNT ? (uint16_t)(MT_CONTROL_SOURCE_AXIS + source) : 0U;
    }
    /* Menu access intentionally migrates to identified Home, never Start. */
}

static int16_t tuned_axis(const mt_controller_config_t *config, const mt_gamepad_state_t *raw, unsigned axis)
{
    if (axis >= MT_GAMEPAD_AXIS_COUNT || (raw->axis_valid_mask & (1U << axis)) == 0U) return 0;
    const mt_gamepad_axis_config_t *setting = &config->axis[axis];
    int32_t value = raw->axis[axis], sign = value < 0 ? -1 : 1;
    const bool trigger = (raw->trigger_mask & (1U << axis)) != 0U;
    int32_t magnitude = trigger ? value + 32768 : value * sign;
    const int32_t range = trigger ? 65535 : 32768;
    const int32_t dead = (int32_t)setting->deadzone_percent * range / 100;
    magnitude = magnitude <= dead ? 0 : (int32_t)((int64_t)(magnitude - dead) * range / (range - dead));
    magnitude = magnitude * setting->sensitivity_percent / 100;
    if (magnitude > range) magnitude = range;
    value = trigger ? magnitude - 32768 : magnitude * sign;
    if (setting->invert != 0U) value = -value;
    if (value > INT16_MAX) value = INT16_MAX;
    if (value < INT16_MIN) value = INT16_MIN;
    return (int16_t)value;
}

static bool source_pressed(uint16_t source, const mt_controller_config_t *config,
                           const mt_gamepad_state_t *raw, bool held)
{
    if (source >= MT_CONTROL_SOURCE_DPAD_STICK && source < MT_CONTROL_SOURCE_COUNT) {
        const unsigned direction = (unsigned)source - MT_CONTROL_SOURCE_DPAD_STICK;
        const unsigned axis = config->axis[direction < 2U ? MT_GAMEPAD_AXIS_Y : MT_GAMEPAD_AXIS_X].source;
        const uint16_t stick = axis < MT_GAMEPAD_AXIS_COUNT ?
            (uint16_t)(MT_CONTROL_SOURCE_AXIS_NEG + axis * 2U + (direction & 1U)) : 0U;
        return source_pressed((uint16_t)(MT_CONTROL_SOURCE_HAT + direction), config, raw, held) ||
            source_pressed(stick, config, raw, held);
    }
    if (source == 0U || source >= MT_CONTROL_SOURCE_AXIS) return false;
    if (source < MT_CONTROL_SOURCE_HAT) return (raw->buttons & (UINT32_C(1) << (source - 1U))) != 0U;
    if (source < MT_CONTROL_SOURCE_AXIS_NEG)
        return (mt_gamepad_hat_bits(raw->hat) & (1U << (source - MT_CONTROL_SOURCE_HAT))) != 0U;
    const unsigned axis = ((unsigned)source - MT_CONTROL_SOURCE_AXIS_NEG) / 2U;
    if ((raw->axis_valid_mask & (1U << axis)) == 0U) return false;
    const int32_t value = tuned_axis(config, raw, axis);
    const int32_t threshold = held ? 8000 : 11000;
    return (((unsigned)source - MT_CONTROL_SOURCE_AXIS_NEG) & 1U) != 0U ? value > threshold : value < -threshold;
}

void mt_controls_evaluate(uint32_t core, const mt_controller_config_t *config,
                          const mt_gamepad_state_t *raw, uint32_t previous,
                          mt_controls_output_t *out)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (raw == NULL || !raw->connected || !mt_controls_config_valid(core, config)) return;
    const uint32_t mask = mt_controls_action_mask(core);
    for (unsigned action = 0U; action < MT_CONTROL_COUNT; ++action)
        if ((mask & BIT(action)) != 0U && source_pressed(config->binding[action], config, raw, (previous & BIT(action)) != 0U))
            out->actions |= BIT(action);
    if ((out->actions & 3U) == 3U) out->actions &= ~UINT32_C(3);
    if ((out->actions & 12U) == 12U) out->actions &= ~UINT32_C(12);
    for (unsigned a = MT_CONTROL_AXIS_X; a <= MT_CONTROL_AXIS_Y; ++a) {
        const uint16_t source = config->binding[a];
        if (source >= MT_CONTROL_SOURCE_AXIS && source < MT_CONTROL_SOURCE_DPAD_STICK) {
            int16_t value = tuned_axis(config, raw, (unsigned)source - MT_CONTROL_SOURCE_AXIS);
            if (a == MT_CONTROL_AXIS_X) out->x = value;
            else out->y = value == INT16_MIN ? INT16_MAX : (int16_t)-value;
        }
    }
    if ((out->actions & 12U) != 0U) out->x = (out->actions & 4U) != 0U ? INT16_MIN : INT16_MAX;
    if ((out->actions & 3U) != 0U) out->y = (out->actions & 1U) != 0U ? INT16_MAX : INT16_MIN;
    /* A dedicated menu shortcut is consumed before gameplay routing. */
    const uint32_t system_actions = BIT(MT_CONTROL_MENU) | BIT(MT_CONTROL_SCREENSHOT) | BIT(MT_CONTROL_SCREENSHOT_OUTPUT);
    if ((out->actions & system_actions) != 0U) {
        out->actions &= system_actions; out->x = 0; out->y = 0;
    }
}

bool mt_controls_neutral(const mt_gamepad_state_t *raw)
{
    if (raw == NULL || !raw->connected) return true;
    if (raw->buttons != 0U || raw->extra_active != 0U || raw->hat < 8U) return false;
    for (unsigned i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        if ((raw->axis_valid_mask & (1U << i)) == 0U) continue;
        if ((raw->trigger_mask & (1U << i)) != 0U) { if (raw->axis[i] > -24576) return false; }
        else if (raw->axis[i] < -8192 || raw->axis[i] > 8192) return false;
    }
    return true;
}

uint16_t mt_controls_capture_source(const mt_gamepad_state_t *before,
                                    const mt_gamepad_state_t *now, bool analog)
{
    if (before == NULL || now == NULL || !now->connected) return 0U;
    if (!analog) {
        const uint32_t buttons = now->buttons & ~before->buttons;
        for (unsigned i = 0U; i < 32U; ++i) if ((buttons & (UINT32_C(1) << i)) != 0U) return (uint16_t)(i + 1U);
        const unsigned hat = (unsigned)mt_gamepad_hat_bits(now->hat) & ~(unsigned)mt_gamepad_hat_bits(before->hat);
        for (unsigned i = 0U; i < 4U; ++i) if ((hat & (1U << i)) != 0U) return (uint16_t)(MT_CONTROL_SOURCE_HAT + i);
    }
    unsigned best = 0U; int32_t strength = 0;
    for (unsigned i = 0U; i < MT_GAMEPAD_AXIS_COUNT; ++i) {
        if ((now->axis_valid_mask & (1U << i)) == 0U) continue;
        const bool trigger = (now->trigger_mask & (1U << i)) != 0U;
        const int32_t value = now->axis[i], old = before->axis[i];
        const int32_t magnitude = trigger ? value + 32768 : value < 0 ? -value : value;
        const int32_t old_magnitude = trigger ? old + 32768 : old < 0 ? -old : old;
        if (magnitude >= 18000 && old_magnitude < 18000 && magnitude > strength) { best = i; strength = magnitude; }
    }
    if (strength == 0) return 0U;
    if (analog) return (uint16_t)(MT_CONTROL_SOURCE_AXIS + best);
    return (uint16_t)(MT_CONTROL_SOURCE_AXIS_NEG + best * 2U +
        (((now->trigger_mask & (1U << best)) != 0U || now->axis[best] >= 0) ? 1U : 0U));
}
