/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_controls.h"
#include "multitini_c64_interface.h"
#include "multitini_gameboy_interface.h"
#include "multitini_genesis_interface.h"
#include "multitini_vectrex_interface.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIT(value) (UINT32_C(1) << (value))

static void check(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static mt_controller_device_t device(unsigned transport)
{
    mt_controller_device_t result = {0};
    result.connected = 1U;
    result.identity.transport = (uint8_t)transport;
    result.buttons_present = UINT32_MAX;
    result.home_button = transport == MT_CONTROLLER_TRANSPORT_HID ? UINT8_MAX : 10U;
    result.axis_mask = 0x3fU;
    return result;
}

static mt_gamepad_state_t neutral(void)
{
    mt_gamepad_state_t state = {0};
    state.connected = 1U;
    state.hat = 8U;
    return state;
}

static mt_controller_config_t empty_config(uint32_t core)
{
    const mt_controller_device_t pad = device(MT_CONTROLLER_TRANSPORT_HID);
    mt_controller_config_t config;
    mt_controls_default(core, &pad, &config);
    memset(config.binding, 0, sizeof(config.binding));
    return config;
}

static void test_defaults_and_validation(void)
{
    const uint32_t cores[] = { MT_C64_CORE_ID, MT_GBC_CORE_ID, MT_GENESIS_CORE_ID, MT_VECTREX_CORE_ID };
    for (unsigned model = MT_CONTROLLER_TRANSPORT_HID; model <= MT_CONTROLLER_TRANSPORT_PS4; ++model) {
        mt_controller_device_t pad = device(model);
        for (unsigned index = 0U; index < sizeof(cores) / sizeof(cores[0]); ++index) {
            mt_controller_config_t config;
            mt_controls_default(cores[index], &pad, &config);
            check(mt_controls_config_valid(cores[index], &config), "all supported core/controller defaults validate");
            const uint32_t mask = mt_controls_action_mask(cores[index]);
            check((mask & BIT(MT_CONTROL_SCREENSHOT_OUTPUT)) != 0U && MT_CONTROL_SCREENSHOT_OUTPUT == 16,
                  "per-core action masks preserve the full-screen screenshot bit above sixteen bits");
            check(config.binding[MT_CONTROL_SCREENSHOT] == 0U && config.binding[MT_CONTROL_SCREENSHOT_OUTPUT] == 0U,
                  "screenshot shortcuts are optional and start unassigned");
            check(config.binding[MT_CONTROL_MENU] == (model == MT_CONTROLLER_TRANSPORT_HID ? 0U : 11U),
                  "only an identified Home/Guide button opens the menu by default");
            for (unsigned action = 0U; action < MT_CONTROL_COUNT; ++action) {
                check(mt_controls_action_name(cores[index], action)[0] != '\0', "every action has a readable label");
                if ((mask & BIT(action)) == 0U) check(config.binding[action] == 0U, "unsupported actions remain unassigned");
            }
            config.binding[MT_CONTROL_A] = MT_CONTROL_SOURCE_COUNT;
            check(!mt_controls_config_valid(cores[index], &config), "out-of-range input source is rejected");
            mt_controls_default(cores[index], &pad, &config);
            config.axis[0].deadzone_percent = 51U;
            check(!mt_controls_config_valid(cores[index], &config), "invalid axis tuning is rejected");
        }
    }
    mt_controller_device_t generic = device(MT_CONTROLLER_TRANSPORT_HID);
    mt_controller_config_t config;
    mt_controls_default(MT_GBC_CORE_ID, &generic, &config);
    check(config.binding[MT_CONTROL_SELECT] == 9U && config.binding[MT_CONTROL_START] == 10U && config.binding[MT_CONTROL_MENU] == 0U,
          "generic Select and Start are available to the game and never guessed as Menu");
    generic.home_button = 24U;
    mt_controls_default(MT_GBC_CORE_ID, &generic, &config);
    check(config.binding[MT_CONTROL_MENU] == 25U, "a generic descriptor's identified Home remains distinct from Select and Start");
    const uint32_t gbc = mt_controls_action_mask(MT_GBC_CORE_ID);
    const uint32_t vec = mt_controls_action_mask(MT_VECTREX_CORE_ID);
    const uint32_t md = mt_controls_action_mask(MT_GENESIS_CORE_ID);
    check((gbc & BIT(MT_CONTROL_C)) == 0U && (gbc & BIT(MT_CONTROL_AXIS_X)) == 0U &&
          (vec & BIT(MT_CONTROL_AXIS_X)) != 0U && (md & BIT(MT_CONTROL_Z)) != 0U,
          "each core exposes only its own action set");
    check(mt_controls_action_mask(0U) == (BIT(MT_CONTROL_MENU) | BIT(MT_CONTROL_SCREENSHOT) | BIT(MT_CONTROL_SCREENSHOT_OUTPUT)) &&
          mt_controls_action_mask(UINT32_C(0x12345678)) == 0U,
          "standby exposes only system shortcuts and unknown cores expose no actions");
    config.assignment = MT_CONTROLLER_PORT2;
    check(!mt_controls_config_valid(MT_GBC_CORE_ID, &config), "Game Boy profiles reject a second player assignment");
    config = empty_config(MT_GBC_CORE_ID);
    for (unsigned source = 0U; source < MT_CONTROL_SOURCE_COUNT; ++source) {
        config.binding[MT_CONTROL_A] = (uint16_t)source;
        const bool whole_axis = (source >= 49U && source < 55U) || (source >= 67U && source < 71U);
        check(mt_controls_config_valid(MT_GBC_CORE_ID, &config) == !whole_axis,
              "digital actions accept buttons, directions and composite defaults but not whole axes");
        char text[48];
        mt_controls_source_name(&generic, (uint16_t)source, text, sizeof(text));
        check(text[0] != '\0', "all valid controller inputs have readable names");
    }
    config = empty_config(MT_VECTREX_CORE_ID);
    config.binding[MT_CONTROL_AXIS_X] = MT_CONTROL_SOURCE_BUTTON;
    check(!mt_controls_config_valid(MT_VECTREX_CORE_ID, &config), "analog actions reject a button source");
}

static void test_independent_buttons_and_shortcuts(void)
{
    mt_controller_config_t config = empty_config(MT_GBC_CORE_ID);
    mt_gamepad_state_t raw = neutral(), before = neutral();
    mt_controls_output_t output;
    for (unsigned button = 0U; button < 32U; ++button) {
        config.binding[MT_CONTROL_A] = (uint16_t)(MT_CONTROL_SOURCE_BUTTON + button);
        raw.buttons = BIT(button);
        mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
        check(output.actions == BIT(MT_CONTROL_A), "each of all 32 button bits reaches its assigned action");
        check(mt_controls_capture_source(&before, &raw, false) == button + 1U,
              "capture retains every button identity including high bits");
        check(mt_controls_capture_source(&raw, &raw, false) == 0U, "a held button does not create another capture edge");
        raw.buttons = BIT((button + 1U) % 32U);
        mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
        check(output.actions == 0U, "neighboring controller buttons cannot alias to the assigned button");
    }
    config.binding[MT_CONTROL_A] = 1U;
    config.binding[MT_CONTROL_MENU] = 11U;
    config.binding[MT_CONTROL_SCREENSHOT] = 31U;
    config.binding[MT_CONTROL_SCREENSHOT_OUTPUT] = 32U;
    raw.buttons = BIT(0U) | BIT(31U);
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_SCREENSHOT_OUTPUT) && output.x == 0 && output.y == 0,
          "full-screen screenshot keeps its distinct action bit and consumes gameplay input");
    raw.buttons = BIT(0U) | BIT(30U);
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_SCREENSHOT), "filtered video screenshot is a separate shortcut");
    raw.buttons = BIT(0U) | BIT(10U);
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_MENU), "Home shortcut is consumed before gameplay routing");
    raw.connected = 0U;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == 0U, "disconnected controller cannot retain a held shortcut");
}

