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
- Icarus tests for AXI, registers, video, audio, and PSRAM BIST; strict C tests,
  sanitizer tests, malformed-content exercise, Tcl completeness, pin coverage,
  and a host compile of the Xilinx-specific entry point.

## Deliberately blocked

- C64 import: 11 planned files have explicit local grants; 23 planned files
  still need a defensible license basis. The staging script refuses to copy
  while that list is non-empty.
- AK4493 I2S output timing: exact receiver setup/hold values are not in the
  publicly accessible product material. The timing gate must remain red.
- PSRAM activation: PHY timing, IDELAYCTRL, calibration, and board measurement
  are unresolved. The diagnostic top holds CE high.
- USB input, SD file I/O, DDR test, and all cold/warm boot claims require a
  Vitis build and the modified board.

## First vendor-machine sequence

1. Run `make check` and require a clean result.
2. Run `vivado -mode batch -source scripts/create_project.tcl`.
3. Fix only source/Tcl defects. Commit them before assigning a release build ID.
4. Obtain and record AK4493SEQ timing; add reviewed I2S output delays.
5. Run `vivado -mode batch -source scripts/build_and_export_xsa.tcl`.
6. Review all reports in `build/vivado/reports`; do not rely only on run status.
7. Run `vitis -s scripts/create_vitis_workspace.py` and retain its complete log.
8. Execute the power, pin-isolation, boot, DDR, SD, DVI, audio, and USB gates in
   `MULTITINI_ONE_PLAN.md` and `docs/VIVADO_HANDOFF.md`.

Only after the minimal board image passes should PSRAM be connected and its
BIST exposed. Only after the rights gate passes should the C64 staging script
be allowed to populate `third_party/c64/upstream`.
