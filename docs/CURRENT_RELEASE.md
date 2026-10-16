# Current release

The C64 and GBC functional baselines are accepted. This package preserves the
tested images; promotion does not rebuild firmware, FPGA images, or ROMs.

The newer [shared interface](SHARED_FRONTEND.md), firmware `20260920` with
FPGA packages `20260919`, is also owner-confirmed flashed and board tested.
This page describes the retained `build/current` and root firmware aliases;
the shared-interface source commit does not replace those generated files.

| Component | Build | ABI |
| --- | --- | --- |
| ARM firmware (`FIRMWARE-MULTITINI.BIN`) | 20260917 | 0.10 |
| Supervisor FPGA and C64 PAL/NTSC cores | 20260915 | 0.10 |
| Game Boy Color core with PSRAM | 20260913 | 0.9 |

These build IDs intentionally differ. The supervisor accepts the existing GBC
package. Build IDs identify artifacts and are not their calendar build dates.
The current selection was accepted on 2026-09-14.

## Install

The assembled package is `build/current/`; its ZIP is
`build/multitini-current-20260917.zip`. Firmware is also available at the
repository root as `FIRMWARE-MULTITINI.BIN`. Promotion keeps the historical
selection record and image bytes, and uses the current delivery filename.

1. Back up the SD card, including writable disks, saves, settings, and previous
   firmware.
2. Merge the package's `multitini` directory into the SD card root. It contains
   both C64 variants, the GBC core, four stock C64/1541 ROMs, and the matching
   fast KERNAL/1541 pair. Preserve existing media, saves, and settings.
3. Update `FIRMWARE-MULTITINI.BIN` using the normal golden recovery/SD update procedure.
4. Select C64 PAL or NTSC. Enable **Fast serial ROMs** to select the paired
   fast profile; CPU turbo remains a separate option.

For GBC, put user-supplied `.gb` or `.gbc` cartridges under
`multitini/cores/gbc/media`, select **Game Boy Color** in Cores, and load a
cartridge from Media. Battery saves use `<cartridge-name>.sav` beside the
cartridge. Wait for **Saved** before removing power. See `GBC_BASELINE.md`
in this package for the accepted scope and save behavior.

In **Mount & Run**, Enter loads the selected program through the emulated
drive and starts it. Right performs native LOAD only; type RUN afterward when
needed. Mount a replacement disk without restarting the game at a disk prompt.
No game images, writable disks, saves, or user settings are in this package.

## Acceptance and checks

The owner confirmed Ultima II starts through Mount & Run, reaches its
second-disk prompt, and accepts the Player disk. A clean Player image works
with fast ROMs enabled. The owner attributed the remaining LOAD ERROR failures
to the old corrupted Player image; the cause of that corruption is unconfirmed.
Fast loading and GBC battery saving were also confirmed on hardware.

On 2026-09-14 the owner explicitly confirmed that the current GBC baseline
works, including tested saves. The GBC functional baseline is complete with
the retained `20260913` image. Broader mapper coverage, RTC persistence, and
power-loss qualification remain separate work; see
[GBC baseline](GBC_BASELINE.md).

Final source validation passed all 28 host tests and 139 Python tests (three
skipped), generated-file checks, repository policy, and both imported-core
audits. The selected C64/supervisor FPGA builds passed their strict timing
gates; the firmware payload audit and focused ASan/UBSan regression passed.
This is the accepted current baseline, not exhaustive game compatibility
qualification. Detailed launch investigations remain in
`docs/C64_LAUNCH_20260915.md`, `docs/C64_LAUNCH_20260916.md`, and
`docs/C64_LAUNCH_20260917.md` in the source repository.

## Reproduce the package

`provenance/current_release.json` pins every runtime input by exact source
path, size, SHA-256, and SD destination. From the repository root:

```text
python scripts/promote_current_release.py
python scripts/promote_current_release.py --check
```

The script requires the selected dated build outputs and local ROM inputs.
It fails on missing or changed inputs and never substitutes another build.
It refreshes `build/current`, root `FIRMWARE-MULTITINI.BIN`,
`build/firmware-current/FIRMWARE-MULTITINI.BIN`, and `build/core-packages`.
The ZIP contains `CURRENT_RELEASE.json`, `SHA256SUMS.txt`, and notices.
Generated artifacts remain outside Git; source, provenance, and packaging
scripts are committed. Stock ROMs and fast derivatives retain their recorded
`NOASSERTION` rights status; original fast-loader patch sources are GPL-3.0-only.

The legacy GBC binary is hash-verified and retained unchanged. Its historical
build record names a dirty source tree without a complete saved source delta;
an exact source-to-binary mapping for that older image is not established.
This local promotion does not newly publish that binary. Current firmware and
C64/supervisor captured build inputs match the accepted build records.
