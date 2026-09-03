# Multitini One Handoff Status

Status date: 2026-09-04. The first independent diagnostic image and the second
PAL C64 UART-ROM/frozen-snapshot image have both passed their initial board
tests. The current source replaces the frozen buffer with a four-bank,
four-bit-palette live frame ring. That bridge passes focused XSim, a fresh
routed PAL build in Vivado 2025.2, and the complete first live-video board
procedure. Build `2026082B` also proves USB1 connector power, keyboard input,
and one generic HID gamepad. Build `2026082C` fixed the US symbolic map and
Xbox enumeration; its board test qualified both keyboard maps and Xbox
directional input. Build `2026082D` qualifies the full intended wired Xbox 360
input map plus the player LED command. Build `2026082E` passes automatic SD ROM
and 1541 loading, default read-only D64 mounting, Decathlon gameplay, and
joystick-port swapping on hardware. Current build `2026082F` connects live
machine PCM to I2S and SPDIF. Its first board test was mostly clean, but
Decathlon produced a roughly ten-second scratch/random passage and a later
stuck high tone; Ultima II also produced a stuck tone. Candidate `20260830`
defaulted the C64 to 6581 instead of the prior hardwired 8580, but the
2026-09-02 report says the scratching remains after that latest attempted fix.
Candidate `20260902` fixes a separately reproduced dual-SID result-scheduling
error. The exact build booted on the connected board, and the 2026-09-03 user
retest reports that the scratching/audio bug is fixed.

Standalone `20260903` PAL and NTSC images then completed the strict vendor
flow. The user reports that both C64 cores look okay on the board; this records
a visual smoke test, not complete media or long-run qualification. Build
`2026090E` now includes the ABI 0.6 text overlay, SD package catalog, multicore
menu, guarded full-PL PCAP supervisor, branded standby screen, and
PSRAM-enabled GBC package. PAL, NTSC, and GBC pass strict vendor gates. The
installed board reports the expected build and ABI, switches from C64 to GBC,
validates and loads cartridges, and runs Tomb Raider and V-Rally into gameplay
without the former white-screen lockup.

## Completed repository and portable validation

- Repository layout, GPL-3.0-only project license, ignore policy, source pins,
  license ledger, and SHA-256 import lock.
- Schematic/source audit for power assumptions, fixed clocks, PS MIO, USB
  roles, DVI straps, DAC address/clock, PSRAM wiring, and Apple-bus isolation.
- Script-generated Zynq-7020 project definition and implementation/XSA report
  flow for Vivado 2025.2.
- Stand-alone top with Apple, Ethernet, and PSRAM pins in inactive states.
- AXI3 register slave, generated ABI, sticky error/IRQ merge behavior, build
  ID, counters, and safe muted reset defaults.
- 1080p60 RGB565 diagnostic pattern and a 48 kHz I2S/SPDIF output stage that
  selects coherent live machine PCM or the fixed diagnostic tone.
- Minimal Vitis 2025.2 workspace and UART diagnostic application.
- Transport-independent AK4493 initialization, TMP102 decode, shell command
  link, and generic PRG/D64/G64/CRT/ROM format inspection.
- Destructive PSRAM BIST sequencer, not yet connected to the board PHY.
- Exact 34-file C64 import at the pinned commit, per-file license rows, complete
  applicable license texts, SHA-256 lock, and ROM/media policy checks.
- ROM-free FPGA64 machine build list; writable BASIC/KERNAL/character stores;
  C64 adapter, RAM, raw video/audio, PS/2/joystick input bridge, coherent input
  event transport, and reproducible PAL/NTSC clock selection.
- ARM ROM stream commands with bounded backpressure retry and abort, coherent
  XPM clock crossing, strict PL framing, sticky load errors, and a portable
  reset-safe three-ROM boot helper.
- Source-only PRG/CRT increment: strict portable parsers and a C64 media
  service; a PL stream loader; KERNAL `$FFCF` readiness gating; paused PRG RAM
  and BASIC-pointer writes; `RUN` plus Return injection; and attachment only
  after the PL completes without `CORE_LOAD_ERROR`.
