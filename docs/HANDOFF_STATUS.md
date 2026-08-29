# Mac-to-Vivado Handoff Status

Status date: 2026-08-29. No Vivado, Vitis, or board result is claimed.

## Completed without vendor tools

- Repository layout, GPL-3.0-only project license, ignore policy, source pins,
  license ledger, and SHA-256 import lock.
- Schematic/source audit for power assumptions, fixed clocks, PS MIO, USB
  roles, DVI straps, DAC address/clock, PSRAM wiring, and Apple-bus isolation.
- Script-generated Zynq-7020 project definition and implementation/XSA report
  flow for Vivado 2025.2.
- Stand-alone top with Apple, Ethernet, and PSRAM pins in inactive states.
- AXI3 register slave, generated ABI, sticky error/IRQ merge behavior, build
  ID, counters, and safe muted reset defaults.
- 1080p60 RGB565 diagnostic pattern and 48 kHz I2S/SPDIF tone source.
- Minimal Vitis 2025.2 workspace and UART diagnostic application.
- Transport-independent AK4493 initialization, TMP102 decode, shell command
  link, and PRG/D64/G64/CRT/ROM validation.
- Destructive PSRAM BIST sequencer, not yet connected to the board PHY.
- Exact 34-file C64 import at the pinned commit, per-file license rows, complete
  applicable license texts, SHA-256 lock, and ROM/media exclusion checks.
- ROM-free FPGA64 machine build list; writable BASIC/KERNAL/character stores;
  C64 adapter, RAM, raw video/audio, PS/2/joystick input bridge, coherent input
  event transport, and reproducible PAL/NTSC clock selection.
- ARM ROM stream commands with bounded backpressure retry and abort, coherent
  XPM clock crossing, strict PL framing, sticky load errors, and a portable
  reset-safe three-ROM boot helper.
- Exact 47-file Game Boy import at the pinned commit, per-file license rows,
  SHA-256 lock, open SameBoy-derived CGB bootstrap source/image, and deterministic
  strict-tool ports for the selected upstream `gb` and `cart` modules.
- GBC interface definitions; GB/GBC header validation; reset-safe cart/bootstrap
  boot helper; gamepad mapping; clock enables; raw RGB/audio adapter; cartridge
  loader; one-line cache; and a one-outstanding common 64-bit memory ABI.
- Tested control/machine memory CDC and PSRAM line service, plus a prepared
  optional GBC PSRAM PHY branch. That branch defaults off and is not accepted
  for hardware use.
- Icarus tests for AXI, registers, video, audio, PSRAM BIST, asynchronous
  mailbox, C64 loader/input, GBC loader/input/clock/cache, memory CDC, and PSRAM
  service; board-shell elaboration for the selected GBC branch; Verilator lint
  for selected C64 and GBC SystemVerilog; strict C and sanitizer tests;
  malformed-content exercise; Tcl completeness; pin coverage; and host
  compilation of the Xilinx entry point.

## Requires vendor tools or hardware

- Mixed VHDL/SystemVerilog C64 elaboration, the inferred writable memories,
  achieved PAL/NTSC clock, XPM constraints, resource use, and timing have not
  been checked by Vivado.
- Mixed VHDL/SystemVerilog GBC elaboration, portable dual-port memory inference,
  achieved 33.554432 MHz clock, common-memory CDC constraints, resource fit,
  and timing have not been checked by Vivado.
- AK4493 I2S output timing: exact receiver setup/hold values are not in the
  publicly accessible product material. The timing gate must remain red.
- PSRAM activation: PHY timing, IDELAYCTRL, calibration, constraints, command
  edge/count tuning, and board measurement are unresolved. The default build
  holds CE high; the prepared GBC branch is explicit opt-in only.
- Physical machine video/audio selection is intentionally absent until
  synthesis and diagnostic outputs pass. USB input, SD file I/O, DDR test, and all
  cold/warm boot claims require a Vitis build and the modified board.

## First vendor-machine sequence

1. Run `make check` and require a clean result.
2. Run `vivado -mode batch -source scripts/create_project.tcl`.
3. Fix only source/Tcl defects. Commit them before assigning a release build ID.
4. Build PAL first, then recreate with
   `MULTITINI_C64_VIDEO_STANDARD=NTSC`; confirm the C64 VHDL/SV compile order,
   inferred memories, achieved selected clock, XPM CDC constraints, and
   resource estimate before implementation.
5. Create a separate GBC project with `MULTITINI_MACHINE=GBC`; keep
   `MULTITINI_ENABLE_GBC_PSRAM=0` for the first elaboration and synthesis review.
   Confirm the selected upstream hierarchy, open bootstrap initialization,
   inferred memories, requested machine clock, and resource estimate.
6. Obtain and record AK4493SEQ timing; add reviewed I2S output delays.
7. Run `vivado -mode batch -source scripts/build_and_export_xsa.tcl`.
8. Review all reports in `build/vivado/reports`; do not rely only on run status.
9. Run `vitis -s scripts/create_vitis_workspace.py` and retain its complete log.
10. Execute the power, pin-isolation, boot, DDR, SD, DVI, audio, and USB gates in
   `MULTITINI_ONE_PLAN.md` and `docs/VIVADO_HANDOFF.md`.

Only after the minimal board image passes should PSRAM be connected and its
BIST exposed. Only after a dedicated PSRAM test passes should a GBC project set
`MULTITINI_ENABLE_GBC_PSRAM=1`. Both machine integrations remain off the
physical outputs until their clock, reset, video, and audio paths pass their
corresponding gates.
