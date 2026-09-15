# Vectrex test build

This page records the earlier VEC2 bundle under `build/vectrex-media-fix/`.
The newer [VEC3 video update](VIDEO_TEST_BUILD.md) includes these media fixes
and was accepted by the owner after physical testing on 2026-09-15.
Detailed ARM frame deadlines have not been measured on an Appletini ONE board.

## Contents and identity

- `sd/FIRMWARE.BIN`: fresh Vitis 2025.2 FSBL and application, plus the selected
  supervisor FPGA image. The application reports `56454332` (`VEC2`). This
  revision fixes folder navigation and nested BASIC program/asset loading.
- `sd/multitini/cores/vectrex/core.cfg` and `vectrex-native.bin`: the V5 FPGA
  machine, core ID `56454358` (`VECX`), ABI 0.11. It runs cartridges and accepts
  Vectrex32 vectors/audio from the ARM runtime.
- `source.zip` and `source-manifest.json`: exact working-tree source bytes,
  including new files, licenses, constraints and build scripts. The baseline
  commit is `2ede66e487dfa403799929f708acaf1563b33def`; this is a dirty source
  snapshot, so that commit alone cannot reproduce the binary.
- `manifest.json`, `SHA256SUMS.txt`, and `evidence/`: selected artifact hashes,
  build settings, implementation reports, package byte audit and test results.

The shell build ID remains `2ede66e4`. Use the bundle hashes and source snapshot
to identify this test, rather than that shared baseline ID. The core meets
setup, hold, pulse-width, routing, CDC and bus-skew gates; its setup WNS is
+0.103 ns. See [acceptance](VECTREX_ACCEPTANCE.md) for exact scope and limits.

## Local play test

Use the usual golden recovery/SD firmware-update procedure with the bundle's
`FIRMWARE.BIN`, and merge its `multitini` directory into the SD root. Retain
existing C64/GBC packages and media if testing core switching.

Supply the combined 8192-byte Mine Storm plus executive ROM image at
`multitini/cores/vectrex/roms/bios.bin`. Put native `.vec` or `.bin` cartridges
under `multitini/cores/vectrex/media/`. Copy the complete supplied Vecalabeth
directory to `multitini/cores/vectrex/media/Vecalabeth/`, keeping `vec.bas`
beside all its asset files. Replace `core.cfg` with the updated copy too: it
sets `content_root=0:/multitini/cores/vectrex/media`. The
bundle excludes Vectrex BIOS and game content; the existing external assets
remain available for local tests.

Open F12, select the Vectrex core and choose a cartridge or open the BASIC
browser, enter `Vecalabeth`, and select `vec.bas`. Enter/Right opens folders;
Backspace/Left or `..` goes up. Escape closes the browser.
Arrow keys move; Z, X, C and V are buttons 1 through 4. Gamepads expose all four
buttons and two controller ports. F12 returns from BASIC to the menu; Reset
core reloads the selected BASIC program. BIOS-only native boot runs Mine Storm.
See [firmware controls](VECTREX_FIRMWARE.md) for details.

## Rebuild

Run `scripts/create_project.tcl` and `scripts/build_and_export_xsa.tcl` with
`MULTITINI_MACHINE=VECTREX` and a dedicated `MULTITINI_VIVADO_DIR`. Build the
supervisor separately with `MULTITINI_MACHINE=SUPERVISOR`. Both must pass the
existing export gates. Set `MULTITINI_XSA` to the accepted Vectrex XSA and
`MULTITINI_VITIS_WORKSPACE` to a short fresh workspace, then run
`scripts/create_vitis_workspace.py` through Vitis 2025.2. Long Windows build
paths can exceed the vendor tools' limits.

Use `scripts/run_native_tool.py` around Windows compiler, simulator and vendor
tool commands. It supplies the installed UCRT DLL directory to child processes
and turns loader failures into terminal errors instead of modal dialogs.

Package only explicit inputs with `scripts/make_core_package.py` and
`scripts/make_firmware_bin.bat`. Run `scripts/audit_vectrex_package.py` against
the selected FSBL, supervisor BIT, application ELF, native BIT and core manifest.
The audit matches all initialized ELF load segments, load addresses and entry
points, and regenerates processed bitstreams for byte comparison.
