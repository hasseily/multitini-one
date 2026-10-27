# C64 EasyFlash

Status, 2026-10-27: stages 0-4 are implemented, committed, and packaged as
M0.7.1dev3 and M0.7.1dev4. M0.7.1 reuses the dev4 C64 images unchanged; the
owner reported it validated on the board on 2026-10-27. XBank (type 33) stays
rejected by the owner's choice. See [As built](#as-built-stages-1-4) for
where the implementation differs from the plan below.

## Goal

Load and run EasyFlash CRTs on the C64 PAL and NTSC images. Let C64 software
program the flash. Keep those writes across power cycles. Free fabric before
adding logic, because the C64 images are at the slice limit.

## Known facts

### Existing code

- The mapper port already has EasyFlash. `hdl/core/c64/ports/cartridge.sv`
  handles cases 32 and 33: a 6-bit bank register at `$DE00`, a mode register
  at `$DE02` decoded by A1, and Ultimax boot through `cart_boot`. The adapter
  ties `cart_boot` to 1 (boot position). The GAME/EXROM equations match the
  VICE mode table entries checked so far with the jumper in its default
  position. All eight `$DE02` combinations still need a bench.
- The mapper places `$DF00-$DFFF` RAM through `get_bank(..., ram)` in the
  cartridge RAM region, so it would live in PSRAM at `0x011F00`.
- MiSTer's AM29F040 model (`third_party/c64/upstream/rtl/am29f040.sv`,
  module `ez_rom`) is compiled and instantiated in the mapper. It assumes
  MiSTer SDRAM timing: program reads the old byte at a fixed three clocks,
  erase writes one byte per `io_cycle`, and writes reach only banks present in
  the CRT (`lobanks_map`/`hibanks_map`).
- dev8 added Ultimate UCI control command `0x20` (EasyFlash sector erase,
  `firmware/platform/xilinx/c64_uci_control.c`) and a 64-bit erase engine in
  `multitini_c64_cart_memory.sv`. The engine fills one 64 KiB sector with
  `$FF` in the canonical layout: `0x200000 + bank * 16 KiB + ROMH * 8 KiB`.
  It runs only while UCI holds a paused core (`erase_allowed` in the adapter).
- The loader requires CHIP type 0, ordered and contiguous banks, and fixed
  per-type layouts. EasyFlash CRTs are often sparse, and many use CHIP type 2
  (flash).
- Firmware reads the whole CRT into a 2 MiB buffer before validating and
  streaming it (`c64_content_service.c`).
- GBC already has a save path to reuse: `multitini_gameboy_save.sv` marks
  writes dirty, flushes after a quiet interval or at an eject barrier, and
  reads a snapshot through the common memory service. `persistent_save.h`
  writes a journaled `NAME.sav` beside the content.
- A loaded cartridge forces the exact 1 MHz CPU path. Cartridge turbo (M7) is
  separate work.

### Fabric baseline

From `build/c71dev2-fpga/pal/reports/utilization.rpt` (routed, PAL):

| Instance | LUTs | FFs |
| --- | ---: | ---: |
| Whole design | 46,869 | 38,618 |
| `cartridge_i` (mapper) | 1,056 | 1,314 |
| of which `cartridge_i` own logic | 413 | 1,131 |
| of which `ez_rom` (two AM29F040 models) | 643 | 183 |
| `cart_memory_i` | 226 | 153 |
| `content_loader_i` | 1,712 | 776 |

The mapper's own FFs are almost all bank tables: `lobanks`/`hibanks` are
64 x 7 bits each (896 FFs) and `lobanks_map`/`hibanks_map` are 128 FFs.

## Reference behaviour

Read from the commits already pinned in `provenance/sources.lock.json`. These
files are not yet imported under `third_party/`.

VICE 3.10 (`4d283a2e`, `vice/src/c64/cart/easyflash.c`):

- Both chips are emulated as AM29F040B (`FLASH040_TYPE_B`) through
  `flash040core`. Stores use offset `bank * 0x2000 + (addr & 0x1fff)`.
- `$DF00` RAM is 256 bytes, readable and writable, and filled with a pattern
  at power-up, not zeros.
- `$DE00` keeps 6 bank bits. `$DE02` keeps `value & 0x87` (LED, mode, EXROM,
  GAME). The registers are write-only; reads are not driven.
