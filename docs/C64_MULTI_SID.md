# Multitini One C64: Multi-SID, up to 8 SIDs (design)

Design only. Nothing here is implemented or tested.

- **Date.** 2026-10-24.
- **Repository.** `multitini-one`, branch `c64-turbo` at `c7c2fba` (M0.7.0dev6, C64 image `4D00C706`, ABI 0.24). The branch has since moved to `3df6d04` (M0.7.0dev7, image `4D00C707`, ABI 0.25, released). Every line anchor and the budget baseline in this document were checked against `3df6d04`, and line numbers are for that commit. No repository file other than this document was changed. Nothing was built or simulated.
- **Place in the plan.** [C64_U64_EXTENSIONS_PLAN.md](C64_U64_EXTENSIONS_PLAN.md) (called **Plan** below) is the overview and shared frame: ABI 0.26, register offsets, settings locations, menu arguments, the address and priority map, milestones XM0-XM8, gates and budget ceilings. The sibling designs are [C64_SUPERCPU_DETECT.md](C64_SUPERCPU_DETECT.md) (**SCPU**; its `$D0BC` compare loads the same fast-read address nets, §5), [C64_UCI.md](C64_UCI.md) (**UCI**) and [C64_TITLE_CONFIG.md](C64_TITLE_CONFIG.md) (**CFG**). This document is the multi-SID design. It fills milestones XM6 (joint fit experiment) and XM7 (multi-SID) and owns decision record **Q24**. Plan sections are cited by number and topic, for example "Plan §3.4 (SID table)".
- **Citation base.**
  - Repository paths are relative to the repository root. Short names: `top` = `hdl/board/multitini_one_top.sv`; `adapter` = `hdl/core/c64/multitini_c64_adapter.sv`; `fpga64` = `hdl/core/c64/ports/fpga64_sid_iec.vhd`; `sid_top` = `hdl/core/c64/ports/sid_top.sv`; `main.c` = `firmware/platform/xilinx/main.c`. `main.c` anchors name a function, because its lines move often.
  - TD = `docs/C64_TURBO_DESIGN.md`, UAD = `docs/C64_ULTIMATE_AUDIO_DESIGN.md`, FSID = `docs/C64_TURBO_FAST_SID.md`, EXP = `docs/C64_EXPANSIONS.md`. They are cited by section (TD §4.3) or by fact number (FSID F19), because their line numbers change.
  - `1541ultimate <path>:<lines>` = GideonZ/1541ultimate at `d43546fe` (GPL-3.0, except `fpga/sid6581`, see 0.3).
  - `1541u-documentation <path>:<lines>` = GideonZ/1541u-documentation at `2da134b`.
  - `VICE <path>:<lines>` = VICE-Team/svn-mirror at `4d283a2` (VICE 3.10, the commit of `third_party/vice_reference`; `src/sid/` is not in that subset).
  - `SIDDetector-II <file>:<lines>` = MichaelTroelsen/SIDDetector-II at `0476dde` (documentation files only).
  - `TT8 cfg` = `tt8-ultimate.cfg` in `u64_124.zip` (Ultimate 64 firmware 1.24 package on CSDb, which ships The Tuneful Eight). `Heartbeat cfg` = `Heartbeat_UE2_64MHz.cfg` attached to 1541ultimate issue #922. Neither file may be committed (§9.3).
  - `#nnn` = an issue in GideonZ/1541ultimate.
  - `dev7 <report>:<line>` = the M0.7.0dev7 release evidence in `build/Multitini-070dev7/evidence/c64-pal/reports/` (`c64-ntsc/` for NTSC). `dev7 placed:<line>` = `build/c707p3/build/c64-pal/multitini_one.runs/impl_1/multitini_one_top_utilization_placed.rpt` (NTSC: `build/c707n2/build/c64-ntsc/...`). `build/` is not under source control, so these figures cannot be checked from the repository.
  - Design-stage research notes are not cited as evidence. A claim that rests only on such a note is tagged [I].
- **Tags.** **[V]** verified by reading a primary source (path:line). **[D]** documented in a design document or manual. **[R]** user report or third-party test result. **[I]** inference, including our own arithmetic. **[O]** open: needs a measurement, a source check or a decision. **USER** marks a decision for the user, with a recommended default.

Nothing in this document claims hardware support.

---

## 0. Summary

### 0.1 What software sees

