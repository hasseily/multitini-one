# Multitini One

Multitini One turns the Appletini ONE card into a stand-alone FPGA system.
The first product target is a C64 built from the GPL MiSTer C64 core. Game Boy
Color is the second adapter used to prove that the shell is reusable. Neither
machine uses an Apple II host.

The independent PAL DVI timing, fixed-tone audio, UART control, and golden
reboot paths have passed their first board test. The frozen-frame image proved
UART ROM upload, reset release, C64 boot, and coherent scaling. The preserved
four-bank live PAL baseline also passed its board gate, and later images proved
USB keyboard/gamepad input and the wired Xbox 360 path. Build `2026082E` now
also passes the SD ROM, 1541, and read-only D64 path on hardware: Decathlon
loads and runs from drive 8, with UART `j` selecting its joystick port 1.
Build `2026082F` routes live machine PCM to the physical audio outputs, but its
first board test found mostly clean SID audio plus repeatable source-side
faults: Decathlon produced about ten seconds of scratch/random noise near the
end of its opening theme and later a stuck high tone; Ultima II also produced a
stuck tone. Candidate build `20260830` changed the C64 default from the
previously hardwired 8580 SID model to 6581, but the 2026-09-02 report says the
same scratching remains. Candidate `20260902` instead fixes a reproduced
dual-SID pipeline error that briefly published a filter intermediate as the
right channel. The exact build was installed on the connected board, and the
2026-09-03 user retest reports that the scratching/audio bug is fixed. Build
`2026090E` adds the ABI 0.6 multicore supervisor, F12 menu, branded standby
screen, PSRAM-enabled GBC image, and packed two-device PSRAM address mapping.
Its PAL, NTSC, and GBC images pass the strict vendor gates. On 2026-09-04 the
connected board reported the expected build and ABI, switched from C64 to GBC
through PCAP, loaded cartridges through the menu, and ran Tomb Raider and
V-Rally beyond their title screens without the former white-screen lockup.
ABI 0.7 build `2026090F` adds journaled GBC battery-RAM persistence. Fresh PAL,
NTSC, and PSRAM-enabled GBC images pass the strict vendor gates, and Vitis
2025.2 built the matching exFAT-enabled supervisor. After installing the staged
image, the user confirmed GBC game-save persistence on the board.

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
- Current source configures diagnostic UART0 at the golden-compatible 921600
  baud; UART1 remains at 115200. The standalone `20260903` builds include that
  setting. The older packaged `20260902` image remains a 115200-baud historical
  artifact.
- A fail-closed dual-clock live bridge stores the fixed C64 palette as four-bit
  indices in four 382x270 frame banks. Complete PAL frames enter a Gray-pointer
  ring; the 1080p60 side changes display banks only at vertical blank. A missing
  PAL frame is repeated cleanly, and a full ring drops a whole incoming frame
  before capture rather than exposing a partial frame.
- The 47-file Game Boy allowlist, open SameBoy-derived CGB bootstrap, generated
  strict-tool compatibility ports, GB/GBC content validation, cartridge boot
  helper, adapter, one-line cartridge cache, and shared external-memory ABI are
  implemented and provenance locked.
- The board shell still selects one machine at project creation, but the
  ARM-resident multicore supervisor discovers strict
  SD core packages, safely replaces the complete PL through DevCfg/PCAP,
  validates the new shell/core/ABI, and reloads the previous package on
  failure. This is full PL reconfiguration, not partial reconfiguration.
- Every current ABI 0.7 image includes the same double-buffered 80x36 overlay.
  The F12
  menu selects core variants, machine-specific media, active or diagnostic
  video, input settings, core reset, and golden recovery. C64 and GBC both
  have activation services; GBC accepts `.gb` and `.gbc` cartridges. See
  [`docs/MULTICORE_RUNTIME.md`](docs/MULTICORE_RUNTIME.md).
- GBC video crosses into a four-bank RGB565 frame ring and scales 160x144 by
  6x into the common 1080p output. The PSRAM-enabled `2026090E` image passes
  timing/CDC/DRC/route gates and its first cartridge board tests. ABI 0.7 build
  `2026090F` adds journaled battery-RAM `.sav` files, automatic quiet-time
  commits, and fail-closed detach/reset/switch/reboot barriers. Its fresh PAL,
  NTSC, and PSRAM-enabled GBC images pass the full Vivado gates at
  +0.308/+0.054 ns, +0.192/+0.057 ns, and +0.264/+0.056 ns setup/hold slack.
  Portable tests and the matching Vitis build pass. The staged image was
  installed and GBC game-save persistence was confirmed on the board; RTC and
  broader mapper/input/audio qualification remain open.