- The jumper resource `EasyFlashJumper` defaults to 0, which boots the
  cartridge in Ultimax mode.
- `EasyFlashWriteCRT` and `EasyFlashOptimizeCRT` both default on. On detach,
  VICE writes the CRT back and skips chips that are all `$FF`.

Ultimate (`d43546fe`, `software/io/c64/c64_crt.cc`):

- Type 32 is supported. Type 33 is `CART_NOT_IMPL`.
- Cartridge memory is first filled with `$FF`. Chips go to `bank * 16 KiB`,
  plus 8 KiB for ROMH. That is the same layout as our erase engine.
- Ultimate does not emulate the flash command set. If bank 0 offset `$1800`
  (`$B800`) starts with ASCII `eapi`, it replaces those 768 bytes with its
  own EAPI. That driver erases through UCI `0x20`. On save, Ultimate writes the
  original EAPI back into the file.
- Subtype 0 sets `CART_PROHIBIT_IO` and requires `CART_UCI_DE1C`, which moves
  UCI to `$DE1C`. Subtype 1 prohibits everything but the REU.

## Approach

Emulate the AM29F040 command set, as VICE does, instead of patching EAPI, as
Ultimate does:

- Unmodified CRTs and tools that program the flash directly keep working.
- No third-party EAPI binary needs to be shipped or licensed.
- Flash writes do not depend on UCI.

UCI `0x20` stays available for software written for Ultimate.

Store the flash in the canonical layout. Then the erase engine, UCI `0x20`,
the flash front end and the save path all use the same address arithmetic,
and the mapper needs no bank-translation tables.

## As built (stages 1-4)

### Load and run

- **Firmware (`c64_content.c`).** Accepts type 32 subtype 0 with sparse
  banks 0-63 in any order, CHIP type 0 or 2, ROML at `$8000`, ROMH at `$A000`
  or `$E000`, and 16 KiB packets at `$8000`. It rejects a repeated bank half,
  bank 64 or higher, other addresses and sizes, and other subtypes.
  - `mt_c64_easyflash_expand` builds the 1 MiB canonical image, filled with
    `$FF`.
  - `mt_c64_easyflash_stream` writes the dense 128-packet CRT that the loader
    accepts (EXROM 1, GAME 0, CHIP type 0).
- **Capability.** Bit 21 of `C64_EXPANSION_CAPS` (`CART_EASYFLASH`), checked
  before any reset or stream.
- **Loader.** Type 32 must be exactly 128 packets: packet `i` is bank `i/2`,
  at `$8000` when `i` is even and `$A000` when it is odd.
- **Mapper.** Case 32 uses `bank_lo = {bank,0}`, `bank_hi = {bank,1}`. With
  `$DE02` M=0 the boot jumper drives GAME (stage 4); the coexist bench checks
  all eight `$DE02` values in both jumper positions against the VICE table.
  `$DF00-$DFFF` is a 256 x 8 distributed RAM returned through
  `IO_rd`/`IO_data`; it starts at zero.
- **Expansions.** Policy is `010`: REU and Ultimate Audio are suppressed, and
  UCI moves to I/O1 (stage 4).

### Flash writes

- **Front end (`multitini_c64_ef_flash.sv`).** Two AM29F040B command
  decoders and one operation engine.
  - Commands: autoselect (`01`/`A4`), program (AND into the stored byte),
    sector erase, chip erase and `F0` reset.
  - Status reads: DQ7 polling, DQ6 toggle on every read and DQ3 during erase.
    The adapter returns them in place of memory data while a chip is busy or
    in autoselect.
- **Ultimax only.** Commands are accepted only in Ultimax mode, where the C64
  asserts ROML/ROMH on writes. In 8 KiB and 16 KiB modes, writes reach C64
  RAM only.
- **One step per CPU access.** A cartridge access that misses the cache stalls
  on its request clock and is issued again when the line arrives. The front
  end sees only the retiring issue (`ram_ce && !cart_stall`); otherwise one
  write would step a command twice and break the unlock sequence, and DQ6
  would toggle twice per read. On 2026-10-27 the coexist bench showed each
  missed cartridge read issued twice (stalled, then a hit). The bench's own
  writes now occupy one native cycle each, as one `STA` does, and its erase
  check waits for the chip to finish, as software polling does.