- Conservative read-only CRT support is limited to type IDs `0`, `4`, `5`,
  `7`, `8`, `10`, `13`, `15`, `17`, `18`, `19`, `21`, `22`, `28`, `43`, `57`,
  and `85`: Normal, Simons' BASIC, Ocean type 1, Fun Play, Super Games, Epyx
  FastLoad, Final Cartridge I/II, C64 Game System, Dinamic, Zaxxon, Magic Desk,
  Comal 80, Waterloo/Structured BASIC, Mikro Assembler, Prophet64, RGCD, and
  Magic Desk 2. Exact binary EXROM/GAME profiles, ROM-only CHIP packets, mapper sizes and
  addresses, and contiguous bank layouts are enforced; unsupported writable,
  freezer, peripheral, sparse, and out-of-order profiles fail closed.
- The pinned upstream `cartridge.v` remains untouched. The product source uses
  a separately licensed local compatibility port with the bare `always`
  corrected to `always @*` and two early-referenced net declarations moved
  before use. Its cartridge cache bridges to the common 64-bit memory service
  and latches a miss stall until the response.
- Portable XMODEM-1K/CRC receiver, UART `:c64rx` command, exact 20 KiB
  BASIC/KERNAL/character uploader, target CRC32/ROM validation, and fail-closed
  reset behavior. The selected three-file ROM set is now a tracked test/build
  input and the uploader uses it by default.
- Hardware-proven predecessor: exact 382x270 PAL capture into one RGB565
  block-RAM buffer, 4x centered 1080p output, vblank-only source selection,
  and one intentionally frozen coherent frame.
- Current live source: four 382x270 banks of fixed-palette four-bit indices;
  Gray-coded publish/release ring pointers; exact palette and geometry checks;
  vblank-only acquisition; clean repeat when no new PAL frame is ready;
  whole-frame drop when the ring is full; and fail-closed independent-reset
  rearming. Focused XSim, the fresh PAL Vivado build, and the first live-video
  board qualification all pass.
- Pinned CherryUSB USB1 host integration; cooperative Zynq EHCI service; USB
  boot-keyboard, common HID gamepad, and wired Xbox 360 parsing; default US
  symbolic and retained MiSTer positional maps; C64 key/joystick translation;
  explicit disconnect releases; rollover retention; alias-safe multi-device
  aggregation; retry, reset reassertion, and runtime joystick port swap.
- One ARM-owned FatFs service; canonical C64/1541 ROM paths; bounded read-only
  D64 service; default drive-8 mounting; and UART directory, mount, status, and
  eject commands. Build `2026082E` passes its first SD-to-game board test with
  Decathlon.
- Current source enables exFAT/LFN in `xilffs` 5.5 with generated-config and
  semantic-stamp checks, RTC-backed FAT timestamps, transactional writable
  data-only D64 images, read-only error-tail D64, journal recovery, read-only
  fixed-speed G64, reverse media data, and safe eject/reset/content/reboot
  barriers. The serial shell integrates `:mount`, `:eject`, `:dir`, `:disk`,
  `:diskretry`, `:load`, and `:cartoff`. This batch is included in the
  standalone `20260903` packages; the user reports that PAL and NTSC look okay,
  but no focused writable-media, exFAT, RTC, G64, PRG, or CRT result was
  recorded.
- Current ABI 0.6 source adds a common double-buffered 80x36 overlay and a
  keyboard-driven F12 menu for core variants, C64 disk/cartridge/program
  content, active/test video, keyboard map, joystick port, core reset, and
  golden recovery. The ARM scans strict versioned packages below
  `0:/multitini/cores`, validates size and CRC32, quiesces the active service,
  loads the complete PL through DevCfg/PCAP, probes shell/core/ABI, and rolls
  back to the prior package on failure. C64 and GBC activation services are
  now connected; GBC loads validated `.gb`/`.gbc` cartridges from its package
  content root and remains in reset when no cartridge is attached.
