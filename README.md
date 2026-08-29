# Multitini One

Multitini One turns the Appletini ONE card into a stand-alone FPGA system.
The first product target is a C64 built from the GPL MiSTer C64 core. It does
not use an Apple II host.

The repository is in pre-hardware-validation development. Nothing here is a
claim of working board support yet.

## Current state

Known:

- The board source baseline is Appletini commit `052fe8aa43c483c193fcbc10ecba4225272c213d`.
- The C64 source baseline is MiSTer C64 commit `62fc697e3c9b5982c9dc76aa8cd03db545d20803`.
- The common register ABI, host-testable firmware code, diagnostic FPGA shell,
  and source-only Vivado flow live in this repository.
- The AXI register path, DVI timing generator, audio path, and PSRAM BIST
  sequencer pass Icarus Verilog simulations.
- Portable content, DAC, temperature, and shell-link code passes normal and
  sanitizer-enabled host tests. The Vitis entry point compiles against strict
  host stubs for the Xilinx APIs it uses.
- System ROMs, game media, and generated FPGA or firmware products are blocked
  from source control.

Not yet proven:

- The current sources have not been opened, synthesized, implemented, or timed
  in Vivado.
- The firmware has not been built in Vitis.
- AK4493 I2S receiver timing remains unavailable from the public product page;
  the Vivado timing-coverage gate must not be waived to hide it.
- The C64 import is blocked on 23 planned files with no file-local license
  grant. No C64 source has been imported.
- No Multitini image has booted on the card.
- DVI, audio, USB host, SD, DDR, PSRAM, and Apple-bus isolation still require
  the hardware gates in `MULTITINI_ONE_PLAN.md`.

## Host checks

The checks available on macOS require Python 3, CMake, a C compiler, Tclsh,
and Icarus Verilog 13 or later:

```sh
make check
```

This regenerates and compares the C/SystemVerilog ABI files, checks repository
policy and provenance, checks the Tcl/build manifests and pin coverage, builds
the portable firmware library with and without sanitizers, compiles the Vitis
entry point against API stubs, and runs host and HDL tests. It does not replace
Vivado, Vitis, timing review, or board testing.

## Vendor-tool handoff

The intended first commands on a Vivado 2025.2 machine are:

```sh
vivado -mode batch -source scripts/create_project.tcl
vivado -mode batch -source scripts/build_and_export_xsa.tcl
vitis -s scripts/create_vitis_workspace.py
```

See `docs/HANDOFF_STATUS.md` and `docs/VIVADO_HANDOFF.md` before running them.

## Licensing

Original Multitini work is licensed under GPL-3.0-only. Imported files retain
their own notices and licenses. See `licenses/README.md` and
`licenses/files.csv`. No ROM or media rights are granted by an HDL license.