- **Bridge (`multitini_c64_cart_memory.sv`).** It has a byte port and a
  flash-erase port.
  - Priority is loader, machine, erase, then flash bytes, so the C64 runs
    during an erase.
  - The cache drops only lines that an erase beat or flash write touches.
  - New content stops a flash erase.
  - A flash erase does not set the UCI erase busy bit, so it never triggers
    the UCI hold pause.
- **Timing.** An operation completes as soon as cartridge memory is updated;
  real program and erase times are not modelled.
- **Not built: dirty bits.** The planned per-sector dirty bits were replaced
  by the journal below.

### Persistence

- **Journal instead of readback.** The plan's PSRAM readback over a C64 media
  slot was replaced. The front end queues each accepted operation (32
  entries) before memory changes. One `multitini_async_mailbox` carries it to
  the control clock as `C64_EF_JOURNAL` (0x17C).
  - Firmware pops an entry by writing back the word it read.
  - A full queue holds the next flash operation, so no entry is lost.
- **Mirror and save.** Firmware keeps the canonical image, applies each entry
  with `mt_c64_easyflash_apply` (the same AND/erase rules), and saves the
  whole image to `NAME.sav` beside the CRT through `mt_persistent_save_*`.
  - The save happens once journal entries have been quiet for 1.5 s.
  - A load uses `NAME.sav` when present, otherwise the CRT.
- **Flush points.** These drain the journal with the guest held, then save:
  - loading anything over an attached EasyFlash;
  - detach;
  - reboot;
  - `supervisor_quiesce` (core switch and USB export).
- **Failure.** A failed save returns `MT_C64_CONTENT_SERVICE_SAVE_FAILED` and
  keeps the cartridge attached rather than discarding flash changes.
- **Menu.** Media shows "Cartridge save": Saved to SD, Changes pending or
  Write failed. There is no revert action; deleting `NAME.sav` restores the
  CRT.

### Stage 4

- **UCI at `$DE1C`.** Ultimate's command interface decodes an 8-byte slot
  whose base its firmware moves from `$DF18` to `$DE18` for EasyFlash
  (`CART_UCI_DE1C`). Here the adapter selects I/O1 instead of I/O2 while
  EasyFlash is attached, so the five registers sit at `$DE1B-$DE1F` (ID,
  control/status, command, response, status). They take priority over the
  EasyFlash register mirrors at those addresses; `$DE00`/`$DE02` and the
  other mirrors are unchanged. UCI follows its normal setting and unlock;
  the earlier refusal is gone.
- **CRT export.** Media > "Export cartridge" drains the journal and writes
  `NAME-saved.crt` beside the CRT through a temporary file, replacing an
  earlier export of that name. It is a type 32 CRT with the original CRT's
  title, one CHIP packet (type 2, flash) per 8 KiB half that is not all
  `$FF`, ROMH at `$A000`. `NAME.sav` and the CRT are unchanged. The export
  loads here as well (`mt_c64_easyflash_export`).
- **Boot jumper.** Media > "Boot jumper" toggles `C64_PERIPHERAL_CONTROL`
  bit 16. Disable releases GAME when `$DE02` M=0, so a reset starts BASIC
  with the cartridge hidden and software can still enable it. The setting
  applies at the next reset and lasts until the C64 image is reloaded; it is
  not saved, so a power cycle returns to Boot.
- **LED.** `$DE02` bit 7 is latched in the mapper and reported in
  `C64_DRIVE_STATUS` bit 10, with the 1541's LED (bit 8) and spindle (bit 9),
  behind capability bit 22 (`DRIVE_ACTIVITY`). The generic drive-activity
  badge shows it as unit `EF` (see [UI services](UI_SERVICES.md)).

### Fabric

Out-of-context synthesis on 2026-10-26: the mapper grows from 341 to 415
LUTs (32 of them the `$DF00` RAM), the flash front end is 141 LUTs and 68 FFs,
and the bridge grows from 214 to 332 LUTs and from 155 to 170 FFs. Together,
about 330 LUTs and 85 FFs, plus one 31-bit mailbox.

### Validation

M0.7.1dev3 (2026-10-27):

