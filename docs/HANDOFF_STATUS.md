# Current handoff

Branch `core-nes` adds the NES integration and M0.5.0 candidate firmware. Its
machine and firmware validation are recorded in [NES build evidence](NES_BUILD.md).
See [NES integration](NES_PLAN.md) for scope and [board checks](NES_HARDWARE_TEST.md)
for installation and physical acceptance.
The package is `build/firmware-core-nes.zip`, with an unpacked SD tree under
`build/firmware-core-nes/sd/`. The NES image passes routed setup `+0.219 ns`,
hold `+0.048 ns` and pulse width `+0.265 ns`.
The existing core images retain their original identities. NES board tests
remain separate from source, simulation and build evidence.

The earlier [M0.1.0](FIRMWARE_M0.1.0.md) is an ARM-only
controller-menu release built over `build/firmware-efx1`. Its package is
`build/firmware-M0.1.0/` with `FIRMWARE-M0.1.0.BIN`, an identical
`sd/FIRMWARE.BIN`, retained machine packages, source and audit evidence.
Supervisor and machine images remain PHO5 (`0x50484F35`, ABI 0.21).
Physical validation of this new release remains pending. The older
`build/current` aliases documented below are retained historical selections.

The owner accepts the C64 and GBC functional baselines. GBC baseline work is
complete following the 2026-09-14 confirmation that the current implementation
and saves were tested and work. Promotion preserves the tested artifacts.
See [GBC baseline](GBC_BASELINE.md) for the accepted scope and evidence.

The board-tested compact shared front end uses firmware `20260920` with the
`20260919` FPGA images (ABI 0.11). Configuration operations preserve the menu's
open state; only an explicit exit closes it.
It adds 128x60 text, tighter 14x16 glyphs, 40-file browsing, and automatic
C64/GBC pause while the menu is visible, alongside core-supplied controls
and cached presentation. It requires matching new supervisor and core FPGA
images. On 2026-09-14 the owner confirmed the firmware was flashed and board
tested and requested the source commit and push. See
[Shared front end](SHARED_FRONTEND.md) and [Hardware results](HARDWARE_RESULTS.md).
The generated current-package aliases below remain on the earlier release.

## Retained current-package artifacts

| Component | Build | ABI |
| --- | --- | --- |
| ARM firmware | 20260917 | 0.10 |
| Supervisor FPGA | 20260915 | 0.10 |
| C64 PAL and NTSC FPGA packages | 20260915 | 0.10 |
| GBC FPGA package, PSRAM enabled | 20260913 | 0.9 |

The different IDs are intentional. Firmware 20260917 is a firmware-only
launch correction over the verified 20260915 supervisor/C64 images; the
existing GBC package remains compatible. Build IDs identify artifacts and
are not calendar build dates.

The canonical assembled package is `build/current/`, including
`FIRMWARE.BIN` and `multitini/cores/{c64,gbc}`. The repository-root
`FIRMWARE.BIN` is the same accepted firmware. The package contains the
selected stock C64/1541 ROMs and the matching `c64fast` pair, with notices;
it contains no games, writable disks, saves, or user settings.

[Current release](CURRENT_RELEASE.md) gives installation instructions.
[current_release.json](../provenance/current_release.json) records each
runtime file's dated source, destination, size, and SHA-256. The explicit
[promotion helper](../scripts/promote_current_release.py) recreates the
package and aliases, and `--check` verifies them without writes. It refuses
changed or missing inputs. Generated artifacts remain outside source control.

## Board evidence

- Firmware 20260917 Mount & Run launches Ultima II and reaches its
  second-disk prompt. Mounting the Player disk and continuing works.
- A clean Player image works with fast ROMs enabled. The owner traced the
  earlier `LOAD ERROR!` to the previous corrupted Player image; when and how
  that copy became corrupted is unconfirmed.
- The owner confirms the fast loader and GBC battery saving work.
- On 2026-09-14 the owner reaffirmed that the current GBC implementation,
  including saves, was tested and works, closing the GBC functional baseline.
