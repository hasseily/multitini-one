# Multitini One

Multitini One turns the Appletini ONE card into a stand-alone FPGA system.
The first product target is a C64 built from the GPL MiSTer C64 core. Game Boy
Color is the second adapter used to prove that the shell is reusable. Neither
machine uses an Apple II host.

The independent PAL DVI timing, fixed-tone audio, UART control, and golden
reboot paths have passed their first board test. The following frozen-frame
image proved UART ROM upload, reset release, C64 boot, and coherent scaling.
The preserved four-bank live PAL baseline also passed its board gate: the
BASIC cursor remained live and blinking, the five-minute display soak stayed
locked and tear-free, repeated `v` switches were whole-frame, `r` reset and
rearmed correctly, and `:reboot` returned to golden.

## Current state

Known:

- The board source baseline is Appletini commit `052fe8aa43c483c193fcbc10ecba4225272c213d`.
- The C64 source baseline is MiSTer C64 commit `62fc697e3c9b5982c9dc76aa8cd03db545d20803`.
- The Game Boy source baseline is MiSTer Game Boy commit
  `7a5ff50528cd9c1d13ffb675e7df8506bffaa078`.
- The common register ABI, host-testable firmware code, diagnostic FPGA shell,
  and scripted Vivado/Vitis flow live in this repository.
- The 34-file C64 allowlist is imported exactly, SHA-256 locked, and covered by
  the per-file license ledger under the recorded redistribution assumption.
- A ROM-free `fpga64_sid_iec` build boundary, writable ROM adapter, ARM byte
  stream, coherent clock-domain mailboxes, live C64 input mapping, and
  reset-safe ROM boot helper are implemented.
- The UART diagnostic accepts an exact 20 KiB BASIC/KERNAL/character bundle
  through XMODEM-1K/CRC, verifies an outer CRC32, and releases reset only after
  all three FPGA ROM streams complete and the core reports ready.
- A fail-closed dual-clock live bridge stores the fixed C64 palette as four-bit
  indices in four 382x270 frame banks. Complete PAL frames enter a Gray-pointer
  ring; the 1080p60 side changes display banks only at vertical blank. A missing
  PAL frame is repeated cleanly, and a full ring drops a whole incoming frame
  before capture rather than exposing a partial frame.
- The 47-file Game Boy allowlist, open SameBoy-derived CGB bootstrap, generated
  strict-tool compatibility ports, GB/GBC content validation, cartridge boot
  helper, adapter, one-line cartridge cache, and shared external-memory ABI are
  implemented and provenance locked.
- The board shell selects C64 or GBC at project creation. A GBC PSRAM CDC/PHY
  path is prepared but deliberately disabled by default pending Vivado timing
  and board validation.
- The AXI register path, DVI timing generator, audio path, PSRAM BIST, and C64
  ROM loader/input bridges pass HDL simulations. The asynchronous mailbox,
  frozen-frame predecessor, and four-bank live C64 bridge pass focused Vivado
  XSim, and the selected SystemVerilog C64 path passes Verilator lint.
- The current PAL C64 USB-input project completed synthesis, implementation,
  bitstream generation, and XSA export in Vivado 2025.2 with shell build ID
  `2026082D`. Routed timing passed with WNS +0.525 ns, WHS +0.040 ns, WPWS
  +1.749 ns, and no failing endpoints.
- The current routed design uses 6,170 LUTs, 5,703 flip-flops, 85 RAMB36 blocks,
  one RAMB18 block, and seven DSP blocks. The four-bank video bridge accounts
  for 546 LUTs, 335 flip-flops, and 64 RAMB36 blocks.
- Vitis 2025.2 built the matching FSBL and diagnostic application from the
  fresh XSA. Bootgen created and read back the expected FSBL, routed bitstream,
  and application partitions. The resulting 4,365,516-byte root
  `FIRMWARE.BIN` has SHA-256
  `536b68ff99f53925d7825d70d7ee30bb586edb8cb301c1e5feff1607248a6600`
  and CRC32 `0680EEAB`.
- The preserved `20260829` live-video baseline passed its PAL board test. Its
  blinking BASIC cursor proves continuous delivery rather than a frozen
  capture; the five-minute soak, whole-frame source toggles, reset/rearm, and
  golden reboot all passed.
- USB1 host input is now implemented with pinned CherryUSB source. The ARM
  translates USB boot-keyboard, common HID gamepad, and wired Xbox 360 reports
  into the existing reset-safe C64 input-event mailbox. The default keyboard
  map is US symbolic; UART `k` restores the original MiSTer positional map.
  Disconnect releases, rollover retention, alias-safe aggregation, delivery
  retry, reset reassertion, and joystick port swap are covered by host tests.
- Build `2026082B` proves USB1 connector power, keyboard input, and one generic
  HID gamepad on the board. It exposed a hybrid symbol-map failure and an Xbox
  `Endpoint num 4 overflow` failure. Build `2026082C` fixed both failures and
  qualified the US symbolic and retained positional keyboard maps. Build
  `2026082D` qualifies wired Xbox 360 report decoding, D-pad and left-stick
  directions, A as Fire, B as Space, X as Control, and the player LED command
  on the tested controller.