- Build `2026082F` adds dual coherent 49-bit machine-audio crossings, whole-I2S
  frame source switching, a 133.333 MHz SPDIF transmitter with stereo-pair
  capture, reset/load silence, and pop-safe DAC unmute order. Focused source
  and HDL tests cover live PCM, tone, mute, reset, cadence, and parity. The
  fresh PAL Vivado build passes timing, CDC, DRC, routing, bitstream, and XSA
  gates; Vitis and Bootgen also pass. Its first hardware run proves generally
  clean live transport and working `t`/`a` source selection, but the two
  game-specific stuck-tone failures leave SID audio unqualified.
- Candidate build `20260830` changes the adapter's default SID model from 8580
  to 6581. The upstream 8580 noise timeout is about 9.77 seconds, matching the
  duration of Decathlon's scratch/random passage closely enough to make the
  hardwired model an early cause candidate. Focused source checks pass. The
  fresh routed PAL Vivado result has +0.176 ns setup slack, +0.051 ns hold
  slack, zero failed nets, and zero Critical warnings. Vitis, Bootgen packaging,
  and three-partition readback pass. The 4,475,348-byte root `FIRMWARE.BIN` has
  SHA-256
  `99a21c6f929ffe056faabd7d80da9f64bded7d83b8ccf989213ba1fa157aee47`;
  2026-09-02 report says the scratching remains after the latest attempted
  fix, so the model change is not a sufficient repair. The exact live banner
  was not captured in this session.
- Candidate `20260902` repairs the stronger reproduced cause in the shared SID
  pipeline. The first state-15 edge launches the right-channel audio MAC, yet
  the old top level published the preceding filter intermediate on that same
  edge. That one-machine-clock transient recurred once per SID period, and the
  unrelated 48 kHz sampler could stretch a captured transient across a full
  PCM frame. Focused XSim recorded four mono left/right mismatches in 128 clocks
  before the repair and zero after waiting one repeated state-15 edge and then
  publishing both channels together. Its fresh PAL route passes with +0.411 ns
  setup slack, +0.029 ns hold slack, zero routing errors, and no Critical
  warnings or critical CDC findings. Vitis and three-partition Bootgen
  packaging pass. The 4,475,348-byte root `FIRMWARE.BIN` has SHA-256
  `9af37acf16f35c0f784ad98a505d4864c38d187bb84675f81a884b6002ef1b52`;
  golden-UART installation completed, and the rebooted shell reported
  `build=20260902`, C64 core `status=0000014D`, and `error=00000000`. The user
  then reported that the scratching/audio bug is fixed on the connected board.
  The exact game sequence and duration, stuck-tone recurrence, channels, level,
  reset behavior, SPDIF, and long-run stability were not separately reported.
- Source after `20260902` changes diagnostic UART0 from 115200 to the golden
  image's 921600 baud and updates the ROM uploader default to match. UART1
  remains at 115200. The new UART0 setting is absent from the packaged
  `20260902` image and present in the current `20260903` build artifacts; it
  has not been retested on the board.
- Exact 47-file Game Boy import at the pinned commit, per-file license rows,
  SHA-256 lock, open SameBoy-derived CGB bootstrap source/image, and deterministic
  strict-tool ports for the selected upstream `gb` and `cart` modules.
- GBC interface definitions; GB/GBC header validation; reset-safe cart/bootstrap
  boot helper; gamepad mapping; clock enables; raw RGB/audio adapter; cartridge
  loader; one-line cache; and a one-outstanding common 64-bit memory ABI.
- Tested control/machine memory CDC and PSRAM line service, plus the opt-in GBC
  PSRAM PHY branch. The packed two-device address translation keeps logical
  cartridge RAM from aliasing ROM. The PSRAM-enabled `2026090E` branch passes
  vendor gates and its first two cartridge gameplay tests.