static void test_direction_hysteresis_and_analog(void)
{
    mt_controller_config_t config = empty_config(MT_GBC_CORE_ID);
    mt_gamepad_state_t raw = neutral();
    mt_controls_output_t output;
    config.binding[MT_CONTROL_A] = MT_CONTROL_SOURCE_AXIS_NEG + 1U;
    config.axis[0].deadzone_percent = 0U;
    raw.axis_valid_mask = 1U;
    raw.axis[0] = 11001;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_A), "axis direction presses above its entry threshold");
    raw.axis[0] = 9000;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, BIT(MT_CONTROL_A), &output);
    check(output.actions == BIT(MT_CONTROL_A), "hysteresis retains a held direction inside its release band");
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == 0U, "the same axis position cannot initiate a new press inside the hysteresis band");
    raw.axis[0] = 7999;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, BIT(MT_CONTROL_A), &output);
    check(output.actions == 0U, "axis direction releases below the lower threshold");
    config = empty_config(MT_GBC_CORE_ID);
    for (unsigned action = 0U; action < 4U; ++action) config.binding[action] = (uint16_t)(action + 1U);
    raw.buttons = 15U;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == 0U && output.x == 0 && output.y == 0, "opposing direction pairs cancel instead of reaching a machine");

    const mt_controller_device_t pad = device(MT_CONTROLLER_TRANSPORT_XBOXONE);
    mt_controls_default(MT_GBC_CORE_ID, &pad, &config);
    raw = neutral(); raw.axis_valid_mask = 1U; raw.axis[0] = -22000;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_LEFT), "default digital controls work from the left stick as well as the D-pad");
    raw.hat = 2U;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == 0U, "opposite D-pad and stick directions cancel within a composite default");

    mt_controls_default(MT_VECTREX_CORE_ID, &pad, &config);
    config.axis[0].deadzone_percent = config.axis[1].deadzone_percent = 0U;
    raw = neutral(); raw.axis_valid_mask = 3U; raw.axis[0] = 12000; raw.axis[1] = -15000;
    mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
    check(output.x == 12000 && output.y == 15000 && output.actions == 0U, "Vectrex receives independent signed analog axes with the correct vertical convention");
    raw.hat = 1U;
    mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
    check(output.x == INT16_MAX && output.y == INT16_MAX && output.actions == (BIT(MT_CONTROL_UP) | BIT(MT_CONTROL_RIGHT)),
          "Vectrex D-pad diagonals override both analog axes in the expected direction");
    raw.hat = 8U; raw.axis[0] = 8000;
    config.axis[0].deadzone_percent = 50U;
    mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
    check(output.x == 0 && output.y == 15000, "dead zone suppresses drift without changing the other axis");
    config.axis[0].deadzone_percent = 0U; config.axis[0].sensitivity_percent = 200U; raw.axis[0] = 20000;
    mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
    check(output.x == INT16_MAX, "gain saturates instead of wrapping an analog output");
    config.axis[0].sensitivity_percent = 100U; config.axis[0].invert = 1U; raw.axis[0] = 12000;
    mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
    check(output.x == -12000, "axis inversion changes only the configured source");
}

