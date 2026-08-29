# PAL C64 USB Input Hardware Test

This procedure qualifies physical USB1 VBUS first and then the C64 keyboard and
gamepad path in firmware build `2026082B`. It assumes the already-qualified
live PAL C64 video path and uses the board's USB1 host port.

Build `2026082A` is a failed-power baseline only. It set `PORTSC.PP`, but the
user observed no keyboard power and no USB attach. Do not use it for this
procedure. Use only a packaged `2026082B` image after its exact size, SHA-256,
CRC32, and Bootgen readback have been recorded in
[`HARDWARE_RESULTS.md`](HARDWARE_RESULTS.md#build-2026082b-usb3300-vbus-correction-awaiting-board-test).
Confirm the UART firmware banner says `build=2026082B`; record the shell build
ID separately rather than assuming that it matches the firmware ID.

Copy repository-root `FIRMWARE.BIN` to the root of the SD card under that exact
name, then send `:reboot` or power-cycle. Golden programs and verifies the
secondary firmware slot, renames the file to `FIRMWARE.OK`, and boots it
automatically. After confirming `build=2026082B`, upload the bundled 20 KiB ROM
set with `scripts/serial_c64_rom_upload.py`. Do not overwrite the golden slot.

## USB1 PHY and VBUS

1. Leave USB1 empty, boot the test image, and press `u` at UART.
2. On the controller line confirm `started=1`, `keyboards=0`, `gamepads=0`, and
   zero submit, transfer, and input error counters.
3. On the PHY line confirm `setup_rc=0`, `disable_rc=0`, `drive_known=1`,
   `drive=1`, `vbus_known=1`, `vbus_valid=1`, `ulpi_err=0`, `timeouts=0`, and
   `id=0424:0004`. Confirm `func=41`, `iface=10`, and `otg=66`. These values use
   the internal VBUS comparator; `UseExternalVbusIndicator` must remain clear.
4. Confirm physical USB1 power with a USB power meter or by connecting a known
   keyboard and observing its power indicator. Record the method and result.
   Register readback alone does not qualify connector power.
5. With the keyboard connected, press `u` again and confirm `keyboards=1` and
   an increasing report count after input, with no ULPI or transfer error.
6. Repeat the empty-port PHY/VBUS check after a cold power-up and after returning
   to golden with `:reboot` and reloading the image. Confirm USB1 power drops
   during the return to golden; any `USB1 VBUS shutdown failed` message is a
   test failure and must cancel the reboot.

## Keyboard

1. After the PHY/VBUS stage passes, display the live C64 BASIC screen with `v`
   if necessary.
2. Use the connected known USB keyboard. At UART, press `u`. Confirm
   `started=1`, `keyboards=1`, and an increasing report count after input.
3. Type `PRINT 2+2` and press Enter. Confirm the C64 prints `4`.
4. Check both shifts, Control, Commodore, Run/Stop, function keys, cursor keys,
   Home, Insert, and Delete. Check a three-key chord and a held repeating key.
5. Hold a harmless key, disconnect the keyboard, reconnect it, and confirm no
   key remains stuck.
6. Hold a key, toggle core reset with `r`, release reset with `r`, and confirm
   the held state is reasserted and the later release is accepted.

## Gamepad

1. Connect one known USB HID gamepad. Press `u` and confirm `gamepads=1`.
2. At BASIC, use `PRINT PEEK(56320)` for the default joystick port 2. Neutral
   is `127`; directions and button 1 must pull the corresponding bits low.
3. Press `j`, confirm the UART says port 1, and repeat with
   `PRINT PEEK(56321)`.
4. Check diagonals, hold/release, button 1, unplug while held, reconnect, and
   hotplug after boot. Buttons 2 and 3 are mapped to Space and Control.

## Regression

During all input checks, confirm the BASIC cursor remains live, DVI remains
locked and tear-free, `v` still switches whole frames, and `:reboot` still
returns to golden. Record the physical VBUS check, keyboard and gamepad
make/model, and both final `u` status lines. Until the VBUS stage and all HID
checks pass, build `2026082B` is implemented but USB1 power and HID input are
not hardware-qualified.
