# PAL C64 USB Input Hardware Test

This procedure covers the US keyboard map and wired Xbox 360 path in firmware
build `2026082D`. It also regresses the already-proven USB1 power, keyboard,
generic HID gamepad, live PAL C64 video, and golden update paths. The recorded
test qualifies the keyboard maps, Xbox functional input mapping, and player
LED command. The recorded follow-up also passes hotplug, disconnect while held,
reconnect, and joystick-port swap on the tested Xbox controller and USB
topology.

Build `2026082A` is a failed-power baseline only. Build `2026082B` proved
connector power, keyboard input, and generic HID gamepad input, but exposed the
symbolic-keyboard and Xbox enumeration failures. Build `2026082C` fixed those
failures and qualified the keyboard maps and Xbox directions, but A/Fire and
the player ring remained open. Use only the root `FIRMWARE.BIN` whose exact
size, SHA-256, CRC32, and Bootgen readback are recorded for build `2026082D` in
[`HARDWARE_RESULTS.md`](HARDWARE_RESULTS.md). Confirm both the UART firmware
banner and shell build ID say `2026082D`.

Copy repository-root `FIRMWARE.BIN` to the root of the SD card under that exact
name, insert the card, and power-cycle. Golden programs and verifies the
secondary firmware slot, renames the file to `FIRMWARE.OK`, and boots it
automatically. Upload the 20 KiB ROM set with
`python .\scripts\serial_c64_rom_upload.py --port COM10`, substituting the
actual COM port. After `[host] target reported C64 boot ok`, press `v` once at
the UART console to switch from color bars to the C64 screen. Do not overwrite
the golden slot and do not use `:reboot` merely to start the loaded C64.

## USB1 PHY and VBUS

1. Leave USB1 empty, boot the test image, and press `u` at UART.
2. On the controller line confirm `started=1`, `keyboards=0`, `gamepads=0`, and
   zero submit, transfer, and input error counters.
3. On the PHY line confirm `setup_rc=0`, `disable_rc=0`, `drive_known=1`,
   `drive=1`, `vbus_known=1`, `vbus_valid=1`, `ulpi_err=0`, `timeouts=0`, and
   `id=0424:0004`. With the port idle, confirm `func=41`, `iface=10`, and
   `otg=66`. Function Control changes with device speed; do not require
   `func=41` after attaching a device. These values use the internal VBUS
   comparator; `UseExternalVbusIndicator` must remain clear.
4. Confirm physical USB1 power with a USB power meter or by connecting a known
   keyboard and observing its power indicator. Record the method and result.
   Register readback alone does not qualify connector power.
5. With the keyboard connected, press `u` again and confirm `keyboards=1`,
   `keymap=us`, and an increasing report count after input.
6. Repeat the empty-port PHY/VBUS check after a cold power-up and after returning
   to golden with `:reboot` and reloading the image. Confirm USB1 power drops
   during the return to golden; any `USB1 VBUS shutdown failed` message is a
   test failure and must cancel the reboot.

## Keyboard

1. After the PHY/VBUS stage passes, display the live C64 BASIC screen with `v`
   if necessary.
2. Use the connected known USB keyboard. At UART, press `u`. Confirm
   `started=1`, `keyboards=1`, `keymap=us`, and an increasing report count.
3. Type `PRINT 2+2` and press Enter. Confirm the C64 prints `4`.
4. At the READY prompt, verify `Shift+2`, `Shift+6`, `Shift+8`, `Shift+-`, `=`,
   `Shift+=`, brackets, semicolon/colon, apostrophe/quote, `Shift+7`,
   `Shift+9`, `Shift+0`, and shifted comma, period, and slash.
5. Check both shifts with `Shift+=`, then check ordinary `Shift+A`, Control,
   Commodore, Run/Stop, cursor/edit keys, and a held repeating key.
6. Verify the documented brace, pipe, grave, and tilde fallbacks. Verify F9,
   F10, F11, F12, keypad operators, and all available keypad digits.
7. Press `k`; confirm `MiSTer positional` and `keymap=positional`. Press `k`
   again; confirm `US symbolic` and `keymap=us`.
8. Hold a harmless key, disconnect the keyboard, reconnect it, and confirm no
   key remains stuck.
9. Hold a key, toggle core reset with `r`, release reset with `r`, and confirm
   the held state is reasserted and the later release is accepted.

## Gamepad

1. Recheck the known generic HID gamepad `06a3:0107`. Press `u` and confirm
   `gamepads=1` and increasing reports.
2. At BASIC, use `PRINT PEEK(56320)` for the default joystick port 2. Neutral
   is `127`; directions and button 1 must pull the corresponding bits low.
3. Press `j`, confirm the UART says port 1, and repeat with
   `PRINT PEEK(56321)`. Port 1 shares CIA keyboard-matrix lines, so direction
   and Fire closures may also appear as BASIC characters; the PEEK value is the
   unambiguous joystick check.
4. Check diagonals, hold/release, button 1, unplug while held, reconnect, and
   hotplug after boot. Buttons 2 and 3 are mapped to Space and Control.
5. Connect the Xbox 360 controller `045e:028e`. Enumeration must complete
   without `Endpoint num 4 overflow`, `Parse config descriptor fail`, or a
   reconnect loop. Confirm `[usb1] XInput connected`.
6. Press `u`; confirm `gamepads=1` and increasing reports. Verify D-pad and
   left-stick directions, neutral and release, A as Fire, B as Space, and X as
   Control. On the per-XInput line, confirm `led=1`, `out_try=1`,
   `out_pending=0`, and zero submit, transfer, and output errors. After all
   controls have been exercised, `seen=7F` confirms all seven intended state
   bits were decoded.
7. Repeat joystick-port swap, unplug while held, reconnect, and hotplug for the
   Xbox controller.

## Regression

During all input checks, confirm the BASIC cursor remains live, DVI remains
locked and tear-free, `v` still switches whole frames, and `:reboot` still
returns to golden. Record counters before and after testing. Reports must keep
increasing, `input_err` must remain zero, and submit/transfer errors must not
grow continuously or stall input; an isolated retry is not by itself a
failure. Record the keyboard and gamepad models and both final `u` status
lines.

Recorded `2026082D` result: the keyboard maps were already qualified. The Xbox
controller reported `reports=839`, `state=10`, `seen=7F`, `changes=86`,
`led=1`, one output attempt, and zero per-XInput submit, transfer, or output
errors; its raw prefix was `0014001000002EFB6B00`. D-pad and left-stick
directions worked, A/Fire produced masked joystick value `15`, B produced
Space, and holding X while pressing a number changed the C64 cursor color,
proving Control end to end. X alone was nonprinting as expected for a modifier.
This qualifies the functional Xbox input map and player LED command on the
tested controller. The follow-up test passed hotplug after boot, disconnect
while held with state release, reconnect, and runtime joystick-port swap. With
port 1 selected, A/Fire and B/Space both appeared as Space, X remained Control,
and D-pad motion produced the left-arrow glyph, `2`, and other
matrix-dependent characters. That is expected because C64 joystick port 1
shares CIA keyboard-matrix lines; it confirms the swap rather than indicating
an Xbox remap. All focused Xbox steps passed on the tested controller and USB
topology.
