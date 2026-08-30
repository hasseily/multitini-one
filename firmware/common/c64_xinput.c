/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_xinput.h"

#include <stdint.h>

#define MT_XINPUT_REPORT_ID UINT8_C(0x00)
#define MT_XINPUT_REPORT_SIZE UINT8_C(0x14)
#define MT_XINPUT_STICK_THRESHOLD INT32_C(10922)

#define MT_JOY_UP UINT8_C(0x01)
#define MT_JOY_DOWN UINT8_C(0x02)
#define MT_JOY_LEFT UINT8_C(0x04)
#define MT_JOY_RIGHT UINT8_C(0x08)
#define MT_JOY_FIRE UINT8_C(0x10)
#define MT_JOY_SPACE UINT8_C(0x20)
#define MT_JOY_CONTROL UINT8_C(0x40)

static int32_t signed_le16(const uint8_t *bytes)
{
    const uint16_t value = (uint16_t)((uint16_t)bytes[0] |
                           (uint16_t)((uint16_t)bytes[1] << 8U));
    if ((value & UINT16_C(0x8000)) != 0U) {
        return (int32_t)value - INT32_C(65536);
    }
    return (int32_t)value;
}

int mt_c64_xinput_decode(const uint8_t *report, size_t length,
                         uint8_t *joystick_state)
{
    uint8_t state = 0U;
    const uint8_t *buttons;
    int32_t x;
    int32_t y;

    if (report == NULL || joystick_state == NULL) return -1;
    if (length < MT_C64_XINPUT_REPORT_BYTES ||
        report[0] != MT_XINPUT_REPORT_ID ||
        report[1] != MT_XINPUT_REPORT_SIZE) {
        return -2;
    }

    buttons = &report[2];
    if ((buttons[0] & UINT8_C(0x01)) != 0U) state |= MT_JOY_UP;
    if ((buttons[0] & UINT8_C(0x02)) != 0U) state |= MT_JOY_DOWN;
    if ((buttons[0] & UINT8_C(0x04)) != 0U) state |= MT_JOY_LEFT;
    if ((buttons[0] & UINT8_C(0x08)) != 0U) state |= MT_JOY_RIGHT;

    x = signed_le16(&report[6]);
    if ((state & (MT_JOY_LEFT | MT_JOY_RIGHT)) == 0U) {
        if (x < -MT_XINPUT_STICK_THRESHOLD) state |= MT_JOY_LEFT;
        else if (x > MT_XINPUT_STICK_THRESHOLD) state |= MT_JOY_RIGHT;
    }
    y = signed_le16(&report[8]);
    if ((state & (MT_JOY_UP | MT_JOY_DOWN)) == 0U) {
        if (y > MT_XINPUT_STICK_THRESHOLD) state |= MT_JOY_UP;
        else if (y < -MT_XINPUT_STICK_THRESHOLD) state |= MT_JOY_DOWN;
    }

    if ((buttons[1] & UINT8_C(0x10)) != 0U) state |= MT_JOY_FIRE;
    if ((buttons[1] & UINT8_C(0x20)) != 0U) state |= MT_JOY_SPACE;
    if ((buttons[1] & UINT8_C(0x40)) != 0U) state |= MT_JOY_CONTROL;

    *joystick_state = state;
    return 0;
}

int mt_c64_xinput_player_led_command(uint8_t player, uint8_t *command,
                                     size_t capacity)
{
    if (command == NULL || capacity < MT_C64_XINPUT_LED_COMMAND_BYTES ||
        player < 1U || player > 4U) {
        return -1;
    }

    command[0] = UINT8_C(0x01);
    command[1] = UINT8_C(0x03);
    command[2] = (uint8_t)(player + 1U);
    return (int)MT_C64_XINPUT_LED_COMMAND_BYTES;
}
