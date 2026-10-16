/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_CONTROLS_H
#define MULTITINI_CONTROLS_H
#include "multitini_gamepad.h"
#include <stddef.h>

enum { MT_CONTROLLER_TRANSPORT_HID, MT_CONTROLLER_TRANSPORT_XBOX360,
       MT_CONTROLLER_TRANSPORT_XBOXONE, MT_CONTROLLER_TRANSPORT_PS4 };
enum { MT_CONTROL_UP, MT_CONTROL_DOWN, MT_CONTROL_LEFT, MT_CONTROL_RIGHT,
       MT_CONTROL_A, MT_CONTROL_B, MT_CONTROL_C, MT_CONTROL_X, MT_CONTROL_Y,
       MT_CONTROL_Z, MT_CONTROL_START, MT_CONTROL_SELECT, MT_CONTROL_AXIS_X,
       MT_CONTROL_AXIS_Y, MT_CONTROL_MENU, MT_CONTROL_SCREENSHOT,
       MT_CONTROL_SCREENSHOT_OUTPUT, MT_CONTROL_COUNT };
enum { MT_CONTROLLER_AUTO, MT_CONTROLLER_PORT1, MT_CONTROLLER_PORT2,
       MT_CONTROLLER_OFF };
enum { MT_CONTROL_SOURCE_NONE = 0, MT_CONTROL_SOURCE_BUTTON = 1,
       MT_CONTROL_SOURCE_HAT = 33, MT_CONTROL_SOURCE_AXIS_NEG = 37,
       MT_CONTROL_SOURCE_AXIS = 49, MT_CONTROL_SOURCE_DPAD_STICK = 55,
       MT_CONTROL_SOURCE_EXTRA_AXIS_NEG = 59,
       MT_CONTROL_SOURCE_EXTRA_AXIS = 67, MT_CONTROL_SOURCE_COUNT = 71 };

typedef struct {
    uint16_t vendor_id, product_id;
    uint32_t descriptor_hash, serial_hash, port_path;
    uint8_t transport, interface_id;
} mt_controller_identity_t;
typedef struct {
    mt_controller_identity_t identity;
    uint32_t buttons_present;
    uint8_t connected, home_button, generic_axes;
    uint16_t axis_mask;
    char name[48];
} mt_controller_device_t;
typedef struct {
    uint16_t binding[MT_CONTROL_COUNT];
    /* Tuning belongs to each physical axis. Bindings select the source. */
    mt_gamepad_axis_config_t axis[MT_GAMEPAD_AXIS_COUNT];
    uint8_t assignment;
} mt_controller_config_t;
typedef struct {
    uint32_t actions;
    int16_t x, y;
} mt_controls_output_t;

uint32_t mt_controls_action_mask(uint32_t core_id);
const char *mt_controls_action_name(uint32_t core_id, unsigned action);
void mt_controls_source_name(const mt_controller_device_t *device,
                            uint16_t source, char *text, size_t capacity);
uint16_t mt_controls_axis_source(unsigned axis, bool analog, bool positive);
/* MT_INPUT_AXIS_COUNT means the source is not an individual axis. */
unsigned mt_controls_source_axis(uint16_t source);
bool mt_controls_source_is_analog(uint16_t source);
void mt_controls_default(uint32_t core_id, const mt_controller_device_t *device,
                         mt_controller_config_t *config);
bool mt_controls_config_valid(uint32_t core_id, const mt_controller_config_t *config);
bool mt_controls_identity_equal(const mt_controller_identity_t *a,
                                const mt_controller_identity_t *b);
bool mt_controls_identity_model_equal(const mt_controller_identity_t *a,
                                      const mt_controller_identity_t *b);
void mt_controls_migrate_legacy(uint32_t core_id, const mt_controller_device_t *device,
                               const mt_gamepad_config_t *legacy,
                               mt_controller_config_t *config);
void mt_controls_evaluate(uint32_t core_id, const mt_controller_config_t *config,
                          const mt_gamepad_state_t *raw, uint32_t previous_actions,
                          mt_controls_output_t *output);
bool mt_controls_neutral(const mt_gamepad_state_t *raw);
uint16_t mt_controls_capture_source(const mt_gamepad_state_t *before,
                                    const mt_gamepad_state_t *now, bool analog);
#endif