- Icarus tests for AXI, registers, video, audio, PSRAM BIST, C64 loader/input,
  GBC loader/input/clock/cache, memory CDC, and PSRAM service; a Vivado XSim
  replay of the final asynchronous-mailbox fix; board-shell elaboration for
  the selected GBC branch; Verilator lint for selected C64 and GBC
  SystemVerilog; strict C and sanitizer tests;
  malformed-content exercise; Tcl completeness; pin coverage; and host
  compilation of the Xilinx entry point. Focused XSim also passes the current
  four-bank live bridge's swap, repeat, full-ring, rejection, diagnostic-toggle,
  and independent-reset cases.
- The full Python repository suite runs 71 checks: 70 pass and one is skipped;
  all 25 strict host C tests pass. Focused XSim/Icarus passes the PRG/CRT content
  loader, RUN injector, cartridge memory bridge, transactional D64 service,
  read-only G64 service, ABI registers, and PAL/NTSC live-video benches. The
  local wait-capable FPGA64 VHDL port analyzes successfully, and the C64
  adapter reaches mixed-language XSim elaboration using `--relax` for exact
  pinned SystemVerilog sources and a simulation-only RGB lookup stub for the
  pinned lookup table's pre-existing strict-XSim case limitation. Coverage also
  includes the common overlay's atomic vblank bank/visibility handoff and the
  complete C64 shell compile. The runtime menu and successful C64-to-GBC PCAP
  path now have initial board validation.

## Completed ABI 0.6 multicore and GBC-fix build

- Fresh source-generated Vivado 2025.2 images for PAL C64, NTSC C64, and
  PSRAM-enabled GBC use shell build ID `2026090E`. All three pass synthesis,
  implementation, bitstream, timing/CDC/DRC/route, and XSA gates with zero
  failing timing endpoints. PAL has +0.244 ns setup and +0.026 ns hold slack;
  NTSC has +0.272/+0.056 ns; GBC has +0.211/+0.055 ns.
- A fresh Vitis 2025.2 workspace built the matching FSBL and supervisor with
  exFAT, LFN, FatFs media validation, RTC timestamps, and missing-card guards.
  Bootgen packaged a 4,550,068-byte `FIRMWARE.BIN` with SHA-256
  `0d84b535791e150e8face4fba6c9a6c6e69cbf86e21767e52f8b877e85f1900d`.
- The installed card preserved seven C64 and seven GBC media files. The live
  board reported ABI 0.6, build `2026090E`, C64 core status `0x0000014D`, and
  no shell error. F12 selected GBC; PCAP loaded the expected 4,045,568-byte
  image with CRC32 `0x11E740AE`; shell probing and activation passed.
- Tomb Raider loaded as MBC5+RAM+battery, 4 MiB, CGB-required and ran into
  gameplay. V-Rally loaded as MBC5, 1 MiB, no cartridge RAM and also ran into
  gameplay. These results cover the reported white-screen transition in two
  MBC5 layouts, but not every mapper or save-persistence path.

## Completed previous standalone PAL, NTSC, and GBC vendor builds

- Clean source-generated Vivado 2025.2 builds completed for PAL C64, NTSC C64,
  and the default-off-PSRAM GBC target. All three completed synthesis,
  implementation, bitstream generation, strict timing/CDC/DRC/route gates, and
  XSA export for `xc7z020clg484-2` with shell build ID `20260903`.
- PAL passes with +0.324 ns setup and +0.036 ns hold slack. NTSC passes with
  +0.327 ns setup and +0.062 ns hold slack. GBC passes with +1.394 ns setup and
  +0.055 ns hold slack. Every target has zero failed routed nets.
- The GBC memories map to block RAM instead of LUT memory. Its former
  LUT-generated CPU/RAM clock was replaced by root-clock enables. A fatal
  implementation gate now rejects any fabric LUT driving a clock pin.
- Vitis 2025.2 built a hardware-matched platform, FSBL, and diagnostic
  application from each XSA. Every BSP enables exFAT and LFN, applies the FatFs
  R0.16 patch-2 media checks, uses the RTC timestamp hook, and bounds missing-SD
  handling. Short `C:\mtv` workspaces avoid the Vitis/Ninja Windows path limit.
