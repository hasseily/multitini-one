# Multitini One

Multitini One turns the Appletini ONE card into a stand-alone FPGA computer.
It runs the GPL MiSTer C64 core and a Game Boy Color adapter through a shared
supervisor, menu, input, audio, storage, and core-loading system. It does not
use an Apple II host.

The C64 baseline is accepted for now. The current package combines ARM
firmware **20260917**, supervisor and C64 PAL/NTSC FPGA images **20260915**
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

## Use the current package

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

- [Handoff status](docs/HANDOFF_STATUS.md): current build, validation, and
  remaining limits.
- [Multicore runtime](docs/MULTICORE_RUNTIME.md): packages, menu controls,
  settings, core switching, and GBC saves.
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

Portable checks require Python 3, CMake, a C compiler, Tclsh, Icarus Verilog
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
[licenses/files.csv](licenses/files.csv). The selected stock ROMs and fast
derivatives retain their recorded `NOASSERTION` rights status. See
[Content policy](docs/CONTENT_POLICY.md); an HDL license grants no ROM or
game-image rights.
