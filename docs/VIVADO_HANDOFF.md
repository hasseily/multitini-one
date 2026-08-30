# Vivado and Vitis Handoff

Status: the diagnostic, PAL C64 frozen-snapshot, and PAL live-video images have
passed their recorded board procedures. Build `2026082B` also proves physical
USB1 VBUS, keyboard transport, and one generic HID gamepad. Build `2026082C`
qualified both keyboard maps and the initial wired Xbox 360 report/directional
path. Build `2026082D` passed Vivado, Vitis, Bootgen, and the board test for the
full intended Xbox input map and player LED command. Build `2026082E` adds the
matched SD ROM/1541/read-only-D64 image and passes its first board test with
Decathlon. Build `2026082F` connects live machine PCM to I2S and SPDIF and
passes its fresh PAL vendor/package gates; audible board testing remains open.

The last fully detailed USB baseline used Vivado 2025.2, part
`xc7z020clg484-2`, shell and firmware build ID `2026082D`, PAL generic
`C64_NTSC_MODE=0`. Its manifest is UTC `2026-08-30T04:33:20Z` and records base
commit `765aee1fbf54c330c7fdab1543da38c0b577b277` plus an intentional dirty
engineering tree.

Routed timing passes with WNS +0.525 ns, WHS +0.040 ns, WPWS +1.749 ns,
all total negative slack zero, and zero failing endpoints. The CDC report has
15 Info entries only: 14 `CDC-3` and one `CDC-9`. All four bus-skew checks pass
at +6.213 ns, +6.194 ns, +125.927 ns, and +125.693 ns. All 11,035 routable nets
are fully routed with zero routing errors. DRC has zero Error or Critical
entries and retains 51 advisory warnings.

The routed design uses 6,170 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
RAMB18 block, and seven DSP blocks. The four-bank live video bridge uses 546
LUTs, 335 flip-flops, and 64 RAMB36 blocks.

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
MULTITINI_BUILD_ID=2026082D \
  vivado -mode batch -source scripts/create_project.tcl
