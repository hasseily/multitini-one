/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared six-axis input, adapted from Appletini ONE be2ea4f. */
#ifndef MULTITINI_GAMEPAD_H
#define MULTITINI_GAMEPAD_H
#include <stdbool.h>
#include <stdint.h>

#define MT_GAMEPAD_SLOTS 9U
enum { MT_GAMEPAD_AXIS_X, MT_GAMEPAD_AXIS_Y, MT_GAMEPAD_AXIS_Z,
       MT_GAMEPAD_AXIS_RX, MT_GAMEPAD_AXIS_RY, MT_GAMEPAD_AXIS_RZ,
       MT_GAMEPAD_AXIS_COUNT };
#define MT_GAMEPAD_AXIS_OFF MT_GAMEPAD_AXIS_COUNT
typedef struct {
    uint8_t axis_valid_mask, buttons_valid, buttons;
    /* Zero means all buttons, for complete vendor reports. */
    uint8_t buttons_mask;
    int32_t axis[MT_GAMEPAD_AXIS_COUNT];
    int32_t logical_min[MT_GAMEPAD_AXIS_COUNT];
    int32_t logical_max[MT_GAMEPAD_AXIS_COUNT];
} mt_gamepad_report_t;
typedef struct {
    uint8_t source, invert, deadzone_percent;
    uint16_t sensitivity_percent;
} mt_gamepad_axis_config_t;
typedef struct {
    mt_gamepad_axis_config_t axis[MT_GAMEPAD_AXIS_COUNT];
    /* Logical A/B/X/Y/LB/RB/View/Menu -> physical button, 8 disables. */
    uint8_t button[8];
} mt_gamepad_config_t;
typedef struct {
    uint8_t connected, axis_valid_mask, buttons, hat, extra_active;
    int16_t axis[MT_GAMEPAD_AXIS_COUNT];
} mt_gamepad_state_t;
typedef struct {
    uint16_t allocated;
    mt_gamepad_state_t raw[MT_GAMEPAD_SLOTS];
    mt_gamepad_config_t config[MT_GAMEPAD_SLOTS];
} mt_gamepad_input_t;
void mt_gamepad_default_config(mt_gamepad_config_t *config);
bool mt_gamepad_config_valid(const mt_gamepad_config_t *config);
void mt_gamepad_init(mt_gamepad_input_t *input);
/* A keyboard may reserve a slot without marking it as a gamepad. */
int mt_gamepad_connect(mt_gamepad_input_t *input, uint8_t preferred);
void mt_gamepad_disconnect(mt_gamepad_input_t *input, uint8_t slot);
void mt_gamepad_report(mt_gamepad_input_t *input, uint8_t slot,
                       const mt_gamepad_report_t *report, uint8_t hat,
                       uint8_t extra_active);
void mt_gamepad_mapped(const mt_gamepad_input_t *input, uint8_t slot,
                       mt_gamepad_state_t *state);
uint8_t mt_gamepad_digital(const mt_gamepad_state_t *state);
uint8_t mt_gamepad_hat_bits(uint8_t hat);
#endif