- The exact AK4493 datasheet has been reviewed. Conservative I2S output delays
  are now part of the implemented-design timing constraints.
- Portable content, XMODEM, DAC, temperature, and shell-link code passes the
  normal strict host tests. The Vitis entry point compiles against strict host
  stubs for the Xilinx APIs it uses. The current Windows GCC installation does
  not provide the requested ASan/UBSan runtime libraries, so that optional
  link was not part of this build's qualification.
- The exact C64 BASIC, KERNAL, and character ROMs selected by the repository
  owner are tracked build/test inputs with recorded hashes. Other unapproved
  ROM/media and generated FPGA or firmware products remain blocked from source
  control. See `docs/CONTENT_POLICY.md` for the per-core rule.

Hardware status and remaining gaps:

- The first diagnostic PAL image booted through the golden updater; DVI bars,
  DVI toggle, fixed audio tone, and `:reboot` were observed working. See
  `docs/HARDWARE_RESULTS.md`.
- The ROM-upload and frozen-C64-frame image passed on a card. That result proves
  the upload, reset release, C64 boot, coherent snapshot, source switch, and
  golden-reboot path. It does not prove live video, physical input, storage, or
  SID audio.
- The four-bank live-video source passes focused XSim, its fresh PAL Vivado
  build, and its first board test. This qualifies live PAL C64 video on the
  tested card/display combination, not physical input, storage, or SID audio.
- NTSC and GBC Vivado builds remain pending. The GBC PSRAM path remains
  disabled by default and awaits timing and board validation.
- SD file-system integration is not implemented. USB1 power, both keyboard
  maps, generic HID gamepad input, and the `2026082D` wired Xbox 360 input map
  and player LED command are board-proven. Xbox disconnect, hotplug, and
  unplug-while-held coverage remain open. The tracked C64 ROM set is loaded
  over UART; the pinned open GBC bootstrap remains the only ROM embedded in an
  FPGA image.
- DVI timing and the fixed diagnostic tone have initial board observations.
  Their long-run gates, remaining USB hotplug checks, SD, DDR, PSRAM,
  and Apple-bus isolation still require the hardware work in
  `MULTITINI_ONE_PLAN.md`.

## Host checks

The checks available on macOS require Python 3, CMake, a C compiler, Tclsh,
Icarus Verilog 13 or later, and Verilator 5:

```sh
make check
```

This regenerates and compares the C/SystemVerilog ABI files, checks repository
policy and provenance, checks the Tcl/build manifests and pin coverage, builds
the portable firmware library with and without sanitizers, compiles the Vitis
entry point against API stubs, runs host and HDL tests, and lints the selected
C64 and GBC SystemVerilog modules. It does not replace Vivado, Vitis, timing
review, or board testing.

## Vendor-tool handoff

The intended first commands on a Vivado 2025.2 machine are:

```powershell
$env:MULTITINI_BUILD_ID = '2026082D'
vivado -mode batch -source scripts/create_project.tcl
vivado -mode batch -source scripts/build_and_export_xsa.tcl
vitis -s scripts/create_vitis_workspace.py
```

Set `MULTITINI_BUILD_ID` to the eight-digit firmware build ID in
`firmware/platform/xilinx/main.c`. The shell and firmware IDs must match.

Build the root firmware directly from the exact generated artifacts in
PowerShell with:

```powershell
.\scripts\make_firmware_bin.bat `
  .\FIRMWARE.BIN `
  .\build\vitis_workspace\multitini_platform\export\multitini_platform\sw\boot\fsbl.elf `
  .\build\vivado\multitini_one.runs\impl_1\multitini_one_top.bit `
  .\build\vitis_workspace\diagnostic\build\diagnostic.elf
```

The completed independent diagnostic and frozen-snapshot tests are recorded in
[`docs/HARDWARE_RESULTS.md`](docs/HARDWARE_RESULTS.md). The preserved snapshot
procedure is in
[`docs/C64_SNAPSHOT_HARDWARE_TEST.md`](docs/C64_SNAPSHOT_HARDWARE_TEST.md).
For the live test image, use
[`docs/C64_LIVE_VIDEO_HARDWARE_TEST.md`](docs/C64_LIVE_VIDEO_HARDWARE_TEST.md).
For the USB keyboard/gamepad image, use
[`docs/C64_USB_INPUT_HARDWARE_TEST.md`](docs/C64_USB_INPUT_HARDWARE_TEST.md).
None of these engineering images is a complete C64 release.

Project creation defaults to PAL. Set
`MULTITINI_C64_VIDEO_STANDARD=NTSC` while running `create_project.tcl` to make
the independently reproducible NTSC build.

Create the source-only GBC project with:

```sh
MULTITINI_MACHINE=GBC vivado -mode batch -source scripts/create_project.tcl
```

This requests 33.554432 MHz and leaves the physical PSRAM path disabled. Read
`docs/VIVADO_HANDOFF.md` before opting into that path.

See `docs/HANDOFF_STATUS.md` and `docs/VIVADO_HANDOFF.md` before rebuilding.

## Licensing

Original Multitini work is licensed under GPL-3.0-only. Imported files retain
their own notices and licenses. See `licenses/README.md` and
`licenses/files.csv`. No ROM or media rights are granted by an HDL license.