- **Passed:**
  - `tb_c64_ef_flash` (29 checks) and the extended `tb_c64_cart_memory`;
  - `tb_control_regs` in four capability variants, including the production
    value `0x807FFF7F` (jumper bit, journal pop rules);
  - host tests (120), including `test_c64_content` (export round trip),
    `test_c64_content_service` (save, flush, failure, reload and export),
    `test_core_frontend` (Media rows) and the new `test_drive_activity`;
  - the Python suite (362 tests);
  - the full C64 simulation batch (56 runners). The coexist bench's
    `easyflash()` scenario loads the 1 MiB stream through the real loader
    and covers the 16 `$DE02` modes in both jumper positions, the LED,
    `$DF00` RAM, UCI at `$DE1B-$DE1F`, autoselect, program, sector erase and
    warm reset through the CPU pins. Two negative controls check the UCI
    mirror mask and the jumper equation.
- **Test time:** `test_c64_cart_coexist.py` took 67 minutes, and each
  standard sat at about 885 s against its 900 s limit. The EasyFlash checks
  take seconds. The time went into streaming about 5 MB of CRT data through
  the loader in the full machine simulation (about 20,000 clocks/s): two
  2 MiB capacity images per standard, then the 1 MiB EasyFlash image. The
  loader wrote each payload byte as its own memory write. Each EasyFlash
  negative control also replayed everything before `easyflash()`.

M0.7.1dev4 changes the loader and the test, not EasyFlash itself:

- The loader collects CRT payload into 64-bit words and writes one word per
  8 bytes. `tb_c64_content_loader` checks byte totals, word totals and that
  no partial word is written.
- Bulk streams keep `load_valid` high between bytes.
- The 2 MiB capacity streams run on PAL only.
- The EasyFlash negative controls run setup and `easyflash()` alone
  (`+easyflash_only`), after one unmutated scoped run passes.
- **Measured, 2026-10-27, idle machine:** the runner passes in 18 minutes,
  from 67. PAL takes 324 s (was 882 s), NTSC 128 s, the EasyFlash-only run
  79 s, and each EasyFlash negative about 78 s (was about 870 s). About 35 s
  of each run is setup: ROM loads and the VIC's first frame.
- **Board:** the owner reported M0.7.1 validated on the board (2026-10-27).

## Stage 0: mapper cleanup (fabric)

Do this first. It removes logic the loader can never reach and gives EasyFlash
room.

- Delete mapper cases the loader never admits: 1 (Action Replay), 9 (Atomic
  Power), 20 (Super Snapshot 5), 36 (Retro Replay), 60 (GMod2), 83 (BMP Data
  Turbo), 99 (GeoRAM), and 32/33 (EasyFlash/XBank). The EasyFlash case used
  the bank tables and the flash model, so stage 1 adds it back in canonical
  form; the MiSTer version stays readable in Git history and in
  `third_party/c64/upstream/rtl/cartridge.v`.
- Delete what only those cases use:
  - the RAM-overlay paths: `romL_we`, `romH_we`, `IOE_wr_ena`, `IOF_wr_ena`
    and the RAM branch of `get_bank`;
  - the Retro Replay register readback: `IOE_rd`, `IO_data`, `clock_port`,
    `reu_map` and `allow_bank`;
  - `geo_bank`.
  Keep the FC3 and FC1/2 NMI and freeze logic, because FC3 software uses it.
- Remove `ez_rom`, its `mem_req`/`mem_cycle` wiring and `am29f040.sv` from
  `hdl/hdl_sources.txt`. EasyFlash is not admitted today, so its behaviour does
  not change.
- Replace `lobanks`, `hibanks` and both maps. The loader already stores
  packets in order, so every admitted type uses bank number = slot except type
  0. For type 0, keep two 7-bit slot registers, one for ROML and one for ROMH
  of bank 0. `bank_cnt` stays for the types that mask with it. Both slot
  registers clear when a load starts. A type 0 Ultimax image has no ROML
  packet, and with the tables its ROML slot was whatever an earlier
  cartridge left there.
- Measured 2026-10-26 with out-of-context synthesis of `cartridge` alone:
  1,796 LUTs and 1,316 FFs before, 341 LUTs and 85 FFs after. The routed
  in-design mapper was 1,056 LUTs and 1,314 FFs, so expect about 700-800
  LUTs and 1,230 FFs saved in a full build. The next routed build confirms
  this.

Tests:

