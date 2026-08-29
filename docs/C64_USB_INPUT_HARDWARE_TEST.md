# PAL C64 USB Input Hardware Test

This procedure qualifies the first physical-input firmware. It assumes the
already-qualified live PAL C64 video path and uses the board's USB1 host port.

Use only this packaged image:

- package: `Multitini-One-PAL-C64-usb-input-2026082A.zip`;
- `FIRMWARE.BIN` size: 4,346,380 bytes;
- `FIRMWARE.BIN` SHA-256:
  `3e211fd424675753afa9a208b8c817bd12e196cb097951a0e38f2b61b8bab0db`;
- `FIRMWARE.BIN` CRC32: `B25E5862`; and
- shell and firmware build ID: `2026082A`.

Load `FIRMWARE.BIN` with the golden UART updater. Then upload the bundled
20 KiB ROM set with `scripts/serial_c64_rom_upload.py`, as for the qualified
live-video image. Do not overwrite the golden slot.

## Keyboard

1. Boot the packaged firmware through the golden updater and display the live
   C64 BASIC screen with `v` if necessary.
2. Connect one known USB keyboard to USB1. At UART, press `u`. Confirm
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
returns to golden. Record the keyboard and gamepad make/model and the final
`u` status line. Until this procedure passes, USB input is implemented but not
hardware-qualified.
