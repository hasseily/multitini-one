# C64 USB Input

Status: implemented, host-tested, and vendor-built; first board test pending.

The first PAL test image uses shell and firmware build ID `2026082A`. Its
4,346,380-byte `FIRMWARE.BIN` has SHA-256
`3e211fd424675753afa9a208b8c817bd12e196cb097951a0e38f2b61b8bab0db`
and CRC32 `B25E5862`. Use `docs/C64_USB_INPUT_HARDWARE_TEST.md` to qualify it.

USB1 runs as a CherryUSB host on the ARM. USB packets do not enter the FPGA.
The firmware converts stable HID state into the existing C64 input-event ABI:
PS/2 Set-2 keyboard events and seven active-high joystick bits. Every release
is explicit. Disconnect clears that device, and a core-ready rising edge
reasserts anything still held after reset.

## Keyboard map

The first target is a USB boot-protocol keyboard. Letters, digits, punctuation,
Enter, Space, shifts, Control, function keys, keypad keys, and cursor/edit keys
use their normal PC positions through the FPGA64 Set-2 map.

The important C64-specific assignments are:

| USB key | C64 function |
| --- | --- |
| Escape | Run/Stop |
| Backspace | Delete |
| Tab, Alt, or GUI key | Commodore |
| Caps Lock | Shift Lock |
| F1-F8 | F1-F8 |
| F9 | Up-arrow glyph key |
| F10 | Equals |
| F11 | Restore |
| Insert | Insert |
| Home | Home |
| Delete | Delete |
| Cursor keys | C64 cursor directions |

Boot-keyboard rollover/error reports retain the last good key set instead of
inventing releases. Multiple keyboards are aggregated, so unplugging or
releasing one keyboard does not release a key still held on another.

## Gamepad map

The parser accepts common HID gamepads with X/Y or Rx/Ry axes, a hat, or
individual D-pad usages. The default is C64 joystick port 2.

| HID control | C64 input |
| --- | --- |
| X axis / D-pad left-right | Left / right |
| Y axis / D-pad up-down | Up / down |
| Button 1 | Fire |
| Button 2 | Space |
| Button 3 | Control |

Press `j` at the UART console to swap the USB gamepad between C64 ports 2 and
1. The firmware releases the old port before asserting the new one. Press `u`
to print USB controller, connected-device, report, and error counters.

## Test boundary

Host tests cover held keys, duplicate keyboards, rollover, extended keys,
disconnect releases, delivery retry, joystick aggregation, port swap, and
reset reassertion. The fresh PAL Vivado build passes timing, CDC, bus-skew,
DRC, routing, bitstream, and XSA gates, and the matching Vitis FSBL and
application build passes. Hardware must still prove enumeration, real reports,
typing/chords, disconnect while held, reset while held, hotplug, and one known
gamepad on the board.