```

Use the eight-digit firmware build ID from `firmware/platform/xilinx/main.c`.
Without this override the project script uses the current Git short hash, so
the shell and firmware banners would not match.

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

The verified vendor result above covers the recorded PAL C64 USB-input
project. Its live-video PL path is unchanged from the separately qualified PAL
live image. Build `2026082B` proves USB1 VBUS and generic HID input, build
`2026082C` qualifies both keyboard maps, and build `2026082D` qualifies the
wired Xbox 360 functional map and player LED command on the tested controller.
The matching `2026082E` vendor image additionally passes the SD ROM, 1541, and
read-only D64 board path. The `2026082F` live-audio reports pass with WNS
+0.252 ns, WHS +0.059 ns, WPWS +1.749 ns, zero failed nets, and no Critical CDC
findings. The separate NTSC C64 and GBC vendor builds remain pending. GBC PSRAM
remains disabled and unqualified.

The selected machine adapter is marked `DONT_TOUCH` so synthesis and timing
cannot silently remove the integration. Its registered PAL RGB output feeds an
exact-frame bridge that recognizes the fixed 16-color palette and stores
four-bit indices in four frame banks. Completed-frame and released-frame
pointers cross through `xpm_cdc_gray`. The 1080p60 side acquires at most one
approximately 50.123 Hz PAL frame at each vertical blank; if none is ready, it
repeats the current bank. Diagnostic/machine source selection also changes only
at vertical blank. A full ring drops an entire incoming source frame before
capture. Build `2026082F` also sends the selected machine's coherent 24-bit PCM
through separate handshakes to the I2S BCLK and SPDIF control domains. The fixed
tone remains selectable, and reset or loss of readiness selects silence without
stopping the physical clocks. Source, focused XSim, and routed vendor checks
pass; the audible board procedure remains open.

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

Confirm the platform uses that exact XSA. For historical build `2026082C`,
Vitis 2025.2
built both the FSBL and USB-input application from the fresh XSA. The
application has 179,856 bytes of text, 1,932 bytes of data, and 141,760 bytes
of BSS. Bootgen packaged and read back the expected FSBL, routed bitstream, and
two application segments. The linked USB class registry retains hub, HID, and
Xbox records, and the Xbox run/stop callbacks are strong definitions. The
4,362,060-byte `FIRMWARE.BIN` has SHA-256
`ae75318d458c6f6203c5ed6a87be1283ace6b39dacf0307c7e82c52e281ea004`,
CRC32 `EF6EB912`, and 10,252,468 bytes free in the firmware slot.

For build `2026082D`, Vitis 2025.2 rebuilt the matching FSBL and
diagnostic application. Bootgen packaged and read back the expected FSBL,
routed bitstream, and two application segments. The 4,365,516-byte root
`FIRMWARE.BIN` has SHA-256
`536b68ff99f53925d7825d70d7ee30bb586edb8cb301c1e5feff1607248a6600`,
CRC32 `0680EEAB`, and 10,249,012 bytes free in the firmware slot.

For build `2026082E`, Vitis 2025.2 rebuilt the matching FSBL and application,
and Bootgen readback verified the expected FSBL, routed bitstream, and
application partitions. The 4,475,348-byte root `FIRMWARE.BIN` has SHA-256
`079257d25af73abfc6c3f9372771660934942d9009655e5684bc78952d4cd118`.
That image passes the user-reported SD ROM, 1541, default read-only D64, and
Decathlon board test.

The historical USB-input application starts with the core held in reset and
the DAC muted. It probes the ABI, logs the TMP102 temperature when I2C works,
initializes the DAC without unmuting, and accepts UART commands for status,
ping, video, audio mute/unmute, core reset, and reboot to the golden image.
`:c64rx` receives an exact 20 KiB
XMODEM-1K/CRC bundle from `scripts/serial_c64_rom_upload.py`, verifies the outer
CRC32 and three ROM slices, streams them, and releases reset only after the core
reports ready. The uploader defaults to the repository's selected three-file
ROM set and retains explicit path overrides. SD loading is absent from that
historical application.
The portable GBC helper can stream a validated cartridge and optional bootstrap
override; it is likewise not connected to SD or the diagnostic CLI.

Build `2026082F` initializes the DAC muted, selects live PCM in the PL, waits at
least two complete 48 kHz frames, and then releases DAC soft mute when the core
is ready. UART `a` selects and unmutes live machine audio, `t` selects and
unmutes the fixed tone, and `m` mutes the selected source. Vivado 2025.2 passes
with WNS +0.252 ns, WHS +0.059 ns, WPWS +1.749 ns, no failing endpoints, zero
failed nets, and a CDC summary of 24 `CDC-3` Info plus one `CDC-9` Info. Vitis
and Bootgen produce a 4,475,348-byte root `FIRMWARE.BIN` with SHA-256
`24c8ca616e939ddd13ae46807bcd315affcd6a56f6836c14af4c7cf70510903c`.
These results do not constitute an audible board pass.

The normal installation path copies the matching root `FIRMWARE.BIN` to the
SD-card root. `:reboot` from a running image, or a power cycle, lets golden program and
verify the secondary firmware slot, replace `FIRMWARE.BIN` with
`FIRMWARE.OK`, and reboot into normal auto-boot. With `2026082E` or later, use
the canonical `/cores/c64` SD layout; the UART ROM uploader remains a fallback.
The golden UART updater is a recovery path, not the normal installation method.
Physical USB1 VBUS and
generic HID input are proven by `2026082B`, both keyboard maps by `2026082C`,
and the Xbox functional input map and player LED command by `2026082D`.

## Board qualification and next gates

The independent diagnostics, ROM-upload/frozen-frame image, and exact
live-video image have passed on the board. The live test produced a blinking
BASIC cursor, a stable tear-free five-minute soak, clean whole-frame `v`
switches, correct `r` reset/rearm behavior, and successful `:reboot` to golden.
Preserve that firmware and report set as the PAL video baseline.

The follow-up Xbox test passed hotplug after boot, disconnect while held with
state release, reconnect, and runtime joystick-port swap on controller
`045e:028e` and the tested USB topology. With port 1 selected, its expected C64
keyboard-matrix side effects appeared: A/Fire and B/Space both produced Space,
X remained Control, and the D-pad produced the left-arrow glyph, `2`, and other
matrix-dependent characters. This confirms port-1 delivery rather than an
Xbox remap. The focused Xbox checklist is complete for that controller.

The `2026082E` follow-up board test loaded the C64 and 1541 ROMs from
`/cores/c64/roms`, mounted `/cores/c64/media/drive8.d64` read-only, and loaded
and ran Decathlon from drive 8. UART `j`
selected the game's required C64 joystick port 1. This qualifies the first
SD-to-game path on the tested card and media, not writes or other formats.

The `2026082F` live-audio image still needs its board procedure. Confirm
automatic SID output, quiet startup, `m` mute, `t` fixed tone, `a` live restore,
silence through `r` reset and ROM/media work, channel behavior, level, and
SPDIF before marking it audible or hardware-qualified.

Retain the board safety checks:

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
