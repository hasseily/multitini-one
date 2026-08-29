# MiSTer C64 Import Record

Status: 34 allowlisted files imported, SHA-256 locked, and licensed in the
repository ledger. Vivado and hardware validation remain pending.

## Pin

- Repository: `https://github.com/MiSTer-devel/C64_MiSTer.git`
- Commit: `62fc697e3c9b5982c9dc76aa8cd03db545d20803`
- Commit date: 2026-08-23
- Commit subject: `Release 20260823.`

## Import and exclusion rule

Do not add the upstream repository as a recursive product dependency. It
contains system and drive ROM images, release media, prebuilt programs, Intel
generated IP, and MiSTer platform files that are not required in the final
Multitini source tree.

The import uses the explicit allowlist in
`third_party/c64/import_manifest.json` and preserves relative paths. It
excludes at least:

- `releases/`;
- `rtl/roms/`;
- `rtl/iec_drive/*_rom.mif` and `c1541_dolphin.mif`;
- `rtl/opl3/fw/` and OPL3 for the first milestone;
- Intel PLL, SDRAM, HPS, scaler, HDMI, and generated platform files; and
- prebuilt `.rbf`, `.exe`, `.com`, disk, cart, tape, and ROM content.

The C64 and drive ROM stores must be writable from the Multitini load stream
and start empty. User ROMs are required until distribution rights are secured.

## License findings

- `c64.sv` declares GPL-2.0-or-later for the combined program.
- Several later modules declare GPL-2.0-or-later, GPL-3.0-or-later, or
  LGPL-3.0-or-later.
- T65 files carry a 3-clause BSD-style hardware license.
- SID DAC and filter files declare CERN-OHL-S-2.0.
- OPL3 mixes a BSD-style license and LGPL-2.1-or-later; it is optional and is
  excluded from the first milestone.
- Twenty-three allowlisted files have copyright or provenance notices but no
  file-local grant. The repository owner directed the project to assume a
  redistribution grant under GPL-2.0-or-later for those files. That assumption,
  its scope, and its exclusions are machine-readable in the import manifest.
- The three referenced full license texts are stored in
  `licenses/third_party/`. T65's complete redistribution notice remains in each
  exact imported file.
- Every imported C64 file has an explicit row in `licenses/files.csv`.

The assumption does not cover ROMs, media, generated IP, binaries, logos,
manuals, or files outside the allowlist. It is an owner-directed project input,
not an independent legal conclusion.

## Reproducible audit gate

`third_party/c64/import_manifest.json` is the explicit first-core allowlist.
It separates 11 files with file-local grants from the 23 files covered by the
recorded assumption. It also records the ROM, media, OPL3, Intel IP, and MiSTer
platform exclusions.

Audit a checkout at the pinned commit with:

```sh
python3 scripts/audit_c64_import.py --source /path/to/C64_MiSTer
```

The repository copy and `provenance/imports.lock.json` can be checked with:

```sh
python3 scripts/audit_c64_import.py
python3 scripts/check_repo.py
```

Use `--update-lock` only while intentionally restaging the pinned allowlist.

## First build boundary

The Vivado manifest builds the FPGA64 machine boundary
`rtl/fpga64_sid_iec.vhd`, not the MiSTer `c64.sv` platform top. Multitini
replaces only the pieces needed for a ROM-free Xilinx build:

- `multitini_c64_buslogic.vhd` removes embedded MIF selections and provides
  writable BASIC/KERNAL and character ROM stores;
- `ports/dprom.vhd` and `ports/spram.vhd` use portable signal-array memory
  inference with no initialization file;
- the four SID compatibility ports make narrowly scoped language/elaboration
  fixes while the exact upstream versions remain imported and locked; and
- `multitini_c64_adapter.sv` supplies main RAM, ROM loading, input mapping,
  reset sequencing, raw video, and 48 kHz signed audio samples.

The imported 1541 files are not in the first build. Their current wrapper also
depends on excluded drive/platform pieces, so drive media requires a separate
adapter and service contract.