- Earlier board work established stand-alone boot and golden recovery,
  live PAL DVI, USB keyboard/gamepad input including wired Xbox 360,
  SD C64/1541 ROM loading, D64 gameplay, C64-to-GBC switching, and
  PSRAM-backed GBC gameplay. The dual-SID scheduling correction fixed the
  reported scratching in the 20260902 retest.

These are the reported configurations and observations. They do not qualify
every game, disk format, ROM replacement, video variant, or failure case.

## Build and source evidence

The selected 20260915 supervisor and PAL/NTSC C64 builds pass Vivado 2025.2
timing, routing, and implementation gates. The retained PSRAM-enabled GBC
20260913 image passed the same vendor flow. Its WNS/WHS/WPWS were
`+0.181/+0.054/+0.265 ns`; the raw packaged payload SHA-256 is
`91937d6059bde81795092146711b3a429939cc753f080d5d8436d996e73e9043`.
The retained GBC binary is hash-verified; the exact mapping of its historical
dirty source tree to current source has not been established. See the current
release record for this provenance limitation.

Firmware 20260917 passes the strict host suite, focused sanitizer checks,
build-input checks, and the fresh Vitis platform/application build. Its
payload audit matches the processed supervisor bitstream and each initialized
FSBL/application ELF segment by load address. The dated reports retain exact
artifacts, hashes, and gate results.

The launch regression is source-proven: clearing the shared load error after
remount could cancel a healthy disk request and desynchronize its mailbox.
Firmware 20260917 preserves that request and refuses RUN when a pre-existing
error is present. The host regression and production-track-service HDL
diagnostic cover the bad-clear and no-clear sequences. No game-name exception
or new FPGA image was needed.

Pinned source baselines:

- Appletini board support: `052fe8aa43c483c193fcbc10ecba4225272c213d`.
- MiSTer C64: `62fc697e3c9b5982c9dc76aa8cd03db545d20803`.
- MiSTer Game Boy: `7a5ff50528cd9c1d13ffb675e7df8506bffaa078`.

The source and import locks, license ledger, build scripts, constraints, and
current-release manifest remain tracked. See [VIVADO_HANDOFF.md](VIVADO_HANDOFF.md)
for rebuilding and [MULTICORE_RUNTIME.md](MULTICORE_RUNTIME.md) for packaging.

## Remaining limits

- Normal data-only D64 images are writable with journaled persistence;
  error-tail D64 and fixed-speed G64 are read-only. Variable-speed G64 maps,
  G64 save-back, tape, a second drive, 1581, and REU are outside the current
  implemented disk scope. See [C64_SD_STORAGE.md](C64_SD_STORAGE.md).
- The selected fast pair is board-confirmed, but alternative ROM pairs,
  arbitrary custom loaders, and CPU turbo compatibility are not broadly
  qualified. Simulator throughput figures are not physical-board timings.
- Exhaustive interrupted-write/power-loss recovery, long-run SD durability,
  repeated core-switch rollback, and the full settings/media recovery matrix
  remain outside the reported board coverage.
- Broad C64 CRT/PSRAM compatibility, GBC mapper/RTC coverage, destructive
  memory stress, electrical measurements, and extended PAL/NTSC, input, and
  audio/channel/SPDIF qualification remain open. Existing passes should not
  be read as completing every gate in the product plan.

These limits describe the accepted baselines; no further C64 or GBC feature
or troubleshooting work is scheduled by this handoff. Optional GBC features
and broader qualification do not reopen the completed functional baseline.

## Historical evidence

[HARDWARE_RESULTS.md](HARDWARE_RESULTS.md) preserves the board chronology.
The focused launch records are
[20260915](C64_LAUNCH_20260915.md),
[20260916](C64_LAUNCH_20260916.md), and
[20260917](C64_LAUNCH_20260917.md). They distinguish source/model/build
evidence from physical results and retain the superseded candidates for
diagnosis. Older snapshot, live-video, USB, and settings test procedures are
historical engineering references, not instructions to install an old image.
