# Multitini firmware M0.1.0

M0.1.0 starts the Multitini firmware release names. Appletini firmware uses
`F`; Multitini uses `M<major>.<minor>.<patch>`. The System page and startup UART
log show `M0.1.0`. Its ARM build ID is `0x4D000100`: the four bytes encode
`M`, major, minor and patch.

## Controller menu

Hold Select and Start together for 500 ms to open or close the menu. The
gesture works in standby and every supported core. It fires once and waits
for both buttons to be released before another toggle. Once both buttons are
down, the pair is consumed while the menu request is pending. A solo press
continues to reach the game immediately; a first button delivered before its
partner arrives cannot be withdrawn.

D-pad moves or adjusts values. A confirms, B goes back or closes the top-level
menu, and L/R changes tabs. F12 and dedicated Menu/Home buttons remain
available. The menu stays open through configuration and media operations
until an explicit exit. The gesture also returns from controller assignment
or test mode using the existing F12 behavior; unsaved edits still require
Save changes or Discard changes. Stick settings allow controller navigation.
Keyboard Delete and Tab remain available for clearing or skipping assignments.

USB generic pads use their reported button layout: the usual ten-button HID
layout uses buttons 9/10 for Select/Start, and eight-button pads use 7/8.
Supported Xbox and PS4 reports use their normalized Select/Start buttons.
Detection uses physical controls independently of each core's gameplay map.

## Build inputs

This is an ARM firmware release over `build/firmware-efx1`. Reuse its exact
FSBL and supervisor bitstream, plus every file under its
`sd/multitini/cores` tree. Supervisor, C64 PAL, GBC, Vectrex and Genesis
NTSC/PAL FPGA images remain PHO5, build ID `0x50484F35`, ABI 0.21.
There is no Vivado build and no new clock, reset, crossing or timing rule.
The existing FPGA splash retains its F12 prompt; the firmware menu help
documents the controller gesture.

The release packager verifies retained input hashes, audits the Bootgen
partitions and embedded CPU1 video worker, and saves separate provenance for
new software and retained hardware. Generated Vitis files, binaries and ZIPs
stay under ignored `build/` directories.

The build verifies a fresh video-worker compilation against EFX1 and selects
EFX1's exact worker ELF for embedding. A fresh ELF's GNU build identifier may
differ; all bytes outside that identifier must match. The attempted rebuild,
comparison record and retained selection remain in the release evidence.

## Validation

The GCC UCRT native suite passes 61/61 checks, including the production USB
runtime, all five core/standby input paths and eight-/ten-button HID plus
vendor layouts. Production menu-runtime tests cover stick navigation and
reserved gesture protection in immediate and periodic assignment capture.
The focused Python suite passes with one optional tool check skipped.

Five affected menu, controller-model and System checks pass with Visual Studio
Clang ASan/UBSan. The full Clang host build cannot compile CherryUSB's existing
packing macros in this toolchain; USB runtime validation uses GCC. The failed
full attempt and successful focused sanitizer run are retained as separate
evidence. This does not claim a complete USB sanitizer run.

Vitis 2025.2 builds the ARM applications. Package validation records hashes,
checks the updater manifest and every Bootgen/ELF partition, and verifies the
retained supervisor, machine packages and embedded CPU1 bytes.

No board was flashed or physically controller-tested. M0.1.0 needs a board
check of menu entry/exit, normal game buttons, controller disconnect/reconnect
and switching between machine cores.

## Reproduce

Use the selected EFX1 baseline and its retained PHO5 exported Vitis platform.
The script refuses an existing application workspace and never runs Vivado.

```powershell
./scripts/build_firmware_release.ps1 -Baseline build/firmware-efx1 `
  -Workspace build/vitis-M0.1.0-final `
  -Platform build/phos5/vitis/multitini_platform/export/multitini_platform/multitini_platform.xpfm

python scripts/package_firmware_release.py --baseline build/firmware-efx1 --workspace build/vitis-M0.1.0-final --version M0.1.0 --build-id 4D000100 --output build/firmware-M0.1.0
```

Repeat the packaging command with `--check-only` to audit an existing package.
To install, copy `sd/FIRMWARE.BIN` to the SD root and use the existing golden
updater. Installed EFX1 core packages can remain. Preserve media and settings.