- Re-run every mapper bench (`tb_c64_adapter_load_sid`,
  `tb_c64_cart_coexist`, `tb_c64_cart_video`, `tb_c64_cart_memory`) and the
  cartridge host tests.
- Add a loader-to-mapper bench for type 0 in its 8 KiB, 16 KiB (one packet),
  16 KiB (two packets) and Ultimax layouts. Done: `normal_layouts` in
  `tb_c64_cart_coexist.sv`. It also checks that an 8 KiB-mode write reaches
  RAM and an Ultimax ROML write does not. `test_c64_cart_coexist.py`
  requires its `NORMAL_LAYOUT PASS` line.
- Compare the synthesis hierarchy report before and after. Check that no
  admitted type changed its address output.

## Stage 1: load and run (ROM and RAM, no flash writes)

Firmware (`firmware/common/c64_content.c`, content service):

- Accept type 32 with subtype 0. Reject other subtypes and type 33.
- Accept CHIP type 0 or 2. ROML loads at `$8000`. ROMH loads at `$A000` or
  `$E000`. Packets are 8 KiB; a 16 KiB packet at `$8000` is split into ROML
  and ROMH. Banks 0-63 may come in any order, with gaps. Reject a repeated
  bank half and bank 64 or higher.
- Normalize into a 1 MiB canonical image filled with `$FF`. Stream it as a
  dense synthetic CRT with 128 packets, CHIP type 0, in the order bank `b`
  ROML at `$8000`, then bank `b` ROMH at `$A000`. That is 1,050,688 bytes,
  below `MT_SIZE_C64_CRT_MAX`. Keep the canonical image in its own static
  buffer, because stage 3 needs it.
- Add a `C64_EXPANSION_CAPS` bit for EasyFlash. Bit 21 appears free; confirm
  this in `spec/interface_v0.json`. Check the bit before any reset or
  streaming, as is done for `CART_ROM_EXT`.

HDL:

- Loader: admit type 32 with one layout rule. Packet `i` is bank `i / 2`, at
  `$8000` when `i` is even and `$A000` when it is odd, and every packet is
  8 KiB. The image is complete at exactly 128 packets. Sequential storage puts
  slot `2b` at ROML and slot `2b + 1` at ROMH, which is the canonical layout.
  Firmware writes a fixed EXROM/GAME header for the synthetic stream, and the
  loader accepts only that value.
- Mapper: type 32 uses `bank_lo = {bank, 0}` and `bank_hi = {bank, 1}`.
- Expansion policy `000`: REU, UCI and Ultimate Audio are suppressed while
  EasyFlash is attached, following Ultimate subtype 0.
- Move `$DF00-$DFFF` RAM into a local 256 x 8 RAM and return it through the
  mapper's `IO_rd`/`IO_data` path. EasyFlash software runs bank-switch code
  from `$DF00`. In PSRAM, every cache-line miss there would stall the machine.
- Flash command writes are ignored in this stage. EAPI does not detect a chip,
  so titles that need saves will report that they cannot save.

Tests:

- Host:
  - sparse, out-of-order and repeated banks;
  - bank 64;
  - wrong load addresses and wrong sizes;
  - CHIP types 0, 1 and 2;
  - 16 KiB packets;
  - a byte-exact canonical image;
  - older FPGA images rejecting EasyFlash before any reset.
- Loader bench: accept the 128-packet stream; reject a short stream, a wrong
  order and a wrong address.
- Mapper bench:
  - all eight `$DE02` combinations against the VICE table;
  - bank switching;
  - Ultimax boot;
  - VIC Ultimax fetches from ROMH;
  - write-only registers;
  - `$DF00` RAM;
  - warm reset.
- Coexistence bench: REU, UCI and Ultimate Audio are suppressed while
  EasyFlash is attached and restored on eject.
- Board: owner-supplied titles that only boot, in PAL and NTSC.

## Stage 2: flash writes (session only)

- Add `multitini_c64_ef_flash.sv` beside the cartridge memory bridge. The
  mapper exports its bank register.
  - It has two AM29F040B command decoders, one per chip, and one shared
    operation engine.
  - Match VICE `flash040core.c` type B: `AA`/`55` unlock at `555`/`2AA` (low
    11 bits), autoselect manufacturer `01` and device `A4`, byte program (AND
    with the old value), 64 KiB sector erase (8 banks of one chip), chip
    erase, `F0` reset, and DQ7 polling, DQ6 toggle and DQ3 status.
  - Reads of a busy chip return status and do not touch memory.
