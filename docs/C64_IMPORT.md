# MiSTer C64 Import Record

Status: pinned and audited at repository level; source import and per-file
clearance are incomplete.

## Pin

- Repository: `https://github.com/MiSTer-devel/C64_MiSTer.git`
- Commit: `62fc697e3c9b5982c9dc76aa8cd03db545d20803`
- Commit date: 2026-08-23
- Commit subject: `Release 20260823.`

## Import rule

Do not add the upstream repository as a recursive product dependency. It
contains system and drive ROM images, release media, prebuilt programs, Intel
generated IP, and MiSTer platform files that are not required in the final
Multitini source tree.

The eventual import must use an explicit allowlist and preserve relative paths.
It must exclude at least:

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
- Several FPGA64 VHDL files contain copyright notices but no file-local license
  grant. Their use under the top-level GPL notice needs confirmation before the
  Phase 0 rights gate is marked passed.
- The upstream tree does not include the license texts referenced by several
  source headers. Multitini must include every applicable text with an import.

No C64 source file is listed in `licenses/files.csv` until its import allowlist
and license entry are complete.

## Prepared audit gate

`third_party/c64/import_manifest.json` is the explicit first-core allowlist.
It separates 11 files with file-local grants from 23 planned files whose
headers do not state a license grant. It also records the ROM, media, OPL3,
Intel IP, and MiSTer platform exclusions.

Audit a checkout at the pinned commit with:

```sh
python3 scripts/audit_c64_import.py --source /path/to/C64_MiSTer
```

The script deliberately refuses `--stage` while any entry remains unresolved.
Do not change an entry to resolved merely by inference from a repository name
or from a different file's header. Record a maintainer statement, upstream
license file, or original-source grant as the basis.