static void test_capture_and_identity(void)
{
    mt_gamepad_state_t before = neutral(), raw = neutral();
    before.axis_valid_mask = raw.axis_valid_mask = (uint8_t)(1U << MT_GAMEPAD_AXIS_Z);
    before.trigger_mask = raw.trigger_mask = before.axis_valid_mask;
    before.axis[MT_GAMEPAD_AXIS_Z] = raw.axis[MT_GAMEPAD_AXIS_Z] = INT16_MIN;
    check(mt_controls_neutral(&raw), "released triggers use their negative endpoint as neutral");
    check(mt_controls_capture_source(&before, &raw, false) == 0U, "a released trigger never captures itself");
    raw.axis[MT_GAMEPAD_AXIS_Z] = -10000;
    check(!mt_controls_neutral(&raw), "a partially pulled trigger is not neutral");
    check(mt_controls_capture_source(&before, &raw, false) == MT_CONTROL_SOURCE_AXIS_NEG + MT_GAMEPAD_AXIS_Z * 2U + 1U,
          "trigger pull captures its positive direction even before its signed value crosses zero");
    check(mt_controls_capture_source(&before, &raw, true) == MT_CONTROL_SOURCE_AXIS + MT_GAMEPAD_AXIS_Z,
          "analog capture records the entire moved axis");
    raw.buttons = BIT(25U); raw.hat = 0U;
    check(mt_controls_capture_source(&before, &raw, false) == 26U, "button edges take priority over simultaneous hat and axis movement");
    raw.buttons = 0U;
    check(mt_controls_capture_source(&before, &raw, false) == MT_CONTROL_SOURCE_HAT,
          "D-pad edges remain separately capturable");
    raw = neutral(); before = neutral(); raw.axis_valid_mask = 1U << MT_GAMEPAD_AXIS_RX; raw.axis[MT_GAMEPAD_AXIS_RX] = -25000;
    check(mt_controls_capture_source(&before, &raw, false) == MT_CONTROL_SOURCE_AXIS_NEG + MT_GAMEPAD_AXIS_RX * 2U,
          "right-stick direction captures independently from the left stick");
    check(mt_controls_capture_source(&raw, &raw, false) == 0U, "held stick motion cannot capture a second wizard step");
    raw.connected = 0U;
    check(mt_controls_capture_source(&before, &raw, false) == 0U, "disconnection cannot produce a capture");

    mt_controller_identity_t first = {0}, second = {0};
    first.vendor_id = second.vendor_id = 0x1234U; first.product_id = second.product_id = 0x5678U;
    first.port_path = 1U; second.port_path = 2U;
    check(mt_controls_identity_model_equal(&first, &second) && !mt_controls_identity_equal(&first, &second),
          "identical controllers without serial numbers remain distinct by stable USB path");
    first.serial_hash = second.serial_hash = UINT32_C(0x12345678);
    check(mt_controls_identity_equal(&first, &second), "a serial identity survives moving to a different USB path");
    second.serial_hash ^= 1U;
    check(!mt_controls_identity_equal(&first, &second), "different serial identities cannot share one controller profile");
}

