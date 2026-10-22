# Arkanoid graphics and ROM compatibility diagnosis

## Findings

The first Arkanoid uses a graphics operation that the pre-audit IIgs MMU decoded
incorrectly. It draws moving objects directly into bank E1 video RAM, then
restores their backgrounds by rewriting unchanged bytes in bank 03 with
all-bank SHR shadowing enabled. The old core failed to copy those bank-03 writes
into E1. This explains visible objects leaving trails and background changes
not reaching the display.

This is a source and directed-simulation diagnosis. The failing board image
and the owner's running disk have not been hash-matched. No serial port was
available during this investigation. The subsequent [MMU audit](APPLE_IIGS_MMU_AUDIT.md)
corrects production HDL through its port generator, with independent decode
and CPU-executed HDL regressions. No corrected FPGA has been built or tested on hardware.

ROM 03 startup failure is a separate, image-dependent question. MAME's
[original-disk manifest](https://raw.githubusercontent.com/mamedev/mame/master/hash/apple2gs_flop_orig.xml)
lists the original Arkanoid as requiring ROM 01. Its
[cleanly cracked manifest](https://raw.githubusercontent.com/mamedev/mame/master/hash/apple2gs_flop_clcracked.xml)
identifies the 12-Jan-89 version as ROM 03 compatible and attributes the
original incompatibility to its copy-protection routines. This is evidence
for a known software incompatibility, not proof that the owner's particular
ROM 03 crash has the same cause. A crash with the known compatible revision
would need a separate trace.

## Exact game evidence

The existing local eXo collection contains `Arkanoid (1988).zip` with an
819,200-byte `Game.po`. A private copy was inspected under ignored
`build/arkanoid-diagnosis/`; game and ROM bytes must remain out of Git.

- Disk SHA-256: `a3a895d7a0ed7bd38a00fd8c65b4ace18a9a79fe5583aaa4af0f8b5ed9e36c38`.
- Extracted `ARK`, 119,628 bytes, SHA-256:
  `16afb1374e597733e882da5466ea7fa0e5cd396a98572f27611ca0d412c24e78`.
- `static/inspect_game.py` extracts the ProDOS files and OMF constant records.
  `static/static-evidence.json` records hashes, offsets and instruction windows.
  `static/static-analysis.json` records annotated instructions and OMF offsets.

In `ARK` OMF segment 1, offset `$0A5F` writes `$57` to `$E0C035` and sets bit
4 of `$E0C036`. This enables SHR shadowing, inhibits the alternative HGR/aux
shadow paths and enables shadowing from banks beyond 00/01.

At segment offset `$1558`, the game selects data bank 03, calls that routine,
then performs paired loads and stores at the same addresses: `$2000,X`,
`$20A0,X`, `$2140,X` and other screen positions. A second routine at `$1DAB`
does the same for lower screen positions starting at `$93A0,X`. These writes
leave the background buffer unchanged; their display effect depends on the
hardware mirror into E1. The adjacent drawing routine at `$15B2` uses explicit
long stores to `$E12000,X`, `$E120A0,X` and `$E12140,X`, so drawing remains
visible even when background restoration fails.

These are offsets within the decoded OMF segment, not claimed live CPU PCs.
The reference emulator reached the Funtime Arcade splash, but gameplay and
the ROM 03 crash were not reproduced there. The game-path evidence above
comes from static instruction analysis.

## Core fault

The pre-audit `hdl/core/apple_iigs/ports/mmu.sv` defined `shr_master_shadow` with the condition
`bank_phys == 8'h01`. The extended-bank branch reuses this expression under
`shadow_all && we`. Consequently, a bank-03 write cannot select SHR shadowing.
With Arkanoid's `$57` setting, HGR shadowing cannot compensate either.

The same condition exists in the pinned source at
`third_party/apple-iigs/upstream/rtl/mmu.sv`, commit
`9adbba8622f378253765b7d438b6cbcc4d03fc57`. This defect is inherited from that
revision. It is not introduced by the Multitini frame scaler or ARM firmware.

The production correction recognizes installed odd banks for SHR writes when
the all-bank path is enabled, preserving ordinary bank-01 operation and inhibit
controls. It follows KEGS precedence and is reproduced by
`scripts/port_apple_iigs_core.py`; the full audit records related corrections.

## Validation boundaries

The exact-game directed test is
`build/arkanoid-diagnosis/tb_arkanoid_shadow_restore.sv`; reproduce it with
`python build/arkanoid-diagnosis/run_arkanoid_shadow_restore.py`.
Its receipt is `build/arkanoid-diagnosis/arkanoid-shadow-validation.json`.
It uses the game's `$57` shadow setting and bank-03 accesses with synthetic
pixel bytes, under both ROM-select settings:

| MMU input | Decode mismatches | Background restored after direct E1 sprite write |
| --- | --- | --- |
| Pre-audit production source | 10 of 18 | No, under either ROM setting |
| Scratch copy with the SHR bank predicate corrected | 0 of 18 | Yes, under both ROM settings |

The scene draws `$AA` directly into E1 over a `$44` background, then reads and
rewrites `$44` in bank 03. The old production leaves E1 at `$AA`; the corrected scratch
copy restores `$44`. Normal bank 01, disabled all-bank shadowing, inhibited
SHR and even-bank controls still pass. This original diagnostic used a copied
MMU under the ignored directory; it remains as failing-baseline evidence.
The repository MMU regression now repeats the scene without private media, and
the integration regression executes the load/store restore on the 65816 RTL
model at stock and maximum speed. No AUXHGR interpretation is needed for it.

The initial Verilator diagnostic in
`build/arkanoid-diagnosis/mmu-shadow-validation.json` reproduces missing SHR
mirrors from extended odd banks. Its additional AUXHGR comparisons disagree
with GSSquared but are not treated as established defects: the hardware
documentation and that reference implementation differ on those rules.

The existing SHR edge/fill test passes six NTSC/PAL 320/640/fill cases and
2,285,568 RGB565 pixel checks. Its receipt is
`build/arkanoid-diagnosis/shr-edges/validation.json`. This checks pixel rendering,
not whether CPU background writes reach video RAM.

The acceleration-gate test derives part of its expected result from the
MMU's own `slowram_ce`; those cases cannot independently validate the MMU
shadow truth table. Earlier Arkanoid ROM1/ROM3 boot tests also stop short of
gameplay validation, as recorded in `APPLE_IIGS_ACCELERATION.md`.

After the full audit and matching SmartPort ROM fix, the same private disk
passes bounded HDD bootstrap simulation with both ROM 01 and ROM 03. Each
120-million-tick run reaches the disk boot block, native IRQ dispatch and
more than 8.6 million RAM instructions; ROM01 completes 1,540 block reads
and ROM03 completes 1,464. The [audit](APPLE_IIGS_MMU_AUDIT.md) identifies
the exact receipts. This does not establish gameplay or reproduce the
owner's ROM03 crash.

The native floppy fixture stalls after 41 completed requests. A paired
pre-audit ROM03 run reaches the identical IWM poll with unchanged media,
other captured sources and simulator arguments, so this observed failure
predates the MMU changes. Native floppy gameplay remains unvalidated.

Hardware acceptance of a correction requires rebuilding and qualifying the
IIgs FPGA, installing the selected image, and testing moving objects and
background restoration on the board. The full MMU audit also requires the
matching rebuilt SmartPort `hdd.rom` to release internal C800 ownership before
entering slot expansion ROM. It does not inherently require an ARM firmware
change. ROM 03 acceptance must identify the game revision separately.
