# MiSTer C64 Import Record

Status: 34 allowlisted files imported, SHA-256 locked, and licensed in the
repository ledger. The historical PAL machine and read-only D64/1541 results
are preserved below. Current source also implements PRG injection, a
conservative read-only CRT path, transactional writable D64, and read-only G64.
The portable tests and focused XSim benches pass, but no full Vivado/Vitis
build or board qualification exists for that newer increment.

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
and start empty in the current adapter. A stable ordinary public ROM source may
be pinned later under `docs/CONTENT_POLICY.md`; otherwise the user supplies the
ROMs at run time.

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
- The pinned `rtl/cartridge.v` remains an exact untouched reference. Product
  source uses `hdl/core/c64/ports/cartridge.sv`, a separately listed local
  compatibility port with its unsynthesizable bare `always` changed to
  `always @*` and two early-referenced net declarations moved before use.
  Mapper behavior is otherwise unchanged.

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

The Vivado manifest builds the local FPGA64 machine-boundary compatibility port
`hdl/core/c64/ports/fpga64_sid_iec.vhd`, not the MiSTer `c64.sv` platform top.
The pinned `rtl/fpga64_sid_iec.vhd` remains an exact imported reference. The
local port stretches the VIC0-to-VIC3 or CPUC-to-CPUF bus phase across an
external cartridge-memory cache miss, so neither the VIC nor T65 can consume
the cache's placeholder byte before PSRAM returns the requested line. This
source path still requires vendor elaboration and board validation. It explicitly
compiles `mos6526.v` as SystemVerilog because that upstream file uses block
declarations despite its `.v` suffix. The local `ports/cpu_6510.vhd` changes
the T65 binding to direct entity instantiation because the upstream wrapper
does not declare a component. Multitini
replaces only the pieces needed for a ROM-free Xilinx build:

- `multitini_c64_buslogic.vhd` removes embedded MIF selections and provides
  writable BASIC/KERNAL and character ROM stores;
- `ports/dprom.vhd` and `ports/spram.vhd` use portable signal-array memory
  inference with no initialization file;
- the four SID compatibility ports make narrowly scoped language/elaboration
  fixes while the exact upstream versions remain imported and locked; and
- `ports/c1541_logic.sv` and `ports/c1541_gcr.sv` are strict-tool compatibility
  ports of the two pinned drive modules, with declarations moved before use;
  the GCR encoder's constant table is written as an equivalent case function
  for portable simulation, and the SD-buffer byte is registered beside the
  imported RAM so consecutive cache-copy writes remain aligned with that
  RAM wrapper's delayed address and write enable;
- `ports/cartridge.sv` is the local synthesis compatibility port of the pinned
  cartridge mapper, with one procedural-sensitivity fix and two declaration
  moves; the upstream reference remains unchanged;
- `multitini_c64_adapter.sv` supplies main RAM, system and 1541 ROM loading,
  PRG memory injection, conservative CRT mapping, input mapping, reset
  sequencing, serial IEC, raw video, and 48 kHz signed audio samples;
- `multitini_c64_content_loader.sv`, `multitini_c64_cart_memory.sv`, and
  `multitini_c64_run_injector.sv` provide the PRG/CRT stream sink, cartridge
  cache/common-memory bridge, BASIC readiness gate, and `RUN` key injection;
  and
- `multitini_c64_d64_track.sv` plus `multitini_c64_1541.sv` provide one
  drive 8 using the imported 1541 CPU/VIA logic, transactional D64 writeback,
  and sector-to-GCR buffering; and
- `multitini_c64_g64_track.sv`, pinned `c1541_direct_gcr.sv`, and its pinned
  `lfsr.v` dependency provide read-only fixed-speed-zone G64 half tracks.

The D64 track geometry and directory-track-first sequence are based on the
sector-buffer design at MiSTer C64 commit
`f11608f66bcb2b017680d7c719aa547f537bfd3f`, immediately before commit
`a1a560e1cc51ab27c8424678a39254aab67a5afe` replaced it with whole-image DDR
DMA. No file from that older revision is imported: the local service implements
the Multitini generation-tagged request contract and uses actual counts of
256-byte blocks. It covers the six recognized 35-, 40-, and 42-track D64 sizes,
including the final 42-track request at LBA 785 for 17 blocks.

