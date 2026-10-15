# Multitini One

Multitini One turns the Appletini ONE card into a stand-alone FPGA computer.
It runs C64, Game Boy Color, Vectrex, Genesis / Mega Drive and Apple IIgs
machines through a shared supervisor, menu, input, audio, storage and
core-loading system. It does not use an Apple II host.

M0.5.2 fixes two IIgs startup faults: lost RTC control writes at stock speed,
and stale interrupt-vector ROM bytes after the first System 6 disk reads.
It also adds writable 800 KiB PO/2MG floppies in the 3.5-inch drive and
140 KiB DOS-order DSK files in the 5.25-inch drive. Both drives retain WOZ2.
The IWM fixes fractional bit-cell carry and byte-latch expiry in production.
The hardware-test bundle is `build/firmware-core-gs-m052.zip`; install its
matching firmware and IIgs image together. The other seven FPGA images keep
their qualified identities, including the M0.5.1 NES fixes. See the
[IIgs checks](docs/APPLE_IIGS_HARDWARE_TEST.md) for the physical test procedure.
The [M0.5.2 build record](docs/APPLE_IIGS_M052_BUILD.md) describes the reproduced
failures, media conversion and qualification limits.

Branch `core-nes` adds a pinned MiSTer NES port with standard NTSC cartridge
loading, two controllers, battery saves and complete 256x240 video through
the shared display services. See [NES integration](docs/NES_PLAN.md) for the
source pin, accepted formats, build steps and validation limits. M0.5.1 fixes
loading supported NES 2.0 cartridges and preserves mapper 2 AND bus conflicts.
The update is `build/firmware-core-nes-m051.zip`; install its firmware and NES
core together. See
[build evidence](docs/NES_BUILD.md) and [hardware checks](docs/NES_HARDWARE_TEST.md).
Physical NES support remains subject to board testing.

M0.5.1 also adds an About tab after System. It contains the existing rows from
Firmware build through Audio output, in their original order, with live values.

The earlier `core-gs` M0.4.0 hardware-test package is preserved at
`build/firmware-core-gs3.zip`. It adds accelerated IIgs execution, the SHR
left-edge correction, nested media folders and USB SD-card export. The new
IIgs FPGA image passes routed timing: setup `+0.040 ns`, hold `+0.049 ns`
and pulse width `+1.250 ns`. The six unchanged FPGA images retain their
qualified identities. This replacement still requires board testing.
See the [IIgs integration](docs/APPLE_IIGS_PLAN.md),
[hardware checks](docs/APPLE_IIGS_HARDWARE_TEST.md),
[selective builds](docs/CORE_ARTIFACT_REUSE.md) and [USB SD mount](docs/USB_SD_MOUNT.md).

Private delivery packages place user-supplied boot ROMs in each core's `roms/`
folder and provide media loaders. IIgs ROMs belong in
`multitini/cores/apple-iigs/roms/`; disks and PRAM belong in the sibling
`multitini/cores/apple-iigs/media/`. ROM bytes stay outside Git and source
archives. Games and operating-system disks are not included.

Earlier firmware [M0.1.0](docs/FIRMWARE_M0.1.0.md) adds a
controller menu shortcut: hold Select and Start together for 500 ms. The ARM
firmware uses the exact supervisor and core images from `build/firmware-efx1`;
there is no FPGA rebuild. The release package is `build/firmware-M0.1.0.zip`.
Physical controller acceptance remains pending.

The C64 and GBC functional baselines are accepted. The retained older package combines
ARM firmware **20260917**, supervisor and C64 PAL/NTSC FPGA images **20260915**
(ABI 0.10), and the unchanged PSRAM-enabled GBC image **20260913** (ABI 0.9).
These build IDs intentionally differ. See
[Current release](docs/CURRENT_RELEASE.md) for installation and exact contents.

The owner confirms Ultima II starts through Mount & Run, reaches the
second-disk prompt, and accepts a Player-disk swap. A clean Player disk works
with fast ROMs enabled. The earlier `LOAD ERROR!` was attributed by the owner
to the old corrupted Player image; the cause of that corruption is unconfirmed. Fast
loading and GBC battery saving are also confirmed on the board. Earlier
board work established live PAL video, USB keyboard/gamepad input, core
switching, and the dual-SID scratching fix. This is the tested baseline,
not exhaustive game or hardware qualification.

On 2026-09-14 the owner confirmed the current GBC implementation works,
including tested saves, and accepted completion of that baseline. See
[GBC baseline](docs/GBC_BASELINE.md) for its scope and evidence.

## Retained dated baseline

The assembled SD tree is `build/current/`, with firmware at
`build/current/FIRMWARE.BIN` and packages under
`build/current/multitini/cores/{c64,gbc}`. The repository-root `FIRMWARE.BIN`
is the same firmware. Generated images remain outside Git.

[provenance/current_release.json](provenance/current_release.json) pins the
selected dated inputs, ROM pairs, sizes, hashes, and destinations. Recreate
and verify the package from those exact local inputs with:

```text
python scripts/promote_current_release.py
python scripts/promote_current_release.py --check
```

The script rejects missing or changed inputs; it does not select a newer
build automatically. The package includes the selected stock C64/1541 ROMs
and the matching `c64fast` pair, with their recorded notices. Game images,
writable disks, saves, and user settings are not included.

