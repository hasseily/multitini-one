# C64 USB Input

Status: build `2026082B` is board-proven for USB1 connector power, keyboard
enumeration and input, and one generic HID gamepad (`06a3:0107`). It is not a
US-keymap or Xbox qualification image: `Shift+=` did not produce arithmetic
plus, and the Xbox 360 controller (`045e:028e`) failed configuration parsing
with `Endpoint num 4 overflow`.

Build `2026082C` introduced the US symbolic keyboard map and dedicated wired
Xbox 360 path. Its board test qualified both US symbolic and retained MiSTer
positional keyboard modes, plus Xbox enumeration, report transport, D-pad, and
left-stick directions. A/Fire and the blinking player ring remained open.

Current build `2026082D` adds per-XInput state/raw-report diagnostics and sends
the wired-controller player-1 LED command. Vivado, Vitis, and Bootgen passed,
and the focused board test qualifies the intended Xbox input map and player LED
command on controller `045e:028e`. Its root `FIRMWARE.BIN` is 4,365,516 bytes,
has SHA-256
`536b68ff99f53925d7825d70d7ee30bb586edb8cb301c1e5feff1607248a6600`,
CRC32 `0680EEAB`, and 10,249,012 bytes free in the firmware slot.

The historical PAL image uses shell and firmware build ID `2026082B` and records
source/manifest commit `c2b87fad2913580de296eb9b06a9186b57deeefa`.
Its 4,351,948-byte `FIRMWARE.BIN` has SHA-256
`e13e1b3df355e38590ebff3c5f496fa10a8b5c1d89bee0066f649aed1caf9425`,
CRC32 `F31417DA`, and 10,262,580 bytes free in the firmware slot. Use
[`HARDWARE_RESULTS.md`](HARDWARE_RESULTS.md) for that historical result. Use
[`C64_USB_INPUT_HARDWARE_TEST.md`](C64_USB_INPUT_HARDWARE_TEST.md) for the
current `2026082D` result and the remaining regression steps.

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
individual D-pad usages. Build `2026082C` and later also bind wired Xbox
360/XInput controllers, including `045e:028e`. The default is C64 joystick
port 2.

| HID control | C64 input |
| --- | --- |
| X axis / D-pad left-right | Left / right |
| Y axis / D-pad up-down | Up / down |
| Button 1 | Fire |
| Button 2 | Space |
| Button 3 | Control |

For Xbox 360 reports, the D-pad and left stick drive directions, A is Fire, B
is Space, and X is Control. Build `2026082D` demonstrated every intended state
bit on hardware: A/Fire produced masked joystick value `15`, B produced Space,
and holding X while pressing a number changed the C64 cursor color, proving
Control end to end. X alone was correctly nonprinting as a modifier. Its
player-1 LED output completed in one attempt. HID and XInput controllers use
separate aggregate slots, so they can coexist on a hub without clearing each
other's state.

Press `j` at the UART console to swap the USB gamepad between C64 ports 2 and
1. The firmware releases the old port before asserting the new one. Press `u`
to print USB controller, connected-device, report, and error counters. Build
`2026082D` also prints a per-XInput line with decoded state, all-seen bits,
state-change count, LED-output state, errors, and the latest raw packet prefix.

Joystick port 1 shares the C64 CIA keyboard-matrix input lines. At the BASIC
prompt, its direction and Fire closures can therefore appear as characters
during the ROM keyboard scan. The recorded port-1 Xbox test produced the
left-arrow glyph, `2`, and other matrix-dependent characters from the D-pad;
A/Fire appeared as Space. Those are expected port-1 fingerprints, not an Xbox
mapping error. B and X remain the separate Space and Control convenience
mappings on either joystick port.

## Test boundary

Eleven host binaries and 44 Python tests cover symbolic and positional key
maps, punctuation, keypad aliases, held keys, duplicate keyboards, rollover,
extended keys, disconnect releases, delivery retry, HID/XInput decoding,
joystick aggregation, port swap, and reset reassertion. Build `2026082B`
proves physical VBUS and one generic HID gamepad; build `2026082C` qualifies
both keyboard maps. Build `2026082D` qualifies the wired Xbox 360 functional
input map and player LED command on the tested controller. Hotplug after boot,
disconnect while held with state release, reconnect, and runtime joystick-port
swap also pass on that controller and USB topology.