- Defaults do not change. The "SID audio" row (Options argument 2) still offers Single, Dual $D420 and Dual $D500 (the default). Those layouts stay bit-identical to today (§3.9 E-1, E-2).
- Images with the multi-SID capability add the presets Triple, Quad and Eight ("Tuneful Eight") to that row. A new row "SID setup..." (argument 16) opens a sub-page for addresses, mirroring, per-SID volume, pan and model, master volume, headroom, DC removal and resampling (§3.4, §4.5).
- Up to 8 SIDs in `$D400-$D7FF`. The U64 items (UltiSID 1/2 Address, UltiSID Range Split, Auto Address Mirroring, SID Socket addresses) give the same SID addresses as on an Ultimate 64. The Tuneful Eight layout is `$D400 $D420 $D480 $D4A0 $D500 $D520 $D580 $D5A0`, each SID also answering at +$40 and +$200 (§3.3).
- Every SID returns its own OSC3 and ENV3. Paddles reach the SID that owns `$D400`; every other SID reads `$FF` for POT. A slot that no SID covers reads the lowest mapped SID, and a write there reaches no SID (§3.5).
- Per-SID volume and static pan, which the U64 cannot do (#664, #668). Per-SID 6581/8580 model when the image has the runtime 8580 capability (§3.6).
- The mix saturates instead of wrapping, with Auto headroom and, from 3 SIDs, DC removal (§3.7).
- A title `.cfg` and UCI LOAD_CONFIG (`$04 $50`) set SID layouts field by field through the title overlay (§3.10).
- UCI GET_HWINFO `$04 $28 $01` answers in the U64 shape (at most 4 frames). `$04 $28 $02` lists every SID (§4.6).

### 0.2 Architecture in a few lines

1. Firmware turns the U64 address rules (split, mirroring, sockets) and our per-SID overrides into a 32-slot table: one 8-bit SID mask per 32-byte slot of `$D400-$D7FF`, plus gains and output options. The HDL only indexes the committed table by address (§3.3, §4.1).
2. The table is written through `C64_SID_INDEX`/`C64_SID_DATA`/`C64_SID_COMMIT` (0x154-0x15C), streamed entry by entry over one async mailbox to `c64_clk`, and swapped in on one `c64_clk` edge: at the next `ce_1m`, or at once while the C64 is paused. The active sequence returns in `status_i` (§4.2, §5.1).
3. Engine E1: unchanged MiSTer `sid_top` pairs, 2 chips each. 4 SIDs first. 8 SIDs with E1 if the fit experiment passes; otherwise a hybrid that keeps pair 0 and serves SIDs 3-8 with a voice-serial engine (E3) (§6, U1).
4. With `C64_EXPANSION_CONFIG` bit 6 = 0, the reset default, decode, engine and mixer are today's text (§3.9 E-1).
5. Map mode mixes every SID and Ultimate Audio through a DSP gain stage into a 32-bit sum, one power-of-two headroom shift and a wide saturating clamp (§3.7).
6. The turbo fast read path decodes only the 3-bit read owner from A9..A5 and selects the owner's register candidates before one register case on A4..A0. This keeps new loads off the T65 address nets, which also drive the half-period island BRAM path (§3.9 E-5, §5.2).

### 0.3 Licence decision

- No UltiSID code. Every `fpga/sid6581/vhdl_source` file says it "is not supposed to be used in other projects without written permission from the author" [V 1541ultimate fpga/sid6581/vhdl_source/*.vhd:9-10]. The U64's 8-SID engine is not public [R #650; V 1541ultimate `external/` holds only binaries].
- The engine is the MiSTer SID already in the tree: `sid_top.sv` (GPL-2.0-or-later, assumed, as recorded today) with its CERN-OHL-S-2.0 filter [V hdl/core/c64/ports/sid_filter.sv:12-15].
- New RTL and firmware are original Multitini code under GPL-3.0-only.
- The U64 firmware address and mixer functions are GPL-3.0 [V 1541ultimate software/u64/u64_config.cc:1231-1255, 1342-1358, 2406-2465; the file has no header and relies on the repository licence, :1-6]. They serve only as a test-only oracle, imported byte-exact into `third_party/ultimate_firmware_reference/` and excluded from product builds (§4.2, §9.2).

### 0.4 Budget and timing baseline (dev7, PAL post-route)

| Resource | Used | Capacity | % | Source |
|---|---|---|---|---|
| LUT | 36,689 | 53,200 | 69.0 | [V dev7 utilization.rpt:25] |
| FF | 30,334 | 106,400 | 28.5 | same |
| BRAM tiles | 128 (119 RAMB36 + 18 RAMB18) | 140 | 91.4 | [V dev7 placed:106] |
| DSP | 33 | 220 | 15.0 | [V dev7 utilization.rpt:25] |
| Slices | 12,519 | 13,300 | 94.13 | [V dev7 placed:76] |
| Worst setup slack | +0.030 ns, fast island: `main_ram_i/character_reg` (falling edge) → `cpu_island_i/cpu_i/cpu/BAL_reg[8]` (rising edge), requirement 7.929 ns, 8 logic levels | | | [V dev7 timing_summary.rpt:166, 65745-65752] |
| 2-SID `sid_top` | 2,629 LUT, 1,963 FF, 2 RAMB36, 7 DSP | | | [V dev7 utilization.rpt:82] |
| NTSC image | 36,769 LUT; 12,362 slices (92.95 %); WNS +0.219 ns | | | [V dev7 c64-ntsc utilization.rpt:25, timing_summary.rpt:166; c707n2 placed:76] |

Facts:
- The repository gate for the fast island is +0.100 ns [V scripts/build_and_export_xsa.tcl:207]. The dev7 PAL image shipped under a one-build waiver: "PAL setup margin +0.030 ns is accepted for this build only; the repository default remains +0.100 ns" [V build/C64-4D00C707-070dev7-SD/README.txt].
- Slice headroom to the Plan's 95 % ceiling is 12,635 − 12,519 = 116 slices [I arithmetic].
- dev7 packs 2.93 LUT per slice (36,689 / 12,519) [I arithmetic].

Consequences for this design:
1. The Plan §10.3 ceilings (fast-island WNS at least +0.100 ns, slices at most 95 %) fail before any new RTL, so G-FIT as written cannot pass for any candidate. The Plan chooses between a timing-recovery milestone XM0 before XM3 that restores the PAL fast-island slack to at least +0.100 ns, and a per-milestone waiver policy (**USER**, Plan P2). This design needs that choice before XM6 and recommends XM0, because multi-SID adds load on the same path (point 3).
2. Every multi-SID candidate needs denser packing than today. §6.2 gives the density each needs to stay at or below 95 % slices.
3. The fast SID read decode reads the T65 address nets that also drive the half-period island BRAM path (§5.2). The decode is shaped to add as few loads as possible, and XM6 records the loads and the slack of both half-period legs for each candidate.

### 0.5 Regression gates (user rules)

- Stock REU behaviour does not change. Expand by Bonzai disks 1 and 2 (G-REU0) is a hard gate, also with the Eight preset active (BT1).
- The stock trace `tests/c64_ua/stock_bus.csv` must reproduce with every new input at its default. It contains REU DMA writes to `$D500` and a DMA read of `$D7F0` [V tests/c64_ua/stock_bus.csv:1849-1850, 1910], so the default must keep the legacy mode 3 decode. `C64_EXPANSION_CONFIG` bit 6 = 0 does that (Plan §6.5). G-STK runs at `SID_COUNT` 2 and at the image value (§9.4).
- 1- and 2-SID audio at the defaults stays bit-identical (G-AUD).
- Debug features are off by default and UART-only: `:sidstat` and the optional clip counters exist only inside a debug session (G-DBG).

### 0.6 Decisions that apply here

- **Q24 (this design):** up to 8 SIDs on the MiSTer SID, through the Plan allocations: registers 0x154-0x160, `C64_EXPANSION_CONFIG` bit 6 and `C64_EXPANSION_CAPS` bits 6 and 8-12 (Plan §3.2-3.4); `0:/multitini/c64_sid.bin`, per profile (Plan §4.1, P11, **USER**); no `$DExx/$DFxx` SIDs before XM8 (Plan P7, **USER**); and the XM6 fit experiment before XM7 (Plan §10.4, §12.1). The deviations from the U64 that this design takes are listed in §7; their TD-style D-numbers come from the multi-SID range D60-D69 of Plan §3.6.
- User rules: build on the MiSTer SID (licence finding, 0.3); fix U64 issues that matter instead of copying them; debug features off by default; stock REU behaviour unchanged; NTSC held until it closes at 64x, so board gates run on PAL first.
- The user reversed TD Q10 for same-name `.cfg` files. CFG owns the reader; this design registers the SID keys (§4.5) and defines how the SID part of the overlay behaves (§3.10).

---

## 1. Goal and scope

### 1.1 Goal

Up to 8 SIDs that C64 software and U64 `.cfg` files see as they would on an Ultimate 64: the UltiSID 1/2 addresses, "UltiSID Range Split", "Auto Address Mirroring" and the SID Socket addresses produce the same SID addresses. Built on the MiSTer SID that the core already uses. No UltiSID code.

### 1.2 In scope

- The internal model of 8 SID slots and how every U64 SID item maps onto it.
- Presets: 1, 2 ($D420, $D500; $DE00/$DF00 later), 3, 4 and 8 SIDs ("Tuneful Eight").
- Address decode in `$D400-$D7FF`: fan-out, read owner, mirrors, OSC3/ENV3 and POT per SID.
- Engine choice, resource estimates, fit experiment, and the fast read timing plan.
- Exactness rules for 1-2 SIDs, the turbo fast SID port and the edge stage.
- Mixer: per-SID gain and pan, headroom, DC removal, resampling.
- Entry layouts inside the Plan's SID table (Plan §3.4), the settings record, the menu sub-page, the `.cfg` keys, the SID part of the title overlay, and the UCI GET_HWINFO content.
- Tests and gates.
- Side finding: the POT tie (§3.11). It is separate from multi-SID.

### 1.3 Out of scope

- `$DExx/$DFxx` SID slots before XM8 (Plan P7). §3.8 gives the rules for later.
- A SID file player. The U64 player maps at most 3 SIDs [V 1541ultimate software/filetypes/filetype_sid.cc:99-134]; Multitini has none.
- Register-driven (dynamic) pan (#668 asks for it). Static per-SID pan is in scope.
- Changes to the sound of the SID engine (filter, envelope, noise). The engine stays MiSTer's.

### 1.4 Engine names

| Here | Plan (§10.1, P31) | Shape |
|---|---|---|
| E1 | E1-4, E1-8 | Replicate unchanged `sid_top` pairs |
| E2 | rejected (P31) | One `sid_top`-like engine with 4 chips sharing one filter and table (32-state schedule), ×2 |
| E3 | E3 hybrid 8 | Voice-serial engine |

---

## 2. U64 behaviour (facts)

### 2.1 Architecture

| # | Fact | Tag |
|---|---|---|
| U1 | The U64 SID engine is closed source. The public `fpga/sid6581` engine is the older 2-SID cartridge engine | [R #650; V 1541ultimate `external/` holds only binaries] |
| U2 | 8 SIDs = 2 UltiSIDs × up to 4 parts: "In total 8 SIDs, thus 24 voices and 8 independent filters" | [D 1541u-documentation config/multi_sid.rst:46-56] |
| U3 | Every `fpga/sid6581/vhdl_source` file says it may not be used in other projects without written permission. Nothing from it may be ported | [V 1541ultimate fpga/sid6581/vhdl_source/*.vhd:9-10] |
| U4 | The firmware that programs the decode is GPL-3.0 and readable: `get_sid_addresses`, `fix_splits`, `auto_mirror`, `setMixer` | [V 1541ultimate software/u64/u64_config.cc:1231-1255, 1342-1358, 2406-2465] |
| U5 | Part names: "UltiSID1-A".."UltiSID1-D", "UltiSID2-A".."UltiSID2-D" (index 0-7) | [V 1541ultimate software/u64/led_strip.cc:55-56] |

### 2.2 Address items

| # | Fact | Tag |
|---|---|---|
| A1 | 49 address choices: "Unmapped", "$D400".."$D7E0", "$DE00".."$DFE0" in $20 steps. One byte holds A11..A4: `$D400` = 0x40, `$DFE0` = 0xFE. Unmapped = base 0x01, mask 0xFE (never matches) | [V u64_config.cc:202-203, 218-235] |
| A2 | Items in `[SID Addressing]`: "SID Socket 1 Address", "SID Socket 2 Address", "Ext DualSID Range Split" (Off, A5..A9), "UltiSID 1 Address", "UltiSID 2 Address", "UltiSID Range Split", "Paddle Override", "Auto Address Mirroring". All four addresses default to `$D400`; Paddle Override and Mirroring default to Enabled | [V u64_config.cc:412-421] |
| A3 | Split names and bits: Off 0x00, "1/2 (A5)" 0x02, "1/2 (A6)" 0x04, "1/2 (A7)" 0x08, "1/2 (A8)" 0x10, "1/4 (A5,A6)" 0x06, "1/4 (A5,A8)" 0x12, "1/4 (A7,A8)" 0x18. One split serves both UltiSIDs | [V u64_config.cc:266, 320, 1240-1246] |
| A4 | `fix_splits` clears the split bits from the base and mask of each SID. A decode matches when `(A11..A4 & mask) == base` | [V u64_config.cc:1249-1255; sid_editor.cc:165] |
| A5 | Part from the split lines: 2-line splits give A5+2·A6, A5+2·A8 or A7+2·A8; 1-line splits give A (0) or B (1) | [V 1541ultimate software/u64/sid_editor.cc:166-181] |
| A6 | `auto_mirror`: after clearing split bits, for each line A5..A9, if every SID whose base is in 0x40..0x7F has the same (base bit, mask bit) pair, that bit is cleared in all their bases and masks. Sockets count even when disabled | [V u64_config.cc:2406-2465, called at :864-867 with no enable check] |
| A7 | The Tuneful Eight and Heartbeat Soundtracker use UltiSID 1 = `$D400`, UltiSID 2 = `$D420`, split "1/4 (A7,A8)", mirroring Enabled, sockets Unmapped. SIDs at `$D400 $D420 $D480 $D4A0 $D500 $D520 $D580 $D5A0` | [V TT8 cfg; Heartbeat cfg; D 1541u-documentation config/multi_sid.rst:46-56; R SIDDetector-II TODO.md:5-11] |
| A8 | Both files set "Combined Waveforms=8580" and "Filter Curve=8580 Lo" for both UltiSIDs | [V same files] |
| A9 | A write to an unmapped `$D4xx` once hung the U64 menu ("POKE 54272+100,0"); fixed in the FPGA for 3.15 | [R #733] |
| A10 | Read priority between overlapping SIDs and the value of unmapped reads are unknown | [O] |

### 2.3 Per-UltiSID settings and mixer

| # | Fact | Tag |
|---|---|---|
| M1 | Filter Curve (7 choices), Filter Resonance, Combined Waveforms (6581/8580) and Digis Level exist per UltiSID only; the 4 parts share them | [V u64_config.cc:424-433, 1257-1300] |
| M2 | Mixer sources are per UltiSID ("Vol UltiSid 1", "Pan UltiSID 1"); the parts of one UltiSID share one volume and one pan | [V u64_config.cc:435-457, 1342-1358] |
| M3 | Volume table: 31 steps, `volume_ctrl[24]` = 0x80 = 0 dB, 0xFF = +6 dB. `combine_mixer_gain(src, master)` = 0 if either is 0, else `min(255, (src·master + 0x40) >> 7)` | [V u64_config.cc:292-315] |
| M4 | Pan: 11 positions "Left 5".."Center".."Right 5"; `pan_ctrl` = {0, 40, 79, 116, 150, 181, 207, 228, 243, 253, 256}, a constant-power law (Center = 181/256 = −3 dB) | [V u64_config.cc:297-298, 317; I for the law] |
| M5 | `setMixer` writes byte 2i = `pan_ctrl[10−p]·g >> 8` (named `vol_right`) and byte 2i+1 = `pan_ctrl[p]·g >> 8`. With "Left 5" (p = 0) byte 2i carries the full gain, so byte 2i drives the left output after the FPGA channel swap | [V u64_config.cc:1350-1357; I for the physical side; R 1541ultimate commit 74c9e71ac6 "Swapped Left and Right channel"] |
| M6 | Spellings differ: "Vol UltiSid 1" but "Pan UltiSID 1"; " 0 dB" has a leading space | [V u64_config.cc:292-295, 437, 447] |
| M7 | The U64 removes the SID DC offset before its mixer ("The FPGA audio mixer sits after the point where that offset is removed") | [V u64_config.cc:1309] |
| M8 | Internal summing and headroom of the 4 parts inside one UltiSID source are unknown | [O] |

### 2.4 Reads

| # | Fact | Tag |
|---|---|---|
| R1 | Each part returns its own OSC3/ENV3 at its own address. SIDDetector-II found all 8 parts of the TT8 layout on a U64 (3 of 3 runs) | [R SIDDetector-II TODO.md:5-11; R #954] |
| R2 | Paddle Override (default Enabled): "the U64 will always read the internal paddle registers, regardless of the address settings and which SID is mapped where" | [D 1541u-documentation ultimate64-manual.rst:170-172; V u64_config.cc:419] |
| R3 | VICE returns real POT values only for chip 0; other chips fall back to engine values, and `$FF` for POT with sound off | [V VICE src/sid/sid.c:220-283] |

### 2.5 UCI GET_HWINFO (`$04 $28`, device 1) on the U64

- Reply: count, then 5-byte frames {lo, hi, lo2, hi2, type}. UltiSID 1 and UltiSID 2 are always frames 1 and 2; sockets follow only if enabled: `data[0] = 2 + (C64_SID1_EN_BAK ? 1 : 0) + (C64_SID2_EN_BAK ? 1 : 0)` [V 1541ultimate software/io/command_interface/control_target.cc:418-468].
- Type = 2 if the mask is 0xFE, else 3, OR 0x80; sockets 4/5 [V control_target.cc:431, 439, 452, 467].
- The base reported is the mirror-cleared `_BAK` value, and split parts are not listed [V :425-439; I]. An unmapped UltiSID reports `$D010` (base 0x01 → lo = 0x10, hi = 0xD0) [I from the code].
- Any device other than 0 and 1 gets INVALID PARAMS [V control_target.cc:380-394 and the final `else`]. `$04 $28` is marked deprecated upstream [D 1541u-documentation uci/control_target.rst:169].

---

## 3. Our design (software-visible behaviour)

### 3.1 Numbering

Internal SID n = 0..7 is shown to users as "SID n+1". It maps to the U64 parts so that the SID count can grow from 4 to 8 without renumbering, and so that the TT8 layout reads in address order:

| SID (user) | n | U64 part | TT8 address |
|---|---|---|---|
| SID 1 | 0 | UltiSID 1-A | `$D400` |
| SID 2 | 1 | UltiSID 2-A | `$D420` |
| SID 3 | 2 | UltiSID 1-B | `$D480` |
| SID 4 | 3 | UltiSID 2-B | `$D4A0` |
| SID 5 | 4 | UltiSID 1-C | `$D500` |
| SID 6 | 5 | UltiSID 2-C | `$D520` |
| SID 7 | 6 | UltiSID 1-D | `$D580` |
| SID 8 | 7 | UltiSID 2-D | `$D5A0` |

- Rule: n = 2·part + (UltiSID − 1) [I]. The U64 LED-strip index is part + 4·(UltiSID − 1) [V led_strip.cc:55-56]; firmware converts where needed.
- Legacy chips: today's left chip (chip 0) is SID 1 and the right chip (chip 1) is SID 2 [V fpga64:1008 `cs => sid_sel_r & sid_sel_l`].
- A 4-SID image has SIDs 1-4 (parts A and B of each UltiSID). Every split is accepted. Under a 1/4 split, parts C and D (SIDs 5-8) still take part in mirroring and are dropped after it, with a report (§3.3 step 4a).

### 3.2 Internal model (firmware, stored per profile)

Two layers. The U64 layer holds exactly the U64 items, so a partial U64 `.cfg` applies on top of the current state as it does on a U64. The per-SID layer holds Multitini overrides; every field defaults to "Follow".

**U64 layer**

| Field | Values | Default |
|---|---|---|
| `ultisid_addr[2]` | U64 index 0-48 (0 = Unmapped, 1-32 = `$D400-$D7E0`, 33-48 = `$DE00-$DFE0`) | per preset |
| `ultisid_split` | 0-7 (A3) | 0 |
| `auto_mirror` | 0/1 | 1 |
| `socket_addr[2]`, `socket_enable[2]`, `ext_split` | U64 index; 0/1; 0-5 | 1 (`$D400`, the U64 default [V u64_config.cc:413-414]), 0, 0 |
| `paddle_override` | 0/1, stored only | 1 |
| Group `model[2]`, `digi[2]` | "Combined Waveforms" 0 = 6581, 1 = 8580; "Digis Level" 0-3 | 6581, 2 |
| Group `curve[2]`, `res[2]` | stored for round trip only; no effect (§3.6) | 0, 0 |
| Group `volume[2]`, `pan[2]` | volume index 0-30, pan index 0-10 | 24 (0 dB), per preset |
| Group `pan_from_preset[2]` | 0/1 (rule below) | 1 |
| `master` | volume index 0-30 | 24 |

- The socket default matters even with sockets disabled: U64 `auto_mirror` counts every socket whose base lies in `$D400-$D7FF`, enabled or not [V u64_config.cc:864-867, 2406-2465]. With `Unmapped` as our default, a partial file such as "UltiSID 1 Address=$D500", "UltiSID 2 Address=$D520" would clear A8 here (both SIDs would also answer in `$D4xx`), while a U64 keeps A8 because its sockets sit at `$D400` [I from the code].

**Per-SID layer (n = 0..7)**

| Field | Values | Meaning of Follow |
|---|---|---|
| `addr` | Follow / Unmapped / U64 index 1-48 | address of the U64 part that maps to n (§3.1), or Unmapped when the split has no such part |
| `model` | Follow / 6581 / 8580 | group `model[n & 1]` |
| `volume` | Follow / 0-30 | group `volume[n & 1]` |
| `pan` | Follow / 0-10 | group `pan[n & 1]` |

**Output settings:** headroom (Auto, 0 dB, −6 dB, −12 dB; default Auto), DC removal (Auto, On, Off; default Auto = On when 3 or more SIDs are mapped), resampling (Point, Average; default Point).

**Overlay rule (per-SID fields under a title `.cfg` or UCI LOAD_CONFIG).** U64 files carry only U64-layer keys. Without this rule, stored per-SID overrides would change the layout a U64 file sets: a stored "SID 3 Address = $D600" would move SID 3 off TT8's `$D480`, and a stored SID 5 pan (the #664 fix) would change the TT8 stereo image. TT8's own file has no Multitini keys [V TT8 cfg]. The SID part of the overlay carries three class bits (§3.10), set by the keys it applied:
- **ADDR**: any address key of `[SID Addressing]` (UltiSID 1/2 Address, UltiSID Range Split, Auto Address Mirroring, SID Socket 1/2 Address, Ext DualSID Range Split). Every per-SID `addr` that the overlay does not set itself reads as Follow. Paddle Override is not in the list, because it has no effect.
- **MIX**: any "Vol UltiSid n" or "Pan UltiSID n" key. Every per-SID `volume` and `pan` that the overlay does not set itself reads as Follow.
- **MODEL**: any "UltiSID n Combined Waveforms" key. Every per-SID `model` that the overlay does not set itself reads as Follow.
- A per-SID field that the overlay sets itself (a `[Multitini SID]` key, a "SID audio" preset key, a menu edit under the overlay) keeps that value.
- Stored overrides are not changed. They apply again when the overlay ends.

**Pan from preset.** Our Dual presets store hard pans (Left 5/Right 5), because the canonical legacy form needs gains (0x80, 0)/(0, 0x80) (§3.3 step 8). The U64 default for both UltiSIDs is Center [V u64_config.cc:447-448]. A partial `.cfg` or UCI LOAD_CONFIG that leaves the mapped SIDs in one group would then play them in one ear. Example: "UltiSID 2 Address=Unmapped" on our default Dual $D500 leaves SID 1 alone with GAIN_R = 0. Rule [I]:
- Every preset sets `pan_from_preset[g]` for both groups. A group pan set explicitly (a "Pan UltiSID n" key from a `.cfg` or UCI LOAD_CONFIG) clears the flag for that group, in the layer that sets it.
- When the flag is set and the effective map has no mapped SID in the other group, firmware uses Center for this group.
- A single mapped SID that owns every slot with its pan from a preset becomes legacy mode 0 (§3.3 step 8): the same stream on both sides, as today's Single and the UA design state [D UAD §2.7].
- Per-SID pan overrides are not affected.

Why two layers [I]: per-SID addresses alone cannot apply a `.cfg` that sets only "UltiSID 2 Address"; the U64 layer alone cannot fix #664 (a right-channel SID at `$D500` inside the TT8 layout).

### 3.3 From the model to the decode table (firmware, `mt_c64_sid_map_build()`)

1. **Part bases.** For UltiSID u and part p: base = (`u64_sid_offsets[addr_u]` & ~split) | part bits, mask = 0xFE. The part bits put p back on the split lines (A5). All 8 parts are built, whatever the image's SID count; parts the image lacks are dropped only after mirroring (step 4a).
2. **Per-SID overrides** replace the address of SID n with base = offset, mask = 0xFE. Under an overlay they may read as Follow (overlay rule, §3.2).
3. **Sockets.** Socket entries (default `$D400`, disabled, as on the U64) enter step 4 with base & ~ext_split and mask 0xFE & ~ext_split, as on the U64 (A6). Enabled or not, they count in mirroring [V u64_config.cc:864-867, 2406-2465]. A disabled socket at `$D400` (base 0, mask 1 on A5-A9) changes the UltiSID decode only on a line where every UltiSID part has a 1, because only there would the line otherwise be cleared. No preset has such a line, so every preset decodes the same as with sockets Unmapped [I, worked through for Single, Dual $D420, Dual $D500, Triple, Quad and Eight]. Whether an enabled socket also gets a SID is decision U4.
4. **Mirroring** (if on): the A6 rule over all entries with base in 0x40..0x7F, including parts the image lacks, generalised from 4 entries to parts. For override-free configurations it must equal the U64 result; a host test checks this exhaustively against the upstream functions (§9.2).
   - 4a. **Missing parts.** Parts the image lacks (n ≥ SID count) are dropped after step 4, so the split lines stay decoded as on a U64. A dropped part that was mapped is reported once, for example "tt8-ultimate.cfg: SIDs 5-8 not available in this image". Its slots become uncovered (step 6).
5. **Slot masks.** For each slot s = 0..31 (address byte 0x40 + 2s, so A9..A5 = s): bit n is set when SID n's (base, mask) matches.
6. **Uncovered slots.** A slot that no SID matches gets a read owner, the lowest-index SID mapped in `$D400-$D7FF` (SID 1 if none), and `WRITE_INHIBIT` (§4.1). Reads are defined; writes reach no SID, on the native, fast and REU DMA paths. On a U64 no SID matches such an address, so its writes are inert too [V u64_config.cc:1249-1255; sid_editor.cc:165]. Uncovered slots arise with mirroring off, with mirroring on when a layout has gaps (the U64 rule never adds overlaps [V u64_config.cc:2406-2410]; Triple leaves `$D460` uncovered), and from dropped parts (step 4a). So every slot has a read owner, as Plan §3.4 requires (§10.2 item 5; decision U3).
7. **Gains** per SID and for UA (§3.7). A SID that owns or shares no slot gets gain 0. Group pans follow the pan-from-preset rule (§3.2).
8. **Canonical form.** Firmware uses legacy mode (EXPANSION bit 6 = 0) instead of the table (§3.9) when the result is one of:
   - (i) legacy mode 0, 2 or 3 exactly: the same table, gains (0x80, 0)/(0, 0x80);
   - (ii) exactly one mapped SID that owns every slot, with its pan from a preset (flag set, §3.2), its volume 0 dB and master 0 dB. This becomes legacy mode 0, today's Single: both chips take every write and play the same stream, one per side, at unity [V fpga64:1008 `cs => sid_sel_r & sid_sel_l` with both selects equal in mode 0; sid_top.sv:505-506].

   Both cases also need model 6581, Point, DC removal not On, UA_GAIN 0x80 and a headroom setting that gives h = 0.

Check against A7 [I]: the TT8 configuration gives the slot mask `1 << (A5 + 2·A7 + 4·A8)` for every slot. No slot is uncovered; each SID also answers at +$40 and +$200.

Check at SID count 4 [I]: the TT8 keys give SIDs 1-4 at `$D400 $D420 $D480 $D4A0`, with the same mirrors (+$40, +$200). The slots of `$D500 $D520 $D580 $D5A0` and their mirrors are uncovered: read owner SID 1, writes inert. Dropping parts before mirroring would instead clear A8 (SIDs 1-4 all have A8 = 0), so SID 1 would also answer at `$D500` and take TT8's SID 5 writes. The U64 never clears A8 here, because its parts differ on that line [V u64_config.cc:2420-2465].

### 3.4 Presets ("SID audio" row, argument 2)

| Preset | Mode | SIDs (address, pan) | Headroom (Auto) | Shown when |
|---|---|---|---|---|
| Single | legacy 0 | 1 at `$D400` everywhere, both sides (U64 layer: both UltiSIDs at `$D400`, Left 5 / Right 5) | 0 dB | caps bit 0 (today) |
| Dual $D420 | legacy 2 | SID 1 `$D400` L, SID 2 `$D420` R (A5) (U64 layer: U1 `$D400` Left 5, U2 `$D420` Right 5, split Off) | 0 dB | today |
| Dual $D500 (default) | legacy 3 | SID 1 `$D400` L, SID 2 `$D500` R (A8) (U64 layer: U1 `$D400` Left 5, U2 `$D500` Right 5, split Off) | 0 dB | today |
| Dual $DE00 / Dual $DF00 | map | SID 2 at `$DE00` / `$DF00` | 0 dB | caps bit 12 only (later, Plan B5) |
| Triple | map | SID 1 `$D400` Center, SID 2 `$D420` Left 5, SID 3 `$D440` Right 5. U64 layer: U1 `$D400` Center, U2 `$D420` Left 5, split 1/2 (A6), mirroring On. Overrides: SID 3 pan Right 5, SID 4 address Unmapped. `$D460` is uncovered: it reads SID 1 and writes there are inert (§3.3 step 6) | −6 dB | caps count ≥ 4 |
| Quad | map | `$D400` L5, `$D420` R5, `$D440` L5, `$D460` R5 (U64: U1 `$D400`, U2 `$D420`, split 1/2 (A6)) | −6 dB | count ≥ 4 |
| Eight (Tuneful Eight) | map | TT8 layout (A7); SIDs 1, 3, 5, 7 (UltiSID 1 group) Left 2; SIDs 2, 4, 6, 8 (UltiSID 2 group) Right 2, as in the TT8 file | −12 dB | count = 8 |
| Custom | map | anything else | Auto | when the stored map is not a preset |

- Presets set the U64 layer, both `pan_from_preset` flags and the per-SID overrides listed for the preset; all other overrides are cleared (Follow). `mt_c64_sid_preset_match()` compares the listed overrides too. Models stay 6581 in presets.
- Triple needs overrides because SID 1 and SID 3 are both parts of UltiSID 1 (n & 1 = 0, §3.1) and share one group pan, and because split 1/2 (A6) always creates SID 4 at `$D460` [I].
- The Triple and Quad addresses are a choice [I]; 3SID/4SID files name their own addresses (decision U9).
- Cycling: Single → Dual $D420 → Dual $D500 → Triple → Quad → Eight → Single. Presets beyond the image's SID count are skipped. "Custom" appears in the cycle only when a custom map is stored.
- Choosing a legacy preset turns the map off (Plan §5.1).

### 3.5 Address decode in `$D400-$D7FF`

| Rule | Behaviour |
|---|---|
| Slot | 32-byte slot index = A9..A5. Registers inside a SID mirror every 32 bytes (A4..A0), as today [V sid_top.sv:21; fpga64:1009] |
| Write | Every SID whose bit is set in the slot mask takes the write (fan-out), on the native, fast and REU DMA paths alike. In a slot with `WRITE_INHIBIT` (§3.3 step 6) no SID takes the write, and no bus latch changes |
| Read owner | Lowest set bit of the slot mask. It drives the data bus. In a `WRITE_INHIBIT` slot the mask names exactly this one SID |
| Other selected SIDs on a read | Update their own bus latch with their own live value for `$19-$1C`, as chip 1 does in mode 0 today [V sid_top.sv:250-257] |
| Unmapped read | None in `$D400-$D7FF`: every slot has a read owner (§3.3 step 6) |
| Write-only registers | Return the owner's bus latch (the last byte written to or read from that SID). No decay, as today (FSID S4) [V sid_top.sv:221; D FSID §2.5] |
| OSC3 `$1B`, ENV3 `$1C` | The owner's own voice 3 [V sid_top.sv:186-187, 254-255] |
| POTX `$19`, POTY `$1A` | Only the "paddle SID" sees the paddles: the read owner of slot 0 (`$D400-$D41F`). Every other SID reads `$FF` for POT. This is today's chip-1 rule [V fpga64:1012-1017] and Plan §6.2. U64 "Paddle Override" is accepted but has no effect: paddles always reach the `$D400` owner |
| REU DMA | DMA uses the native path, so DMA reads and writes follow the same table [D FSID F19] |
| Table change | A commit takes effect at the next `ce_1m` (start of a native cycle), or on the next `c64_clk` edge while the core is paused (`core_paused` = fpga64 `pause_out` = 1). While paused, `sysCycle` is held at `CYCLE_EXT4` and `enableSid` is set only at `CYCLE_CPUF`, so `ce_1m` never fires [V fpga64:565-566, 586-587, 727-743, 1006]. The dense menu pauses the C64 [V top:1284; adapter:731-736], and C64 options other than argument 0 apply while paused [V main.c `menu_action_needs_core_progress()`], so without the pause rule no menu SID change could be acknowledged. `C64_UCI_CONTROL.HOLD` pauses through the same path (UCI §3.8.6), so a `$50` change under HOLD also swaps at once (§3.10). During a guest reset the cycle counter keeps running (it has no reset term [V fpga64:568-591]), so a commit made inside a launch's reset hold swaps at the next `ce_1m`. While paused, no SID engine step and no native access happens. An access uses the table active when it lands in the `c64_clk` domain, so an FIO entry that lands while paused uses the new table. `ACCEPTED_SEQ` moves only after the swap (§4.1) |

Answer to "paddles only on SID 1?": paddles reach the SID that owns `$D400`. In every preset that is SID 1.

### 3.6 Model, filter and digi items

| Item | Our behaviour |
|---|---|
| Model 6581/8580 per SID | 6581 only, unless the image sets `C64_EXPANSION_CAPS` bit 11. With bit 11, each SID has its own model bit; the MiSTer engine already takes a per-chip `mode` [V sid_top.sv:48, 131; sid_tables.sv:693-700]. A per-group model (U64 style) costs the same with E1, because every pair's table and voices serve one chip of each group [I]; so the model is per SID |
| "UltiSID n Combined Waveforms" | Sets the group model. Without caps bit 11, 8580 is skipped and reported ("8580 not available in this image") |
| "UltiSID n Digis Level" | For an 8580 SID: Off → digifix 0, Low/Medium/High → digifix 1 (MiSTer's `ext_in(17)` offset [V fpga64:1022-1026]). No effect on 6581 SIDs, which model the DC offset already [V sid_voice.sv:29-32] |
| "UltiSID n Filter Curve", "Filter Resonance" | Stored, no effect, reported once. MiSTer filters are tied to the model; the U64 curve tables fit only the UltiSID filter [I] |
| MiSTer 6581 curves 1-3 | Not offered. The table is trimmed to curve 0 (§4.1), which is the only curve used today [V adapter:771 `sid_cfg(4'b0000)`] |

### 3.7 Mixer

**Gains (firmware, U64 formulas).** For SID n with effective volume v, pan p and master m:
- g = `combine_mixer_gain(volume_ctrl[v], volume_ctrl[m])` (M3).
- GAIN_L = (`pan_ctrl[10−p]` · g) >> 8; GAIN_R = (`pan_ctrl[p]` · g) >> 8. "Left 5" is hard left (M5).
- Check: 0 dB, master 0 dB and Left 5 give g = (0x80·0x80 + 0x40) >> 7 = 0x80, GAIN_L = 0x80, GAIN_R = 0 [I arithmetic]. So the legacy hard L/R layout is exactly expressible.
- UA (map mode): UA_GAIN_L = UA_GAIN_R = `combine_mixer_gain(ua_level, volume_ctrl[m])`. ua_level is 0x80 until the UA design defines its level row and the "Vol Sampler L/R" keys. The U64 applies Vol Master to all ten mixer sources, Sampler L/R included [V u64_config.cc:1342-1358, 441-442]. Master 0 dB gives 0x80, today's unity. Vol Master −6 dB (`volume_ctrl` 0x40) halves UA_GAIN exactly as it halves the SID gains: (0x80·0x40 + 0x40) >> 7 = 0x40 [I arithmetic].
- Headroom h (0, 1 or 2 steps of 6 dB) is not folded into the gains. The HDL applies it as `HEADROOM_SHIFT` (§4.1) to the summed mix of all SIDs and UA, a master stage. So the SID:UA balance set by the title or the user does not depend on h, and the gains keep their 8-bit resolution. With g >> h instead, low volume steps such as −36 dB and −42 dB would round to Off at h = 2 [I].
- Auto headroom: let S = the larger, over both sides, of Σ GAIN/128 over the SIDs. h = 0 if S ≤ 1.5, 1 if S ≤ 3, else 2. TT8 at Left 2/Right 2 gives S = 4·0.89 + 4·0.45 = 5.4 → −12 dB [I]. These thresholds are provisional until the peak-level measurement (O6).
- UA is not counted in S. At the preset thresholds counting it changes nothing (Triple 1.71 → 2.71, Quad 2.0 → 3.0, Eight 5.4 → 6.4: the same h). It would only give a 1-2 SID custom map with UA on −6 dB, while the canonical legacy layouts with UA on stay at 0 dB [I arithmetic].

**HDL datapath (map mode only).**
```
x[n]      = DC_REMOVE ? dc_out[n] : audio[n]                      // 18-bit signed
prod_L[n] = $signed(x[n]) * $signed({1'b0, GAIN_L[n]})            // 18 x 9 -> 27 bits
ua_L      = $signed(ua_audio_l) * $signed({1'b0, UA_GAIN_L})      // 18 x 9 -> 27 bits
sum_L     = sum of prod_L[0..7] and ua_L, each sign-extended       // 32-bit signed
board_L   = sat24_wide(sum_L >>> (1 + HEADROOM_SHIFT))            // same for R
```
- `sat24_wide(input logic signed [31:0] v)` is a new function with today's limits (0x7FFFFF, −0x800000) and a clip flag. The legacy `sat24` takes a 25-bit input [V adapter:340-344]. Feeding it the wide sum would truncate before the clamp, so loud 4-8 SID mixes would wrap, which is the fault this design fixes.
- Width: 9 terms of at most 2^17 · 255 sum to about 3.0·10^8 < 2^31, so the 32-bit sum cannot wrap even with 8 SIDs and UA at +6 dB [I arithmetic].
- Unity is exact: s·0x80 >>> 1 = s << 6, today's scale [V adapter:334-337]. The single shift of the sum equals today's per-term shifts whenever every product is even, which holds for the legacy gains 0x80 and 0 (E-3) [I].
- With DC removal off the path is combinational on `c64_clk` from the published audio registers, so it has **zero latency** against today's path. With it on, `dc_out` lags the published audio by at most 8 `c64_clk` cycles (≤ 0.26 µs) [I]. 18 DSP48E1 at 8 SIDs and 10 at 4 (2 per SID plus 2 for UA); 31.7 ns period, so no timing concern [I].
- Saturate, never wrap. This fixes the U64 wrap that #744 reports (recorded in UAD §2.7).
- `board_clip`, which feeds the UA sticky clip diagnostic [V adapter:338-339, 669; hdl/core/c64/multitini_c64_ua.sv:156], comes from the `sat24_wide` clip flag on the audio tick in map mode. Today's expression stays for bit 6 = 0.

**Legacy mode (bit 6 = 0):** today's `board_sum_l/r` text, `sat24` and the lines `audio_l <= sat24(board_sum_l);` / `audio_r <= sat24(board_sum_r);` stay literally, under the map-off branch; the test literals stay [V tests/test_audio_path.py:40-41, 67-72].

**Headroom facts [I, computed from source]:**
- One 6581 SID at volume 15 has a DC offset of about 13 % of full scale (≈ 17,000 LSB of the 18-bit output) and peaks near 68 % with three full voices; the clamp limit is 93.75 % [V third_party/c64/upstream/rtl/sid/sid_voice.sv:29-32, 178-195; sid_filter.sv:41, 155-160, 204-217].
- Worst case per side at 0 dB, 4 SIDs per side hard-panned: 3.75 FS plus UA 1.0 FS. The 32-bit sum holds it (width bullet above); the clamp decides what is heard.
- 8 summed 6581 DC offsets reach about 0.5-0.7 FS per side at 0 dB. This is the reason for DC removal and Auto headroom.

**DC removal (map mode; setting Auto/On/Off).** Per SID, a one-pole DC tracker with fractional state:
```
d[n]       signed 34 bits, Q18.16 (the 18-bit sample scale plus 16 fraction bits)
once/us:   d[n] <= d[n] + ((($signed(audio[n]) <<< 16) - d[n]) >>> 15)    // 35-bit difference; after each publish
dc_out[n] = sat18($signed(audio[n]) - ((d[n] + 2**15) >>> 16))             // rounded estimate
```
- Time constant 2^15 µs = 32.8 ms, corner 1/(2π · 32.8 ms) ≈ 4.9 Hz. It passes `$D418` digis (kHz) [I arithmetic].
- Why fractional state: an integer form y = x − x_prev + y − (y >>> 15) never leaks for 0 ≤ y < 2^15, since y >>> 15 = 0 there. Any DC below 32,768 LSB would pass unchanged, and the 6581 offset (≈ 17,000 LSB) sits inside that dead band; negative values would decay by 1 LSB per µs instead [I arithmetic]. With 16 fraction bits the update stops only when the error is below 0.5 LSB, for both signs [I].
- A constant input decays by e^−5 after 5 time constants (17,000 → about 115 LSB) and below 1 LSB after 10 (328 ms) [I arithmetic].
- On enable, and when a pair leaves reset, d[n] is loaded with the current sample, so turning DC removal on does not thump [I].
- Serial over the SIDs with one shared datapath (35-bit subtract, shift and add; the rounding subtract and `sat18`), no multiplier: 8 SIDs in 8 of the 32 `c64_clk` cycles of each µs. State 8 × 34 FF; `dc_out` 8 × 18 FF.
- The U64 removes the offset too (M7). Never in legacy mode.

**Resampling.** Point (default; today's NCO point sampling [V adapter:889-905]) or Average: a boxcar over each 48 kHz period, the same structure as the UA boxcar [D UAD §2.7]. Average is about −0.6 dB at 10 kHz and −2.6 dB at 20 kHz and suppresses aliases near multiples of 48 kHz [I]. Map mode only.

**UA in map mode:** UA_GAIN carries master, as on the U64 (gains above), and `HEADROOM_SHIFT` applies to UA as to the SIDs. At master 0 dB and h = 0 it is 0x80 on both sides, today's unity. The UA level row belongs to the UA design (TD argument 11); this design provides the field and folds master into it.

### 3.8 `$DExx/$DFxx` SIDs (not in the first release)

- Index 0x20-0x2F carries one mask per slot `$DE00 + $20·s`. It must be 0 unless caps bit 12 (Plan §3.4). Caps bit 12 = 0 in XM7.
- When added (Plan XM8): eligibility and priority follow Plan §6.3 exactly. Cartridge IO first; REU, UCI and UA win their ranges; an IO SID slot answers only where none of them is enabled, and is suspended while a CRT is loaded or loading. One owner per address, no fan-out between devices. Menu blocks per Plan B5.
- Every `$DE00-$DFFF` access stays a φ2 forward at turbo [V hdl/core/c64/multitini_c64_island_map.sv:69-76]; the fast port never serves them (binding pacing rule, TD §1.7).
- The fpga64 merge `io_ext => io_ext or sid_sel_r` [V fpga64:790] is replaced by the gated select. Modes 1 and 4 stay clamped [V hdl/common/multitini_control_regs.sv:238].

### 3.9 Exactness

**E-1 Legacy is unchanged by construction.** With EXPANSION bit 6 = 0 (the reset default):
- decode = today's `sid_sel_l/r` and `fast_sid_select` [V fpga64:371-381, 546-548, 989-990];
- engine = today's `sid_top` instance for SIDs 1-2, with the same generics except `MULTI_FILTERS = 0` (E-4), and with one additive output port that legacy mode does not use (E-5);
- audio = today's `board_sum` text and `sat24` lines;
- SIDs 3-8 are held in reset, not selected and not mixed;
- every new fpga64 input has a VHDL default equal to map off (§4.2).
- So `tests/test_audio_path.py`, `tb_sid_output_schedule`, the turbo SID benches and the stock bus trace (G-STK, at `SID_COUNT` 2 and at the image value, §9.4) must pass unchanged. `tb_c64_sid_socket` must pass with only wrapper declarations added to `scripts/test_c64_mame_sid.py`, and with unchanged MAME routing results (§4.2, R9).
- No existing bench checks today's fast select against the native select exhaustively. T-S1c is only planned, as case (c) of a `tb_sid_fast_port` bench that does not exist [D TD §10.2; V `hdl/sim` holds no such bench]. `tb_c64_sid_socket` is the MAME socket-routing bench: native path only, `FAST_IO` false, modes 0/2/3 [V hdl/sim/tb_c64_sid_socket.sv:63-111; scripts/test_c64_mame_sid.py:63]. `fastSidSelect` appears outside fpga64 only as a tied-off signal in that script's wrapper [V grep over hdl/sim, scripts, tests]. B2 therefore first runs against today's text and keeps that run as the legacy baseline (§9.1).

**E-2 Canonical form.** Every 1-2 SID configuration that legacy mode can express runs in legacy mode (§3.3 step 8). So the user-visible 1-2 SID presets are bit-identical to today.

**E-3 Map path equivalence.** The map path with a legacy table (no `WRITE_INHIBIT`), legacy gains, UA_GAIN 0x80, `HEADROOM_SHIFT` 0, model 6581, Point and DC removal off gives the same `board_L/R` on every `c64_clk` edge as the legacy path. That holds because the decode selects the same chips and the mixer adds no latency [I]. A bench proves it (§9.1 B3). This keeps a later removal of the legacy path possible; it is not planned.

**E-4 `MULTI_FILTERS = 0`.** With `cfg = 0`, `f6581_curve[{cfg, Fc[10:1]}]` and `f6581_curve[Fc[10:1]]` read the same entry [V hdl/core/c64/ports/sid_tables.sv:1312-1319]. Output is unchanged; the table should fit one RAMB18 [I; O1 verifies it in OOC synthesis].

**E-5 Fast SID port widened from 2 to N chips (FSID §3.3).** In map mode, with a = the address of the FIO entry (a registered queue entry) and r = `fio_rd_addr` (the live T65 address):
```
fio_cs[n]        = slot_mask[a(9..5)][n]                    when fastSidLatch,
                   or when fastSidWr and not write_inhibit[a(9..5)], else 0
fio_latch_src[n] = (n == owner[a(9..5)])
rd_owner         = owner_lut[r(9..5)]                       (3 bits: the only fast-read logic on r(9..5))
fast_rd_cs[n]    = (rd_owner == n)                          when fio_rd_sid, else 0
fast read data   = sel(r(4..0), cand[rd_owner])             (owner first, then one register case)
cand[n]          = {pot_x[n], pot_y[n], OSC3[n], ENV3[n], bus_data[n]}
sel(x, c)        = $19: c.pot_x, $1A: c.pot_y, $1B: c.osc3, $1C: c.env3, else c.bus_data
```
- `owner` and `owner_lut` hold the lowest set bit of each slot mask, computed at the swap. `fast_rd_cs` as a one-hot of the owner therefore equals the slot-mask form "lowest selected chip drives the data" that `sid_top` implements today [V sid_top.sv:384-399: the loop runs from N−1 down to 0, so the lowest selected chip wins] [I; B2 checks it for every table].
- `cand` comes from a new additive output of `sid_top` (each chip's five fast candidates). Its generic default keeps today's text and behaviour. In legacy mode the fast read data is today's `fast_rd_data` of instance 0, unchanged (E-1).
- Why this shape: the T65 address nets r(9..0) also drive the half-period island BRAM path (§5.2). A per-slot 8-bit mask decode would add loads on all of A5-A9, and a per-pair register case would multiply the loads on A0-A4 by the number of pairs [I].
- With fewer than 8 chips the upper bits are 0. In legacy mode the 2-bit rules stay.
- Register writes per chip are today's: `fast_write = fio_cs[n] && fio_we && !cs[n]`; native has priority [V sid_top.sv:121, 219, 258].
- The 22-bit FIO entry already carries A9..A0, so the island and the FIO queue are unchanged [V hdl/core/c64/multitini_c64_cpu_island.sv:179].

**E-6 Edge stage widened.** One stage per voice control register (`$x04`, `$x0B`, `$x12`) per chip: 3 per chip, 24 at 8 SIDs. A fan-out write stages identically in every selected chip. A native write to chip n's control register cancels only chip n's stage. The FSID §3.4 contract is unchanged per chip [V sid_top.sv:293-378; D FSID §3.4].

**E-7 Per-chip timing.** With E1, SID n is chip n mod 2 of a pair. Odd user SIDs (n even) have today's chip-0 filter timing; even user SIDs have chip-1 timing (a volume write at e_31 is heard one native cycle later) [D FSID F9]. All pairs share `ce_1m` and publish on the same edge [V sid_top.sv:479-501].

**E-8 `$DExx/$DFxx` SIDs stay φ2 forwards** (§3.8).

### 3.10 Title overlay, UCI LOAD_CONFIG and menu edits

Facts:
- CFG's overlay is one RAM object: a mask of owned groups and a value per owned group (CFG §3.1, §3.8). Its SID group holds the expansion byte bits 2:0, and from XM7 the SID map. `$50` LOAD_CONFIG merges a file into the overlay group by group (CFG §3.15). Menu edits go through the overlay's edit function per menu argument (CFG §3.11, §4.2).
- The SID sub-page items have their own index space, not Options arguments (Plan §5.2).
- The U64 applies a partial file on top of the current state, key by key [D CFG §2.3]. The overlay rule (§3.2) depends on which key classes a file set.

So one value per SID group is too coarse: a second `$50` file that holds only "Pan UltiSID 1" would replace the addresses the first file set, and a sub-page edit would never be seen as an overlay edit. From XM7 the SID group is refined to fields:

**SID overlay record (`mt_c64_sid_overlay_t`).**

| Part | Content |
|---|---|
| `value` | an `mt_c64_sid_model_t` (§3.2) |
| `field_mask` | one bit per field of the model. U64 layer: `ultisid_addr[0..1]`, `ultisid_split`, `auto_mirror`, `socket_addr[0..1]`, `socket_enable[0..1]`, `ext_split`, `paddle_override`, `model[0..1]`, `digi[0..1]`, `curve[0..1]`, `res[0..1]`, `volume[0..1]`, `pan[0..1]`, `pan_from_preset[0..1]`, `master` (25 bits). Per-SID layer: `addr`, `model`, `volume`, `pan` × 8 (32 bits). Output: headroom, DC removal, resampling (3 bits). 60 bits in a 64-bit mask |
| `class_bits` | ADDR, MIX, MODEL (§3.2). Set by the U64 keys of any source merged into this overlay; never cleared by a merge |

- The CFG group SID stays one registry group. The CFG `SID_PARAMS` group of key K12 folds into it, so the UltiSID and mixer keys share one record (§10.2 item 13). XM2 keeps CFG's legacy SID group (expansion bits 2:0 only); XM7 replaces it with this record.
- **Effective SID model** = stored model; then every field whose mask bit is set takes the overlay value; then the class rule of §3.2 makes unset per-SID fields of a set class read as Follow; then pan from preset (§3.2). `mt_c64_sid_map_build()` runs on the result.
- **Resolver (replaces CFG §3.7 step 2 at XM7).** SID lines apply in file order, each to the fields it names; a later line overwrites earlier lines for its own fields only. This replaces CFG's "group position = line of its last present member" rule for K11 against K17.
  - The Multitini key "SID audio" (CFG K17) names every field a preset sets: the U64 layer, both `pan_from_preset` flags and every per-SID field (listed overrides set, the rest Follow). At XM7 its values extend to `Triple`, `Quad` and `Eight` (alias `Tuneful Eight`). `Custom` is a BAD_VALUE.
  - Example: `SID audio=Eight` then `UltiSID 2 Address=$D500` gives the Eight preset with UltiSID 2 at `$D500`. In the reverse order the preset line comes last and the result is the plain Eight preset.
- **Merge (`$50`).** Per field: fields the new file names replace the overlay's values; other fields stay; class bits are ORed. Example: file A sets the TT8 address keys, file B sets only "Pan UltiSID 1=Center". After both, the TT8 addresses stay, UltiSID 1 is Center, and ADDR and MIX are both set.
- **Ownership.** A field is owned when its mask bit is set, or when it is a per-SID field of a set class (it then reads Follow because of the overlay).
- **Menu edits.** Argument 2 and `change_c64_sid_setup(item, delta)` check ownership per field. If the action touches an owned field, it edits the overlay and makes no settings save (Plan §8.2 rule 7, CFG §3.11). An action that touches several fields follows CFG's coupling rule: if any touched field is owned, the whole action goes to the overlay, the other touched fields become owned (copied from effective first), and nothing is saved. Otherwise it edits `stored` and saves `c64_sid.bin`, as today's handlers do. Touch sets:

  | Action | Fields |
  |---|---|
  | Argument 2 "SID audio", sub-page item 0 Layout | every field a preset sets |
  | Item 2 Address mirroring | `auto_mirror` |
  | Items 4-7 (selected SID) | that SID's `addr`, `volume`, `pan` or `model` |
  | Item 8 Master volume | `master` |
  | Items 9-11 | headroom, DC removal, resampling |
  | Item 12 Reset SID setup | every field. Under an overlay it resets the overlay's SID values to the profile default (coupling rule). The help text names "Revert title settings" (argument 18) as the way to drop the title's values |

  Every item whose field is owned shows "(title)" (Plan §8.2 rule 4).
- **Live `$50` change while a program runs.** A `$50` file that changes the SID map is applied inside the same `C64_UCI_CONTROL.HOLD` window as the REU and UA changes of that file (UCI §3.8.4, §3.8.6 steps 4-5):
  1. Set HOLD; wait for paused, then for REU idle (release and retry, 1 s in total, as UCI §3.8.4 and §3.8.6 and Plan §8.5).
  2. Write the 57 table words and COMMIT. The swap happens on the next `c64_clk` edge, because the core is paused (§3.5). Poll `C64_SID_STATUS` until `ACCEPTED_SEQ` equals the commit's sequence.
  3. Write `C64_EXPANSION_CONFIG` once, with bit 6 and the REU and UA bits of the file.
  4. Release HOLD; validate the reply.
  - If step 1 times out, nothing is written, the overlay is unchanged and the reply is the UCI design's timeout status. The menu path needs no HOLD, because the menu already pauses the C64. The launch path commits inside the launch's reset hold (CFG §3.9 step 7).
  - Reason: the running program never sees a mix of old and new expansion settings, no REU DMA crosses a table swap, and no "please wait" screen appears. Without HOLD each step is still atomic on its own (the swap at `ce_1m`; bit 6 on one edge of the `controls_i` word), but the SID table, bit 6 and the REU/UA bits would land on different edges [I].
- **Writer.** "Save settings for this title" (CFG §3.14) writes U64-layer fields under their U64 sections and per-SID and output fields under `[Multitini SID]`.

### 3.11 Side finding: POT tie (separate from multi-SID)

- Facts: the adapter ties `pot1..pot4 = 8'hFF` [V adapter:768]. fpga64 inverts them: `pot_x1 = (PA6 = 0) ? $FF : not pot1` [V fpga64:993-996]. So POTX/POTY read `$00` while CIA1 PA6 or PA7 is 1. The KERNAL keyboard scan writes `$DC00 = $7F` (PA6 = 1) [I].
- MiSTer feeds `pot1 = {8{joyA_c64[5]}}` with no paddle, so `$FF` (button up) [V third_party/c64/upstream/c64.sv:989-992]. VICE reads the joyport POT through `makepotval()` for chip 0 [V VICE src/sid/sid.c:229-252]; a port with no device is expected to read `$FF` [I; O7].
- Effect [I]: games that read POTX/POTY as a second fire button see it held. Recorded as FSID F20.
- Action: check in VICE (no device, `$DC00 = $7F`, read `$D419/$D41A`). If VICE reads `$FF`, tie `pot1..pot4 = 8'h00` in a small separate change with its own bench case. Not part of XM7 (decision U10).

---

## 4. Implementation plan by file

Register offsets, bits, caps, the storage file, menu arguments and milestones are fixed by the Plan. This section fills the parts the Plan gives to this design.

### 4.1 Spec and ABI (`spec/interface_v0.json`, generated headers)

Part of the single 0.26 spec edit (Plan §3.1, P1). `C64_MULTI_SID_MIN_ABI_MINOR` = 26 (Plan §3.6).

**`C64_EXPANSION_CONFIG` @0x0D0 bit 6 `SID_MAP_ENABLE`** (Plan §3.3). The clamp allows bit 6 only after a commit has been accepted since control reset. A rejected commit changes nothing (the active table and bit 6 stay).

**`C64_EXPANSION_CAPS` @0x0D4**, bits of this design: 6 `MULTI_SID`, [10:8] `SID_COUNT_M1`, 11 `SID_MODEL_8580`, 12 `SID_IO_SLOTS` (0 in XM7). Example SID bits: 4 SIDs, 6581 only = 0x340; 8 SIDs with 8580 = 0xF40.

**`C64_SID_INDEX` @0x154** rw [5:0]. **`C64_SID_DATA` @0x158** wo: writes the shadow entry at INDEX, then INDEX+1 (wraps to 0), and marks the entry written. Allowed while busy (the shadow is not the bank being sent). **`C64_SID_COMMIT` @0x15C** wo, any value.

**`C64_SID_STATUS` @0x160** ro (fields as Plan §3.4):

| Bits | Field |
|---|---|
| [7:0] | `ACCEPTED_SEQ`: the sequence of the last commit whose table swap the c64 side has reported. It moves only after the swap (§3.5 Table change), when the returned `sid_map_valid` = 1 and `sid_active_seq` equals the commit's sequence. It never moves backwards, also not across a `c64_clk` reset and resend |
| 8 | `BUSY`: from an accepted COMMIT (or a resend after a `c64_clk` reset) until the c64 side reports the swap. The swap comes within one native cycle, or at once while paused |
| 9 | `REJECTED`: the last COMMIT was rejected; cleared by the next accepted commit |
| [14:10] | `REJECT_CODE`: 1 entry missing; 2 zero slot mask; 3 SID ≥ count named; 4 IO slot without caps 12; 5 reserved bit, model/digifix without caps 11, or `HEADROOM_SHIFT` = 3; 6 commit while busy; 7 `WRITE_INHIBIT` with a mask that does not name exactly one SID |
| [21:16] | `REJECT_INDEX`: first failing entry |
| [30:22] | reserved, 0 |
| 31 | valid |

**Entry layouts** (all 32-bit; reserved bits must be 0):

| Index | Name | Layout |
|---|---|---|
| 0x00-0x1F | `SID_SLOT_MASK[s]` (`$D400 + $20·s`) | [7:0] mask, bit n = SID n; non-zero; no bit ≥ count. Bit 8 `WRITE_INHIBIT`: reads go to the owner named by [7:0], which must name exactly one SID; no SID takes a native, fast or REU DMA write in this slot. [31:9] reserved |
| 0x20-0x2F | `SID_IO_SLOT_MASK[s]` (`$DE00 + $20·s`) | [7:0] mask; 0 unless caps bit 12 |
| 0x30-0x37 | `SID_PARAM[n]` | [7:0] `GAIN_L`, [15:8] `GAIN_R` (0x80 = unity, 0xFF ≈ +6 dB); 16 `MODEL_8580`; 17 `DIGIFIX`; bits 16-17 need caps 11; all 0 for n ≥ count |
| 0x38 | `SID_OUTPUT` | [7:0] `UA_GAIN_L`, [15:8] `UA_GAIN_R` (0x80 = today); 16 `RESAMPLE_AVERAGE`; 17 `DC_REMOVE`; [19:18] `HEADROOM_SHIFT` 0-2 (3 is rejected, code 5) |
| 0x39-0x3F | reserved | writes must be 0 |

- COMMIT requires every entry 0x00-0x38 written since the last accepted commit; firmware always writes the full table (57 words).
- Master gain has no entry: firmware folds it into every gain, SID and UA, as `setMixer` does for all ten U64 sources (M3) [V u64_config.cc:1342-1358]. The power-of-two headroom stage is `HEADROOM_SHIFT`. Plan §3.4 gives entry 0x38 (`SID_OUTPUT`) the UA gains, the output options and the shift; 0x39-0x3F stay reserved.
- `WRITE_INHIBIT` and `HEADROOM_SHIFT` refine reserved bits of entries this design owns. Legacy tables never set them, so E-1 and E-3 hold.

**Constants:** `C64_SID_SLOT_COUNT` 32, `C64_SID_IO_SLOT_COUNT` 16, `C64_SID_TABLE_ENTRIES` 0x39, `C64_SID_GAIN_UNITY` 0x80. `tests/test_interface_spec.py` and `make check-generated` gain them (Plan §3.7 rule 4).

### 4.2 RTL

| File | Change |
|---|---|
| `hdl/common/multitini_control_regs.sv` | Decode 0x154-0x160. Shadow and committed banks, each 64×32 LUTRAM; a per-entry "written" bitmap; per-write rule checks (codes 2-5, 7) and commit checks (1, 6); bank flip at an accepted commit. Streamer FSM: sends `{index, data}` words (38 bits) for 0x00-0x38, then a swap token with an 8-bit sequence, over `multitini_async_mailbox`. BUSY holds until the returned `sid_map_valid` = 1 and `sid_active_seq` equals the token's sequence; only then does `ACCEPTED_SEQ` move, and only then may the next commit's entries be sent. It resends the committed bank when the destination leaves reset (the `destination_running` pattern of `multitini_word_mailbox` [V hdl/common/multitini_word_mailbox.sv:17-21, 30-38]). `clamp_c64_expansion()` [V :235-243]: bit 6 forced 0 unless caps bit 6 and `table_ready` |
| `hdl/board/multitini_one_top.sv` | `C64_EXPANSION_CAPS` value from a new `C64_SID_COUNT` parameter (2, 4, 8) and `C64_SID_8580` [V :749, today 0x8000001F]. Instance of the SID table mailbox (`control_clk` → `c64_clk`, destination reset `c64_aresetn`, as `controls_i` [V :1244-1251]). The expansion field of `controls_i` widens from [4:0] to [6:0] (bit 5 UCI, bit 6 this design), and the top wire `expansion_config` widens to [6:0] [V :1232]. `status_i` gains 9 bits, `{sid_map_valid, sid_active_seq}`, for `ACCEPTED_SEQ` and BUSY [V :1252-1257, 228 bits today]. The combined widths (with the UCI: `controls_i` 120 → 136, `status_i` 228 → 301), the bit layout and the `RESET_VALUE` are defined once in Plan §3.5. This design's part of `RESET_VALUE` is `MT_C64_EXPANSION_CONFIG_DEFAULT[6:0]`, with bit 6 = 0. A new adapter port carries bit 6. A match counts only with `sid_map_valid` = 1, so the sequence 0 that a `c64_clk` reset leaves behind cannot match a wrapped sequence 0. Optional: +2 sticky clip bits in `status_i`, counted only while `debug_enable` (G-DBG) |
| `hdl/core/c64/multitini_c64_sid_map.sv` (new, GPL-3.0-only) | `c64_clk`. Accepts every mailbox word at once (`dst_ready` = 1), so the mailbox never stalls and never holds a level handshake across a destination reset. Entries go into a shadow FF bank; a swap token sets `swap_pending`. The swap copies shadow → active in one edge at the next `ce_1m`, or on the next `c64_clk` edge while `core_paused` = 1 (§3.5), then sets `sid_active_seq` to the token's sequence. The streamer sends nothing more until it sees that sequence, so the shadow bank is never overwritten under a pending swap. Outputs: `slot_mask[32][8]`, `write_inhibit[32]`, `owner[32][3]` (lowest set bit, registered at the swap), `owner_lut` for the fast read (E-5; FF bank and mux, or a double-banked LUTRAM whose bank bit flips at the swap, chosen by OOC4), `paddle_sid = owner[0]`, `pair_used[4]`, per-SID gains, model and digifix bits, output bits, `map_valid` (cleared by `c64_aresetn`). Effective enable = EXPANSION bit 6 && `map_valid`. Guest resets do not clear the table: `c64_aresetn` is not among the `core_reset_n` terms [V adapter:404-407] |
| `hdl/core/c64/ports/fpga64_sid_iec.vhd` | New generic `SID_COUNT` (default 2 = today's behaviour; 4 or 8 in images). For `SID_COUNT > 2`: a generate of `SID_COUNT/2` `sid_top` instances. Instance 0 behaves as today in legacy mode; its port expressions become muxes between the legacy and map signals. Native `cs` vector = map ? `slot_mask[cpuAddr(9..5)]` & `cs_sid`, with every bit cleared on a write cycle (CPU or REU DMA) in a `WRITE_INHIBIT` slot : legacy. Data return = instance `owner >> 1` `data_out`. Fast selects and the owner-first fast read per E-5. `pot_x/y` per chip by `paddle_sid` (legacy: chip 0 [V :1012-1017]). Per-chip `mode` and `ext_in(17)`. Per-pair reset = `reset` or (map off or pair unused) for pairs ≥ 1, released at `ce_1m`. Generic map `MULTI_FILTERS => 0` on every instance (E-4); the `sid_top` component gains `MULTI_FILTERS` [V :397-399 has only FAST_PORT and EDGE_STAGE; sid_top.sv:8 default 1]. New ports: `sid_map_*` inputs, `sid_audio_o` (SID_COUNT × 18), `sid_ce_1m_o`. Every new input has a VHDL default equal to map off (`sid_map_enable` := '0'; masks, inhibit bits, owners and gains := (others => '0')), because benches instantiate fpga64 directly with named port maps and default generics [V hdl/sim/tb_c64_reu_machine.sv:61-81; tb_c64_machine_schedule.sv:73; tb_c64_machine_interrupts.sv:50]. `scripts/test_c64_mame_sid.py` copies the `sid_top` component and the span from `--\tRight SID Port:` to the end of the `sid : sid_top` instance into a fixed wrapper with `FAST_IO := false` [V scripts/test_c64_mame_sid.py:40-46, 60-66]. That span changes: instance 0's `cs`, data return, pot inputs, `mode`, `ext_in(17)`, `reset` and the new generic come from new signals [V fpga64:987-1039]. Keep the two anchors (the comment and the `sid : sid_top` label) |
| `scripts/test_c64_mame_sid.py` | The wrapper gains a declaration for every new signal in the copied span, tied to map-off constants. The MAME routing checks stay as they are |
| `hdl/sim/tb_c64_reu_machine.sv`, `hdl/sim/tb_c64_ua_stock_bus.sv`, `scripts/test_c64_ua_stock_bus.py` | A `SID_COUNT` parameter (default 2), passed down to fpga64, and a checker option to run at 2 and at the image value (G-STK, §9.4). The trace must not change |
| `hdl/core/c64/ports/sid_top.sv` | One additive output with each chip's fast candidates `{pot_x, pot_y, OSC3, ENV3, bus_data}` for the owner-first fast read (E-5). Its generic default keeps today's text and behaviour; `fast_rd_data` stays as it is |
| `hdl/core/c64/multitini_c64_sid_mix.sv` (new, GPL-3.0-only) | Parallel mixer (§3.7) with the 32-bit sum, `HEADROOM_SHIFT` and `sat24_wide` (clip flag out); DC removal (serial, one shared datapath, Q18.16 state); Average boxcar with a serial divider (UA pattern). Inputs: `sid_audio`, gains, UA L/R, output bits, the NCO tick |
| `hdl/core/c64/multitini_c64_adapter.sv` | Instances of `multitini_c64_sid_map` and `multitini_c64_sid_mix`. At the 48 kHz tick: map off → today's lines, literally `audio_l <= sat24(board_sum_l);` and `audio_r <= sat24(board_sum_r);` (asserted by tests/test_audio_path.py:67-68); map on → the mixer's `sat24_wide` output. `board_sum`, `sat24` and their text stay [V :334-344, 901-902]. `board_clip`: map on → the `sat24_wide` clip flag on the tick; map off → today's expression [V :338-339]. Keep `SID_VERSION = 2'b00` as the legacy model [V :15, 770]. Ports `sid_map_*` from the top |
| `hdl/core/c64/multitini_c64_island_map.sv`, `multitini_c64_cpu_island.sv`, `multitini_c64_fio_port.sv` | No RTL change (E-5). The address replication of §5.2 step M-c is a constraint or physical-optimisation setting, not an island edit |
| `hdl/hdl_sources.txt`, `licenses/files.csv` | New files listed, GPL-3.0-only, Multitini original. `sid_top.sv` stays GPL-2.0-or-later (assumed). No UltiSID code |
| `tests/c64_ua/stock_bus_manifest.json` | Never edited. It is the provenance record of the one-time capture (`captured_from_commit` a0bf372 and the hashes at capture) [V tests/c64_ua/stock_bus_manifest.json:3-5]. The checker writes it only in `--capture` mode, which refuses once the golden file exists, and in compare mode checks only the trace [V scripts/test_c64_ua_stock_bus.py:59, 64, 129-137]. Each milestone runs the checker in compare mode; the new source hashes go into its receipt (`build/c64-ua-stock-golden/validation.json` [V :144]) and into the milestone build receipt |
| `third_party/ultimate_firmware_reference/` (new, test-only) | Byte-exact `software/u64/u64_config.cc` and `LICENSE.txt` from GideonZ/1541ultimate d43546fe, with `NOTICE.md`, `import_manifest.json` (SHA-256 and Git blob ids) and `licenses/files.csv` rows (GPL-3.0, test-only, excluded from product builds), in the pattern of `third_party/ultimate_reference`. That directory pins another commit (fea2798c) and holds FPGA files only [V third_party/ultimate_reference/NOTICE.md:3-5]. `u64_config.cc` has no licence header and relies on the repository licence, like the sampler files there [V 1541ultimate software/u64/u64_config.cc:1-6]. The host oracle test (§9.2) extracts its functions from this path only. Pin the import in `provenance/sources.lock.json` |

E3 (only if chosen at XM6): a new `hdl/core/c64/multitini_c64_sid_serial.sv` serves SIDs 3-8, with pair 0 kept as above. Its filter part derives from `sid_filter.sv` and stays CERN-OHL-S-2.0 [V hdl/core/c64/ports/sid_filter.sv:12-15]; DAC parts from `sid_dac.sv` likewise.

### 4.3 Firmware

| File | Change |
|---|---|
| `firmware/common/c64_sid_map.c`, `firmware/include/multitini_c64_sid_map.h` (new, portable, in `multitini_common` for host tests) | `mt_c64_sid_model_t` (§3.2); `mt_c64_sid_map_build()` (§3.3) → 57 table words; `mt_c64_sid_gains()` (§3.7); `mt_c64_sid_preset()` and `mt_c64_sid_preset_match()` (§3.4); `mt_c64_sid_canonical_legacy()` (§3.3 step 8); `mt_c64_sid_validate()` (the same rules as the HDL); `mt_c64_sid_summary()` (menu text, ASCII); `mt_c64_sid_hwinfo()` (§4.6: legacy part from XM5, sub-commands `$01` and `$02` complete in XM7); `mt_c64_sid_apply_cfg_key()` (§4.5); the SID overlay record and its functions (§3.10): `mt_c64_sid_overlay_effective()`, `mt_c64_sid_overlay_merge()`, `mt_c64_sid_overlay_owns(field)`, `mt_c64_sid_overlay_edit()`; the overlay rule and pan-from-preset rule (§3.2). Original code; the U64 functions are a test-only oracle (§9.2) |
| `firmware/common/c64_title_overlay.c` (CFG) | For the SID group, `_merge()`, `_owns()` and `_edit()` call the per-field functions above. Other groups are unchanged |
| `firmware/common/c64_features.c`, `firmware/include/multitini_c64_features.h` | `mt_c64_capabilities_t` gains `multi_sid`, `sid_count`, `sid_8580`, `sid_io_slots`, gated by package ABI → live ABI → caps (`mt_c64_probe_capabilities()` [V c64_features.c:7]). `mt_c64_expansion_restore()` [V :112] carries bit 6 |
| `firmware/platform/xilinx/main.c` | `configure_c64_expansion()`: build the effective map (stored + overlay, Plan §8.1-8.2, with the overlay rule of §3.2 and the per-field record of §3.10), write the table, COMMIT, poll `C64_SID_STATUS` until `ACCEPTED_SEQ` equals the commit's sequence (bounded wait; the swap comes within one native cycle, or at once while the menu or HOLD has the C64 paused, §3.5), then set bit 6. To leave map mode, clear bit 6 first. `change_c64_expansion()` argument 2: cycle presets (§3.4) from **stored** state (or the overlay when it owns the preset fields), not from the live register (Plan §8.2 rule 2). New `change_c64_sid_setup(item, delta)` for the sub-page with the per-field ownership check of §3.10; dispatch for argument 16 before the `>= 2U` catch-all (Plan §5.1). `supervisor_menu_view()`: new state fields and "(title)" markers per item. `restore_selection_settings()`: restore the map. Every unowned SID edit uses the existing pattern: copy settings, mutate, save, restore on failure, then write registers. An owned edit changes the overlay only and saves nothing |
| UCI `$50` handler (UCI design) | Applies a SID-map change inside the HOLD window of §3.10, together with the REU and UA changes of the same file |
| `firmware/common/c64_debug.c` | UART `:sidstat` (debug session only): table, owners, gains, clip bits, commit status (G-DBG) |
| `firmware/common/c64_option_registry.c` (Plan §8.6) | Rows for every SID key in §4.5: apply class "SID commit", overlay allowed, registry group SID with the per-field mask bit each key sets |
| UCI control target (UCI design) | GET_HWINFO device 1 (U64 shape) and Multitini device 2 (full list) use `mt_c64_sid_hwinfo()` (§4.6) |

### 4.4 Settings (`0:/multitini/c64_sid.bin` + `.mtj`, per profile; Plan §4.1-4.2)

Header 32 bytes: magic `MTSIDMP1`, version u32 = 1, record size u32 = 128, record count u32 (≤ 16), CRC32 of the body, its complement, reserved u32 = 0. Codec in `firmware/platform/xilinx/supervisor_settings.c` beside `turbo_controls.bin` [V supervisor_settings.c:58-59, 900-937]: `mt_supervisor_sid_map_load()`, `mt_supervisor_sid_map_save()`.

Record (128 bytes, little-endian):

| Offset | Size | Field |
|---|---|---|
| 0 | 24 | `core_id` |
| 24 | 1 | legacy SID mode at save (0, 2, 3) |
| 25 | 1 | flags: bit 0 map active, bit 1 auto mirror, bit 2 resample Average, bit 3 group 1 pan from preset, bit 4 group 2 pan from preset (§3.2); bits 5-7 0 |
| 26 | 1 | preset id (0 Custom, 1 Single, 2 Dual $D420, 3 Dual $D500, 4 Triple, 5 Quad, 6 Eight) |
| 27 | 1 | `ultisid_split` 0-7 |
| 28 | 2 | `ultisid_addr[2]` 0-48 |
| 30 | 2 | `socket_addr[2]` 0-48; default 1 (`$D400`) for both, as on the U64 |
| 32 | 1 | socket enables (bit 0, bit 1) |
| 33 | 1 | `ext_split` 0-5 |
| 34 | 1 | `paddle_override` 0/1 |
| 35 | 1 | master volume 0-30 |
| 36 | 1 | headroom 0 Auto, 1 0 dB, 2 −6 dB, 3 −12 dB |
| 37 | 1 | DC removal 0 Auto, 1 On, 2 Off |
| 38 | 2 | group model[2] 0/1 |
| 40 | 2 | group digi[2] 0-3 |
| 42 | 2 | group curve[2] 0-6 |
| 44 | 2 | group resonance[2] 0-1 |
| 46 | 2 | group volume[2] 0-30 |
| 48 | 2 | group pan[2] 0-10 |
| 50 | 4 | socket volume[2], socket pan[2] |
| 54 | 2 | reserved, 0 |
| 56 | 40 | per SID n × 5: addr (0xFF Follow, 0 Unmapped, 1-48), model (0xFF Follow, 0, 1), volume (0xFF Follow, 0-30), pan (0xFF Follow, 0-10), reserved 0 |
| 96 | 32 | reserved, 0 |

- Any out-of-range byte or non-zero reserved byte invalidates the whole file: legacy behaviour, a status warning, no write to `settings.bin` (Plan §4.2).
- Staleness and downgrade per Plan §4.2. "Nearest legacy mode" written to the expansion byte while a map is active: 0 if one SID is mapped, 2 if SID 2 is at `$D420`, else 3.
- The overlay (title `.cfg`, UCI LOAD_CONFIG) is never written here (Plan §8.2 rule 3). An owned menu edit leaves the file byte-identical (§9.2).
- This design uses no bit of `settings.bin` byte 26.

### 4.5 Menu and `.cfg` keys

**Options rows** (Plan §5.1): argument 2 "SID audio" shows the preset name or "Custom (n SIDs)"; argument 16 "SID setup..." (shown with caps bit 6) opens the sub-page.

**SID sub-page** (the generic core sub-page in `menu.c`, own item indexes, Left/Right adjust, Escape leaves; Plan §5.2):

| Item | Values |
|---|---|
| 0 Layout | preset name or Custom; Left/Right cycles presets |
| 1 SIDs | read-only summary, for example "8: D400 D420 D480 D4A0 D500 D520 D580 D5A0", plus "uncovered->SID 1 (reads)" when §3.3 step 6 applied |
| 2 Address mirroring | On / Off |
| 3 Select SID | 1..count |
| 4 Address | Follow (`$D480`) / Unmapped / `$D400`..`$D7E0`; Left/Right ±$20 |
| 5 Volume | Follow (0 dB) / U64 volume labels |
| 6 Pan | Follow (Left 2) / U64 pan labels |
| 7 Model | Follow (6581) / 6581 / 8580 (only with caps bit 11) |
| 8 Master volume | U64 volume labels |
| 9 Headroom | Auto (shows the result) / 0 dB / -6 dB / -12 dB |
| 10 DC removal | Auto / On / Off |
| 11 Resampling | Point / Average |
| 12 Reset SID setup | back to the profile default (Dual $D500) |

- 13 items, below `MT_MENU_MAX_ITEMS` = 20 [V firmware/include/multitini_menu.h:19].
- Each change commits live; no restart; it applies while paused (Plan §5.1). It saves `c64_sid.bin` only when it touches no field the overlay owns (§3.10); otherwise it edits the overlay and saves nothing.
- All menu and help text is ASCII: the UI draws any byte outside 32-127 as '?' [V firmware/common/ui.c:9-11]. The arrows and minus signs elsewhere in this document are typography; menu strings use "->" and the hyphen-minus, as the U64 volume labels do ("-6 dB") [V u64_config.cc:292-295]. The "SID audio" help changes to "Choose a SID layout; SID setup... edits addresses, pan and volume."
- Items whose field the overlay owns show "(title)" (Plan §8.2 rule 4; §3.10).

**`.cfg` keys** (registry rows; CFG owns the reader and the matching rules):

| Section | Item | Maps to |
|---|---|---|
| `[SID Addressing]` | UltiSID 1 Address, UltiSID 2 Address | `ultisid_addr[]` (class ADDR) |
| | UltiSID Range Split | `ultisid_split` (ADDR) |
| | Auto Address Mirroring | `auto_mirror` (ADDR) |
| | SID Socket 1 Address, SID Socket 2 Address, Ext DualSID Range Split | socket fields (mirror input; U4) (ADDR) |
| | Paddle Override | stored, no effect; no class |
| | Visual SID Address Editor | ignored (function item) |
| `[SID Sockets Configuration]` | SID Socket 1, SID Socket 2 | socket enables, only if U4 adopts socket emulation; detected-type and hardware items ignored. NOT_APPLICABLE until XM7, and after XM7 too unless U4 is adopted |
| `[UltiSID Configuration]` | UltiSID n Combined Waveforms | group model (needs caps 11) (class MODEL) |
| | UltiSID n Digis Level | group digi (§3.6) |
| | UltiSID n Filter Curve, UltiSID n Filter Resonance | stored, no effect, reported |
| `[Audio Mixer]` | Vol Master | master |
| | Vol UltiSid n (alias Vol UltiSID n), Pan UltiSID n | group volume, pan (class MIX) |
| | Vol Socket n, Pan Socket n | socket volume, pan (only if U4 adopts emulation) |
| | Vol Sampler L/R, Pan Sampler L/R, Drive, Tape | not this design (the UA design decides; ignored until then) |
| `[U64 Specific Settings]` | SID Player Autoconfig, Allow Autoconfig uses UltiSid | ignored (no SID player) |
| `[Multitini]` | SID audio (CFG K17) | preset: every field a preset sets (§3.10); values Single, Dual $D420, Dual $D500 (XM2), plus Triple, Quad, Eight (alias Tuneful Eight) from XM7 |
| `[Multitini SID]` (new, XM7) | SID n Address, SID n Model, SID n Volume, SID n Pan (n = 1-8; value "Follow" allowed); SID Headroom; SID DC Removal; SID Resampling | per-SID layer and output settings; these keys express what a U64 cannot (#664) |

- Volume and pan values use the U64 labels, including " 0 dB" with its leading space (M6).
- A SID key that needs a missing capability (8580, IO slots) is skipped and reported, per Plan B8. The split key is never skipped: on a 4-SID image only the parts the image lacks are dropped, after mirroring, and reported (§3.3 step 4a).
- Overlay keys follow the overlay rule (§3.2): the U64 address, volume/pan and model keys set the ADDR, MIX and MODEL class bits, so stored per-SID fields read as Follow unless the same overlay sets them. A "Pan UltiSID n" key clears that group's `pan_from_preset` in the overlay.
- SID lines resolve in file order, per field (§3.10).
- Keys match by section. `[Speaker Mixer]` repeats "Vol UltiSid n" and is ignored [V Heartbeat cfg:24-35].
- CFG must list `[Multitini SID]` as a known section from XM7 (its section table, CFG §3.6.1). Otherwise its unknown-section rule would skip every `[Multitini SID]` key. A parser test with a `[Multitini SID]` fixture guards it (§9.2). U13 keeps the separate section; the alternative is to put these keys into `[Multitini]`.

### 4.6 UCI GET_HWINFO content (`mt_c64_sid_hwinfo()`)

Facts: `$04 $28` is deprecated upstream [D 1541u-documentation uci/control_target.rst:169]. The U64 always sends UltiSID 1 and UltiSID 2 as frames 1-2, then enabled sockets: at most 4 frames [V 1541ultimate software/io/command_interface/control_target.cc:418-468]. Any device other than 0 and 1 gets INVALID PARAMS [V control_target.cc:380-394 and the final `else`]. The only known client, SIDDetector-II, sizes its buffer for 4 frames (22 bytes), reads frames 1-2 at fixed offsets, and lists "Frame count > 4" as untested: "drain loop will stop at 22 bytes and leave data in FIFO — UCI dirty" [D SIDDetector-II UCI.md:179, 369-371; CODE-REVIEW.md:892-902]. No U64 user report asking for a longer list was found [I].

This document owns the frame content. UCI owns the transport and the sub-command dispatch, and points here.

**Sub-command `$01` (U64 shape):**
- count = 2 + emulated enabled sockets, at most 4. Frame 1 = SID 1, frame 2 = SID 2, frames 3-4 = emulated sockets 1 and 2 in U64 order (only if U4 adopts emulation).
- Frame: lo/hi = the SID's configured base address, not the mirror-cleared base. The U64 reports the mirror-cleared `_BAK` value, which is wrong whenever every SID shares a set address line [V control_target.cc:425-439; u64_config.cc:864-870]; this fix is kept. lo2 = hi2 = 0. Type = 0x80 | 2 when the SID decodes exactly one slot, 0x80 | 3 when it decodes more (mirrors, fallback slots); emulated sockets 0x80 | 4 / 5.
- An unmapped SID 1 or 2 reports address `$0000`, type `$80` (the U64 reports `$D010`, §2.5).
- Legacy modes (every image from XM5): count 2, 11 bytes. Mode 0: SID 1 `$D400`/`$83`, SID 2 `$D400`/`$83`. Mode 2: `$D400`/`$83`, `$D420`/`$83`. Mode 3 (default): `$D400`/`$83`, `$D500`/`$83`. Each SID decodes 16 or 32 slots in these modes, so the type is `$83`. A U64 with the same two addresses and Auto Address Mirroring on reports the same frames [I from u64_config.cc:2406-2465 and control_target.cc:421-439].
- On the Eight preset `$01` lists SID 1 and SID 2 only. SIDDetector-II finds the other parts by its OSC3 method, as it does on a U64 [R SIDDetector-II TODO.md:5-11].

**Sub-command `$02` (Multitini; decision U14):** count = every mapped SID plus emulated sockets (at most 10); frames in SID index order, same frame format; at most 1 + 10 × 5 = 51 bytes. Existing clients never send `$02`, and a U64 rejects it, so it cannot break them.

**Delivery.**
- XM5 (UCI release): the legacy part of `mt_c64_sid_hwinfo()` (sub `$01` for modes 0, 2 and 3) ships with the UCI control target, because XM5 ships before XM7 and every image then runs a legacy mode. Before XM7, sub `$02` answers `81,INVALID PARAMS`, as a U64 does.
- XM7: the map-mode part of `$01` and sub `$02`.

---

## 5. Clocks, resets, crossings and timing rules (review list)

### 5.1 Items to review

| # | Item | Kind | Primitive | Review point |
|---|---|---|---|---|
| Plan C2 | EXPANSION bit 6 | wider existing word crossing | `controls_i` word mailbox, coherent with bits [5:0] | REU, UA, UCI and SID ownership change on one `c64_clk` edge. Layout and `RESET_VALUE` in Plan §3.5 |
| Plan C8 | SID table, `control_clk` → `c64_clk` | new crossing | `multitini_async_mailbox`, `{index, data}` words and a swap token with an 8-bit sequence; destination `dst_ready` = 1 | in order; never stalls; shadow never overwritten under a pending swap; the active table swaps on one edge at `ce_1m`, or on the next edge while paused. The active table is about 670 bits (masks 256, IO masks 128, params 256, output 32), streamed entry by entry |
| Plan C18 | swap report, `c64_clk` → `control_clk` | wider existing word crossing | `status_i` +9 bits `{sid_map_valid, sid_active_seq}` | a match counts only with valid = 1; `ACCEPTED_SEQ` never moves backwards; resend after a `c64_aresetn` (B1) |
| R-SID1 | pairs ≥ 1 | reset | held in reset while the map is off or the pair is unused; released at `ce_1m` | same clock, no crossing (R8) |
| R-SID2 | `c64_aresetn` | reset | clears `map_valid` and the active table; the streamer resends | guest resets keep the table (§4.2) |
| Plan C17 | fast SID read decode (E-5) | logic on the T65 address nets | — | shares A0-A9 with the half-period T65 → island BRAM path that sets fast-island slack; §5.2. Plan §11 carries it as C17 beside C3 (`$D0BC` compare), which loads the same nets |
| — | mixer, DC removal, Average | logic | `c64_clk` only, 31.7 ns period | none |

No new clock. No new MMCM.

### 5.2 Fast read decode and the half-period island path

Facts:
- The island drives the island memory and the FIO read path from the same T65 address: `cpu_mem_addr = cpu_address` and `fio_rd_addr = cpu_address[9:0]` [V hdl/core/c64/multitini_c64_cpu_island.sv:163, 165]. The island BRAMs read on the falling edge of `clk_cpu` at that address [V hdl/core/c64/multitini_c64_island_memory.sv:60-75]. Fast read data returns through `io2_q` on the rising edge [V cpu_island.sv:209].
- So every load that the fast SID read decode puts on A0-A9 also loads the address nets of the half-period T65 (rising) → island BRAM (falling) path.
- dev7 PAL: the worst path of the group is the other half-period leg, BRAM (falling) → T65 `BAL_reg[8]` (rising), at +0.030 ns [V dev7 timing_summary.rpt:65745-65752]. The listed paths include T65 `MCycle_reg` → main RAM `DIBDI` (write data) at +0.293 ns [V :66230-66233]. No T65 → BRAM address path is among the listed paths, so its slack is not known [O10].
- TD records that the FIO read muxes already load T65 A0-A3 with about 136 + 40 loads, which drive the critical A → BRAM path [D TD §9 R2]. Today's SID fast select uses only A5 and A8 (modes 2 and 3) [V fpga64 `fast_sid_select`, :371-381], and one register case on A4..A0 in one `sid_top` instance [V sid_top.sv:384-399].
- A direct port of the map decode (8-bit slot mask per slot for `fast_rd_cs`, per-pair `fast_rd_data`) would add loads on all of A5-A9 (A6, A7 and A9 are new) and multiply the A0-A4 case by the number of pairs: 4 instances at 8 SIDs [I].

Mitigation plan, decided before XM6:

| Step | Measure | When |
|---|---|---|
| M-a | Owner-only read decode: the fast read needs only the 3-bit owner from A9..A5 (E-5). Roughly 25 LUT loads on A5 and A6 and fewer on A7-A9 with an FF-bank mux, about 3 per bit with a double-banked LUTRAM [I; OOC4 measures both] | in the RTL from the start |
| M-b | Owner-first candidate mux: select the owner's five candidates first, then one register case on A4..A0. The A0-A4 loads then grow by one case, not by one case per pair [I] | in the RTL from the start (`sid_top` additive output) |
| M-c | Address replication for the FIO read path: replicated drivers of the T65 address bits A0-A9 that serve only the FIO read decode, through `MAX_FANOUT` or physical-optimisation replication of the T65 address registers. This is TD §9 R2 step F1 (Pblock plus address replication / `MAX_FANOUT`); no T65 RTL edit | if a candidate misses the island gate |
| M-d | If the legacy fast read path then limits: move legacy mode to the owner-first path too. E-1 then holds by bench (B3) instead of by construction | after M-c |
| M-e | Registered fast read address for SID reads (one more fast slot per SID read, like TD's `VIC_READ_GAP` = 3 lever). Needs a pacing check against TD §1.7 [O] | last resort |

What XM6 records per candidate, in addition to §6.4: the load count and worst net delay of each T65 address bit A0-A9; the worst slack of both half-period legs (BRAM → T65; T65 → BRAM address and data); the full-period T65 → `io2_q` path; and the island gate result.

---

## 6. Resource and timing budget

### 6.1 Baseline

The dev7 PAL image (0.4): 36,689 LUT, 128 BRAM tiles, 33 DSP, 12,519 slices (94.13 %), fast-island WNS +0.030 ns under a one-build waiver. The SuperCPU Detect, UCI and W5100S bridge milestones (XM3-XM5) add about +2,200 LUT with the Plan §10.1 ×2 margins and 1 BRAM tile for the UCI buffer (a full RAMB36 tile, UCI §4.7), so the base for the SID candidates is about **38,900 LUT (73.1 %) and 129 tiles** [I]. The UCI design's own estimate is lower; this design keeps the larger figure.

### 6.2 Candidates (PAL; base = measured 2-SID `sid_top`: 2,629 LUT, 1,963 FF, 2 RAMB36, 7 DSP [V dev7 utilization.rpt:82])

"Density needed" is the image LUT count divided by 12,635 (95 % of 13,300 slices): the average LUT per slice the placer must reach to meet the slice ceiling. dev7 runs at 2.93. A 7-series slice holds four 6-input LUTs [D Xilinx UG474], so 4.0 is the physical limit; dense designs reach about 3.3-3.6 in practice [I].

| Candidate | ΔLUT (raw) | ΔLUT with ×2 on new RTL | ΔFF | ΔBRAM tiles | ΔDSP | Image LUT (base ≈ 38,900) | Image BRAM tiles (base 129) | Density needed (LUT/slice) | Timing risk |
|---|---|---|---|---|---|---|---|---|---|
| E1-4 (2 pairs, 6581) | +3,400-4,000 | +4,200-5,300 | +3,300 | −1 trimmed [O1]; +2 untrimmed | +17 | ≈ 43,100-44,200 (81-83 %) | 128 / 131 | 3.41-3.50 | moderate |
| E1-4 + 8580 | +4,300-5,700 [I] | +5,100-7,000 | +3,400 | −1 / +2 | +17 | ≈ 44,000-45,900 (83-86 %) | 128 / 131 | 3.48-3.63 | moderate |
| E1-8 (4 pairs, 6581) | +8,900-9,500 | +9,900-11,100 | +7,400 | 0 trimmed [O1]; +6 untrimmed | +39 | ≈ 48,800-50,000 (92-94 %) | 129 / 135 | 3.86-3.96 | high |
| E1-8 + 8580 | +10,600-12,900 [I] | +11,600-14,500 | +7,600 | 0 / +6 | +39 | ≈ 50,500-53,400 (95-100 %) | 129 / 135 | 4.00-4.23 (beyond the limit) | very high |
| E2-8 (pair + quad + pair) | ≈ E1-8 − 600-700 | — | ≈ E1-8 − 300 | ≈ E1-8 − 0.5 | +38 | ≈ 48,100-49,400 (90-93 %) | ≈ 129 | 3.81-3.91 | high |
| E3 hybrid 8 (pair 0 + serial SIDs 3-8) | +2,900-4,500 | +3,900-6,000 (×1.5 engine, ×2 rest) | +2,500-3,000 | −1 to −0.5 | +21 | ≈ 42,800-44,900 (80-84 %) | 128-128.5 | 3.39-3.55 | low-moderate; design risk high |

Reading [I]:
- Slice use at 69 % LUT is a packing outcome: the placer spreads logic while room exists, so the density rises as the design grows. The density needed is the meaningful number, and only a full build shows whether it is reached.
- E1-4 and the E3 hybrid need about 3.4-3.6 LUT per slice: possible, not assured.
- E1-8 needs about 3.9 and E1-8 + 8580 more than 4.0. E1 at 8 SIDs is unlikely to meet the 95 % slice ceiling (R1).
- FF stays below 36 % and DSP below 75 of 220 in every case. BRAM stays at or below the 136-tile ceiling, also untrimmed.
- NTSC (36,769 LUT, 12,362 slices at dev7) is within a few hundred slices of PAL; XM6 builds both.

### 6.3 Basis

[I]: each extra pair = the measured pair (7 DSP); map and decode +300-600 LUT and +700-900 FF; control regs and streamer +200-350 LUT; mixer +300-650 LUT and 2 DSP per SID plus 2 for UA; DC removal ≈ 420 FF at 8 SIDs (8 × 34 state + 8 × 18 output), inside the FF column's rounding; 8580 = +250-500 LUT per pair table plus 30-60 LUT per voice; E3 engine ≈ 2,100-2,950 LUT for 6 chips, scaled to 18 voices with 2 filter datapaths of 3 chips each. BRAM trims assume `MULTI_FILTERS = 0` fits one RAMB18 per table (O1).

Calibration: earlier new-RTL estimates in this repository ran 25-300 % low. Ultimate Audio was estimated at 2,200-2,600 LUT and the combined image at 28,500-28,900 LUT [D TD §4.3]; dev6 measured 36,187 LUT for the image and dev7 measures 9,066 LUT for the UA block [V dev7 utilization.rpt:138]. Hence the ×2 column. Replicated measured blocks (E1 pairs) are reliable to about ±10 % [I].

Levers, cheapest first (Plan §10.3): trim each `f6581_curve` to curve 0; replace the UA 7-multiplier mixer by a serial mixer shared with the SID mix (≈ −1,000 LUT, −6 DSP [D UAD §2.7]); true dual-port buslogic ROMs (−5 tiles); pack the video frame banks (≈ −12 tiles).

### 6.4 What XM6 records, and the pass rule

- Per candidate, PAL and NTSC: LUT, FF, BRAM tiles, DSP, slices, achieved LUT per slice, WNS/WHS per clock group and the worst path of each group, plus the §5.2 items.
- Pass (G-FIT): the Plan §10.3 ceilings: BRAM ≤ 136 tiles; routed WNS > 0 and WHS > 0 on every group; fast-island WNS at least +0.100 ns (or the floor of the P2 waiver policy, if the user chooses it); slices ≤ 95 %.
- If E1-8 fails and E3 does not exist yet, ship 4 SIDs (XM7a) or apply the levers and retry.

---

## 7. Fix-instead-of-copy list

These are this design's deviations from the U64 (Q24). Each gets a D-number from the multi-SID range D60-D69 of Plan §3.6 at the 0.26 spec edit.

| U64 issue | Matters? | Our behaviour |
|---|---|---|
| #733: a write to an unmapped `$D4xx` hung the menu | Yes | Uncovered writes are inert (`WRITE_INHIBIT`); reads return the owner's registers and bus latch. The decode is a total function, so nothing can hang (§3.3 step 6, §3.5) |
| #664: a right-channel SID at `$D500` without breaking the TT8 layout | Yes | Per-SID pan and address overrides (§3.2) and `[Multitini SID]` keys |
| #668: 8 individually pannable SIDs | Partly | Static per-SID volume and pan. Register-driven pan is not planned |
| Shared model, curve and digi per 4 parts (M1) | Yes, for mixed-model tunes | Per-SID model when caps bit 11 is set |
| GET_HWINFO lists 2 UltiSIDs, mirror-cleared bases, `$D010` for unmapped (§2.5) | Partly: wrong bases matter; no user report asking for a longer list was found | `$01` keeps the U64 shape (at most 4 frames, which SIDDetector-II's buffer expects) with configured bases and `$0000`/`$80` for unmapped; the full list is on the Multitini sub-command `$02` (§4.6) |
| Overlap read priority undocumented (A10) | Yes | Documented: the lowest-index SID owns the read |
| Mixer wraps above full scale (#744, UAD §2.7) | Yes | 32-bit sum with `sat24_wide`, Auto headroom as a master-stage shift on every source, DC removal |
| 4 parts share one mixer channel (M2) | Yes | Per-SID gain pair |
| Default 8580 filter with 6581 waveforms (M1 defaults) | Low | One coherent model per SID |
| Mixer byte named `vol_right` drives left (M5) | Low | Gains named by physical side; a bench checks "Left 5" = left only |
| Freezer `$D418` click (u64_config.cc:1302-1317) | N/a | Our firmware never writes SID registers; the menu mutes at the board output (`ui_audio_mute` in the top) |
| Autoconfig ignores the UltiSID split, mixer caps at 4 | N/a | No SID player |
| `auto_mirror` counts disabled sockets | No issue reported | Copied, together with the U64 socket default `$D400`, so partial U64 `.cfg` files give the same addresses (§3.2, §3.3 step 3) |
| Engine issue: noise taps (#954) | Already fixed | MiSTer uses `lfsr[22] ^ lfsr[17]` and models the combined-waveform lock-up [V third_party/c64/upstream/rtl/sid/sid_voice.sv:113-127] |
| ADSR 00→FF wrap (#702), 8580 OSC3 delay (#951) | Check | Not verified in the MiSTer engine [O8] |

---

## 8. Risks

| # | Risk | Level | Mitigation |
|---|---|---|---|
| R1 | E1 at 8 SIDs does not fit: ≈ 48,800-50,000 LUT (92-94 %) with the other features, needing about 3.9 LUT per slice for the 95 % slice ceiling against 2.93 today | High | XM6 decides; 4 SIDs first (XM7a); E3 hybrid for 8 (§6, U1) |
| R2 | Fast SID read decode: (a) 2-3 more LUT levels on the full-period T65 → `io2_q` path (owner lookup, 8:1 select); (b) more loads on the T65 address nets, which also drive the half-period T65 → island BRAM path that sets fast-island slack (+0.030 ns on dev7 PAL) | High | Owner-only decode and owner-first mux in the RTL from the start (M-a, M-b); address replication (M-c, TD §9 R2 F1); per-candidate load and slack records in XM6 (§5.2) |
| R3 | Legacy 1-2 SID output changes by accident | Moderate | Legacy path kept as today's text; E-1 benches; G-STK; G-AUD |
| R4 | Runtime 8580 cost unknown: estimate +860-1,720 LUT at 4 SIDs, +1,720-3,440 at 8 [I] | Moderate | OOC measurement (OOC3); 8580 only if the fit passes. TT8 and Heartbeat set 8580 (A8), so without it they play on 6581 engines and sound different |
| R5 | `MULTI_FILTERS = 0` does not trim to one RAMB18 | Low | OOC check (OOC2); otherwise 2 RAMB36 per pair (E1-4: +2 tiles, E1-8: +6) |
| R6 | Loudness and clipping differ from the U64 (M8 unknown) | Moderate | Auto headroom, DC removal, saturation; measure peaks (O6); board A/B by ear |
| R7 | New `control_clk` → `c64_clk` table crossing (Plan C8) | Moderate | One in-order mailbox that never stalls (`dst_ready` = 1), swap token, swap at `ce_1m` or at once while paused, sequence returned through `status_i`, resend on destination reset; bench B1 |
| R8 | Stale voices when switching maps during playback | Low | Pairs ≥ 1 held in reset while unused; released at `ce_1m` |
| R9 | Test anchor churn: `scripts/test_c64_mame_sid.py` copies the `sid_top` component and the fpga64 span from `--\tRight SID Port:` to the end of the instance, and map mode changes both; `tests/test_audio_path.py` literals | Low | Keep the two anchors and the literals; the wrapper gains declarations for the new signals, tied to map-off constants; MAME routing results unchanged |
| R10 | E3 exactness: chips 3-8 update at slot k instead of `ce_1m` | High (if E3) | Pair 0 unchanged; `ce_1m` register snapshot (+936 FF); lockstep bench against `sid_top` pairs |
| R11 | Test content cannot be committed (TT8, Heartbeat, HVSC/DeepSID tunes) | Low | Board tests with user-supplied files under `build/`; our own fixtures for `.cfg` key lines |
| R12 | Menu complexity | Low | Presets first; sub-page for experts |
| R13 | Uncovered slots (mirroring off, layouts with gaps such as Triple, dropped parts) read SID 1's registers, which may surprise software that probes for an empty slot | Low | Writes there are inert, so no SID is garbled; shown in the sub-page summary; decision U3 |
| R14 | The overlay rule (§3.2) hides stored per-SID overrides while a U64 `.cfg` is active | Low | Intended: U64 files then give U64 layouts. "(title)" markers in the menu; `[Multitini SID]` keys in the same file keep per-SID values |
| R15 | The dev7 baseline already misses the +0.100 ns fast-island ceiling (+0.030 ns PAL) and sits at 94.13 % slices | High | Plan decision before XM6: timing-recovery milestone XM0 (recommended) or waiver policy (0.4); levers of §6.3 |
| R16 | A `$50` SID change under HOLD cannot reach paused, or REU idle, while a program runs an REU loop | Low | Bounded wait as UCI §3.8.6; nothing is applied on timeout; UCI T-U4 and the runtime test cover it (§9.2) |

---

## 9. Tests and gates

### 9.1 Simulation benches (small tests before any full build)

| # | Bench | Content | Pass |
|---|---|---|---|
| B1 | `hdl/sim/tb_c64_sid_map.sv` (new) | SID_DATA/COMMIT protocol, every reject code (1-7), a rejected commit changes nothing, bank flip; swap at `ce_1m` while running; commit while paused (swap on the next edge); commit during a guest reset (swap at the next `ce_1m`); two commits in one paused menu visit (both accepted, in order, no code 6 once the first swap is reported); `ACCEPTED_SEQ` moves only after the swap; the mailbox never stalls (`dst_ready` = 1); resend after `c64_aresetn`; `map_valid`; a guest reset keeps the table | all cases |
| B2 | `hdl/sim/tb_c64_sid_decode.sv` (new) | First, against today's fpga64 text (`SID_COUNT` 2, before any edit): `fast_sid_select` equals the native select for all 8 `sid_mode` values × A11..A0 wherever `cs_sid` = 1. This run is kept as the legacy baseline; no existing bench covers it (E-1). Then exhaustive A11..A0 × legacy modes 0-7 × {every preset, 1,000 random valid tables}: native `cs`, fast `fio_cs`, `fast_rd_cs`, owner, latch source, data return; native = fast for every address; the owner-only `fast_rd_cs` and the owner-first data mux equal the slot-mask reference (lowest selected chip) for every table; `WRITE_INHIBIT` slots change no chip and no latch on native, fast and DMA writes | baseline identical before and after the edit; bit-exact against a reference model |
| B3 | `hdl/sim/tb_c64_sid_array_legacy.sv` (new) | Today's single `sid_top` instance as reference vs the array with map off, and vs the array with map on plus legacy tables and gains (E-3); random native and FIO traffic; `audio`, `data_out`, `fast_rd_data`, board sum every edge | identical every edge |
| B4 | `hdl/sim/tb_c64_sid_mix.sv` (new) | Unity identity; hard pans; extreme gains; saturation: 8 SIDs at GAIN 0xFF on both sides, full-scale same-sign samples and UA at 0xFF, both signs → 0x7FFFFF / 0x800000 with `board_clip` set; `HEADROOM_SHIFT` 0-2 scales SIDs and UA alike (the SID:UA ratio does not depend on the shift); UA path; DC removal (block run standalone with a fast enable): constant +17,000 and −17,000 LSB inputs decay to ≤ 116 LSB after 5 time constants and to ≤ 1 LSB after 10, symmetrically, with no step at enable; Average vs a reference | bit-exact against a Python model |
| B5 | `hdl/sim/tb_c64_sid_osc3_8.sv` (new) | TT8 table: drive voice 3 of SID n, read OSC3/ENV3 at all 32 slots; only SID n's slots change (the SIDDetector-II method, R1); POT `$FF` on every SID except the paddle SID. Triple table: a voice-3 write at `$D460` leaves SID 1's OSC3 and registers unchanged. TT8 table at `SID_COUNT` 4: writes at `$D500` change no chip | all cases |
| B6 | `tb_c64_turbo_sid_fast.sv`, `tb_c64_turbo_sid_edges.sv`, `tb_c64_turbo_sid_native.sv` (extend) | Fan-out writes to 2-8 chips, latch source = owner, edge stage per chip, native cancel per chip, owner-first fast reads | existing cases unchanged, new cases pass |
| B7 | `hdl/sim/tb_c64_sid_trace_replay.sv` (new, for O6) | Replays SID write traces of test tunes (captured with `scripts/c64sim`, not committed) into the array; reports per-SID and summed peaks | measurement, no pass bit |
| — | Unchanged must-pass | `tb_sid_output_schedule`; `tb_c64_sid_socket` (`scripts/test_c64_mame_sid.py`, wrapper declarations added only, MAME routing results unchanged); `tb_c64_ua_stock_bus` (G-STK at `SID_COUNT` 2 and at the image value); `tb_c64_ua_mix`; REU, schedule and interrupt machine benches (new fpga64 inputs at their defaults) | unchanged |
| — | Optional, for E3 | `tb_c64_sid_serial_lockstep.sv`: serial chips vs `sid_top` pairs, bit-exact audio, OSC3/ENV3 at every native read edge, latches | identical in exact mode |

### 9.2 Host and runtime tests

| Test | Content |
|---|---|
| `tests/host/test_c64_sid_map.c` (new) | U64 oracle: `get_sid_addresses`, `fix_splits`, `auto_mirror`, `combine_mixer_gain` and their tables, extracted at test time from `third_party/ultimate_firmware_reference/` (byte-exact `u64_config.cc` pinned to d43546fe, GPL-3.0, test-only, §4.2), never from another copy. Exhaustive: 49 × 49 UltiSID addresses × 8 splits × mirror on/off, in two socket passes: sockets Unmapped, and sockets at the U64 default `$D400`, disabled, with ext_split 0-5. Sampled socket-address sweeps. Our slot masks must equal the oracle decode (UltiSID and part per slot) for every covered slot; an uncovered slot (no oracle SID) must have a read owner and `WRITE_INHIBIT`. TT8 list and mirrors (+$40, +$200). TT8 keys at `SID_COUNT` 4 (§3.3 step 4a: SIDs 1-4 with U64 mirrors; the `$D500 $D520 $D580 $D5A0` slots and mirrors write-inhibited). Presets, with `mt_c64_sid_preset_match()` comparing the Triple overrides. Canonical legacy detection, including one SID owning every slot. Gains equal the U64 `combine_mixer_gain`/`pan_ctrl` math for all 31 × 31 × 11 combinations. UA gain: for a Heartbeat-like fixture (8 SIDs + UA) the gains do not change with h (`HEADROOM_SHIFT`), and Vol Master −6 dB halves UA_GAIN as it halves the SID gains. Auto headroom. Validation codes match the HDL rules |
| same file, overlay cases | Overlay rule: a stored set of per-SID overrides plus the TT8 key fixture gives exactly the TT8 slot masks `1 << (A5 + 2·A7 + 4·A8)` and the TT8 gains. Pan from preset: stored Dual $D500 + "UltiSID 2 Address=Unmapped" gives sound on both sides; the same plus "Pan UltiSID 1=Left 5" gives left only. Two `$50` files merged (TT8 addresses, then "Pan UltiSID 1=Center" only): addresses kept, pan replaced, ADDR and MIX set. File order per field: `SID audio=Eight` then `UltiSID 2 Address=$D500`, and the reverse order. Ownership of per-SID fields through a class bit |
| `tests/host/test_supervisor_settings.c` | `c64_sid.bin` codec: CRC, every out-of-range byte → invalid → legacy, stale legacy mode → map off, 16 records, downgrade (older firmware ignores the file) |
| `tests/host/test_core_frontend.c` | Argument 2 labels and preset cycle per SID count; argument 16 only with caps bit 6; sub-page items; "(title)" per owned item; largest row set ≤ 20 (Plan §5.1) |
| `tests/test_c64_expansion_runtime.py` | Commit-then-enable ordering; leaving map mode clears bit 6 first; handlers start from stored state; overlay never saved. An owned sub-page edit leaves `c64_sid.bin` and `settings.bin` byte-identical; an unowned one saves. `$50` SID change under HOLD: HOLD set, paused seen, table written, COMMIT, `ACCEPTED_SEQ` polled, `C64_EXPANSION_CONFIG` written once with bit 6 and the REU/UA bits, HOLD released; timeout before step 2 writes nothing |
| `.cfg` parser suite (CFG) | Our own fixture with the TT8/Heartbeat SID key lines (facts only, not the original files); partial U64 key sets; " 0 dB"; "Vol UltiSid"/"Vol UltiSID"; a `[Multitini SID]` fixture whose keys are applied, not skipped as an unknown section; `[SID Sockets Configuration]` NOT_APPLICABLE unless U4 is adopted |
| UCI host test (UCI, `test_c64_uci_control.c`) | GET_HWINFO `$01` in XM5: legacy modes 0, 2, 3 give the frames of §4.6, 11 bytes; `$02` → `81,INVALID PARAMS`. From XM7: `$01` at most 4 frames for every preset (Eight included) and for unmapped SID 1/2; `$02` the full list |
| `tests/test_interface_spec.py`, `make check-generated` | New constants (G-SPEC) |
| `tests/test_audio_path.py` | Existing literals unchanged (the legacy `audio_l <= sat24(board_sum_l)` lines sit under the map-off branch); add: the map-mode mixer instance is present, `sat24_wide` takes a 32-bit input, and `SID_VERSION = 2'b00` is kept |

### 9.3 Board tests (on hardware, PAL; NTSC when it closes at 64x)

Every gate runs inside `:debug on` with `:sidstat` logged, plus one control run without a debug session (TD board procedure). Test content that may not be committed is user-supplied and kept under `build/`.

| # | Test | Expected |
|---|---|---|
| BT1 | G-REU0 (Expand by Bonzai disks 1 and 2, reudiag) with defaults; again with the Eight preset active | Unchanged REU behaviour in both |
| BT2 | Dual-SID modes 0/2/3 at turbo (the existing TD board item) | Unchanged |
| BT3 | SIDDetector-II ≥ 1.4.37 on Single, Dual $D500, Quad, Eight (Command Interface Off, the default) | Finds 1, 2, 4, 8 SIDs at the preset addresses |
| BT3b | As BT3 with Command Interface On: on Single and Dual $D500 from XM5, and on Eight from XM7. BT3 alone never exercises GET_HWINFO, because the UCI is off by default | The same SIDs as in BT3; `uci_resp+22` (the `$DF1F` status byte after the drain) reads `$00` [D SIDDetector-II UCI.md:264, 347] |
| BT4 | The Tuneful Eight (Ultimate Edition) with its own `.cfg` beside the `.prg` | 8 distinct SIDs; 8580 if caps bit 11, else reported and played as 6581 |
| BT5 | SID-Wizard 1.9x in 4SID mode, SIDs set to the Quad addresses | 4 SIDs audible and independent |
| BT6 | DeepSID 4SID and 8SID tunes (WebSid player), wrapped into a test PRG plus a `[Multitini SID]` `.cfg` by a test tool (outputs under `build/`, not committed) | Plays with the addresses from the file header |
| BT7 | #922 digi tunes (Insomnia, SkyBuster). Multitini has no SID player (§1.3), so play them with the demo "Next Level" PRG, which ships and plays them [R #922], or wrap the `.sid` files with the BT6 tool extended to PSID v2. Compare Dual $D500 (legacy) against the Eight preset, each with DC removal On and Off, and record the headroom in use. On Eight, SID 1 is panned Left 2, so judge distortion, not balance | Digis clean in every run; level differences match the recorded headroom |
| BT8 | Heartbeat Soundtracker with its `.cfg` (needs REU 16 MB, UA, UCI, turbo, SuperCPU Detect) | Final integration; UA/SID balance by ear, the same at every headroom setting |
| BT9 | Eight preset at 64x turbo, music retrigger A/B (edge stage) | As at 1x, within the edge-stage contract |
| BT10 | Paddles/POT check (side finding, after U10) | POT `$FF` with no device |

### 9.4 Gates

- **G-SID** (Plan §14), defined here: B1-B6 pass, with B2's legacy baseline identical before and after the fpga64 edit; the legacy E-1 benches unchanged (`tb_c64_sid_socket` with wrapper declarations added only); the host oracle and overlay tests pass; GET_HWINFO frames per §4.6; BT1-BT5 on the board, and BT3b.
- **G-STK**, as used here: `scripts/test_c64_ua_stock_bus.py` in compare mode at `SID_COUNT` 2 and at the image value (4 or 8), each reproducing the same golden trace. Source hashes go into the receipts; the manifest is never edited (§4.2).
- **G-AUD**, **G-TURBO**, **G-SET**, **G-DBG**, **G-IMG**, **G-SPEC** from Plan §13. For G-TURBO: T-S1c is only planned (TD §10.2), so B2's legacy baseline stands in for it until it exists.
- **G-FIT**: §6.4 (Plan §10.3 ceilings).

---

## 10. Open questions and user decisions

### 10.1 User decisions

| # | Decision | Recommendation | Reason |
|---|---|---|---|
| U1 | 8-SID engine | Run XM6. Ship E1 at 4 SIDs first (XM7a). For 8 SIDs use E1 if BC3/BC4 pass the ceilings; otherwise the E3 hybrid (pair 0 kept, voice-serial for SIDs 3-8) as XM7b. Reject E2 | E2 saves only ≈ 600-700 LUT over E1, changes the publish timing of every chip (32-state schedule with zero slack; chip 4 publishes in the next window) and edits the CERN-OHL-S filter. The register shape is the same for all engines, so the step from 4 to 8 needs no ABI change. §6.2 shows E1-8 is unlikely to meet the slice ceiling |
| U2 | Runtime 6581/8580 per SID | Yes, if the fit passes (candidates BC2/BC4) | TT8 and Heartbeat set 8580 (A8); MiSTer already supports a per-chip `mode` |
| U3 | Uncovered slots (mirroring off, layouts with gaps such as Triple, dropped parts) | Reads → the lowest mapped SID; writes inert (`WRITE_INHIBIT`) | Plan §3.4 wants a read owner for every slot. Inert writes match the U64, where no SID matches such an address [V u64_config.cc:1249-1255], and fix #733. Writes aliased onto SID 1 would garble it (a 4SID tune's `$D460` writes on Triple). What a detection tool reports at an uncovered slot depends on SID 1's state [I] |
| U4 | Emulate enabled SID sockets from a U64 `.cfg` on spare SIDs | Yes, listed as type 4/5 in GET_HWINFO; only when a spare SID exists | Otherwise a U64 Elite `.cfg` with real chips in its sockets plays wrong |
| U5 | Multi-SID settings per profile or global | Per profile (Plan P11) | The legacy SID mode is per profile today |
| U6 | Default headroom | Auto (0 / −6 / −12 dB by summed gain) | 8 summed 6581 SIDs clip at 0 dB; thresholds revised after O6 |
| U7 | DC removal default | Auto (on with 3+ SIDs, map mode only) | Keeps 1-2 SID output identical; frees headroom; the U64 removes the offset too (M7) |
| U8 | Resampling default | Point; Average as an option | Point keeps identity and today's sound |
| U9 | Triple and Quad preset addresses | `$D400/$D420/$D440` and `$D400/$D420/$D440/$D460` | Contiguous; Quad is U64-expressible (split 1/2 (A6)). Change if the user prefers `$D500/$D600` layouts |
| U10 | POT tie fix | Check VICE first; then a separate small change (`8'h00`) | Independent of multi-SID |
| U11 | `$DExx/$DFxx` SID slots | Later (XM8), per Plan B5 | Agrees with Plan P7 |
| U12 | GET_HWINFO `$01` content | U64 shape: SID 1, SID 2, then emulated sockets (at most 4 frames); `$0000`/type `$80` when unmapped | Keeps the U64 entry order and SIDDetector-II's 4-frame buffer safe; no garbage address |
| U13 | `[Multitini SID]` section name | As named | Keeps U64 sections untouched. Alternative: put the keys into `[Multitini]` |
| U14 | Full SID list over UCI | Multitini sub-command `$04 $28 $02` (§4.6), shipped with XM7 | Listing every SID in `$01` would send SIDDetector-II, the only known client, down its untested > 4-frame path and leave its UCI dirty. `$02` gives the same list without that risk. If a full list in `$01` is a firm requirement, confirm it knowing this risk |

### 10.2 Items for the Plan and the sibling designs

No Plan allocation is changed. These texts must agree with this design. The Plan and the sibling designs carry each of them (cross-checked on 2026-10-24):

1. **EXPANSION bit 6 clamp** (Plan §3.3): "a commit has been accepted since control reset; a rejected commit changes nothing", not "unless the last commit was accepted". Otherwise a rejected commit would drop a working map.
2. **C8 width** (Plan §11 C8): the active table is ≈ 670 bits, not ~400. It is streamed entry by entry over one async mailbox.
3. **Entry 0x38** (Plan §3.4): master is folded into the per-SID and UA gains by firmware (as on the U64); 0x38 holds the UA gains and output options.
4. **Fit experiment** (Plan §10.4, §12.1): candidates gain an OOC stage and runtime-8580 variants; E3 is a hybrid that keeps pair 0; XM7 is split into XM7a (4 SIDs) and XM7b (8 SIDs).
5. **Every slot has a read owner** (Plan §3.4 rules, §6.2). The zero-mask rejection stays. Writes to uncovered slots are inert and cannot hang; the `$D400-$D7FF` write column names no SID in a `WRITE_INHIBIT` slot. This replaces the first frame's "every slot has an owner ... a write to an 'unmapped' slot can never be lost" (Plan §19 item 21).
6. **Mailbox widths** (Plan §3.5, C2): this design adds EXPANSION bit 6 to `controls_i` and 9 bits to `status_i`. With the UCI the widths are 136 and 301 bits. The combined layout and `RESET_VALUE` (which also carries the UCI's `C64_UCI_CONTROL` reset value) are given once in Plan §3.5; this design's share of `RESET_VALUE` is `MT_C64_EXPANSION_CONFIG_DEFAULT[6:0]`.
7. **Stock-trace manifest** (Plan §6.5, §13 G-STK): the manifest records the one-time capture and is never edited. Each milestone runs compare mode and records the new source hashes in the checker receipt and the build receipt (§4.2).
8. **C8 return leg** (Plan §11 C18, §3.5): `{map_valid, active sequence}` crosses back in `status_i`, the forward mailbox never stalls, and the swap also happens while the core is paused or under HOLD (§3.5, §5.1).
9. **T-S1c** (Plan §13 G-TURBO): only planned (TD §10.2). Until it exists, B2's legacy baseline covers the fast select.
10. **GET_HWINFO** (Plan §12.1 XM5 and XM7a rows, P37; UCI §3.8.3): "GET_HWINFO `$02` lists every SID; `$01` keeps the U64 shape". UCI uses §4.6 for the frame content, and its XM5 scope includes the legacy part (§4.6 Delivery).
11. **DSP** (Plan §10.1): the mixer needs 2 more DSP for UA (§6.2); DSP stays far below 220.
12. **Timing row** (Plan §11 C17): the fast SID read decode on the T65 address nets, beside C3 (§5.1, §5.2).
13. **CFG**: the SID group is one registry group with the per-field record of §3.10, and K12 folds into it (no `SID_PARAMS` group). `$50` merges the SID group per field. K17 "SID audio" gains Triple, Quad, Eight at XM7, and K11/K17 lines apply in file order per field. `[Multitini SID]` is a known section from XM7 (CFG §3.6.1); `[SID Sockets Configuration]` is NOT_APPLICABLE until XM7 and after it unless U4 is adopted (CFG §3.6.3).
14. **UCI** (§3.8.4): a `$50` SID-map change is applied under `C64_UCI_CONTROL.HOLD`, with the REU and UA changes of the same file (§3.10); UCI test T-U4 gains a case.
15. **Budget baseline** (Plan §2.2, §10.3): dev7 numbers of 0.4; the XM0-or-waiver decision (P2) before XM6.
16. **Decision records** (Plan §3.6): Q24 is multi-SID; the deviations of §7 take D-numbers from the multi-SID range D60-D69.

### 10.3 Open items (measurement or source check)

| # | Item |
|---|---|
| O1 | OOC: `sid_top` with `MULTI_FILTERS = 0` → one RAMB18? |
| O2 | OOC: runtime `mode` cost per pair (8580 tables and per-voice paths) |
| O3 | Full-build fit and timing of each candidate (XM6) |
| O4 | U64 read priority with overlaps, unmapped read value, POT at split parts (A10) |
| O5 | U64 internal summing of 4 parts (M8): the loudness reference for Auto headroom |
| O6 | Peak levels of the board-test tunes through the array (B7) |
| O7 | VICE POT value with no device (§3.11) |
| O8 | MiSTer engine vs #702 (ADSR wrap) and #951 (8580 OSC3 delay) |
| O9 | Per XM6 candidate: A0-A9 load counts and net delays, worst slack of both half-period legs, and whether M-c, M-d or M-e is needed (§5.2) |
| O10 | Slack of the T65 → island BRAM address leg in dev7; it is not among the listed paths of the timing summary |
| O11 | `owner_lut` as FF bank and mux, or as double-banked LUTRAM: loads and slack (OOC4) |
| O12 | The LUT-per-slice density Vivado reaches for this image as it grows (§6.2) |

---

## 11. Work steps in order

Sizes: XS < 1 day, S 1-3 days, M 1-2 weeks, L 2-4 weeks, XL > 1 month.

| Step | Content | Size | Milestone | Gate |
|---|---|---|---|---|
| 1 | Import the pinned firmware reference (`third_party/ultimate_firmware_reference/`, §4.2); host model `c64_sid_map.c` with the U64 oracle test (§9.2). Firmware only | S | before XM6; may land with XM5 | host tests |
| 2 | `mt_c64_sid_hwinfo()` legacy part (sub `$01`, modes 0/2/3) and its host test, wired into the UCI control target | XS | XM5 | UCI host test, BT3b (Single, Dual $D500) |
| 3 | OOC measurements: OOC1 `sid_top` as built (calibrate against 2,629 LUT), OOC2 `MULTI_FILTERS = 0`, OOC3 runtime `mode`, OOC4 `sid_map` + decode at 4 and 8 (both `owner_lut` forms, with address-bit load counts), OOC5 mixer at 4 and 8, OOC6 E3 prototype if any | S | XM6 stage A | numbers recorded |
| 4 | B2's legacy baseline against today's fpga64 text, before any edit; then RTL for E1 at `SID_COUNT` 4 and 8 with M-a and M-b: control regs and streamer, `multitini_c64_sid_map`, fpga64 generate and decode, `sid_top` candidate output, mixer, top caps; benches B1-B6 | M | XM6 inputs | B1-B6 green; legacy benches unchanged |
| 5 | XM6 full diagnostic builds, PAL and NTSC, with the XM3-XM5 blocks, after the Plan's timing-baseline decision (0.4): BC1 E1-4, BC2 E1-4 + 8580, BC3 E1-8, BC4 E1-8 + 8580 (only if BC3 passes), BC5/BC6 E3 hybrid ± 8580 if prototyped. Record §6.4 and §5.2 items; apply M-c if a candidate misses the island gate | M (build time) | XM6 | G-FIT |
| 6 | Decide engine and 8580 (U1, U2) | XS | XM6 | user |
| 7 | Firmware: settings companion, presets, sub-page, change handlers with per-field ownership, SID overlay record and merge, registry rows and `.cfg` keys, `$50` under HOLD (with UCI), GET_HWINFO map part and `$02`, `:sidstat` | M | XM7a | host and runtime tests |
| 8 | Release build at the chosen count; G-STK (at `SID_COUNT` 2 and at the image value), G-AUD, G-TURBO, G-IMG; source hashes into the receipts (the stock-trace manifest is never edited) | S | XM7a | gates |
| 9 | Board tests BT1-BT7, BT9; BT3b on Eight | M | XM7a | G-SID, G-REU0 |
| 10 | If 8 SIDs did not fit with E1: E3 hybrid engine for SIDs 3-8 with the lockstep bench; rebuild; BT3-BT9 at 8 SIDs | XL | XM7b | lockstep bench, G-FIT, G-SID |
| 11 | Heartbeat integration BT8 (needs XM3 and XM5) | S | after XM5 and XM7 | board |
| 12 | Later: `$DExx/$DFxx` slots (caps bit 12, Plan B5) | S-M | XM8 | Plan B5 rules, G-REU0, G-STK |
| — | Separate: POT tie check and fix (U10) | XS | any | VICE check, one bench case |

---

## 12. Review

Every review issue was checked against the primary source before it was applied. The reviews changed:

- **DC removal:** integer state had a dead band below 32,768 LSB that passes the ≈ 17,000 LSB 6581 offset. Now Q18.16 fractional state, seeded at enable, with B4 bounds of ≤ 116 LSB at 5 time constants and ≤ 1 LSB at 10 (a "below 1 LSB within 5 time constants" bound was arithmetically wrong).
- **Saturation:** the legacy `sat24` takes 25 bits; the map sum now goes through `sat24_wide`, and the legacy lines stay literally.
- **Paused swaps:** `ce_1m` never fires while the menu pauses the C64. The swap now also happens on the next edge while paused, `ACCEPTED_SEQ` moves only after the swap, and the active sequence returns in `status_i` with `map_valid`.
- **Presets:** Triple needs per-SID overrides; presets now set listed overrides and `mt_c64_sid_preset_match()` compares them.
- **Sockets:** the socket default is the U64's `$D400`, because `auto_mirror` counts disabled sockets; the oracle test runs two socket passes.
- **Stock trace:** G-STK runs at `SID_COUNT` 2 and at the image value; new fpga64 inputs have map-off defaults; the stock-trace manifest is never edited.
- **Oracle location:** the U64 functions come from a pinned, byte-exact `third_party/ultimate_firmware_reference/`, never from a scratch copy.
- **Benches:** T-S1c does not exist; B2 runs a legacy baseline before any edit. The MAME script's copied span changes and keeps its anchors.
- **Headroom and master:** the headroom is a master-stage shift on SIDs and UA; Vol Master is folded into UA_GAIN as on the U64. Counting UA in S was rejected (it changes no preset and would cut 1-2 SID custom maps with UA).
- **Overlay rule and pan from preset:** stored per-SID overrides read as Follow under U64 keys of the same class; a lone group in a partial file plays Center.
- **Uncovered slots:** writes are inert (`WRITE_INHIBIT`, reject code 7), as on the U64, instead of aliasing onto SID 1.
- **4-SID images:** mirroring runs over all 8 parts before the missing parts are dropped, so a 1/4 split does not garble the TT8 layout.
- **GET_HWINFO:** `$01` keeps the U64 shape (SIDDetector-II's 4-frame buffer); the full list moved to `$02`. The legacy-mode part now ships in XM5.
- **Labels, text, DSP:** OOC and build-candidate labels renamed; menu strings ASCII; DSP counts include the UA multipliers; BT7 uses the "Next Level" demo instead of a SID player.
- **Budget:** rebased on the released dev7 image (`3df6d04`, `4D00C707`): +0.030 ns fast-island slack under a one-build waiver, 94.13 % slices, 36,689 LUT; slice density per candidate added; the XM0-or-waiver decision named as a precondition of XM6.
- **Fast read timing:** the decode shares the T65 address nets with the half-period BRAM path. The design now decodes only the owner and muxes candidates before the register case, plans address replication (TD R2 F1), and records loads and both half-period legs per candidate.
- **Overlay granularity:** the SID part of the title overlay is a per-field record with class bits; `$50` merges per field; sub-page edits check ownership per field and do not save owned fields; K12 folds into the SID group; K17 gains the multi-SID presets; SID lines resolve in file order per field.
- **Live `$50` changes:** SID-map changes from `$50` run under `C64_UCI_CONTROL.HOLD` together with REU and UA changes.
- **Frame alignment:** mailbox widths and `RESET_VALUE` defer to Plan §3.5; `[Multitini SID]` must be a known CFG section; decision record Q24.
