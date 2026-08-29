# Multitini One

Multitini One turns the Appletini ONE card into a stand-alone FPGA system.
The first product target is a C64 built from the GPL MiSTer C64 core. Game Boy
Color is the second adapter used to prove that the shell is reusable. Neither
machine uses an Apple II host.

The repository is in pre-hardware-validation development. Nothing here is a
claim of working board support yet.

## Current state

Known:

- The board source baseline is Appletini commit `052fe8aa43c483c193fcbc10ecba4225272c213d`.
- The C64 source baseline is MiSTer C64 commit `62fc697e3c9b5982c9dc76aa8cd03db545d20803`.
- The Game Boy source baseline is MiSTer Game Boy commit
  `7a5ff50528cd9c1d13ffb675e7df8506bffaa078`.
- The common register ABI, host-testable firmware code, diagnostic FPGA shell,
  and source-only Vivado flow live in this repository.
- The 34-file C64 allowlist is imported exactly, SHA-256 locked, and covered by
  the per-file license ledger under the recorded redistribution assumption.
- A ROM-free `fpga64_sid_iec` build boundary, writable ROM adapter, ARM byte
  stream, coherent clock-domain mailboxes, live C64 input mapping, and
  reset-safe ROM boot helper are implemented.
- The 47-file Game Boy allowlist, open SameBoy-derived CGB bootstrap, generated
  strict-tool compatibility ports, GB/GBC content validation, cartridge boot
  helper, adapter, one-line cartridge cache, and shared external-memory ABI are
  implemented and provenance locked.
- The board shell selects C64 or GBC at project creation. A GBC PSRAM CDC/PHY
  path is prepared but deliberately disabled by default pending Vivado timing
  and board validation.
- The AXI register path, DVI timing generator, audio path, PSRAM BIST, C64 ROM
  loader/input bridges, and asynchronous mailbox pass Icarus simulations. The
  selected SystemVerilog C64 path passes Verilator lint.
- Portable content, DAC, temperature, and shell-link code passes normal and
  sanitizer-enabled host tests. The Vitis entry point compiles against strict
  host stubs for the Xilinx APIs it uses.
- Unapproved ROM/media and generated FPGA or firmware products are blocked from
  source control. The allowlisted open GBC bootstrap is the current exception;
  `docs/CONTENT_POLICY.md` applies the same rule to every core.

Not yet proven:

- The current sources have not been opened, synthesized, implemented, or timed
  in Vivado.
- The firmware has not been built in Vitis.
- AK4493 I2S receiver timing remains unavailable from the public product page;
  the Vivado timing-coverage gate must not be waived to hide it.
- The mixed-language C64 build has not been compiled by Vivado. macOS checks
  do not validate VHDL elaboration, Xilinx memories, XPM CDC constraints, the
  requested PAL/NTSC core clock, or resource/timing results.
- The mixed-language GBC build has not been compiled by Vivado. Its portable
  dual-port memories, requested 33.554432 MHz clock, common-memory CDC, PSRAM
  I/O resources, external timing, and resource fit remain vendor-tool gates.
- C64 video and audio are retained for synthesis but are not connected to the
  physical outputs yet. The board top deliberately continues to show/play the
  independent diagnostics.
- SD file-system and USB input integration are not implemented. The portable
  firmware can transfer caller-supplied C64 ROM buffers. The only embedded ROM
  content is the pinned open GBC bootstrap.
- No Multitini image has booted on the card.
- DVI, audio, USB host, SD, DDR, PSRAM, and Apple-bus isolation still require
  the hardware gates in `MULTITINI_ONE_PLAN.md`.

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

```sh
vivado -mode batch -source scripts/create_project.tcl
vivado -mode batch -source scripts/build_and_export_xsa.tcl
vitis -s scripts/create_vitis_workspace.py
```

Project creation defaults to PAL. Set
`MULTITINI_C64_VIDEO_STANDARD=NTSC` while running `create_project.tcl` to make
the independently reproducible NTSC build.

Create the source-only GBC project with:

```sh
MULTITINI_MACHINE=GBC vivado -mode batch -source scripts/create_project.tcl
```

This requests 33.554432 MHz and leaves the physical PSRAM path disabled. Read
`docs/VIVADO_HANDOFF.md` before opting into that path.

See `docs/HANDOFF_STATUS.md` and `docs/VIVADO_HANDOFF.md` before running them.

## Licensing

Original Multitini work is licensed under GPL-3.0-only. Imported files retain
their own notices and licenses. See `licenses/README.md` and
`licenses/files.csv`. No ROM or media rights are granted by an HDL license.
