# C64 USB Input

Status: build `2026082B` is board-proven for USB1 connector power, keyboard
enumeration and input, and one generic HID gamepad (`06a3:0107`). It is not a
US-keymap or Xbox qualification image: `Shift+=` did not produce arithmetic
plus, and the Xbox 360 controller (`045e:028e`) failed configuration parsing
with `Endpoint num 4 overflow`.

Current build `2026082C` source defaults to a US symbolic keyboard map and adds
a dedicated wired Xbox 360 input path. Host tests pass. Board qualification is
pending. Its current root `FIRMWARE.BIN` is 4,362,060 bytes, has SHA-256
`ae75318d458c6f6203c5ed6a87be1283ace6b39dacf0307c7e82c52e281ea004`,
CRC32 `EF6EB912`, and 10,252,468 bytes free in the firmware slot. The matching
routed bitstream has SHA-256
`e0b6df0a38dcbd1c4a6f31e2274d4c74e62cf77489f9332bd36a276a9c650e4b`;
the XSA has SHA-256
`530322983caa359baedcd1cbc50b990b0657862c3c6cf545ba91f9f86289158c`.
Bootgen readback found the expected FSBL, bitstream, and two application
segments.

The historical PAL image uses shell and firmware build ID `2026082B` and records
source/manifest commit `c2b87fad2913580de296eb9b06a9186b57deeefa`.
Its 4,351,948-byte `FIRMWARE.BIN` has SHA-256
`e13e1b3df355e38590ebff3c5f496fa10a8b5c1d89bee0066f649aed1caf9425`,
CRC32 `F31417DA`, and 10,262,580 bytes free in the firmware slot. Use
[`HARDWARE_RESULTS.md`](HARDWARE_RESULTS.md) for that historical result. Use
[`C64_USB_INPUT_HARDWARE_TEST.md`](C64_USB_INPUT_HARDWARE_TEST.md) to qualify
the current `2026082C` artifact above.

USB1 runs as a CherryUSB host on the ARM. USB packets do not enter the FPGA.
The firmware converts stable HID state into the existing C64 input-event ABI:
PS/2 Set-2 keyboard events and seven active-high joystick bits. Every release
is explicit. Disconnect clears that device, and a core-ready rising edge
reasserts anything still held after reset.

## USB1 power setup

Build `2026082B` and later explicitly configure the USB3300 through the Zynq ULPI
viewport. It requires Function Control `0x41`, Interface Control `0x10`, and
OTG Control `0x66`: host `AutoResume` is enabled, both VBUS-drive bits assert
`CPEN`, and the internal VBUS comparator checks the switched output. The
external VBUS indicator remains disabled because the board's open-drain
TPS25221 `FAULT#` net has no pull-up. UART command `u` reports the setup,
drive, VBUS-valid, identity, register, and error state used by the board test.
The idle setup reads Function Control `0x41`. That register changes with the
active device speed; the working low-speed generic gamepad was observed with
`func=46`, so attached-device tests must not require `func=41`.

## Keyboard map

The default is `keymap=us`, a US/PC symbolic layout. Letters, digits, Space,
Enter, cursor/edit keys, and ordinary shifted letters retain their normal
behavior. Symbol substitutions apply when that symbol is the only ordinary key
in its keyboard report. Multi-key reports retain safe positional behavior so
ordinary chords such as `Shift+A` remain intact.

The symbolic assignments and C64 fallbacks are:

| PC input | C64 result |
| --- | --- |
| `Shift+2` | `@` |
| `Shift+6` | Up-arrow |
| `Shift+8` | `*` |
| `Shift+-` | `_` through Commodore+`@` |
| `=` / `Shift+=` | `=` / `+` |
| `[` / `{` | `[` / closest `[` fallback |
| `]` / `}` | `]` / closest `]` fallback |
| `;` / `Shift+;` | `;` / `:` |
| `'` / `Shift+'` | `'` / `"` |
| `Shift+7` / `Shift+9` / `Shift+0` | `&` / `(` / `)` |
| `Shift+,` / `Shift+.` / `Shift+/` | `<` / `>` / `?` |
| Backslash / pipe | C64 pound / shifted-pound fallback |
| Grave / tilde | C64 left-arrow / shifted PETSCII fallback |
| Escape | Run/Stop |
| Backspace | Delete |
| Tab, Alt, or GUI key | Commodore |
| Caps Lock | Shift Lock |
| F1-F8 | F1-F8 |
| F9 | Raw C64 up-arrow |
| F10 | Raw C64 plus |
| F11 | Restore |
| F12 | Raw C64 equals |
| Keypad operators and digits | Matching C64 keys |
| Insert | Insert |
| Home | Home |
| Delete | Delete |
| Cursor keys | C64 cursor directions |

The C64 has no ASCII braces, pipe, grave accent, or tilde. The table states the
deliberate native fallback for each. Alt or either GUI key remains Commodore,
so Commodore-letter and Commodore-symbol PETSCII chords remain available.
F9-F12 provide the C64 keys that have no direct US-PC key.

Press `k` at the UART console to toggle between `US symbolic` and the original
`MiSTer positional` layout. No Enter is required. Press `u` to confirm
`keymap=us` or `keymap=positional`. The choice is runtime-only; every firmware
boot starts in US symbolic mode.

Boot-keyboard rollover/error reports retain the last good key set instead of
inventing releases. Multiple keyboards are aggregated, so unplugging or
releasing one keyboard does not release a key still held on another.

## Gamepad map

The parser accepts common HID gamepads with X/Y or Rx/Ry axes, a hat, or
individual D-pad usages. Build `2026082C` also binds wired Xbox 360/XInput
controllers, including `045e:028e`. The default is C64 joystick port 2.

| HID control | C64 input |
| --- | --- |
| X axis / D-pad left-right | Left / right |
| Y axis / D-pad up-down | Up / down |
| Button 1 | Fire |
| Button 2 | Space |
| Button 3 | Control |

For Xbox 360 reports, the D-pad and left stick drive directions, A is Fire, B
is Space, and X is Control. HID and XInput controllers use separate aggregate
slots, so they can coexist on a hub without clearing each other's state.

Press `j` at the UART console to swap the USB gamepad between C64 ports 2 and
1. The firmware releases the old port before asserting the new one. Press `u`
to print USB controller, connected-device, report, and error counters.

## Test boundary

Eleven host binaries and 44 Python tests cover symbolic and positional key
maps, punctuation, keypad aliases, held keys, duplicate keyboards, rollover,
extended keys, disconnect releases, delivery retry, HID/XInput decoding,
joystick aggregation, port swap, and reset reassertion. Build `2026082B`
already proves physical VBUS, keyboard input, and one generic HID gamepad on
the board. Build `2026082C` must still pass the new keymap and Xbox procedure
before either change is claimed as hardware-qualified.