- Bootgen packaged explicit PAL, NTSC, and GBC images under
  `build/firmware-current`; three-partition readback confirms the FSBL,
  bitstream, and application in every image. Each is 4,508,116 bytes. Their
  SHA-256 values are
  `f2ce808009a35600b65bd6be796fba7494d796189ecebff7cc256759e803dd8b`,
  `6f77f9ec91e3433c978bbfbd9889cf825570d531021c61b651c9f647f7bc3451`,
  and `0adae823b0674b5858d578db70938d63d3a475032465cad5a89afaf902e16741`,
  respectively.
- These are build/test artifacts from a dirty engineering tree. The user has
  since visually smoke-tested both C64 images and reports that they look okay.
  The GBC image has not been exercised on the board.

## Completed vendor flow for the board-proven PAL live-video baseline

- Fresh default PAL C64 project creation, mixed VHDL/SystemVerilog
  elaboration, synthesis, implementation, bitstream generation, and XSA export
  completed in Vivado 2025.2 for `xc7z020clg484-2`.
- The fresh manifest is `2026-08-29T15:33:38Z`; it records shell build ID
  `20260829`, PAL generic `C64_NTSC_MODE=0`, base commit
  `fd554dbf46e22514bb132f3dc063ee0a892bdcc6`, and a dirty engineering tree.
- Routed timing passes with WNS +0.675 ns, WHS +0.053 ns, WPWS +1.749 ns, and
  zero failing setup, hold, or pulse-width endpoints. The DVI output path group
  passes with setup slack +1.672 ns and hold slack +1.718 ns.
- All 11,033 routable nets are fully routed with zero routing errors. DRC has
  zero errors; its 51 reported checks are warnings for report limits, DSP
  pipelining, asynchronous BRAM controls, and one net with no routable load.
- The final CDC report contains 14 `CDC-3` Info crossings and one `CDC-9` Info
  crossing, with no Warning or Critical entries. The fatal critical-CDC gate
  passed.
- All four bus-skew constraints pass. The two live Gray-pointer crossings have
  actual skews 0.507 ns and 0.610 ns against a 6.733 ns requirement.
- The routed design uses 6,168 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
  RAMB18 block, and seven DSP blocks. The live bridge itself uses 543 LUTs, 335
  flip-flops, and 64 RAMB36 blocks. Total block-RAM use is 85.5 of 140 tiles
  (61.07%), leaving 54.5 tiles.
- `check_timing` found zero no-clock registers, unconstrained internal
  endpoints, missing input delays, or genuinely missing output delays. The
  SPDIF false path and forwarded I2S/DVI clocks are informational exceptions.
- The exact AK4493 datasheet was obtained and reviewed locally. Conservative
  +/-5 ns I2S receiver-window constraints were applied and passed routed
  timing.
- Vitis 2025.2 built the FSBL and diagnostic application from the matching
  `multitini_one.xsa`. Bootgen packaged and read back the expected FSBL, routed
  bitstream, and two application segments.
- The resulting `FIRMWARE.BIN` is 4,241,740 bytes, has SHA-256
  `a44943738448e3114e58c03a30a2210270684ecf849911621e045ae5771f8703`
  and CRC32 `01F764E0`, and leaves 10,372,788 bytes free in the firmware slot.

These are the preserved live-bridge baseline results. The separate board
observations below qualify the corresponding firmware on hardware.

## Completed vendor flow for historical build 2026082A

- A separate PAL C64 build with shell build ID `2026082A` completed synthesis,
  implementation, bitstream generation, and XSA export in Vivado 2025.2.
- The manifest timestamp is `2026-08-29T16:43:18Z`; it records base commit
  `5150d830ddf7f63c4e6eefe9c9e861cccb69e3d8` and the exact dirty engineering
  source snapshot recorded for that build.