The build uses `iecdrv_via6522.vhd`, a local `iecdrv_misc.sv` port with a
Vivado-supported XPM asymmetric RAM, strict local ports of `c1541_logic.sv`
and `c1541_gcr.sv`, and the pinned direct-GCR/LFSR pair. The
larger upstream `iec_drive`, `c1541_multi`, `c1541_drv`, DDR-track, and C1581
paths remain excluded. Drive 8 requires a separately loaded 16 KiB standard
1541 ROM. Current source supports transactional writable data-only D64,
read-only error-tail D64, and read-only fixed-speed G64; T64, TAP, a second
drive, and 1581 remain later work.

## Current PRG/CRT source boundary

The portable firmware parser accepts a PRG only when its two-byte load address
and complete payload fit C64 RAM. The PL waits for the KERNAL bus address
`$FFCF` after reset before it accepts the payload, pauses the machine while it
writes RAM and repairs BASIC end pointers, and then injects `RUN` followed by
Return. The service remains busy until key injection completes.

CRT acceptance is deliberately narrower than the imported mapper's full
feature set. The current firmware and generated interface specification admit
only these cartridge type IDs and profiles:

- `0` Normal;
- `4` Simons' BASIC;
- `5` Ocean type 1;
- `7` Fun Play;
- `8` Super Games;
- `10` Epyx FastLoad;
- `13` Final Cartridge I/II;
- `15` C64 Game System;
- `17` Dinamic;
- `18` Zaxxon;
- `19` Magic Desk;
- `21` Comal 80;
- `22` Waterloo/Structured BASIC;
- `28` Mikro Assembler;
- `43` Prophet64;
- `57` RGCD; and
- `85` Magic Desk 2.

The host and HDL validators require the exact CRT header, binary EXROM/GAME
values, ROM-only CHIP packets, exact mapper sizes and addresses, and a
mapper-compatible contiguous bank layout. Sparse or out-of-order banks are
rejected. Zaxxon is normalized only from its specific aligned 4 KiB plus 8 KiB
plus 8 KiB layout. Types needing writable cartridge RAM or flash, freezer
controls, or unimplemented peripherals are rejected even when the imported
mapper contains some related logic. EasyFlash and XBank remain rejected;
`am29f040.sv` is compiled only because it is an unconditional dependency of
the local compatibility port.

Validated CRT ROM bytes are normalized into the common 64-bit external-memory
service. The C64 machine selection now connects that service to the board
PSRAM path, and the cartridge bridge latches a first-miss stall through the
memory response. This is source architecture, not a hardware result: PSRAM
timing, CDC, and resource gates pass in the PSRAM-enabled `20260912` GBC vendor
build, while C64 CRT behavior still needs a board test.

The firmware source includes strict PRG/CRT parsing and a C64 media service
which streams only validated content and reports attachment only after the PL
finishes without `CORE_LOAD_ERROR`. The dedicated host tests and focused XSim
benches pass. The serial main loop exposes `:load` and `:cartoff`, including the
safe disk barrier around content changes. The general F12 multicore menu and
per-core configuration page are implemented. Fresh `20260912` vendor and
package builds pass; the current C64 increment still needs board validation.

## Verified engineering-build boundary

The result below predates the current PRG/CRT source and must not be used as
validation evidence for that increment.

The recorded default PAL build used Vivado 2025.2, part
`xc7z020clg484-2`, commit
`fd554dbf46e22514bb132f3dc063ee0a892bdcc6`, and a dirty engineering tree. Its
manifest timestamp is `2026-08-29T12:41:42Z`. Vivado generated a routed
bitstream and XSA. Timing passed with WNS +0.278 ns, WHS +0.024 ns, WPWS +1.749
ns, and zero failing endpoints; routing and DRC reported zero errors. The final
CDC report contained only 11 `CDC-3` Info crossings and one `CDC-9` Info reset
crossing, and the fatal critical-CDC gate passed.

Vitis 2025.2 built the FSBL and diagnostic ELF, and Bootgen packaged them with
the bitstream successfully. This is not a working-C64 result: no board test has
run, the physical DVI/audio paths still carry shell diagnostics, and the
diagnostic CLI does not load BASIC, KERNAL, or character ROMs. The core remains
held in reset. A separate NTSC vendor build is still pending.