- Give the engine its own requester in `multitini_c64_cart_memory.sv`, with
  priority loader > C64 > flash engine.
  - The C64 keeps polling, and keeps reading the other chip, without new
    stalls.
  - Reuse the 64-bit erase loop. Allow erases started by the flash front end
    while the core runs.
  - Program reads the line, ANDs the byte, writes one byte, then invalidates
    the cache line.
- Report completion when the memory work finishes. Real erase times (about
  1 s) are not emulated unless a title needs them.
- Keep 16 per-sector dirty bits, two chips x 8 sectors. UCI `0x20` also sets
  them.

Tests:

- One bench case per command, plus:
  - invalid and interrupted sequences;
  - interleaved writes to the other chip;
  - status polling;
  - cache coherence;
  - priority of C64 runtime reads during an erase.
- Writes outside Ultimax mode: `multitini_c64_buslogic.vhd` asserts ROML on
  writes in 8 KiB and 16 KiB modes. Check how VICE hooks the EasyFlash ROML
  store (`c64cart.c`) before choosing the behaviour.
- A VICE differential run with a small flash test program built from source.
- Regression runs of the stage 0 benches.

## Stage 3: persistence

- Save as `NAME.sav` beside the CRT. It holds the 1 MiB canonical image,
  written with `mt_persistent_save_*` and erased value `$FF`. On load, a valid
  `.sav` replaces the CRT's ROM data.
- An HDL save module modelled on `multitini_gameboy_save.sv`:
  - flush after a quiet interval;
  - flush at eject, reset, core switch and USB export barriers;
  - read back only dirty sectors, with a short core pause per 64 KiB sector,
    over a new C64 media slot.
- Menu: show the **Saved** state, and add a revert action that deletes the
  `.sav`.

Tests:

- Journal recovery and power loss during a commit.
- A short, oversized or mismatched `.sav`.
- Eject during a flush.
- A bench for dirty-sector snapshots.
- A board power-cycle test with an owner-supplied title that saves through
  EAPI.

## Stage 4: optional parity

Built for M0.7.1dev3 except XBank; see [Stage 4](#stage-4) as built.

- UCI at `$DE1C` while EasyFlash is attached (Ultimate `CART_UCI_DE1C`).
- Export to a `.crt` that skips empty chips, as VICE and Ultimate do.
- LED bit 7 on the on-screen display.
- A boot-jumper option.
- XBank (type 33): not built.

## Decisions (owner, 2026-10-26)

1. Save format: a `NAME.sav` sidecar holding the whole canonical flash image.
   The user's CRT is never modified.
2. Stages 1-3 ship together.
3. The UCI unlock override is refused while EasyFlash is attached, until
   stage 4 adds UCI at `$DE1C`.

On 2026-10-27 the owner asked for stage 4 without XBank, with the LED shown
by a drive-activity indicator generic to every drive, after Appletini's
storage widget.

## Risks

- Fabric: stage 0 must land and be measured before stages 1-3 add the local
  RAM, the flash front end and the save module.
- PSRAM cache: one 8-byte line serves both CPU and VIC reads. A title where
  the VIC fetches ROMH in Ultimax mode while the CPU reads ROML could miss on
  every access. Count `memory_wait` cycles per frame on a bank-heavy title.
  The fallback is a separate line for VIC fetches.
- Load time: the loader writes one byte per PSRAM transaction, so 1 MiB per
  load. Packing eight bytes per write would help every large cartridge.
- Test content: no EasyFlash titles enter the repository. Use synthetic CRTs.
  Check the EAPI source license before vendoring it for tests.

## Sources

- VICE `4d283a2e7dd59b7e378524878e81ecc7826b700c`:
  `vice/src/c64/cart/easyflash.c`, `flash040core.c`, `c64cart.c`. Import
  them as reference-only files before stage 2.
- Ultimate `d43546fe25b26cb332f43f00219d33153dfe44f8`:
  `software/io/c64/c64_crt.cc`,
  `software/io/command_interface/control_target.cc`.
- MiSTer C64 core (pinned in `third_party/c64`): `rtl/cartridge.v`,
  `rtl/am29f040.sv`.
