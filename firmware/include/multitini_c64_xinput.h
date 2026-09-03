/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MULTITINI_C64_XINPUT_H
#define MULTITINI_C64_XINPUT_H

#include <stddef.h>
#include <stdint.h>

#define MT_C64_XINPUT_REPORT_BYTES 20U
#define MT_C64_XINPUT_LED_COMMAND_BYTES 3U

/* Decode one wired Xbox 360 report into the shared eight-button pad format.
 * C64 consumes bits 0..6; other cores may use bit 7 for Start. */
int mt_c64_xinput_decode(const uint8_t *report, size_t length,
                         uint8_t *joystick_state);

/* Build the wired Xbox 360 player-ring command for players 1 through 4. */
int mt_c64_xinput_player_led_command(uint8_t player, uint8_t *command,
                                     size_t capacity);

#endif