- Timing passes with WNS +0.394 ns, WHS +0.057 ns, WPWS +1.749 ns, and zero
  failing endpoints. All 11,048 routable nets are fully routed with zero
  routing errors. DRC has zero errors and 51 retained non-blocking warnings.
- CDC remains 14 `CDC-3` and one `CDC-9` informational crossing, with no
  Warning or Critical entry. All bus-skew constraints pass; the two live-video
  pointer skews are 0.764 ns and 0.777 ns against 6.733 ns.
- The routed design uses 6,172 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
  RAMB18 block, and seven DSP blocks.
- Vitis 2025.2 rebuilt the hardware-matched FSBL and USB-input application.
  Bootgen readback identifies the expected FSBL, bitstream, and two application
  segments. `FIRMWARE.BIN` is 4,346,380 bytes, has SHA-256
  `3e211fd424675753afa9a208b8c817bd12e196cb097951a0e38f2b61b8bab0db`
  and CRC32 `B25E5862`, and leaves 10,268,148 bytes free in the firmware slot.

Build `2026082A` did not qualify a physical keyboard or gamepad. The later
`2026082B` board observations below supersede that boundary for USB1 power,
keyboard transport, and generic HID input.

## Completed vendor flow for build 2026082C

- A fresh default PAL C64 project completed synthesis, implementation,
  bitstream generation, and XSA export in Vivado 2025.2 for
  `xc7z020clg484-2` with shell build ID `2026082C`.
- The initial manifest is UTC `2026-08-30T03:27:47Z`; it records base commit
  `ee71f5dd5a77e8a15c9881e2a07d3ee48c9c9c77` and an intentional dirty
  engineering tree.
- Timing passes with WNS +0.599 ns, WHS +0.036 ns, WPWS +1.749 ns, and zero
  failing endpoints. All 11,035 routable nets are fully routed. DRC has zero
  errors or critical warnings and retains 51 advisory warnings.
- CDC contains 14 `CDC-3` and one `CDC-9` Info-only crossing. All four bus-skew
  constraints pass; the live-pointer skews are 0.476 ns and 0.575 ns against
  6.733 ns.
- The routed design uses 6,167 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
  RAMB18 block, and seven DSP blocks.
- The routed bitstream SHA-256 is
  `e0b6df0a38dcbd1c4a6f31e2274d4c74e62cf77489f9332bd36a276a9c650e4b`.
  The matching XSA SHA-256 is
  `530322983caa359baedcd1cbc50b990b0657862c3c6cf545ba91f9f86289158c`.
- Vitis 2025.2 rebuilt the matching FSBL and application. The application uses
  179,856 bytes of text, 1,932 bytes of data, and 141,760 bytes of BSS. Its
  linked USB registry retains hub, HID, and Xbox class records, and the Xbox
  run/stop callbacks are strong definitions.
- Bootgen readback identifies the expected FSBL, bitstream, and two application
  segments. The root `FIRMWARE.BIN` is 4,362,060 bytes, has SHA-256
  `ae75318d458c6f6203c5ed6a87be1283ace6b39dacf0307c7e82c52e281ea004`
  and CRC32 `EF6EB912`, and leaves 10,252,468 bytes free in the firmware slot.

This build fixed the US symbolic-keymap behavior and the CherryUSB endpoint
capacity failure in source. Its later board test qualified both keyboard maps,
Xbox enumeration and report transport, and D-pad/left-stick directions, but A
did not assert Fire and the player ring kept blinking.

## Completed vendor flow for build 2026082D

- Vivado 2025.2 completed the fresh PAL C64 synthesis, implementation,
  bitstream, and XSA flow with shell build ID `2026082D`.
- The manifest is UTC `2026-08-30T04:33:20Z`; it records base commit
  `765aee1fbf54c330c7fdab1543da38c0b577b277` and an intentional dirty
  engineering tree.
- Timing passes with WNS +0.525 ns, WHS +0.040 ns, WPWS +1.749 ns, and zero
  failing endpoints.