static void test_legacy_axis_migration(void)
{
    const mt_controller_device_t pad = device(MT_CONTROLLER_TRANSPORT_XBOXONE);
    mt_gamepad_config_t legacy;
    mt_controller_config_t config;
    mt_gamepad_state_t raw = neutral();
    mt_controls_output_t output;
    mt_gamepad_default_config(&legacy);
    legacy.axis[MT_GAMEPAD_AXIS_X].source = MT_GAMEPAD_AXIS_RX;
    legacy.axis[MT_GAMEPAD_AXIS_X].invert = 1U;
    legacy.axis[MT_GAMEPAD_AXIS_X].deadzone_percent = 25U;
    legacy.axis[MT_GAMEPAD_AXIS_X].sensitivity_percent = 150U;
    legacy.axis[MT_GAMEPAD_AXIS_Y].source = MT_GAMEPAD_AXIS_OFF;
    legacy.button[0] = 7U;
    legacy.button[1] = 8U;
    mt_controls_migrate_legacy(MT_VECTREX_CORE_ID, &pad, &legacy, &config);
    check(mt_controls_config_valid(MT_VECTREX_CORE_ID, &config), "custom legacy axis mappings produce a valid profile");
    check(config.binding[MT_CONTROL_AXIS_X] == MT_CONTROL_SOURCE_AXIS + MT_GAMEPAD_AXIS_RX &&
          config.binding[MT_CONTROL_AXIS_Y] == 0U,
          "legacy Vectrex source remapping and disabled vertical axis survive migration");
    check(config.axis[MT_GAMEPAD_AXIS_RX].invert == 1U &&
          config.axis[MT_GAMEPAD_AXIS_RX].deadzone_percent == 25U &&
          config.axis[MT_GAMEPAD_AXIS_RX].sensitivity_percent == 150U,
          "legacy horizontal tuning follows its physical source and takes priority over a secondary alias");
    check(config.binding[MT_CONTROL_A] == 8U && config.binding[MT_CONTROL_B] == 0U && config.binding[MT_CONTROL_MENU] == 11U,
          "legacy button remaps and disabled buttons survive while Menu migrates to identified Home");
    raw.axis_valid_mask = (1U << MT_GAMEPAD_AXIS_X) | (1U << MT_GAMEPAD_AXIS_Y) | (1U << MT_GAMEPAD_AXIS_RX);
    raw.axis[MT_GAMEPAD_AXIS_RX] = 25000; raw.axis[MT_GAMEPAD_AXIS_Y] = -25000;
    mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
    check(output.x == INT16_MIN && output.y == 0, "migrated axis source applies its preserved gain, dead zone and inversion");
    mt_controls_migrate_legacy(MT_GBC_CORE_ID, &pad, &legacy, &config);
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_LEFT), "digital composite direction defaults follow a migrated right-stick source and ignore the disabled vertical source");
    raw.axis[MT_GAMEPAD_AXIS_RX] = 0; raw.axis[MT_GAMEPAD_AXIS_X] = -25000;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == 0U, "old unselected physical axes do not leak into migrated digital directions");
    raw.hat = 0U;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_UP), "D-pad remains usable when the legacy analog vertical source was disabled");
}

