# Vivado and Vitis Handoff

Status: the first diagnostic image and the PAL C64 UART-ROM/frozen-snapshot
image have passed their initial board tests. The current four-bank live-video
source passes focused XSim, a fresh routed PAL build in Vivado 2025.2, and its
first complete board procedure using the matching Vitis/Bootgen package.

The fresh default build used Vivado 2025.2, part `xc7z020clg484-2`, shell build
ID `20260829`, PAL generic `C64_NTSC_MODE=0`, base commit
`fd554dbf46e22514bb132f3dc063ee0a892bdcc6`, and a dirty engineering tree. Its
manifest timestamp is `2026-08-29T15:33:38Z`. Vivado generated the routed
bitstream and XSA. Routed timing passes with WNS +0.675 ns, WHS +0.053 ns, WPWS
+1.749 ns, and zero failing endpoints. All 11,033 routable nets are fully
routed with zero routing errors, and DRC reports zero errors.

The final CDC report contains only 14 `CDC-3` informational crossings and one
`CDC-9` informational reset crossing; the fatal critical-CDC gate passed.
`check_timing` reports zero no-clock registers, unconstrained internal
endpoints, missing input delays, and genuinely missing output delays. The
SPDIF false path and the forwarded I2S/DVI clocks remain reported as
informational exceptions.

The routed live design uses 6,168 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
RAMB18 block, and seven DSP blocks. The four-bank bridge uses 543 LUTs, 335
flip-flops, and 64 RAMB36 blocks. Total design use is 85.5 of 140 block-RAM
tiles (61.07%), leaving 54.5 tiles. All four bus-skew constraints pass; the two
live Gray-pointer crossings have actual skews 0.507 ns and 0.610 ns against a
6.733 ns requirement. The DVI output path group passes with setup slack
+1.672 ns and hold slack +1.718 ns.

The exact AK4493 datasheet was obtained and reviewed locally. The I2S LRCK and
SDATA constraints now use a conservative +/-5 ns receiver window, and the
routed build passes with those delays active.

## Required environment

- Vivado 2025.2 with Zynq-7000 support.
- Vitis 2025.2.
- A clean clone with no generated project state.
- The exact commits in `provenance/sources.lock.json`.

## Before project creation

Run:

```sh
make check
git status --short
```

Require all checks to pass and the working tree to contain only intentional
changes.

## Project creation

Run from the repository root:

```sh
vivado -mode batch -source scripts/create_project.tcl
```

Stop on every Tcl error. Confirm:

- part `xc7z020clg484-2`;
- top `multitini_one_top`;
- PS input clock 33.333333 MHz;
- QSPI, SDIO1, I2C0, UART0/1, USB0/1 MIO assignments;
- USB reset MIO remains disabled;
- no Apple clock or reset feeds an internal clock/reset;
- the default C64 Clocking Wizard requests 31.527954 MHz PAL from FCLK2 and has
  its own synchronized reset;
- the mixed VHDL/SystemVerilog compile order resolves `fpga64_sid_iec`, the
  Multitini bus logic/memory ports, SID compatibility ports, and adapter;
- the selected C64 source includes the four-bank indexed live bridge and its
  Gray-pointer crossing primitive;
- all source and constraint files came from the tracked manifests; and
- the block design validates.

The default is the PAL C64 project. The tracked manifest has explicit common,
C64, and GBC sections; project creation loads only common sources plus the
selected machine section. This keeps an inactive upstream core from becoming
a build dependency for the selected image.

Create a separate first-pass GBC project with:

```sh
MULTITINI_MACHINE=GBC \
  MULTITINI_ENABLE_GBC_PSRAM=0 \
  vivado -mode batch -source scripts/create_project.tcl
```

Confirm that the build generic selects `ACTIVE_MACHINE=1`, the machine Clocking
Wizard requests 33.554432 MHz, and `ENABLE_GBC_PSRAM=0`. The PSRAM pins must
remain inactive in this project. The wizard and generated block-design port
retain their historical `c64_clk` names; they carry the selected machine clock.

## Synthesis and implementation

Run:

```sh
vivado -mode batch -source scripts/build_and_export_xsa.tcl
```

The script must fail if synthesis, implementation, timing, constraint binding,
or bitstream generation fails. Review, do not waive:

- unconstrained endpoints;
- critical warnings;
- every generated clock and clock group;
- the achieved C64 clock frequency and error from 31.527954 MHz PAL or
  32.727266 MHz NTSC;
- for the GBC project, the achieved machine frequency and error from
  33.554432 MHz;
- C64 main RAM and writable ROM inference, port mode, and read latency;
- all four 382x270x4 video-bank inferences, port modes, read latency, and total
  block-RAM use;
- XPM handshake constraints, the error-clear toggle synchronizer, and every
  control-to-machine CDC path;
- both live-video `xpm_cdc_gray` pointer crossings, their monotonic source
  behavior, the independent-reset grant/acknowledge barrier, and absence of a
  simultaneous source-write/display-read bank;
- GBC T80/mapper/audio/video mixed-language elaboration, the portable dual-port
  RAM modes and read latencies, the bootstrap hex initialization, and the
  one-line cartridge-cache stall path;
- every false path and max-delay object count;
- setup, hold, pulse-width, and bus-skew reports;
- Apple-bus pin constants after optimization;
- DVI source-synchronous output timing;
- audio clock crossings;
- PSRAM I/O placement and capture rules; and
- LUT, register, BRAM, DSP, BUFG, MMCM/PLL, and I/O use.

