# Multitini One C64: U64 extensions plan (SuperCPU Detect, 8 SIDs, Command Interface, title `.cfg` files)

Status: Design only. Nothing here is implemented or tested.

- Date: 2026-10-24.
- Branch: `c64-turbo`. The feature designs were drafted at `c7c2fba` plus the dev7 work, which was uncommitted then. dev7 is now commit `3df6d04` (M0.7.0dev7, C64 image `4D00C707`, ABI 0.25). Repository anchors below refer to `3df6d04`, and the build figures were re-read from the dev7 release reports.
- This document is the overview plan and the shared frame for four features. It fixes the allocations, rules, milestones and gates they share. Each feature has its own detailed design (register behaviour, file-by-file work, benches, work steps), written alongside this plan: `docs/C64_SUPERCPU_DETECT.md`, `docs/C64_MULTI_SID.md`, `docs/C64_UCI.md` and `docs/C64_TITLE_CONFIG.md`. On a shared allocation or rule this plan is binding; a feature design that finds a plan rule wrong records it as an open frame item, and this plan is corrected before the milestone starts. Section 19 lists the conflicts between the earlier drafts that this plan resolves.
- **Citation base.**
  - Repository paths are relative to the repository root, at `3df6d04`. `firmware/platform/xilinx/main.c` is cited by function name, because its lines move.
  - `1541ultimate <path>:<lines>` = GideonZ/1541ultimate at `d43546fe` (GPL-3.0, except the FPGA SID sources, section 1.2).
  - `1541u-documentation <path>` = GideonZ/1541u-documentation at `2da134b31b54`.
  - `Appletini <path>:<lines>` = the sibling repository `appletini-one` at `73de6fe` (GPL-3.0). It is read-only for this work: code is copied or ported, never edited there.
  - `build/...` = local Vivado reports and release folders. `build/` is not under source control (`.gitignore:10`), so these figures cannot be checked from the repository.
  - Third-party C64 programs and libraries (xlar54 `ultimate_lib`, the oscar64 UCI library, UBoot64, DMBoot, SID Detector II, GDOS64, GNW64, Ozmoo) are cited by project and file. Nothing from them is copied.
  - TD = `docs/C64_TURBO_DESIGN.md`, UAD = `docs/C64_ULTIMATE_AUDIO_DESIGN.md`, ISS = `docs/C64_TURBO_U64_ISSUES.md`, SPEC = `docs/C64_TURBO_U64_SPEC.md`, FVIC = `docs/C64_TURBO_FAST_VIC.md`, PRIOR = `docs/C64_TURBO_PRIOR_ART.md`, EXP = `docs/C64_EXPANSIONS.md`.
- **Tags.** [V] verified in a primary source (path:line). [D] documented in a repository document. [R] user report or third-party file. [I] inference. [O] open: needs a measurement, a source check or a decision. **USER** marks a user decision; the text gives the recommended default.
- **Test results.** None. Nothing was built, simulated or run on the board for this plan.

Nothing in this document claims hardware support.

---

## 0. Summary

- Four features, each Disabled or absent by default: SuperCPU Detect (`$D0BC`), up to 8 SIDs, the Ultimate Command Interface (UCI), and same-name title `.cfg` files. A fifth piece, the board's W5100S Ethernet, is shared infrastructure for the UCI Network target.
- One ABI step, 0.26, defines every register of this plan. Images light features up through caps bits as milestones land.
- Order: XM0 timing recovery → XM1 stored/effective split → XM2 title `.cfg` files → XM3 spec 0.26 and SuperCPU Detect → XM4 network → XM5 UCI (XM5b RAM DMA and SoftIEC) → XM6 fit experiment → XM7 multi-SID → XM8 HTTP and later items.
- Budget: the released dev7 PAL image already misses the repository's fast-island timing floor (+0.030 ns against +0.100 ns, waived for that build only) and uses 94.13 % of the slices. Slices, not LUTs or BRAM, bind every new HDL feature. Multi-SID at 8 chips is unlikely to fit with replicated `sid_top` pairs (section 10).
- Every milestone keeps Expand by Bonzai disks 1 and 2 (G-REU0) and the immutable stock bus trace (G-STK) unchanged at default settings.

---

## 1. Purpose and scope

### 1.1 SuperCPU Detect (`$D0BC`)

A new Options row, "SuperCPU Detect (D0BC)", Disabled by default and global, as on the U64 [V 1541ultimate software/u64/u64_config.cc:174, 394]. When it is Enabled and Turbo Control is not Off, a CPU read of `$D0BC` returns `$01`. Otherwise the VIC answers `$FF`, exactly as today. Visibility depends only on configuration: every speed index including 1 MHz, the register modes right after reset, every forced-exact period, and both CPU read paths. This fixes U64 issue #654, where the register is visible only in Manual mode above 1 MHz [R]. A toggle is never an apply event, so it cannot move the live speed (the U64 strobes `SPEED_UPDATE` [V u64_config.cc:926]). Writes to `$D0BC` are ignored. There are no `$D0BD-$D0BF` or `$D1BC` mirrors, no SuperCPU RAM and no 65816. `$01` passes every detection idiom found, including GNW64's `cmp #$01`. GDOS64 takes its 65816 path on any value other than `$FF`, and the GEOS SuperCPU64 patch on any value with bit 7 clear; on our 6510 both then crash, so the help text and status line warn [V the programs' detection code; I for the crash]. This reverses TD Q11 for `$D0BC` only (new record Q21). `$D0B8`/`$D0B9` are a separate user decision (P28).

### 1.2 Up to 8 SIDs

Up to 8 SIDs that C64 software and U64 `.cfg` files address as on an Ultimate 64: the UltiSID 1/2 addresses, "UltiSID Range Split", "Auto Address Mirroring" and the socket addresses give the same SID addresses. Firmware computes a 32-slot decode table for `$D400-$D7FF`; the HDL only indexes it. The 1- and 2-SID layouts that exist today stay on today's legacy path and stay bit-identical. A slot that no SID covers reads a defined owner and ignores writes, so nothing can hang (U64 #733 [R]). A new mixer gives per-SID gain and pan, a 32-bit sum with saturation, Auto headroom and optional DC removal. Presets: Single, Dual `$D420`, Dual `$D500` (today's three), Triple, Quad and Eight ("Tuneful Eight"). A SID sub-page edits addresses, pan, volume and model. Four SIDs come first; eight only if the fit experiment passes. **Licence:** all 16 files in 1541ultimate `fpga/sid6581/vhdl_source/` say they may not be used in other projects without written permission from the author [V, e.g. `sid_mapper.vhd`]. No UltiSID engine, mapper or mixer is ported. The design is built on the MiSTer `sid_top` already in the core. The U64 firmware's address functions (`software/u64/u64_config.cc`, GPL-3.0) serve only as a pinned, test-only oracle.

### 1.3 Ultimate Command Interface

