# Vivado and Vitis Handoff

Status: commands and review gates prepared; none has run yet.

Known handoff blocker: the public AK4493SEQ page identifies the controlled
datasheet but gates the document behind a request form. `audio_i2s_lrck` and
`audio_i2s_sdata` therefore have no guessed output-delay values. The timing
coverage check is expected to reject them until the exact receiver setup/hold
requirements and board skew are recorded. Do not suppress that failure.

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
- all source and constraint files came from the tracked manifests; and
- the block design validates.

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
- C64 main RAM and writable ROM inference, port mode, and read latency;
- XPM handshake constraints, the error-clear toggle synchronizer, and every
  control-to-C64 CDC path;
- every false path and max-delay object count;
- setup, hold, pulse-width, and bus-skew reports;
- Apple-bus pin constants after optimization;
- DVI source-synchronous output timing;
- audio clock crossings;
- PSRAM I/O placement and capture rules; and
- LUT, register, BRAM, DSP, BUFG, MMCM/PLL, and I/O use.

Save the exact reports and build manifest. A clean build is not hardware proof.

The C64 adapter is marked `DONT_TOUCH` and its raw video/audio outputs are kept
so synthesis and timing cannot silently remove the integration. The physical
DVI and audio pins still use the independent diagnostic generators. Confirm
that the C64 hierarchy is present and timed, but do not interpret this image as
a C64 display/audio test.

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

The imported PSRAM PHY and simulated BIST are present as staged source but are
not connected in `multitini_one_top`. Keep PSRAM inactive for the first board
image. Its command timing, input capture edge/tap, IDELAYCTRL, and external
constraints are a separate Phase 3 review.

## Vitis

After a successful XSA export:

```sh
vitis -s scripts/create_vitis_workspace.py
```

Confirm the platform uses that exact XSA. Build the diagnostic firmware with a
matching build ID. Do not enable writable media until the read-only path and
power-loss rules are tested.

The diagnostic starts with the core held in reset and both the shell and DAC
muted. It probes the ABI, logs the TMP102 temperature when I2C works, initializes
the DAC without unmuting, and accepts UART commands for status, ping, video,
audio mute/unmute, and core reset. Portable firmware functions can validate and
stream caller-supplied BASIC, KERNAL, and character ROM buffers, but SD file
loading and a UART file-transfer command are not wired into the diagnostic.

## First board program

Use JTAG and a current-limited supply. Before release from reset, verify:

- the stand-alone power jumper and connector;
- all Apple main transceiver OE pins are high;
- Apple direction pins and auxiliary OE are low;
- Apple address, data, R/W, and control lanes are high impedance;
- PSRAM CE is high;
- W5100S CS and read/write strobes are high; and
- audio output is muted.

Then execute the Phase 1 and Phase 2 gates in `MULTITINI_ONE_PLAN.md`. Record
board revision, jumper details, display, keyboard, gamepad, SD card, supply,
ambient temperature, bitstream build ID, firmware build ID, and UART log.
