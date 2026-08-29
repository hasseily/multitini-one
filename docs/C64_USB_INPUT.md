# C64 USB Input

Status: implemented, host-tested, and vendor-built. Physical USB1
VBUS and HID validation of the corrected image remain pending; the preceding
`2026082A` image reached controller startup but failed its connector-power test.

The current PAL image uses shell and firmware build ID `2026082B` and records
source/manifest commit `c2b87fad2913580de296eb9b06a9186b57deeefa`.
Its 4,351,948-byte `FIRMWARE.BIN` has SHA-256
`e13e1b3df355e38590ebff3c5f496fa10a8b5c1d89bee0066f649aed1caf9425`,
CRC32 `F31417DA`, and 10,262,580 bytes free in the firmware slot. Use
[`C64_USB_INPUT_HARDWARE_TEST.md`](C64_USB_INPUT_HARDWARE_TEST.md) to qualify
this exact artifact.

USB1 runs as a CherryUSB host on the ARM. USB packets do not enter the FPGA.
The firmware converts stable HID state into the existing C64 input-event ABI:
PS/2 Set-2 keyboard events and seven active-high joystick bits. Every release
is explicit. Disconnect clears that device, and a core-ready rising edge
reasserts anything still held after reset.

## USB1 power setup

Build `2026082B` explicitly configures the USB3300 through the Zynq ULPI
viewport. It requires Function Control `0x41`, Interface Control `0x10`, and
OTG Control `0x66`: host `AutoResume` is enabled, both VBUS-drive bits assert
`CPEN`, and the internal VBUS comparator checks the switched output. The
external VBUS indicator remains disabled because the board's open-drain
TPS25221 `FAULT#` net has no pull-up. UART command `u` reports the setup,
drive, VBUS-valid, identity, register, and error state used by the board test.

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
reset reassertion. The fresh `2026082B` PAL build passes timing at WNS
+0.119 ns, WHS +0.053 ns, and WPWS +1.749 ns, with all negative-slack totals
and failing-endpoint counts zero. CDC has 15 Info entries only, all four
bus-skew checks pass, routing and DRC have zero errors, and the matching Vitis
and Bootgen artifact is complete. Hardware must still prove physical VBUS,
enumeration, real reports, typing/chords, disconnect while held, reset while
held, hotplug, and one known gamepad on the board.