The U64 Command Interface at `$DF1C-$DF1F`, in the full U64 slot `$DF18-$DF1F`, Disabled by default [V 1541ultimate fpga/io/command_interface/vhdl_source/command_protocol.vhd:97-108]. A PL block on `c64_clk` holds the protocol state and a 2 KB dual-clock buffer. CPU0 firmware serves the targets in its polled loop. Targets: Ultimate DOS 1 and 2 on the SD media folder; Control (identify, REU load and save, drive and hardware info, FREEZE, REBOOT, LOAD_CONFIG); Network on the W5100S; Software IEC on drive 9 (XM5b); HTTP (XM8). Register semantics are bit-compatible with the U64. Fixes for U64 faults that users reported are kept wherever no known client depends on the fault: a stuck handler wedging the server (#740), sockets surviving a C64 reset (#808), pops by DMA or debug reads (#983), a refused PUSH latching the freeze bit, a dead TCP socket answering "no data" forever. There is no `$D038/$D036` unlock: Disabled means off. With the option Off the C64 bus is bit-identical to today; with it On only `$DF1B-$DF1F` change.

### 1.4 Same-name title `.cfg` files

When the user launches a title (Program browser, Cartridge browser, Mount & Run, UART `:load`) and a file with the same stem and the extension `.cfg` lies beside it (for a PRG, then `.usr`), its settings apply for that title only. They are never saved to `settings.bin`; the next title without a file runs with the saved settings. Files written by a U64 or C64U for firmware 3.15 "Automatic per-title configuration" work unchanged [D 1541u-documentation howto/release_3.15.md]. Our options without a U64 equivalent go in a `[Multitini]` section, which a U64 skips. The same parser serves UCI Control `$50` LOAD_CONFIG. Rows: "Title .cfg files" (default On), "Revert title settings", and "Save settings for this title" (XM2d). Firmware only; no ABI change. It needs a stored/effective split first (XM1), because today a temporary value would leak into `settings.bin` at the next menu change [V main.c `change_c64_expansion()` reads the live `C64_EXPANSION_CONFIG`]. This reverses TD Q10 (new record Q22).

### 1.5 Network note

- UCI DOS (`$01`, `$02`), Control (`$04`) and Software IEC (`$05`) need no network.
- Network (`$03`) and HTTP (`$06`) need one. The board has a WIZnet W5100S 10/100 Ethernet controller with hardware TCP/IP on the PL-facing 8-bit parallel MCU bus, Bank 35, LVCMOS33 [V Appletini scripts/create_project.tcl:19, 260; Appletini hdl/constraints/appletini_yarz.xdc:367-398]. Appletini drives it as an Uthernet II [V Appletini hdl/apple/uthernet2_card.sv:1-54; R user]. Multitini declares the pins [V hdl/board/multitini_one_top.sv:88-94] and holds the chip in reset in every image [V top.sv:2329-2334].
- The PS GEMs are disabled in both projects; the W5100S is the only Ethernet [V Appletini scripts/create_project.tcl:260].
- W5100S limits: 4 hardware sockets, 16 KB socket buffer memory, TCP, UDP, IPRAW and MACRAW, no DNS client, no TLS. UCI clients may ask for up to 16 sockets [V 1541ultimate software/io/network/network_target.h:31].
- XM4 brings the chip up as a shell service owned by the PS. The C64 never reaches the W5100S directly.
- Appletini's FTP server for the SD card [V Appletini ps_sources/frontend/ftp_sd_service.c] is not requested. It stays a possible later option, default Off.

### 1.6 Out of scope

- A SID file player. Register-driven (dynamic) SID pan. Any change to the SID engine's sound.
- `$D0B0-$D0BB` (except P28), `$D07E/$D07F`, SuperCPU RAM, a 65816.
- The U64 cartridge-requested UCI slots `$DE18` and `$DFF8`, and UCI clients that need a cartridge other than a generic 8K/16K/Ultimax one.
- Loading ROMs, cartridges or REU images named in a `.cfg`; network, streaming and UI keys from a `.cfg`; automatic PAL/NTSC image switching.
- TLS. Any listening network service in the first releases (the UCI TCP listener is P48).
- Any change to stock REU behaviour.

---

## 2. Starting state at `3df6d04`

### 2.1 Facts

1. **dev7 is committed and released.** HEAD `3df6d04`, clean tree [V git log]. Spec ABI minor 25 [V spec/interface_v0.json:5]. Release `4D00C707` "PAL + NTSC, ABI 0.25" [V build/C64-4D00C707-070dev7-SD/README.txt]. So no feature of this plan can join 0.25.
2. **Drive 9.** Registers `C64_IEC_CAPS` 0x114 to `C64_IEC_TX` 0x124 [V hdl/generated/multitini_regs_pkg.sv:68-72]; `C64_IEC_MIN_ABI` = 0x19 [V :350]. A PS-backed IEC device with a DOS layer, channel API and path-protection callback [V firmware/include/multitini_c64_iec_dos.h:17-31, 65-80], polled from `service_runtime()` with a budget of 32 events, quiesced around resets, content changes and mounts [V main.c `service_runtime()`, `set_core_reset_request()`, `prepare_c64_content_change()`, `mount_c64_disk()`].
3. **C64 media browsers use folders** under the package `content_root` [V firmware/common/core_frontend.c:476-479; main.c `supervisor_activate()`]. A same-name `.cfg` lives in the title's own folder.
4. **W5100S** (section 1.5). `eth_int_n` is false-pathed as a static input [V hdl/board/multitini_timing.xdc:93-96]. The timing coverage gate fails a build when a driven output port has neither an output delay nor a false path [V scripts/build_and_export_xsa.tcl:615-631].
5. **Mailboxes.** `controls_i` is a 120-bit word mailbox and `status_i` 228 bits [V top.sv:1244-1257]. dev7 already carries its 18-bit IEC control word and 36-bit status word in them.
6. **Free register space** [V regs_pkg offsets]: 0x02C-0x03C, 0x06C-0x07C, 0x0E8/0x0EC (reserved for turbo growth [D TD:1736]), 0x0FC (reserved for the UA G9 probe [D TD:1740]), 0x128-0x13C, 0x154-0x17C, 0x1C8/0x1CC, 0x1DC and up. Register offsets are 12 bits [V regs_pkg:68]. GP0 offsets 0xC000-0xFFFF are unused.
7. **Menu.** `MT_MENU_MAX_CORE_OPTIONS` = 12, `MT_MENU_MAX_ITEMS` = 20, `MT_MENU_MAX_SYSTEM_ITEMS` = 21 [V firmware/include/multitini_menu.h:18-20]. `build_c64()` appends without a bounds check [V firmware/common/core_frontend.c]. Release images show 9 C64 rows. The status field is drawn `MT_UI_COLUMNS` − 6 = 122 columns wide [V firmware/common/menu.c:1543; firmware/include/multitini_regs.h:237], but the status text buffer holds 63 characters and the setters truncate [V main.c:190 `supervisor_status_text[64]`, :1981-1984].
8. **Settings.** `settings.bin` schema 10; byte 26 is written as `(turbo_flags & 3) | (UA ? 4 : 0)` and decoded with masks 3 and 4 [V firmware/platform/xilinx/supervisor_settings.c:588-589, 710-711]. The test asserts `stored_file[26] == (detail & 7)` and sets future bits with `0xF8` [V tests/host/test_supervisor_settings.c:994-996].
9. **Stock trace.** `tests/c64_ua/stock_bus.csv` reads `$FF` at `$DF18-$DF1F` with the REU on [V :26-33] and contains no `$D0BC` access [V grep]. The checker writes the manifest only under `--capture`; compare mode checks only the trace [V scripts/test_c64_ua_stock_bus.py:128-137]. The manifest records the capture at `a0bf372` [V tests/c64_ua/stock_bus_manifest.json:3].

### 2.2 Budget and timing baseline (dev7 release `4D00C707`)

| Measure | PAL | NTSC | Source |
|---|---|---|---|
| LUT (routed) | 36,689 of 53,200 (69.0 %) | 36,769 (69.1 %) | build/Multitini-070dev7/evidence/c64-{pal,ntsc}/reports/utilization.rpt:25 |
| FF | 30,334 (28.5 %) | 30,150 (28.3 %) | same |
| Slices (placed) | 12,519 of 13,300 (94.13 %) | 12,362 (92.95 %) | build/c707p3 and build/c707n2 `.../impl_1/multitini_one_top_utilization_placed.rpt:76` |
| Slices left below 95 % (12,635) | 116 | 273 | computed |
| LUT per used slice | 2.93 | 2.97 | placed LUTs (:35) / slices |
| BRAM tiles | 128 of 140 (119 RAMB36 + 18 RAMB18) | 128 | placed.rpt:106 |
| DSP | 33 of 220 | 33 | placed.rpt:121 |
| Design WNS / WHS | +0.030 / +0.029 ns | +0.219 / +0.014 ns | evidence/.../timing_summary.rpt:166 |
| Fast island (`clk_out2` c64 CPU clock) | +0.030 ns | +0.219 ns | timing_summary.rpt:196-210 (intra-clock table) |
| `c64_clk` | +3.483 ns | +1.914 ns | same |
| Video (148.5 MHz) | +0.086 ns | +0.219 ns | same |
| `control_clk` (`clk_fpga_0`, 133 MHz) | +0.288 ns | +0.496 ns | same |
| Worst PAL path | `machine_adapter/main_ram_i/character_reg` (negedge RAMB36) → `cpu_i/cpu/BAL_reg[8]`, 7.929 ns half period, 8 logic levels | | timing_summary.rpt:65745-65752 |
| 2-SID `sid_top` | 2,629 LUT, 1,963 FF, 2 RAMB36, 7 DSP | | build/Multitini-070dev7/evidence/c64-pal/reports/utilization.rpt:82 (dev6: 2,635 LUT); re-measure in XM6 |

Facts from the release: "PAL setup margin +0.030 ns is accepted for this build only; the repository default remains +0.100 ns" [V build/C64-4D00C707-070dev7-SD/README.txt]. The floor is enforced by `check_c64_turbo_timing ... 0.100` [V scripts/build_and_export_xsa.tcl:207].

Consequences [I]:
- Before any new RTL, every HDL milestone of this plan fails the repository's fast-island floor on PAL. Either XM0 recovers the margin first, or each build needs a waiver (decision P2).
- The fast island's critical loop is T65 address → negedge island BRAM → T65 [V hdl/core/c64/multitini_c64_cpu_island.sv:163-165; hdl/core/c64/multitini_c64_island_memory.sv:60-75]. SuperCPU Detect adds loads on A0-A9 and multi-SID adds loads on A5-A9 of the same nets (section 11, C3 and C17).
- 116 slices of headroom on PAL is about 340 LUT at today's packing. The UCI alone is estimated at 300-1,200 LUT.
- `control_clk` (+0.288 ns PAL) gains the W5100S bridge, the network registers and the UCI buffer port. The video clock (+0.086 ns PAL) is already tight; a miss there uses the existing repair flow, never the island ladder [D TD:2193 R27].

---

## 3. ABI and registers

### 3.1 One ABI step: 0.26

- Facts: every past register addition took its own minor; gating is package ABI → live ABI → caps; a caps bit reads 0 on an image without the feature; firmware never accesses a free or reserved offset [D TD:1701, 1717-1726].
- Rule: **one spec edit to ABI 0.26**, made once, at the start of XM3, after this plan is reviewed. It defines every register and bit in this section. Each feature has its own `*_MIN_ABI_MINOR` = 26 and its own caps bits. An image implements a subset; missing features read caps 0. Milestones XM3-XM8 then ship without further ABI steps.
- `scripts/make_core_package.py` `DEFAULT_ABI_MINOR` 25 → 26 in the same edit [V :19]; `tests/test_core_package_tool.py` asserts it equals the spec minor.
- If XM6 changes the SID register shape, the SID block moves to 0.27 and its 0.26 offsets stay reserved for ever. Never reuse an offset.
- Only C64 images rebuild. Other images keep their ABI and read `NET_CAPS` = 0 (firmware checks their ABI first).
- Alternatives (**USER**, P1): one minor per milestone (0.26 SuperCPU, 0.27 network and UCI, 0.28 SIDs) costs three spec edits and three package-ABI bumps. Folding into 0.25 is no longer possible: `4D00C707` shipped at 0.25.

### 3.2 Offset map after 0.26

| Offset | Name | Access | Feature | Notes |
|---|---|---|---|---|
| 0x02C | `NET_CAPS` | ro | network | 0 on images without the bridge |
| 0x030 | `NET_CONTROL` | rw | network | default 0 |
| 0x034 | `NET_ACCESS` | wo | network | one W5100S register access per write |
| 0x038 | `NET_STATUS` | ro | network | |
| 0x03C | reserved | — | network | for a later burst register; not decoded |
| 0x0D0 | `C64_EXPANSION_CONFIG` | rw | existing; bits 5-6 new | 3.3 |
| 0x0D4 | `C64_EXPANSION_CAPS` | ro | existing; bits 5-6, 8-14 new | 3.3 |
| 0x0D8 | `C64_CPU_CONFIG` | rw | existing; bit 20 new | 3.3 |
| 0x0E4 | `C64_CPU_CAPS2` | ro | existing; bit 15 new | 3.3 |
| 0x0E8, 0x0EC, 0x0FC | reserved | — | turbo, UA | unchanged |
| 0x114-0x124 | `C64_IEC_*` | | drive 9 | taken at 0.25 |
| 0x128 | `C64_UCI_STATUS` | ro | UCI | |
| 0x12C | `C64_UCI_HANDSHAKE` | wo | UCI | |
| 0x130 | `C64_UCI_CONTROL` | rw | UCI | reset 0x0000001F |
| 0x134 | `C64_UCI_STATUS2` | ro | UCI | read as a pair with STATUS (shadow) |
| 0x138 | `C64_UCI_RAM_READ` | wo | UCI (XM5b) | caps bit 13 |
| 0x13C | reserved | — | — | not decoded |
| 0x154 | `C64_SID_INDEX` | rw | multi-SID | |
| 0x158 | `C64_SID_DATA` | wo | multi-SID | |
| 0x15C | `C64_SID_COMMIT` | wo | multi-SID | |
| 0x160 | `C64_SID_STATUS` | ro | multi-SID | |
| 0x164-0x17C | free | — | — | |
| GP0 0xC000-0xC7FF | `C64_UCI_BUFFER` window | rw, 32-bit single beats | UCI | decoded in the top beside the framebuffer and UI VRAM selects, outside the control span below 0x1000; reads above 0x1000 return 0 today [V top.sv:219-220, 829-843]; `C64_UCI_BUFFER_OFFSET` = 0xC000, `C64_UCI_BUFFER_BYTES` = 0x800 |

Network is a shell service, so it takes the shell area. The UCI is C64 I/O, so it follows drive 9. The SID table copies the indexed write-and-commit pattern of the video effects table at 0x140-0x150. The UCI buffer is bulk memory, like UI VRAM.

### 3.3 New bits in existing registers

**`C64_CPU_CONFIG` @0x0D8.** Bit 20 `SCPU_DETECT`: 1 = `$D0BC` reads `$01` while the effective Turbo Control mode is not 0. Reset 0; reset value 0x151 unchanged; mask 0x000FFFFF → 0x001FFFFF. Clamped to 0 unless `C64_CPU_CAPS2` bit 15. Not in `C64_CPU_CONFIG_DEBUG_MASK`. Not an apply event: the counter follows bits 6:0 only [V firmware/common/c64_features.c:108-110]. It rides inside `cpu_config` in `controls_i`, so no new crossing.

**`C64_CPU_CAPS2` @0x0E4.** Bit 15 `SCPU_DETECT`. `C64_CPU_CAPS2_RELEASE` 0x80002F01 → 0x8000AF01 [V regs_pkg:334].

**`C64_EXPANSION_CONFIG` @0x0D0.** Mask 0x1F → 0x7F; default stays 0x0B [V regs_pkg:286].
- Bit 5 `UCI_ENABLE`. Clamped to 0 unless caps bit 5.
- Bit 6 `SID_MAP_ENABLE`: 1 = SID decode and mix from the committed table, 0 = legacy `sid_mode`. Clamped to 0 unless caps bit 6 and a commit has been accepted since control reset. A rejected commit changes nothing: the active table and bit 6 stay.
- Bits [6:0] cross in one word, so REU, UA, UCI and SID ownership change on one `c64_clk` edge.

**`C64_EXPANSION_CAPS` @0x0D4** (today 0x8000001F in the C64 image [V top.sv:749]).

| Bit | Name | Feature |
|---|---|---|
| 5 | `UCI` | UCI (XM5) |
| 6 | `MULTI_SID` | multi-SID (XM7) |
| 7 | reserved | — |
| [10:8] | `SID_COUNT_M1` (valid with bit 6; 3 = 4 SIDs, 7 = 8 SIDs) | multi-SID |
| 11 | `SID_MODEL_8580` (runtime model per SID) | multi-SID |
| 12 | `SID_IO_SLOTS` (`$DE00-$DFE0` slots accepted; 0 until XM8) | multi-SID |
| 13 | `UCI_RAM_DMA` (no-reset RAM write, FREEZE hold, RAM read) | UCI (XM5b) |
| 14 | `UCI_C64_IRQ` (0 until XM8) | UCI |
| [30:15] | reserved | — |

### 3.4 New registers

**`NET_CAPS` @0x02C (ro).** [31] valid; [0] W5100S bridge present; [7:4] bridge revision; rest 0.

**`NET_CONTROL` @0x030 (rw, default 0).** [0] `RUN`: 0 holds `eth_rst_n` low; 0 → 1 starts the 10 ms reset hold and the 61 ms ready wait. [31:1] reserved.

**`NET_ACCESS` @0x034 (wo).** [14:0] W5100S address; [15] reserved, must be 0; [23:16] write data; [24] `WRITE`; [31:25] reserved. A write starts one W5100S register access. The bridge reloads the W5100S indirect address only when its tracked pointer differs, so sequential bytes cost one bus cycle [V Appletini uthernet2_card.sv:1-11]. A write while BUSY, while not READY, or with a reserved bit set is a bad write and sets ERROR.

**`NET_STATUS` @0x038 (ro).** [0] READY (reset done, idle); [1] BUSY; [2] DONE; [3] ERROR; [4] RESET_DONE; [5] INT (synchronized INT# asserted); [15:8] read data; [23:16] sequence, +1 per completed access; [31] valid.

**`C64_UCI_STATUS` @0x128 (ro, from `c64_clk` through `status_i`).**

| Bits | Field |
|---|---|
| [1:0] | C64-side state: 00 Idle, 01 Busy, 10 Data Last, 11 Data More |
| 2 | ABORT pending (never 1 in Idle) |
| 3 | DATA_ACC pending |
| 4 | ERROR |
| 5, 6, 7 | FREEZE, TRIGGER, C64 IRQ-enable latched at the accepted PUSH |
| [17:8] | while CMD_BUSY: the length latched at the PUSH; otherwise the current fill count (0-896) |
| [21:18] | push sequence, +1 per accepted PUSH |
| [25:22] | C64 reset epoch, +1 per block reset |
| 26 | suspended (cartridge not generic, or loading) |
| 27 | effectively enabled |
| 28 | `HANDSHAKE_BUSY`: the op mailbox has not taken the last op (from the `control_clk` side) |
| [30:29] | reserved |
| 31 | valid |

**`C64_UCI_HANDSHAKE` @0x12C (wo, one-entry async mailbox, 32 bits).** [3:0] op: 1 ACCEPT, 2 NEXT, 3 ABORT_DONE, 4 VALIDATE_LAST, 5 VALIDATE_MORE, 15 RESET; others are bad writes. [14:4] response length 0-896. [15] reserved (bad write if set). [24:16] status length 0-256. [28:25] push sequence the op answers. [31:29] epoch[2:0] the op was built for. The block drops any op whose sequence or epoch tag differs from its own. One write carries the op and both lengths, so a validate is atomic.

**`C64_UCI_CONTROL` @0x130 (rw, reset 0x0000001F).** [4:0] value read at `$DF1B` (31 = no SoftIEC device). [8] `FREEZE_ALLOWED` (forced 0 unless caps bit 13). [9] `C64_IRQ_ALLOWED` (forced 0 unless caps bit 14). [10] `HOLD`: whole-machine HDL pause through the existing pause path, with no menu; used around the REU invalidate and for live `$50` expansion changes (forced 0 unless caps bit 5; XM5). [13:12] slot select; only 0 (`$DF18-$DF1F`) is defined, others are bad writes. Rest reserved.

**`C64_UCI_STATUS2` @0x134 (ro).** A STATUS read latches STATUS2 into a shadow in `multitini_control_regs.sv` on the same edge; a STATUS2 read returns the shadow. So the pair always comes from one snapshot, although `status_i` resends whenever any of its bits changes [V hdl/common/multitini_word_mailbox.sv:18].

| Bits | Field |
|---|---|
| [9:0] | response bytes the C64 read from the current block |
| [18:10] | status bytes read |
| [20:19] | `op_ack`, +1 per op applied or dropped |
| 21 | the last op was dropped |
| 22 | `$DF1D` bytes written while CMD_BUSY (not stored) since the last PUSH |
| 23 | command overflow since the last fill-count reset |
| [27:24] | XM5b RAM-read done sequence |
| [30:28] | idle-abort count, +1 per abort completed in the block |
| 31 | valid |

**`C64_UCI_RAM_READ` @0x138 (wo, XM5b, caps bit 13).** [15:0] C64 address; [26:16] length − 1 (at most 1,152 bytes into the reply area); [27] start. Gated by the UCI hold, not by the debug session.

**`C64_UCI_BUFFER` @GP0 0xC000-0xC7FF.** Byte `b` at 0xC000 + `b`. Command 0x000-0x37F (written by the C64), response 0x380-0x6FF and status 0x700-0x7FF (written by the PS), the U64 split [V 1541ultimate fpga/io/command_interface/vhdl_source/command_if_pkg.vhd:33-41]. The PS uses volatile 32-bit single accesses with explicit little-endian packing, never `memcpy`: the register slave answers SLVERR to bursts and sub-word transfers [V hdl/common/multitini_axi3_regs.sv:4-7].

**`C64_SID_INDEX` @0x154 (rw, [5:0]). `C64_SID_DATA` @0x158 (wo): writes the shadow entry at INDEX, then INDEX + 1 (wraps). `C64_SID_COMMIT` @0x15C (wo, any value).**

**`C64_SID_STATUS` @0x160 (ro).** [7:0] `ACCEPTED_SEQ`, which moves only after the `c64_clk` side reports the table swap; [8] BUSY; [9] REJECTED; [14:10] reject code (1 entry missing, 2 zero slot mask, 3 SID ≥ count named, 4 IO slot without caps 12, 5 reserved bit, model or digifix without caps 11, or `HEADROOM_SHIFT` = 3, 6 commit while busy, 7 `WRITE_INHIBIT` with a mask that does not name exactly one SID); [21:16] first failing index; [31] valid.

SID table entries (32-bit; reserved bits must be 0; COMMIT needs every entry 0x00-0x38 written since the last accepted commit):

| Index | Entry | Layout |
|---|---|---|
| 0x00-0x1F | `SID_SLOT_MASK[s]`, slot `$D400 + $20·s` | [7:0] mask, bit n = SID n; never 0. [8] `WRITE_INHIBIT`: the mask names exactly one SID, the read owner; no SID takes a native, fast or REU DMA write in this slot |
| 0x20-0x2F | `SID_IO_SLOT_MASK[s]`, slot `$DE00 + $20·s` | [7:0] mask; 0 unless caps bit 12 |
| 0x30-0x37 | `SID_PARAM[n]` | [7:0] `GAIN_L`, [15:8] `GAIN_R` (0x80 = unity); [16] `MODEL_8580`; [17] `DIGIFIX` (16-17 need caps 11); all 0 for n ≥ count |
| 0x38 | `SID_OUTPUT` | [7:0] `UA_GAIN_L`, [15:8] `UA_GAIN_R` (0x80 = today); [16] `RESAMPLE_AVERAGE`; [17] `DC_REMOVE`; [19:18] `HEADROOM_SHIFT` 0-2 |
| 0x39-0x3F | reserved | writes must be 0 |

Rules: every slot has a read owner (the lowest set bit); writes to uncovered slots are inert and cannot hang. Master volume has no entry: firmware folds it into every SID and UA gain, as the U64 does for all its mixer sources [V u64_config.cc:1342-1358]. The legacy modes are tables of this form (mode 3: A8 = 0 → SID 1, A8 = 1 → SID 2; mode 2 by A5; mode 0: both chips written, SID 1 read) [V hdl/core/c64/ports/fpga64_sid_iec.vhd:987-991], but firmware runs every layout that a legacy mode can express on the legacy path (bit 6 = 0), so 1-2 SID output is bit-identical by construction.

### 3.5 Mailbox layouts (final, given once)

`controls_i` (`control_clk` → `c64_clk`), 120 → **136** bits, landed whole in XM5. XM3 changes no width: bit 20 rides inside `cpu_config`.

```
value    = { c64_uci_control[13:0],          // 14, new (XM5)
             iec9_control_value[17:0],        // 18, dev7
             c64_cpu_config[31:0],            // 32
             c64_expansion_config[6:0],       //  7, was [4:0]
             debug_control[0],                //  1
             c64_ram_peek_request[31:0],      // 32
             c64_reu_host_request[31:0] }     // 32
RESET_VALUE = { 14'h001F, 18'd0, MT_C64_CPU_CONFIG_DEFAULT,
                MT_C64_EXPANSION_CONFIG_DEFAULT[6:0], 65'd0 }
```

`status_i` (`c64_clk` → `control_clk`), 228 → 292 bits in XM5 → **301** bits in XM7. New fields go at the top so existing slices keep their positions.

```
value = { sid_map_valid, sid_active_seq[7:0],   //  9, new (XM7)
          uci_status2[31:0], uci_status[31:0],  // 64, new (XM5)
          iec9_status_value[35:0], cpu_status, ua_status, ua_fetches,
          reu_misses, peek_status, reu_host_status }  // 228, today
```

A SID sequence match counts only with `sid_map_valid` = 1, so the zero left by a `c64_clk` reset never matches. Optional sticky SID clip bits are not allocated; if added later they count only while `debug_enable` is set.

### 3.6 Other numbers

- Load kind 0x17 = "C64 RAM write, no reset" (UCI DMA loads, SoftIEC LOAD_EX; XM5b). Kinds 0x10-0x16 are taken [V firmware/include/multitini_c64_interface.h:19-25].
- `*_MIN_ABI_MINOR` = 26: `C64_SCPU_DETECT`, `C64_UCI`, `C64_MULTI_SID`, `NET`. This is the spec's naming convention; dev7's `C64_IEC_MIN_ABI` stays as it is.
- Decision records in TD §0.6: **Q21** SuperCPU Detect (reverses Q11 for `$D0BC`), **Q22** title `.cfg` files (reverses Q10), **Q23** UCI, **Q24** multi-SID, **Q25** network on the W5100S.
- Deviation ranges (D10 stays unused [D TD:460]): D20-D29 SuperCPU Detect (D20: visible in every mode and through forced exact, the #654 fix; D21: not an apply event); D30-D39 title `.cfg`; D40-D59 UCI; D60-D69 multi-SID; D70-D79 network. Design decisions DL7 and up, numbered in the order they are recorded.

### 3.7 Register rules every milestone keeps

1. A clamp in `hdl/common/multitini_control_regs.sv` forces every new field to 0 without its caps bit.
2. Every new default keeps today's behaviour (section 6.5).
3. Pop-on-read and other side effects fire only on accepted CPU transactions (`cpu_bus_valid`), never on REU DMA, VIC DMA, debug peeks or firmware readback.
4. `tests/test_interface_spec.py` and `make check-generated` gain every new constant.

---

## 4. Settings storage

### 4.1 Allocation

| State | Location | Scope | Default | Milestone |
|---|---|---|---|---|
| Title `.cfg` files | `settings.bin` byte 26 bit 5 (1 = **Off**) | global | 0 = On | XM2 |
| SuperCPU Detect | byte 26 bit 3 (1 = Enabled), held as `c64_turbo_flags` bit 3 | global | 0 | XM3 |
| Command Interface | byte 26 bit 4 (1 = Enabled) | global | 0 | XM5 |
| UA level row | byte 26 bits 6-7 | — | reserved [D TD:1917] | — |
| Legacy SID mode, REU | per-profile expansion byte (frozen format) | profile | mode 3, REU on | unchanged |
| Multi-SID model | `0:/multitini/c64_sid.bin` + `.mtj` | profile | absent = legacy | XM7 |
| Network | `0:/multitini/network.ini` | board | absent = Off | XM4 |
| Title overlay, title session | RAM | title session | none | XM2 |
| UCI state, sockets, DNS cache, runtime SET_IPADDR | RAM | session | — | XM4, XM5 |
| `$DF1B` id | derived: 9 when drive 9 is enabled **and** the SoftIEC target exists (XM5b), else 31 | — | 31 | — |

No schema change: schema 11 would make every older firmware discard all settings [D TD:1948].

### 4.2 Companion files

- **`c64_sid.bin`.** Header 32 bytes: magic `MTSIDMP1`, version 1, record size 128, record count ≤ 16, CRC32 of the body and its complement, reserved 0. One record per profile, keyed by the 24-byte `core_id`, journaled like `turbo_controls.bin` [V supervisor_settings.c:900-937]. Each record stores the legacy SID mode it was saved with; if the expansion byte now holds another mode (changed under older firmware), the map is stale and stays off. While a map is active, firmware writes the nearest legacy mode into the expansion byte, so older firmware plays a sensible 2-SID layout. Any out-of-range or non-zero reserved byte invalidates the file: legacy behaviour, a status warning, no write to `settings.bin`.
- **`network.ini`.** Section 9.8.

### 4.3 Byte 26 tests per milestone

| Milestone | Assigned bits | Assertion on `stored_file[26]` | Future-bit mask in the downgrade test |
|---|---|---|---|
| today | 0-2 | bits 0-2 | 0xF8 |
| XM2 | 0-2, 5 | bits 0-2 and 5 | 0xD8 |
| XM3 | 0-3, 5 | bits 0-3 and 5 | 0xD0 |
| XM5 | 0-5 | bits 0-5 | 0xC0 |

Each step adds a round trip of the new bit through the frozen schema-10 fixture: older firmware ignores it and writes 0 on its next save, which is the default. Downgrade therefore gives SuperCPU Detect Off, Command Interface Off and title files On.

---

## 5. Menu

### 5.1 Options arguments

Facts: arguments 0-10 are taken, 11 is reserved for the UA level row, 12 and 13 are the controller actions CPU faster and slower [D TD:1913; V main.c `change_c64_turbo()` cases 12/13]. XM1 raises `MT_MENU_MAX_CORE_OPTIONS` to 20 and adds one bounds-checked append helper with a host test that builds the largest row set.

| Arg | Row | Position | Shown when | Values (each gains ` (title)` while the overlay owns it) | Milestone |
|---|---|---|---|---|---|
| 14 | SuperCPU Detect (D0BC) | turbo group, after Badline timing | ABI ≥ 26 and CAPS2 bit 15 | `Disabled`, `Enabled`, `Enabled (inactive: Turbo Off)`, `Enabled (not applied)` | XM3 |
| 15 | Command Interface | after RAM expansion, before Ultimate Audio | ABI ≥ 26 and EXPANSION_CAPS bit 5 | `Off`, `On`, `On (suspended: cartridge)` | XM5 |
| 16 | SID setup... | after SID audio; opens the SID sub-page | EXPANSION_CAPS bit 6 | summary | XM7 |
| 17 | Title .cfg files | last group | C64 core, always | `Off`, `On`, `On: NAME.cfg`, `On: NAME.cfg, n skipped` | XM2 |
| 18 | Revert title settings | after 17 | an overlay is active | source file name | XM2 |
| 19 | Save settings for this title | after 18 | a title session is active | `NAME.cfg` or `Create NAME.cfg` | XM2d |
| 20-31 | reserved | | | | |

- All new rows are Enter-only and apply while the menu has the C64 paused; none needs a restart [V main.c `menu_action_needs_core_progress()`].
- Each new argument gets its own branch in `handle_menu_action()` before the `>= 2U` expansion catch-all.
- Row counts: release today 9; after XM2 10-12; after XM5 12-14; after XM7 13-15; diagnostic image +2 (17); with the UA level row 18. All ≤ 20.
- "SID audio" (argument 2) stays the preset row: Single, Dual `$D420`, Dual `$D500`, then Triple, Quad, Eight where the SID count allows, and `Custom (n SIDs)` when a custom map is stored. Choosing a legacy preset turns the map off.
- Help text is ASCII; the UI draws bytes outside 32-127 as `?` [V firmware/common/ui.c:9-11].

### 5.2 SID sub-page (XM7)

A generic "core sub-page" in `menu.c`, entered from argument 16, left with Escape, Left/Right adjust like the Effects page. Its items have their own index space. Items: Layout, SIDs (read-only summary), Address mirroring, Select SID, Address, Volume, Pan, Model (with caps bit 11), Master volume, Headroom, DC removal, Resampling, Reset SID setup: 13 items. An item whose field the overlay owns edits the overlay and saves nothing (section 8.3); other items commit live and save.

### 5.3 System page (XM4)

Two rows, as menu actions, not Options arguments: "Network" (Off / DHCP / Static) and "Network status" (Enter renews DHCP or re-applies the static address). `MT_MENU_MAX_SYSTEM_ITEMS` 21 → 23, and `system_frontend.c` is re-indexed. On an image without the bridge both rows show "Available with the C64 core" and are disabled.

### 5.4 The launch status line

One line per launch, owned by the launch path (`docs/C64_TITLE_CONFIG.md` owns the templates). The buffer holds 63 characters (section 2.1 item 7), so exactly one text is shown: the highest case that applies. The other cases go to the UART summary line and to `:cfg`. A file name longer than 20 characters is shortened, so every template fits 63 characters.

1. Error: any launch error today's code sets, the commit error (`Settings could not be applied; saved settings restored`), a transfer failure (`Load failed; saved settings restored`), the CRT refusal (`Turn REU off, or disable the REU in NAME.cfg`).
2. SuperCPU warning, always shown when no error is: `Title .cfg turned SuperCPU Detect on; Revert before GEOS` (from `$50`: `UCI config turned SuperCPU Detect on; Revert before GEOS`).
3. Skipped, refused or adjusted: `Content loaded; NAME.cfg: 3 settings, 2 skipped`; `Content loaded; NAME.cfg not used (not text)` / `(over 16 KB)` / `(SD read failed)` / `(open on drive 9)`; `NAME.cfg: 3 settings; cartridge runs at 1 MHz`.
4. Command Interface note: `NAME.cfg: 3 settings; Command Interface on`.
5. Clean: `Content loaded; NAME.cfg: 3 settings`.

Every text is ASCII. A host test checks each template at the name limit, and the `.cfg` runtime test T-RT11 covers every case. P56 decides whether the buffer stays at 63 characters.

---

## 6. I/O address and priority map

### 6.1 `$D000-$D3FF`

| Address | Read (first match wins) | Write |
|---|---|---|
| `$D030`, `$D031` | turbo registers override (existing) | VIC, plus turbo snoop (existing) |
| `$D079-$D07B` | VIC (`$FF`) | VIC, plus turbo snoop (existing) |
| `$D0BC` (A9..A0 = `$0BC`, I/O visible) | `$01` when CONFIG bit 20 and mode ≠ 0; otherwise the VIC answers through the unchanged path | VIC only; VIC register `$3C` is unused, so no effect |
| everything else, including `$D0B0-$D0BB`, `$D0BD-$D0BF`, `$D1BC/$D2BC/$D3BC` | VIC | VIC |

- The override is gated, so with Detect off the byte comes from the VIC by construction [V hdl/core/c64/multitini_c64_turbo_regs.sv:63-71 shows today's ungated `$030/$031` terms].
- REU DMA reads of `$D0BC` see the VIC, as for `$D031` today. The debug RAM peek is not I/O.
- The U64 UCI unlock (`$AB` → `$D038`, `$CD` → `$D036`) is not decoded (P9).
- If P28 (a) is chosen: `$D0B8`/`$D0B9` read `$C1` at live index 0 and `$01` otherwise, in the register modes only, not gated on Detect.

### 6.2 `$D400-$D7FF`

| State | Read | Write |
|---|---|---|
| `SID_MAP_ENABLE` = 0 | legacy `sid_mode` decode, unchanged | unchanged |
| `SID_MAP_ENABLE` = 1 | the slot's read owner (lowest set bit of its mask) | every SID in the slot mask; none in a `WRITE_INHIBIT` slot |

- 32-byte slots by A9..A5; registers inside a SID mirror every 32 bytes, as today.
- Native, fast and REU DMA paths use the same table.
- POTX/POTY reach only the read owner of slot 0 (`$D400-$D41F`); every other SID reads `$FF` for POT (today's chip-1 rule [V fpga64_sid_iec.vhd:1012-1017]).
- A table commit takes effect at the next `ce_1m`, or on the next `c64_clk` edge while the core is paused.

### 6.3 `$DE00-$DFFF` with every feature on

"Eligible" means enabled and no REU DMA in progress; REU DMA releases every internal IO2 device, as today [V hdl/core/c64/multitini_c64_adapter.sv:608-614]. Reads: first match wins. A write goes only to the device that owns the read; there is no fan-out between devices.

| Address | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| `$DE00-$DEFF` | cartridge IO1 | IO1 SID slot (caps bit 12 only, XM8; suspended while any CRT is loaded or loading) | open bus | | |
| `$DF00-$DF17` | cartridge IO2 | REU registers (REU on; never with a CRT) | IO2 SID slot (caps bit 12; REU off) | open bus | |
| `$DF18-$DF1F` | cartridge IO2 | **UCI** (bit 5 on; no CRT or a generic 8K/16K/Ultimax CRT; not loading) | REU index `$18-$1F`, reads `$FF` (REU on) | IO2 SID slot (caps bit 12; REU and UCI off) | open bus |
| `$DF20-$DFFF` | cartridge IO2 | UA (on; implies REU on) | REU 32-byte mirror (REU on, UA off) | IO2 SID slots (caps bit 12; REU off) | open bus |

UCI rules:
- It owns the full 8-byte slot: `$DF18-$DF1A` read `$FF`, `$DF1B` the SoftIEC id, `$DF1C-$DF1F` the protocol registers. No mirrors: with UA off the REU still answers `$DF38-$DF3F` and up.
- With the REU on, `$DF18-$DF1A` read `$FF` either way, so turning the UCI on changes only `$DF1B-$DF1F`. The UCI does not need the REU.
- A generic cartridge (`cartridge.sv` type 0) never decodes IO1 or IO2 [V hdl/core/c64/ports/cartridge.sv:161-168, 185-190], so it cannot collide with the UCI. The U64 also keeps the UCI for these types [V 1541ultimate software/io/c64/c64_crt.cc:472-479]. Every other cartridge type, and any cartridge while loading, holds the UCI block in reset (status bit 26). The CRT load is never refused for the UCI. The REU stays suspended under every cartridge, so with a generic CRT the rest of `$DF00-$DFFF` is open bus.
- UCI accesses stay φ2 forwards at every speed; the fast port never serves `$DF00-$DFFF` [V hdl/core/c64/multitini_c64_island_map.sv:69-76]. `$DF1C-$DF1F` are outside the island acknowledge class [V :72-75]; a UCI C64 IRQ would need them in it, so it is deferred (P8).
- Ultimate Audio, REU and cartridge rules are unchanged.

### 6.4 Combinations that are blocked (HDL clamps are the backstop)

| # | Rule | State |
|---|---|---|
| B1 | A CRT load is refused while the REU is requested. Covers `handle_c64_content_load()`, the menu LOAD_CONTENT branch, the stored-CRT restore and the `MT_MENU_ACTION_BROWSE_CARTRIDGE` guard. With title files On the refusal moves to the launch and uses the requested REU (section 8.1) | exists; XM2 changes where it is checked |
| B2 | UA and REU cannot be enabled from the menu while a cartridge is attached ("Eject the cartridge before enabling ...") | exists |
| B3 | UA on turns the REU on; REU off turns UA off | exists (Q18) |
| B4 | Command Interface with a cartridge: never blocked; generic CRT keeps it active; other types show `On (suspended: cartridge)` | new (XM5) |
| B5 | IO1/IO2 SID slots (from caps bit 12, XM8): refuse a map that puts a SID into a range owned by an enabled REU, UA or UCI; refuse turning those on while a SID owns part of their range; refuse a CRT load while an IO1 SID is mapped | new (XM8) |
| B6 | The legacy "SID audio" preset and the SID map are one control | new (XM7) |
| B7 | SuperCPU Detect is never blocked; it shows `(inactive: Turbo Off)` while Turbo Control is Off | new (XM3) |
| B8 | A `.cfg` or `$50` value that breaks B1-B6 is BLOCKED and reported ("NAME.cfg: REU ignored (cartridge)") | new (XM2) |
| B9 | A `$50` file with `RAM Expansion Unit=Enabled` or `Map Ultimate Audio $DF20-DFFF=Enabled` while a CRT is attached is BLOCKED, so the whole `$50` load is refused with `89,CONFIG FILE HAD ERRORS` | new (XM5) |

### 6.5 Defaults that keep the stock trace and Expand identical

| Default | Effect |
|---|---|
| CONFIG bit 20 = 0 | `$D0BC` reads the VIC's `$FF`; the trace reads no `$D0BC` anyway |
| EXPANSION bit 5 = 0 | the REU owns `$DF18-$DF1F` and reads `$FF`, matching stock_bus.csv:26-33 |
| EXPANSION bit 6 = 0 | legacy mode 3, so the trace's REU DMA at `$D500` and `$D7F0-$D7FF` is unchanged |
| `C64_UCI_CONTROL` = 0x1F | no effect while bit 5 = 0 |
| `NET_CONTROL` = 0 | W5100S held in reset; no C64-visible effect |
| no `.cfg` beside the title | no overlay; the launch hook is NULL, so the MMIO write sequence equals today's |
| reset values | `C64_EXPANSION_CONFIG` 0x0B and `C64_CPU_CONFIG` 0x151 unchanged |

The stock-trace checker runs in compare mode at every milestone, with every new input at its default (and, for multi-SID, at `SID_COUNT` 2 and at the image value). The manifest is never edited: it records the one-time capture. The new source hashes go into the checker receipt and the milestone build receipt.

---

## 7. Firmware service model

### 7.1 Placement

| Service | Function | Where | Budget per pass |
|---|---|---|---|
| Network | `net_service_poll()` | `service_runtime()`, every core whose `NET_CAPS` is valid, before the core-specific block | ≤ 1 ms; ≤ 1 KB through the W5100S |
| Drive 9 | `c64_iec_service_poll(32)` | unchanged | unchanged |
| UCI | `c64_uci_service_poll()` | `service_runtime()`, after drive 9 | ≤ 1 ms; file I/O ≤ 4 KB |
| Title `.cfg` | parse and resolve | synchronously in the launch paths, before the load's reset | one file ≤ 16 KiB per launch |
| UCI `$50` | the same parser | inside the UCI service, with its own static 16 KiB buffer | across passes |

- CPU0 only. CPU1 stays out: it runs video-effect jobs and must not touch PL registers or shared drivers [V firmware/include/multitini_video_worker.h:13-27]. FatFs has one non-reentrant owner [V firmware/platform/xilinx/sd_storage.h:41-49].
- No interrupt in these milestones. A later ISR may only set a flag.
- Every UCI handler is a bounded state machine. File copies, REU loads up to 16 MB, DNS, TCP connect and READ_SOCKET waits run across passes.
- ABORT and C64 reset are checked first in every pass and always complete within that pass (fixes U64 #740).

### 7.2 Resets and quiesce points

- Every C64 reset resets the UCI block and advances its epoch. The service then cancels the handler, finishes a UCI REU load with drain and invalidate, closes UCI files, directory snapshots and UCI sockets, and frees HTTP slots. The DHCP lease and a runtime SET_IPADDR address survive.
- The UCI service is quiesced at the drive 9 points: `set_core_reset_request()`, `prepare_c64_content_change()`, `mount_c64_disk()` (except a UCI mount), `supervisor_activate()`, USB SD export, core switch, standby.

### 7.3 Posted supervisor actions and their single definitions

Control `$05` FREEZE, `$06` REBOOT and DOS `$23` MOUNT_DISK, `$24` UMOUNT_DISK never run inside `c64_uci_service_poll()`: the handler posts an action in the `pending_menu_action` pattern and returns PENDING; the main loop runs it after the poll returns. Reason: `set_core_reset_request()` quiesces the C64 services, so a direct call would re-enter the UCI service inside a handler.

- **REBOOT** (one definition for the UCI and `.cfg` designs): a C64 reset through `set_core_reset_request()`. It does not call `restore_selection_settings()`, does not re-read any `.cfg`, keeps the drive 8 image (the reset's own flush and remount of the same image [V main.c `set_core_reset_request()`, `service_runtime()`]) without restoring the stored drive 8 name, and leaves the overlay, the title session, `stored` and the registers unchanged. An attached cartridge stays attached and boots again with the reset. For the overlay it is a "reset by C64 software" event (section 8.4). This matches the U64, where REBOOT is `start_cartridge(NULL)`, a restart with the configured cartridge [V 1541ultimate software/io/command_interface/control_target.cc:134-138; software/io/c64/c64_subsys.cc:279-285]. No reply can arrive, because the reset clears the block.
- **FREEZE** opens the Multitini menu, which pauses the C64; the empty reply is validated when the menu closes.
- **MOUNT/UMOUNT** act exactly like the menu mount and eject of drive 8 (no reset, name remembered); the reply is validated when the action completes.

### 7.4 Files, paths and the temporary folder

- UCI DOS root = the active package's `content_root` (default `0:/multitini/cores/c64/media`), the root drives 8 and 9 use. It must also lie below the active package directory; otherwise every file command answers `DEVICE NOT READY`.
- Aliases at the first path component, case-insensitive: `SD`, `Usb0`, `Usb1`, `Usb2` → the root; `Temp` → `<package directory>/uci_temp` (default `0:/multitini/cores/c64/uci_temp`). That folder lies outside the media root, is created on first use, and is emptied at every C64 core start (bounded, across passes). The `$50` default file `/temp/uci_config.cfg` lives there and is deleted with it.
- Wildcards `*` and `?` in every component, first match in FatFs order; at the root the aliases are tried first. So `/usb*/DMBCFG.CFG` reaches the root, as UBoot64 and DMBoot expect [R UBoot64 README, "configuration files ... saved in the root directory of the first USB drive found (using /usb*/ as path)"].
- One shared protected-path callback, `c64_iec_path_protected()`, serves drive 9 and both UCI DOS targets [V main.c `c64_iec_path_protected()`]. It refuses write, create, rename, delete and open-for-write of: the mounted drive 8 image and its journal; files drive 9 holds open for writing; every `*.mtj`; `core.cfg`, `roms/` and bitstreams if the root is the package directory; and (P21) **real title sidecars**: a `.cfg` or `.usr` whose stem matches a `.prg`, `.crt` or `.d64` in the same folder, checked at each of those operations. Answer `98,FUNCTION PROHIBITED` (UCI) or the drive 9 equivalent. A client's own configuration file such as `DMBCFG.CFG` stays writable. Drive 9 can already replace an existing sidecar by name, so the sidecar rule lands with XM2 and the UCI DOS targets use it from XM5. Drive 9 asks the callback on every open, also for reads, so the callback gains an access-kind argument. Changing this callback re-runs G-IEC9.
- The UCI uses at most 4 of the 16 managed files.
- REU loads from the UCI (DOS `$21`, Control `$08`) do not use the menu pause and do not hold UA. They copy in 4 KB steps, drain, then set `C64_UCI_CONTROL.HOLD`, wait for paused and REU idle (≤ 1 s, retried), invalidate, release. Reason: an invalidate while the C64 runs can deadlock the REU store if a DMA request arrives [V hdl/core/c64/multitini_c64_reu_cache.sv:117-118, 170-172]. The stock REU RTL stays unchanged.

### 7.5 Latency

- Pickup of a PUSH: one superloop pass; target ≤ 1 ms when nothing blocks [O measure].
- Worst case: the longest synchronous action in the loop (content load, journaled save, D64 commit, menu render), seconds. The C64 sees Busy. The U64 gives no bound either.
- At turbo every UCI register access is one φ2, about 1 byte per µs at any speed.

---

## 8. Stored, effective and the title overlay

### 8.1 Model

```
effective = debug_overlay( title_overlay( stored ) )
stored        = settings.bin + c64_sid.bin                  (only stored is saved)
title_overlay = owned fields + values + source + session    (RAM; one at a time)
debug_overlay = today's :speed and debug bits               (RAM; UART session only)
```

`mt_c64_effective()` returns two values: **requested** (what stored settings and the title ask for) and **applied** (what the registers get; REU and UA cleared while a cartridge is attached, matching the HDL clamp [V adapter.sv:608, 611]). CRT-versus-REU decisions and the menu use `requested`; register writes use `applied`.

### 8.2 Rules

1. Every register write for a C64 option comes from `applied`.
2. Every change handler starts from `stored` (or from the overlay when the overlay owns the field), never from a register.
3. Savers serialise `stored` only. This includes the joystick port, which today is saved from the live USB HID value [V main.c `remember_c64_input_settings()`].
4. The menu shows `effective` with ` (title)` on owned rows and ` (cartridge)` where the applied value differs from the requested one.
5. Sources: a title launch replaces the overlay; UCI `$50` merges into it (section 8.5). One parser, one overlay.
6. End events: section 8.4.
7. A menu row or controller action that changes an owned field edits the overlay and saves nothing (P12). An action that touches any owned group goes wholly to the overlay; the other groups it touches become owned, copied from `effective`.
8. Overlay values pass the same caps checks and blocks (section 6.4).
9. Debug knobs are never overlay keys.
10. Apply events: an overlay change of Turbo Control, CPU Speed or Badline Timing is an apply event only if the value changes; SuperCPU Detect never; the CRT restore relaunch never.

### 8.3 Overlay groups and fields

| Group | Replaces | Milestone |
|---|---|---|
| TURBO_CONTROL | profile byte 243 (turbo on) and byte 27 bits 7:6 | XM2 |
| SPEED, BADLINE, FOLLOW | byte 27 bits 3:0, bit 5, bit 4 of the **normalised** byte | XM2 |
| AUTOSLOW, CART_TURBO | byte 26 bits 0, 1 | XM2 |
| EXPANSION_MEM | REU (expansion byte bit 3) and UA (byte 26 bit 2), one group because B3 couples them | XM2 |
| JOYSTICK | profile byte 241 | XM2 |
| SCPU | byte 26 bit 3 | XM3 |
| UCI | byte 26 bit 4 | XM5 |
| SID | XM2: one field, the legacy mode. XM7: a **per-field mask** over `mt_c64_sid_model_t` | XM2, XM7 |

The SID overlay from XM7:
- Fields: the U64 layer (`ultisid_addr[2]`, `ultisid_split`, `auto_mirror`, `socket_addr[2]`, `socket_enable[2]`, `ext_split`, `paddle_override`, group `model[2]`, `digi[2]`, `curve[2]`, `res[2]`, `volume[2]`, `pan[2]`, `pan_from_preset[2]`, `master`), the per-SID layer (`addr`, `model`, `volume`, `pan` for SIDs 1-8) and the output settings (headroom, DC removal, resampling). The UltiSID and mixer keys (`.cfg` K12) are fields of this mask; there is no separate SID_PARAMS group.
- Three key-class bits record which U64 key classes the overlay set: ADDR (any `[SID Addressing]` address key), MIX (any "Vol UltiSid n" or "Pan UltiSID n"), MODEL (any "UltiSID n Combined Waveforms"). A merge ORs them; nothing but the end of the overlay clears them. While a class bit is set, the stored per-SID fields of that class read as Follow, unless the overlay itself sets them through `[Multitini SID]`. So a U64 file gives the U64 layout even when stored per-SID overrides exist.
- `_merge()` is per field for SID. Two `$50` files, one setting addresses and one setting only "Pan UltiSID 1", leave the first file's addresses in place.
- K11 (U64 address keys), K17 (`[Multitini]` "SID audio") and `[Multitini SID]` keys apply in file order, per field. At XM7 K17 gains Triple, Quad and Eight.
- `change_c64_sid_setup(item, delta)` checks ownership per sub-page item: an owned item edits the overlay and saves nothing; `c64_sid.bin` stays byte-identical.

### 8.4 When the overlay ends

Rule: every overlay end writes `effective(stored)` (changed registers only, apply = true), unless a launch commits right after or the core stops.

| Event | Overlay | Title session |
|---|---|---|
| Next trigger launch | replaced by the new file, or cleared if none | replaced |
| Launch refused or failed before the reset | unchanged | unchanged |
| Launch failed after the commit | cleared, stored values written | cleared |
| Options → Reset | PRG source: cleared. D64 source: kept while drive 8 holds a disk (the source or a later swap), cleared when drive 8 is empty. CRT source: cleared, then re-applied by the CRT restore relaunch | PRG cleared; D64 kept with the overlay; CRT kept |
| "Fast serial ROMs" toggle (restarts the C64) | CRT source kept; D64 source as Options → Reset; PRG source cleared | same as the overlay |
| Reset by C64 software; UCI REBOOT | unchanged | unchanged |
| Cartridge detach | cleared; stored values committed inside a reset hold | cleared |
| Drive 8 eject or mount (disk swap) | unchanged | unchanged |
| Core switch, variant change, `supervisor_activate()` | cleared | cleared |
| Standby, power-off | cleared | cleared |
| USB SD export | cleared | cleared |
| "Revert title settings" (argument 18), `:cfg revert` | cleared, stored values written | kept |
| "Title .cfg files" → Off | cleared, stored values written | kept |
| UCI `$50` LOAD_CONFIG | merged | unchanged |
| Load into REU, `:speed`, debug commands | unchanged | unchanged |

- The boot auto-mount and the D64 restore never apply a `.cfg`; a plain drive 8 mount never does (status hint only). The stored CRT restore re-applies its file as a relaunch with apply = false (P15).
- **SuperCPU consequence.** A title file that turned Detect on stays on across a disk swap, a C64 software reset and, for a D64 source, Options → Reset. The advice in the help text, the status line and the docs is therefore **"Revert title settings" before booting GEOS or GDOS64**, which always ends the overlay (or setting the row to Disabled). `docs/C64_SUPERCPU_DETECT.md` owns the exact help and status wording.

### 8.5 UCI `$50` LOAD_CONFIG

- Path: bytes 2 and later, resolved by the DOS rules; default `/temp/uci_config.cfg` (section 7.4).
- Same parser and resolver; own 16 KiB buffer.
- **All or nothing:** a MALFORMED, BAD_VALUE or BLOCKED line applies nothing and answers `89,CONFIG FILE HAD ERRORS`. UNKNOWN_ITEM, UNAVAILABLE and NOT_APPLICABLE are logged, not errors. The candidate is separate from the overlay, so a failed load leaves nothing behind (the U64 writes good lines into its stores even when the parse fails).
- **Merge on success**, per group and, for SID, per field. With no overlay a new one is created, attached to the title session if one exists.
- `Command Interface=Disabled` from `$50` is INFO "ignored when loaded through the Command Interface".
- Applied live, without a reset. Turbo and SuperCPU changes write their registers directly. REU, UA and SID-map changes are applied under `C64_UCI_CONTROL.HOLD`: set HOLD, wait for paused and REU idle (≤ 1 s), write `C64_EXPANSION_CONFIG` and the SID table, release. Same steps as the LOAD_REU invalidate.
- With a CRT attached, B9 applies.
- Reply: the log, ≤ 511 bytes; status `00,OK`, `88,CANNOT OPEN CONFIG FILE`, `89,CONFIG FILE HAD ERRORS`, or `87,INTERNAL ERROR` when HOLD or REU idle is not reached (nothing applied).

### 8.6 `.cfg` dialect and option registry

- Sections read: `U64 Specific Settings` (alias `C64U Specific Settings`), `C64 and Cartridge Settings` (alias `Cartridge and ROM Settings`), `SID Addressing`, `UltiSID Configuration` and `Audio Mixer` (XM7), `Multitini`, and `Multitini SID` (XM7).
- `SID Sockets Configuration` is NOT_APPLICABLE until XM7, and from XM7 only if P34 (socket emulation) is adopted. A parser test uses a `[Multitini SID]` fixture.
- NEVER applied: `Network Settings`, `Ethernet Settings`, `WiFi settings`, `Data Streams`, `Modem Settings`, `User Interface Settings`.
- File cap 16 KiB (a full U64 dump is about 5-9 KB [I]); lines up to 319 characters; a BOM, CR-only and CR LF line ends, and a last line without LF are accepted.
- All four features register their options in one portable table, `firmware/common/c64_option_registry.c` (host-tested): id, menu argument, storage, scope, caps gate, `.cfg` section, item, aliases and values, apply class, overlay allowed. Menu rows, the `.cfg` reader, `$50`, the overlay and the settings codec all read it. Keys outside the registry are ignored and reported.

---

## 9. Network infrastructure (XM4)

### 9.1 Facts

- Pins: Bank 35, LVCMOS33 [V third_party/appletini/constraints/multitini_pins.xdc:362-392]. Ports [V top.sv:88-94]. Idle block with `eth_rst_n` = 0 [V top.sv:2329-2334].
- Appletini's front end runs at 133 MHz: reset hold 10 ms, ready wait 61 ms, read and write strobes 16 cycles, bus setup 2 cycles, recovery 5 cycles [V Appletini hdl/apple/uthernet2_card.sv:22-28, 70-71]. Its host port is `host_req/host_write/host_addr[15:0]/host_wdata/host_ready/host_done/host_error/host_rdata` [V :46-53].
- Multitini's `control_clk` is PS FCLK0 at 133 MHz [V scripts/create_project.tcl:193], so the cycle counts carry over unchanged (16 × 7.5 ns = 120 ns strobes).
- Appletini constrains the `eth_*` pins only for power estimation (`set_switching_activity`) [V Appletini hdl/constraints/appletini_yarz.xdc:661-673]. There is no timing rule to copy.
- Appletini's driver issues one byte per command, has socket helpers, reads PHYSR bit 0 for link, and runs DHCP over MACRAW [V Appletini ps_sources/frontend/uthernet2_control.c:16-20, 366-383]. Its DHCP parser keeps options 53, 54, 1 and 3 only: no DNS server, no lease time [V :646-673]. Its MAC is a fixed `02:41:50:50:4C:01` [V :93-101], which would collide between boards. It splits socket memory 4/2/1/1 KB (`0x06`) [V Appletini uthernet2_control.h:12-14].
- Appletini is GPL-3.0; these files carry no per-file header. Multitini already pins other Appletini imports in `provenance/sources.lock.json`.
- W5100S datasheet items not checked here [O, E-X3]: MACRAW on socket 0 only; RMSR/TMSR encoding for 2 KB per socket; UDP broadcast with a configured source address; how a refused connect (RST) shows in Sn_IR/Sn_SR; the UDP receive header; RTR/RCR defaults; read data valid time after RD# against the 112 ns before sampling.

### 9.2 PL bridge

- New shell module `hdl/board/multitini_w5100s_bridge.sv`, ported from the host half of `uthernet2_card.sv`: bus-master FSM, reset hold and ready wait, indirect-address tracking. The Apple slot decode and the W5100 mode emulation are dropped. Appletini `73de6fe` is pinned in `provenance/sources.lock.json`, `licenses/files.csv` gains the rows, and the file header names the source commit.
- Clock `control_clk` only. No new clock, no MMCM, no crossing except `eth_int_n` (two-flop `cdc_bit_sync`; the existing static false path stays).
- Reset: `control_aresetn` and `NET_CONTROL.RUN`. After every PL load the bridge holds RST# low until RUN, then performs the full hold and wait, because the pins float during configuration [O: board pull-ups, E-X14].
- Every output, the data output enable and the data input capture are IOB registers. `eth_d` is tri-stated.
- Images (P5): instantiated under a top parameter `NET_BRIDGE`, set to 1 only in the C64 images. Every other image keeps today's idle block and reads `NET_CAPS` = 0. So every switch away from the C64 resets the chip.

### 9.3 Constraints (exact text for `hdl/board/multitini_timing.xdc`)

```
# W5100S parallel bus, driven by multitini_w5100s_bridge on control_clk (133 MHz).
# The bridge drives eth_a and write data from the first cycle of an access,
# lowers CS# and RD#/WR# together 2 cycles (15 ns) later, holds the 16-cycle
# (120 ns) strobe and samples eth_d in its last cycle, then raises CS#, RD#/WR#
# and the data enable on one edge; eth_a holds through the 5 recovery cycles. All of
# these are IOB flops, so pin-to-pin skew is small against that margin.
# tb_multitini_w5100s_bridge asserts the cycle shape.
set_false_path -to   [get_ports {eth_d[*] eth_a[*] eth_rd_n eth_wr_n eth_cs_n eth_rst_n}]
set_false_path -from [get_ports {eth_d[*]}]
```

- The timing coverage gate then reports these ports as "no output delay but user has a false path constraint (MEDIUM)" (and the same for the input), the class it already accepts for the PSRAM inputs [V build/Multitini-070dev7/evidence/c64-pal/reports/timing_coverage_gate.rpt:40-58].
- In images without the bridge the ports stay constants; the false paths have no effect there.
- The comment describes the ported Appletini sequence [V Appletini hdl/apple/uthernet2_card.sv:729-815]. In it, write data has no designed hold after WR# rises. docs/C64_UCI.md (open frame items FI-1 and FI-2) proposes a one-cycle data hold and a build check of the IOB packing; both wait for the W5100S hold time (E-X3).
- Review item C9 (section 11). The argument holds only while the FSM keeps the stated shape, which the bench checks on every build.

### 9.4 Firmware

- Portable driver `firmware/common/net_w5100s.c` (register access through an ops struct; sockets; DHCP; DNS), host-tested against a C register model of the W5100S. Ported from `uthernet2_control.c` where it fits; DHCP lease handling and DNS are new code.
- Platform service `firmware/platform/xilinx/net_service.c`: settings, poll, status, UART `:net`, and the API the UCI uses: `mt_net_interface()`, `mt_net_resolve_start/poll()`, `mt_net_open/poll()`, `mt_net_listen()`, `mt_net_send/recv()`, `mt_net_close()`, `mt_net_close_owner()`, `mt_net_set_runtime_address()`, `mt_net_clear_runtime_address()`. Owners (`MT_NET_OWNER_UCI`) let a C64 reset close only the UCI's sockets.
- About 2-3 GP0 accesses per W5100S byte with the single `NET_ACCESS` write, against 4-6 in Appletini's driver [I; O measure].

### 9.5 Socket plan

| Socket | Use | Mode | Local port | When |
|---|---|---|---|---|
| S0 | DHCP acquisition (INIT, SELECTING, REQUESTING) | MACRAW | — | only while no address is held |
| S0 | DHCP renew and rebind, DNS | UDP | 68 | whenever a DHCP address is held; received datagrams are demultiplexed by source port (67 → DHCP, 53 → DNS) and by transaction id |
| S0 | DNS (static mode) | UDP | 68 | always in static mode |
| S1-S3 | UCI sockets: TCP, UDP, the listener | TCP or UDP | ephemeral 49152-65535 | on demand |

- While S0 is in MACRAW there is no address, so every UCI open answers `83,INTERFACE NOT AVAILABLE` at once. No DNS request ever waits on MACRAW.
- At lease expiry the service cancels a pending lookup (`84,UNRESOLVED HOST`), closes the UCI sockets on S1-S3 (each answers its next READ with `00 00` and `01,CONNECTION CLOSED BY HOST` once), drops the address and switches S0 to MACRAW.
- One DNS query is in flight at a time; at most two waiters queue (UCI, later HTTP), each with its own deadline.
- DNS shares S0's fixed local port 68, so only the 16-bit transaction id protects against spoofed replies. Accepted for a LAN device, listed for review [I].
- UCI socket ids are logical, 1-15, handed out in rotation, never 0 (xlar54's `uii_connect()` treats 0 as failure [R ultimate_lib.c]). A fourth concurrent open answers `85,ERROR OPENING SOCKET`. A live socket is never evicted. A socket the C64 already closed that still lingers in FIN or TIME-WAIT is reclaimed when an open needs it.
- Socket memory 2 KB RX and 2 KB TX per socket [I; O E-X3]. A full Ethernet frame (1,514 bytes) fits S0 in MACRAW; the largest UCI read (1,472 bytes) fits S1-S3.

### 9.6 DHCP, DNS, timeouts and lifetimes

- DHCP options parsed: 1, 3, 6 (two servers), 51, 53, 54, 58, 59. Without 58/59, T1 = 0.5 and T2 = 0.875 of the lease.
- Renew at T1 by unicast UDP to the server; rebind at T2 by broadcast UDP from the leased address [O: W5100S UDP broadcast with SIPR set; if it fails, rebind falls back to MACRAW after closing UCI sockets]. Expiry → INIT. A NAK → INIT.
- Link loss keeps the lease and the sockets. Link return with a valid lease → one unicast renew; else INIT.
- Return to the C64 core after a core switch: the chip was reset, so firmware writes the cached address (if the lease is still valid) and confirms it with a renew; else INIT. Expected 1-3 s, asynchronous [I].
- DNS: A records only; CNAME followed up to 4 steps; dotted IPv4 bypasses DNS; two servers; 2 s per server, one retry, then the other server; 16-bit random transaction id; replies with a wrong id or source port are dropped; an 8-entry cache honouring TTL.
- Timeouts (all deferred replies; ABORT cancels any of them in the next pass): TCP connect 10 s firmware deadline; READ_SOCKET 40 ms (U64 parity); WRITE_SOCKET buffer space 2 s; close linger 1 s.
- Idle READ on a live socket answers `FF FF` and `02,NO DATA: 11`, as on the U64; xlar54-based clients take a length of 0 as end of file, so the reply is kept (P52). A closed TCP socket answers `00 00` and `01,CONNECTION CLOSED BY HOST` once, then `FF FF` and `02,NO DATA: 9`.
- C64 reset closes every UCI socket (fixes U64 #808). The lease survives.
- SET_IPADDR (UCI) writes the address at run time only and suspends renew; it stays until the Network setting changes, a core switch or a board reset (P46).

### 9.7 MAC address

- A `mac=` key in `network.ini` wins. It must be unicast.
- Without it, firmware generates a MAC once, at the first switch from Off to DHCP or Static, and writes it to `network.ini` at once. Seed: a CRC-based hash over the SD card CID (16 bytes, unique per card), the 64-bit global timer at that key press, and 16 XADC readings through the PS-XADC interface. First octet `(x & 0xFC) | 0x02`: locally administered unicast [I; O E-X13 that the SD driver exposes the CID]. It is never regenerated while the key exists.
- Alternative (**USER**, P54): the Zynq device DNA through a `DNA_PORT` in the bridge. Stable per board even if the card changes, but the primitive's clock must stay below its data-sheet limit [O], which is a new clock rule to review.

### 9.8 Settings and status

- `0:/multitini/network.ini`, strict `key=value` in the core-package parser style [V firmware/common/core_package.c:203-253], rewritten whole through the journal. Keys: `mode=off|dhcp|static`, `ip`, `netmask`, `gateway`, `dns1`, `dns2`, `hostname`, `mac`. A malformed value rejects the file: mode Off and a warning. An unknown key is a warning only. It is text so a static address can be set on a PC; the menu has no address editor.
- Default when absent: Off, until G-NET passes on the board. Then P53 decides.
- System rows (section 5.3) show `Off`, `DHCP: waiting for lease`, `DHCP 192.168.1.37, link up`, `Static 192.168.1.50, link down`.
- UART `:net` (normal command): mode, link, MAC, IP, mask, gateway, DNS servers, lease left, T1/T2, socket table. `:net set KEY VALUE` writes `network.ini`. `:net resolve NAME` runs one lookup. Raw W5100S register access and packet traces are debug features: off by default, refused without `:debug on`.
- A title `.cfg` never changes the network (section 8.6). A title file can enable the Command Interface, but not the network.

### 9.9 Tests (gate G-NET)

| # | Test | Content |
|---|---|---|
| T-NET1 | `hdl/sim/tb_multitini_w5100s_bridge.sv` (host half of Appletini `hdl/sim/tb_uthernet2_card.sv`) | reset hold and ready wait (scaled); read and write cycle shape: setup ≥ 2, strobe 16, recovery 5, outputs stable through the strobe (the XDC argument); indirect pointer reload only when needed; bad write while busy; sequence counter; INT synchronizer; RUN = 0 holds RST# low |
| T-NET2 | `tests/host/test_net_w5100s.c` with a C register model | socket open/close/send/recv; RX and TX ring wrap; `Sn_RX_RSR` read until stable; command-completion timeout |
| T-NET3 | DHCP host test | discover, offer, request, ack over the MACRAW model; options 1, 3, 6, 51, 54, 58, 59; renew at T1 (UDP), rebind at T2, expiry closes UCI sockets; NAK; link loss and return; core-switch return with a cached lease |
| T-NET4 | DNS host test | query build and parse, compression pointers, CNAME chain, NXDOMAIN, timeout, retry, second server, TTL cache, source-port demux on S0 port 68, wrong id dropped; lookup cancelled at expiry |
| T-NET5 | socket allocator host test | ids 1-15 never 0, fourth open `85`, lingering reclaim, close by owner on C64 reset |
| T-NET6 | `network.ini` host test | strict parse, bad values, missing file = Off, journaled write, MAC rules and generation |
| T-NET7 | `tests/host/test_system_frontend.c` (the System page is built in `firmware/common/system_frontend.c`) | System page re-indexed to 23 items; rows disabled without the bridge; status texts fit |
| T-NET8 | runtime test (mocked MMIO) | poll budget; quiesce on core switch; no `NET_*` access below ABI 26 or with `NET_CAPS` = 0 |
| build | C64 PAL and NTSC | timing coverage gate passes with the new XDC; a non-C64 image reads `NET_CAPS` = 0 and keeps RST# low |
| board | N-B1 to N-B7 | link up and down; DHCP lease from a home router; renew seen at T1 with a short-lease PC DHCP server; static mode; `:net resolve`; core switch away and back; two boards on one LAN with different MACs |

### 9.10 Work steps (sizes in working days [I])

| Step | Content | Size |
|---|---|---|
| N1 | Datasheet checks (E-X3); pin the Appletini import | 1 |
| N2 | Bridge RTL, T-NET1 | 3-5 |
| N3 | Top parameter, registers in `multitini_control_regs.sv`, XDC, C64 build, CDC review | 2 |
| N4 | Driver port and W5100S model, T-NET2 | 4-6 |
| N5 | DHCP with lease, renew and rebind; DNS; T-NET3, T-NET4 | 4-6 |
| N6 | Service, socket allocator, `network.ini`, MAC, System rows, `:net`; T-NET5 to T-NET8 | 3-4 |
| N7 | Board N-B1 to N-B7 | 2-3 |

Total about 19-27 working days, plus build time.

---

## 10. Resource budget

### 10.1 Per-feature estimates

New-RTL estimates in this repository have run 25-300 % low; replicated measured blocks are good to about ±10 % [I from past builds]. The table gives the raw estimate and, for new RTL, a ×2 bound.

| Block | LUT raw | LUT ×2 | FF | BRAM tiles | DSP | Milestone |
|---|---|---|---|---|---|---|
| SuperCPU Detect | 10-20 | — | 0 | 0 | 0 | XM3 |
| W5100S bridge | 150-300 | ≤ 600 | 100-150 | 0 | 0 | XM4 |
| UCI block, transport, window, STATUS2 shadow, HOLD | 300-600 | ≤ 1,200 | 280-450 | +1 (RAMB36: a 32-bit true dual-port port does not fit a RAMB18 [I; O E-X4]) | 0 | XM5 |
| `controls_i` +16, `status_i` +73 | ~60 | — | ~180 | 0 | 0 | XM5, XM7 |
| XM5b loader kind, FREEZE hold, RAM read | 150-300 | ≤ 600 | 100-150 | 0 | 0 | XM5b |
| Multi-SID E1-4 (2 `sid_top` pairs, 6581) | 3,400-4,000 | 4,200-5,300 | +3,300 | −1 (curve trim) to +2 | +17 | XM7a |
| E1-4 with runtime 8580 | 4,300-5,700 | 5,100-7,000 | +3,400 | −1 to +2 | +17 | XM7a |
| E1-8 (4 pairs) | 8,900-9,500 | 9,900-11,100 | +7,400 | 0 to +6 | +39 | XM7b |
| E1-8 with runtime 8580 | 10,600-12,900 | 11,600-14,500 | +7,600 | 0 to +6 | +39 | XM7b |
| E3 hybrid 8 (pair 0 kept, voice-serial SIDs 3-8) | 2,900-4,500 | 3,900-6,000 | +2,500-3,000 | −1 to −0.5 | +21 | XM7b |
| `.cfg`, network firmware | 0 | 0 | 0 | 0 | 0 | — |

SID figures include the slot table, decode, control registers and streamer, and the mixer (2 DSP per SID plus 2 for UA); the "curve trim" is `MULTI_FILTERS = 0`, one RAMB18 per curve table [O, E-X2].

### 10.2 Projected totals (PAL)

"Base" = dev7 plus XM3, XM4, XM5 and XM5b: 36,689 + 670 to 36,689 + 2,480 LUT. The last column is the average LUT per slice needed to stay at or below 95 % slices (12,635). Today the design packs 2.93 LUT per slice. Vivado packs denser as the device fills, so a linear projection is pessimistic [I]; above about 3.5 the design is unlikely to place and route [I]. The routed build is the only evidence.

| Configuration | Image LUT | LUT % | BRAM tiles | DSP | LUT/slice needed for ≤ 95 % |
|---|---|---|---|---|---|
| dev7 release | 36,689 | 69.0 | 128 | 33 | 2.90 |
| Base | 37,400-39,200 | 70-74 | 129 | 33 | 2.96-3.10 |
| Base + E1-4 | 40,800-44,500 | 77-84 | 128-131 | 50 | 3.23-3.52 |
| Base + E1-4 + 8580 | 41,700-46,200 | 78-87 | 128-131 | 50 | 3.30-3.65 |
| Base + E3 hybrid 8 | 40,300-45,200 | 76-85 | 128-129 | 54 | 3.19-3.57 |
| Base + E1-8 | 46,300-50,300 | 87-95 | 129-135 | 72 | 3.66-3.98 |
| Base + E1-8 + 8580 | 48,000-53,700 | 90-101 | 129-135 | 72 | 3.80-4.25 |

Findings [I]:
- Even Base needs denser packing than today to stay under 95 % slices. Every HDL milestone must record slices.
- Four SIDs need 3.2-3.5 LUT per slice, or a lever. Eight SIDs with replicated pairs are very unlikely to fit; with 8580 the upper bound exceeds the device. The E3 hybrid is the realistic 8-SID path.
- BRAM and DSP are not limiting.

### 10.3 Ceilings and levers

Ceilings (P3): BRAM ≤ 136 tiles; routed WNS > 0 and WHS > 0 on every clock group, PAL and NTSC; fast-island WNS ≥ +0.100 ns (the repository default, scripts/build_and_export_xsa.tcl:207); slices ≤ 95 %. dev7 PAL meets the slice and BRAM ceilings and misses the fast-island one.

Levers, cheapest first:
1. Trim each `f6581_curve` to curve 0 (−1.5 tiles per table instance; curves 1-3 are unreachable today [V adapter.sv:771 `sid_cfg(4'b0000)`]).
2. Turn the shipped UA mixer (1,520 LUT, 7 DSP, parallel multipliers [V build/Multitini-070dev7/evidence/c64-pal/reports/utilization.rpt:141; hdl/core/c64/multitini_c64_ua_mix.sv:46, 64-65]) into the one-multiplier serial mixer that UAD §2.7 describes, and share it with the SID mix: about −1,000 LUT, −6 DSP [I].
3. True dual-port buslogic ROMs: −5 tiles (edits an upstream-derived file) [D TD:2175 R9].
4. Pack the video frame banks: about −12 tiles [I].

Levers 1 and 3-4 save BRAM, which is not limiting; only lever 2 saves LUTs. LUT savings beyond it need design work on existing blocks (XM0 may find some).

### 10.4 Fit experiment XM6

- Stage A, out of context: OOC1 `sid_top` as built (calibrate against 2,629 LUT); OOC2 `MULTI_FILTERS = 0`; OOC3 runtime 8580 per pair; OOC4 slot table and decode at 4 and 8; OOC5 mixer at 4 and 8; OOC6 the E3 engine if a prototype exists.
- Stage B, full diagnostic builds (never released, `MULTITINI_C64_DIAGNOSTIC_BUILD=1`), PAL and NTSC, with the XM3-XM5 blocks: BC1 E1-4, BC2 E1-4 + 8580, BC3 E1-8, BC4 E1-8 + 8580 (only if BC3 passes), BC5/BC6 E3 hybrid ± 8580.
- Record per candidate: LUT, FF, slices, BRAM, DSP, WNS/WHS per clock group, the worst path per group, and for the fast island the A5-A9 load counts, their net delays and the worst half-period slack (C17).
- Pass: the ceilings of 10.3. If no 8-SID candidate passes, ship 4 SIDs, or apply lever 2 and retry.
- Small benches pass before any full build.

---

## 11. New clocks, resets, crossings and timing rules (review list)

No new clock and no new MMCM in any milestone.

| # | Item | Kind | Primitive | Review point | Milestone |
|---|---|---|---|---|---|
| C1 | CONFIG bit 20 | existing crossing | `controls_i` | none new | XM3 |
| C2 | EXPANSION bits [6:0] | wider existing crossing | `controls_i` 120 → 136 (with C13) | REU, UA, UCI and SID ownership on one edge | XM5 |
| C3 | `$D0BC` compare | logic on the fast read D path into `io2_q` (full period) **and** new loads on T65 A0-A9, which also drive the negedge island BRAM address, the half-period path that sets fast-island slack | — | one shared compare with the `$030/$031` terms; record A0-A9 loads, net delays and worst half-period slack before and after; TD R2 ladder step F1 (Pblock, address replication / `MAX_FANOUT`) if below the floor. Registering the detect bit does not help | XM3 |
| C4 | UCI buffer | dual-clock BRAM | `xpm_memory_tdpram`, independent clocks, 8-bit `c64_clk` port, 32-bit `control_clk` port | the C64 writes the command area only while CMD_BUSY = 0; the PS reads it only while CMD_BUSY = 1; the PS writes the reply area only in Busy, before the op | XM5 |
| C5 | UCI ops PS → C64 | event crossing | `multitini_async_mailbox`, 32 bits | stale-op drop by sequence and epoch tag | XM5 |
| C6 | UCI STATUS, STATUS2 | wider word crossing | `status_i` +64 | both words from one cycle; read as a pair through the shadow | XM5 |
| C7 | UCI block reset | C64 reset, suspension, disable | `core_reset_n`, `uci_active` | epoch and sequence counters outside the block reset | XM5 |
| C8 | SID table PS → C64 | new crossing; about 670 table bits sent entry by entry | `multitini_async_mailbox` (38 + 8 bits) that never stalls (`dst_ready` = 1), plus a swap token | swap on one edge at `ce_1m` or while paused; resend after a destination reset | XM7 |
| C9 | W5100S bridge outputs and input | I/O timing exception | IOB flops; `set_false_path` (section 9.3) | the protocol holds every signal ≥ 2 setup and 16 strobe cycles and samples late in the strobe; T-NET1 asserts it | XM4 |
| C10 | `eth_int_n` | async input | 2-flop `cdc_bit_sync`; existing static false path kept | | XM4 |
| C11 | C64 RAM write for the UCI (load kind 0x17) | extends the load path | `c64_load_mailbox` | must not change REU DMA timing (G-REU0) | XM5b |
| C12 | UCI HOLD (REU invalidate, live `$50`); FREEZE hold | existing pause path via `controls_i` | — | shared with the menu pause and `prg_install_pause`; whether an REU DMA stops and reaches IDLE under it (E-X5) | XM5, XM5b |
| C13 | `C64_UCI_CONTROL` | word crossing | `controls_i` +14 | static value; `$DF1B` may lag one snapshot | XM5 |
| C14 | PS buffer writes before the op write | ordering on one AXI slave | firmware DSB plus a STATUS read before the op write | the GP0 memory type in the BSP translation table [O, E-X15] | XM5 |
| C15 | early read address for the buffer window | `control_clk` only | new outputs of `multitini_axi3_regs` | BRAM data valid on the `reg_rd_en` edge | XM5 |
| C16 | STATUS2 shadow | `control_clk` only | register latched on the STATUS read | | XM5 |
| C17 | SID fast-read decode | new loads on T65 A5-A9 (the same nets as C3, much larger load count: slot mask, owner and write-inhibit tables indexed by `fio_rd_addr[9:5]` [V cpu_island.sv:165]) | — | count A5-A9 loads per XM6 candidate; plan the mitigation before XM6: a replicated copy of the T65 address register bits 9:5 that feeds only the SID decode (`MAX_FANOUT` or explicit duplicate flops), or TD R2 step F1. Logic depth on the full-period `io2_q` leg is a second, smaller concern | XM6, XM7 |
| C18 | SID swap status C64 → PS | wider word crossing | `status_i` +9 | match only with `sid_map_valid` = 1 | XM7 |

---

## 12. Milestones

### 12.1 Order, dependencies, size and gates

Sizes are working-day estimates for implementation and host tests [I]; Vivado build time and board days come on top.

| ID | Content | Depends on | Image | ABI | Size | Gates |
|---|---|---|---|---|---|---|
| **XM0** | Recover PAL fast-island slack to ≥ +0.100 ns with the dev7 feature set, using the TD R2 ladder; record the A0-A9 fanout baseline for C3 and C17; re-check NTSC | — | C64 PAL, NTSC | 0.25 | 1-3 weeks | §13 gates; fast island ≥ +0.100 ns |
| **XM1** | Stored/effective split, `mt_c64_effective()` (requested/applied), option registry, bounds-checked append and limit 20, joystick saver fix; golden MMIO traces recorded before the change | — | none (firmware) | none | 2-3 days | G-SET; MMIO traces identical; menu capacity test; G-REU0 on the board |
| **XM2** | Title `.cfg` files for today's options (turbo, REU, UA, legacy SID layouts, `[Multitini]` keys); byte 26 bit 5; rows 17-18; `:cfg` | XM1 | none | none | 3-5 weeks | G-CFG |
| **XM2d** | "Save settings for this title" (row 19, merge writer, `:cfg save`) | XM2 | none | none | 2-3 days | G-CFG writer cases |
| **XM3** | The 0.26 spec edit (every register of section 3); SuperCPU Detect RTL, row 14, byte 26 bit 3, `.cfg` key; package ABI 26 | XM0 (or P2 waiver), XM1, XM2 (for the key); review of this plan | C64 PAL, NTSC | 0.26 | 1.5-2 weeks | G-SCPU |
| **XM4** | W5100S bridge (C64 image), network service, `network.ini`, System rows, `:net` (section 9) | XM3 (spec) | C64 | 0.26 | 4-6 weeks | G-NET |
| **XM5** | UCI: PL block, buffer, dispatcher; DOS 1/2; Control subset including `$50` and GET_HWINFO `$01` for legacy modes 0/2/3; Network `$03` on XM4 (0 interfaces without it); SoftIEC answers `$05`; HTTP an empty target; HOLD; row 15; byte 26 bit 4 | XM3, XM2 (`$50`), XM4 (`$03`) | C64 | 0.26 | 7-14 weeks | G-UCI |
| **XM5b** | Load kind 0x17, FREEZE hold, `C64_UCI_RAM_READ`, SoftIEC target on drive 9, `$DF1B` = 9 | XM5 board pass | C64 | 0.26 | 2-4 weeks | G-UCI (XM5b part), G-IEC9, G-REU0 |
| **XM6** | Joint fit experiment (section 10.4); choose the SID engine | XM5 blocks present; stage A can start earlier | diagnostic only | — | 3-6 weeks | results recorded against G-FIT |
| **XM7a** | Multi-SID at 4 SIDs (E1): table, decode, mixer, sub-page, `c64_sid.bin`, SID `.cfg` keys, GET_HWINFO `$01` map mode and `$02` | XM6 pass | C64 | 0.26 (0.27 if the shape changed) | 3-5 weeks | G-SID, G-FIT |
| **XM7b** | 8 SIDs: E1 if BC3/BC4 passed, else the E3 hybrid | XM7a | C64 | same | E1: 1-2 weeks; E3: over 1 month | G-SID, G-FIT, E3 lockstep bench |
| **XM8** | HTTP target `$06`; later items each on its own: IO1/IO2 SID slots (caps 12, B5), UCI C64 IRQ (caps 14), the TCP listener if not in XM5, FTP SD service (default Off) | XM4, XM5 | C64 | 0.26 | 2-4 weeks per item | per item |

### 12.2 Why this order

- XM0 first, because XM3 and XM7 both add loads to the half-period path that is already below the floor.
- XM1-XM2 are firmware only, fix the persistence leak before any new option exists, and give every later feature its `.cfg` key path.
- XM3 is small. It proves the 0.26 spec, caps gating and a full C64 rebuild with almost no new logic.
- XM4 before the XM5 Network target, because the target needs a separately tested network service. XM4 can run in parallel with the XM5 PL work.
- Multi-SID last, behind the fit experiment, because it is the only feature that can break the fit.
- HTTP later: the W5100S has no TLS and we have no TLS stack, so `https://` would silently be plain TCP, as on the U64; the target is large (header and body slots, a JSON builder) for few clients; it needs DNS, TCP and timeouts that XM5 proves first; the U64 itself added it only in 3.15. Until XM8, `$06 $01` answers `NO TARGET`.

---

## 13. Regression gates every milestone keeps

| Gate | Definition |
|---|---|
| G-REU0 | Expand by Bonzai disks 1 and 2 on the board: UA Off, Turbo Off, then Manual index 0, Registers preference `$00`, TurboEnable 64 MHz; also after a 16 MiB preload; reudiag `reutiming` [D TD:2300-2305]. Firmware-only milestones run it too, because firmware writes the configuration |
| G-STK | `scripts/test_c64_ua_stock_bus.py` in compare mode reproduces `tests/c64_ua/stock_bus.csv` with every new option at its default (and at `SID_COUNT` 2 and the image value from XM7). The manifest is never edited; source hashes go into the receipts |
| G-LOCK | M2 lockstep: exact mode equals stock at the consumers [D TD:2275-2305]; runs with CONFIG bit 20 = 0 |
| G-TURBO | X-T1 pacing at all 16 indices, X-DET UA detection, edge-stage cases, schedule and interrupt suites. T-S1c (exhaustive `sid_mode` × address) is only planned [D TD:2351]; until it exists, the multi-SID legacy baseline bench (B2 of that design, run against today's text) stands in |
| G-AUD | 1- and 2-SID outputs bit-identical at defaults: `tests/test_audio_path.py` literals and UA T7 |
| G-SET | frozen schema-10 fixture round trip; the byte 26 steps of section 4.3; companion-file codecs (CRC; bad file = defaults) |
| G-DBG | debug features off by default and UART-only; every debug command refuses without a session |
| G-IMG | routed timing within the ceilings of 10.3 (or under the P2 policy); slices recorded and within the ceiling; CDC and timing-rule review of every new item of section 11; timing coverage gate passes; small tests before any full build |
| G-SPEC | `tests/test_interface_spec.py`, `make check-generated`, `tests/test_core_package_tool.py` |
| G-IEC9 | drive 9 suites (`scripts/test_c64_iec9.py`, `tests/host/test_c64_iec_dos.c`); re-run whenever the shared protected-path callback changes |

Firmware-only milestones (XM1, XM2, XM2d) do not change the image, so G-STK, G-LOCK and G-TURBO hold by construction there; they still run G-REU0.

---

## 14. Feature gates

| Gate | Passes when |
|---|---|
| G-CFG | Parser, overlay, content-service and runtime tests pass. No `.cfg` gives byte-identical MMIO writes and `settings.bin`; the one listed UI change is the Cartridge row and its browser guard, which stay enabled with title files On so that a CRT whose `.cfg` turns the REU off can be chosen. `settings.bin` never holds an overlay value after any sequence of apply, menu change and revert (property test). Every end event of section 8.4 behaves as listed. Board: G-REU0 with title files On and no file; a hand-written Expand `.cfg` across the disk swap; a fixed-speed game with `Turbo Control=Off` while the saved speed is 48 MHz; hotkeys during it leave `settings.bin` unchanged; a CRT with `RAM Expansion Unit=Disabled` while the stored REU is on; power-cycle hash check |
| G-SCPU | `$D0BC` = `$FF` with Detect Off in every mode and with Turbo Off; `$01` with Detect On in Manual, Registers and TurboEnable at all 16 indices, on native and fast paths, during the reset window and every forced-exact cause; `$D03C`, `$D07C`, `$D0BD`, `$D1BC` read the VIC; REU DMA sees the VIC; a toggle leaves the live speed and apply counter unchanged; the debug `:speed` override never exposes `$D0BC` while Turbo Control is Off; the detection idioms (GNW64, GDOS64, GEOS patch, Ozmoo, CCGMS, cc65) give the expected branches in simulation; routed PAL within the ceilings; board: own probe, GNW64 (if no licence allows a local GNW64 test, the simulated detection-idiom check stands in and the receipt says so), cc65 `detect_scpu()`, GDOS64 and GEOS boots with Detect Disabled equal to the previous release, a Registers-mode toggle test, and the `.cfg` cases including a D64 source and a PRG source each ended with "Revert title settings" |
| G-NET | Section 9.9 |
| G-UCI | Register-block bench against a reference model and the upstream `command_protocol.vhd` as a pinned, test-only oracle; transport and wiring benches (decode sweep over UCI × REU × UA × cartridge type × DMA); G-STK unchanged with UCI Off, and a UCI-On trace variant that differs only at `$DF1B-$DF1F` (`1f 00 c9 00 00`); host tests of the engine and every target, including `/usb*/DMBCFG.CFG` create allowed and `GAME.CFG` beside `GAME.PRG` refused; REBOOT, FREEZE, MOUNT and UMOUNT run as posted actions; a `$50` REU-enable file under a generic CRT gives `89`; two `$50` files merge; routed timing and slices within the ceilings; board: own register suite at 1 MHz and 64x, G-REU0 with UCI On, DOS clients, the REU load under a running DMA loop, Network Off clients, a generic CRT and a cartridge with I/O. Network board tests gate the Network target only |
| G-SID | Legacy modes 0/2/3 bit-identical (legacy baseline bench identical before and after the `fpga64` edit); decode exhaustive on native and fast paths against a reference model; OSC3/ENV3 per SID; mixer saturation and headroom; the U64-oracle host test over every address, split and mirror combination; owned sub-page edits leave `c64_sid.bin` byte-identical; GET_HWINFO `$01` U64 shape and `$02` full list; board: G-REU0 with the Eight preset, SID Detector II with the UCI Off and On, a 4SID tracker tune, the Tuneful Eight with its own `.cfg` |
| G-FIT | Section 10.3 ceilings on the routed release build, PAL and NTSC |

Hardware support for each feature, target or preset is claimed only after its board tests pass.

---

## 15. Experiments to run first

| # | Experiment | Settles | Before |
|---|---|---|---|
| E-X1 | Analyse the dev7 PAL half-period path; OOC sweep of the island; first TD R2 steps | XM0 size and method; whether P2 is needed | XM0 |
| E-X2 | SID OOC1-OOC6 (section 10.4 stage A) | SID estimates; curve trim to one RAMB18 | XM6 stage B |
| E-X3 | W5100S datasheet items of section 9.1; Appletini bench run read-only | bridge timing, socket plan, rebind path | XM4 N2 |
| E-X4 | Synthesis of an 8/32-bit `xpm_memory_tdpram` with independent clocks | UCI buffer = one RAMB36 | XM5 |
| E-X5 | Bench: an REU DMA under the whole-core pause; does REU IDLE arrive under HOLD | the HOLD approach, or the cache-RTL alternative | XM5 REU commands |
| E-X6 | Bench: `C64_EXPANSION_CONFIG` bits 3-4 toggled during UA DMA | live `$50` expansion changes | XM5 `$50` |
| E-X7 | Bench: REU/UA enables set while `cart_loaded` = 1, then the detach | the PRG-after-CRT commit order of XM2 | XM2 |
| E-X8 | Measure the `service_runtime()` pass time on dev7 firmware (UART timer statistics, no HDL) | UCI pickup latency target | XM5 |
| E-X9 | U64/C64U measurement of `$D0BC` (value, mirrors, DMA, toggle effect) when a unit is available | P26 evidence only | unscheduled |
| E-X10 | Read the UBoot64 CRT header (hardware type, EXROM/GAME) | whether UBoot64 runs with the UCI (generic CRT rule) | XM5 board tests |
| E-X11 | Fetch the 1541ultimate PR #98 diff read-only | listener reply bytes (P48) | XM5 code freeze |
| E-X12 | Real full U64 "Save to File" dump size | the 16 KiB cap | XM2 |
| E-X13 | SD driver access to the card CID; PS-XADC reads | the MAC seed | XM4 N6 |
| E-X14 | Schematic check of RST# and INT# pull-ups | bridge reset behaviour during PL reload | XM4 N2 |
| E-X15 | GP0 memory attributes in the BSP translation table | write ordering before an op (C14) | XM5 |
| E-X16 | VICE POT read with no device (side finding: Multitini ties POT inputs so POTX/POTY read `$00` while CIA1 PA6/PA7 is set [V adapter.sv:768; fpga64_sid_iec.vhd:993-996]) | a separate small fix, not part of this plan | any |

---

## 16. User decisions

Recommended defaults first. The feature designs give the full reasoning.

| # | Decision | Recommended default | Reason |
|---|---|---|---|
| P1 | ABI | one step, 0.26, at XM3 | one reviewed spec edit; caps keep partial images safe; 0.25 is shipped |
| P2 | Timing policy | XM0 recovers ≥ +0.100 ns before XM3 | XM3 and XM7 add loads to the path already at +0.030 ns. Alternative: per-build waiver if WNS > 0 and no worse than the previous release, recorded in each README |
| P3 | Fit ceilings | BRAM ≤ 136; WNS, WHS > 0 everywhere; fast island ≥ +0.100 ns; slices ≤ 95 % | margin for later work; XM6 reports whether 4 SIDs can meet 95 % |
| P4 | Milestone order | XM0 → XM8 as in 12.1 | section 12.2 |
| P5 | W5100S bridge images | C64 image only for now | thin shell; no re-timing of other images |
| P6 | HTTP | later (XM8) | no TLS; size; needs XM5 first |
| P7 | IO1/IO2 SID slots | later (XM8) under B5 | priority and exclusion work; no client needs them first |
| P8 | UCI C64 IRQ | deferred (caps bit 14 = 0) | needs turbo-critical island decode |
| P9 | `$D038/$D036` UCI unlock | left out | the setting is the user's consent for file and network access |
| P10 | "Title .cfg files" default | On | no file, no effect; the U64 has no switch |
| P11 | Multi-SID settings scope | per profile | the legacy SID mode is per profile today |
| P12 | Editing an owned field | edits the overlay, no save | one game's tweak must not change every game |
| P13 | `.cfg` on a plain drive 8 mount | no, status hint only | a mount does not reset |
| P14 | `.cfg` on the boot auto-mount and D64 restore | no | mount only |
| P15 | `.cfg` on the stored CRT restore | yes, relaunch with apply = false | a CRT whose file turns the REU off could not be restored otherwise |
| P16 | Options → Reset | PRG overlay ends; D64 overlay kept while drive 8 holds a disk; CRT re-applied | the usual crash recovery `LOAD"*",8,1` runs the same title |
| P17 | C64 software reset, UCI REBOOT | overlay kept | not firmware events; U64 parity |
| P18 | "Save settings for this title" | yes, XM2d, merge writer, argument 19 | keeps U64 lines and requests this image cannot meet |
| P19 | `.usr` fallback for PRG titles | yes, with the text check | U64 parity |
| P20 | Title files may enable the Command Interface | yes, named in the status line | Heartbeat and Doom need it; the network stays a board setting |
| P21 | C64 writes to title sidecars (UCI DOS, drive 9) | refused for real sidecars only (stem matches a `.prg`, `.crt` or `.d64` in the same folder) | sidecars auto-apply; client config files such as `DMBCFG.CFG` stay writable. Alternative: allow, and name the file that turned the UCI on |
| P22 | `System Mode` mismatch in a `.cfg` | notice only; never switch the image | a reload takes seconds |
| P23 | `REU Size` other than 16 MB; `GeoRAM Mode` | 16 MB with a notice; GeoRAM line skipped | no size register; no GeoRAM |
| P24 | `.cfg` files in the media browsers | hidden | not launchable; `:cfg test` serves authors |
| P25 | Later or never | ROM keys never; REU preload key later; a "Run disk" action later; folder default files no; `<name>.<ext>.cfg` no | content rules and scope |
| P26 | `$D0BC` value | `$01` | passes every idiom found, including GNW64's `cmp #$01` |
| P27 | Detect visibility | Manual, Registers and TurboEnable, all indices, through forced exact | fixes #654; probes run at start, often in the reset window |
| P28 | `$D0B8/$D0B9` | (a) read-only in the register modes, not gated on Detect, `$C1` at live index 0 else `$01`; only if XM0 leaves margin for the extra compare term. (b) a documented limitation | MegaPatch ScrSaver64 and the SuperMouse64 poll drop register-mode GEOS to 1 MHz while `$D0B8` reads `$FF` (U64 #666) |
| P29 | Row label | `SuperCPU Detect (D0BC)` | matches U64 menus and `.cfg` files |
| P30 | U64 files with `SuperCPU Detect (D0BC)=Enabled` in a register mode or at 1 MHz | honour, and always say so in the status line | the file's text and the #654 fix; alternative: ignore such lines |
| P31 | SID engine | XM6 decides; 4 SIDs (E1) first; 8 by E1 if it fits, else the E3 hybrid; reject the shared-filter engine (E2) | section 10.2 |
| P32 | Runtime 6581/8580 per SID | yes, if the fit passes | Tuneful Eight and Heartbeat set 8580 |
| P33 | Uncovered SID slots | reads go to the lowest mapped SID; writes inert | U64 parity for writes; fixes #733 |
| P34 | Emulate enabled U64 sockets on spare SIDs | yes, when a spare SID exists | U64 Elite files with real chips otherwise play wrong |
| P35 | Mixer defaults | headroom Auto; DC removal Auto (3+ SIDs, map mode only); resampling Point | 1-2 SID output stays identical |
| P36 | Triple and Quad addresses | `$D400/$D420/$D440` and `$D400/$D420/$D440/$D460` | contiguous; Quad is U64-expressible |
| P37 | GET_HWINFO (`$04 $28`) | `$01` in the U64 shape: SID 1 and SID 2 always, then emulated sockets, at most 4 frames, configured base address, `$0000`/`$80` when unmapped; legacy modes 0/2/3 in XM5. Every SID on a new Multitini sub-command `$02` in XM7 | SID Detector II reads frames 1-2 at fixed offsets and sizes its buffer for 4 frames [V 1541ultimate control_target.cc:418-468] |
| P38 | `[Multitini SID]` section name | as named | U64 sections untouched |
| P39 | POT tie | separate small change after the VICE check | independent of multi-SID |
| P40 | XM5 content | DOS 1/2, Control subset, Network on XM4, SoftIEC "not loaded", HTTP empty | section 12.1 |
| P41 | UCI with a cartridge | active with a generic 8K/16K/Ultimax CRT only | UBoot64 ships as a `.crt`; type 0 never decodes I/O. Alternative: suspended with every CRT |
| P42 | UCI DOS root | package `content_root`, also below the active package directory | same files as the browsers; system files out of reach |
| P43 | Aliases and wildcards | `/SD`, `/Usb0-2`, `/Temp`, `*` and `?`; `/Temp` volatile, outside the media folder | U64 software hard-codes them |
| P44 | Home directory | folder of the title launched last this session, else the root | answers U64 #724 |
| P45 | SET_TIME | allowed, validated (2000-2099) | UBoot64 NTP sync needs it |
| P46 | SET_IPADDR | runtime only; kept across C64 resets | every PRG load is a C64 reset |
| P47 | Product name | `MULTITINI ONE` | honest |
| P48 | TCP listener `$12-$15` | XM5 if PR #98 confirms the replies, else XM8 | u-echoserver needs it |
| P49 | RAM bytes at `$DF18-$DF1A` | no | would change REU-visible reads |
| P50 | Control `$08/$09` source | the REU image last loaded from the menu this session (overlay preload keys later) | no stored preload setting |
| P51 | FREEZE (`$04 $05`) | opens our menu | DolphinDOS2 uses it |
| P52 | Idle READ_SOCKET on a live socket | keep the U64 `FF FF` + `02,NO DATA: 11` | xlar54 clients read 0 as end of file |
| P53 | `network.ini` name and default | as named; Off until G-NET passes, then DHCP | not a `.cfg`; no network traffic until tested |
| P54 | MAC source | generated once from SD CID, timer and XADC, stored | no new clock rule. Alternative: device DNA |
| P55 | FTP SD service | not planned; possible later option, default Off | not requested |
| P56 | Status text buffer | keep 63 characters; one text per launch, names shortened to 20 characters | no change to a shared buffer. Alternative: raise `supervisor_status_text` to 123 bytes in XM2 so longer texts and appended cases fit the drawn 122 columns |

---

## 17. Open items [O]

1. XM0 method and result (E-X1).
2. Every resource figure of section 10 until XM6 runs; slice packing above 3 LUT per slice.
3. W5100S datasheet items (E-X3); board pull-ups (E-X14).
4. UCI buffer mapping (E-X4); `c64_clk` slack after the UCI `io_data` mux input.
5. REU DMA under HOLD (E-X5); expansion toggles during UA DMA (E-X6); REU enable under a loaded cartridge (E-X7).
6. Superloop pickup latency, typical and worst case (E-X8).
7. U64 `$D0BC` details (E-X9); real SuperCPU `$D0BC` after boot.
8. UBoot64 CRT type (E-X10); PR #98 reply bytes (E-X11).
9. GP0 memory type for write ordering (E-X15).
10. U64 errno values for a refused connect and an unreachable UDP peer.
11. U64 read priority with overlapping SIDs, unmapped read value, POT at split parts; U64 internal summing of four parts (loudness reference for Auto headroom).
12. Peak levels of real multi-SID tunes through the mixer.
13. Whether the C64 restarts on a CRT detach (the hooked detach holds the reset either way).
14. Whether C64U firmware accepts the `U64 Turbo Registers` label in files we write.

---

## 18. Superseded statements

"Note added" means a one-sentence note "Superseded 2026-10-24 by docs/C64_U64_EXTENSIONS_PLAN.md: ..." was added at that line on 2026-10-24. "At XMn" means the statement is still true of the shipped code and is updated when that milestone lands.

| Doc:line | Old statement | New rule | Action |
|---|---|---|---|
| TD:13 | "all SuperCPU detection removed" | SuperCPU Detect at `$D0BC` planned for XM3 (Q21) | note added |
| TD:76 | "there is no SuperCPU detect register ... (Q11)" | `$D0BC` decoded behind SuperCPU Detect | note added |
| TD:171 | Q10: no per-title sidecar `.cfg` and no "Save ... for this title" | reversed for same-name `.cfg` files and the save action (Q22); the M7 part stands | note added |
| TD:172 | Q11: no `$D0BC`, no option, no bits | reversed for `$D0BC` only (Q21); `$D0B8/$D0B9` per P28 | note added |
| TD:221 | "No SuperCPU register is decoded in any mode (Q11)" | `$D0BC` reads `$01` with Detect On and mode ≠ 0 | note added |
| TD:258 | "No SuperCPU registers ... There is no SuperCPU Detect option" | option planned, default Disabled | note added |
| TD:264 | "Multitini has no SCPU Detect setting" | setting planned; still not an apply event (D21) | note added |
| TD:460 | D10 removed: "No SuperCPU register is decoded" | D10 stays unused; SuperCPU Detect deviations are D20-D21 | note added |
| TD:1726 | "Offsets 0x0D8-0x13C are free today" | 0x114-0x124 drive 9 (0.25); 0x128-0x138 UCI (0.26) | note added |
| TD:1746 | 0x114-0x13C "free" | same | note added |
| TD:1769 | CONFIG [31:20] reserved | bit 20 `SCPU_DETECT` (0.26) | note added |
| TD:1835 | CAPS2 [30:15] reserved | bit 15 `SCPU_DETECT` (0.26) | note added |
| TD:1917 | 12 rows = `MT_MENU_MAX_CORE_OPTIONS` | limit 20 (XM1); arguments 14-19 | note added |
| TD:1920 | "Removed (Q10, Q11): no per-title settings action and no SuperCPU row" | rows 14, 17, 18, 19 planned | note added |
| TD:1969 | byte 26 bits 3-7 reserved | bits 3-5 assigned; 6-7 stay reserved for the UA level row | note added |
| TD:1989 | "No per-title settings (Q10): no sidecar `.cfg` is read" | same-name `.cfg` files planned (XM2) | note added |
| TD:2441 | M8: "Its SuperCPU content (`$D0BC`) is removed (Q11)" | `$D0BC` moves to XM3 of this plan | note added |
| TD:2530 | Q9-Q12 listed as closed | Q10 and Q11 reversed in part (Q21, Q22) | note added |
| TD:2897, TD:2898 | v3 change rows for Q10, Q11 | the reversals | note added |
| TD:277, TD:2284 | index 0 with badline timing is bit-identical to Off | qualifier "with SuperCPU Detect Disabled"; G-LOCK runs with bit 20 = 0 | at XM3 |
| FVIC:446 | "`$D0BC` stays deferred (baseline D10)" | `$D0BC` per Q21, XM3 | note added |
| SPEC:79 | "Multitini: $00 when enabled. Do not implement it in the first release." | `$01` when enabled (P26), XM3 | note added |
| SPEC:180 | `$D0BC` value "$00, feature deferred" | same | note added |
| ISS:256 | the apply counter counts SCPU Detect changes | SuperCPU Detect is never an apply event (TD:264, D21) | note added |
| ISS:432, ISS:439 | "no UCI" | UCI planned (XM5) | notes added |
| PRIOR:349 | "Do not expose SuperCPU detect ($D0BC)" | reversed by the user (Q21), default Disabled | note added |
| UAD:403 | a future `$DF00/$DE00` SID must be exclusive with UA | exclusive with REU, UA, UCI and cartridges (B5) | note added |
| UAD:840 | "loaded through UCI, which Multitini lacks" | UCI planned (XM5) | note added |
| UAD:894 | "if UCI ever exists" | same | note added |
| docs/BOARD_FACTS.md | no W5100S entry | W5100S bullet | bullet added |
| EXP:16-19 | SID menu offers Single, Dual `$D420`, Dual `$D500` | multi-SID presets | at XM7 |
| EXP:30-34 | REU and CRT exclusive "pending a simultaneous IO2 mapping policy" | section 6.3 is that policy; REU and CRT stay exclusive; the UCI stays active with a generic CRT | at XM5 |
| `MULTITINI_ONE_PLAN.md`:16-19 | board list has no Ethernet | add the W5100S | at XM4 |
| docs/ARCHITECTURE.md:94, docs/VIVADO_HANDOFF.md:319, docs/FIRST_HARDWARE_TEST.md:58 | Ethernet pins idle | idle except in images with the bridge | at XM4 |
| `spec/core_interface_v0.md` | no sections for 0.24, 0.25 | add them with the 0.26 section | at XM3 |

---

## 19. Conflicts resolved between the feature drafts

| # | Conflict | Resolution here |
|---|---|---|
| 1 | All drafts sized against dev6 (+0.143 ns, 92.8 % slices, 295 slices of headroom) and said dev7 had no build | rebased on `4D00C707` (section 2.2); XM0 or the P2 waiver; 116 slices of headroom; slice column in 10.2 |
| 2 | Two GET_HWINFO `$01` specifications (one frame per owning SID, sorted, mode 0 = 1 frame; versus the U64 shape) | the U64 shape (P37), because upstream always sends both UltiSID frames first and SID Detector II reads them at fixed offsets [V control_target.cc:418-468]; the legacy part ships in XM5 |
| 3 | Sidecar write protection would refuse UBoot64's and DMBoot's `/usb*/DMBCFG.CFG` | protect only real sidecars (P21); host cases for both directions; G-IEC9 re-run |
| 4 | SuperCPU advice "use Options → Reset" fails for a D64 source, whose overlay survives that reset | advise "Revert title settings" (section 8.4); board case runs once with a D64 and once with a PRG source |
| 5 | REBOOT was "Options → Reset" in one draft and "a C64 software reset" in another | one definition (section 7.3) |
| 6 | The SID overlay needed field-level ownership; group-level merge would let a second `$50` file drop the first file's addresses; `SID_PARAMS` undefined; sub-page edits always saved | per-field SID mask with key-class bits (section 8.3) |
| 7 | The multi-SID fast-read decode loads T65 A5-A9, the half-period path; only full-period logic depth was counted | C17 with measurement in XM6 and a replicated-address mitigation planned first |
| 8 | The network milestone had no design at the depth of the other features (socket 0 roles, MAC entropy, XDC, tests) | section 9 |
| 9 | The frame lacked allocations the drafts added | section 3: STATUS2 at 0x134, RAM_READ at 0x138, the epoch tag, HOLD in XM5, `HANDSHAKE_BUSY`, final mailbox layouts and RESET_VALUE, argument 19, decision numbers Q21-Q25 and deviation ranges |
| 10 | `/Temp` mapped to two different folders | `<package directory>/uci_temp`, emptied at core start (section 7.4) |
| 11 | `[Multitini SID]` keys would have been skipped as an unknown section | added to the dialect (section 8.6) |
| 12 | Live `$50` expansion changes relied on a menu pause | applied under HOLD (section 8.5) |
| 13 | A `$50` file could enable the REU under a generic CRT, which the menu refuses | B9 |
| 14 | The byte 26 test step at XM2 would have asserted bits that have no field until XM3 and XM5 | section 4.3 |
| 15 | One status line, three claimants, no precedence | section 5.4 |
| 16 | "Update only the hashes" of the stock-trace manifest | compare mode only; the manifest is never edited (sections 6.5, 13) |
| 17 | `controls_i` "102 → 104 bits" predated dev7 | 120 → 136 (section 3.5) |
| 18 | UCI buffer "+0.5 tile (RAMB18)" | +1 tile (section 10.1) |
| 19 | `.cfg` parse "≤ 4 KB" | 16 KiB cap (section 8.6) |
| 20 | "An idle read returns length 0, not FFFF" | U64 `FF FF` kept (P52) |
| 21 | "Every slot has an owner; a write can never be lost" | every slot has a read owner; uncovered writes are inert (section 3.4) |
| 22 | UCI suspended under every cartridge | active with a generic CRT (P41, section 6.3) |
| 23 | `$DF1B` = 9 whenever drive 9 is enabled | only once the SoftIEC target exists (section 4.1) |
| 24 | UCI LOAD_REU "reuses the REU host path: pause, UA hold" | no menu pause, no UA hold; HOLD only around drain and invalidate (section 7.4) |
| 25 | `$50` "replaces" the overlay | merges; title launches replace (section 8.2) |
| 26 | Overlay ends at drive 8 eject, at every Options → Reset, and is not re-applied on the CRT restore | section 8.4 (P15, P16) |
| 27 | G-CFG "No `.cfg` → byte-identical behaviour" | byte-identical MMIO writes and `settings.bin`, with the Cartridge-row UI change listed (section 14) |
| 28 | G-TURBO cited T-S1c as an existing bench | it is only planned; a stand-in is named (section 13) |
| 29 | REBOOT "does not remount drive 8", but every reset through `set_core_reset_request()` flushes and remounts the same image (title-config F-m, UCI U16) | REBOOT keeps the drive 8 image and does not restore the stored name (section 7.3) |
| 30 | The `$50` status list had no code for a HOLD that does not reach paused and REU idle | `87,INTERNAL ERROR`, nothing applied (section 8.5) |
| 31 | The section 9.3 XDC comment held CS# and write data through recovery; the ported Appletini sequence does not (UCI FI-1) | comment corrected to the ported sequence; the one-cycle data hold stays an open proposal (FI-1, E-X3) |
| 32 | T-NET7 named `tests/host/test_core_frontend.c` (UCI FI-4) | `tests/host/test_system_frontend.c` (section 9.9) |
| 33 | G-SCPU required a GNW64 board run with no licence fallback (SCPU B2, X9) | the simulated detection-idiom check stands in, recorded in the receipt (section 14) |
| 34 | The 2-SID `sid_top` figure was the dev6 measurement in a dev7 budget | dev7: 2,629 LUT (sections 2.2, 10.4) |