- All four bus-skew checks pass at +6.213 ns, +6.194 ns, +125.927 ns, and
  +125.693 ns. The routed design uses 6,170 LUTs, 5,703 flip-flops, 85 RAMB36
  blocks, one RAMB18 block, and seven DSP blocks; the live bridge uses 546
  LUTs.
- Vitis 2025.2 rebuilt the matching FSBL and diagnostic application. Bootgen
  packaging and readback passed.
- The root `FIRMWARE.BIN` is 4,365,516 bytes, has SHA-256
  `536b68ff99f53925d7825d70d7ee30bb586edb8cb301c1e5feff1607248a6600`
  and CRC32 `0680EEAB`, and leaves 10,249,012 bytes free in the firmware slot.

## Completed first board observations

- The golden UART updater loaded and booted the first PAL diagnostic image.
- The monitor locked to the 1080p DVI color bars; the final half-width grey bar
  matches the diagnostic generator's known grouping.
- `v` disabled and restored DVI, `a` produced the fixed tone, and `:reboot`
  returned to golden.
- `r` had no visible effect because that image held the C64 in reset and its
  bars/tone were independent of machine reset.

## Completed PAL C64 snapshot board observations

- The golden UART updater loaded and booted the preserved snapshot firmware.
- The UART uploader transferred the exact 20 KiB BASIC/KERNAL/character bundle,
  target validation succeeded, reset released, and the C64 reached its PAL
  ready screen.
- DVI showed a coherent centered frozen machine frame. Switching between bars
  and the machine frame worked as intended.
- `:reboot` returned to the golden image.
- This result proves the ROM transport, loader/reset-release path, C64 boot,
  exact-frame capture, and frozen display. It does not prove live refresh,
  keyboard, joystick, storage, or SID audio.

## Completed PAL C64 live-video board observations

- The golden UART updater loaded and booted the exact packaged live firmware.
- The bundled ROM uploader completed, and `v` displayed a live C64 BASIC
  screen with a continuously blinking cursor.
- The five-minute display soak remained locked and tear-free.
- Repeated `v` source changes were clean whole-frame transitions.
- `r` reset/fallback/rearm behavior passed, and the live machine returned.
- `:reboot` returned to the golden image.
- This qualifies the PAL live-frame path on the tested board/display
  combination. It does not qualify physical input, storage, SID audio, NTSC,
  or extended environmental operation.

## Completed build 2026082B USB board observations

- Golden loaded the root firmware, renamed it `FIRMWARE.OK`, and booted it.
- USB1 powered a keyboard; the keyboard enumerated, delivered reports, and
  controlled the live C64 BASIC screen.
- Generic low-speed HID gamepad `06a3:0107` enumerated and delivered joystick
  input. This proves the common HID path on the tested board.
- The keyboard symbol map failed its US-layout check: `Shift+=` was a shifted
  graphics plus rather than arithmetic plus, so `PRINT 2+2` evaluated as `22`.
- Xbox 360 controller `045e:028e` failed enumeration with
  `Endpoint num 4 overflow`; no Xbox input was delivered.
- These results qualify USB1 power, keyboard transport, and generic HID input,
  but not the new `2026082C` symbolic map or Xbox path.

## Completed builds 2026082C and 2026082D USB board observations

- Build `2026082C` qualified both the US symbolic and retained MiSTer
  positional keyboard maps on the tested keyboard.
- The same build proved that Xbox controller `045e:028e` enumerated, bound its
  input interface, delivered reports, and drove D-pad and left-stick
  directions. A/Fire and the blinking player ring remained open.
- Build `2026082D` sent the player-1 LED command successfully and exposed
  per-XInput report, state, and raw-packet diagnostics.
- Its final line reported `reports=839`, `state=10`, `seen=7F`, `changes=86`,
  `led=1`, one output attempt, and zero per-XInput submit, transfer, or output
  errors. The final raw prefix was `0014001000002EFB6B00`.
