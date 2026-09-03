/* SPDX-License-Identifier: GPL-3.0-only */
#include "multitini_c64_xinput.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void set_axis(uint8_t *report, size_t offset, int32_t value)
{
    const uint16_t encoded = (uint16_t)(value & INT32_C(0xFFFF));
    report[offset] = (uint8_t)(encoded & UINT16_C(0x00FF));
    report[offset + 1U] = (uint8_t)(encoded >> 8U);
}

static void expect_state(uint8_t *report, uint8_t expected)
{
    uint8_t state = UINT8_C(0xFF);
    assert(mt_c64_xinput_decode(report, MT_C64_XINPUT_REPORT_BYTES,
                                &state) == 0);
    assert(state == expected);
}

int main(void)
{
    uint8_t report[MT_C64_XINPUT_REPORT_BYTES] = {0};
    uint8_t command[MT_C64_XINPUT_LED_COMMAND_BYTES] = {0};
    uint8_t state = 0U;

    report[1] = MT_C64_XINPUT_REPORT_BYTES;
    expect_state(report, UINT8_C(0x00));

    report[2] = UINT8_C(0x0F);
    report[3] = UINT8_C(0x70);
    expect_state(report, UINT8_C(0x7F));

    report[2] = UINT8_C(0x30);
    report[3] = UINT8_C(0x00);
    expect_state(report, UINT8_C(0xC0));

    memset(report, 0, sizeof(report));
    report[1] = MT_C64_XINPUT_REPORT_BYTES;
    set_axis(report, 6U, -10923);
    set_axis(report, 8U, 10923);
    expect_state(report, UINT8_C(0x05));

    set_axis(report, 6U, 10923);
    set_axis(report, 8U, -10923);
    expect_state(report, UINT8_C(0x0A));

    set_axis(report, 6U, -10922);
    set_axis(report, 8U, 10922);
    expect_state(report, UINT8_C(0x00));

    report[2] = UINT8_C(0x08);
    set_axis(report, 6U, -32768);
    expect_state(report, UINT8_C(0x08));

    assert(mt_c64_xinput_decode(NULL, sizeof(report), &state) == -1);
    assert(mt_c64_xinput_decode(report, sizeof(report), NULL) == -1);
    assert(mt_c64_xinput_decode(report, sizeof(report) - 1U, &state) == -2);
    report[0] = UINT8_C(0x01);
    assert(mt_c64_xinput_decode(report, sizeof(report), &state) == -2);
    report[0] = UINT8_C(0x00);
    report[1] = UINT8_C(0x13);
    assert(mt_c64_xinput_decode(report, sizeof(report), &state) == -2);

    assert(mt_c64_xinput_player_led_command(1U, command,
                                             sizeof(command)) == 3);
    assert(command[0] == UINT8_C(0x01));
    assert(command[1] == UINT8_C(0x03));
    assert(command[2] == UINT8_C(0x02));
    assert(mt_c64_xinput_player_led_command(4U, command,
                                             sizeof(command)) == 3);
    assert(command[2] == UINT8_C(0x05));
    assert(mt_c64_xinput_player_led_command(0U, command,
                                             sizeof(command)) == -1);
    assert(mt_c64_xinput_player_led_command(5U, command,
                                             sizeof(command)) == -1);
    assert(mt_c64_xinput_player_led_command(1U, NULL,
                                             sizeof(command)) == -1);
    assert(mt_c64_xinput_player_led_command(1U, command,
                                             sizeof(command) - 1U) == -1);

    return 0;
}