Save the exact reports and build manifest. A clean build is not hardware proof.

The verified result above covers the current default PAL C64 live-video
project. The corresponding packaged image also passes its first board test.
The separate NTSC C64 and GBC vendor builds remain pending. GBC PSRAM remains
disabled and unqualified.

The selected machine adapter is marked `DONT_TOUCH` so synthesis and timing
cannot silently remove the integration. Its registered PAL RGB output feeds an
exact-frame bridge that recognizes the fixed 16-color palette and stores
four-bit indices in four frame banks. Completed-frame and released-frame
pointers cross through `xpm_cdc_gray`. The 1080p60 side acquires at most one
approximately 50.123 Hz PAL frame at each vertical blank; if none is ready, it
repeats the current bank. Diagnostic/machine source selection also changes only
at vertical blank. A full ring drops an entire incoming source frame before
capture. Physical audio still uses the independent diagnostic generator. This
description is supported by source, focused XSim, and the completed PAL Vivado
build and the recorded live-video board procedure.

After the default PAL build is reviewed, recreate the project for NTSC with:

```sh
MULTITINI_C64_VIDEO_STANDARD=NTSC \
  vivado -mode batch -source scripts/create_project.tcl
```

Keep separate PAL and NTSC logs/reports. The script rejects other standard
names and records the selected generic and requested clock.

The TFP410 constraint uses the rev. D, single-edge receiver requirements that
match the schematic straps: 1.2 ns setup and 1.3 ns hold. Confirm that Vivado
models the ODDR forwarded edge one half pixel after the RGB/control launch.

The imported PSRAM PHY and simulated BIST are present as staged source. The GBC
top has an opt-in physical branch, but it defaults off. Keep PSRAM inactive for
the first board image. Its command timing, input capture edge/tap, IDELAYCTRL,
and external constraints are a separate Phase 3 review.

After the inactive GBC project elaborates and its resource estimate is
acceptable, validate PSRAM independently. Add and review external timing
constraints, confirm the controller's fixed command edge and count choices,
run the destructive BIST over temperature/reset cases, and record measured
latency. Only then create an integration project with:

```sh
MULTITINI_MACHINE=GBC \
  MULTITINI_ENABLE_GBC_PSRAM=1 \
  vivado -mode batch -source scripts/create_project.tcl
```

This opt-in command is not permission to program hardware before the PSRAM
gates above pass. Re-run synthesis, CDC, timing, utilization, and pin reports
with the physical path enabled.

## Vitis

After a successful XSA export:

```sh
vitis -s scripts/create_vitis_workspace.py
```

Confirm the platform uses that exact XSA. For the current live test image,
Vitis 2025.2 built both the FSBL and diagnostic ELF from the fresh XSA. Bootgen
packaged and read back the expected FSBL, routed bitstream, and two application
segments. The 4,241,740-byte `FIRMWARE.BIN` has SHA-256
`a44943738448e3114e58c03a30a2210270684ecf849911621e045ae5771f8703` and
CRC32 `01F764E0`. Do not enable writable media until the read-only path and
power-loss rules are tested.

The USB-input test application starts with the core held in reset and the DAC muted. It probes
the ABI, logs the TMP102 temperature when I2C works, initializes the DAC without
unmuting, and accepts UART commands for status, ping, video, audio mute/unmute,
core reset, and reboot to the golden image. `:c64rx` receives an exact 20 KiB
XMODEM-1K/CRC bundle from `scripts/serial_c64_rom_upload.py`, verifies the outer
CRC32 and three ROM slices, streams them, and releases reset only after the core
reports ready. The uploader defaults to the repository's selected three-file
ROM set and retains explicit path overrides. SD loading remains absent.
The portable GBC helper can stream a validated cartridge and optional bootstrap
override; it is likewise not connected to SD or the diagnostic CLI.

The current USB-input image was rebuilt from a separate PAL XSA with shell and
firmware build ID `2026082A`. Timing passes at WNS +0.394 ns, WHS +0.057 ns,
and WPWS +1.749 ns. Its 4,346,380-byte `FIRMWARE.BIN` has SHA-256
`3e211fd424675753afa9a208b8c817bd12e196cb097951a0e38f2b61b8bab0db`
and CRC32 `B25E5862`. Physical input remains unqualified.

## Next step: physical input

The independent diagnostics, ROM-upload/frozen-frame image, and exact packaged
live-video image have passed on the board. The live test produced a blinking
BASIC cursor, a stable tear-free five-minute soak, clean whole-frame `v`
switches, correct `r` reset/rearm behavior, and successful `:reboot` to golden.
Preserve that firmware and report set as the PAL video baseline.

Run `docs/C64_USB_INPUT_HARDWARE_TEST.md` one subsystem at a time. Start with
the USB keyboard path, then joystick/gamepad mapping. Before treating either
as a machine result, retain the board safety checks:

- the stand-alone power jumper and connector;
- all Apple main transceiver OE pins are high;
- Apple direction pins and auxiliary OE are low;
- Apple address, data, R/W, and control lanes are high impedance;
- PSRAM CE is high;
- W5100S CS and read/write strobes are high; and
- audio output is muted.

Then execute the applicable Phase 1 and Phase 2 gates in
`MULTITINI_ONE_PLAN.md`. Record
board revision, jumper details, display, keyboard, gamepad, SD card, supply,
ambient temperature, bitstream build ID, firmware build ID, and UART log.
