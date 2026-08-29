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
- Icarus tests for AXI, registers, video, audio, PSRAM BIST, asynchronous
  mailbox, and C64 loader/input; Verilator lint for the selected C64
  SystemVerilog; strict C and sanitizer tests; malformed-content exercise; Tcl
  completeness; pin coverage; and host compilation of the Xilinx entry point.

## Requires vendor tools or hardware

- Mixed VHDL/SystemVerilog C64 elaboration, the inferred writable memories,
  achieved PAL/NTSC clock, XPM constraints, resource use, and timing have not
  been checked by Vivado.
- AK4493 I2S output timing: exact receiver setup/hold values are not in the
  publicly accessible product material. The timing gate must remain red.
- PSRAM activation: PHY timing, IDELAYCTRL, calibration, and board measurement
  are unresolved. The diagnostic top holds CE high.
- Physical C64 video/audio selection is intentionally absent until synthesis
  and diagnostic outputs pass. USB input, SD file I/O, DDR test, and all
  cold/warm boot claims require a Vitis build and the modified board.

## First vendor-machine sequence

1. Run `make check` and require a clean result.
2. Run `vivado -mode batch -source scripts/create_project.tcl`.
3. Fix only source/Tcl defects. Commit them before assigning a release build ID.
4. Build PAL first, then recreate with
   `MULTITINI_C64_VIDEO_STANDARD=NTSC`; confirm the C64 VHDL/SV compile order,
   inferred memories, achieved selected clock, XPM CDC constraints, and
   resource estimate before implementation.
5. Obtain and record AK4493SEQ timing; add reviewed I2S output delays.
6. Run `vivado -mode batch -source scripts/build_and_export_xsa.tcl`.
7. Review all reports in `build/vivado/reports`; do not rely only on run status.
8. Run `vitis -s scripts/create_vitis_workspace.py` and retain its complete log.
9. Execute the power, pin-isolation, boot, DDR, SD, DVI, audio, and USB gates in
   `MULTITINI_ONE_PLAN.md` and `docs/VIVADO_HANDOFF.md`.

Only after the minimal board image passes should PSRAM be connected and its
BIST exposed. The C64 integration remains off the physical outputs until its
clock, reset, video, and audio paths pass their corresponding gates.