- The AXI register path, DVI timing generator, diagnostic audio path, PSRAM
  BIST, and C64 ROM loader/input bridges pass HDL simulations. Build
  `2026082F` adds coherent live stereo PCM crossings for I2S and SPDIF while
  retaining the fixed diagnostic tone. The asynchronous mailbox,
  frozen-frame predecessor, and four-bank live C64 bridge pass focused Vivado
  XSim, and the selected SystemVerilog C64 path passes Verilator lint.
- Build `2026082F` completed synthesis, implementation, bitstream generation,
  and XSA export in Vivado 2025.2. Routed timing passed with WNS +0.252 ns,
  WHS +0.059 ns, WPWS +1.749 ns, no failing endpoints, zero failed nets, and
  zero Critical CDC findings.
- The `2026082F` routed design uses 8,214 LUTs, 8,536 flip-flops, 91 RAMB36 blocks,
  two RAMB18 blocks, and seven DSP blocks. The audio stage accounts for 300
  LUTs and 766 flip-flops; the four-bank video bridge accounts for 544 LUTs,
  335 flip-flops, and 64 RAMB36 blocks.
- Vitis 2025.2 built the matching FSBL and diagnostic application from the
  fresh XSA. Bootgen created and read back the expected FSBL, routed bitstream,
  and application partitions. The resulting 4,475,348-byte root
  `FIRMWARE.BIN` has SHA-256
  `24c8ca616e939ddd13ae46807bcd315affcd6a56f6836c14af4c7cf70510903c`.
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
  on the tested controller. Hotplug after boot, disconnect while held with
  state release, reconnect, and runtime joystick-port swapping also passed on
  that controller and USB topology.
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
- Build `2026082E` adds a single-owner FatFs service,
  canonical C64 ROM autoload paths, optional 1541 ROM loading, and a read-only
  drive-8 D64 service. Host tests, the matching Vivado/Vitis/Bootgen build, and
  the first SD-card board test pass. Decathlon loads and runs, and `j` selects
  the game's required joystick port 1. See
  [`docs/C64_SD_STORAGE.md`](docs/C64_SD_STORAGE.md) for the exact card layout,
  startup behavior, UART commands, and serial fallback.
- The later storage/content batch extends that historical baseline. The Vitis
  generator enables exFAT in
  Xilinx `xilffs` 5.5, keeps every generated/exported configuration copy in
  sync, carries the upstream FatFs media-validation guards, and connects
  FatFs timestamps to the board PCF8563 RTC with a fixed-date fallback until a
  valid clock reading is available. Normal data-only D64 images are writable;
  every accepted write is persisted through a checksummed write-ahead journal
  before the FPGA receives success. A leftover complete journal is replayed
  when that D64 is mounted again. D64 images with error-info tails remain
  read-only. G64 version-zero images are read-only and support fixed speed
  zones 0 through 3; variable per-byte speed maps are rejected explicitly.
  Eject, core reset, content changes, and reboot all use the same
  flush/eject barrier and refuse to discard a dirty image on failure. UART
  exposes `:mount`, `:eject`, `:dir`, `:disk`, and `:diskretry` for this path.
  The standalone PAL and NTSC `20260903` builds that include the storage batch
  were subsequently reported by the user to look okay on the board. That is a
  visual smoke result, not complete writable-media, exFAT, RTC, or content
  qualification.
- Build `2026082F` connects the selected machine's signed 24-bit PCM stream to
  the common I2S and SPDIF outputs. Live audio is selected automatically when
  the core is ready; UART `a` restores live audio, `t` selects the fixed test
  tone, and `m` mutes either source. Source checks, focused XSim, and the fresh
  routed vendor reports cover stereo coherence, mute, source changes, reset
  silence, timing, and CDC. Its first board test was mostly clean, but
  Decathlon produced a roughly ten-second scratch/random passage and a later
  stuck high tone; Ultima II also produced a stuck tone. Selecting the fixed
  tone with `t` masked the fault and `a` restored it, localizing it to live
  machine audio rather than the shared physical transport.