static void test_extended_axis_controls(void)
{
    _Static_assert(MT_GAMEPAD_AXIS_COUNT == 6 && MT_GAMEPAD_AXIS_OFF == 6,
                   "Persisted gamepad tuning and disabled source must stay unchanged");
    _Static_assert(MT_CONTROL_SOURCE_BUTTON == 1 && MT_CONTROL_SOURCE_HAT == 33 &&
        MT_CONTROL_SOURCE_AXIS_NEG == 37 && MT_CONTROL_SOURCE_AXIS == 49 &&
        MT_CONTROL_SOURCE_DPAD_STICK == 55 && MT_CONTROL_SOURCE_EXTRA_AXIS_NEG == 59 &&
        MT_CONTROL_SOURCE_EXTRA_AXIS == 67 && MT_CONTROL_SOURCE_COUNT == 71,
        "Saved binding identifiers must stay stable");
    const char *const names[] = {"Slider", "Dial", "Wheel", "Pan"};
    mt_controller_device_t generic = device(MT_CONTROLLER_TRANSPORT_HID);
    generic.generic_axes = 1U;
    mt_controller_config_t config;
    mt_controls_default(MT_VECTREX_CORE_ID, &generic, &config);
    check(config.assignment == MT_CONTROLLER_OFF, "axis devices do not automatically claim a player");
    for (unsigned action = 0; action < MT_CONTROL_COUNT; ++action)
        check(config.binding[action] == 0U, "axis devices start without gameplay or menu shortcuts");
    mt_gamepad_config_t legacy;
    mt_gamepad_default_config(&legacy);
    mt_controls_migrate_legacy(MT_VECTREX_CORE_ID, &generic, &legacy, &config);
    check(config.assignment == MT_CONTROLLER_OFF && config.binding[MT_CONTROL_A] == 0U,
          "a legacy gamepad profile does not create mouse or rotary defaults");
    for (unsigned axis = 0; axis < MT_INPUT_AXIS_COUNT; ++axis) {
        for (unsigned positive = 0; positive < 2U; ++positive) {
            const uint16_t source = mt_controls_axis_source(axis, false, positive != 0U);
            check(source == (axis < 6U ? 37U + axis * 2U : 59U + (axis - 6U) * 2U) + positive,
                  "old and extended axis directions have distinct stable numbers");
            check(mt_controls_source_axis(source) == axis && !mt_controls_source_is_analog(source),
                  "direction sources identify the exact physical axis");
        }
        const uint16_t analog = mt_controls_axis_source(axis, true, false);
        check(analog == (axis < 6U ? 49U + axis : 67U + axis - 6U) &&
              mt_controls_source_axis(analog) == axis && mt_controls_source_is_analog(analog),
              "full-axis bindings preserve six old IDs and extend without collisions");
    }
    check(mt_controls_axis_source(MT_INPUT_AXIS_COUNT, true, true) == 0U &&
          mt_controls_source_axis(MT_CONTROL_SOURCE_DPAD_STICK) == MT_INPUT_AXIS_COUNT,
          "invalid and composite axes cannot index tuning arrays");
    mt_gamepad_state_t raw = neutral(), before = neutral();
    mt_controls_output_t output;
    raw.generic_axes = before.generic_axes = 1U;
    config.assignment = MT_CONTROLLER_PORT1;
    for (unsigned axis = MT_INPUT_AXIS_SLIDER; axis < MT_INPUT_AXIS_COUNT; ++axis) {
        const uint16_t bit = (uint16_t)(1U << axis);
        raw.axis_valid_mask = before.axis_valid_mask = bit;
        raw.relative_mask = before.relative_mask = bit;
        before.axis[axis] = 0;
        char text[48];
        mt_controls_source_name(&generic, mt_controls_axis_source(axis, true, false), text, sizeof(text));
        check(strcmp(text, names[axis - MT_INPUT_AXIS_SLIDER]) == 0, "extra axis names are distinct");
        for (int sign = -1; sign <= 1; sign += 2) {
            const uint16_t source = mt_controls_axis_source(axis, false, sign > 0);
            memset(config.binding, 0, sizeof(config.binding));
            config.binding[MT_CONTROL_A] = source;
            raw.axis[axis] = (int16_t)sign;
            check(!mt_controls_neutral(&raw), "one wheel detent counts as active motion");
            check(mt_controls_capture_source(&before, &raw, false) == source,
                  "a single relative step captures its exact axis and direction");
            check(mt_controls_capture_source(&raw, &raw, false) == 0U,
                  "an unreleased relative pulse cannot bind a second wizard step");
            mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
            check(output.actions == BIT(MT_CONTROL_A), "a one-unit relative pulse activates its binding");
            raw.axis[axis] = 0;
            mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, output.actions, &output);
            check(output.actions == 0U && mt_controls_neutral(&raw), "expired relative motion releases immediately");
        }
        memset(config.binding, 0, sizeof(config.binding));
        config.binding[MT_CONTROL_AXIS_X] = mt_controls_axis_source(axis, true, false);
        raw.axis[axis] = 1234;
        mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
        check(output.x == 1234 && output.actions == 0U, "relative analog binding preserves transient velocity");
        raw.relative_mask = before.relative_mask = 0U;
        raw.axis[axis] = before.axis[axis] = -24000;
        check(mt_controls_neutral(&raw) && mt_controls_capture_source(&before, &raw, true) == 0U,
              "an off-center slider arms without capturing its resting position");
        raw.axis[axis] = -16000;
        check(mt_controls_capture_source(&before, &raw, true) == 0U, "small absolute changes do not capture");
        raw.axis[axis] = -15000;
        check(mt_controls_capture_source(&before, &raw, true) == config.binding[MT_CONTROL_AXIS_X],
              "slow slider motion captures against a fixed resting baseline");
        raw.axis[axis] = -30000;
        mt_controls_evaluate(MT_VECTREX_CORE_ID, &config, &raw, 0U, &output);
        check(output.x == -30000, "extra absolute axes have unity gain without a dead zone");
    }
    config = empty_config(MT_GBC_CORE_ID);
    config.binding[MT_CONTROL_A] = MT_CONTROL_SOURCE_AXIS_NEG + 1U;
    config.axis[0].deadzone_percent = 50U;
    config.axis[0].sensitivity_percent = 25U;
    raw = neutral(); raw.generic_axes = 1U;
    raw.axis_valid_mask = raw.relative_mask = 1U; raw.axis[0] = 1;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_A), "relative XY bypasses stick deadzone and retains a small scaled pulse");
    config.axis[0].invert = 1U;
    config.binding[MT_CONTROL_A] = MT_CONTROL_SOURCE_AXIS_NEG;
    mt_controls_evaluate(MT_GBC_CORE_ID, &config, &raw, 0U, &output);
    check(output.actions == BIT(MT_CONTROL_A), "relative XY respects inversion");
}

int main(void)
{
    mt_controller_config_t turbo = empty_config(MT_C64_CORE_ID);
    mt_gamepad_state_t raw = neutral();
    mt_controls_output_t output;
    for (unsigned action = MT_CONTROL_TURBO_TOGGLE; action < MT_CONTROL_ACTION_COUNT; ++action) {
        check(mt_controls_binding(&turbo, action) == 0, "turbo actions start unbound");
        mt_controls_bind(&turbo, action, 1);
        turbo.binding[MT_CONTROL_A] = 1;
        raw.buttons = 1;
        mt_controls_evaluate(MT_C64_CORE_ID, &turbo, &raw, 0, &output);
        check(output.actions == BIT(action), "turbo shortcuts consume the gameplay button");
        check(!mt_controls_config_valid(MT_GBC_CORE_ID, &turbo), "turbo actions are C64-only");
        mt_controls_bind(&turbo, action, 0);
    }
    test_defaults_and_validation();
    test_independent_buttons_and_shortcuts();
    test_direction_hysteresis_and_analog();
    test_capture_and_identity();
    test_legacy_axis_migration();
    test_extended_axis_controls();
    puts("PASS controls: all button sources, per-core defaults, capture, analog tuning and stable identities");
    return 0;
}