- On the live C64, A/Fire produced masked joystick value `15`, B produced
  Space, and holding X while pressing a number changed the cursor color,
  proving Control end to end. X alone remained nonprinting as expected for a
  modifier. All seven intended state bits were observed.
- These results qualify the wired Xbox 360 functional input map and player LED
  command on the tested controller.
- A follow-up test passed hotplug after boot, disconnect while held with state
  release, reconnect, and runtime joystick-port swap on that controller and
  USB topology.
- With port 1 selected, A/Fire and B/Space both appeared as Space, X remained
  Control, and D-pad motion produced the left-arrow glyph, `2`, and other
  matrix-dependent characters at BASIC. This is expected because C64 joystick
  port 1 shares CIA keyboard-matrix lines, and it confirms the swap end to end.

## Still requires vendor tools or hardware

- Current C64 CRT source connects the common memory service to the board PSRAM
  path. The shared PHY now has timing, CDC/DRC/route, and initial GBC cartridge
  gameplay evidence, but destructive BIST, broad address/data stress, C64 CRT
  use, and electrical board measurement remain open. GBC remains an explicit
  opt-in build setting.
- The current C64 storage/content increment has PAL and NTSC Vivado, Vitis,
  packaged-image, and visual smoke results, but still needs focused board
  validation. The ABI 0.6 multicore menu and PCAP supervisor have a
  consolidated vendor build and successful C64-to-GBC board switch; rollback,
  repeated bidirectional switching, and the full media matrix remain open.
- Build `2026082F` routes audible live machine PCM and proves source switching,
  but its Decathlon and Ultima II faults leave SID compatibility, long-run
  behavior, level, channel behavior, reset behavior, and SPDIF open. The
  reported `20260830` retest still scratched. The connected-board `20260902`
  retest reports that the scratching/audio bug is fixed after the dual-SID
  scheduling repair; broader game-specific and long-run coverage remains open.
- Initial golden-slot boot, DVI timing, fixed-tone audio, USB1 power, keyboard
  transport, generic HID input, and golden reboot have observations.
  Pin-isolation measurements, DDR, the remaining non-Xbox USB regressions,
  temperature, cold/warm cycles, and the long-run DVI/audio gates remain open.

## Next validation sequence

1. Extend the `20260902` audio pass with recorded Decathlon opening/gameplay and
   Ultima II sequences. Recheck UART `m`, `t`, and `a`, reset/load silence,
   channels, level, and SPDIF. Record whether either stuck tone returns before
   calling the full live SID audio path qualified.
2. Continue the installed ABI 0.6 package test with PAL/NTSC switching in both
   directions, CRC/ABI rejection, failed-load rollback, repeated switching,
   reset, and golden recovery. F12 rendering and one C64-to-GBC transition
   already pass.
3. Exercise BASIC and non-BASIC PRGs, RUN typing,
   each accepted CRT mapper/profile, malformed and unsupported CRT rejection,
   cartridge cache misses, D64/G64 mount/eject/remount, reset/detach, and PSRAM
   fault handling through the menu.
4. Extend the passing `MULTITINI_ENABLE_GBC_PSRAM=1` Tomb Raider and V-Rally
   runs across the remaining MBC1/MBC5 cartridge samples, save-data behavior,
   video, audio, keyboard/gamepad input, detach, reset, and repeated C64/GBC
   switching on the board.
5. Execute the remaining boot, DDR, input, and machine gates in
   `MULTITINI_ONE_PLAN.md` and `docs/VIVADO_HANDOFF.md`.

The checked-in C64 CRT source now connects PSRAM, but it is not releasable until
the dedicated PSRAM and cartridge tests pass in the vendor build and on the
board. Only a dedicated candidate build should set
`MULTITINI_ENABLE_GBC_PSRAM=1` while the remaining PSRAM and cartridge matrix
is incomplete. GBC now has initial physical video and cartridge gameplay
evidence but remains incompletely qualified. C64 DVI
must pass the live gate above. C64 live audio remains unqualified until the
remaining `20260902` long-run, game-specific, channel, level, reset, and SPDIF
checks pass.