- Candidate build `20260830` defaults both C64 SID instances to 6581 instead of
  the `2026082F` hardwired 8580 model. The upstream 8580 noise timeout is about
  9.77 seconds at its nominal SID rate, which made the model mismatch an early
  cause candidate. Its fresh routed
  PAL Vivado result passes with +0.176 ns setup slack, +0.051 ns hold slack,
  zero failed nets, and zero Critical warnings. Vitis and Bootgen also pass;
  the 4,475,348-byte root `FIRMWARE.BIN` has SHA-256
  `99a21c6f929ffe056faabd7d80da9f64bded7d83b8ccf989213ba1fa157aee47`.
  The 2026-09-02 report that the scratching remains after the latest attempted
  fix rejects the SID-model change as a sufficient repair; the exact live
  banner was not captured in this session.
- Candidate `20260902` fixes the stronger reproduced cause. The shared SID
  filter launches its right-channel audio MAC on the first state-15 edge, but
  the old logic published `faudio` on that same edge while it still held a
  filter intermediate. That corrupt value existed for one roughly 31.7 ns
  machine clock per roughly 1.0 us SID period, giving the unrelated 48 kHz
  sampler about a 3.1% chance per sample to stretch a bad value over a complete
  PCM frame. Focused XSim saw four mono left/right mismatches in 128 clocks
  before the fix and none after coherent publication was delayed one clock.
  Its fresh PAL route passes with +0.411 ns setup slack, +0.029 ns hold slack,
  zero routing errors, and no Critical warnings or critical CDC findings.
  Vitis and three-partition Bootgen packaging pass. The 4,475,348-byte root
  `FIRMWARE.BIN` has SHA-256
  `9af37acf16f35c0f784ad98a505d4864c38d187bb84675f81a884b6002ef1b52`.
  Golden-UART installation completed, and the rebooted shell reported
  `build=20260902`, C64 core `status=0000014D`, and `error=00000000`. The user
  then reported that the scratching/audio bug is fixed on the connected board.
  The exact game sequence, duration, stuck-tone recurrence, channel and level
  checks, reset behavior, and SPDIF were not separately reported.

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
- Fresh ABI 0.6 PAL, NTSC, and PSRAM-enabled GBC `2026090E` builds pass the
  strict vendor gates at +0.244/+0.026 ns, +0.272/+0.056 ns, and
  +0.211/+0.055 ns setup/hold respectively. The consolidated supervisor and
  packages are installed on the board. F12, C64-to-GBC PCAP loading, package
  CRC validation, GBC cartridge loading, Tomb Raider gameplay, and V-Rally
  gameplay pass. Failure rollback and repeated bidirectional switching still
  need focused board tests.
- SD file-system, C64/1541 ROM autoload, default drive-8 mounting, and read-only
  D64 service pass on the board in build `2026082E`; Decathlon loads and runs.
  USB1 power, both keyboard maps, generic
  HID gamepad input, and the `2026082D` wired Xbox 360 input map and player LED
  command are board-proven. The tested Xbox controller also passes hotplug,
  held-disconnect release, reconnect, and joystick-port swap. Build `2026082E`
  loads the tracked C64 ROM set from SD; UART upload remains a fallback. The
  pinned open GBC bootstrap remains the only ROM embedded in an FPGA image.
- Writable D64, journal recovery, exFAT, RTC-backed FAT timestamps, G64 reads,
  and the media retry/eject/reset barriers are present in the standalone
  `20260903` images, but the user report is only that both C64 images look
  okay. The detailed `2026082E` storage run remains the most recent evidence
  for actual disk loading and covers read-only D64 only.
- DVI timing and the fixed diagnostic tone have initial board observations.
  Build `2026082F` proves that live SID audio is generally clean but does not
  pass qualification because of the Decathlon and Ultima II stuck-tone faults.
  The reported `20260830` retest still scratched. The connected-board retest of
  `20260902` reports that the scratching/audio bug is fixed; broader long-run,
  game-specific, channel, level, reset, and SPDIF coverage remains open.
  The long-run audio gate, remaining non-Xbox USB hotplug checks, DDR,
  destructive PSRAM testing, and Apple-bus isolation still require the hardware work in
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
$env:MULTITINI_BUILD_ID = '2026090F'
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
The runtime package layout, menu controls, and post-build packaging command are
in [`docs/MULTICORE_RUNTIME.md`](docs/MULTICORE_RUNTIME.md).

## Licensing

Original Multitini work is licensed under GPL-3.0-only. Imported files retain
their own notices and licenses. See `licenses/README.md` and
`licenses/files.csv`. No ROM or media rights are granted by an HDL license.
