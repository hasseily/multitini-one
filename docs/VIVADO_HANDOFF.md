# Vivado and Vitis Handoff

Status: the diagnostic, PAL C64 frozen-snapshot, and PAL live-video images have
passed their recorded board procedures. The corrected USB-input image has a
fresh Vivado 2025.2 build and matching Vitis/Bootgen artifact; physical USB1
VBUS and HID validation remain pending.

The fresh default PAL C64 build used Vivado 2025.2, part
`xc7z020clg484-2`, shell and firmware build ID `2026082B`, PAL generic
`C64_NTSC_MODE=0`, and source/manifest commit
`c2b87fad2913580de296eb9b06a9186b57deeefa`. The exported XSA has SHA-256
`f3b0608b77e7b0c8f4612d3a61665234453c3f4f98dc067888f1635f157c953e`; the
routed bitstream has SHA-256
`1db57776fab49b7fea3056032c4a20205652fabe6805887979e5541d5feddba2`.

Routed timing passes with WNS +0.119 ns, WHS +0.053 ns, WPWS +1.749 ns,
all total negative slack zero, and zero failing endpoints. The CDC report has
15 Info entries only: 14 `CDC-3` and one `CDC-9`. All four bus-skew checks pass,
with worst slack +6.210 ns. All 11,046 routable nets are fully routed with zero
routing errors. DRC has zero Error or Critical entries and retains 51 advisory
warnings.

The routed design uses 6,169 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
RAMB18 block, and seven DSP blocks.

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

The verified vendor result above covers the current default PAL C64 USB-input
project. Its live-video PL path is unchanged from the separately qualified PAL
live image, but the `2026082B` image has not yet passed USB1 VBUS or HID board
testing. The separate NTSC C64 and GBC vendor builds remain pending. GBC PSRAM
remains disabled and unqualified.

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

Confirm the platform uses that exact XSA. For build `2026082B`, Vitis 2025.2
built both the FSBL and USB-input application from the fresh XSA. The
application has 169,768 bytes of text, 1,932 bytes of data, and 133,568 bytes
of BSS. Bootgen packaged and read back the expected FSBL, routed bitstream, and
two application segments. The 4,351,948-byte `FIRMWARE.BIN` has SHA-256
`e13e1b3df355e38590ebff3c5f496fa10a8b5c1d89bee0066f649aed1caf9425`,
CRC32 `F31417DA`, and 10,262,580 bytes free in the firmware slot.

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

The normal installation path copies this exact `FIRMWARE.BIN` to the SD-card
root. `:reboot` from a running image, or a power cycle, lets golden program and
verify the secondary firmware slot, replace `FIRMWARE.BIN` with
`FIRMWARE.OK`, and reboot into normal auto-boot. Run the ROM uploader only after
the `2026082B` firmware banner appears. The golden UART updater remains a
recovery path, not the normal installation method. Physical USB1 VBUS and HID
input remain unqualified.

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