The supervisor starts on the standby screen unless `Autoboot current core`
is enabled. F12 opens the core and media menus. C64 `Fast serial ROMs` and
`CPU turbo` are separate settings, both defaulting to Off. Mount & Run uses
the drive's LOAD path by default; Right loads only, and Left retains the
explicit direct-RAM shortcut for eligible BASIC programs.

## Documentation

- [NES integration](docs/NES_PLAN.md), [build evidence](docs/NES_BUILD.md) and
  [hardware checks](docs/NES_HARDWARE_TEST.md): cartridge formats, controls,
  saves, shared video, reproducible imports and selective builds.
- [Apple IIgs integration](docs/APPLE_IIGS_PLAN.md),
  [CPU validation](docs/APPLE_IIGS_CPU_VALIDATION.md) and
  [hardware checks](docs/APPLE_IIGS_HARDWARE_TEST.md): pinned source, runtime
  ROMs, media, acceleration, complete-image timing and board-test procedure.
- [Selective FPGA builds](docs/CORE_ARTIFACT_REUSE.md): retain qualified
  unchanged images with their original source and timing evidence.
- [USB SD mount](docs/USB_SD_MOUNT.md): exclusive USB0 card export and recovery.
- [Genesis / Mega Drive integration](docs/GENESIS_PLAN.md): modern Nuked-MD,
  cartridge formats, two-player controls, saves, NTSC/PAL builds, and pending
  vendor/board qualification.

- [Video settings](docs/VIDEO_SETTINGS.md): per-core optional borders,
  muted colors, padding, and the ABI 0.12 update requirements.
- [Vectrex plan](docs/VECTREX_PLAN.md), [game tests](docs/VECTREX_GAME_TESTS.md),
  [test bundle](docs/VECTREX_TEST_BUILD.md), and
  [acceptance record](docs/VECTREX_ACCEPTANCE.md):
  the `vectrex` development branch adds a fresh FPGA machine and ARM GS BASIC
  runtime for Vectrex32. The owner accepted the
  [VEC3 video update](docs/VIDEO_TEST_BUILD.md) after physical testing on
  2026-09-15; detailed game qualification remains separately recorded.
- [Handoff status](docs/HANDOFF_STATUS.md): current build, validation, and
  remaining limits.
- [Shared front end](docs/SHARED_FRONTEND.md): board-tested firmware `20260920`,
  Appletini-style layout, core profiles, automatic pause, efficient redraws, and previews.
- [Appletini shared-platform imports](docs/appletini-parity.md): controller
  mapping, USB recovery, display modes, golden self-update, and build hardening;
  local test results and remaining board gates.
- [Multicore runtime](docs/MULTICORE_RUNTIME.md): packages, menu controls,
  settings, core switching, and GBC saves.
- [GBC baseline](docs/GBC_BASELINE.md): accepted gameplay and save behavior,
  exact package, and deferred features.
- [C64 SD workflow](docs/C64_SD_STORAGE.md): ROMs, Mount & Run, disk swaps,
  D64 persistence, G64 scope, and serial recovery.
- [C64 fast disk](docs/C64_FAST_DISK.md): paired ROM protocol, reproducible
  build, simulation measurements, and compatibility limits.
- [Hardware results](docs/HARDWARE_RESULTS.md): dated board evidence.
- [Launch 20260915](docs/C64_LAUNCH_20260915.md),
  [20260916](docs/C64_LAUNCH_20260916.md), and
  [20260917](docs/C64_LAUNCH_20260917.md): preserved launch investigations,
  corrections, and retests.

Read [MULTITINI_ONE_PLAN.md](MULTITINI_ONE_PLAN.md) before changing the design.
Source pins are recorded in [provenance](provenance/sources.lock.json), and
the board shell is built from scripts rather than a checked-in vendor project.

## Build and check

Portable checks require Python 3, CMake, C and C++17 compilers, Tclsh, Icarus Verilog
13 or later, and Verilator 5:

```sh
make check
```

This checks generated ABI files, policy and provenance, build manifests,
portable firmware, host and HDL regressions, and selected C64/GBC lint. It
does not replace vendor timing review or board testing.

Use Vivado and Vitis 2025.2 for FPGA and ARM builds. Project creation defaults
to the supervisor-only shell; C64 PAL/NTSC and PSRAM-enabled GBC are explicit
machine builds. Follow [VIVADO_HANDOFF.md](docs/VIVADO_HANDOFF.md) for the
exact settings and [MULTICORE_RUNTIME.md](docs/MULTICORE_RUNTIME.md) for core
packaging. Firmware-only updates can retain verified compatible FPGA images,
as the current release does.

`scripts/make_firmware_bin.bat` takes explicit output, FSBL, supervisor BIT,
and application ELF paths. It requires the supervisor kind marker and rejects
machine bitstreams, which belong in the SD core packages.

## Licensing

Original Multitini work is GPL-3.0-only. Imported files retain their notices
and licenses; see [licenses/README.md](licenses/README.md) and
[licenses/files.csv](licenses/files.csv). Historical stock ROM and fast-ROM
hashes retain their recorded `NOASSERTION` rights status. Current ROM bytes
are external user inputs for private generated packages. See
[Content policy](docs/CONTENT_POLICY.md); an HDL license grants no ROM or
game-image rights.
