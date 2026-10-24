# Multitini One C64 turbo: a CPU island matching the U64/C64U turbo model (final design, v3)

This is a design-stage document. No HDL, firmware, script or build file was changed for it, and no build, synthesis or simulation was run.

v3 applies the user decisions of 2026-10-23 (section 0.6, Q6-Q16) and the items the user adopted from the U64/C64U issue sweep (C64_TURBO_U64_ISSUES.md). The main changes:
- indices 14 and 15 deliver 48x and 64x; a firmware-only bit restores the C64U's 47x and 63x;
- disk auto-slow stays On. The design now calls it a stricter form of the U64's own IEC slowdown, labels Off as risky, reports why it blocks, and makes its holdoff programmable; a static-hold release is designed but ships disabled;
- forced exact mode overrides `$D031` bit 7;
- a TurboEnable apply no longer raises a zero live speed, and applies happen only on real value changes;
- the menu shows requested and effective speed with the reason; the speed rows stay editable while Off; three bindable turbo actions and a one-step Turbo on/off row;
- an NMI edge hold and rearm (section 14.4), an unmapped-address rule and a turbo timing revision in the caps;
- an opt-in "Expansion memory speed: Follow CPU" REU milestone (M9) that leaves the stock REU untouched;
- all SuperCPU detection removed; `$D07A`/`$D07B`/`$D079` stay as U64 turbo switch registers;
- the Ultimate Audio sampler's I/O2 window and menu argument aligned with docs/C64_ULTIMATE_AUDIO_DESIGN.md. Ultimate Audio ships in the same release, and one ABI step, 0.24, covers turbo, Ultimate Audio, REU image preload and the debug registers;
- N0 (a stock NTSC build and board test) is the first implementation step; NTSC ships only when it closes at 64x (listed release timing fallbacks allowed), and F6 is a diagnostic build only;
- three host features in the first release: REU image preload (a user feature), C64 RAM readback and audio capture (debug features);
- every debug and diagnostic facility is off at power-on and can be enabled only from the UART console (`:debug on`); none appears in the menu;
- the reconciliation with docs/C64_ULTIMATE_AUDIO_DESIGN.md applies user decisions Q17-Q20 (one release and one ABI step; turning Ultimate Audio on turns the REU on; every debug facility of both features off by default and UART-only; the Expand gate runs with Ultimate Audio off) and design decisions DL1-DL6, which the user may override (section 0.6).

Section 15 lists every v3 change and where it lands. Section 14.2 lists the findings of the v3 review and how each was settled. Section 14.3 lists what the reconciliation with docs/C64_ULTIMATE_AUDIO_DESIGN.md changed. Section 14.4 lists the seven findings of an independent review (2026-10-23) and how each was fixed.

v2 applied the user's first five decisions (Q1-Q5). It replaced the v1 internal-I/O sketch with a full fast internal I/O path (section 2.13), added the NTSC image (section 13), and re-planned the milestones so the first release ships with U64-parity VIC/SID register timing and BA-low internal I/O. Its final revision resolved the 18 findings of three reviews (VIC/SID path, bus ordering, clocking/NTSC); section 14.1 lists them, and "review finding N" in the text means row N there.

v1, v2 and the pre-reconciliation draft of v3 (before the changes listed in section 14.3) are earlier drafts of this document and are not kept in the repository. Where this document compares with them, it states the earlier rule in place.

**Evidence documents.** The investigation reports and sub-designs are cited by name:
- [C64_TURBO_IIGS_LESSONS.md](C64_TURBO_IIGS_LESSONS.md), [C64_TURBO_FIRMWARE_PATH.md](C64_TURBO_FIRMWARE_PATH.md), [C64_TURBO_VERIFICATION.md](C64_TURBO_VERIFICATION.md), [C64_TURBO_BUS_CONTRACT.md](C64_TURBO_BUS_CONTRACT.md), [C64_TURBO_PRIOR_ART.md](C64_TURBO_PRIOR_ART.md), [C64_TURBO_T65_CORE.md](C64_TURBO_T65_CORE.md), [C64_TURBO_T65_TIMING.md](C64_TURBO_T65_TIMING.md): investigation reports.
- [C64_TURBO_U64_SPEC.md](C64_TURBO_U64_SPEC.md): what U64/C64U software sees (the "spec").
- [C64_TURBO_FAST_VIC.md](C64_TURBO_FAST_VIC.md), [C64_TURBO_FAST_SID.md](C64_TURBO_FAST_SID.md), [C64_TURBO_BA_LOW_IO.md](C64_TURBO_BA_LOW_IO.md), [C64_TURBO_NTSC.md](C64_TURBO_NTSC.md): sub-designs.
- [C64_TURBO_U64_ISSUES.md](C64_TURBO_U64_ISSUES.md): the U64/C64U issue sweep.
- docs/C64_ULTIMATE_AUDIO_DESIGN.md: the Ultimate Audio sampler (Q12), designed separately; it ships in the same release (Q17).

**Model scripts** are collected, with their outputs, in [C64_TURBO_MODELS.md](C64_TURBO_MODELS.md), and cited as "C64_TURBO_MODELS.md (<script>.py)":
- perf.py, final.py, rot.py: performance model.
- sim.py: reviewer's independent model.
- plan_ps.py: MMCM plans under Vivado's 1 ps rounding.
- vicread_model.py, vicpath_model.py: fast VIC read and loop costs.
- sid_fio_model.py: SID cost under the rejected non-posted variant (kept for the record).
- lead_model.py: costs of the merged fast I/O path (write barrier, fetch-ordered local writes, shared VIC/SID FIFO).
- final_model.py: the final barrier rules (side-effect reads, selective clears, φ2 forwards, the 26-cycle Doom `miPlay` loop).
- top_rows_model.py (new in v3): the final_model.py and final.py rules at 48 and 64 slots, with the C64U rows 47 and 63 for comparison. Every index 14 and 15 figure in sections 0.3, 2.13.4, 2.13.6, 7, 8 and 13.7 comes from it. It loads final_model.py and vicpath_model.py (and, through final_model.py, lead_model.py) from its own folder, so the four files travel together.
- rev_fastio_latch.py, rev_collision_alias.py: reviewer models that found findings 1 and 2 of section 14.1.
- balow_calc.py: BA-low exposure and #665 arithmetic.
- ntsc_plans.py: NTSC clock plan search with a ps-exact 2:1 test, and constants.
- Added with the section 14.4 fixes:
  - governor_credit.py: the forward-completion credit of section 2.7 against the old rule and the rejected own-grant rule. It loads perf.py and final.py.
  - nmi_rearm.py: the NMI hold and rearm of section 2.9 against no latch, the old latch and stock, and the turbo → exact carry against the wait it replaced.
  - vic_t2_sticky.py: the T2 sticky-state check (DUT, collapsed reference, per-event scoreboard, stock with separate-cycle writes; sections 2.13.6 (c), 10.2).
  - sid_edge_stage.py: literal model of the SID edge-stage pseudocode; the expected engine samples of bench (d) (sections 2.13.7, 10.2).
  - svf_persistence.py: how long a one-cycle filter difference lasts in the sid_filter.sv state-variable filter; backs bench (e) (section 10.2).
  - preload_cache.py: REU image preload against the PS data cache (section 6.4 (a)).

Tags:
- **[V]**: verified in the repository (file:line) or in a cited document.
- **[M]**: measured by a third party on U64/C64U hardware (sources listed in [C64_TURBO_U64_SPEC.md](C64_TURBO_U64_SPEC.md) §I and C64_TURBO_U64_ISSUES.md).
- **[I]**: inference or design choice.
- **[O]**: open. The experiment that settles it is named.

**Build reports.** Citations of `c705r2` and of `*.rpt` files name the local Vivado reports of the M0.7.0dev5 C64 image `4D00C705` (`build/c705r2/c64-pal/reports/`). `build/` is not under source control, so these figures cannot be checked from the repository; docs/C64_VICE310_COMPARISON.md:51 records the image's +0.133 ns setup slack. The combined build (section 10.3, step 4) reports every figure again.

Nothing here has been tested on the board, and no hardware support is claimed. The C64 NTSC image has never passed a board test.

Notation (used throughout): one native CPU cycle has 32 phases p = 0..31 (EXT0 = 0, EXT4 = 8, VIC0 = 12, VIC3 = 15, CPUC = 28, CPUF = 31) [V fpga64_sid_iec.vhd:196-202]. Fast clock f = 2p + h, so f = 0..63. e_p is the c64 rising edge ending phase p; it coincides with the fast rising edge ending f = 2p + 1. R = e_31, the native retire. "fpga64" means `hdl/core/c64/ports/fpga64_sid_iec.vhd`; "VIC" means `hdl/core/c64/ports/video_vicII_656x.vhd`; "buslogic" means `hdl/core/c64/multitini_c64_buslogic.vhd`. "u64_config.cc" means GideonZ/1541ultimate `software/u64/u64_config.cc` at commit d43546f. "#n" means issue or pull request n of GideonZ/1541ultimate. "UA" means Ultimate Audio.

---

## 0. Summary

### 0.1 What C64 software sees

**Registers** follow the U64E2/C64U model:
- `$D031`: bits 3-0 are the speed index. Bit 7 = 1 disables badline timing.
- `$D030`: bit 0 is TurboEnable.
- `$D07A`/`$D07B`/`$D079`: the U64 turbo switch registers (normal/turbo writes).
- All of them read `$FF` when they are not live.
- Nothing else is decoded: there is no SuperCPU detect register and no other SuperCPU register (Q11).

**Turbo Control** (menu) has four modes: Off, Manual, U64 Turbo Registers, TurboEnable Bit. The default is Off.

**Speed** index table: 1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64 MHz. Delivered CPU cycles per φ2 equal the label: exact (index 0), then 2 … 40, 48 and 64 (Q9). No code runs faster than the label at any index, including index 0 with badline timing disabled (section 2.7). The C64U delivers 47 and 63 at the top two indices [M #874]; a firmware-only bit restores those two rows (D17).

**Access classes at turbo (first release):**
- **1 slot:** RAM, BASIC/KERNAL/character ROM, `$00/$01` reads and writes (Q3), colour RAM writes. Colour RAM reads take 2 slots.
- **VIC and SID registers (Q1):** turbo rate. A read costs the same as RAM at indices 0-12 and up to about one extra slot at 13-15, as on the C64U [M]. A write costs 1 slot and lands 3-4 fast clocks later through an ordered queue.
- **One access per φ2, never posted:** CIA1/CIA2, I/O1/I/O2 (REU registers, a `$DE00`/`$DF00` SID, the Ultimate Audio registers at `$DF20-$DFFF` when that option is on), cartridge. This matches the U64.
- **Badline timing disabled (Q1):** the CPU, VIC/SID registers, CIA1/CIA2, REU registers, I/O1/I/O2 without a cartridge and the `$FF00` REU trigger all proceed while BA is low. Only cartridge accesses and RAM-bus fallback classes wait for BA = 1.
- **Forced exact mode wins (Q9):** while the reset window, disk auto-slow, a cartridge or the content loader forces exact mode, the CPU runs the stock path whatever `$D031` bit 7 says. The stored bit still reads back.
- **No fast or local path answers an unmapped address (Q9):** reads of empty I/O return the floating bus, as on stock.

**REU:** the CPU halts on the triggering write. DMA runs at 1 byte per φ2. Stock REU behaviour does not change in any mode (Q8). An opt-in "Expansion memory speed: Follow CPU" is planned after the first release (M9, D19).

**Ultimate Audio** (Q12, Q17, Q18; designed in docs/C64_ULTIMATE_AUDIO_DESIGN.md, shipped in the same release): when the option is on, `$DF20-$DFFF` belongs to the sampler and the REU keeps `$DF00-$DF1F`. Turning it on also turns the REU on, because it plays from REU memory; turning the REU off also turns it off; a loaded cartridge blocks both. Its registers are I/O2-class accesses under turbo: one access per φ2, as on the U64, never posted and never on the fast I/O port. With badline timing disabled they complete in BA-low cycles through I/O-only ownership.

**Reset:**
- The live register is cleared.
- Execution is exact for 2.0 s.
- Register writes made during that window are latched and applied when it ends.

**Menu and controls (Q9):** the menu shows requested and effective speed with the reason for any block; the speed rows stay editable while Turbo Control is Off; three bindable actions ("Turbo on/off", "CPU faster", "CPU slower") and a one-step Turbo on/off row (section 6.2).

**Host and debug features (Q15, Q16, Q19):** REU image preload is a menu feature (section 6.4). C64 RAM readback, audio capture and the other diagnostics are off at power-on, can be enabled only from the UART console with `:debug on`, and do not appear in the menu (section 6.5). The same rule covers Ultimate Audio's `:uastat` command and its debug status fields; one arming command, `:c64diag on`, arms the diagnostic counters of both features. C64 software cannot see or enable any of them.

**Images:** PAL and NTSC are separate images: variants `pal` and `ntsc` of the `c64` package (Q2, section 13). Software sees the same turbo behaviour on both, unless one image needs a release timing fallback (section 13.2); absolute times scale by 0.963 on NTSC. NTSC ships only when it closes at 64x; PAL may ship alone (Q14).

### 0.2 Architecture in seven lines

1. One MMCM gives `c64_clk` (its role is unchanged) and `c64_cpu_clk` = 2 × `c64_clk`: 63.056 MHz PAL and 65.455 MHz NTSC, exactly 64 fast clocks per native CPU cycle. Each standard is its own image with its own pinned plan.
2. The unedited `cpu_6510`/T65 moves into a CPU island on `c64_cpu_clk`. It does one 6502 bus cycle per fast clock against island BRAM ports on the inverted fast clock.
3. A native proxy in the adapter (`c64_clk`) presents φ2-class island requests to the unchanged phase-28/31 native slot. Turbo forwards load only at the VIC3 edge. They own the shared bus only while BA is high; with badline timing disabled, internal targets get an I/O-only ownership in BA-low cycles.
4. A fast I/O (FIO) port serves VIC and SID registers without the native slot: a 2-clock read from a combinational copy of the register read muxes, and a posted, ordered 4-entry write queue that lands one entry per c64 edge.
5. Stock ("exact") mode sends every cycle through the proxy. The T65 gets one enable per native retire, with the inputs sampled at the retire edge. A lockstep bench proves equivalence before any turbo work, with the fast-I/O hardware present and idle.
6. A grant table gives N of the 64 slots per φ2 to the CPU, all 64 at index 15. IRQ stays live at every enable. NMI is live too, except that a fall that arrives while the island has no enable is held, or re-armed when the T65's last sample was low, so it is taken once (section 2.9). Only IRQ-acknowledge accesses complete later.
7. The U64 register block, the reset window, the speed policy and the IEC guard sit in the c64 domain.

### 0.3 Expected speeds (model [I], not measured)

Source: C64_TURBO_MODELS.md (top_rows_model.py) for indices 14 and 15; the other models listed above otherwise.

| Workload | Expected |
|---|---|
| RAM/ROM loops | 64x at index 15, 48x at index 14 (63x and 47x with the C64U rows): the delivered cycle rate. The 300-iteration loop model gives 63.88x and 47.95x, because it counts the start-up slot. About 60.5x PAL / 60.0x NTSC at index 15 with badline timing enabled and the screen on |
| 1% of cycles to φ2 I/O (CIA, I/O1/I/O2) | about 38.6x |
| 5% to φ2 I/O | about 15.8x |
| `sta $de00,x / dex / bne` (I/O1) | 2.0 µs per iteration. C64U: 1920 ns per `sta $de00,x` [M]. **Matches** |
| Unrolled `lda $d012` / `lda $d419` / `lda $d41a` | 0.078 φ2 per read at index 15 (0.079 with the C64U row); equal to RAM at indices 0-12. C64U: 0.080 / 0.085 / 0.080 [M]. **Matches within one slot**. The SID read's latch entry is outside the read barrier (section 2.13.4) |
| `sta $d020,x / dex / bne` (VIC) | 0.172 φ2 (0.17 µs PAL) per iteration at index 15. The φ2 fallback build takes 2.0 µs |
| Doom `miPlay` 25-register SID burst (Doom C64U `src/music.asm`, 26 cycles per register) | about 10.7 µs PAL at index 15 (about 51 µs in the φ2 fallback build) |
| `sta $0400,x / sta $d800,x / dex / bne` | about 60x at index 15 (the colour dummy read costs 2 slots) |
| `$8F` timer digi, screen on | no badline-correlated stall of the CIA acknowledge, so no #665 growl expected (gate G-BA1) |
| Raster- or CIA-paced games | no change |

### 0.4 What "64x+" means here

The island has 64 single-cycle slots per native φ2. Index 15 grants all 64 (Q9). The C64U grants 63 and keeps one slot per φ2 for its VIC [M #874]. Multitini's VIC fetches through its own RAM port and never uses island slots (C64_TURBO_FAST_VIC.md §3.11), so the 64th slot is free. A firmware-only bit restores the C64U's 63 (D17).

Going beyond 64x is rejected (section 7):
- it gains only 5-17% in 0.5-5% I/O mixes;
- there is no timing margin for it in the full image;
- the U64 register interface has no index above 64 MHz.

### 0.5 Biggest risks

1. **[O] Clock model.** Vivado may not time the pair as exactly 2:1, because each clock is rounded to 1 ps: PAL gives 31.715 vs 2 × 15.857 ns; every exact NTSC plan sits on a half-picosecond tie (30,552.5 ps). E2 and E2-NTSC decide this, and a fix ladder exists (sections 3.1, 13.3).
2. **[O] Island closure** in the full image, worst at NTSC 65.45 MHz (half period 7.64 ns), once MMCM duty-cycle precision is included. The FIO read mux adds about 136 loads to T65 address bits A0/A1, which also drive the critical half-period path. E1 and M1 decide this. NTSC has its own fallback ladder, whose release fallbacks (`VIC_READ_GAP` = 3, F7) may ship; if NTSC still misses 64x at its end, NTSC is held back and PAL ships alone (Q14). F6 (32x) exists only as a diagnostic build.
3. **[O] Fast I/O correctness.** The VIC landing rule (holds H1, H2) must leave the VIC's control state stock-equivalent; the queue and barriers must keep program order (every barrier flag is registered from the post-push state, section 2.13.2); the I/O-only ownership edits upstream-derived buslogic on the stock path; the exact→turbo seam must not re-run the last exact access (section 2.6). T2 (with negative controls), the ordering fuzz, index-based assertions and the M2 lockstep decide this. Every piece has a fallback generic for diagnosis; shipping one would bring back D1 or D5, so it needs a user decision (section 2.13.2).
4. **[O] IRQ-acknowledge timing.** The phases are derived from reading the RTL, not simulated. E4 and T7 decide this.
5. **[O] NTSC board baseline.** No NTSC C64 image has ever passed on the board, and HP2 at the NTSC clock has never been in a full image. N0 (a stock NTSC build and board test) decides this. It is the first implementation step (Q13).
6. **[O] Host features and coherence.** REU image preload writes the REU's DDR memory from the PS and must invalidate the REU cache; RAM readback reads C64 RAM through the loader port. Both are safe only under the pause and quiescence rules of section 6.4; Ultimate Audio is held, with an acknowledgement, during the copy. Host tests, the cache-invalidate bench, the Doom and Expand gates and X-PRELOAD (section 10.3) decide this.
7. **[O] Combined image placement.** Turbo and Ultimate Audio ship in one C64 image (Q17). The island, the UA logic and its HP3 port share placement with the image's limiter, the 148.5 MHz video clock (+0.133 ns in c705r2), and with `control_clk` (+0.446 ns). M1 is built with Ultimate Audio placed once its RTL exists (section 10.3). A miss outside the island goes to the existing repair flow (R27, R34).

### 0.6 User decisions (binding) and the project rules they replace

| # | Decision | Documented rule replaced | Where applied here |
|---|---|---|---|
| Q1 | VIC and SID register accesses may bypass native slots and run at turbo rate. With badline timing disabled, internal I/O (CIA, SID, VIC registers, REU registers) may proceed while BA is low. D1 and D5 disappear from what ships | `docs/C64_RAM_CPU.md:68-69`: "I/O accesses use native slots. VIC bus arbitration and NMOS RDY behavior remain authoritative." [V]. New text: in turbo, VIC/SID registers use the fast I/O port; φ2 targets use the native slot; with badline timing disabled, internal φ2 targets may complete while BA is low without the shared address bus; VIC arbitration of RAM, colour RAM and character ROM stays authoritative in every mode; stock mode keeps NMOS RDY behaviour | 0.1, 1.6, 1.7, 1.12, 2.5, 2.8, 2.13, 5, 8, 10 (M4, M5), 12 |
| Q2 | NTSC C64 is in scope | `MULTITINI_ONE_PLAN.md:6` image set lists "C64 PAL" only [V]; `docs/CPU1_VIDEO_EFFECTS.md:376-379`: NTSC "is no longer a release requirement" [V]. New: C64 NTSC is a release image (variant `ntsc` of package `c64`) with its own go/no-go | 0.1, 3.1, 6, 9, 10, 12, 13 |
| Q3 | `$00/$01` writes run locally at turbo speed. `FORWARD_PORT_WRITES` stays only as a build fallback | `docs/C64_RAM_CPU.md:66-68`: "Accesses to the 6510's `$0000/$0001` processor port remain in native slots, completing the port-register update before another accelerated request." [V] (the Smart 2x rule) | 1.7, 1.12 (D11), 2.5, 5.4, 12 |
| Q4 | Disk auto-slow default stays On. The user's "yes" was read as accepting the design default. **Superseded by Q6**, which states it explicitly | none; D3 stays a permanent, documented option | 1.4, 1.9, 1.12, 6 |
| Q5 | Turbo detail is stored in global settings bytes 26-27, shared across C64 packages (and across the `pal`/`ntsc` variants) | none. Bytes 26-27 are zero-filled and not written today (`supervisor_settings.c:573-578` writes 24, 25 and 28+) [V] | 6.3 |

These decisions also close the earlier drafts' open questions on the same topics.

**Decisions and rules recorded in v3.** "User list" is the number in the user's list of 2026-10-23; its item 1 restates Q1-Q5.

| # | Date | User list | Decision | Documented rule replaced | Where applied here |
|---|---|---|---|---|---|
| Q6 | 2026-10-23 | 2 | Disk auto-slow default **On**. The user first chose Off "like the U64", then reverted to On after learning that U64 users hit IEC failures at turbo. The menu option stays | Q4's reading; the earlier draft's claim that disk auto-slow Off gives U64 parity, which was wrong (section 1.9) | 0.1, 1.4, 1.9, 1.12 D3, 6.2, 11 |
| Q7 | standing rule | 3 | "If users are opening issues for U64 that really matter then we should implement the solution and not blindly follow U64." | none | The sweep (C64_TURBO_U64_ISSUES.md) applies it; Q9 lists what the user adopted. DL6 (`EDGE_STAGE` default 1; formerly question 9, Q-S1) follows the sweep's recommendation under this rule |
| Q8 | standing rule | 4 | The REU is fully correct at stock today (Expand by Bonzai runs flawlessly). Nothing may change stock REU behaviour. Expand disks 1 and 2 are a hard regression gate at stock and with turbo configured but forced exact | none | 0.1, 1.8, 6.4, gate G-REU0 (10.2: M2, M5, M9), 12 |
| Q9 | 2026-10-23 | 5 | Adopt from the sweep: (a) full 48x/64x at indices 14/15, with a hidden firmware-only bit restoring 47/63; (b) disk auto-slow corrections: a stricter form of the U64's own IEC slowdown, Off labelled as risky, block-cause status, a programmable holdoff with experiment E-IEC, a static-hold release designed but shipped disabled; (c) forced exact overrides `$D031` bit 7; (d) a TurboEnable apply raises live only if live ≠ 0, and the apply counter changes only on real value changes; (e) menu visibility: requested/effective speed and reason, sticky status bits, speed rows editable while Off; (f) supervisor actions "Turbo on/off", "CPU faster", "CPU slower" bindable in Controls, plus a one-step Turbo on/off menu row; (g) an opt-in "Expansion memory speed: Follow CPU" REU DMA as a post-first-release milestone (default stays 1 byte per φ2; stock path untouched); (h) an NMI edge latch; (i) no fast or local path answers an unmapped address; (j) a turbo timing revision in CAPS; (k) the sweep's gates for these items | Earlier draft: indices 14/15 delivered 47/63 and "Max 64: no"; disk auto-slow Off described as U64 parity; every TurboEnable apply raised live; the speed rows disabled while Off; D16 provisional (see the note below) | 0.1-0.4, 1.2-1.12, 2.5, 2.7, 2.9, 2.11, 2.13.4, 2.13.6, 3, 4.3, 5.8, 6, 7, 8, 9, 10, 11, 12 |
| Q10 | 2026-10-23 | 6 | Do not adopt from the sweep: per-title sidecar `.cfg` settings and "Save turbo settings for this title"; moving cartridge turbo (M7) earlier. M7 stays where v2 has it, cartridges force exact mode until then, and only the status display "(cartridge: 1 MHz)" is taken | none | 1.10, 6.2, 6.3, 10.2 |
| Q11 | 2026-10-23 | 7 | Disregard the SuperCPU: no `$D0BC` detect, no `$D0B8`/`$D0B9`, no SuperCPU Detect option and no config, caps or settings bits for it; deviation D10 and milestone M8's SuperCPU content are removed; no SuperCPU naming in the menu. **Interpretation [I]:** `$D07A`/`$D07B`/`$D079` stay, live in the register modes as on the U64, because U64-targeted software (Image BBS style) uses them; this document describes them as U64 turbo switch registers, not as SuperCPU support. Design decision DL4 keeps them; the user can still ask to drop them | Earlier draft: a `$D0BC` SuperCPU detect register with a SuperCPU Detect option (default Disabled), deviation D10, CONFIG bit 7, CAPS bit 19, settings byte 27 bit 4 and M8's `$D0BC` content | 0.1, 1.1, 1.2, 1.4, 1.5, 1.12, 6.1, 6.3, 10, 11 |
| Q12 | 2026-10-23 | 8 | The U64 "Ultimate Audio $DF20-$DFFF" sampler becomes an option in the C64 Options tab, and turning it on enables the REU. It is designed in docs/C64_ULTIMATE_AUDIO_DESIGN.md. In this design: when it is on, `$DF20-$DFFF` belongs to it (REU registers stay at `$DF00-$DF1F`; their mirrors above `$DF1F` disappear); its registers are I/O2-class accesses under turbo (one access per φ2, as on the U64), and its acknowledge writes join the IRQ-acknowledge class, as that design states; the turbo rows keep explicit Options-tab argument numbers (1, 4-7, 9, 10) and Ultimate Audio takes argument 8; the two features share one ABI step (0.24) and one offset map (section 6.0) | none | 0.1, 1.7, 1.8, 2.5, 2.13.3, 2.13.8, 5.8, 6.0, 6.2, 6.3, 10.2, 10.3, 11, 12, 14.3 |
| Q13 | 2026-10-23 | 9 (Q-N2) | N0 runs **first**, before any turbo work, as the first step of implementation: one Vivado build of today's tree as a stock NTSC image, packaged with the `pal` and `ntsc` variants, plus a board test. Nothing is built now | Earlier draft: "N0: M0 if Q-N2 allows, else M1" | 0.5, 10.1, 10.2, 11, 13.6 |
| Q14 | 2026-10-23 | 9 (Q-N1) | **Hold NTSC until it closes at 64x.** No NTSC image capped at 32x (F6) ships. PAL may ship alone. F6 stays only as a diagnostic or experiment build option | Earlier draft: an NTSC image that missed 64x shipped as F6 (32x cap, caps mask `0x1FFF`) with matching menu clamp text, as Q-N1 in C64_TURBO_NTSC.md recommended | 0.5, 1.3, 3.1, 4.3, 6.1, 6.2, 9 (R2, R26), 10, 11, 13 |
| Q15 | 2026-10-23 | 9 (host features) | All three host features are in the first release: (a) REU image preload (load a `.reu` file from SD into the REU's DDR memory with the REU cache invalidated; Doom C64U needs it); (b) C64 RAM readback over UART for automated board gates; (c) audio capture for automated audio gates (for example the #665 digi test). The strict M5 gates use them | Earlier draft: host features were an option with new firmware scope, left as an open question | 6.4, 6.5, 9 (R17, R28), 10.2, 12 |
| Q16 | 2026-10-23 | 10 | Debug features are **off by default**: audio capture, C64 RAM readback and every other debug or diagnostic facility (sticky diagnostic status bits used only for tests, trace and dump commands, the UART `:speed N` benchmark command, F6 and other diagnostic builds) are disabled at power-on and can be enabled only from the UART console. They do not appear in the normal menu. User-facing features stay in the menu; REU image preload is a user feature, not a debug feature. **Interpretation [I]:** a debug session is not persisted across power cycles or reboots; for diagnostic builds, "off by default" means the build and release scripts never produce or package them without an explicit build variable (section 6.5); the rule covers the facilities this design and docs/C64_ULTIMATE_AUDIO_DESIGN.md add (including that design's `:uastat` and observation counters), while existing console commands such as `:diag`, `:hid`, `:display` and `:c64rx` stay ungated (DL5). Q19 extends the rule to every debug facility of Ultimate Audio | Earlier draft: STATUS bits 24-25 were plain status, and a UART `:speed N` was optional and ungated | 1.4, 6.1, 6.2, 6.4, 6.5, 9 (R29), 10.2 (gate procedure), 12 |

**Decisions recorded in the reconciliation with docs/C64_ULTIMATE_AUDIO_DESIGN.md.** "Reconciliation list" is the number in the user's list of 2026-10-23 for that work. Its item 5 restates Q5 and adds DL1; its item 6 confirms Q1-Q16; its item 7 asked the design lead to decide DL2-DL6.

| # | Date | Reconciliation list | Decision | Documented rule replaced | Where applied here |
|---|---|---|---|---|---|
| Q17 | 2026-10-23 | 1 | Turbo and Ultimate Audio ship in one release with one ABI step: ABI minor 0.24 covers both, together with REU image preload and the debug registers. Neither feature ships alone. Order-independent rules stay only where they keep downgrades safe: each feature keeps its own ABI constant and capability bits, and reserved settings bits are ignored on decode | The pre-reconciliation draft's section 6.0: one ABI step per image that adds registers, "Ultimate Audio is expected to ship first and the turbo release to take 0.25", preload "can ride in whichever image ships first"; question 21 | header, 0.1, 0.5, 6.0, 6.3, 10, 10.3, 11, 12 |
| Q18 | 2026-10-23 | 2 | Turning the "Ultimate Audio $DF20-$DFFF" row on also turns the REU on, in the same apply, because UA plays from REU memory. Turning the REU off while UA is on also turns UA off, so UA is never on while the REU is off. A loaded cartridge blocks the REU and therefore UA ("eject any cartridge first"). Persistence: the UA flag is global (DL1) and the REU flag stays per profile; both are written in one save (section 6.3). docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2-6.4 owns the apply rules, the row states and the texts | The UA design draft: the row greyed with "Needs REU", "Off-to-On refused without the REU" and an "Inactive" state; the first coordination item of question 22 in the pre-reconciliation draft | 0.1, 1.4, 1.8, 1.10, 6.1, 6.2, 6.3 |
| Q19 | 2026-10-23 | 3 | Debug features of both features are off by default and enabled only over UART: audio capture, C64 RAM readback, dump and trace commands, diagnostic counters and status used only for tests (including Ultimate Audio's `:uastat`, `C64_UA_FETCHES`, `C64_REU_MISSES` and its coherence probe), the `:speed` benchmark command and diagnostic builds. They do nothing until a UART debug session enables them, are not in the menu and are not persisted across power cycles. Board gates enable debug over UART first. User features stay in the menu; REU image preload is a user feature | Q16's interpretation, which reached UA only through a coordination item (question 22 of the pre-reconciliation draft); the UA design draft: an ungated `:uastat` and free-running counters | 0.1, 1.4, 2.13.2, 3.2, 3.3, 3.4, 6.0, 6.1, 6.5, 9 (R29), 10.2, 10.3 |
| Q20 | 2026-10-23 | 4 | The REU is fully correct at stock today (Expand by Bonzai runs flawlessly), and nothing may change stock REU behaviour (restates Q8). Expand disks 1 and 2 are a hard regression gate with Ultimate Audio off and turbo Off or forced exact. With UA on, the REU register mirrors above `$DF1F` disappear, as on a U64; an Expand failure caused only by that is accepted and documented, not a regression | Q8's gate, which did not name the UA state; the UA design's separate gate G1 | 1.8, 10.2 (M2, M5), 10.3 (G-REU0) |

**Design decisions** (each recorded as "design decision, user may override"). The design lead decided these on 2026-10-23 to close open items.

| # | Decision | Closes | Where applied here |
|---|---|---|---|
| DL1 | The Ultimate Audio preference is one global flag, settings byte 26 bit 2, consistent with the approved global turbo bytes (Q5). The REU setting stays per profile. Older firmware ignores byte 26, so the flag is downgrade-safe | the UA design's open item O9 | 6.3 |
| DL2 | The bindings of the three turbo actions live in the companion file `0:/multitini/turbo_controls.bin`. Settings schema 10 stays, so a downgrade loses only the bindings | question 18 | 6.2, 6.3, 9 (R30) |
| DL3 | Q-BA1 is answered "no". Native-slot REU DMA keeps the VICE BA rules in every mode, and D16 is permanent for native-slot DMA. Only the opt-in Follow CPU bytes (M9) are exempt | question 25 | 0.6 (note below), 1.8, 1.12 (D16), 2.13.1 (CF7), 9 (R23) |
| DL4 | `$D07A`/`$D07B`/`$D079` stay as U64 turbo switch registers | question 19 | 0.6 (Q11), 1.2 |
| DL5 | A debug session survives a core switch and ends at reboot, golden update and power-off. The existing console commands (`:diag`, `:hid`, `:display`, `:c64rx`) stay ungated | questions 20 and 24 | 0.6 (Q16), 6.5 |
| DL6 | `EDGE_STAGE` defaults to 1 | question 9 (Q-S1) | 0.6 (Q7), 1.12 (D15), 2.13.2, 9 (R19) |

**IDs.** "Q" with a number (Q1-Q20) always names a user decision of this section, and DL1-DL6 name design decisions. Open questions of section 11 are written "question N" and keep their own numbers: question 9 is not Q9, and questions 18, 19 and 20 are not Q18-Q20 (questions 9, 18, 19 and 20 are closed by DL6, DL2, DL4 and DL5).

**Note on D16 and Q-BA1.** The sweep's item 8 bundles the Follow CPU option with two related rules: D16 (native-slot REU DMA honours badlines with badline timing disabled) becomes permanent, and Q-BA1 ("if U64/C64U REU DMA proceeds during badlines with badline timing disabled, should Multitini copy it?") is answered "no". The user's adoption list (Q9) names only the Follow CPU option. Design decision DL3 settles both: D16 is permanent for native-slot REU DMA, which keeps native REU DMA on the stock VICE rules in every mode, as Q8 and Q20 favour. The RAM-side bytes of the opt-in Follow CPU path (M9) are the one exception to D16.

---

## 1. Software-visible behaviour

### 1.1 Turbo Control modes and register liveness

Source: C64_TURBO_U64_SPEC.md §B; u64_config.cc:325-326, 387-394, 1625-1636 [V].

| Turbo Control (default **Off**) | Effective speed | `$D031` | `$D030` | `$D07A`/`$D07B`/`$D079` |
|---|---|---|---|---|
| Off | Stock, exact | reads `$FF`, writes ignored | `$FF` | ignored |
| Manual | Menu index and menu badline setting | reads `$FF`, writes ignored | `$FF` | ignored |
| U64 Turbo Registers | Live register | live, read/write | `$FF`, writes ignored | live |
| TurboEnable Bit | Live register | live, read/write | live, read/write | live |

- "Ignored" means the write still reaches the VIC, where it has no effect.
- FPGA64's C128 turbo path stays off, because `turbo_en` = 0: `turbo_mode` is tied to "00" (fpga64:1077; VIC:1756, 1821) [V].
- Liveness depends only on Turbo Control. Conditions that force exact mode never change liveness or readback (section 1.11).
- No SuperCPU register is decoded in any mode (Q11): `$D0BC`, `$D0B8`/`$D0B9` and the rest read the VIC's `$FF`.

### 1.2 Registers

**Decode** [I, C64_TURBO_U64_SPEC.md §C]:
- Native path (exact mode, and the φ2 fallback build): the decode matches the full 16-bit address of a completed CPU transaction (`cpu_bus_valid`, fpga64:1039), qualified by the VIC chip select. That gives exact A9-A0 inside `$D000-$D3FF`, with no mirrors.
- Fast path (turbo): writes are snooped when the queue lands them (section 2.13.5), with the same exact A9..A0 decode on VIC-target entries. Reads of `$D030`/`$D031` are overridden in the fast read mux (section 2.13.4).
- REU DMA never matches, because `cpu_bus_valid` excludes DMA and DMA never uses the queue.
- With I/O banked out, a write to `$D031` goes to RAM, as on a C64.

**`$D031`** (live in Registers and TurboEnable modes)

| Bits | Write | Read |
|---|---|---|
| 3-0 | live speed index | live index |
| 6-4 | ignored | 0 |
| 7 | 1 = badline timing **disabled**, 0 = enabled | stored bit 7 |

- Not live: reads `$FF` (from the VIC), and writes are ignored.
- Polarity is as software sees it [V turbo_mode.rst:84; M #665]. The firmware's internal preference register is inverted (u64_config.cc:1630 [V]). Multitini keeps that inversion inside its hardware.

**`$D030`** (live in TurboEnable mode only)

| Bit | Write | Read |
|---|---|---|
| 0 | 1: live := preference (menu index and menu badline setting). 0: live := `$00` | 1 when live ≠ `$00` (the doc's "off" is exactly 1 MHz with badlines on, turbo_mode.rst:122-132) |
| 7-1 | ignored | 1 |

- So the register reads `$FE` or `$FF`. This matches MiSTer's layout (VIC:1821).
- Consequence [V cc65 acc_detect_c128.s]: in TurboEnable mode with turbo off, cc65 `detect_c128()` sees `$FE` and reports a C128. Whether the U64 behaves the same is [O] (E7).

**`$D07A`, `$D07B`, `$D079`: the U64 turbo switch registers** (write-only; live in Registers and TurboEnable modes, as on the U64) [I, C64_TURBO_U64_SPEC.md §C]:
- A write to `$D07A` sets live := `$00`.
- A write to `$D07B` or `$D079` sets live := preference.
- Reads return the VIC's `$FF`.
- U64-targeted software such as Image BBS uses them (C64_TURBO_U64_ISSUES.md §1.2). They are kept as U64 turbo switch registers, not as SuperCPU support (Q11; DL4).

**No SuperCPU registers** (Q11). `$D0BC`, `$D0B8`/`$D0B9` and every other SuperCPU register are not decoded and read the VIC's `$FF`. There is no SuperCPU Detect option. Software that looks for a SuperCPU through `$D0BC` finds none, as on a U64 with SuperCPU Detect at its default, Disabled [V u64_config.cc:394].

**Live and preference** [V #153; u64_config.cc:1603-1637]:
- The preference is {menu index, menu badline setting}.
- Live is cleared to `$00` on every C64 reset.
- An apply event (section 1.5) sets live := preference at once in Registers mode. In TurboEnable mode it does so only if live ≠ `$00`, and entering TurboEnable from another mode sets live := `$00` (Q9; D18; the #153 residual, C64_TURBO_U64_ISSUES.md §3(e)).
- The U64's `SPEED_UPDATE` hooks fire for Turbo Control, CPU Speed, Badline Timing and SCPU Detect (u64_config.cc:923-926 [V]). Multitini has no SCPU Detect setting, and its apply event fires only when one of the other three changes value (section 6.1).
- Whether the U64 FPGA applies `SPEED_UPDATE` in TurboEnable mode is [O] (E7).

### 1.3 Speed table

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Label (MHz) | 1 | 2 | 3 | 4 | 6 | 8 | 10 | 12 | 14 | 16 | 20 | 24 | 32 | 40 | 48 | 64 |
| CPU slots per φ2 (N) | exact* | 2 | 3 | 4 | 6 | 8 | 10 | 12 | 14 | 16 | 20 | 24 | 32 | 40 | **48** | **64** |

- Labels come from u64_config.cc:322 [V]. They are the same on PAL and NTSC, as on the U64.
- Indices 14 and 15 deliver what the label says (Q9). The C64U delivers 47 and 63 [M #874], because it keeps one slot per φ2 for its VIC (GideonZ, #874/#203). Multitini's VIC fetches through its own RAM port and needs no island slot (C64_TURBO_FAST_VIC.md §3.11).
- The firmware-only config bit `c64u_top` (section 6.1) selects the C64U rows 47 and 63 for indices 14 and 15. It serves a board A/B test and a fallback, if a board gate ever finds software that depends on 47/63 (D17). It has no menu row. A UART debug command sets it for one session; a firmware build default can set it as the fallback. It is never persisted (section 6.5).
- \*Index 0 with badline timing enabled is the stock exact path. The C64U times Off and Registers index 0 identically [M Amiga64 HARDWARE.md:1305-1307]. In this design it is bit-identical in timing to Off; M2 proves it with lockstep scenarios for Manual index 0, Registers live `$00` and TurboEnable live `$00`, and the transitions among them and Off (C64_TURBO_U64_ISSUES.md §3(g)2).
- Index 0 with badline timing **disabled** is a 1-slot turbo whose local and internal-I/O cycles ignore BA [I, C64_TURBO_U64_SPEC.md §D2]. The menu labels it "1 MHz, badlines off (not cycle-exact)". It delivers at most one CPU cycle per φ2 for any code, because every φ2 forward's completion is charged to a grant (completion credit, section 2.7) [I]. "Not cycle-exact" refers to BA and to where accesses fall within the φ2, not to the rate.
- Slots are spread evenly by a Bresenham table (section 2.7).
- F6 (two fast clocks per CPU cycle, section 13.5) is a diagnostic build option only (Q14). A diagnostic F6 image reports caps mask `0x1FFF`, and indices above 12 clamp to 32; `$D031` still reads back the written value. No released image uses F6: NTSC ships only when it closes at 64x.

### 1.4 Defaults

- Menu defaults [V u64_config.cc:389-393]:
  - Turbo Control Off
  - CPU Speed index 0
  - Badline Timing Enabled
- Multitini options:
  - Disk auto-slow (the IEC guard), default **On** (deviation D3; Q6). Its holdoff after the last IEC edge is 500 ms; it is programmable only for experiment E-IEC (section 1.9).
  - Cartridge forces 1 MHz, default Off once cartridge turbo exists (M7). Until then a loaded cartridge always forces exact mode (D2), and the status line shows "(cartridge: 1 MHz)".
  - Expansion memory speed, default "C64U (1 byte per cycle)", once M9 exists (D19).
  - Ultimate Audio $DF20-$DFFF, default Off (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.5). Turning it on also turns the REU on (Q18, section 1.8).
- Debug facilities: off at every power-on and reboot (Q16, Q19, section 6.5). C64 diagnostics (`C64_CPU_CONFIG` bit 19, which arms the diagnostic fields of turbo and Ultimate Audio) are not armed.
- Hardware reset value of `C64_CPU_CONFIG`:
  - Fields: Manual, index 1, badline timing enabled, disk auto-slow on, `authoritative` = 0, `c64u_top` = 0, holdoff 500 ms, static-hold release off, Follow CPU off, diagnostics not armed.
  - Effect: old firmware, which toggles only CONTROL bit 5, gets "2 MHz Manual". That is the successor of Smart 2x.

### 1.5 Reset and other events

**Any C64 reset** (menu, PRG load, CRT, power-on):
- Live := `$00`.
- Exact mode is forced for 2.0 s, counted in native cycles, so pause stops the count: 1,970,497 cycles PAL, 2,045,454 NTSC.
- `$D031`/`$D030`/`$D07x` writes during the window update the live register and read back, but execution stays exact.
- At the end of the window:
  - Manual: the preference applies.
  - Registers: live := preference if nothing was written in the window; otherwise the latched value stays.
  - TurboEnable: live keeps whatever was written (0 by default).
- An apply event during the window follows the apply rule below and counts as a write.
- Sources: C64_TURBO_U64_SPEC.md §D5 and §F (item F7); D4.

**Apply event** (the value of Turbo Control, CPU Speed or Badline Timing changes, from a menu row, the one-step Turbo on/off row, a turbo action, a profile switch or the debug command `:speed`; section 6.1):
- Registers: live := preference at once.
- TurboEnable: live := preference only if live ≠ `$00`. Entering TurboEnable from another mode sets live := `$00` (Q9; D18). Software that turned turbo off with `$D030` = 0 therefore stays at 1 MHz after a menu change (#153 residual).
- Manual: uses the new preference at once.
- No apply happens for: boot restore, migration or CRC repair of the settings, re-selecting the same value, or a change to disk auto-slow, cartridge, expansion memory speed, an expansion row (SID audio, RAM expansion, Ultimate Audio) or a debug knob (`c64u_top`, IEC holdoff, static-hold release, C64 diagnostics armed) (C64_TURBO_U64_ISSUES.md §3(e); #787).

**`$D031`/`$D030`/`$D07x` write:** takes effect at the island's first φ2 boundary after the write lands (native retire in exact mode; queue landing in turbo). The U64 takes about 14-20 φ2 [M Amiga64:1313] (D9).

**IRQ, NMI:** no change. RESTORE produces no NMI in this machine today (D12, section 1.12). In turbo, an NMI fall that arrives while the island has no enable is taken once: at the next enable if the T65's last sample was high, one enable later through the rearm if it was low (section 2.9).

**Pause, menu open:** the CPU halts at a cycle boundary. Queued VIC/SID writes still land (section 2.13.5). Nothing else changes.

**U64 evidence for the reset rules:**
- [V #153] Live is cleared at reset, and fast-after-reset was called a bug in 2020.
- [V u64_config.cc:1034-1050, 1079, 1634-1636] The firmware reset task runs `setCpuSpeed` and strobes `C64_SPEED_UPDATE` on every reset unless `skipReset`.
- [M #874; xahmol] The FPGA forces 1 MHz for about 2 s and ignores `$D031` writes during that time.
- [O] How the FPGA treats the reset strobe in TurboEnable mode is unknown (E7).

### 1.6 Badline timing option (CPU side only; BA and the VIC are unchanged)

| Setting | Index ≥ 1 | Index 0 |
|---|---|---|
| Enabled (default) | Every island cycle, of every class, halts while BA = 0. A forward owns the bus only in BA-high cycles. As on a C64 and the U64 | Stock exact path |
| Disabled | Local cycles and fast VIC/SID accesses run through BA low. Forwarded accesses to CIA1/CIA2, I/O1/I/O2 (no cartridge) and the `$FF00` snoop complete in BA-low cycles through I/O-only ownership (section 2.13.8). Only cartridge accesses (M7) and the RAM-bus fallback classes (`FAST_COLOUR_RAM=0`, `FORWARD_PORT_WRITES=1`) wait for BA = 1 | 1-slot turbo, at most one CPU cycle per φ2 (section 2.7); local and internal-I/O cycles ignore BA |

- [V turbo_mode.rst:84, 167-176] U64: badline timing does not change BA; with it disabled "the CPU can continue even when BA=0, given that the access is to an internal address"; external-bus accesses wait for BA = 1.
- [I] CIA reads that complete in BA-low cycles are behaviour no real C64 shows: the real PLA blocks I/O reads while BA is low (1541ultimate 08c8033d, slot_master_v4.vhd comment). This happens only in turbo with badline timing disabled. It is U64-like and approved by Q1 (risk R22).
- Not modelled: the C64U's loss of slot 32 in BA-low cycles with badline timing disabled (#665). The effect is under 0.2% (D6).
- Forced exact mode overrides the setting (section 1.11): while any forced-exact cause holds, every cycle takes the stock path, including the BA halts.

### 1.7 I/O timing contract at turbo

| Class | First release (turbo) | U64E2/C64U [M] | Fallback build |
|---|---|---|---|
| RAM, BASIC/KERNAL/char ROM | 1 slot | 1 slot | — |
| `$00/$01` read | 1 slot (internal port) | fast [I] | — |
| `$00/$01` write | 1 slot, local (Q3; section 5.4) | fast [I] | `FORWARD_PORT_WRITES=1`: one φ2 (D11) |
| Colour RAM `$D800-$DBFF` | write 1 slot; read 2 slots; high nibble = last VIC byte | [O] | `FAST_COLOUR_RAM=0`: one φ2 |
| VIC registers `$D000-$D3FF` | read: equal to RAM at indices 0-12, +0.5 to +1 slot at 13-15 (0.078 φ2 per unrolled `lda` at 15; 0.079 with the C64U row). Write: 1 slot, posted, lands 3-4 fast clocks later. Runs during BA low with badline timing disabled | reads at turbo rate, about +1 slot (0.080 at 15); writes effectively 1 slot [I from #203] | `FAST_VIC_IO=0`: one per φ2 (D1) |
| SID registers `$D400-$D7FF` (both internal SIDs) | as VIC registers | reads 0.080-0.085 at 15; writes not measured | `FAST_SID_IO=0`: one per φ2 (D1) |
| CIA1, CIA2 | one per φ2, never posted. With badline timing disabled, completes in BA-low cycles (I/O-only) | one per φ2; immune to BA with badline timing disabled [M #665] | `BA_LOW_IO=0`: waits for BA = 1 (D5) |
| I/O1, I/O2 without a cartridge (REU registers, `$DE00`/`$DF00` SID, Ultimate Audio `$DF20-$DFFF` when on, open bus) | as CIA | one per φ2 | as CIA |
| `$FF00` writes | 1 local RAM write + one φ2 snoop forward; completes in BA-low cycles with badline timing disabled | [O] | `FF00_SNOOP=0`: one φ2 RAM-bus forward, waits for BA = 1 (D5r) |
| Cartridge windows and cartridge I/O | exact mode (D2) until M7, then φ2, waiting for BA = 1 | φ2, waits for BA = 1 | — |

The φ2 access rule [I]:
- A forwarded request registered by fast clock 30 (of 64) runs in that native cycle. Otherwise it runs in the next one.
- The CPU resumes at fast clock 4 of the following cycle. IRQ-acknowledge accesses resume at about fast clock 13 (section 5.8).
- Back-to-back forwarded accesses cost exactly one φ2 each.
- Every bus cycle is its own access: indexed-store dummy reads, both RMW writes, page-cross dummy reads. T65 issues every NMOS bus cycle [V C64_TURBO_T65_CORE.md §5]. This holds for fast VIC/SID accesses too: each dummy read is a fast read with its side effects.
- No φ2-class write is ever posted. VIC/SID writes are posted only into the ordered FIO queue, with barriers (section 2.13.9).
- CIA timers, TOD and the raster counter run at the native rate.

The grant pattern is rotated so the first granted slot of each frame is at fast clock 5. The U64E measures an unrolled `lda $de80` at 2 φ2 per access at 3-5 MHz and 1 φ2 at 6 MHz and above [M gist839 meas_c; C64_TURBO_U64_SPEC.md §D1]. On Multitini's table the model gives 2 φ2 at 3 and 4 MHz and 1 φ2 at 6 MHz and above, which matches [I, C64_TURBO_MODELS.md (final.py, sim.py)]. It also gives 2 φ2 at 2 MHz; that value is the model's alone, because the U64 data has no 2 MHz figure. At index 0 with badline timing disabled the completion credit gives 4 φ2, at most one CPU cycle per φ2 (section 2.7) [I, C64_TURBO_MODELS.md (governor_credit.py)]; no U64 figure exists for that case [O, section 11].

**Unmapped addresses** [I; Q9; C64_TURBO_U64_ISSUES.md §3(g)3, #205/#304]. No fast or local path answers an address that nothing decodes:
- Empty I/O1/I/O2 (no REU, no cartridge, and outside the Ultimate Audio window when that option is on) is always forwarded, so buslogic returns the floating-bus byte `lastVicData`, as on stock [V buslogic:157-163].
- FIO reads return exactly the native read-mux value, including the VIC's unused registers (`$FF`) and SID write-only and mirror reads (section 2.13.4).
- M7 keeps the rule for Ultimax-unmapped ranges.

**Ultimate Audio registers** (Q12; docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.10). With the option on, `$DF20-$DFFF` is I/O2: a φ2 forward, never posted, I/O-only eligible with badline timing disabled, exactly like the REU registers. Writes to `$DF3F`, `$DF5F`, … `$DFFF` join the IRQ-acknowledge class (section 5.8). Neither the REU registers nor the UA registers may ever move to the fast I/O class: the common UA detection routine waits at most 128 status reads for its 102.4 µs probe, which needs one φ2 per read (that design, §1.10). **Pacing rule (binding):** the fast I/O port never serves a CPU access to `$DF00-$DFFF`, and no such access completes less than one native cycle after the previous I/O2 access. Back-to-back accesses complete exactly one φ2 apart while BA stays high, and also in BA-low cycles with badline timing disabled; with badline timing enabled, BA-low cycles add waits. At one φ2 per read, the 128-read wait lasts about 130 µs, which covers the probe. An acknowledge-class access completes at about fast clock 13 of the next native cycle instead of fast clock 4, but the next forward still loads at the following e_15, so the pacing stays one φ2 per access (sections 5.8, 5.9).

### 1.8 REU

**Stock REU behaviour does not change (Q8).** The REU is correct at stock today: Expand by Bonzai runs flawlessly on M0.7.0dev5 [V docs/C64_EXPANSIONS.md:103-108]. No first-release turbo change touches the REU, its cache or its DDR path in exact mode. The first-release REU-side additions are idle unless used, or observation-only, and none changes REU logic: the cache-invalidate input for REU image preload (section 6.4); the read-only outputs Ultimate Audio takes from the REU cache (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.5); and, for Ultimate Audio's `C64_REU_MISSES` counter, one REU-state output on `multitini_c64_reu.sv` and one phase-15 DMA-slot strobe on fpga64 (that design, §6.1). X-T4 checks the two cache additions together (section 10.3). The existing REU suites, run unchanged, UA T2 (the golden trace, re-run on the merged tree as X-T3) and G-REU0 check that none of the four changes stock REU behaviour. The opt-in M9 path comes later and edits the stock REU path; it must be functionally identical to stock with the option off (section 10.2, M9). Expand disks 1 and 2 are a hard regression gate with Ultimate Audio off, at stock and with turbo configured but forced exact (Q20; gate G-REU0, sections 10.2 and 10.3).

- The CPU stops on the triggering write (`$DF01` execute, or `$FF00` when armed). No later instruction runs until the transfer is done, including the BA states after the last access [V reu_turbo_test.py:4-11; reu.sv:163-170, 213-220].
- DMA rate: 1 byte per φ2 at every speed (1.0227 bytes/µs on NTSC). The C64U measures a flat 1 byte/µs [M doom/reu-format.md:24].
- REU register programming arrives at one write per φ2, now also inside badlines with badline timing disabled. The REU cache is qualified only with at least four native cycles between register writes [V docs/C64_EXPANSIONS.md:96-101], so the DDR prefetch may start later; data is unaffected. E5 and E-BA3 test this, with the cases of C64_TURBO_U64_ISSUES.md §2 (U64 3.15 "REU stale data" row): back-to-back register writes then a trigger; a bank change on the last write; a fetch right after a stash to the same address; a stash right after CPU writes to the source; an `$FF00`-armed trigger; each at index 15 and index 1, badline timing on and off, with exact bytes and no island enable between trigger and completion.
- With badline timing disabled the trigger write may land in a BA-low cycle. DMA takeover still honours BA and the VICE post-access checks [V fpga64:479-533]. D16: native-slot REU DMA keeps the VICE BA rules in every mode, because it cannot skip badlines without taking c-accesses from the VIC. D16 is permanent for native-slot DMA (design decision DL3; Q-BA1 answered no). The only exception is the RAM side of the opt-in Follow CPU path (M9), which never uses the shared bus. E-BA4 still measures the U64 for the record.
- **Expansion memory speed** (M9; D19; opt-in, default off): "Follow CPU" moves REU transfers to and from RAM to at most one byte per granted CPU slot while the effective mode is turbo. Bytes to I/O or colour RAM and fixed-address transfers keep the native φ2 path. The default, "C64U (1 byte per cycle)", must behave exactly as stock; G-REU0 and the REU suites on the M9 netlist prove it (section 10.2, M9).
- **Ultimate Audio** (Q12, Q18, Q20): when on, the REU decodes only `$DF00-$DF1F`; its register mirrors above `$DF1F` disappear and `$DF20-$DFFF` belongs to the sampler (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.9). Turning Ultimate Audio on also turns the REU on in the same apply, turning the REU off also turns UA off, and a loaded cartridge blocks both (Q18; docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2-6.4 owns the rules, row states and texts). An Expand failure with UA on that is caused only by the missing mirrors is accepted and documented (Q20, section 10.3). UA reads its samples from REU memory under that design's coherence rules (§2.5); nothing here changes them.
- **REU image preload** (Q15) writes a `.reu` file into the REU's DDR memory while the machine is paused, the REU is idle and Ultimate Audio is held inactive, then invalidates the REU cache (section 6.4). The UA hold is acknowledged ("UA held", `C64_REU_HOST` bit 14) before the copy starts.

### 1.9 Disk auto-slow (IEC policy)

- Disk auto-slow is a menu option, default On (**D3**; Q6). The existing guard (multitini_c64_cpu_turbo_guard.sv:37-50) forces exact mode during IEC or parallel-cable activity and for a holdoff after it (500 ms by default).
- **It is a stricter form of the U64's own IEC slowdown.** The U64 FPGA has had an undocumented IEC slowdown since 1.34 [V commit 3034a3b4], and U64 users still report IEC failures at turbo [M #737; Lemon64 t=75567] (C64_TURBO_U64_ISSUES.md §3(b)). Off is therefore **below** U64 parity, not equal to it. The menu labels the values "On" and "Off (disk access may fail)". v2's "Off gives U64 parity" was wrong.
- The guard output is combinational [V]. B1 captures it at e_1, after the CIA2 port update at e_0 [V mos6526.v:132] (section 5.2). This also holds for a `$DD00` write completed through I/O-only ownership.
- As a result, no turbo local cycle runs after a forwarded `$DD00`/`$DD01` access that trips the guard.
- Activity started by the drive reaches the island through a live crossing within 2 fast clocks. The CPU can observe the lines only through later forwarded reads.
- Forced exact mode while the guard blocks also overrides `$D031` bit 7 (section 1.11). This avoids the U64 1.34 known issue (some loaders corrupt disk access with badlines disabled) [V U64 firmware 1.34 release notes, via C64_TURBO_U64_ISSUES.md §2].

**Block cause** (Q9). STATUS bits [7:6] report why the guard blocks, highest priority first: 1 "line held" (an IEC line, on either side, is low), 2 "strobe held" (a parallel-cable strobe is low), 3 "holdoff" (every line and strobe is released, and the holdoff after the last edge on any of them is running). The menu shows the cause, for example "(disk: line held)", "(disk: cable held)" or "(disk: 1 MHz)". Today the guard blocks for as long as any line is held low [V turbo_guard.sv:45-50, 70-71], so a program or drive code that parks a line low cuts turbo silently; the cause makes that visible.

**Programmable holdoff** (Q9). `C64_CPU_CONFIG[17:16]` selects 500 ms (default), 100, 50 or 20 ms, counted in `c64_clk` cycles. The guard keeps one cycle-count parameter per code: `IEC_HOLDOFF_CYCLES` stays as code 0, with today's default (`CORE_CLOCK_HZ` + 1) / 2 [V turbo_guard.sv:11-12], and three new parameters derived from `CORE_CLOCK_HZ` serve codes 1-3. Benches override them with short values, as tb_c64_cpu_turbo_guard already does for `IEC_HOLDOFF_CYCLES` [V tb_c64_cpu_turbo_guard.sv:19-21]. The 0.5 s value was copied from MiSTer c64.sv, not derived; MEGA65 uses about 1 ms (C64_TURBO_U64_ISSUES.md §3(b)). Experiment E-IEC runs the IEC gate set (G-IEC, section 10.2) at 100, 50 and 20 ms. A code becomes the default only if the whole gate set also passes at the next shorter code, which is at most half its value (at least a 2× margin): 100 ms needs a pass at 50 ms, and 50 ms a pass at 20 ms. 20 ms is a test point only and cannot become the default. Until then only the UART debug command sets the field, and it is not persisted (section 6.5).

**Static-hold release** (Q9; designed, shipped disabled, `C64_CPU_CONFIG[11]` = 0). When enabled, a separate counter of at least 26 bits counts `c64_clk` cycles since the last edge on any IEC line or parallel strobe; 2.0 s is 63.06 M cycles PAL and 65.45 M NTSC, more than the 25-bit holdoff counter holds [V turbo_guard.sv:26]. When it reaches 2.0 s:
- the held-level term (`iec_lines_released`) is masked in both the `turbo_active` expression and the holdoff reload condition, and the holdoff counter is cleared [V turbo_guard.sv:49-50, 70-71]. Masking only the output would leave the reload running every clock and turbo still blocked;
- it releases held IEC lines and held parallel strobes alike;
- any edge clears the counter, removes the mask and reloads the holdoff.
- Safety [I]: every IEC handshake step starts with an edge, and every edge re-trips the guard. After the timeout, the guard is therefore exactly as safe as today's "lines idle" state, where turbo already runs until the next edge.
- It becomes the default only if board telemetry (the block cause) shows real titles stuck in "line held" **and** the IEC gate set passes with it enabled. Whether the KERNAL or common loaders leave a line low when idle is not known; that is why telemetry comes first.
- Until then only the UART debug command enables it, and it is not persisted.
- Disk-streaming titles (constant traffic) stay at 1x while streaming, by design.

**Rejected** [I]:
- An explicit `$D031`/`$D030`/`$D07x` write ending the holdoff: IRQ code may write the turbo registers every frame while a main-loop loader (Bitfire style) is receiving, which would resume turbo mid-protocol.
- Device-address filtering: Multitini has no virtual printer and no physical IEC port. Device-4 traffic trips the guard briefly, and the KERNAL times out with "device not present". Every IEC device on Multitini has authentic timing, so a filter would add logic and risk for no benefit.

**Cassette.** When TAP support lands, motor and sense join the guard (`cass_motor` is unconnected today, section 2.10).

### 1.10 Cartridge policy

- First release: exact mode while `cart_loaded | cart_loading` (adapter:515 rule kept; **D2**). The status line shows "(cartridge: 1 MHz)" (Q10).
- M7 stays where v2 placed it, after the first release (Q10). The sweep's proposal to move it earlier was not adopted.
- M7 adds cartridge turbo:
  - every ROML, ROMH, I/O1, I/O2 and Ultimax-unmapped access is forwarded and never gets I/O-only ownership (cartridge.sv uses the shared address and `ramWE`, section 2.13.8);
  - GAME/EXROM enter the island map, with recomputation on change;
  - vector pulls are forwarded while a cartridge is loaded;
  - `nmi_ack` is stretched;
  - the "Cartridge forces 1 MHz" option is added, default Off per C64_TURBO_U64_SPEC.md §D6;
  - the unmapped-address rule of section 1.7 holds for Ultimax-unmapped ranges.
- The REU stays off while a cartridge is loaded (adapter:427), and so does Ultimate Audio, which plays from REU memory (Q18).

### 1.11 Register liveness and readback while exact mode is forced [I]

| Condition | Registers live? | Readback | Execution |
|---|---|---|---|
| Turbo Control Off | no | `$FF` | stock |
| Manual | no | `$FF` | menu speed |
| Registers/TurboEnable, unblocked | yes | live | live speed |
| Reset window | yes; writes latch | latched value | exact |
| Disk auto-slow blocking | yes | live | exact |
| Cartridge forcing exact (D2 or option) | yes | live | exact |
| Content loader active | yes | live | exact |
| Legacy firmware (`authoritative` = 0) with CONTROL bit 5 = 0 | as Off | `$FF` | stock |

**Forced exact overrides `$D031` bit 7** (Q9). While any forced-exact cause in the table holds, the island runs the stock path: there is no BA-ignoring local cycle, no FIO access and no I/O-only ownership. The stored bit 7 still reads back. The U64 1.34 notes list a known issue of this kind, and no fix note was found up to 3.15 (section 1.9).

**Visibility** (Q9). STATUS reports each cause (section 6.1), and the menu shows the requested speed, the live `$D031`, the effective speed and the cause, for example "64 MHz, now 1 MHz: disk" or "reset window" (section 6.2).

**Consequence.** GNW64 or xahmol-style detection still finds "U64 turbo" and then runs at 1x. That is the same as running on a stock C64. A stated deviation, under D2/D3.

### 1.12 Deviations

**Turbo deviations from the U64 (first release)**

| # | Deviation | Status |
|---|---|---|
| D1 | VIC and SID register accesses take one per φ2 | **Retired** by Q1. Exists only in the fallback builds `FAST_VIC_IO=0` / `FAST_SID_IO=0` |
| D2 | A loaded cartridge forces exact mode | until M7 |
| D3 | Disk auto-slow, default On. It is a stricter form of the U64's own IEC slowdown (section 1.9); Off is below U64 parity | permanent option (Q6) |
| D4 | Writes in the reset window are latched, not dropped; TurboEnable boots with live = 0 | permanent choice |
| D5 | With badline timing disabled, forwarded internal accesses wait for BA = 1 (#665 growl) | **Retired** by Q1. Exists only with `BA_LOW_IO=0`. Residual D5r (`$FF00` writes only) only with `FF00_SNOOP=0` |
| D6 | Slot 32 in BA-low cycles is not taken from the CPU | permanent |
| D7 | The FLI colour nibble in turbo comes from the idle dummy read (fpga64:698), not from opcode bytes. With badline timing disabled it can also come from the low nibble of a forwarded I/O read that lands in BA-low cycles 1-3 of a mid-line badline | permanent; cosmetic |
| D8 | Forwarded-request cutoff at fast clock 30 vs about 32-36 on the U64. With rotation, the measured `meas_c` boundary still matches | permanent |
| D9 | Speed-switch latency is the next φ2 (U64: about 14-20 φ2) | permanent |
| D10 | Removed (Q11). No SuperCPU register is decoded; on the U64 this matches SuperCPU Detect at its default (Disabled). The number is not reused | — |
| D11 | Only with build fallback `FORWARD_PORT_WRITES=1`: `$00/$01` writes cost one φ2 each. This hurts code that flips `$01` per access, e.g. Doom's REU transfers | fallback only (Q3) |
| D12 | RESTORE gives no NMI. `freeze_key` is unconnected (adapter:525; fpga64:1121) and the cartridge gets `freeze_key` = 0 (adapter:493) [V]. Pre-existing, not caused by turbo | until RESTORE is wired; then model it as a held level |
| D13 | IRQ-acknowledge accesses complete later: forwarded CIA ICR and `$DF00` reads and Ultimate Audio acknowledge writes about 9 fast clocks (0.14 µs) after other φ2 accesses; fast `$D019`/`$D01A` writes block the next enable for about 9 fast clocks (section 5.8) | permanent; timing only |
| D14 | Fast-I/O micro-timing may differ from the C64U: read-after-write wait (0-3 slots), landing latency (3-4 fast clocks), H1/H2 holds, selective collision clear, fetch-ordered local writes, a badline turned off and on within one φ1 restarting the BA→AEC count, sub-cycle DEN or raster-compare pulses that still set `rasterEnable` or IRST, and an NMI fall after a low T65 sample taken one T65 enable later through the rearm (section 2.13.12, T-i and T-j) | permanent; timing only; [O] E7-V1..V6, E7-V9, E7 (CIA2 NMI case, question 26) |
| D15 | The SID edge stage keeps isolated sub-µs gate/test-bit pulses (at most one per control register per 2 µs; denser sequences are compressed, section 2.13.7) that the U64 probably drops | a deviation only if E7-S shows the U64 drops them. `EDGE_STAGE` = 1 ships by design decision DL6, which follows the sweep's recommendation under Q7 to keep the stage even then; `EDGE_STAGE=0` removes it only if the user overrides DL6 |
| D16 | Native-slot REU DMA honours badlines with badline timing disabled: the VICE BA rules hold in every mode. Exception: the RAM-side bytes of the opt-in Follow CPU path (M9, D19) | permanent for native-slot DMA (DL3; note in section 0.6). E-BA4 records the U64 behaviour |
| D17 | Indices 14 and 15 deliver 48 and 64 CPU cycles per φ2; the C64U delivers 47 and 63 [M #874]. The firmware-only `c64u_top` bit restores 47/63 | permanent (Q9) |
| D18 | In TurboEnable mode an apply raises live only if live ≠ `$00`, and entering TurboEnable sets live := `$00`. The U64 sets live := preference on `SPEED_UPDATE` (whether in TurboEnable mode is [O], E7) | permanent (Q9; closes the #153 residual) |
| D19 | Opt-in "Expansion memory speed: Follow CPU": REU transfers to and from RAM run at up to one byte per granted CPU slot. The C64U is flat at 1 byte/µs and the U64E plateaus at about 5 B/µs [M #839] | only when the user enables it (M9); the default is C64U parity |

**Pre-existing stock deviations from VICE 3.10.** These are inherited by the island in both modes and are not worsened [V C64_TURBO_T65_CORE.md §4; C64_TURBO_FAST_SID.md §2.5]:
- SD1: ANE (`$8B`) uses magic `$EE`; VICE uses `$EF` (`$EE` when RDY steals) (T65.vhd:671).
- SD2: The 12 JAM opcodes run as 2-cycle NOPs; VICE halts the CPU (T65_MCode "KIL" branch).
- SD3: After a write to SID `$19-$1C` the bus latch holds the live value, not the written byte (sid_top.sv:200-238). reSID keeps the written byte.
- SD4: The SID bus latch never decays (sid_top.sv:89, 201-238). reSID decays it after `databus_ttl` cycles (`0x1d00` for the 6581) [V VICE svn-mirror `vice/src/resid/sid.cc`].

---

## 2. Architecture

### 2.1 Block diagram

```
                FCLK2 150 MHz
                     |
     clk_wiz_c64 MMCM (PAL D=9 M=56.75 VCO 945.83 | NTSC D=5 M=24 VCO 720; one plan per image; E2 may change)
        CLKOUT0 /30 | /22  (clk_wiz clk_out1)     CLKOUT1 /15 | /11  (clk_wiz clk_out2)
              v                                         v
   c64_clk (BUFG) 31.528 | 32.727 MHz        c64_cpu_clk (BUFG) 63.056 | 65.455 MHz
              |                                         |
+-------------+----------------------------+ +----------+----------------------------------+
| c64_clk domain (machine unchanged)       | | CPU island (c64_cpu_clk)                    |
|  fpga64_sid_iec  EXTERNAL_CPU, FAST_IO   | |  cpu_6510 (T65 + 6510 port, unedited)       |
|   VIC (+fast port) CIA1 CIA2             | |  page-class map (registered, map_ok bubble)  |
|   SID (+fast port) buslogic (+I/O-only)  | |  grant table N/64, frame counter from c64    |
|   scheduler (native slot only)           | |  DI mux: RAM_L RAM_H ROM CHR IO2 B0          |
|   ^ext_cpu_addr/do/we/io/ioonly | cpuDi  | |  turbo_q; FSM: EXACT RUN FWD ACKWAIT DMA HALT|
|  cpu_proxy (SV, adapter):                |<+--isl_x_req {addr,wdata,we,port,exact,      |
|   frame register (exact: e_1; turbo:     | |              ioonly,seq}                     |
|   dummy at R, forward at e_15)           |-+->B0, B1, live IRQ/NMI/BA/hold, phase+run,   |
|   B0 @ R, B1 @ e_1 (combinational policy)| |  port tick, IRQ-safe marker, lastVicDi       |
|  fio_port: pop <=1/edge, H1/H2 holds,    |<+--isl_x_fio_mem[4], wp (VIC/SID writes,      |
|   native-busy barrier, done counter      |-+->  clears, latch loads); rp, done, busy     |
|  VIC/SID fast read muxes (comb)          |-+->io2_q (2-clock read; shared with COL)     |
|  turbo_regs: $D030/$D031/$D07x, live,    | |  barriers: all_done, fetch_done, ack_wait    |
|   2.0 s window, apply counter, policy    | |  NMI hold/rearm (seen, fell, force)          |
|  REU+cache+HP2, 1541, loaders, video,    | |                                              |
|  audio, cartridge, guard (+cause),       | |                                              |
|  mailboxes, RAM peek, REU invalidate     | |                                              |
+---------------+--------------------------+ +-----------------+----------------------------+
                | port B: posedge c64_clk                       | port A: negedge c64_cpu_clk
                v                                               v
     +-------------------------------------------------------------------------------+
     | main_ram_i: 64 KiB = 2 x 32 KiB TDP, 16 RAMB36 (32Kx1, no cascade)             |
     | colour RAM: 1Kx4 TDP, 1 RAMB18 (moved out of fpga64 under EXTERNAL_CPU)        |
     | island ROM copies: BASIC/KERNAL 4 + CHAR 1 RAMB36 (port A read, port B loader) |
     +-------------------------------------------------------------------------------+
```

### 2.2 Clocks

- There are two outputs of the existing MMCM.
- `c64_clk` stays a free-running 50%-duty BUFG for everything that uses it today:
  - `video_sync`'s falling edge (video_sync.vhd:50);
  - HP2 ACLK (create_project.tcl:555-556);
  - PSRAM (multitini_one_top.sv:1292-1309);
  - mailboxes.
- Each image pins its own plan (PAL or NTSC). Details and the fix ladder for clock modelling are in sections 3.1 and 13.3.

### 2.3 CPU placement

- There is one T65 in every mode: the existing `cpu_6510`, instantiated in the new `multitini_c64_cpu_island.sv` on `c64_cpu_clk`.
- The drive's T65 (c1541_logic.sv:90-105) is a separate instance and is untouched [V].
- `fpga64_sid_iec.vhd` gains two generics:
  - `EXTERNAL_CPU` (default `false`): replaces the `cpu_6510` instance (fpga64:984-1008) by ports and exports colour RAM.
  - `FAST_IO` (default `false`): enables the VIC and SID fast ports (`FAST_PORT => FAST_IO` on both instances) and the I/O-only ownership. It is kept separate from `EXTERNAL_CPU` so a φ2-only build stays possible.
- The default `false` for both keeps today's instance text, so existing benches and text-anchored runners still work.

| New fpga64 port (EXTERNAL_CPU) | Dir | Connection |
|---|---|---|
| `ext_cpu_addr[15:0]`, `ext_cpu_do[7:0]`, `ext_cpu_we` | in | `cpuAddr_pre`, `cpuDo_pre`, `cpuWe_pre` from the proxy frame register |
| `ext_cpu_io[7:0]` | in | `cpuIO` (bankSwitch, fpga64:617), from the frame register's port field |
| `ext_cpu_ioonly` | in | frame bit: this forward may take I/O-only ownership (section 2.13.8). Default `'0'` |
| `ext_nmi_ack` | in | `nmi_ack`, direct from the island T65 |
| `ext_reset` | out | fpga64's internal `reset`, registered at the edge ending phase 31 (fpga64:433-440) [V: not exported today, fpga64:44-190]. The island, proxy, fio_port, turbo_regs, `cpuIoOwn` and the SID stages use it (RST1, RST2), never `core_reset_n` directly |
| `ext_cpu_enable` | out | `enableCpu and not dma_active and not memory_wait_q` (today's T65 enable, fpga64:989) |
| `ext_cpu_di[7:0]` | out | `cpuDi` |
| `ext_irq_n`, `ext_nmi_n`, `ext_rdy` | out | `irq_cia1 and irq_vic and irq_n and irq_ext_n`; `irq_cia2 and nmi_n`; `baLoc` (fpga64:993-996) |
| `ext_port_tick` | out | `enableSid and not memory_wait_q` (fpga64:1006) |
| `ext_phase[4:0]`, `ext_frame_run` | out | `sysCycle` position, and "the frame advanced on this edge" (`memory_wait_q = 0` and `sysEnable = 1`, fpga64:404-429) |
| `ext_cpu_bus_valid`, `ext_cs_vic`, `ext_last_vic_di[7:0]`, `ext_cpu_ioown` | out | completion and decode observation |
| `ext_dma_hold` | out | `dma_req or dma_active or dma_post_pending or dma_wait_vic` |
| `ext_color_*` | out/in | port B of the external colour RAM |

| New fpga64 port (FAST_IO) | Dir | Connection |
|---|---|---|
| `ext_fio_rd_addr[9:0]`, `ext_fio_rd_data[7:0]` | in/out | island T65 address (unregistered) into the VIC/SID read muxes; combinational data back (section 2.13.4) |
| `ext_fio_vic_wr`, `_wa[5:0]`, `_wd[7:0]`, `_clr_m2m`, `_clr_m2d` | in | registered strobes from `multitini_c64_fio_port`; on a clear, `_wd` carries the mask of reported bits |
| `ext_fio_sid_wr`, `_latch`, `_a[9:0]`, `_d[7:0]` | in | registered strobes from `multitini_c64_fio_port` |
| `ext_fio_vic_hold[1:0]`, `ext_fio_vic_busy` | out | H1/H2 hold windows (H2 includes phase 13 of `cycleCalcSprites`) and the VIC native write latch `we_r` |

- The adapter ties `turbo_mode` = "00", `turbo_speed` = "00" and `local_turbo_allowed` = 0. The scheduler then issues only the native slot (scheduler:42-58) [V], and its entity and positional bench stay valid.
- Smart 2x is retired.

### 2.4 Memories (details in section 4)

- Main RAM becomes true dual-port and is split into two uncascaded 32 KiB banks: port A for the island, port B for the machine and the loader.
- The island gets copies of the BASIC/KERNAL and character ROMs.
- Colour RAM becomes true dual-port.
- The fast I/O path uses no BRAM.
- Main RAM gains a read-only host peek on port B, below the loader in priority and used only while the machine is paused (C64 RAM readback, section 6.4).
- The audio capture FIFO (a debug feature, section 6.5) takes one RAMB18 in the control domain.

### 2.5 Island bank map and classes

- The map holds 16 page classes. It is recomputed one fast clock after the island's 6510 port pins change, with a 1-clock `map_ok` bubble.
- GAME = EXROM = 1 while no cartridge is loaded.
- The lookup is a 16:1 mux on A[15:12], plus A[11:10] for page D, the A[15:1]=0 test and a compare against `$FF00`. It follows buslogic:217-297 for the no-cartridge case [V].

| Range | Read | Write |
|---|---|---|
| `$0000-$0001` | PORT (inside `cpu_6510`, cpu_6510.vhd:92-94) | PORTW: the 6510 port register updates inside `cpu_6510`; RAM at the address gets `lastVicDi_q` through port A (section 5.4) |
| `$0002-$9FFF`, `$C000-$CFFF` | RAM | RAM |
| `$A000-$BFFF` | HIRAM & LORAM ? ROM : RAM | RAM |
| `$D000-$DFFF` | (HIRAM\|LORAM)=0: RAM. CHAREN=1: `$D800-$DBFF` COL; `$D000-$D7FF` FIO; `$DC00-$DFFF` FWD_IO. Otherwise CHR | (HIRAM\|LORAM)=0: RAM. CHAREN=1: COL, FIO or FWD_IO. Otherwise RAM |
| `$E000-$FFFF` | HIRAM ? ROM : RAM | RAM, except `$FF00`, which is always FWD_FF00 |

Classes [I]:

| Class | Addresses | Path | I/O-only eligible (badline timing disabled) |
|---|---|---|---|
| L | RAM, ROM, CHR, COL, PORT read, PORTW | port A | n/a (local; ignores BA) |
| FIO | `$D000-$D7FF` with I/O visible: VIC (A10 = 0), SID (A10 = 1) | fast I/O port (section 2.13) | n/a (never on the native bus). A target built with `FAST_VIC_IO=0` or `FAST_SID_IO=0` is forwarded instead and is eligible |
| FWD_IO | `$DC00-$DFFF` with I/O visible | φ2 forward | yes, if no cartridge is present |
| FWD_FF00 | `$FF00` writes | local RAM write + φ2 snoop forward (section 2.13.8) | yes |
| FWD_RAM | fallback forwards: COL (`FAST_COLOUR_RAM=0`), PORTW (`FORWARD_PORT_WRITES=1`) | φ2 forward | no (needs the shared bus) |
| FWD_CART | M7 cartridge windows and cartridge I/O | φ2 forward | no, permanently |

Class notes:
- `$00/$01` writes are local (Q3). Build fallback `FORWARD_PORT_WRITES=1` maps them to FWD_RAM (D11).
- `$FF00` writes are always seen by the REU snoop. The island holds no live REU state.
- In M7, GAME/EXROM enter the map, and the cartridge windows plus Ultimax-unmapped ranges become FWD_CART.
- With Ultimate Audio on, `$DF20-$DFFF` stays FWD_IO (I/O2); the island map needs no UA state. UA acknowledge writes (`$DF3F`, `$DF5F`, … `$DFFF`) are decoded by address into the IRQ-acknowledge class (section 5.8).
- No class answers locally for an address that nothing decodes: I/O1 and I/O2 are always forwarded, so empty space returns the floating bus from buslogic (section 1.7; Q9).

### 2.6 Native proxy (`multitini_c64_cpu_proxy.sv`, c64_clk, in the adapter)

The proxy serves only exact cycles and φ2 forwards. FIO traffic never touches it (section 2.13).

**Frame register.** It drives `ext_cpu_addr/do/we/io/ioonly`.
- **Exact mode:**
  - It loads the island's registered request `isl_x_req` at the first c64 edge from e_1 to e_14 after a new exact request appears (normally e_1).
  - It holds that request through R. A request that does not retire (DMA or `memory_wait` withholds `ext_cpu_enable`) stays in the frame for the next slot.
  - **At the R where the request retires** (the B0 capture condition: `ext_cpu_enable` = 1 with an exact request presented), the frame returns to the idle dummy. A retired request is never shown at a later e_15, so it can never run twice. Nothing native reads the frame between R and e_14 (section 2.10, point 3), so this changes no stock behaviour.
  - During reset it shows the island's reset-state request.
  - `ioonly` is always 0.
  - [V] Review finding 8: v1 and the first v2 draft kept the retired request "until the next load". At an exact→turbo switch no exact load follows, so fpga64 re-decided `cpuHasBus` from the stale request at the next e_15 (fpga64:530-531) and the slot re-issued it (scheduler:41-42; fpga64:1018-1023, 1039): duplicated CIA/VIC/REU side effects, a stale RAM write over newer island writes, a stale SID write, a spurious B0, and a forward that could take the shared bus with BA low.
- **Turbo mode:**
  - At **every** R it returns to the idle dummy (read of `$0000`, we = 0, port = island port, `ioonly` = 0), whether or not the access completed.
  - It loads a pending forward only at e_15, and only if the forward was registered by end f=30.
  - It loads `frame.ioonly <= isl_x_req.ioonly & ~cart_present` with the forward, where `cart_present = cart_loaded | cart_loading`.
  - This keeps `cpuWe_pre` = 0 at every e_15 that decides ownership (fpga64:530-531, 553 [V]).

**Bundles.**
- B0 at R: data (`cpuDi` or the U64 read override), IRQ term, NMI term, `baLoc`, valid.
- B1 at e_RESUME (default e_1):
  - `ext_dma_hold`;
  - the combinational blocker term (guard, cartridge, loader, reset window);
  - mode/N/badline;
  - pause.

**No shadow port.** The island's port output travels with every request (`isl_x_req.port`), so native decode always uses the banking that was current when the request was issued (section 5.4).

**Sequence numbers** give "done once" semantics. Completion is `cpu_bus_valid` at R, which now includes I/O-only ownership. A BA-blocked turbo forward is reloaded from `isl_x_req` at the next e_15, never replayed from the frame register.

**Generic** `BA_LOW_IO` (default 1). 0 ties `frame.ioonly` to 0, which brings back D5 (diagnostic builds only; section 6.5).

### 2.7 Speed governor

- The frame counter is `fc` = {`phase_q`, h}:
  - `phase_q` is `ext_phase` registered into the island.
  - h is the fast-clock parity within the c64 period.
- Grant = `TABLE[N][fc] & frame_run_q`. Grants therefore exist only on fast clocks where the native frame actually advanced: pause and `memory_wait` stretches give none.
- `TABLE` is an 18 × 64-bit constant, generated offline and unit-tested: one row per index, plus the C64U rows 47 and 63, which replace rows 14 and 15 when `c64u_top` = 1 (D17). Each row holds N Bresenham-spaced ones, rotated so the first one is at f = 5. The rotation absorbs the island's one-clock view lag.
- N = 64 (index 15) grants every slot, including f = 4. When a forward completes with its enable at end f = 4 (section 5.2), that enable takes the f = 4 grant and clears the completion credit (below), so the slot is never used twice. This is the N = 64 case of the credit rule. The C64U row 63 leaves a gap at f = 4, as v2 did.
- `grant_q` is one deep. Slots unused during forwards, barriers, BA halts, DMA or pause are lost, as U64 time slots are. The one exception is the completion credit below: one slot unused during a forward, ACKWAIT or DMA_WAIT is kept for that wait's completion enable.
- The grant rows, the forward cutoff (f = 30), the resume edges (`RESUME_PHASE`; end f = 4; f = 5 under F6), `IRQ_SAFE_PHASE`, `FIO_ACK_EDGES`, `FIO_DEPTH`, `VIC_READ_GAP`, `FIO_RAW_PRECISE`, `EDGE_STAGE` and the hold windows define the **turbo timing revision** that CAPS2 reports (section 6.1; Q9). The revision is per image: an image built with a release fallback that changes one of these values reports its own revision (section 2.13.2). A regression pins delivered slots and phases and fails on any change that does not bump the revision (section 10.2, M4-FW).
- FIO accesses use ordinary grants (section 2.13.4).
- **Speed contract** [I]: delivered CPU cycles per φ2 never exceed the label at any index, for any code (section 0.1).
- **Completion credit** `fwd_credit_q` (one bit, island) [I]. Below N = 64 no grant falls on f = 4, so a completion enable at end f = 4 with no grant term is one free enable per forward. At N = 1 (index 0, badline timing disabled) that would give 3 φ2 per unrolled `lda $de80` (1.33x) and 1.11-1.33x for I/O-heavy loops [I, C64_TURBO_MODELS.md (governor_credit.py)]. The credit charges each completion to a grant the CPU could not use:
  - *Set* by any grant pulse (`TABLE[N][fc] & frame_run_q`) while a forward, ACKWAIT or DMA_WAIT is outstanding, from the clock where the T65 presents the forward-class address up to the completion enable. A forward requested at the grant right after a completion (f = 5) credits that unused grant, so back-to-back forwards stay one φ2 apart.
  - *Used* by every completion enable that is not a granted local or FIO cycle: the end f = 4 forward completion (section 5.2), the ACKWAIT exit (sections 5.2, 5.8) and the DMA_WAIT exit (section 5.5). Each needs `fwd_credit_q | grant_q` (`cmpl_ok`, section 2.13.2) and clears the credit. With neither, it waits for the next grant and takes it. At N = 64 the completion clears the credit and takes the coincident f = 4 grant, still one enable per clock; the N = 64 rule above is this case.
  - *Cleared* also when `turbo_q`, N or the badline mode changes, so no credit crosses a mode switch. In exact mode it stays 0.
  - *N ≥ 2: no change.* A forward waits at least 38 fast clocks (section 5.9) and grants are at most 32 clocks apart, so the credit is always set at the completion edge. Every N ≥ 2 figure in this design, the `meas_c` match (section 1.7) and the section 8 table stay as they are. ACKWAIT and DMA_WAIT exits come after end f = 5, so a grant has set the credit by then at every N.
  - *N = 1:* a forward requested at f_r = 6-30 completes at the f = 5 grant, one clock after end f = 4. An unrolled `lda $de80` takes 4 φ2; `lda $dc01,x / dex / bne` takes 9 φ2, the same as the RAM loop. Over all periodic L/F patterns of length 1-10 the maximum is 1.000 CPU cycles per φ2 (2.000 without the credit) [I, C64_TURBO_MODELS.md (governor_credit.py)].
  - *Invariant:* each enable uses a distinct grant at or before it. Enables in any 64-clock window are therefore at most N + 1, and over any turbo period enables never exceed grants. The governor unit test checks both (section 10.2, M3).
  - *Rejected:* making each completion wait for its own grant at or after f = 4. It breaks the measured 6 MHz `meas_c` row (2 φ2 against a measured 1 [M]) and changes other rows: `lda $dc01,x / dex / bne` at index 2 from 3 to 4 φ2, `inc $de00` at index 1 from 4 to 5 φ2 [I, C64_TURBO_MODELS.md (governor_credit.py)].
  - Exact mode, stock REU behaviour and gate G-REU0 do not change: the credit gates only turbo completion enables.

### 2.8 Badline, DMA and mode gating

- **`turbo_q`** is an island-side mode register, updated only at enable boundaries with no forward in flight and an empty FIO queue (`all_done_q`), for every switch and every cause. A switch to exact does not wait for the NMI rule; the carry of section 2.9 finishes a pending rearm on the first exact enables. It gates port-A EN, port-A WE, `grant_q`, FIO issue and the late-acknowledge completion rule. In exact mode the island makes **no** port-A access and no FIO access.
- **Port A:**
  - EN = `turbo_q & state==RUN & grant_q & run_q & ~halt_q & ~ack_wait_q`. **All terms are registered**, as in v1. EN carries no live class decode, so the half-period path from the T65 address to every island BRAM EN pin does not grow (review finding 14).
  - WE = EN & `map_ok` & `fetch_done_q` & local-write class (RAM, PORTW, COL). Only WE uses the live class, as in v1; `map_ok` and `fetch_done_q` are registered inputs to the same final AND. The `fetch_done_q` term is ordering rule O3 (section 2.13.9): a local write waits, with no T65 enable, until it holds.
  - A cycle with EN = 1 and WE = 0 whose T65 enable is withheld (a stalled local write, an FIO or forward cycle) is a harmless read: port A is NO_CHANGE and the read data is not used.
- **Badline timing enabled, index ≥ 1:** `halt_q = ~ba_q`, where `ba_q` is `baLoc` registered into the island. It gates every enable, FIO included.
- **Badline timing disabled:** local and FIO cycles ignore BA.
- **Forwards.** A forwarded access owns the **shared bus** only if `baLoc` = 1 at e_15, because the frame shows the dummy's we = 0 at that edge. Forwarded writes never steal a badline c-access [V C64_TURBO_BUS_CONTRACT.md §5c risk 2; fpga64:530]. With badline timing disabled, an eligible forward that finds `baLoc` = 0 gets I/O-only ownership at e_16 instead and still completes in that cycle (section 2.13.8).
- **DMA:** a forwarded write's completion is withheld while `ext_dma_hold` = 1 at B1. The island stays in DMA_WAIT until a live sample of `ext_dma_hold` reads 0. This covers `dma_post_pending` and `dma_wait_vic` (fpga64:465-533).
- **RDY:** T65 `Rdy` = 1 in turbo, so the SHA/SHX `rdy_mod` quirks never fire there. Exact mode keeps them, because `Rdy` comes from B0.

### 2.9 IRQ and NMI

- **No blanket mask.** In turbo, `IRQ_n` is the native term registered once into the island and fed live to the T65 at every enable. `NMI_n` is the same kind of registered term, passed through the hold-and-rearm rule below.
- T65 samples IRQ/NMI only on Enable. For IRQ it keeps VICE's two-sample history (T65.vhd:753-789 [V]), so the NMOS polling rules hold in CPU cycles. For NMI it keeps one previous sample (below).
- **Late acknowledges.** An acknowledge that removes IRQ after the island's next enable could let that enable see a stale level and take an interrupt that stock would not take. Two mechanisms close this (section 5.8):
  - φ2 forwards in the acknowledge class (CIA ICR reads, `$DF00` reads, Ultimate Audio acknowledge writes) complete only after the island has registered a c64 marker toggled at e_`IRQ_SAFE_PHASE` (default 5).
  - Fast `$D019`/`$D01A` writes set `ack_wait_q`, which blocks every enable until the queue reports the write done, `FIO_ACK_EDGES` (default 2) c64 edges after it lands.
- This delays only those instructions (D13). It never masks IRQ, so polling loops take interrupts with normal latency.
- NMI is edge-detected in T65 [V T65.vhd:784-789]: every Enable stores `NMI_n` in `NMI_n_o`, and NMIAct is set only when `NMI_n_o` = 1 and `NMI_n` = 0. A high-then-low that lies wholly between two enables is therefore invisible to the T65. CIA2 holds NMI low until the ICR is read, but that does not make turbo halts safe: after the read the line can rise and fall again while the island has no enable (the CIA2 case below).
- **NMI hold and rearm** (Q9; the U64 fixed a lost NMI pulse in a badline, commit 57bfb741, C64_TURBO_U64_ISSUES.md §3(g)5; section 14.4 replaced the v3 set/clear latch `nmi_hold_q`) [I]:
  - Registers on `c64_cpu_clk`:
    - `nmi_seen_q`: the NMI value last shown to the T65 at an enable. It is updated at every enable in both modes; in steady exact mode it takes B0's NMI.
    - `fell_q`: set on any fast clock where the registered native NMI term falls; cleared at every turbo enable and every carry enable (below); held at 0 in steady exact mode.
    - `force_q`: a rearm is in progress.
    - `carry_q`: the carry (below) is running. It is set when turbo → exact is adopted, and cleared by the carry enable that ends the carry, when exact → turbo is adopted, and by RST1.
  - At each turbo enable, with `fell` = `fell_q` OR a fall on this same clock, the T65 is shown:
    1. 0 if `force_q` = 1, and `force_q` clears;
    2. else 0 if `fell` = 1 and `nmi_seen_q` = 1 (a held fall, as the v3 latch did);
    3. else 1 if `fell` = 1 and `nmi_seen_q` = 0, and `force_q` is set, so the next enable shows 0 and the T65 sees one high-to-low edge (the rearm);
    4. else the live registered term.
  - The enable's clear of `fell_q` wins over a fall on the same clock, because `fell` already counts that fall. So a fall is taken once, never twice. An enable that ignored a same-clock fall and kept it would take the NMI twice; one that ignored and dropped it would lose case 3 (model S5a, S5b).
  - Several falls before one enable, or a fall while `force_q` = 1, merge into one NMI. Stock merges them the same way while RDY holds the CPU, because NMIAct is one flag.
  - Cost: case 3 takes the NMI one T65 enable later than case 2.
  - Why case 3 is needed: the v3 latch (set on a fall; the T65 sees 0 until an enable samples it; then it clears) fails when the T65's last sample was low. The T65 sees low, then low, finds no edge, and the latch clears with the NMI lost. With no latch the same NMI is lost: the stall causes the loss, and the plain latch does not cover it [I, C64_TURBO_MODELS.md (nmi_rearm.py), cases S1, S1b].
  - **CIA2 trigger, reachable with badline timing enabled** (index ≥ 1; badline timing enabled is the menu default, section 1.4):
    - The handler's `LDA $DD0D` loads at e_15 of cycle X with BA high and retires at R(X). CIA2 releases `irq_n` at e_0 of X+1 [V mos6526.v:518, 524-526].
    - A refresh-cycle badline drops BA at the same e_0: `baChars` changes only on phi = 0 edges and vicCycle advances at e_15 [V VIC:654-668; fpga64:575-576, 711]. `halt_q` then withholds the completion enable (section 2.8) for up to about 40-43 cycles, while the T65's last NMI sample is still the low level from before the read.
    - A second NMI-enabled CIA2 source firing in that window pulls the line low again. So does a timer underflow on the acknowledge edge, which leaves the line high for exactly one cycle [V mos6526.v:216, 520-526].
    - With the v3 latch, or with none, that NMI is lost. CIA2 then holds the line low and no handler reads `$DD0D`, so every later CIA2 NMI is lost too [I]. Likely victims: Kernal RS-232 (timers A and B plus FLAG), double-timer NMI code, short-period NMI digis.
    - Stock takes this NMI: an NMOS 6510 edge-detects while RDY is low, and MiSTer exact mode retires every BA-held cycle [V multitini_c64_cpu_scheduler.vhd:40-43; T65.vhd:786]. With the rearm the model takes it once, as stock (S2, S2c).
    - With badline timing disabled the case does not occur: the ACKWAIT exit at about end f=13 of X+1 (section 5.2) samples the released line before the earliest re-assert at e_0 of X+2 (model S2', S2c').
  - A pulse of one c64 clock is seen, because the island registers the native term on every fast clock (X1). The rule sits on the island side, where "no enable" is known.
  - Short NMI pulses also come from RESTORE once it is wired (D12) and from M7 cartridges. A pulse after a low sample, or two pulses in one stall, hit the same gap; cases 2 and 3 cover them.
  - **Steady exact mode is unchanged:** outside the carry below, the T65 samples NMI from B0 at each retire, as stock; `fell_q` and `force_q` are held at 0; `nmi_seen_q` follows B0. With turbo never enabled (stock boot, Turbo Control Off) no carry starts, so `fell_q` and `force_q` stay 0 and the stock path is bit-identical. Stock REU behaviour and the Expand gate (G-REU0, section 10.3) do not change, because the rule acts only on turbo enables and carry enables.
  - **Mode switches** (section 2.11):
    - Turbo → exact is adopted at the normal switch boundary for every cause: the island's own enable boundary with no forward in flight and `all_done_q`. It does not wait for the NMI rule. Section 1.9 (no turbo local cycle after a forwarded `$DD00`/`$DD01` access that trips the guard), section 2.11 (a forced-exact period has no BA-ignoring local cycle, no FIO access and no I/O-only ownership) and their M3 assertions (section 10.2) therefore hold as written.
    - **The carry.** The switch does not clear `fell_q` or `force_q`, and `fell_q` keeps taking falls after the last turbo enable. The first exact enables after the switch (carry enables) take NMI from the rule instead of B0. At each carry enable, with `fell` = `fell_q` OR a fall on this same clock, the T65 is shown:
      1. 0 if `force_q` = 1, and `force_q` clears;
      2. else 0 if `fell` = 1 and `nmi_seen_q` = 1 (a held fall);
      3. else 1 if `fell` = 1 and `nmi_seen_q` = 0, and `force_q` is set, so the next exact enable shows 0 (the rearm);
      4. else B0.
    - Each carry enable clears `fell_q`. The carry ends after the first exact enable whose shown value equals B0 and that left `force_q` = 0. From then on `fell_q` and `force_q` are held at 0 and NMI comes from B0: steady exact mode is stock.
    - Timing: a carry enable mixes B0, sampled at R, with `fell`, taken from the registered term. Every native fall that B0 holds must reach `fell` by that enable's clock. The 2-clock lag of section 5.9 puts a fall at e_30 on the enable's own clock, where `fell` counts it. One more register on the fall path loses S11's NMI when its fall is at e_30, and makes the exact → turbo seam take such a fall twice [I]. Benches: a fall at e_30 and at e_31 before a carry enable, and before the exact enable that adopts turbo.
    - Length: with nothing pending, the first exact enable shows B0 and ends the carry, so NMI comes from B0 from the first exact enable, as with no carry. A forced 0 shown over a high line, or a rearm, takes two exact enables (model S6, S8). In the switch fuzz (seed 2), whose line changes at most once per native cycle at e_0, no carry took more than two exact enables; the same generator with seeds 5, 7 and 9 gives three, in S11's pattern. That generator lets the line rise without an ICR read, so it is not a CIA2 model; whether CIA2 alone can need a third carry enable is open. A carry runs longer only while the line changes again in the switch gap or the first exact cycles; each further fall adds a rearm (model S11; at most three exact enables in the forced-switch fuzz).
    - What the carry covers: a switch with `force_q` = 1 (a rearm in progress), where dropping `force_q` loses a short pulse (model S6); and a case-2 enable that showed a held 0 while the line was already high again, followed by a new fall before the first exact enable. Showing B0 there gives the T65 0, then 0, and loses the NMI (model S8); case 3 of the carry takes it.
    - Rejected: waiting in turbo for an enable that leaves `force_q` = 0 and showed the live registered term (the rule the carry replaced). It ran one or two more turbo enables after the switch was due (model S6, S9-S11), so after a forced-exact cause it breaks sections 1.9 and 2.11. Also rejected: ending the carry after at most two exact enables whatever B0 shows. A fall in the first exact cycle after a forced 0 shown over a high line is then lost (model S11).
    - Exact → turbo starts from `nmi_seen_q` (the last value shown). `fell_q` and `force_q` pass to the turbo rule unchanged. They are 0 unless a carry is still running; clearing them would lose a pending rearm (model S12). Starting from 1 would turn a later case 3 into case 2 and lose the NMI (model S7).
  - Rejected as a standalone fix: letting the completion enable of an already finished forward ignore `halt_q`. It closes only the CIA2 acknowledge case, not DMA_WAIT, ACKWAIT, pause or a BA halt that follows no forward. It may be added only together with the rearm.
  - Model [I, C64_TURBO_MODELS.md (nmi_rearm.py)]: turbo-only fuzz, 3,000 random waveforms with random stalls (fixed seed): the v3 latch misses 737 of 5,239 falls, the rearm misses 0, and neither adds an NMI without a fall; 733 falls take the one-enable-later path. Switch fuzz, 2,000 runs with random exact/turbo switches: the rearm with the carry misses 0 of 4,573 falls, adds none, runs no turbo enable while a switch to exact is due, and takes at most two carry enables; the replaced wait rule also misses 0 but runs 110 such turbo enables. Forced-switch fuzz, 3,000 runs with pulses on any clock before one forced switch to exact: the carry misses 0 of 7,541 falls and takes at most three carry enables; a carry that ends after two enables misses 4, and the wait rule runs 457 turbo enables while the switch is due.
  - [O] How the U64 handles a high-then-low after a low sample is not documented; C64_TURBO_U64_ISSUES.md §3(g)5 describes only a latch. E7 can run the CIA2 case on a C64U. This design follows stock either way.
  - Benches (T3; section 10.2, M3), at index 15 and index 1, badline timing enabled unless stated, with expected results:
    - a one-cycle NMI pulse after a high sample during a BA halt, during DMA_WAIT and during ACKWAIT: taken exactly once (case 2);
    - last sample low, then high-low and high-low-high during a BA halt, during DMA_WAIT and during ACKWAIT: taken exactly once, one enable after the stall ends (case 3);
    - CIA2 acknowledge plus BA halt: a held timer-A NMI, the handler's `LDA $DD0D` in the cycle before a refresh-cycle BA fall, timer B (or FLAG) firing during the halt: the second NMI is taken once after RTI, its handler reads `$DD0D`, and the line is high afterwards; the same with timer A underflowing on the acknowledge edge (one-cycle high): taken once; the same with badline timing disabled: taken once at the ACKWAIT exit, with no rearm;
    - a fall on the same fast clock as an enable, with the last sample high and with it low: taken once, never twice;
    - two falls in one BA halt: one NMI, as stock;
    - turbo → exact requested while `force_q` = 1, and while a case-2 enable shows a held 0 with the line high again and a new fall follows before the first exact B0: the switch happens at the next boundary, the carry takes each NMI once, and no carry enable makes a false edge (model S6, S8); the same followed by a fall in the first exact cycle: taken once, through a further rearm (S11); exact → turbo with B0 low (CIA2 holding) followed by the CIA2 case: taken once; exact → turbo while a carry holds `force_q` = 1: taken once, at the first turbo enable (S12);
    - a guard-tripping `$DD00` write whose completion enable is a case-3 enable (last sample low; a non-CIA2 short pulse, RESTORE or a cartridge, releases and re-asserts the line during the forward): the switch to exact happens at that completion enable, so the guard and forced-exact assertions of section 10.2 (M3) hold with zero turbo enables after it, and the NMI is taken exactly once, on the exact enables (S9); the same with a case-2 completion enable over a high line: taken once, no false edge (S9b);
    - a forced-exact cause (cartridge attach or loader start) while `force_q` = 1: the switch happens at the next boundary, the forced 0 is shown there or at the first carry enable, the NMI is taken exactly once, and the forced-exact assertions hold (S10);
    - a held CIA2 NMI with no stall: taken once;
    - fuzz: random NMI waveforms, stalls (forward, BA halt, DMA_WAIT, ACKWAIT, pause), mode switches and guard trips, against a reference that compresses the native NMI history per enable window: every fall taken by the first or second enable after it, no NMI without a fall, and no turbo enable while a switch to exact is due. In steady exact mode the reference is stock (one sample per native cycle at R, none under DMA): a pulse that lies between two R samples is lost there, as in stock. The model avoids the case by changing the line only at e_0, at most once per native cycle, wherever exact mode can run.
- During DMA_WAIT and BA halts the T65 samples nothing. Stock MiSTer also samples nothing under `dma_active` (fpga64:989), so steady exact mode loses a high-then-low during REU DMA, as stock MiSTer does today (a carry still running takes it); turbo takes it through case 3. Stock does sample in every BA-held cycle (multitini_c64_cpu_scheduler.vhd:40-43 [V]), which is why the BA-halt case needs the rearm.

### 2.10 Stock (exact) mode and why it is cycle-exact

Each exact cycle (section 5.1):
- B0 is captured at the native retire edge R when `ext_cpu_enable` = 1 and an exact request is presented. This includes RDY-held reads (B0.rdy = 0).
- The island T65 gets one enable at end f=0 with DI/IRQ/NMI/Rdy = B0. The one exception is NMI on the carry enables after a turbo → exact switch (section 2.9); with turbo never enabled no carry starts.
- Its next request is registered at end f=1 and loaded into the frame at e_1, two c64 clocks later than today.

Equivalence argument [I, checked against the RTL by two reviewers]:
1. T65 state changes only on `Clk` with `Enable`. The exceptions are reset and SO, and SO is tied to 1. BusStart is unused [V T65.vhd:144-151, 391, 490, 575, 731; cpu_6510.vhd:84].
2. Every island enable sees exactly the inputs today's T65 sampled at the same R.
3. Nothing native consumes `cpuAddr_pre/Do/We/IO` in phases 0-14, so the dummy that the frame shows from a retiring R until the next load at e_1 is never used:
   - `cpuHasBus` is cleared at e_31 and decided at e_15 (fpga64:530, 556-557).
   - Buslogic uses `vicAddr` and decodes no CPU chip selects while `cpuHasBus` = 0 and `cpuIoOwn` = 0 (buslogic:216, 298-324). `cpuIoOwn` is never set in exact mode, because `frame.ioonly` = 0.
   - The VIC latches the CPU bus only while phi = 1 (VIC:300-309).
   - CIA and SID act only with chip select.
   - The DMA peek release is in CPU0-CPUB (fpga64:547-553).
4. The port value used for bankSwitch reaches the frame at e_1. bankSwitch is consumed only when `cpuHasBus` = 1 (phases 16-31) or by DMA at phase 28. `cass_motor`/`cass_write` are unconnected (adapter:553) [V].
5. The island's port decay tick arrives at end f=2, after the enable at end f=0. The order "enable(k), tick, enable(k+1)" holds (cpu_6510.vhd:103; fpga64:1006).
6. `nmi_ack` goes directly from the T65 to the cartridge. It changes after end f=0 and is sampled at e_0, the same edge as today (cartridge.sv:130, 155).
7. DMA and `memory_wait` withhold `ext_cpu_enable`, so no B0 is produced and the island waits.
8. Reset start-up:
   - `reset` changes at R (fpga64:433-440). The island and proxy receive this same signal through the new `ext_reset` port (section 2.3), so the release is phase-aligned as today.
   - The island releases `Res_n` at end f=0. T65 needs 2 clocks (T65.vhd:355-364), done by end f=2.
   - The island's reset-state request is presented from reset onward, so the first native slot sees the same address as today.
9. The fast ports are idle in exact mode: the queue is empty (the switch to exact waits for it), no FIO strobe fires, and the VIC/SID fast blocks only act on those strobes. Their read muxes have no side effect.

The remaining risk is implementation error. M2 retires it with a consumer-level lockstep against `EXTERNAL_CPU=false`, with `FAST_IO=true` present and idle, including every forced-exact cause (section 10).

### 2.11 Mode switching and policy

The c64 domain computes `mode_live` (exact or fast), N and `badline_en`. Exact is forced when any of these holds:
- effective Turbo Control is Off;
- the effective index is 0 with badline timing enabled;
- the reset window is open;
- a cartridge is loaded or loading (until M7, or with the option);
- the content loader is not IDLE (so the `$FFCF` check at content_loader.sv:503 sees native fetches);
- disk auto-slow is on and the guard is blocking.

The blocker term reaches B1 combinationally.

**Forced exact wins over `$D031` bit 7** (Q9). Every cause in the list above yields exact mode whatever bit 7 says, so a forced-exact period has no BA-ignoring local cycle, no FIO access and no I/O-only ownership; the stored bit reads back (section 1.11). "Index 0 with badline timing disabled" is a 1-slot turbo only while no forced-exact cause holds. turbo_regs reports each cause in STATUS bits 16-20, and the disk cause in bits [7:6] (section 6.1).

Switching rules [I]:
- The island adopts a mode, N or badline change only at its own enable boundary with no forward in flight and `all_done_q` (FIO queue empty and settled). A change to exact uses this boundary for every cause and does not wait for the NMI rule; the carry finishes a pending NMI rearm or held fall on the first exact enables (section 2.9). Every request carries an exact/turbo flag.
- Exact → turbo: the last exact request retires at R(k), and the frame shows the dummy from that edge (section 2.6). The island adopts turbo at its enable at end f=0; the proxy does not load turbo-flagged requests at e_1-e_14. So at e_15(k+1) the frame shows the dummy's we = 0, `cpuWe_pre` = 0 holds at every turbo e_15, and nothing runs twice. The first FIO access waits for `busy_q` = 0 (the VIC's native write latch), at most until e_16 of the cycle after the last exact VIC write.
- Turbo → exact: the next request is exact-flagged and loads at the next e_1-e_14 edge, normally in the same native cycle. Every queued fast write has landed before it.
- Any bus-cycle boundary is valid, because T65 state is per cycle.

### 2.12 Pause, loader, reset

- **Turbo:** `run_q = ~(pause_request | prg_install_pause | pause_out)`, registered into the island. The island halts at the next boundary, and port-A EN and WE drop. A forward already loaded, including an I/O-only one, completes when the native frames resume; a loaded forward always retires before the phase 7/8 pause point (bus reviewer). The FIO queue keeps draining during pause (section 2.13.5, rule 6).
- **Exact:** native frames freeze, so no B0 arrives.
- **Loader:** forces exact mode, so its port-B writes never meet island accesses.
- **Reset:** section 3.3.

### 2.13 Fast internal I/O path (first release; Q1)

This replaces the v1 sketch. It merges three sub-designs (C64_TURBO_FAST_VIC.md, C64_TURBO_FAST_SID.md, C64_TURBO_BA_LOW_IO.md) into one mechanism.

#### 2.13.1 What it does, and how the sub-designs were reconciled

- **Reads** of VIC and SID registers are "2-clock reads" from a combinational copy of each chip's read mux, captured into the island register `io2_q`. Cost: equal to RAM at indices 0-12; about one extra slot at 13-15.
- **Writes** (and read side effects) go into one ordered 4-entry FIFO in the island. The c64 side lands one entry per c64 edge, 3-4 fast clocks after the CPU write.
- **Barriers** keep program order across classes.
- **I/O-only ownership** lets φ2 forwards to internal targets complete in BA-low cycles with badline timing disabled.

Conflicts between the sub-designs, and the resolution [I]:

| # | Conflict | Resolution | Reason |
|---|---|---|---|
| CF1 | Write path: VIC posts writes into a 4-entry FIFO; SID executes each access non-posted at the first c64 edge (3-4 fast clocks per access); the ordering design allowed any depth | **One posted FIFO for both targets.** SID writes land from the FIFO; the SID's per-landing rules (latch, edge stage) apply at the landing edge | VIC writes must cost 1 slot: with 2 slots per write the #203 pattern drifts 0.33 pixel per pair instead of 0.02 [I C64_TURBO_MODELS.md (vicpath_model.py)]. One stream gives one ordering mechanism. The SID design's objections to posting (latch, read-after-write, mode switches, DMA) are covered by the barriers the VIC already needs |
| CF2 | Read path: VIC uses the island-side 2-clock read; SID rejected an island-side read because it assumed a half-period path from SID flops into the island | **Shared 2-clock read for both targets.** The SID's `$19-$1C` latch update becomes a queue entry (LATCH), like the VIC's collision clear. Side-effect entries stay out of the read barrier except for the reads they can change (section 2.13.4) | The 2-clock read uses only full-period paths (X8a/X8b, section 3.2). SID read cost drops from 0.095 to 0.079 φ2 at N = 63 (0.078 at N = 64); the C64U measures 0.080-0.085 [M]. (With LATCH entries inside a global barrier, as first drafted, each unrolled `$D419` read waited for the previous LATCH: 0.125 φ2, review finding 1) |
| CF3 | Landing during pause and memory waits: VIC drains on every c64 edge; the ordering design applied entries only on `frame_run` edges | **Drain on every c64 edge** | Holds are phase-based and the frozen pause phase (EXT4, position 8 [V fpga64:196-202, 404]) is outside both hold windows. Memory waits need a cartridge, which forces exact mode. Draining empties the queue during pause, which removes hazard H10 outright. SID edge stages wait for `ce_1m`, which stops with the frame |
| CF4 | IRQ acknowledge: VIC counts the write done one edge after landing (about 7 fast clocks); the ordering design used a separate toggle two edges after landing | **One `fio_done` counter.** Acknowledge-class entries count done `FIO_ACK_EDGES` c64 edges after landing, default 2 | One mechanism. Default 2 keeps one edge of margin over the VIC's derived edge (IRST clears one edge after landing); E4/T7 may lower it to 1. Cost: about 2 fast clocks per acknowledge |
| CF5 | Local writes after queued VIC writes: VIC had no rule; the ordering design (O3) made local writes wait for any VIC entry, or for the whole queue | **Wait only for fetch-affecting VIC entries** (`$D011`, `$D015`, `$D017`, `$D018`, sprite Y) | Those registers change what the VIC fetches from RAM; a reordered RAM write can show new data at an old address (H3). Colour and other pixel registers do not change fetches. SID entries share the queue now, so "wait for the whole queue" would slow every music player |
| CF6 | Generic names: `FAST_FIO` (ordering), `FAST_IO` (VIC, fpga64), `FAST_PORT => EXTERNAL_CPU` (SID) | fpga64 `FAST_IO` drives both chips' `FAST_PORT`; island `FAST_VIC_IO` and `FAST_SID_IO` route each target | The φ2-only build must stay buildable per target |
| CF7 | Deviation D14 meant different things in the SID and ordering designs | D14 = fast-I/O micro-timing; D15 = SID edge stage (a deviation only if E7-S shows the U64 drops the pulses); D16 = REU DMA vs badlines (permanent for native-slot DMA by DL3, section 1.12) | One table |
| CF8 | SID variant V-C (raw T65 address into a c64 flop) | Dropped | The shared read path already uses the raw address, into a fast flop |
| CF9 | `$FF00`: the ordering design recommended a local write plus snoop forward; v1 forwarded every `$FF00` write on the RAM bus | **Adopt the snoop forward** (`FF00_SNOOP=1`) | Q1 requires D5 to disappear from what ships; the RAM-bus forward waits for BA = 1 |
| CF10 | The VIC loop table used a per-register read-after-write barrier, while the design ships the global one | The design reports global write-barrier numbers (raster-bar loop 16.0 fast clocks at index 15, not the precise 14.0, or 14.2 at N = 63) | Numbers must match the shipped default |
| CF11 | The SID design sent FIO through `isl_x_req.fio`, `seq` and B0 | Dropped | FIO never touches the proxy, frame register or B0 |

Unresolved: none of the above remains open. New questions it raises are in section 11 (the fetch-affecting set, `FIO_ACK_EDGES`).

#### 2.13.2 Island FIO logic

**Enable conditions** [I]. All terms are registered except the class decode, as in section 5.7:
```
base        = turbo_q & RUN & grant_q & run_q & ~halt_q & map_ok & ~ack_wait_q
fio_rd_ok   = base & ~en_q & ~wr_pend_q & dep_ok & ~busy_q   // 2-clock read + write barrier + side-effect rule
              & (~may_push | ~full_q)                      // a read that may push needs a free slot
fio_wr_ok   = base & ~full_q & ~busy_q                       // posted write
local_wr_ok = base & fetch_done_q                            // ordering rule O3
fwd_issue   = (forward class) & all_done_q                   // ordering rule O2
cmpl_ok     = (fwd_credit_q | grant_q) & ~halt_q & ~ack_wait_q // completion enables: end f=4, ACKWAIT exit, DMA_WAIT exit (2.7)
dep_ok      = VIC $1E: ~clr_m2m_pend_q | VIC $1F: ~clr_m2d_pend_q
            | SID $00-$18, $1D-$1F (write-only, bus latch): ~latch_pend_q | any other register: 1
may_push    = VIC $1E/$1F or SID $19-$1C (any mirror)
```
- `en_q`: the T65 was enabled on the previous edge.
- `busy_q`: the VIC's native latch `we_r`, registered (covers a native VIC write's late strobes, section 2.13.5).
- FIO issue includes `~halt_q`, so with badline timing enabled no FIO access runs while BA = 0.
- Routing: an FIO address whose target is built slow (`FAST_VIC_IO=0` for A10 = 0, `FAST_SID_IO=0` for A10 = 1) is a FWD_IO forward instead.
- Completion enables (the end f=4 forward completion, the ACKWAIT exit and the DMA_WAIT exit) use `cmpl_ok`, not `base`: they need the completion credit or a coincident grant, and each clears `fwd_credit_q` (section 2.7).

**Barrier flags: registered from the post-push state** [I]. Every flag is computed from the FIFO contents and `wp` *after* this fast edge's push, and from the c64 counter `fio_done` compared directly into the flag flop (crossing X7). A flag is therefore never stale on the clock after a push. `fio_done` only increases, so its one-clock crossing lag can only hold a flag longer, never release it early. The island no longer needs `fio_rp`.
```
push       = T65 enable that stores an entry (FIO write, or a read with a side effect)
wp_n       = wp + push                                   // write pointer after this edge
pend_n(s)  = slot s lies in [fio_done, wp_n)             // 3-bit pointers, 2-bit slot index
all_done_q     <= (fio_done == wp_n)                     // 0 on every push edge
wr_pend_q      <= OR_s pend_n(s) & kind_n(s) == WR
clr_m2m_pend_q <= OR_s pend_n(s) & kind_n(s) == CLR_M2M  // likewise clr_m2d_pend_q, latch_pend_q
fetch_done_q   <= ~(OR_s pend_n(s) & fa_n(s))            // fa = fetch-affecting VIC WR entry (O3)
full_q         <= (wp_n - fio_done) >= FIO_DEPTH          // a slot is free only once its entry is done
ack_end        <= wp_n                     on an acknowledge-class push
ack_wait_q     <= ack_push | (ack_wait_q & (fio_done != ack_end))
```
- `kind_n`/`fa_n` include the entry being written at this edge.
- `full_q` counts entries up to done, not up to acceptance, so a slot is never rewritten while its entry is still pending and the per-slot kind bits stay valid. At depths 2 and 4 this costs nothing in any modelled loop; at depth 1 (fuzz only) an RMW's final write waits for the dummy write to be done [I C64_TURBO_MODELS.md (final_model.py), variant].
- `ack_wait_q` gates the local, FIO and forward-completion enables (`base` and `cmpl_ok`), so no T65 cycle of any class runs during the wait and no entry can be pushed behind an acknowledge entry. `ack_end` is therefore always equal to `wp` while the wait lasts. The exact-equality compare is wrap-safe because pointers are one bit wider than the slot index and at most `FIO_DEPTH` entries are outstanding.
- Review findings 7 and 13: the first v2 draft registered `fio_done == wp` from the pre-push `wp`, so `all_done_q` stayed 1 for one clock after a push. `ack_wait_q` ("cleared by `all_done_q`") then dropped one clock after it was set, which reopened the `LSR $D019 ; RTI` re-entry (H7); `full_q` lagged the same way and could overflow at depth 1 or with `FIO_RAW_PRECISE`.
- Cost: a 4-slot OR per flag, about 20 LUTs (section 4.3).

**FIFO** (`isl_x_fio_mem`, island, fast clock). Depth `FIO_DEPTH` = 4; pointers are one bit wider than the index (3 bits at depth 4). Each entry is 22 bits:

| Field | Bits | Values |
|---|---|---|
| tgt | 1 | VIC (A10 = 0), SID (A10 = 1) |
| kind | 2 | WR; CLR_M2M; CLR_M2D (VIC read side effects); LATCH (SID read side effect) |
| fa | 1 | fetch-affecting VIC WR entry (O3); island-side only, not sent to the c64 domain |
| A9..A0 | 10 | address within the 1 KiB chip window |
| D | 8 | write data; the byte returned by a SID `$19-$1C` read (LATCH); the bits returned by a `$1E`/`$1F` read (CLR mask) |

**Generics** (all with today's behaviour available):

| Generic | Default | Meaning / fallback |
|---|---|---|
| `FAST_VIC_IO` (island) | 1 | 0: VIC registers forwarded at φ2 (D1 for the VIC) |
| `FAST_SID_IO` (island) | 1 | 0: SID registers forwarded at φ2 (D1 for the SID) |
| `FAST_IO` (fpga64) | true in the adapter | false: no fast ports, no I/O-only (today's netlist) |
| `VIC_READ_GAP` | 2 | 3: registered read address if E1 fails on the read path; a release fallback (0.094 φ2 per read at index 15, 0.125 at index 14, +25% at index 12) [I C64_TURBO_MODELS.md (vicread_model.py)] |
| `FIO_RAW_PRECISE` | 0 | 1: per-register read-after-write wait in place of `~wr_pend_q` (table in 2.13.9); only if E7-V2 shows the C64U has no penalty. The side-effect rule (`dep_ok`) and the free-slot terms stay |
| `FIO_DEPTH` | 4 | 1-8. Depth 1 is legal because `full_q` is registered from the post-push pointer; it serves the ordering fuzz |
| `FIO_ACK_EDGES` | 2 | 1-3, swept in E4 |
| `BA_LOW_IO` (proxy) | 1 | 0: D5 (diagnostic builds only) |
| `FF00_SNOOP` (island) | 1 | 0: `$FF00` forwarded on the RAM bus (D5r) |
| `EDGE_STAGE` (sid_top) | 1 | 0: strict level sampling (probable U64 behaviour). The default 1 is design decision DL6 |

The φ2-only engine of milestone M3 is `FAST_VIC_IO = FAST_SID_IO = 0`, `BA_LOW_IO = 0`, `FF00_SNOOP = 0`. It stays buildable as a diagnostic fallback and is never released.

**Release defaults, release fallbacks and diagnostic settings** (Q14, Q16, Q19) [I]. Every turbo build setting is in one of three sets:
- *Release defaults:* the defaults in this table, plus `FAST_COLOUR_RAM` = 1, `FORWARD_PORT_WRITES` = 0, `RESUME_PHASE` = 1, `IRQ_SAFE_PHASE` = 5 and `C64_ISLAND_CYCLE` = 1.
- *Release fallbacks:* allowed in a shipped image when that image's timing ladder (R2, section 13.4) or a settled experiment needs them:
  - `VIC_READ_GAP` = 3;
  - F7: `FAST_COLOUR_RAM` = 0, `FORWARD_PORT_WRITES` = 1;
  - `RESUME_PHASE`, `IRQ_SAFE_PHASE` and `FIO_ACK_EDGES` at values that E4/T7 validate;
  - `FIO_RAW_PRECISE` = 1, only after E7-V2;
  - `EDGE_STAGE` = 0, only if the user overrides DL6;
  - F4 `FAST_NMOS_PATHS`, only in both images together and only after the R13 suites pass.

  Each one shows in CAPS (bits 22 and 30 for F7) or changes the image's timing revision (section 2.7), so a board log names it. A difference between the PAL and NTSC images is listed in the release notes (section 13.2).
- *Diagnostic settings,* never released: `C64_ISLAND_CYCLE` = 2 (F6); `FAST_VIC_IO` = 0, `FAST_SID_IO` = 0, `BA_LOW_IO` = 0 or `FF00_SNOOP` = 0 (they bring back D1, D5 or D5r, which Q1 removed from what ships); `FIO_DEPTH` ≠ 4 (ordering fuzz only); `FAST_IO` = false; and Ultimate Audio's `UA_COHERENCE_PROBE` = 1 (its optional G9 probe, which also decodes register 0x0FC). Only `MULTITINI_C64_DIAGNOSTIC_BUILD` = 1 produces them. Such an image sets CAPS2 bit 14, and the release packaging refuses it (section 6.5).
- *Ultimate Audio build parameters* (Q17: they ship in the same C64 image): `UA_MIN_ISSUE_GAP` = 32, `UA_YIELD_TO_REU` = 0, `UA_POST_B_HOLD` = 16 and `UA_PERIOD_OFFSET_HALF_TICKS` = 3 are release defaults. A different value ships only after the experiment docs/C64_ULTIMATE_AUDIO_DESIGN.md names for that parameter (its O7/R1, O2/R2 and O1/R4). The build-input capture records the values, and TR4 checks them like the turbo settings.

#### 2.13.3 Classes after this change

| Access (I/O visible) | Exact mode | Turbo, badline timing enabled | Turbo, badline timing disabled |
|---|---|---|---|
| `$D000-$D3FF` VIC (all 16 mirrors) | native slot | FIO; island halts while BA = 0 | FIO; runs during BA low |
| `$D400-$D7FF` SID (both internal chips, every `sid_mode`) | native slot | FIO; island halts while BA = 0 | FIO; runs during BA low |
| `$DC00-$DDFF` CIA1/CIA2 | native slot | φ2 forward, BA-high cycles only | φ2 forward; I/O-only in BA-low cycles |
| `$DE00-$DFFF` without a cartridge (REU, `$DE00`/`$DF00` SID in modes 1/4, Ultimate Audio `$DF20-$DFFF` when on, open bus) | native slot | φ2 forward, BA-high cycles only | φ2 forward; I/O-only in BA-low cycles |
| `$FF00` write | native slot | local write + φ2 snoop, BA-high cycles only | local write + φ2 snoop; I/O-only in BA-low cycles |
| REU DMA, including DMA to VIC/SID registers | native | native, CPU halted | native, CPU halted, stock BA rules |

- With Ultimate Audio on, the `$DE00-$DFFF` row covers the UA registers at `$DF20-$DFFF`. With it off, `$DF20-$DFFF` holds the REU mirrors (REU on) or open bus (`lastVicData`). The island map holds no UA state, so the class is FWD_IO in every UA state.
- No CPU access to `$DF00-$DFFF` is ever FIO or posted, and the pacing rule of section 1.7 holds in every mode. REU DMA keeps the stock BA rules in every mode (D16, DL3) and never reaches the UA registers (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.1, §2.2).

#### 2.13.4 Fast reads (2-clock read)

**Read muxes** (c64 domain, combinational, addressed by the island T65's live address):
- VIC: `fast_rd_data`, a copy of `readRegisters` (VIC:1771-1823) indexed by A5..A0 [V structure]. One difference: `$1C` returns MCDelay instead of MC. MC equals MCDelay except during the one pixel after a write (VIC:1306-1313); a stock read always comes at least 16 phases after the e_3 landing, so it never sees the difference [I]. `$D02F-$D03F` read `$FF`; `$D030` reads `$FF` with `turbo_en` = 0 [V VIC:1767-1826; fpga64:1077; adapter:513].
- SID: per chip, `(a in $19-$1C) ? live(POTX, POTY, OSC3, ENV3) : bus_data[i]`, then the stock chip choice (`data_out = cs[0] ? bus_data[0] : bus_data[N-1]`, sid_top.sv:344) using the `sid_mode` decode of section 2.13.7 [V sid_top.sv:201-202, 231-238].

**Final mux in the island** [I]:
```
fio_rd  = u64_hit ? u64_byte : (A10 ? sid_rd : vic_rd)
u64_hit = ~A10 & ((A9..A0 = $031 & d031_live) | (A9..A0 = $030 & d030_live))
io2_q  <= col_sel ? {lastVicDi_q[7:4], col_q[3:0]} : fio_rd       // every fast rising edge
```
- `io2_q` is shared with the 2-slot colour RAM read (section 4.1). The T65 DI mux therefore keeps six sources.
- Capture is unconditional. No side effect depends on it.

**Read timeline**

| Edge | Action |
|---|---|
| end of clock X−2 or earlier | previous T65 enable; the FIO read address appears |
| end of X−1 | `io2_q` ← `fio_rd`(A). Chip state is sampled here, pre-edge |
| end of X | T65 enable with DI = `io2_q` if `fio_rd_ok`. A side-effect entry is pushed if needed (D = `io2_q`) |

**Read side effects** (pushed only by the completing enable; at most one per read bus cycle, dummy reads and RMW reads included):
- VIC `$1E`/`$1F` (any mirror), **only when the returned byte D is nonzero**: an entry {VIC, CLR_M2M or CLR_M2D, D}. At landing the VIC clears **only the reported bits**, applied to the value the stock code assigns on that edge (so a coincident pixel edge's new collisions are kept):
  - `$1E`: `M2M <= M2M_next and not D`; `M2Mhit <= '0'` (re-armed, as VIC:1525-1528).
  - `$1F`: `M2D <= M2D_next and not D`; `M2DDelay <= M2DDelay_next and not D`; `M2Dhit <= '0'` (as VIC:1561-1566).
  - A collision set between the sample (end of X−1) and the clear survives unless it re-sets a bit already reported, so the next read sees it. No bit is reported twice for one event: a second read of the same register waits for the pending clear (`dep_ok`).
  - With D = 0 nothing is pushed. Then M2M = 0 at the sample, which implies M2Mhit = 0 (M2Mhit is set only while M2M ≠ 0 and cleared only by a clear) [V VIC:1515-1528]; a clear would change nothing except re-arming the hit flag, which could raise IMMC twice for one event. The same holds for M2D/M2Dhit (VIC:1547-1558).
  - A re-armed hit flag may raise IMMC/IMBC again for a surviving bit. The IRQ flag is sticky, so this matters only if software acknowledged it within the few fast clocks of the window [I].
  - Review finding 2: the first v2 draft cleared every bit at landing. Its blind window (end of X−1 to C2) held 3 of the 4 c64 edges of each 8-fast-clock pixel period. A barrier-limited `LDA $D01E / BEQ` loop at index 15 locks to 8 fast clocks with the visible edge almost never on a pixel edge, so it could spin forever on a persistent overlap [I C64_TURBO_MODELS.md (rev_collision_alias.py): visible fraction 0.00-0.01]. Stock loses about 9 pixel edges after a read (VIC:1475-1486), but a 1 MHz CPU reads at most once per 32 pixels.
  - The fast clear is a single event without stock's multi-pixel `M2MClr` tail [I].
- SID `$x19-$x1C` (any chip mapping): an entry {SID, LATCH, A, D = returned byte}. At landing, the chip that supplied D loads D into `bus_data`; another selected chip (the right chip in `sid_mode` 0) loads its own live value. This equals the stock end state, where the latch holds the value read [V sid_top.sv:231-238; SF17].

**Which reads wait for which entries** (`dep_ok`, section 2.13.2) [I]:
- Every FIO read waits until no WR entry is pending (`~wr_pend_q`, the write barrier, ordering rule O2).
- A side-effect entry changes only some reads, and only those wait for it:
  - CLR_M2M changes only `$1E` reads; CLR_M2D only `$1F` reads.
  - LATCH changes only reads of SID write-only registers (`$00-$18`, `$1D-$1F`), which return `bus_data` [V sid_top.sv:200-202, 231-238, 344]. To keep the logic small, such a read waits for any pending LATCH, of either chip.
  - A `$19-$1C` read returns live POTX/POTY/OSC3/ENV3 and waits for no side-effect entry.
- Forwards, mode adoption and the acknowledge wait still wait for every entry (`all_done_q`).
- A read that may push (`$1E`, `$1F`, `$19-$1C`) also needs `~full_q`, so a side-effect push always has a free slot without relying on the queue being empty.
- Back-to-back `LDA $D01E / LDA $D01E` with a collision present: the second read waits for the first clear, so it cannot report the same bits twice. With no collision present nothing is pushed and nothing waits.

**Cost model** (C64_TURBO_MODELS.md (vicread_model.py, lead_model.py, final_model.py, top_rows_model.py); Bresenham grants, first grant at f = 5; all 64 slots at N = 64; a gap at f = 4 only in the C64U row 63) [I]. `$1E`/`$1F` return M2M/M2D (VIC:1804-1805) [V]:

| Index (N) | RAM φ2 per unrolled `lda` | VIC/SID φ2 per unrolled `lda` | Extra slots per read |
|---|---|---|---|
| 0-12 (1-32) | 4/N | equal to RAM | 0 |
| 13 (40) | 0.1000 | 0.1250 | 1.00 |
| 14 (48) | 0.0833 | 0.0937 | 0.50 |
| 15 (64) | 0.0625 | 0.0781 | 1.00 |
| C64U rows 47 / 63 (`c64u_top` = 1) | 0.0851 / 0.0635 | 0.1000 / 0.0790 | 0.70 / 0.97 |
| C64U index 15 [M amiga64_HARDWARE.md:1301-1313] | 0.064 | `$D012` 0.080, `$D41A` 0.080, `$D419` 0.085 | about 1 |

- The VIC/SID column holds for `$D012`, for `$D419`/`$D41A`/`$D41B` (which push LATCH entries that no later `$19-$1C` read waits for), and for `$D01E`/`$D01F` when no collision is present [I C64_TURBO_MODELS.md (final_model.py)].
- `$D01E`/`$D01F` with a collision present: each read waits for the previous clear, 0.125 φ2 at indices 13-15 (equal to RAM at 0-12).
- A write-only SID register read right after a `$19-$1C` read waits for its LATCH entry: 0.188 φ2 per pair at index 15 instead of 0.156 (0.190 instead of 0.158 with the C64U row).
- The rule matches the C64U shape: indices 0-12 cost the same as RAM, and 13-15 cost 12-25% more in the model, against 15-30% measured on the C64U [M amiga64:1311].
- A 3-clock read (`VIC_READ_GAP` = 3) gives 0.094 φ2 at N = 64, 0.125 at N = 48 (+33%) and +25% at index 12 [I C64_TURBO_MODELS.md (vicread_model.py)], which departs from the C64U data. The 2-clock read is therefore the default; the 3-clock read is only a timing fallback (section 2.13.2).
- In a 48-slot U64E frame the model gives 32 MHz equal (measured 2062/2064) and a 48 MHz ratio of 1.237 (measured 1.257) [M gist839/meas_c_external.json]. At 40 MHz the model gives 1.083 against a measured 1.22; the U64E's 39-of-48 pattern is evidently not Bresenham and is not our target.
- **Slot accounting.** A read uses one grant; when the previous clock had an enable, it also forfeits that grant. Grants are adjacent only for N ≥ 33, hence 0 extra at indices ≤ 12 and +0.5 to +1 at 13-15. The C64U's "+1 slot" falls out of this rule. The C64U reserves a VIC slot (the gap at f = 4); Multitini needs none and grants it at N = 64 (D17) [V #203; #874 comments; C64_TURBO_FAST_VIC.md §3.11].
- **Live sampling.** A read returns the register value at the end of X−1, at any phase. A `$D012` read can see a new line up to 0.25 µs earlier than a stock read in the same native cycle. SID OSC3/ENV3 change only at e_0, so a fast read sees the same cycle's value as a stock read anywhere in phases 1-31 [V SF5-SF6]. Inherent to a turbo CPU [I].

#### 2.13.5 Fast writes and the pop stage

**Island push.** At the T65 enable of a FIO write (posted, 1 slot), the island stores {tgt, WR, fa, A9..A0, DO} in `isl_x_fio_mem[wp]` and increments `wp`. A and DO are T65 outputs that have been stable since the previous enable. All barrier flags see the push on the next clock (section 2.13.2).

**Pop stage** (`multitini_c64_fio_port.sv`, c64_clk):
```
head    = isl_x_fio_mem[fio_rp[1:0]]
spr     = head.a[5:0] in {01,03,05,07,09,0B,0D,0F,15}
held    = head.tgt==VIC & head.kind==WR &
          ((head.a[5:0]==6'h11 & vic_hold_d011) | (spr & vic_hold_spr))
accept  = (fio_rp != isl_x_fio_wp) & ~vic_busy & ~held & ~reset
ack     = head.tgt==VIC & head.kind==WR & head.a[5:0] in {19,1A}
on each c64 edge:
  vic strobes  <= accept & tgt==VIC  (wr, wa=a[5:0], wd=d (data or clear mask), clr_m2m, clr_m2d)
  sid strobes  <= accept & tgt==SID  (wr, latch, a=a[9:0], d)
  u64_wr       <= accept & tgt==VIC & kind==WR;  u64_a <= a[9:0]; u64_d <= d
  done pipe    <= accept; an entry is done at the later of (landing + 1 c64 edge,
                  or + FIO_ACK_EDGES if ack) and (previous entry's done edge + 1)
  fio_done     <= fio_done + (pipe output, 0 or 1);  fio_rp <= fio_rp + accept
```
- `vic_hold_spr` (from the VIC) is high at acceptance phases 14..29 of the sprite-0 `cycleSpriteA` cycle **and at acceptance phase 13 while `vicCycle = cycleCalcSprites`** (rule 5).
- The done pipe is strictly in order and adds at most one per edge, so `fio_done` never skips an entry. Because `ack_wait_q` stops all pushes behind an acknowledge entry, the "previous entry + 1" term never binds in the shipped design; it keeps `FIO_ACK_EDGES` = 3 and fallback builds safe (review finding 7). Assertion A7 checks it.

**Write timeline**

| Edge | Domain | Action |
|---|---|---|
| end of t (T65 enable) | fast | write complete for the CPU; entry pushed |
| C1 = first c64 edge after t (t+1 if t is even, t+2 if t is odd) | c64 | accepted if not busy or held; strobes registered |
| C2 = C1 + 1 c64 clock | c64 | **landing**: VIC register(s) or SID register file take the value; turbo_regs snoop |
| C3 = C2 + 1 c64 clock (ack class: C2 + `FIO_ACK_EDGES`) | c64 | pulse effects settled (`$D019` → IRST/IMMC/IMBC/ILP cleared); `fio_done`++ |
| done + 1 fast | fast | island registers `done`; `io2_q` samples settled state |
| done + 2 fast | fast | earliest dependent enable |

- Landing latency is 3-4 fast clocks when nothing is held. A pixel is 8 fast clocks.
- Throughput: pops run at one per c64 edge (2 fast clocks). The T65 writes faster than that only in RMW pairs (dummy write and write on consecutive cycles); depth 4 absorbs those and short holds (H1 at most 4 c64 clocks; H2 at most 16 in the sprite-0 p-access cycle plus 1 in the calc cycle, once per line) before the island stalls on `full_q`.

**The landing rule** [I]:
1. **Edge.** An accepted entry lands at the next c64 edge (C2).
2. **Order.** Entries are accepted in FIFO order, one per c64 edge, VIC and SID entries in one stream. A held head blocks later entries.
3. **Native barrier.** No entry is accepted while the VIC's `we_r` = 1. `we_r` is 1 from e_16 of a native VIC write cycle until e_16 of the next cycle, so it covers that write's colour landings (e_19..e_31), `myWr_a` (e_31), `myWr_b` (e_3) and `myWr_c` (e_7) [V VIC:300-315]. In turbo a native VIC write only comes from REU DMA or from the tail of exact mode, so a fast write never lands before an older native write's late strobes.
4. **H1.** A `$D011` write (any mirror) is not accepted when the acceptance phase is 10..13, so it never lands at e_11..e_14.
5. **H2.** A write to `$D015` or a sprite Y register (`$D001..$D00F` odd, any mirror) is not accepted when the acceptance phase is 14..29 in the cycle where `vicCycle = cycleSpriteA and sprite = 0`, so it never lands at e_15..e_30 of that cycle. It is also not accepted at acceptance phase 13 while `vicCycle = cycleCalcSprites`, so it never lands at e_14 of that cycle (sprite-0 BA, section 2.13.6). Only sprite 0's `$D015` bit and `$D001` need the second window; the hold uses the whole H2 set for simplicity. The rule holds for every VIC model, including the 6567R8, because it keys on `vicCycle`.
6. **Not gated by pause or memory waits.** During pause `sysCycle` is frozen at EXT4, position 8 [V fpga64:404, 196-202], which is outside both hold windows; a landing there equals a landing just after e_7. Memory waits stretch only phases 13 and 29 and need a cartridge, which forces exact mode (D2).
7. **SID targets** have no holds. Their landing is governed by the edge stage (section 2.13.7).
8. **Native and fast on one edge cannot happen.** In turbo the native slot carries the dummy, forwards (never `$D000-$D7FF` while the fast targets are built) or DMA (island halted, queue empty by O2/O6), and a retired exact request is never re-run (section 2.6). Assertion A6 checks it. If it ever happened anyway, the RTL resolves it differently per chip, and the text says so: in the VIC the fast write and clear blocks (P3-P5) come after the stock code, so the fast strobe wins (VHDL last assignment); in `sid_top` the register-block source mux selects the stock write first, so the native write wins. Neither outcome is relied on (review finding 5).

#### 2.13.6 VIC: why the landing rule keeps stock internals

**Stock write timing** [V VIC:300-315]:

| Strobe | Lands at | Registers |
|---|---|---|
| `myWr_a` | e_31 of the write cycle | RSEL, `$D019` acknowledge pulses, `$D01D` |
| `myWr_b` | e_3 of the next cycle | other `$D011` bits, `$D012`, `$D015`-`$D018`, `$D01A`-`$D01C` (→ MCDelay) |
| `myWr_c` | e_7 of the next cycle | sprite X/Y, `$D010`, XSCROLL, `$D030` turbo_reg |
| NMOS colour write | e_19..e_31 of the write cycle | `$D020-$D02E` |

All of these are pixel edges. The source gives no reason for the chosen phases [I]. Multitini fixes `vic_variant` = "00", NMOS (adapter:516) [V]; the HMOS grey dot is dead here.

**Consumers** fall into four classes [I, from the RTL, VIC:329-1566]:
- **(a) Pixel consumers** (colours, MX, `$D010`, XSCROLL, MCM, CSEL, RSEL, MPRIO, MC, MXE, display-mode delays). They see a change from the next pixel. That is the intended turbo effect, and it matches #203. These are shift registers and comparators; stock writes already move them at 8-pixel granularity, so their control state cannot get stuck. The collision latches are the exception [V VIC:1498-1529, 1543-1559]: M2M/M2D and IMMC/IMBC are sticky and set from pixel data. A single landing that changes sprite or background pixels at a non-stock pixel edge during sprite display (MX, `$D010`, `$D011` mode bits, `$D016`, `$D01C`, `$D01D`, or a `$D018` fetch-data difference) can create or remove a collision that a stock write in the mapped cycle would not, and the latch keeps it until `$D01E`/`$D01F` is read (M2M/M2D) or `$D019` acknowledges it (IMMC/IMBC) [I]. This follows from the pixel-level timing by design, not from the T2 collapse; T2 does not compare these latches strictly at those times (section 10.2, T2; question 28 in section 11).
- **(b) Once-per-cycle state** sampled at e_15 or by phi0 chains ending at e_15 (MDMA, MCBase, MCnt, MYE_ff, RC/VC/idle, `fetchBMM`, MPtr, `baChars`/`baSprite`/`reuSprite0Grace`). A landing in (e_15(k−1), e_14(k)] gives the same state at e_15(k) as a stock write in cycle k−1, with the e_14 exception below; a later landing equals a stock write in cycle k.
  - **The e_14 edge is special** for state registered on every φ1 edge from a combinational function of a register: `baChars` (from `badLine`, i.e. YSCROLL and DEN; VIC:652-667) and `baSprite04` (from `MDMA_next(0)`, i.e. ME(0)/MY(0); VIC:678, 711). A landing at e_14 reaches them only at e_15, one edge after the e_15 consumers (`baCnt`, fpga64 `cpuHasBus`, DMA takeover) sampled them, while MDMA itself (registered at e_15, VIC:1224-1227) already sees the new value [V]. H1 already excludes e_14 for `$D011`. For sprite 0, H2 now excludes e_14 of the `cycleCalcSprites` cycle (rule 5).
  - Worked case without that hold (review finding 3) [V VIC:343-353, 762-768, 1321-1322; fpga64:697]: a `$D015`/`$D001` landing at e_14 of the calc cycle turns MDMA(0) on at e_15 but drops BA one edge late, so `baCnt` reads 0/1/2/3 instead of 1/2/3/4 at the e_15 edges of the four cycles before the first s-access. `aec` is then 0 during that s-access, the VIC reads `vicBus` = `$FF`, and `MPixelStore(15:8)` loads `$FF`: the first 8 (16 if X-expanded) pixels of the sprite's first row are solid. Stock writes land at e_3/e_7 and never reach this state. Sprites 1-7 lower BA from the registered MDMA (VIC:714-735) and are not affected.
- **(c) Every-edge consumers.** The fetch address (p11, p27) has no stored state. The sticky flags `rasterEnable` and IRST/`rasterIrqDone` are evaluated on every clock [V VIC:617-623, 899-907], so each landed value acts for at least one edge, even when a later landing in the same mapped cycle replaces it: a DEN = 1 that lasts one edge on line 48 sets `rasterEnable` until line 248, and with it that frame's badlines; a `$D012`/RST8 value that matches `rasterY` for one edge sets IRST; a value that mismatches for one edge re-arms the compare. `$D019` acknowledges are one-clock pulses [V VIC:1584, 1692], so every landed mask acts, not only the last; DEC/LSR/ASL `$D019` acknowledge with their dummy write (`$F1` when only IRST is pending [V VIC:1799]), as on stock. Several landings therefore do not collapse to the last value here. The result equals that of a stock C64 issuing the same writes in separate cycles (for example INC `$D012` on a line that the compare does not match, whose dummy write stores the current line), at most one cycle earlier [I, C64_TURBO_MODELS.md (vic_t2_sticky.py)]; a raster IRQ can be raised in mid-cycle. When the raster line changes between a landing and its mapped stock landing, the landing acts on the earlier line: a DEN = 1 landing just before line 48 ends sets `rasterEnable`, and a `$D012` value equal to the current line raises that line's IRQ, where the mapped stock write would not [I]. These sticky effects are expected turbo behaviour, not faults. This VIC also catches a DEN or compare pulse shorter than one cycle, which a real 6569 or the U64 VIC may miss (T-i, section 2.13.12).
- **(d) One register feeding two state elements in two phases of one cycle**, with a stock invariant. H1 and H2 remove exactly these windows:

| Hazard | Invariant broken by an unrestricted landing | Window | Hold |
|---|---|---|---|
| H1 `$D011` ECM/BMM | `fetchBMM`/`fetchECM` must hold the mode used by the last phi1 fetch (comment at VIC:522). A landing at e_11..e_14 lets the p11 fetch use the old mode while e_15 records the new one | land e_11..e_14 | H1 |
| H2 ME/MY | In the sprite-0 `cycleSpriteA` cycle, MPtr (e_15, VIC:572) and MActive (e_19..e_31, VIC:1233) must come from the same ME/MY | land e_15..e_30 of that cycle | H2 |
| H2 sprite-0 BA | In the `cycleCalcSprites` cycle, MDMA(0) (e_15) and `baSprite04` (every φ1 edge, from `MDMA_next(0)`) must turn on in the same cycle as seen by the e_15 consumers | land e_14 of that cycle | H2 (calc-cycle phase) |

**Conclusion** [I, to be proven by T2]: the VIC's once-per-cycle control state at every e_15 (class (b); pre-edge, `baCnt` and `aec` included) equals that of a stock VIC fed with a mapped stock write sequence in which several landings to one register in one mapped cycle collapse to the last value, with `rasterEnable` taken from the sticky rule below. The sticky state of class (c) (`rasterEnable`, IRST/`rasterIrqDone`, the `$D019` acknowledges) does not collapse: it equals that of a stock C64 issuing the same writes in separate cycles, flags within one cycle. The one accepted exception is T-h: two `$D011` landings in one mapped cycle that turn the badline off and on again restart `baCnt` ("BA in mid-cycle" below). T-i records that a DEN or compare pulse shorter than one cycle acts here but may not on a real 6569 or the U64 VIC. Other non-stock behaviour exists only in pixel-level and fetch-address data paths and in the collision latches they feed (class (a)), which is what a turbo CPU should cause.

**Per register**

| Register | Any c64 edge safe? | Rule |
|---|---|---|
| MX, `$D010`, `$D016`, `$D017`, `$D018`, `$D01B`-`$D01D`, colours, `$D030` | yes | next edge |
| `$D011` | yes except e_11..e_14 (ECM/BMM) | H1 |
| `$D012` | yes (IRQ may set mid-cycle) | next edge |
| MY, `$D015` | yes except e_15..e_30 of the sprite-0 p-access cycle and e_14 of the calc cycle | H2 |
| `$D019`, `$D01A` | yes | next edge + acknowledge barrier (section 5.8) |
| `$D01E`/`$D01F` clears | yes | next edge |

Stock lands `$D011` RSEL (e_31) and `$D016` XSCROLL (e_7) separately from the other bits. The fast path lands all bits of a register together. This has no stock-visible meaning at turbo [I; E7-V8].

**Pixel-level behaviour (#203).** A colour write shows from the first pixel edge after C2. Model (C64_TURBO_MODELS.md (vicpath_model.py, top_rows_model.py); unrolled `LDA #0 / STA $D020 / LDA #1 / STA $D020`) [I]:
- index 14 (48 CPU cycles per 8 pixels): every colour lasts exactly one pixel (789 single runs, no doubles). The pattern is regular, as C64_TURBO_U64_ISSUES.md §4.2 predicts.
- C64U row 47 (`c64u_top` = 1; the U64's 48 MHz ratio): runs of 1 pixel with a 2-pixel run every about 46 pixels (772 singles, 17 doubles). The U64 shows "alternating pixels … a glitch every 48:th or so" [M #203]. The match is qualitative.
- index 15: each colour lasts 0.75 pixel (0.76 with the C64U row 63), so colours are sometimes skipped.

**BA in mid-cycle.** A fast `$D011` write can move `baChars` in mid-phi0 (it is re-evaluated from `badLine` on every phi = 0 edge, VIC:652-667). The proxy (`cpuHasBus`) and the REU DMA logic sample BA only at e_15. `baCnt` is different [V VIC:343-353]: it counts only at e_15 but **clears on any clock with `baLoc` = 1**.
- One fast `$D011` landing per mapped stock cycle (the T2 mapping, landings in (e_15(k−1), e_14(k)]) gives the same `baCnt` at e_15(k) as a stock write (a stock landing at e_3 also clears it mid-cycle).
- Two landings in one such window (H1 keeps them out of e_11..e_14) that turn the badline off and on again pulse `baLoc` high and restart the count. `aec` then stays 0 for three more cycles, so those c-accesses read `vicBus` (`$FF`) and the `cpuDi` colour nibble, as in an FLI retrigger. No single stock write reaches this state, but a real VIC driven by a turbo CPU would most likely restart its BA→AEC delay the same way [I]. The design accepts it as turbo behaviour (D14, T-h) rather than adding a hold that would delay later queue entries; T2 documents it as its one allowed `baCnt` difference.
- The island's halt logic sees intermediate BA levels, only in turbo [I].
- Review finding 11 corrected the earlier claim that `baCnt` samples BA only at e_15.

**U64 registers alongside the VIC.**
- Every fast VIC write still reaches the VIC: `$D031` is a VIC no-op, `$D030` sets turbo_reg (no effect, `turbo_en` = 0), `$D07A` is the no-op `$3A`.
- turbo_regs snoops the pop stage (`u64_wr`, A9..A0, data) at C2 with the exact decode of section 1.2. Native exact-mode writes keep the `cpu_bus_valid` snoop. The two sources are disjoint.
- `u64_hit` overrides the read only for exact `$D031` (Registers/TurboEnable live) and `$D030` (TurboEnable live). `$D071`, `$D131` and similar read the VIC (`$FF`).
- A `$D031` read after a `$D031` write sees the new value, because the write barrier waits for done.

#### 2.13.7 SID: register landing, latch, edge stage

**Verified facts** (this repository; MiSTer port 62fc697; SF1-SF20, numbered as facts F1-F20 of C64_TURBO_FAST_SID.md):

| # | Fact | Source |
|---|---|---|
| SF1 | `sid_top` holds one register file per chip, written on any `clk` edge where `cs[i]` and `we` are both high; `we` is not qualified by `ce_1m` | [V] sid_top.sv:179-230 |
| SF2 | The bus latch `bus_data[i]` takes `data_in` on every write. On a read it takes POTX/POTY/OSC3/ENV3 for `$19-$1C`; otherwise it is unchanged. No register clears on read | [V] sid_top.sv:201-202, 231-238, 344 |
| SF5 | Engine step: `ce_1m = enableSid and not memory_wait_q`, active at e_0 | [V] fpga64:563-587, 828 |
| SF6-SF7 | Oscillator, test bit, sync, waveform, OSC3 and DCA update only under `ce_1m`. The envelope samples `gate` only under `ce_1m` and detects edges against its previous sample | [V] sid_voice.sv:66-195; sid_envelope.sv:69-106 |
| SF9 | Filter register sample edges: chip 0 at e_3/e_6/e_8 (states 2/5/7); chip 1 at e_11/e_14, then `Mode_Vol[3:0]` at every edge e_16..e_0. Chip 1 publishes its sample one edge later, so the sample left at e_0 carries the volume sampled at e_31: a chip-1 volume write landing at e_31 is heard one native cycle later than a stock e_29 write | [V] sid_filter.sv:136-215 (210); sid_top.sv:274, 303-318, 334-340 |
| SF11 | Combined-waveform lookups sample `acc_t` at states 1-11; the waveform select is applied at `ce_1m` for `comb` and the waveform-on test, but the single-waveform output reaches `dac_out` through `norm_dac`, which registers it on every clock, so `dac_out` uses control bits 7:4 from one edge before `ce_1m` | [V] sid_top.sv:277-297; sid_voice.sv:76, 138-175, 163-164, 184-187 |
| SF12 | Audio leaves the core point-sampled at 48 kHz | [V] adapter:655-673 |
| SF15-SF17 | Stock CPU write lands at e_29 (`pulseWr_io`); a stock write to `$19-$1C` leaves the latch holding the live value (SD3); a stock read returns the e_30 value | [V] fpga64:666-676, 829-830; sid_top.sv:200-238; buslogic:172-173 |
| SF19 | REU DMA writes use the same native path | [V] fpga64:1094-1096 |
| SF20 | `pot1..4` are tied to `$FF`; POTX/POTY read `$FF` or `$00` per CIA1 PA6/PA7 | [V] adapter:536; fpga64:819-822, 834-839 |

**Dual-SID addressing** [V fpga64:815-817; supervisor_settings.h:21; core_frontend.c:130-132]:

| `sid_mode` | Left chip | Right chip | Menu |
|---|---|---|---|
| 0 Single | all of `$D400-$D7FF` | same (both written; reads return left) | yes |
| 1 | all of `$D400-$D7FF` | `$DE00-$DEFF` (I/O1) | no (HDL only) |
| 2 Dual `$D420` | A5 = 0 | A5 = 1 | yes |
| 3 Dual `$D500` (default) | A8 = 0 | A8 = 1 | yes |
| 4 | all of `$D400-$D7FF` | `$DF00-$DFFF` (I/O2) | no |
| 5-7 | all of `$D400-$D7FF` | never | no |

`sid_mode` already reaches the c64 domain through an existing mailbox [V multitini_one_top.sv:1187-1206]. The fast path computes selects in fpga64 on the landing (or read) address:
```
fio_sid   = A(15..10) = "110101"
left_term = (sid_mode(0)='0' and A(5)='0') or (sid_mode(0)='1' and A(8)='0')
sel_l     = fio_sid and (sid_mode(2..1) /= "01" or left_term)
sel_r     = fio_sid and (sid_mode = 0 or (sid_mode(2..1) = "01" and not left_term))
```
For every address in `$D400-$D7FF` with `ioe_i` = `iof_i` = 0 this equals fpga64:815-816 (bench T-S1c checks it exhaustively).

**At landing (C2), for each selected chip i, register offset a = A4..A0** [I]:

| Entry | Register file | Latch `bus_data[i]` after C2 |
|---|---|---|
| WR, a in `$00-$18` | stock field update (sid_top.sv:203-229), except staged control writes (edge stage) | `wdata` |
| WR, a in `$19-$1C` | none | live value (stock end state, SD3) |
| WR, a in `$1D-$1F` | none | `wdata` |
| LATCH | none | D for the data-supplying chip; own live value for another selected chip |

Every access sequence gives the same architectural result in exact and turbo mode; only the timing differs.

**Edge stage** (`EDGE_STAGE` = 1, per voice control register `$x04`, `$x0B`, `$x12` of each chip) [I]:
- A sample window runs from one `ce_1m` edge to the next. A landing on a `ce_1m` edge belongs to the new window.
- The first fast landing to the register in a window is applied at once. Any later landing in the same window goes into a one-byte stage; the latest wins. The stage is applied at the next `ce_1m` edge and counts as the first write of that window. A window that starts by applying a stage therefore sends all its own landings to the stage; the latest wins.
- A native write (exact mode or REU DMA) to the register cancels a pending stage. Reset clears all stages.
```
if reset:              pend<=0; flag<=0
elif native_wr(r):     pend<=0; flag<=0             // the stock branch writes r itself
elif ce_1m:                                         // window boundary: the engine samples the old r here
    if pend and w:     r<=stage; stage<=wdata; pend<=1; flag<=1
    elif pend:         r<=stage;               pend<=0; flag<=1
    elif w:            r<=wdata;               pend<=0; flag<=1
    else:                                      pend<=0; flag<=0
elif w:
    if flag:           stage<=wdata; pend<=1
    else:              r<=wdata;     flag<=1
```
- Effect [I; literal cases in C64_TURBO_MODELS.md (sid_edge_stage.py)]:
  - **Window that starts without applying a stage:** the engine sees the first and the last value written in it, each for at least one engine step. Gate 1→0→1 inside one µs becomes one sample of release then attack (engine gate samples 1 0 1 1), the same envelope path as the 4-µs pulse that `sta`/`sta` gives at 1 MHz. An isolated RMW double write matches stock sample for sample.
  - **Window that starts by applying a stage:** all its writes merge into the stage, so the engine sees only its last value. A window starts this way when the window before it ended with a stage pending: two or more writes in a window that started clean, or any write in a window that started with a stage. One window with no write to the register ends the chain.
  - **Contract.** Isolated pulses are kept: at most one gate or test pulse per register per 2 µs, with no other write to that register in the µs after the pulse (precisely: a pulse whose writes fall in one window is kept when that window starts without applying a stage; a pulse split across two windows is kept when each of its writes is the last write in its window). Denser sequences are compressed. Two consecutive windows that each write gate 1→0→1 give engine samples 1 0 1 1 1, where four 1 MHz writes give 1 0 1 0 1: the second pulse is merged. Two consecutive test pulses give one oscillator reset instead of two.
  - The stage delays a write by at most one µs, and only when another write to the same control register landed less than 2 µs before. In exact mode it never triggers: native writes cancel stages and never stage. Stock SID behaviour, stock REU DMA writes to the SID and the Expand gate (G-REU0) are unchanged.
  - **Rejected: keep every pulse.** That needs backpressure: a SID hold in the shared FIFO (section 2.13.5) that keeps a control write at the queue head while its stage is pending. VIC and SID entries queued behind it would wait up to 1 µs per held write, and a burst would stall the island on `full_q`. That is a partial form of the paced queue that C64_TURBO_FAST_SID.md 3.5 rejects, and it would move raster-split landings (T6). The U64 most likely drops every sub-µs pulse (U64 evidence below; E7-S), so compressing dense sequences is no worse than the reference [I].

**Why turbo landing is safe for the engine** [V SF5-SF11; I]:
- Every engine block reads a register at one defined edge. Only three places combine two samples of one register taken at different edges: combined waveforms (stock already mixes these on every control write, because its writes land at e_29 after every lookup); the waveform DAC (SF11: a fast control write landing at e_31 gives one `dac_out` step with the old `norm_dac` and the new `comb` and waveform-on test, which a stock e_29 write never does; one µs, inaudible [I]); and chip-0 filter routing vs volume (a `$D418` landing between e_3 and e_8 gives one µs with new volume and old routing; the 48 kHz point sample has about a 1-in-21 chance of seeing it; inaudible [I]).
- No illegal state is reachable: the envelope changes state only on `ce_1m` from the sampled gate level; the oscillator and noise generator react to levels at `ce_1m`.
- The only lasting effect is a lost sub-µs gate or test pulse, which changes the envelope, oscillator phase or LFSR state for the rest of the note. The edge stage keeps isolated pulses; in denser sequences a pulse can still be merged (Effect above).

**Digis** [I unless marked]:
- 4-bit `$D418` digis: chip 0 samples the write at the next e_8; added jitter is under 1 µs against a 45-250 µs sample period and the 20.8 µs output grid.
- With badline timing disabled, the write proceeds during BA low and the CIA acknowledge completes through I/O-only ownership, so the #665 growl does not occur [V issue665: the U64 fix]. Both pieces are needed (risk R18).
- REU fixed-address digis (DMA to `$D418`): native path, 1 byte per φ2, unchanged (SF19).
- A value written to a level register and overwritten within the same µs is lost (e.g. the first write of `INC $D418`). At 1 MHz it would have lasted one µs, which the 48 kHz point sample cannot resolve.

**Second SID at `$DE00`/`$DF00`** (modes 1 and 4; not in the menu): stays a φ2 I/O1/I/O2 forward and is I/O-only eligible. Reasons: the U64 measures I/O1 as one access per φ2 [M]; the native path keeps the cartridge `io_ext` read priority (fpga64:817) and the shared IO2 decode; the difference is timing only. If firmware ever offers these modes and E7-S shows the U64 runs them at turbo, add a `sid_io_fast` map bit.

**U64 evidence** [V/M]: UltiSID writes are a single `wren` strobe into a register array with a one-clock pipeline, no queue and no rate limit [V 1541ultimate d43546f sid_regs.vhd:152-202]. Its envelope reads `gate` as a level once per voice iteration [V adsr_multi.vhd:166-200], so the U64 probably drops sub-µs pulses (E7-S; D15). The UltiSID files forbid reuse without permission (sid_regs.vhd:9-10); they are evidence only and no code is copied.

#### 2.13.8 Internal φ2 I/O while BA is low (I/O-only ownership)

**Verified facts** (fpga64 unless named):
- [V 530-531] At e_15 with no DMA, `cpuHasBus <= '1'` if `baLoc = '1' or cpuWe_pre = '1'`. `cpuHasBus` and `phi0_cpu` clear at R (555-558). In turbo the frame shows the dummy's we = 0 at e_15, so `cpuHasBus` = 0 after e_15 with no DMA means BA was low.
- [V buslogic:298-314] With `cpuHasBus` = 0 the shared bus carries `vicAddr` (if `aec`) or `cpuAddr`, only the VIC RAM select or char ROM is set, no I/O select, `systemWe` = 0.
- [V mos6526.v:90-91; fpga64:876-877] CIA `rd`/`wr` = `phi2_n & !cs_n & rw`, with `rs = cpuAddr(3:0)` and `db_in = cpuDo`; neither depends on `systemAddr`.
- [V 666-676] `pulseWr_io` does not depend on `cpuHasBus`. It writes SID and colour RAM.
- [V adapter:427-439] REU register access is `cpu_bus_valid & reu_io_select`; the `$FF00` trigger is `cpu_bus_valid & write & addr == $FF00`.
- [V cartridge.sv:105-123, 157-161; adapter:492] The cartridge uses `addr_in = systemAddr` and `mem_write = ramWE`.
- [V VIC:341-353, 762-768] FPGA64's `aec` is 0 in φ2 of BA-low cycles 1-3 and 1 from cycle 4.
- [M #665, comment of 2026-03-23] U64 fw 3.14 with badline timing disabled: "REU, UltiSID, CIA's all seem to be immune to BA shenanigans". Only real SIDs on the external bus growled. The fix let the bus bridge start one slot late, "a jitter of 1/64 of a cycle, which still completes at the end of the very same PHI2 cycle" [V #665]. U64 sources are not public [V GideonZ #710].

**Eligibility.** Island flag, registered with the request:
```
isl_x_req.ioonly = turbo_q & ~badline_en_q &
                   (cls in {FWD_IO, FWD_FF00} | (cls == FIO-forwarded-because-slow))
```
The proxy loads `frame.ioonly <= isl_x_req.ioonly & ~cart_present` at e_15 and clears it at R (section 2.6). `cart_present` is redundant before M7 (a cartridge forces exact mode) and stops M7 from inheriting the rule by accident.

**The rule** [I]. On the edge ending CPU0 (e_16), fpga64 sets `cpuIoOwn` when `phi0_cpu` = 1, `cpuHasBus` = 0, `dma_active` = 0 and `ext_cpu_ioonly` = 1. `cpuIoOwn` clears at R and at reset. While it is 1:
- buslogic keeps its VIC branch: `currentAddr` (RAM, colour, char ROM address), `dataToVic`, `systemWe` = 0. The VIC's RAM select drives only `cs_ram`, as a new `vicRamLoc`; it is not a CPU data source.
- Chip selects are decoded from `cpuAddr`, using `bankSwitch` = the frame's carried port and `ultimax` = 0. No colour-RAM select is ever decoded.
- `dataToCpu` takes the chip, I/O1/I/O2 `io_data`, or open-bus `lastVicData` by the existing priority chain.
- `cpu_bus_valid = enableCpu & ~dma_active & (cpuHasBus | cpuIoOwn) & ~memory_wait_q`. So the REU snoop, B0 capture, `done_seq` and the turbo-register snoop each see the access once.
- CIA `phi2_p`/`phi2_n`, `enableSid` and `pulseWr_io` are unchanged.

Why e_16 and not e_15: nothing needs a chip select in phase 16 (CIA acts at e_29; SID and colour RAM at e_29; `myRd` at e_19 or later; `pcr` only needs `cs_n` low on some clock), and deciding at e_16 avoids a new fast→c64 combinational path into the e_15 decision. An I/O-only access always completes at R: the retire fires every native cycle, DMA is excluded, `memory_wait` needs a cartridge, and the pause point is phase 7/8. No access can half-happen.

**Per native cycle of a BA-low window** (badline or sprite DMA):

| Cycle | `baLoc` at e_15 | FPGA64 `aec` in φ2 | Stock FPGA64 CPU read | Stock FPGA64 CPU write | This design (`ioonly`) |
|---|---|---|---|---|---|
| BA high | 1 | 0 | normal | normal | normal (`cpuHasBus`) |
| BA-low 1-3 | 0 | 0 | RDY hold, no chip select (matches the real PLA) | `cpuHasBus` via `cpuWe_pre`, full decode | chip select from `cpuAddr`; the native RAM read at the forward address has no effect; a mid-line c-access takes its colour nibble from the forwarded data (D7) |
| BA-low 4+ | 0 | 1 | RDY hold | would corrupt the fetch; a 6510 never writes 4 cycles in a row [V C64_TURBO_BUS_CONTRACT.md §5c.2] | VIC keeps `systemAddr`; chip select from `cpuAddr`; VIC fetch unchanged |

Throughput: one I/O-only access per native cycle, the same φ2 rule as BA-high cycles (cutoff f = 30, resume f = 4 or about f = 13 for the acknowledge class). Under D5 a random φ2 forward waited on average 1.20 µs PAL / 1.38 µs NTSC with the screen on, and 4.22 µs PAL with 8 sprites on every line; with I/O-only the extra wait is 0 [I C64_TURBO_MODELS.md (balow_calc.py)].

**REU** [I]:
- Register reads and writes at `$DF00-$DFFF` complete in BA-low cycles. `$DF00` reads clear status once (completion is `cpu_bus_valid`; sequence numbers prevent replay).
- A `$DF01` execute in a BA-low cycle: R sets `active`; e_1 sets `dma_active` and B1.hold, so the island enters DMA_WAIT; the next e_15 with `baLoc` = 0 sets `dma_wait_vic` [V 510-517]; DMA takes the slot at the first VIC3 with `ba_reu` = 1 and runs with the VICE post-access checks [V 479-533]. Stock differs only in where the trigger can land.
- REU DMA is never I/O-only, including DMA to an I/O address.

**Ultimate Audio registers** (Q12). With the option on, `$DF20-$DFFF` is I/O2, like the REU registers. Those accesses complete in BA-low cycles through I/O-only ownership with badline timing disabled, with no UA-specific logic here, because UA uses the REU's `cpu_bus_valid` and I/O2 select path (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.10, §2.2):
- under `cpuIoOwn`, buslogic decodes IOF from `cpuAddr`, so the UA select (`cart_iof` and `addr[7:5]` ≠ 0) works unchanged;
- `cpu_bus_valid` includes `cpuIoOwn`, so a UA write strobes once; read data returns through `io_data` and the existing priority chain;
- REU DMA cycles never reach UA in any mode: the UA select excludes `reu_dma_req`, and `cpu_bus_valid` excludes DMA;
- a UA acknowledge write completes after the e_`IRQ_SAFE_PHASE` marker (section 5.8);
- a cartridge forces exact mode until M7 (D2), and the REU and UA are off while one is loaded. After M7, cartridge I/O is FWD_CART and never I/O-only; UA stays off because the cartridge still excludes the REU.

**`$FF00`: local write plus snoop forward** (`FF00_SNOOP` = 1) [I]:
- At the granted enable that would retire the cycle, the island writes RAM[`$FF00`] through port A (local-write rules, including O3) but withholds the T65 enable, and registers a FWD_FF00 forward (with `ioonly` when badline timing is disabled).
- The forward completes once: with `cpuHasBus` (BA high), the native RAM write repeats the same byte, harmless and collision-free (section 4.2); with `cpuIoOwn` (BA low), no chip select is decoded (the address is not in `$Dxxx`), so it is a pure snoop.
- `cpu_bus_valid` at R triggers the REU if armed. The RAM write precedes R, so a transfer that reads `$FF00` sees the new byte, as on stock.
- `FF00_SNOOP=0` restores the v1 RAM-bus forward (D5r).

**Cartridge I/O1/I/O2** never gets I/O-only ownership: cartridge.sv would see the VIC's address and `ramWE` = 0, and the U64 rule is the same (external-bus accesses wait for BA = 1) [V turbo_mode.rst:174].

**Badline timing enabled:** nothing new. `halt_q = ~ba_q` halts every class while BA = 0, and `ioonly` = 0. `ba_q` lags `baLoc` by up to 2 fast clocks, so up to 2 island cycles may run after BA falls; this is harmless, because BA falls 3 native cycles before the VIC needs φ2 and island cycles do not use the native bus (a fast write that late corresponds to a 6510 write inside its 3-cycle grace) [I].

**#665 reproduced: no growl with `$8F` and the screen on** [I]. A timer-driven digi handler (`STA $D418`, `LDA $DD0D`, `RTI`): entry, pushes and sample fetch are local; `STA $D418` is a fast write; `LDA $DD0D` completes at R of the native cycle it loads in, BA high or low, then waits for the e_5 marker. The timer runs at the native rate and grants are locked to φ2, so handler latency varies only with the interrupted instruction and a main-loop forward in flight (at most about 1 µs), not with badlines. Under D5 the ICR read could stall up to 43 µs on every 8th raster line (1953 Hz PAL / 1967 Hz NTSC); at 8 kHz that is up to 34% of a sample period against at most 0.8% here [I C64_TURBO_MODELS.md (balow_calc.py)]. What still growls by design: `$0F` (badline timing enabled), REU-DMA-fed digis (E-BA4), and fallback builds.

#### 2.13.9 Ordering across all classes

The T65 issues one bus cycle per enable, in program order. Reordering can only come from different path latencies. Observers: the VIC (port-B RAM/colour fetches and its registers), SID audio, CIA ports and IRQs, the REU (registers and `$FF00` snoop), DMA (only while the island is halted), and the IRQ/NMI lines back to the island.

Rules [I]:

| # | Rule | Implementation |
|---|---|---|
| O1 | At most one non-local operation is outstanding, except posted FIFO entries. φ2 forwards (either ownership) and the acknowledge wait block the island until done | FSM states FWD, ACKWAIT; `ack_wait_q` |
| O2 | Before a φ2 forward or adopting a mode/N/badline change, the island waits for `all_done_q` (every entry done). Before an FIO read it waits until no WR entry is pending and no side-effect entry that can change the value read is pending (section 2.13.4) | Flags registered from the post-push state, with `fio_done` compared directly into the flag flops (X7) |
| O3 | A local write (RAM, colour RAM, PORTW, the `$FF00` local write) waits while a fetch-affecting VIC entry (`$D011`, `$D015`, `$D017`, `$D018`, sprite Y; any mirror) is not done. Local reads and other queued entries do not delay it | `fetch_done_q` (island keeps the index of the last fetch-affecting push) |
| O4 | Entries apply in FIFO order, at most one per c64 edge, on every c64 edge (CF3), and become done in the same order, at most one per edge | fio_port; in-order done pipe; A7 |
| O5 | Acknowledge-class accesses complete only after the IRQ term reflects them (section 5.8) | e_`IRQ_SAFE_PHASE` marker (forwards); `FIO_ACK_EDGES` done delay (fast writes) |
| O6 | DMA starts only from a φ2 trigger (`$DF01`, `$FF00`), so by O2 the queue is empty. The island stays in DMA_WAIT until live `ext_dma_hold` = 0. After DMA, `busy_q` covers the last DMA VIC write's late strobes | section 2.8; assertion A6 |
| O7 | Pause: the island stops at a boundary; the queue drains; a loaded φ2 forward (including I/O-only) retires at R before the phase 7/8 pause point. Reset clears the queue, proxy, `cpuIoOwn`, SID stages and island together (≥32 c64 clocks). Mode switch: at an enable boundary with no forward in flight and `all_done_q`, for every cause; a pending NMI rearm crosses to exact through the carry of section 2.9 | sections 2.11, 2.12, 3.3 |
| O8 | `$FF00`: the local RAM write precedes the snoop forward. Carried port: every forward and DMA decodes with the port current at issue (section 5.4) | island FSM |

The global write barrier (O2: every FIO read waits for every pending WR entry) is the shipped choice. A precise variant (`FIO_RAW_PRECISE` = 1) would wait only for pending entries that affect the value read:

| Read | Waits for pending |
|---|---|
| VIC `$19` (`$D019`) | VIC WR to `$11`, `$12`, `$19`, `$1A` |
| VIC `$1E` / `$1F` | VIC WR to the same register; CLR_M2M / CLR_M2D (as in the shipped rule) |
| VIC `$30` / `$31` | VIC WR to `$30`, `$31`, `$39`, `$3A`, `$3B` |
| VIC `$12`-`$14` | nothing |
| VIC, any other register | VIC WR to the same register |
| SID `$19-$1C` | nothing: the value is live POTX/POTY/OSC3/ENV3, which a pending write can change only through a later `ce_1m` step or a CIA forward (already ordered by O2), i.e. only through timing [I] |
| SID write-only register (`$00-$18`, `$1D-$1F`) | every SID WR or LATCH entry for a chip that the read selects under the current `sid_mode`, because each one rewrites that chip's bus latch [V sid_top.sv:200-202, 231-238, 344]. The first v2 draft said "itself", which would let `STA $D405 / LDA $D41D` return a stale latch (review finding 4) |

The precise variant also keeps `~full_q` on every push: a read that may push and the dummy write of an RMW can meet a non-empty queue, which the post-push `full_q` handles (review finding 9). It saves about 1.8 fast clocks per raster-bar iteration at index 15 and risks an incomplete table, so it stays a generic until E7-V2 shows the C64U has no read-after-write penalty.

Hazard check:

| # | Hazard | Removed by |
|---|---|---|
| H1 | Queued write, then an FIO read sees stale data, including causal pairs across addresses (`STA $D40E` then `LDA $D41B`; `STA $D031` then `LDA $D031`) | O2 (write barrier) |
| H2 | Queued write, then a CIA/REU forward lands first (`STA $D418` then `STA $DD0D`; `STA $D01A` then `LDA $DC0D`) | O2 |
| H3 | `STA $D018` (buffer flip) queued, then a RAM write to the old buffer is fetched by the VIC first: a one-character glitch | O3 |
| H4 | Local write, then fast write | none needed; local lands first |
| H5 | Local read after a fast write | none needed; disjoint storage |
| H6 | `$01` change vs forwards, DMA and queued entries | carried port; a queued entry was issued while I/O was visible and stays valid |
| H7 | Stale IRQ after an acknowledge lets the next enable re-enter the handler | O5 |
| H8 | DMA reads VIC/SID registers with an entry still queued | O6 (trigger is φ2, so O2 applies) |
| H9 | DMA writes VIC registers, then a fast access after resume overtakes the late strobes | `busy_q`; assertion A6 |
| H10 | Queued entry across pause, reset or a mode switch, then an exact native VIC access overtakes it | O4 (drain), O7 |
| H11 | An I/O-only attempt that does not complete leaves a side effect | impossible: `cpuIoOwn` is set only in a retiring cycle; a BA-blocked, non-eligible attempt asserts no chip select [V buslogic:298-314] |
| H12 | BA changes on the e_15 edge itself | both ownership types use the pre-edge sample, as stock |
| H13 | Eligibility changes between retries (e.g. `cart_loading` rises) | re-evaluated at every e_15 load |
| H14 | `isl_x_req` changes at end f=31, coincident with e_15 | the proxy sees the old `seq` (TR7 assertion) |
| H15 | A colour or other pixel-register write and a RAM write reorder by up to 3 fast clocks | accepted [I]: the visible result is a timing shift of at most one character cell within one frame, no worse than the turbo CPU running a few nanoseconds faster |
| H16 | A pending side-effect entry, then a read whose value it changes (`LDA $D01E / LDA $D01E` with a collision present; `LDA $D41B / LDA $D41D`) | `dep_ok` (section 2.13.4) |
| H17 | A barrier flag still shows the pre-push state on the clock after a push (early acknowledge release, overflow, early forward or mode adoption) | flags registered from the post-push state (section 2.13.2); A5 and A7 assert on indices, not on the flags |
| H18 | The last exact request re-runs at the exact→turbo seam | the frame returns to the dummy at the retiring R (section 2.6); A8 |

#### 2.13.10 Mode switches, DMA, pause and reset

- **Turbo → exact:** adopted at the first enable boundary with no forward in flight and `all_done_q`, for every cause; the first exact enables finish a pending NMI rearm or held fall (the carry, section 2.9).
- **Exact → turbo:** the last exact request leaves the frame at its retiring R (section 2.6); the first FIO access waits for `busy_q` = 0.
- **REU DMA:** the trigger forward waits for `all_done_q`; the island is halted during DMA; afterwards `busy_q` covers the last DMA VIC write. DMA SID writes are native and cancel pending SID stages for the registers they write.
- **Pause:** the island halts at the next boundary; the pop stage keeps draining; SID stages wait for `ce_1m`.
- **Reset:** the island FIFO side (`wp`, `ack_wait_q`, `ack_end`, the barrier flags) resets with RST1; `fio_rp`, `fio_done`, the done pipe, the strobes, `cpuIoOwn` and the SID stages reset with RST2. Both take `ext_reset`. The VIC's reset branch has priority over the fast write block.

#### 2.13.11 BA and badline timing summary

- Enabled, index ≥ 1: `halt_q = ~ba_q` gates every island enable, FIO included. Queued entries may still land during BA low; a real 6510 also completes writes during BA-low cycles 1-3.
- Disabled: FIO reads and writes proceed during BA = 0 (the register ports use no bus); internal φ2 targets complete through I/O-only ownership.

#### 2.13.12 Fine timing that may differ from the U64 (D14; none known to matter to software)

| # | Detail | Size | Settled by |
|---|---|---|---|
| T-a | Read-after-write wait | 0-3 slots for a read within about 7 fast clocks of a write | E7-V2 |
| T-b | Acknowledge wait after `$D019`/`$D01A` writes | about 9 fast clocks (`FIO_ACK_EDGES` = 2) | E7-V4, E4 |
| T-c | H1/H2 holds | at most 4 / 16 (+1 in the calc cycle) c64 clocks for those registers | not observable in practice |
| T-d | Landing latency | 3-4 fast clocks (0.05-0.06 µs); U64 unknown | E7-V1 |
| T-e | Selective collision clear, without stock's multi-pixel tail | only bits that the read reported are cleared; a reported bit re-set within the 4-6 fast clocks before the clear is lost; stock loses about 9 pixel edges of every bit after a read | E7-V6 |
| T-f | Fetch-ordered local writes (O3) | up to about 4 slots after a fetch-affecting VIC write | E7-V3 |
| T-g | SID read cost | 0.078 φ2 per unrolled `$19-$1C` read at index 15 (0.079 with the C64U row) vs C64U 0.080-0.085; a `$1E`/`$1F` read with a collision present 0.125 | Amiga64 M5 gate; T4 |
| T-h | A badline turned off and on again by two `$D011` landings in one mapped cycle restarts the BA→AEC count (section 2.13.6) | 3 more cycles of `$FF` c-accesses | T2 directed case; E7-V on a C64U if available |
| T-i | A DEN or raster-compare value that lasts less than one cycle (two `$D011`/`$D012` landings in one mapped cycle) still sets `rasterEnable` on line 48 or IRST, or re-arms the compare, because this VIC evaluates them on every clock (section 2.13.6 (c)); a real 6569 or the U64 VIC may miss such a pulse if it samples once per cycle | badlines on for one frame, or one extra raster IRQ | T2 directed cases; E7-V9 on a C64U if available |
| T-j | An NMI fall after a low T65 sample is taken through the rearm (section 2.9, case 3) | one more T65 enable of NMI latency than case 2; the U64's handling is not documented | E7 (CIA2 case on a C64U); T3 NMI cases |

---

## 3. Clocking, reset and crossings review (AGENTS.md)

### 3.1 New or changed clocks

Each image has one pinned plan. NTSC details, alternatives and the NTSC-only fallbacks are in section 13.3.

| ID | Clock | Plan | Justification and check |
|---|---|---|---|
| CLK1 | `c64_cpu_clk` (new) | MMCM CLKOUT1 (clk_wiz `clk_out2`; Vivado name `clk_out2_zynq_ps_bd_clk_wiz_c64_0`) → BUFG. PAL /15 from VCO 945.833 = 63.0556 MHz; NTSC /11 from VCO 720 = 65.4545 MHz | Exactly 2× `c64_clk` in hardware, same VCO, 0° phase. Odd divides need the MMCM's 50%-duty odd mode (EDGE bit, 5.5/5.5 VCO cycles on /11 [V XAPP888 v1.8 p.2]); check the waveform in `report_clocks` (E2, E2-NTSC) |
| CLK2 | `c64_clk` (changed plan) | CLKOUT0 (clk_wiz `clk_out1`, name unchanged). PAL D=9, M=56.75, /30 = 31.527778 MHz, −5.6 ppm (today D=5 M=43.875 O=41.75, −32 ppm [V C64_TURBO_BUS_CONTRACT.md §5b]). NTSC D=5 M=24 /22 = 32.727273 MHz, 0 ppm against 360/11 MHz and +0.21 ppm against `MT_C64_NTSC_HZ` (32,727,266) | Legal under DS187 Table 72 for -2 (VCO 600-1440 MHz, PFD 16.67/30 MHz) [V clocking reviewer; DS187 v1.21]. Only CLKFBOUT is fractional, and only for PAL. No NTSC plan is on record today, because no NTSC build exists under `build/` [V] |
| CLK3 | BRAM port-A inverted clock | `IS_CLK*_INVERTED` inside each island RAMB36/RAMB18. No new clock net | Out-of-context scheme V1 met timing in every configuration (C64_TURBO_T65_TIMING.md §2). Creates half-period fast→fast paths only. A diagnostic F6 build uses the non-inverted clock instead (section 13.5) |

Vectrex keeps its `clk_out2` at 279° (create_project.tcl:486-493).

The fast I/O path adds no clock, no reset and no half-period path (section 3.2). The host and debug features add no clock and no half-period path either (section 3.2, X10-X12).

Ultimate Audio adds no clock (Q17). In the C64 image its HP3 port's ACLK moves to clk_wiz `clk_out1` (`c64_clk`) and joins the `c64_clk` ASSOCIATED_BUSIF. HP3 must stay on `clk_out1`, and nothing Ultimate Audio uses runs on `c64_cpu_clk` (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.8, §3). X-T9 checks both clock assignments (section 10.3).

**Clock-model exactness [O, E2 and E2-NTSC].**
- [V] Vivado represents MMCM outputs as master edges {1 2 3} plus edge shifts rounded to 1 ps, on a master `clk_fpga_2` of 6.666 ns (build/c705r2/c64-pal/reports/clocks.rpt:28, 33, 69-74). The exporter notes that "PERIOD properties are rounded to 1 ps" (build_and_export_xsa.tcl:430).
- [I] Under that model the PAL periods round to 31.715 and 15.857 ns: 2 × 15.857 ≠ 31.715 (C64_TURBO_MODELS.md (plan_ps.py)).
- [I] NTSC: the fast clock models as 15,276.25 ps (15.276 ns) and the c64 clock as 30,552.5 ps, exactly on a half-picosecond tie. Half-even or half-down rounding gives an exact 2:1 pair; half-up gives 30.553 ns and an inexact pair. Every exact NTSC plan has the same tie, because the modelled period depends only on the frequency (C64_TURBO_MODELS.md (ntsc_plans.py)). The tie may also be decided by Vivado's internal floating-point arithmetic rather than a stated rounding rule: in IEEE double, 6.666 × 5 × 22 / 24 lands just below 30.5525 ns and 6.666 × 22 × 5 / 24 just above it (same script). Only E2-NTSC can show which way it falls; the fix ladder covers both outcomes.
- If Vivado finds no common period, the pair is unexpandable (UG903). Every fast↔c64 path then gets a near-zero requirement and fails visibly. A wrong pair cannot pass silently.
- [I] Within ±60 ppm no PAL plan is ps-exact under this model. A ps-exact PAL plan exists at −102 ppm (D=7 M=38.25 /26 /13), and one ps-exact NTSC plan at −48 ppm (N4: D=6 M=47.125 /36 /18).
- [I] ps-exact means round(c64) = 2 × round(fast) when each period is rounded to 1 ps on its own. That holds under every tie rule exactly when frac(fast period in ps) is below 0.25 or above 0.75. `ntsc_plans.py` requires below 0.23 or above 0.77 (0.02 ps margin on the fast period, 0.04 ps on the c64 period); `plan_ps.py` tests the rounded pair directly and agrees (C64_TURBO_MODELS.md (ntsc_plans.py), (plan_ps.py)). Having no tie is not enough: N5 (D=7 M=48.875 /32 /16, +46.5 ppm, frac 0.540) has none, but models as 30.551 vs 2 × 15.276 ns, so it is rejected. The fix ladder below does not change: step 3 already names only the ps-exact plans.

Fix ladder, tried in order, per standard:
1. **E2 / E2-NTSC check.** In a scratch project, read the generated D/M/O back. `report_clocks` shows the fast waveform ({0 7.929} PAL, {0 7.638} NTSC); `report_clock_interaction` shows the pair timed with a common period; a named fast→c64 probe path has a requirement of exactly one fast period. Record DJ and uncertainty.
2. If not, an implementation-only Tcl constraint redefines the c64 clock under its existing name on MMCM CLKOUT0:
   - master `clk_fpga_2`, source pin CLKIN1 (as Vivado's own derivation does);
   - `-edges {1 2 3}` with edge shifts computed from the derived fast clock, so its period is exactly 2 × the fast period (model-only change, about 30 ppm PAL, under 1 ps NTSC). One script serves both standards.
   - Then verify: latency propagates through the MMCM with no "no path from master" warning; methodology waveform checks stay clean; the machine-clock filters (multitini_timing.xdc:54-61) still match.
3. If not, use the ps-exact plans above (PAL −102 ppm; NTSC −48 ppm). That needs a check of the video bridge and audio tolerance (R12).

**Duty-cycle precision [V DS187 Table 72: `MMCM_TOUTDUTY` 0.20 ns, including the global buffer].**
- Vivado's 7-series uncertainty `((TSJ²+TIJ²)^½+DJ)/2+PE` has no duty term [V timing_summary.rpt:321].
- The design adds `set_clock_uncertainty -setup 0.200` on `c64_cpu_clk` rise→fall and fall→rise, and uses it in E1, M1 and signoff.
- E2 also records DJ and the uncertainty for the new plans. The PAL PFD drops from 30 to 16.67 MHz with fractional M, so DJ is likely to exceed today's 0.193 ns. NTSC keeps a 30 MHz PFD with integer M.

### 3.2 Crossings

- All c64↔fast crossings are synchronous between related clocks and timed as single fast-period paths (15.28 ns NTSC / 15.86 ns PAL).
- Among them there are no synchronisers and no clock-group cuts. The control↔c64 mailboxes (X3, X4, X10, X11) use the existing reviewed `xpm_cdc_handshake` pattern.

| ID | Path | Content | Rule |
|---|---|---|---|
| X1 | c64 → fast | B0 (data, irq, nmi, rdy, valid, toggle); B1 (hold, blockers, mode/N/badline, pause); live `baLoc`, IRQ/NMI terms, `ext_dma_hold`, `phase`/`frame_run`, port-tick toggle, IRQ-safe marker, `lastVicDi` | Each path has a requirement of one fast period and positive slack. B0.data → DI mux → `localDi` → BusA → BAL adder is about 8 levels; it fits the full period [V C64_TURBO_T65_TIMING.md §3]. Every c64 signal used in port-A logic is first re-registered on the fast rising edge, so no c64-rise → fast-fall path exists |
| X2 | fast → c64 | `isl_x_req_*` (addr, wdata, we, port, exact, ioonly, seq) → frame register; T65 `NMI_ack` → cartridge | Requests start at `isl_x_*` flops (registered in both modes), ≤2 LUTs to the frame register (≤2 more for `ioonly & ~cart_present`). `NMI_ack` is direct (one fast period) to keep today's e_0 sampling |
| X3 | control → c64 | `C64_CPU_CONFIG` through a coherent `xpm_cdc_handshake` mailbox (copy of multitini_c64_expansion_config.sv). Bit 19 (C64 diagnostics armed) is part of the word; the adapter feeds the same c64-side level to turbo_regs and to the Ultimate Audio block | Existing reviewed pattern. It carries a level word. The apply event is an apply counter inside the word, never a separate strobe (section 6.1) |
| X4 | c64 → control | `C64_CPU_STATUS` | Same mailbox pattern in the other direction. No per-bit `cdc_bit_sync` for multi-bit fields |
| X5 | BRAM two-clock true dual-port | Main RAM, colour RAM, ROM copies | Collision analysis in section 4.2 |
| X6 | fast → c64 | `isl_x_fio_mem[4]`, `isl_x_fio_wp` → fio_port accept, strobes, `fio_rp` | One fast period. 4:1 mux, 3-bit compare, hold AND: about 4 LUT levels, 4-5 ns [I]. An entry pushed at a fast edge coincident with a c64 edge is not seen at that c64 edge (accepted at the next one; TR7-style assertion) |
| X7 | c64 → fast | `fio_done`, VIC `we_r`, `d030/d031` read values and live flags | One fast period; re-registered on the fast rising edge before use (X1 rule). `fio_done` enters the barrier-flag flops through the pointer compares of section 2.13.2 (about 2-3 LUT levels); those flops are the re-registering stage |
| X8a | fast → (c64 comb) → fast | T65 A (from T65 flops) → fpga64 → VIC and SID read muxes → U64 override → `io2_q` | One fast period. T65 A about 3 LUT + carry [V C64_TURBO_T65_CORE.md §3], VIC mux 3 levels with F7/F8, SID mux and chip choice about 4 levels in parallel, final select 1-2 levels, two chip-crossing routes: about 10-11 ns, slack about +4 ns at NTSC [I] |
| X8b | c64 → fast | VIC/SID register flops → read muxes → `io2_q` | One fast period (c64 rise → next fast rise); about 5 ns [I] |
| X9 | fast → fast | `io2_q` → DI mux → T65 (BAL adder) | Full fast period; same as the existing B0 path (fits [V C64_TURBO_T65_TIMING.md §3]) |
| — | c64 → c64 | fio_port strobes → about 270 VIC register CE/D and about 50 SID register-file/stage enables; `frame.ioonly`/`cpuHasBus`/`dma_active` → `cpuIoOwn` → `cs_*` → CIA/REU → `dataToCpu` → B0 at R | One c64 period (31.7 ns PAL / 30.6 ns NTSC). The worst c64 path today (24.5 ns, T65 → PLA → cartridge → T65 DI [V C64_TURBO_BUS_CONTRACT.md §0.3]) leaves with the island |
| X10 | control → c64 → control | C64 RAM peek (debug): request word {address, request counter, debug enable} through a coherent `xpm_cdc_handshake` mailbox (copy of multitini_c64_expansion_config.sv); response {data, done counter, refused} through the same pattern in the other direction | Existing reviewed pattern; level words with counters, never strobes. The c64 side acts only on a counter change |
| X11 | control → c64 → control | REU image preload: request word {invalidate request counter, UA hold}; response word {invalidate done counter, REU idle, UA held} | Same pattern as X10; level words with counters, never strobes |
| X12 | none (control domain) | Audio capture (debug) taps the existing `machine_audio_to_control` handshake output and the control-domain 48 kHz tick inside multitini_audio_test.sv (:128-133, :177-185) [V] | No new crossing: the capture FIFO, its pop and its registers are all on `control_clk` |

CONTROL bit 5 still crosses on its existing `cdc_bit_sync` (top.sv:869-874). When `C64_CPU_CONFIG.authoritative` = 1, the c64 side ignores it, so mode and liveness come from one crossing.

**Combined CDC review (Q17).** The C64 image also carries Ultimate Audio's new crossings C3 (expansion config, widened from 4 to 5 bits), C4 (a second HP3 quiescence synchronizer) and C5 (the UA status snapshot) (docs/C64_ULTIMATE_AUDIO_DESIGN.md §3). Ultimate Audio also consumes the UA hold and UA held levels of X11 and the armed level of X3; these add no crossing. One CDC review covers X1-X12 and C3-C5. The CDC gate must report exactly these new crossings and no other unexpected critical crossing.

**Load risk.** The read muxes put about 136 extra loads on T65 A0/A1 and about 40 on A2/A3. A is also the critical half-period path to port-A BRAM (5.87 ns, 7 levels out of context [V C64_TURBO_T65_CORE.md §3]). Vivado `phys_opt` normally replicates the driver LUTs. E1 must include the read muxes (R2).

### 3.3 Reset

| ID | Path | Rule |
|---|---|---|
| RST1 | Island reset | `ext_reset` (fpga64's `reset`, registered at phase 31, fpga64:433-440, exported by the new port of section 2.3) OR `!c64_aresetn`, registered once on `c64_cpu_clk`. It drives `cpu_6510` reset (T65 `Res_n` is asynchronous with a 2-clock release) and all island state, including the FIFO write side (`wp`, `ack_wait_q`, `ack_end`, the barrier flags). `core_reset_n` is not phase-aligned and must not be used (review finding 10) |
| RST2 | Proxy, fio_port, turbo_regs, `cpuIoOwn`, SID stages | The same `ext_reset` in the c64 domain (`cpuIoOwn` and the SID stages inside fpga64 use `reset` directly). The turbo config/live registers reset on `c64_aresetn` only; live is also cleared by C64 reset per section 1.5. The VIC's reset branch has priority over its fast write block |
| RST3 | Start-up | After reset release the island presents its reset-state request as the first exact request (section 2.10, point 8) |
| RST4 | Host and debug blocks | `DEBUG_CONTROL`, the audio capture FIFO and the control side of the X10/X11 mailboxes reset on `control_aresetn`, so every debug enable is 0 after power-on, reboot and PL reload. `C64_CPU_CONFIG` bit 19 (C64 diagnostics armed) and `C64_REU_HOST` bit 13 (UA hold) reset to 0 with their registers, so no diagnostic field of either feature counts and Ultimate Audio is not held. The c64 sides reset on `c64_aresetn`. `fell_q` and `force_q` reset to 0 with RST1. `nmi_seen_q` has no reset, like the T65's `NMI_n_o`, which the T65 reset branch leaves unchanged [V T65.vhd:219, 719-730, 784]; it updates only on enables while the T65 is out of reset, so the two stay equal |

`reset` lasts at least 32 c64 clocks, so both sides are in reset together and no stale toggle or FIFO pointer can complete a new request. No new asynchronous reset crossing is added.

### 3.4 Timing rules and export gates

**TR1.** "No new XDC" is retracted. New constraints, each reviewed:
- (a) the duty-cycle uncertainty on `c64_cpu_clk` half-period paths;
- (b) the c64 clock redefinition, only if E2 / E2-NTSC needs it;
- (c) the island Pblock, if M1 adopts it.

**Where they live** (review finding 16) [V]:
- Not in `multitini_timing.xdc`: it is shared by every image (multitini_timing.xdc:1-8), XDC cannot hold Tcl control flow (build_and_export_xsa.tcl:203-205), and a `*clk_out2*clk_wiz_c64*` filter would also match the Vectrex 279° PSRAM capture clock (create_project.tcl:486-493; build_and_export_xsa.tcl:526).
- New file `hdl/board/multitini_c64_turbo_timing.xdc`, listed in the `[c64]` section of `hdl/hdl_sources.txt` (per-machine sections, hdl_sources.txt:10, 260-280), so only C64 images read it. It names clocks exactly (`clk_out2_zynq_ps_bd_clk_wiz_c64_0`, `clk_out1_zynq_ps_bd_clk_wiz_c64_0`), never by wildcard.
- `create_project.tcl` adds the file name to its hard-coded implementation-only list (`USED_IN_SYNTHESIS false`, `PROCESSING_ORDER LATE`, create_project.tcl:161-164), because the clock-wizard hierarchy is black-boxed during top-level synthesis.
- (a) and (c) are plain XDC. (b) needs edge shifts derived from the fast clock: either per-standard literal values selected with the standard, or an unmanaged Tcl constraint file if the computed form is kept. E2 decides which form works [O].

The fast I/O path needs no constraint of its own. `machine_root_clock` already matches `*clk_out*zynq_ps_bd_clk_wiz_c64*` (multitini_timing.xdc:54-61) [V].

**TR2.** `DIRECT_ENABLE` on the island T65 clock-enable driver (IIgs lesson [V C64_TURBO_IIGS_LESSONS.md §4]).

**TR3.** Crossing registers use the prefix `isl_x_` (requests and the FIFO memory). They must not match the CDC first-stage regex (multitini_timing.xdc:70-73).

**TR4.** New exporter checks in `scripts/build_and_export_xsa.tcl`, after the NES MMCM gate (:229-256), keyed on `C64_NTSC_MODE` and `C64_ISLAND_CYCLE`:
- MMCM D/M/O0/O1 equal the reviewed values for the image's standard (PAL {9, 56.75, 30, 15}; NTSC {5, 24, 22, 11}).
- The pair is related:
  - same MMCM, MASTER_CLOCK `clk_fpga_2` at 6.666 ns;
  - period(c64) − 2 × period(fast) = 0 ps exactly;
  - fast waveform {0 7.929} PAL or {0 7.638} NTSC;
  - a named probe path fast→c64 has a requirement equal to one fast period.
- Print `C64_CLOCK_PAIR_PASS standard=...`.
- No false path, max delay or multicycle exception touches `c64_cpu_clk`, and no `isl_x_*` pin carries a false path.
- Island port-A clock pins: with `C64_ISLAND_CYCLE` = 1 the pin on the `c64_cpu_clk` net is inverted, and with 2 (F6) it is not; the pin on the `c64_clk` net is never inverted. Checked by net, not by pin name; the OOC run mapped the island port to CLKBWRCLK [V C64_TURBO_T65_TIMING.md §4].
- Island memories have no port in READ_FIRST, or `RDADDR_COLLISION_HWCONFIG` = DELAYED_WRITE.
- `main_ram_i` is 16 RAMB36 with `RAM_EXTENSION_*` = NONE. Island ROM copies are exactly 5 RAMB36.
- Every c64↔fast path (X1, X2, X6, X7, X8a, X8b) has a requirement of one fast period and positive slack. No c64-rise → fast-fall path exists.
- A named `c64_cpu_clk` half-period probe path (island T65 address → port-A BRAM) reports a clock uncertainty that includes the 0.200 ns user term, so a lost or misnamed duty constraint fails the build.
- Print the island slack separately: worst setup slack of `c64_cpu_clk` intra-clock paths and of the c64↔fast paths. M1 and the NTSC go/no-go use it (the island gate, section 10.2); release signoff keeps the existing image-wide gates (worst slack ≥ 0 in the exporter, build_and_export_xsa.tcl:600-611; WNS > 0 in packaging, package_cpu1_video_test.py:115-125).
- Release settings: every turbo build setting at its release default or at a listed release fallback, and every Ultimate Audio build parameter at its release default with `UA_COHERENCE_PROBE` = 0 (section 2.13.2), unless `MULTITINI_C64_DIAGNOSTIC_BUILD` = 1, which allows the diagnostic settings and sets CAPS2 bit 14. Print `C64_DIAGNOSTIC_BUILD=0|1` and every release fallback in use. The release packaging refuses an image with the bit set (Q14, Q16, Q19).

This replaces the earlier "≤4 logic levels" gate, which every build would fail.

**TR5.** Island Pblock: M1 builds with and without one (one BRAM column segment of at least 22 RAMB36 plus adjacent slices) and adopts the variant that closes, per standard.

**TR6.** Hold at coincident edges is ordinary BUFG-to-BUFG hold.

**TR7.** Two-clock RTL benches generate both clocks in one process in the same time step, at PAL and NTSC periods. Bench assertions check that a fast register updated at end f=31 is not seen by the c64 frame register at e_15, and that a FIFO entry pushed at a coincident edge is not accepted at that edge.

**Doc.** The text "No new clock or clock-domain crossing is added" in `docs/C64_RAM_CPU.md:97` [V] is replaced by this review.

---

## 4. Memory and BRAM plan

### 4.1 Per memory

| Memory | Port A (island) | Port B (native) | Organisation | BRAM change |
|---|---|---|---|---|
| Main RAM `main_ram_i` | negedge `c64_cpu_clk`; NO_CHANGE; EN and WE per 2.8; DIA = `lastVicDi_q` for PORTW, else DO | posedge `c64_clk`; machine read/write muxed with the loader (priority kept) and the host peek (read only, below the loader, only while paused; section 6.4); WRITE_FIRST (today's bus echo) | 2 × 32 KiB; each bank 8 × RAMB36 at 32K×1, no cascade (saves about 0.8 ns [V C64_TURBO_T65_TIMING.md §3]); A15 select registered on both ports | 0 (16 stays 16) |
| BASIC/KERNAL copy | negedge read, address {A14, A12:0} as buslogic:147 | loader write when `bios`="00" (adapter:313-326) | 4 × RAMB36 at 16K×2 | +4 |
| Character ROM copy | negedge read | loader write when `bios`="01" | 1 × RAMB36 at 4K×9 | +1 |
| Colour RAM | negedge; write 1 cycle; read captured into `io2_q` with {`lastVicDi_q`[7:4], q[3:0]}, then T65 enable one fast clock later (2 slots) | posedge, every clock as today: today's address, `cs_color & pulseWr_io` write, WRITE_FIRST as spram.vhd:29-38 | 1K×4 true dual-port, 1 RAMB18, moved to the adapter under `EXTERNAL_CPU` | 0 |
| Native BASIC/KERNAL and char dproms | not used | unchanged (buslogic:122-149) | — | 0 |

- The ROM copies cannot diverge from the native ROMs: ROM loads run only with the core held in reset [V adapter:378-385].
- **Island DI mux:**
  - One-hot select registered on the falling edge over {RAM_L, RAM_H, ROM, CHR, IO2, B0}, then the unchanged `localDi` mux in `cpu_6510` (cpu_6510.vhd:93-94).
  - IO2 is `io2_q`: the colour RAM read or the FIO read (section 2.13.4).
  - B0 serves exact mode and forwarded reads. It is a c64 register, stable since R.
  - Two LUT levels in all.
- **Option for E1:** read colour RAM directly into the DI mux (1 slot). Adopt it only if E1 shows no loss.
- The fast I/O path uses flops only: the FIFO is 4 × 22 bits.

### 4.2 Collision analysis

[V UG473 v1.14 p.20, Table 1-5 note]: for independent clocks, a collision is possible only when the port clock edges fall within "the lesser of 3000 ps or the two clock periods".

[I] Every port-A edge is a fast falling edge. Every port-B edge is a c64 rising edge, which coincides with a fast rising edge. Their separation is half a fast period, 7.64 ns (NTSC) or 7.93 ns (PAL), less:
- duty precision, 0.20 ns;
- static phase offset, 0.12 ns;
- BUFG skew, under 0.5 ns.

That leaves at least about 6.8 ns, more than 3 ns. Same-address accesses on the two ports therefore cannot collide. A diagnostic F6 build keeps the same separation with the other polarity (section 13.5).

| Port A | Port B | Possible? | Outcome |
|---|---|---|---|
| Island write X | VIC fetch of X (e_12, e_28), dummy read of `$0000` | yes, in turbo | No collision (edges ≥6.8 ns apart). The VIC sees old or new data, as on a real C64. Ordering against queued VIC register writes is rule O3 |
| Island `$FF00` local write | native repeat of the same byte (FWD_FF00 with `cpuHasBus`) | yes, in turbo | No collision; same data; the native write comes later |
| Island read | REU DMA, forwarded write, loader | no | Port A is disabled outside RUN (section 2.8); the loader forces exact mode |
| Island colour write | VIC colour read on every c64 edge | yes, in turbo | No collision, same argument |
| ROM copy read | Loader write | no | Core held in reset |
| Island access | Host peek read (port B) | no | The peek runs only while paused, when port A is disabled (section 2.12); otherwise it is refused (section 6.4) |

Write modes:
- Main RAM port A NO_CHANGE, port B WRITE_FIRST.
- Colour RAM port A NO_CHANGE, port B WRITE_FIRST.
- ROM port A read only.
- No READ_FIRST, so `RDADDR_COLLISION_HWCONFIG` stays DELAYED_WRITE.

Benches keep `SIM_COLLISION_CHECK = ALL` (M3). The earlier `wmask_q` fallback is removed.

Any later change that moves a BRAM clock phase reopens this analysis: a BUFGCE-derived clock, a shifted BRAM clock, or a non-2:1 ratio.

### 4.3 Budget

| Item | Value |
|---|---|
| BRAM today [V c705r2, C64_TURBO_BUS_CONTRACT.md §6] | 114 RAMB36 + 17 RAMB18 = 122.5 / 140 |
| BRAM after | 128.0 / 140 (91%), both standards: 127.5 for the island memories plus 0.5 for the audio capture FIFO (one RAMB18). Ultimate Audio adds no BRAM (docs/C64_ULTIMATE_AUDIO_DESIGN.md §4.1) |
| BRAM fallback (−5) | Make the buslogic dproms true dual-port (port B native read plus loader write; port A island). This edits an upstream-derived file |
| LUT [I] | about +1,510 (24,763 → about 26,275 of 53,200, 49%) |
| FF [I] | about +885 |
| Clocking | +1 BUFG, +1 MMCM output |

| Block | LUT | FF |
|---|---|---|
| Island map, grants, FSM, DI mux, PORTW mux, late-acknowledge decode | 320 | 200 |
| Proxy (no shadow port) | 130 | 120 |
| turbo_regs and policy | 190 | 95 |
| Mailboxes | 60 | 80 |
| Removed Smart-turbo logic | −40 | −10 |
| VIC fast port: read-mux copy (~100), write legs and decode (~110), holds/selective clears/busy (~30) | ~240 | 0 |
| fio_port (pop, holds, in-order done pipe, SID strobes) | ~55 | ~48 |
| Island FIFO (4 × 22), pointers, post-push barrier flags, `dep_ok`, ready terms | ~100 | ~110 |
| `fio_rd` select and U64 override (`io2_q` shared with COL) | ~20 | ~8 |
| turbo_regs fast snoop and read values | ~25 | ~5 |
| SID fast port: register-file mux (28), edge stage (84), read mux and latch rule (56), select (6) | ~175 | ~60 |
| I/O-only: buslogic decode and `vicRamLoc` (15-20), fpga64 `cpuIoOwn` (4), proxy bit (3), island `ioonly`/FWD_FF00 (15) | ~40 | ~5 |
| Host and debug: audio capture FIFO control and pop (~60), RAM peek (~40), REU cache invalidate (~10), guard holdoff, cause and static release (~30), NMI hold, rearm and carry (~8), CAPS2, sticky bits and the apply rule (~40), C64U grant rows (~10) | ~195 | ~165 |
| **Total** | **about 1,510** | **about 885** |

A diagnostic F6 build adds about 20 LUT. No DSP. Every new fast-I/O, host and debug block is [I] until M1/M4 utilisation reports.

**Combined C64 image (Q17)** [I until the M1 and UA-M4 reports]:
- Base, c705r2 PAL: 24,763 LUT, 22,998 FF, 122.5 RAMB36 equivalents and 26 DSP [V c705r2 utilization.rpt:25].
- Turbo adds about 1,510 LUT, 885 FF and 5.5 RAMB36 (above). Ultimate Audio adds about 2,200-2,600 LUT, 2,600 FF, no BRAM and 1-3 DSP (that design, §4.1).
- Together: about 28,500-28,900 LUT (54%), 26,500 FF (25%), 128.0 of 140 RAMB36 (91%) and 27-29 DSP.
- The combined build watches two tight timing domains: the 148.5 MHz video clock (+0.133 ns in c705r2) and `control_clk` (+0.446 ns) [V docs/C64_VICE310_COMPARISON.md:51 for the video clock; c705r2 timing_summary.rpt:202, 207]. Risk R34.

---

## 5. Native proxy and fast-port protocol

Notation as at the top of the document. Port-A BRAM edges are fast falling edges, in the middle of each f.

### 5.1 Exact (stock) cycle

| Edge | Domain | Action |
|---|---|---|
| R (end f=63) | c64 | If `ext_cpu_enable` and an exact request is presented: B0 ← {`cpuDi` or U64 override, IRQ term, NMI term, `baLoc`}; flip `b0_tgl`; the frame returns to the dummy (the request retired). U64 register write snoop on `cpu_bus_valid`. Without `ext_cpu_enable` (DMA, `memory_wait`) the frame keeps the unretired request |
| end f=0 | fast | T65 enable with DI = B0.data and IRQ_n/NMI_n/Rdy from B0 (NMI_n from the carry on the carry enables, section 2.9). The port register writes if the cycle was a `$00/$01` write. A/DO/R_W_n settle |
| e_0 (end f=1) | c64 | Native port tick (`tick_tgl` flips). The cartridge samples `nmi_ack` (same edge as today) |
| end f=1 | fast | `isl_x_req` ← {A, DO, WE, port, exact = 1, ioonly = 0, seq} |
| end f=2 | fast | Island port decay tick (after the enable, before the next one) |
| e_1 (end f=3) | c64 | Frame register ← `isl_x_req` |
| e_15 (end f=31) | c64 | VIC3: `cpuHasBus <= baLoc or cpuWe_pre` with the exact request (native rule unchanged) |
| p 16-31 | c64 | Native CPU half as today; issue at 28, retire at R |

### 5.2 Turbo forwarded read (for example `LDA $DC01`)

| Edge | Domain | Action |
|---|---|---|
| end f_r | fast | The T65 presents a forward-class address; Enable = 0. Once `all_done_q` (O2): `isl_x_req` ← {addr, we = 0, port, turbo, ioonly}; `seq` flips |
| e_15 (end f=31) | c64 | If `seq ≠ done_seq` and f_r ≤ 30: frame ← request, `frame.ioonly` ← `ioonly & ~cart_present`. In the same edge `cpuHasBus <= baLoc or cpuWe_pre(dummy = 0)`, which equals `baLoc` |
| e_16 | c64 | If `cpuHasBus` = 0, `dma_active` = 0 and `frame.ioonly` = 1: `cpuIoOwn` ← 1 |
| p 16-31 | c64 | VIC latches (VIC:300-309); CIA `phi2_n` at CPUC; slot issues at 28. With `cpuHasBus`: normal decode. With `cpuIoOwn`: chip selects from `cpuAddr`, the shared bus stays with the VIC. With neither (BA low, not eligible): the slot is inert, no chip select, no `systemWe` (buslogic:196-216) |
| R | c64 | If `cpu_bus_valid` (`cpuHasBus` or `cpuIoOwn`): B0.data ← `cpuDi` (or override), `done_seq` ← `seq`. **Always:** frame ← dummy; `cpuIoOwn` ← 0. If not valid, the request stays pending for the next e_15 |
| e_0 | c64 | CIA `irq_n` released after an ICR read |
| e_1 (end f=3) | c64 | B1 ← {`ext_dma_hold`, combinational blockers, mode/N/badline, pause}; `ack_tgl` ← done |
| end f=4 | fast | If B1.hold: DMA_WAIT. If an IRQ-acknowledge class: ACKWAIT. Else, with the completion credit or a grant (section 2.7): T65 enable with DI = B0.data, Rdy = 1, live IRQ, NMI per section 2.9; the credit clears; a mode change is applied here. With neither (only at N = 1, for f_r = 6-30) the same enable comes at the next grant, end f=5 |
| about end f=13 | fast | ACKWAIT exit: the island has registered the marker toggled at e_5; T65 enable (live IRQ, NMI per section 2.9), which uses and clears the completion credit (set by then at every N, section 2.7). With badline timing enabled, a BA fall at e_0 of this cycle withholds it (`halt_q`): the CIA2 case of section 2.9 |

### 5.3 Turbo forwarded write (for example `STA $DD00`, `STA $DF01`)

- Same as 5.2. DI is ignored, because writes loop DO back as DI (cpu_6510.vhd:93).
- The write executes in the loaded cycle if `baLoc` = 1 at e_15, or, with badline timing disabled and an eligible target, through `cpuIoOwn`. CIA `wr` at e_29 with `pa_out` at e_0; SID (`$DE00`/`$DF00` modes) via `pulseWr_io` at e_29; REU register update at R. `vicBus` stays `$FF` because `cs_vic` = 0. Otherwise it is reloaded at the next e_15; it is never kept in the frame across R.
- `STA abs,X` to φ2 I/O issues the dummy read and then the write: two φ2 in a row, about 2 µs. The C64U measures 1920 ns for `sta $de00,x` [M].
- A `$DD00` write still trips the IEC guard, captured at e_1 (section 1.9).

### 5.4 `$00/$01` write (turbo; local; Q3)

1. At the granted enable, `cpu_6510` updates its port register (`write_enable = accessIO & ~localWe & enable`, cpu_6510.vhd:103).
2. In the same cycle, port A writes RAM[`$0000`/`$0001`] with `lastVicDi_q` (PORTW class). This copies the stock rule (fpga64:1015) with the island's copy of the last VIC byte [I].
3. The new port value travels in `isl_x_req.port` with every later request and dummy. The native PLA therefore decodes every later forward and any DMA with the banking in program order:
   - DMA can start only after a forwarded trigger write, and the island cannot change its port while a forward is in flight.
   - Queued FIO entries were issued while I/O was visible and stay valid after a `$01` change.
   - [V C64_TURBO_BUS_CONTRACT.md §5c risk 6 names this alternative.]
4. The page map recomputes one clock later, with a `map_ok` bubble.

In exact mode, `$00/$01` writes are ordinary native cycles: the native RAM write uses `lastVicDi`, and port A is disabled.

The build fallback `FORWARD_PORT_WRITES=1` forwards them instead (D11).

### 5.5 REU trigger (`$DF01` with bits 7 and 4 set, or `$FF00` when armed)

| Edge | Action |
|---|---|
| before issue | The trigger forward waits for `all_done_q`, so every earlier VIC/SID write has landed (a stash of `$D000-$D02E` sees them). A `$FF00` trigger first writes RAM[`$FF00`] locally (FWD_FF00) |
| R | REU `active` ← 1 (reu.sv:163-170, 213-220); `dma_req` = 1 combinationally (reu.sv:76). With badline timing disabled this R may end a BA-low cycle (I/O-only) |
| e_1 | `dma_active` ← 1 (fpga64:465-466); B1.hold = 1 (because `dma_req` is in `ext_dma_hold`). The island enters DMA_WAIT without running any further cycle, i.e. it halts on the trigger write |
| … | DMA runs in native slots at 1 byte per φ2, with the post-access BA checks (fpga64:479-533). Takeover still waits for BA |
| first c64 edge with `ext_dma_hold` = 0 | The island sees it one fast clock later, completes the trigger write with an enable, and continues with live IRQ. That enable uses and clears the completion credit, which a grant has set by then at every N (section 2.7). Its first FIO access waits for `busy_q` = 0 |

### 5.6 Idle and dummy

- With no forward in the frame, the frame shows a read of `$0000` (we = 0, `ioonly` = 0). `$0000-$0FFF` is RAM in every mapping, Ultimax included (buslogic:286-293).
- The slot still issues, because `io_enable` = 1 (fpga64:1069-1072). With `cpuHasBus` = 0, the VIC's φ2 fetch uses it (fpga64:1021-1023).
- The dummy's `cpu_bus_valid` reaches the REU at address `$0000`, which matches neither IO2 nor `$FF00` (adapter:437-439).
- The proxy never counts a dummy retire as a completion.

### 5.7 Local cycle (turbo)

| Edge | Action |
|---|---|
| end f−1 | Enable(k−1); T65 outputs settle |
| mid f (fall) | Port A latches the address; EN uses registered terms only; WE (and DIA) for a write also uses the live local-write class with `map_ok` and `fetch_done_q`; the select register latches |
| end f | Enable(k) if `turbo_q & grant_q & run_q & ~halt_q & ~ack_wait_q & local class & map_ok`, and for a write also `fetch_done_q`. All terms are registered except the class |

### 5.8 IRQ-acknowledge class

| Access | Path | Effect on the IRQ term | Completion |
|---|---|---|---|
| CIA1/CIA2 ICR read (`$DC0D`/`$DD0D` and 16-byte mirrors) | φ2, normal or I/O-only | `irq_n` released at e_0 [V mos6526.v:518-526]; an ICR write never releases it (:522) | after the e_`IRQ_SAFE_PHASE` marker (default 5) |
| `$DF00` read (REU status, mirrors every 32 bytes) | φ2, normal or I/O-only | status cleared at R; `irq` combinational [V reu.sv:84, 157] | same |
| Ultimate Audio acknowledge write: `$DF3F`, `$DF5F`, … `$DFFF` (A4..A0 = `$1F`, A7..A5 ≠ 0) | φ2, normal or I/O-only | can release /IRQ (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.7, §1.10) | same |
| VIC `$D019` write (mirrors in `$D000-$D3FF`), including both RMW writes (`INC`/`LSR`/`ASL $D019`) | FIO, posted | `resetX` set at landing C2; IRST cleared at C2+1; IRQ combinational [V VIC:899, 1573, 1581-1584, 1684-1692] | `ack_wait_q` blocks every enable until `fio_done` passes the entry at C2 + `FIO_ACK_EDGES` (default 2) c64 edges; first enable 2 fast clocks later |
| VIC `$D01A` write | FIO, posted | ERST etc. at C2 [V VIC:1717-1721] | same |
| `$D019`/`$D01A` in the fallback `FAST_VIC_IO=0` | φ2 | `myWr_a` e_31 / `myWr_b` e_3 [I] | e_5 marker |

- The write's own enable samples IRQ before the acknowledge lands, as on stock; the next enable sees the released line. The T65's two-sample history gets the same sample sequence as stock, so `STA $D019 / RTI` cannot re-enter [I; T7].
- Worked case (index 15, `LSR $D019 ; RTI`, final write enabled at end t, t even) [I]: C1 = t+1, landing C2 = t+3, IRST clears at the c64 edge t+5 (VIC:899), the island's IRQ register sees the released line from end t+6. `ack_wait_q` is set at end t and, being registered from the post-push state, holds until `fio_done` reaches the entry: done at t+7 with `FIO_ACK_EDGES` = 2, so the first enable (RTI's first cycle) is at end t+9, two clocks after the line is visible. With `FIO_ACK_EDGES` = 1 the first enable is at end t+7, with zero clocks of margin. For t odd every edge moves by one. This depends on `ack_wait_q` holding from the push edge; the first v2 draft let it drop at t+1 (review finding 7).
- An RMW acknowledge (`INC`/`LSR`/`ASL $D019`) waits twice: once after the dummy write (which acknowledges, as on stock) and once after the final write.
- Default `IRQ_SAFE_PHASE` = 5: forward completion at about end f=13, 2 c64 clocks after the latest derived effect.
- E4 measures the real edges and sweeps `IRQ_SAFE_PHASE` from 3 to 7 and `FIO_ACK_EDGES` from 1 to 3.
- `RESUME_PHASE` (the B1 edge) is limited to 1-3. At 0, B1 would miss both the e_0 port update and the e_0 IRQ release.
- Not acknowledge class: `$D011`/`$D012` writes that make the raster compare match assert IRQ about 5-6 fast clocks after the write, not on the next enable. That is assertion latency, never a spurious interrupt [I]. Whether to add them is Q-V7.
- SID accesses are not in the class; the SID raises no interrupt.
- NMI: a CIA2 ICR read releases NMI at e_0, as it releases IRQ. NMI needs no acknowledge wait, because it is edge-triggered and a stale low cannot re-enter the handler. The risk is the opposite: with badline timing enabled, a BA fall at that e_0 withholds the completion enable, and a CIA2 re-assert during the halt is a high-then-low the T65 cannot see. Section 2.9 (case 3) takes it once; the T3 CIA2 bench checks it.
- Ultimate Audio acknowledge writes are decoded by address only (I/O visible, A15..A8 = `$DF`, A4..A0 = `$1F`, A7..A5 ≠ 0), independent of the UA state. With UA off they are REU mirror writes to an unused register, and the late completion costs only a few fast clocks. The `$DF00`-mirror read entry stays unchanged; with UA on, reads above `$DF1F` have no side effect and only complete a few fast clocks later (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.10). The address-only decode keeps UA state out of the island path; with UA off, a missed REU status acknowledge would be a real fault.
- The UA end-of-sample IRQ is a level, not an access: `irq_ext_n = !(reu_irq | ua_irq)`, where `ua_irq` is the OR of the voice flags (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.7, §2.2). It enters fpga64's IRQ term the same way as the REU IRQ. Only the `$1F` clear writes are acknowledge-class accesses; reads never clear UA flags. A clear write updates the flags at the R of its native cycle, and `irq_ext_n` rises before e_5, so the island's next enable, which comes after the marker, sees the released line and `STA $DF3F / RTI` cannot re-enter the handler. In turbo the island samples IRQ live at every enable; in exact mode it uses B0. When a flag is set depends only on the UA timebase on `c64_clk`, never on the CPU speed.

### 5.9 Latencies (fast clocks; 15.86 ns PAL / 15.28 ns NTSC)

| Case | Latency |
|---|---|
| Exact | One enable per native cycle, at f=0 after each retire |
| Local read or write | 1 slot (a write may wait for O3) |
| Colour RAM read | 2 slots |
| FIO read (VIC/SID) | 1 slot, or 2 when the previous clock had an enable; plus the wait for a pending WR entry or for a side-effect entry that changes the value read |
| FIO write (VIC/SID) | 1 slot (posted); lands 3-4 fast clocks later; done 2 fast clocks after landing |
| FIO acknowledge write | 1 slot, then about 9 fast clocks of `ack_wait_q` (`FIO_ACK_EDGES` = 2) |
| Forward requested at f_r ≤ 30 | Completion at f=68 relative to frame start: 38-68 clocks (0.60-1.08 µs PAL). At N = 1 only, f_r = 6-30 completes at f=69, the next grant (completion credit, section 2.7) |
| Same, IRQ-acknowledge class | f ≈ 77 |
| Forward requested at f_r ≥ 31 | +64 |
| Back-to-back forwards | exactly 64 (one φ2) each |
| BA low at e_15, badline timing enabled (or ineligible target) | +64 per BA-low cycle |
| BA low at e_15, badline timing disabled, eligible target | +0 (I/O-only) |
| REU trigger | + transfer time (1 byte per φ2) + post states |
| Native IRQ/NMI change to T65 input | 2 fast clocks; an NMI fall taken through the rearm (section 2.9, case 3) reaches the T65 one enable later |

### 5.10 I/O-only forwarded read, cycle by cycle (`LDA $DC0D`, index 15, badline timing disabled, BA low)

| Edge | Domain | Action |
|---|---|---|
| end f_r | fast | T65 presents `$DC0D`, Enable = 0. Class FWD_IO, `ioonly` = 1, acknowledge class. The island waits for `all_done_q` (O2), then registers `isl_x_req` and flips `seq` |
| e_15 (end f=31) | c64 | Proxy loads the frame (needs f_r ≤ 30), with `frame.ioonly` = 1. fpga64: `cpuHasBus <= baLoc or 0` = 0, `phi0_cpu <= 1`. VIC φ1 data edge |
| e_16 (end f=33) | c64 | `cpuIoOwn <= 1` |
| p17-p31 | c64 | buslogic VIC branch. `systemAddr` = `vicAddr` (BA-low cycle ≥ 4) or `cpuAddr` (cycles 1-3). `cs_cia1` from `cpuAddr`; `systemWe` = 0; `dataToCpu` = `cia1Do` |
| e_28 | c64 | Native slot issues. `ramCE` from `cs_ram` = the VIC's select, so the VIC's φ2 fetch reads RAM at `systemAddr`. `enableCia_n <= 1` |
| e_29 | c64 | CIA `rd`: `db_out <= ICR`, `int_reset <= 1` |
| R | c64 | `cpu_bus_valid` = 1. B0 ← `cpuDi` (CIA data); `done_seq` ← `seq`; frame ← dummy; `cpuIoOwn`, `cpuHasBus`, `phi0_cpu` ← 0. The VIC latches its own φ2 fetch |
| e_0 | c64 | CIA `irq_n <= 1` |
| e_1 | c64 | B1 capture |
| end f=4 | fast | Acknowledge class → ACKWAIT |
| about end f=13 | fast | e_5 marker registered → T65 enable, DI = B0, live IRQ |

### 5.11 Fast VIC/SID access, cycle by cycle

**Read** (`LDA $D012`): see section 2.13.4. The read completes on a granted edge X if clock X−1 had no enable, no WR entry is pending, `dep_ok` and `~busy_q` hold (and `~full_q` for a read that may push); DI = `io2_q`, captured at the end of X−1.

**Write** (`STA $D020` at index 15, enable at the end of t, t even):

| Edge | Domain | Action |
|---|---|---|
| end t | fast | T65 enable; entry {VIC, WR, `$020`, data} pushed; `all_done_q` and `wr_pend_q` register the post-push state (0 and 1) at this edge; the CPU continues next clock |
| end t+1 = C1 | c64 | fio_port accepts (no hold applies to `$D020`; `we_r` = 0); strobes registered |
| end t+3 = C2 | c64 | `$D020` takes the value; visible from the next pixel edge |
| end t+5 = C3 | c64 | `fio_done`++ |
| end t+6 | fast | `all_done_q <= (fio_done == wp)` = 1, `wr_pend_q` = 0; dependent enables from end t+7 |

---

## 6. Firmware, menu and ABI

### 6.0 ABI step and offset map, shared with Ultimate Audio (Q12, Q17)

**One step (Q17).**
- One spec edit raises the ABI in `spec/interface_v0.json` from 0.23 [V spec/interface_v0.json:3-6] to **0.24**. It adds every register, bit and constant of turbo, Ultimate Audio, REU image preload and the debug registers at once, then regenerates `hdl/generated/multitini_regs_pkg.sv` and `firmware/include/multitini_regs.h` with `scripts/generate_registers.py`. Commit 3fdea82 is the template.
- Only C64 images are rebuilt for the release. Other images keep their builds and ABI 0.23. Firmware already gates each package on its own ABI major and minor, so 0.23 and 0.24 images coexist [V firmware/platform/xilinx/main.c:416, 1946-1968, 2266-2267]. `MT_C64_EXPANSION_MIN_ABI_MINOR` stays 23 for the SID and REU rows [V firmware/include/multitini_regs.h:189].
- Neither feature ships alone. Shipping one without the other needs a new user decision and a second ABI step (section 10.3).

**Constants.** Each feature keeps its own constant, all equal to 24, so each feature gate stays independent of the others and downgrades stay safe. Firmware never hard-codes the ABI number.

| Constant | Value | Gates |
|---|---|---|
| `MT_C64_TURBO_MIN_ABI_MINOR` | 24 | 0x0D8-0x0E4, the turbo rows and actions |
| `MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR` | 24 | `C64_EXPANSION_CONFIG` bit 4, 0x0F0-0x0F8, the UA row |
| `MT_C64_REU_HOST_MIN_ABI_MINOR` | 24 | 0x110, REU image preload |
| `MT_DEBUG_MIN_ABI_MINOR` (new name) | 24 | 0x100-0x10C |

**Capability bits.** A capability register reads 0 on an image without the feature.

| Feature | Register: bits | First-release C64 image |
|---|---|---|
| Dual SID / REU 16 MB / REU in DDR | `C64_EXPANSION_CAPS` (0x0D4): bits 0 / 1 / 2 | existing, set |
| Ultimate Audio | `C64_EXPANSION_CAPS` bit 3, `MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO` = 0x08 | set |
| REU image preload (`C64_REU_HOST`, cache invalidate, UA hold) | `C64_EXPANSION_CAPS` bit 4, `MT_C64_EXPANSION_CAPS_REU_HOST_LOAD` = 0x10 | set |
| Expansion caps valid | `C64_EXPANSION_CAPS` bit 31 | set |
| Turbo | `C64_CPU_CAPS` (0x0DC): bit 31 valid; bits 16-30 (section 6.1) | bits 16-20, 22, 23, 28-31 |
| Turbo extras, timing revision | `C64_CPU_CAPS2` (0x0E4): bit 31 valid; [7:0] revision; bits 8-14 (section 6.1) | bits 8-11, 13, 31; revision 1 |
| C64 RAM peek (debug) | `C64_CPU_CAPS2` bit 13 | set |
| Diagnostic build (any diagnostic setting, including `UA_COHERENCE_PROBE` = 1) | `C64_CPU_CAPS2` bit 14 | clear; packaging refuses an image with it set |
| Audio capture (debug) | `DEBUG_STATUS` (0x104) bit 12 | set |

The top-level parameter `C64_EXPANSION_CAPS`, `32'h80000007` today [V hdl/board/multitini_one_top.sv:736], becomes `32'h8000001F` in the C64 image and stays 0 elsewhere.

**Gating rule (firmware).** A feature F is available on the active package only when all of these hold, checked in this order:
1. The package ABI major equals `MT_ABI_MAJOR`, and the package ABI minor is at least F's constant.
2. The live `ABI_VERSION` register says the same (the pattern of c64_content_service.c:33-41 [V]).
3. Only then does firmware read F's capability register. F is available when the valid bit and F's bits are set.

Firmware never reads or writes an F register before steps 1 and 2 pass. An unavailable feature hides its rows and refuses its commands with "<feature> not available on this image". A debug feature also needs a debug session (section 6.5). An image with ABI 0.23 or older therefore shows no new row and accepts no new command.

**Development images.** The spec edit lands once. A C64 image whose RTL lacks a feature reports that feature's caps as 0, so firmware hides it. No release ships until both features report their caps.

**Offset map.** Offsets 0x0D8-0x13C are free today [V spec/interface_v0.json; C64_TURBO_FIRMWARE_PATH.md §2]. No offset or bit below overlaps another. Firmware never accesses a reserved or free offset.

| Offset | Name | Access | Owner | Class |
|---|---|---|---|---|
| 0x0D0 | `C64_EXPANSION_CONFIG` | rw | existing; bit 4 (UA enable) docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1 | user |
| 0x0D4 | `C64_EXPANSION_CAPS` | ro | existing; bit 3 Ultimate Audio, bit 4 this design | user |
| 0x0D8 | `C64_CPU_CONFIG` | rw | section 6.1 (bit 19 shared with Ultimate Audio) | user; bits 7, 11, 16-17 and 19 have debug-only writers |
| 0x0DC | `C64_CPU_CAPS` | ro | section 6.1 | user |
| 0x0E0 | `C64_CPU_STATUS` | ro | section 6.1 | user; bits 24-26 debug (armed) |
| 0x0E4 | `C64_CPU_CAPS2` | ro | section 6.1 | user |
| 0x0E8, 0x0EC | reserved for turbo growth | — | — | not decoded |
| 0x0F0 | `C64_UA_STATUS` | ro | docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1 | bit 0 (`ua_fault`) user; the rest debug (armed) |
| 0x0F4 | `C64_UA_FETCHES` | ro | docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1 | debug (armed) |
| 0x0F8 | `C64_REU_MISSES` | ro | docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1 | debug (armed) |
| 0x0FC | reserved for the optional UA G9 probe register | ro | docs/C64_ULTIMATE_AUDIO_DESIGN.md §9 (G9) | decoded only in a diagnostic build with `UA_COHERENCE_PROBE` = 1; not decoded in a release image |
| 0x100 | `DEBUG_CONTROL` | rw | section 6.1 | debug |
| 0x104 | `DEBUG_STATUS` | ro | section 6.1 | debug |
| 0x108 | `AUDIO_CAPTURE_DATA` | ro, pop on read | section 6.1 | debug |
| 0x10C | `C64_RAM_PEEK` | rw | section 6.1 | debug |
| 0x110 | `C64_REU_HOST` | rw | section 6.1 | user (preload) |
| 0x114-0x13C | free | — | — | not decoded |

- The meaning of the Ultimate Audio registers and status bits is in docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.2-1.3 and §6.1. Their debug fields count only while `C64_CPU_CONFIG` bit 19 is armed (section 6.1).
- This map closes question 22, which kept it open until both documents matched (section 11).

### 6.1 Registers

**`C64_CPU_CONFIG` @ 0x0D8 (read/write)**, crossed by mailbox X3. Reserved bits read 0. Values outside the caps are clamped.

| Bits | Field |
|---|---|
| [3:0] | menu speed index |
| [5:4] | Turbo Control: 0 Off, 1 Manual, 2 U64 Registers, 3 TurboEnable |
| [6] | badline timing enabled (1 = enabled) |
| [7] | `c64u_top`: 1 = indices 14 and 15 use the C64U rows 47 and 63 (D17). Firmware-only: no menu row, set only by a UART debug command or a firmware build default, never persisted. (v2 used this bit for SuperCPU detect; reclaimed by Q11) |
| [8] | disk auto-slow (1 = on) |
| [9] | cartridge forces 1 MHz (meaningful once caps bit 21 is set) |
| [10] | `authoritative`: 1 = this word alone defines the mode (CONTROL bit 5 ignored); 0 = legacy, effective mode = CONTROL bit 5 ? field : Off |
| [11] | IEC static-hold release (1 = enabled). Ships 0; set only by a UART debug command (section 1.9) |
| [15:12] | apply counter |
| [17:16] | IEC holdoff: 0 = 500 ms (default), 1 = 100 ms, 2 = 50 ms, 3 = 20 ms. Set only by a UART debug command until E-IEC adopts a shorter default (section 1.9) |
| [18] | Expansion memory speed: 1 = Follow CPU (reads 0 unless CAPS2 bit 12; M9) |
| [19] | C64 diagnostics armed (shared with Ultimate Audio; Q19). One level gates every diagnostic counter and test-only status bit of both features: the sticky diagnostic bits 24-26 of `C64_CPU_STATUS` and the debug fields of `C64_UA_STATUS`, `C64_UA_FETCHES` and `C64_REU_MISSES` (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1). They count only while it is 1, and hold and read 0 while it is 0; its rising edge clears them. It is not an apply event and changes no behaviour. Set only by `:c64diag on` inside a UART debug session (section 6.5); never persisted |
| [31:20] | reserved |

- Reset value: `0x0000_0151` (unchanged from v2).
- **Apply counter** (Q9).
  - Firmware increments it only when Turbo Control, CPU Speed or Badline Timing changes value, whatever the source: a menu row, the one-step Turbo on/off row, a turbo action, a profile switch (byte 243) or the debug command `:speed`.
  - It never increments for: boot restore, migration or CRC repair of the settings; re-selecting the same value; a change to any other field (disk auto-slow, cartridge, expansion memory speed, `c64u_top`, holdoff, static release, C64 diagnostics armed); a change of an expansion row (SID audio, RAM expansion, Ultimate Audio).
  - The c64 side applies on a counter change: in Registers mode live := preference; in TurboEnable mode live := `$00` if the previous mode was not TurboEnable, otherwise live := preference only if live ≠ `$00` (D18); Manual uses the preference at once.
  - It never strobes on the first word after `c64_aresetn`, including the mailbox's force-send.
  - A level mailbox can collapse intermediate values, but the counter then still differs, so the apply still happens.
- A change to `c64u_top` is not an apply: it changes N for indices 14 and 15, which the island adopts at its next enable boundary like any N change (section 2.11).

**`C64_CPU_CAPS` @ 0x0DC (read-only)**

| Bits | Meaning |
|---|---|
| [15:0] | index mask (`0xFFFF`; `0x1FFF` on a diagnostic F6 image) |
| 16 | register modes |
| 17 | TurboEnable mode |
| 18 | badline control |
| 19 | C64U top rows (`c64u_top`) supported. (v2: SCPU detect; reclaimed by Q11) |
| 20 | disk auto-slow option |
| 21 | cartridge turbo (M7) |
| 22 | fast colour RAM (`FAST_COLOUR_RAM`) |
| 23 | fast VIC/SID registers (`FAST_VIC_IO & FAST_SID_IO`) |
| [27:24] | fast-clock multiple = 2 |
| 28 | BA-low internal I/O (`BA_LOW_IO`) |
| 29 | `$FF00` snoop (`FF00_SNOOP`) |
| 30 | local `$00/$01` writes (`!FORWARD_PORT_WRITES`) |
| 31 | valid |

The first release sets bits 16-20, 22, 23, 28-31. A fallback build reports what it implements, so a board log always shows which deviations are present.

**`C64_CPU_STATUS` @ 0x0E0 (read-only)**

| Bits | Meaning |
|---|---|
| [3:0] | effective index |
| 4 | fast mode active |
| 5 | C64U rows in use (`c64u_top` = 1) |
| [7:6] | disk auto-slow block cause, highest priority first: 1 line held, 2 parallel strobe held, 3 holdoff after an edge; 0 none (section 1.9) |
| [15:8] | live `$D031` |
| 16-20 | blocked by: cartridge, disk auto-slow, loader, reset window, pause |
| 21 | sticky: software changed the live speed since reset (a `$D031`/`$D030`/`$D07x` write changed live). User-facing hint; exposes the #287 junk-write trap |
| 22 | sticky: software asked for turbo through a register that is not live in this mode: a `$D031` write with a nonzero speed index, or a `$D07B`/`$D079` write, in Off or Manual. User-facing hint: "Software tried to set the CPU speed; select U64 registers" |
| 23 | sticky: a `$D030` write with bit 0 = 1 while `$D030` is not live (Off, Manual, U64 Registers). User-facing hint: "Software tried to use the C128 turbo bit; select TurboEnable bit" |
| 24 | diagnostic sticky: exact-deadline miss. In exact mode an exact request retired at R, and by e_14 of the next native cycle the proxy had loaded no new request although the island was still in exact mode (section 2.6). Stock equivalence needs this never to happen; the M2 lockstep asserts it (counts only while armed) |
| 25 | diagnostic sticky: an I/O-only access completed (gate G-BA2; counts only while armed) |
| 26 | diagnostic sticky: a turbo enable or a carry enable used the NMI hold or rearm (`fell_q` or `force_q` set; section 2.9) (counts only while armed) |
| 27 | reserved |
| [31:28] | last applied counter |

- Bits 21-23 clear on every C64 reset and on every apply. They drive menu hints (section 6.2) and are not debug features. Writes of zero (for example a loop that clears every VIC register) set neither bit 22 nor bit 23.
- Bits 24-26 are debug features (Q16, Q19). turbo_regs masks them to 0 whenever CONFIG bit 19 is 0, so they read 0 unless a UART debug session armed them, and again after a disarm (section 6.5). The same bit gates Ultimate Audio's debug fields; `C64_UA_STATUS` bit 0 (`ua_fault`) stays live, like bits 21-23 here.

**`C64_CPU_CAPS2` @ 0x0E4 (read-only)**

| Bits | Meaning |
|---|---|
| [7:0] | turbo timing revision, per image; 1 = first release with release defaults (section 2.7) |
| 8 | programmable IEC holdoff |
| 9 | IEC block cause in STATUS |
| 10 | IEC static-hold release |
| 11 | NMI hold and rearm (section 2.9) |
| 12 | Expansion memory speed: Follow CPU (M9) |
| 13 | C64 RAM peek (`C64_RAM_PEEK`) |
| 14 | diagnostic build: a diagnostic setting of section 2.13.2 is present, for example `C64_ISLAND_CYCLE` = 2 (F6) or Ultimate Audio's `UA_COHERENCE_PROBE` = 1 |
| [30:15] | reserved |
| 31 | valid |

The first release sets bits 8-11, 13 and 31. An image with release defaults reports revision 1. An image built with a release fallback that changes the timing (section 2.13.2) reports the next free revision number, and the release notes map each revision to its images.

**0x0E8, 0x0EC:** reserved for turbo growth, not decoded. **0x0F0-0x0F8:** Ultimate Audio's registers. **0x0FC:** reserved for Ultimate Audio's optional G9 probe, decoded only in a diagnostic build (section 6.0).

**`DEBUG_CONTROL` @ 0x100 (read/write; reset 0; core-independent; Q16)**

| Bits | Field |
|---|---|
| 0 | debug enable. Firmware sets it only during a UART debug session (section 6.5) |
| 1 | audio capture run; has an effect only while bit 0 is 1 |
| 2 | audio capture clear: write 1 to empty the FIFO and clear the overflow bit (self-clearing) |
| [31:3] | reserved |

**`DEBUG_STATUS` @ 0x104 (read-only)**

| Bits | Meaning |
|---|---|
| [9:0] | audio capture FIFO level (0-512) |
| 10 | audio capture overflow (sticky until cleared) |
| 11 | debug enable (echo of `DEBUG_CONTROL` bit 0) |
| 12 | audio capture present in this image |
| [31:13] | reserved |

**`AUDIO_CAPTURE_DATA` @ 0x108 (read-only).** {left[23:8], right[23:8]} of one 48 kHz sample. A read pops one entry only when debug enable = 1 and the FIFO is not empty; otherwise it returns 0 and pops nothing. It follows the existing pop-on-read pattern of `MEDIA_DATA` [V multitini_control_regs.sv:499-500].

**`C64_RAM_PEEK` @ 0x10C (read/write; debug only)**

| Bits | Write | Read |
|---|---|---|
| [7:0] | address bits 7-0 | data of the last completed peek |
| [15:8] | address bits 15-8 | 0 |
| [19:16] | request counter: a new value starts a peek | done counter: equals the request counter once the peek has completed or been refused |
| 20 | — | refused: the last request found debug off, the machine not paused, or the loader writing, and made no RAM access |
| [31:21] | reserved | 0 |

**`C64_REU_HOST` @ 0x110 (read/write; user feature, REU image preload; Q15)**

| Bits | Write | Read |
|---|---|---|
| [3:0] | invalidate request counter: a new value requests one REU cache invalidate | written value |
| [7:4] | — | 0 |
| [11:8] | — | invalidate done counter: equals the request counter once the four read lines are invalid |
| 12 | — | REU idle: no transfer active and the cache and DDR path not busy (`!reu_memory_busy`, adapter:474 [V]) |
| 13 | UA hold: 1 holds Ultimate Audio inactive (in reset, buffers empty, no new read), as when its option is off (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.8). Stored as written; no effect unless `C64_EXPANSION_CAPS` bit 3 is set | written value |
| 14 | — | UA held: 1 once the c64 side has applied the hold and the UA block is in reset. Reads 0 without `C64_EXPANSION_CAPS` bit 3 |
| [31:15] | reserved | 0 |

- Crossing X11: the request word carries {request counter, UA hold}; the response word carries {done counter, REU idle, UA held}. Both are level words with counters, never strobes.
- In the c64 domain, `reu_active = cfg[3] & ~cart_loaded & ~cart_loading` stays today's rule [V hdl/core/c64/multitini_c64_adapter.sv:427]; `ua_active = cfg[4] & reu_active & ~ua_hold`; `ua_held = ua_hold & ua_block_in_reset`, registered. Ultimate Audio is held in reset whenever `ua_active` = 0 (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.8).
- `C64_EXPANSION_CAPS` bit 4 (`REU_HOST_LOAD`) says the image has this register and the invalidate input; Ultimate Audio uses bit 3. The C64 image's parameter becomes `32'h8000001F` (section 6.0).
- `C64_EXPANSION_CONFIG` (0x0D0) bit 4 is Ultimate Audio's enable. Firmware sets it only together with bit 3 (the REU), and the control-register clamp clears it unless bit 3 and caps bit 3 are set, so the register can never read back "UA on, REU off" (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1). Its reset value stays `0x0B` (dual SID at `$D500`, REU on, UA off) [V firmware/include/multitini_regs.h:195].

**Firmware rules:**
- CONTROL bit 5 stays (test_interface_spec.py:121).
- New firmware writes the config with `authoritative` = 1 and also sets bit 5 = (mode ≠ Off) for compatibility.
- Every access follows the gating rule of section 6.0, in its order: package ABI, then live ABI, then the feature's capability bits (pattern at c64_content_service.c:33-41). Turbo uses `MT_C64_TURBO_MIN_ABI_MINOR` with `C64_CPU_CAPS` and `C64_CPU_CAPS2`. REU image preload uses `MT_C64_REU_HOST_MIN_ABI_MINOR` with expansion caps bit 4. The debug registers 0x100-0x10C use `MT_DEBUG_MIN_ABI_MINOR`, with CAPS2 bit 13 for the peek and `DEBUG_STATUS` bit 12 for audio capture. Firmware reads a capability register only after the package and live ABI say that it exists.
- Firmware that touches C64 state (preload, peek, capture) uses wall-clock waits with timeouts and never counts C64 cycles as delays (C64_TURBO_U64_ISSUES.md §3(g)9, U64 commit 21adfd11).

### 6.2 Menu and controls (core_frontend.c:111-147; main.c:4505-4608)

**Options-tab rows and argument numbers.** All rows use `MT_MENU_ACTION_TOGGLE_CORE_OPTION`. Arguments are semantic, not row positions [V tests/host/test_core_frontend.c:157-158]. This is the full order of the C64 Options tab, turbo and Ultimate Audio together (Q17):

| Screen order | Argument | Row | Values | Shown when | Owner |
|---|---|---|---|---|---|
| 1 | 0 (kept) | Fast serial ROMs | unchanged | ABI ≥ 8 | existing |
| 2 | 1 (kept) | CPU turbo | Off / Manual / U64 registers / TurboEnable bit; legacy Off / Smart 2x when the ABI is below the turbo minimum | ABI ≥ 8 | this section |
| 3 | 9 | Turbo on/off | On / Off: one press toggles between Off and the stored mode | turbo available | this section |
| 4 | 4 | CPU speed | 1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64 MHz; index 0 with badline timing disabled shows "1 MHz, badlines off (not cycle-exact)"; it never runs more than one CPU cycle per φ2 (section 2.7). Editable in every mode, including Off (it edits the stored preference) | turbo available | this section |
| 5 | 5 | Badline timing | Enabled / Disabled; editable in every mode | turbo available | this section |
| 6 | 6 | Disk auto-slow | On / Off (disk access may fail) | turbo available | this section |
| 7 | 7 | Cartridge turbo | Allowed / Forces 1 MHz | `C64_CPU_CAPS` bit 21 (M7; not in the first release) | this section |
| 8 | 10 | Expansion memory speed | C64U (1 byte per cycle) / Follow CPU | `C64_CPU_CAPS2` bit 12 (M9; not in the first release) | this section |
| 9 | 2 (kept) | SID audio | unchanged | expansion caps bit 0 | existing |
| 10 | 3 (kept) | RAM expansion | 16 MB REU / Off. On an image with Ultimate Audio, turning it off also turns Ultimate Audio off | expansion caps bit 1 | existing; help text in docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2 |
| 11 | 8 | Ultimate Audio $DF20-$DFFF | On / Off. On also turns the REU on (Q18; docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.3). While UA is off and a cartridge is attached, the row is disabled and shows "Eject cartridge first"; after a memory error it shows "Fault" (enable rule and states in docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2) | expansion caps bits 1 and 3, and the UA ABI | docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2 |
| 12 | 11 | reserved: Ultimate Audio level row | — | only if UA O3 or listening tests show a need; not in the first release | docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.6 |

- "Turbo available" means the turbo feature passes the gating rule of section 6.0.
- The turbo rows keep explicit numbers: v2's turbo rows took 4-7, Ultimate Audio takes 8, the two new turbo rows take 9 and 10, and the optional UA level row takes 11.
- The first release shows 9 rows (orders 1-6 and 9-11). With M7 and M9 it shows 11. With the UA level row as well it shows 12, which equals `MT_MENU_MAX_CORE_OPTIONS` [V firmware/include/multitini_menu.h:18]. If the level row is ever added, its storage comes from the reserved bits of byte 26 (section 6.3); until then those bits stay reserved and ignored.
- Routing in `handle_menu_action` (C64 core): arguments 4, 5, 6, 7, 9 and 10 go to the turbo handler, placed before today's `file_index >= 2` expansion branch (main.c:4505-4508 [V]). Arguments 2, 3, 8 and 11 go to `change_c64_expansion()`. Argument 0 keeps today's path; it restarts the C64 and is the only option that does (main.c:3551-3558 [V]). Argument 1 keeps today's CPU turbo path, extended by this section.
- A change of argument 2, 3 or 8 makes one settings save, then one `C64_EXPANSION_CONFIG` write that carries bits [4:0], so the REU and UA bits change on the same `c64_clk` edge. docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2-6.4 owns the apply table, the restore rule and the UA row states and texts (Q18).
- Removed (Q10, Q11): no per-title settings action and no SuperCPU row.

**Visibility** (Q9; the Bus Operation Mode lesson, C64_TURBO_U64_ISSUES.md §3(e)):
- The CPU turbo row value and the status line when the menu opens show the requested speed, the live `$D031`, the effective speed and the reason. Examples: "64 MHz, live $8F"; "64 MHz, now 1 MHz: disk"; "(disk: line held)"; "(disk: cable held)"; "(cartridge: 1 MHz)"; "reset window"; "(max 32 MHz on this image)" (diagnostic F6 images only).
- STATUS bit 22 shows the hint "Software tried to set the CPU speed; select U64 registers". Bit 23 shows "Software tried to use the C128 turbo bit; select TurboEnable bit". Bit 21 shows "Speed set by software" next to the live value.
- The speed and badline rows stay editable while Off; they edit the stored preference, as the U64 keeps the speed while Off (u64_config.cc:1610-1622).
- Help text is ASCII only, because `mt_ui_cell` maps everything outside 32-127 to `?` (ui.c:9-11) [V]:
  - Speed: "Speed in multiples of the C64 clock; 48 and 64 MHz give 48x and 64x."
  - Badline timing: "Disabled: the CPU and internal I/O ignore badlines, like the Ultimate 64. Timing-sensitive code may break."
  - Disk auto-slow, Off: "Fast loaders, JiffyDOS and GEOS may crash, and disk writes can be corrupted."
  - TurboEnable: "For C128-aware games; try CPU speed 2 MHz for titles that time border code for a C128."
  - Turbo on/off: "Switch turbo off or back to the stored mode and speed."
- Enter cycles each row through an `mt_c64_next_speed(index, mask)` helper modelled on the IIgs one. Optionally, generic Left/Right on rows marked steppable (C64_TURBO_FIRMWARE_PATH.md §2).
- On a diagnostic F6 image (never released, Q14) the CPU speed row cycles only the indices in mask `0x1FFF`. The stored global index is not rewritten, so switching back to an unrestricted image restores 64.
- Rows apply while the menu pauses the machine. That keeps argument 1's paused-apply rule (test_menu_runtime.py:381-382).
- Labels stay U64 nominal MHz on PAL and NTSC.
- No debug facility of either feature has a menu row (Q16, Q19).

**Turbo actions** (Q9(f)). Three supervisor actions for the C64 core: "Turbo on/off", "CPU faster" and "CPU slower".
- Unassigned by default; bindable in Controls like the screenshot actions; consumed before gameplay routing, like the other system actions (controls.c:295-299 [V]).
- "Turbo on/off" toggles per-profile byte 243 between Off and the stored mode (byte 27 keeps the mode and index while Off).
- "CPU faster" and "CPU slower" step the stored speed index by one table entry within the caps mask and saturate at the ends. "CPU faster" while Off turns turbo on in Manual mode one step above the stored index; "CPU slower" while Off only shows "Turbo is off".
- Every real change goes through the apply path (section 6.1), saves the settings as a menu change does, and shows a status message with the requested and effective speed and the reason. A turbo action, like a menu change of a turbo row, first ends any `:speed` override and steps from the stored values (section 6.5).
- Actions apply live, without pausing. The island adopts the change at its next enable boundary, the same path a `$D031` write takes (section 2.11), so no pause is needed.
- **Storage.** The bindings cannot go into the schema-10 controller record:
  - the record has exactly `MT_CONTROL_COUNT` = 17 bindings, enforced by a static assert (supervisor_settings.c:33-41 [V]);
  - and schema-10 firmware rejects the whole settings file if a controller record binds an action outside the core's action mask (controls.c:160-170; supervisor_settings.c:465-470 [V]).

  So `MT_CONTROL_COUNT` stays 17. `mt_controller_config_t` gains `extra_binding[3]` in memory, and the three bindings are stored in a companion file `0:/multitini/turbo_controls.bin`, with its own magic, version and CRC, journalled like `settings.bin` (supervisor_settings.c:829-865 [V]) and keyed by core id and controller identity (`mt_controls_identity_equal`). Schema-10 firmware never reads it, so a downgrade loses only these bindings. A missing or bad file means "unbound". This is design decision DL2 (the user may override): schema 11 would be simpler code, but a downgrade to older firmware would then drop every saved setting.
- The Controls page shows the three rows only for the C64 core.

### 6.3 Persistence (schema 10 kept; Q5, DL1, DL2)

**Per-profile byte 243** stays 0/1: 0 = turbo Off, 1 = turbo on with the byte 27 detail. Today's decoder rejects values above 1 [V supervisor_settings.c:598, 763, 772].

**Global byte 27** (turbo detail; Q5) holds `mode<<6 | badline_disabled<<5 | follow_cpu<<4 | index`:
- [7:6] mode: 0 = no stored detail, so the legacy default applies (Manual, index 1, badline timing enabled) and bits 5:0 are ignored; 1 = Manual; 2 = U64 Registers; 3 = TurboEnable.
- [5] badline timing disabled.
- [4] "Expansion memory speed: Follow CPU" (M9; 0 = C64U rate). Written 0 until CAPS2 bit 12 is set, and ignored on decode without that bit. v2 used it for SuperCPU detect; reclaimed by Q11.
- [3:0] speed index, clamped to the image's mask at apply and never rejected.
- The byte has no reserved bits; every value decodes. The detail stays stored while byte 243 is 0, so Off keeps the speed, as the U64 does (u64_config.cc:1610-1622).

**Global byte 26** (flags; defined in this release, Q17). Zero means default:

| Bit | Meaning (1 =) | Owner | Default (0) |
|---|---|---|---|
| 0 | disk auto-slow Off | this design | On |
| 1 | cartridge forces 1 MHz (written 0 until `C64_CPU_CAPS` bit 21) | this design (M7) | Off |
| 2 | Ultimate Audio on | docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.4 (DL1) | Off |
| 3-7 | reserved: written 0; **ignored on decode, never rejected**, so later flags stay downgrade-safe | — | — |

**Per-profile expansion byte** (at `MT_SETTINGS_V9_WIRE_SIZE` + profile index): [2:0] SID mode, [3] REU [V supervisor_settings.c:608-609]. Bits 7:4 must stay 0, because a schema-10 decoder rejects the whole file otherwise [V supervisor_settings.c:699-703]. Ultimate Audio is never stored there. The per-profile REU default stays On [V firmware/platform/xilinx/supervisor_settings.h:22].

**How the Ultimate Audio and REU flags work together** (Q18, DL1). docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.3-6.4 owns the apply table and the restore rule; this section owns the storage:
- every change of the SID audio, RAM expansion or Ultimate Audio row writes the active profile's SID and REU flags (expansion byte bits 2:0 and 3). A change that flips the UA bit (UA on, UA off, or REU off while UA is on) also writes the global UA flag (byte 26 bit 2, only on an image with Ultimate Audio) in the same save. One save is followed by one register write. If the save fails, firmware restores the settings and does not write the register;
- after every apply, an effective UA bit of 1 implies that the active profile's stored REU = 1;
- at restore, firmware computes the effective state from the stored flags and saves nothing. A profile whose cartridge is stored with its REU stored off restores the cartridge with REU and UA off, and the global flag stays unchanged (that design, §6.3). A later SID or REU change in that profile does not flip the UA bit, so it leaves the global flag unchanged too.

**Downgrade and upgrade.**
- Schema-10 firmware at HEAD neither writes nor validates bytes 26-27: the encoder zero-fills the wire and writes 24, 25 and 28+ [V supervisor_settings.c:573, 577-580], and the decoder does not check them [V supervisor_settings.c:686]. It keeps the file, but its next save zeroes bytes 26-27. A downgrade therefore loses the turbo detail, the two turbo flags and the UA flag. The per-profile REU, SID and byte 243 survive, and the old firmware ignores the companion file. A downgrade no longer wipes the whole file.
- On upgrade, zeros decode as defaults.

**API.** `mt_supervisor_settings_t` gains the byte 26 flags (including `bool c64_ultimate_audio`, default false) and the byte 27 detail. A combined remember call, or two calls before one save, updates the profile SID/REU and the global UA flag; one `save_persistent_settings()` call follows.

**Sharing and other stores.**
- **Q5 (approved):** global storage is shared by all C64 packages (for example a c64fast package) and by the `pal` and `ntsc` variants of `c64`. Byte 243 is per profile of package `c64`, so both variants share it too.
- The chosen variant is already saved per profile; a package holds up to 8 variants (main.c:2213-2262) [V].
- Turbo action bindings live in the companion file `0:/multitini/turbo_controls.bin` (section 6.2; DL2). It has its own magic, version and CRC, is journalled like `settings.bin` and is keyed by core id and controller identity. A missing or bad file means unbound. Schema-10 firmware never reads it.
- **Never persisted:** the debug session, the C64 diagnostics armed flag, `c64u_top`, the IEC holdoff and static-release fields, a `:speed` override, audio capture state, RAM peek state, the UA hold and every Ultimate Audio debug field (section 6.5). These live outside `mt_supervisor_settings_t`, so every save writes only stored values.
- **No per-title settings** (Q10): no sidecar `.cfg` is read, and there is no "Save turbo settings for this title" action.

### 6.4 Host features (first release; Q15)

Three host features serve users and the board gates. REU image preload is a user feature; RAM readback and audio capture are debug features (section 6.5). Every one works only under the safety rules below.

**(a) REU image preload** (user feature; Doom C64U needs it, doom README:102-124).
- *Use.* The C64 file browser of the media menu offers "Load into REU" for `.reu` files (raw REU images, at most 16 MiB); it loads at REU offset 0, which Doom C64U needs. The UART command `:reuload FILE.reu [OFFSET]` does the same, with an optional offset (default 0); it is a user command like `:mount`, available without a debug session. Bytes outside the loaded range keep their content: the PS writes only the file's bytes, through the aperture's non-cacheable mapping (below), and the step-5 invalidate only drops REU read lines and writes no DDR. The C64 is not reset; the program that uses the image is loaded next, as on the U64.
- *Refusals,* each before step 1 and with no register write: the capability bit absent ("REU image load not available on this image"); file size plus offset above 16 MiB; a cartridge attached (it excludes the REU, adapter:427); a content load (PRG, CRT or disk launch) in progress ("Busy; try again"). With RAM expansion off the image is still written and kept: reset does not erase DDR [V docs/C64_EXPANSIONS.md], and the status says "REU image loaded; turn on RAM expansion to use it". Ultimate Audio is then off as well, because it is never on while the REU is off (Q18). The preload never writes `C64_EXPANSION_CONFIG`, so it never turns the REU or Ultimate Audio on or off.
- *Aperture mapping (firmware; PS cache rule).* At CPU0 start-up, next to the shell mapping (main.c:5059) and before CPU1 is released (main.c:5073), firmware maps the whole REU aperture, 0x25000000-0x25FFFFFF (16 sections of 1 MiB; spec/core_interface_v0.md:54), as Normal non-cacheable: `Xil_SetTlbAttributes(section, NORM_NONCACHE)` for each section, `NORM_NONCACHE` = 0x11DE2 (xil_mmu.h:54) [I].
  - Not `DEVICE_MEMORY`: `memcpy` makes unaligned accesses, which fault on Device memory.
  - Each call cleans and invalidates the whole data cache, L1 and L2 (xil_mmu.c:86-106 [V]), so no line cached under the old attributes survives. CPU1 is not yet running, so the full flush does not disturb the video worker.
  - Every PS access to the aperture uses this mapping and no other: the preload copy (step 4) and `:reudump` (6.4 (b)). The standalone flat map has no other alias of these addresses, and CPU1 never accesses them.
  - Facts [V]: the BSP maps all DDR as Normal write-back write-allocate cacheable (`SECT + 0x15de6`, translation_table.S:23, 113-114) and enables L1 and L2 prefetch (boot.S:126, 374-375). No firmware file maps or references the aperture today (grep). The repo already maps PL-shared DDR as `NORM_NONCACHE` to "Avoid cacheable aliases" (video_worker.c:110-117; also xusbps_class_storage.c:375). BSP line numbers refer to the standalone BSP that scripts/create_vitis_workspace.py generates under build/vitis_workspace (not tracked); regenerate it to recheck them.
  - Safe to remap [V]: the CPU0 image ends below 0x20000000 and CPU1 owns 0x20000000-0x20FFFFFF (scripts/create_vitis_workspace.py:33-34, 694-695); the CPU1 mailbox and frame buffers lie at 0x21000000-0x229FFFFF (multitini_video_worker.h:9; multitini_video_frame.h:10-14). No code, stack, heap or other buffer lies in the aperture.
  - Why [I]: with the cacheable mapping, a clean line left by `:reudump`, or brought in by prefetch or speculation, goes stale once REU DMA writes DDR through HP2, which the PS caches do not snoop. A preload that starts or ends inside such a line merges into it, and the flush writes the stale neighbouring bytes back over the C64's data: up to 31 bytes on each side of the range (an upper bound), none when the offset and the end are 32-byte aligned (C64_TURBO_MODELS.md (preload_cache.py)). A flush after the copy follows only half of the rule in firmware/golden/lib/common.h:181-187.
  - Scope: firmware only. No HDL changes, so exact mode, stock REU behaviour, the idle `invalidate` input and G-REU0 are unaffected (Q8, Q20).
  - Cost [I]: uncached stores still merge in the store buffer, so the copy stays faster than the SD reads.
- *Sequence (firmware).* All waits are wall-clock waits; firmware never counts C64 cycles as delays (section 6.1, firmware rules).
  1. Pause the machine through the menu pause path and wait for `core_paused` (timeout 100 ms).
  2. If the image has Ultimate Audio (`C64_EXPANSION_CAPS` bit 3), write `C64_REU_HOST` bit 13 = 1 (UA hold) and wait for bit 14 (UA held) = 1 (timeout 100 ms). UA stops, empties its buffers and issues no new read; a read already in flight completes and is discarded, as on a C64 reset (docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.8).
  3. Wait for `C64_REU_HOST` bit 12 (REU idle) = 1 (timeout 1 s). Paused, no transfer can start; idle means the write-combining buffers have drained to DDR (the cache `flush` input is `!reu_busy`, adapter:453-454 [V]). If the machine was paused in the middle of a REU transfer, the REU stays busy and the load is refused ("REU busy; try again").
  4. Copy the file into the REU DDR aperture `0x25000000 + offset` (multitini_c64_ddr_path.sv:69) in blocks, through the non-cacheable mapping above and no other path, with no cache maintenance: the PL reads DDR through HP2 and HP3, not through the CPU caches (firmware/golden/lib/common.h:181-187 [V]). End the step, also when it fails, with `dsb`, an L2 cache sync and `dsb` again. The PL310 runs with Early BRESP (AuxControl 0x72360000, boot.S:126; bit 30, xl2cc.h:101 [V]), so a store the CPU sees as complete can still sit in the L2 controller's buffers until the sync. The BSP's `Xil_L2CacheSync` is static (xil_cache.c:166-172 [V]), so firmware writes the Cache Sync register (`XPS_L2CC_BASEADDR + XPS_L2CC_CACHE_SYNC_OFFSET`, xl2cc.h:61) itself.
  5. Write a new invalidate request counter to `C64_REU_HOST` [3:0], and wait until the done counter [11:8] equals it (timeout 100 ms). On a timeout, write a new counter once more and wait again (timeout 100 ms); a second timeout takes the step-5 error path below.
  6. Write bit 13 = 0. UA leaves the hold in its reset state (voices idle, flags clear), as after a C64 reset.
  7. Resume the machine.
- *Error paths.* Every wait has a timeout, and each timeout or failure has one defined path:
  - A timeout at steps 1-3: clear the hold if it was set, resume, and report the cause; no DDR byte was written.
  - A read or write failure during step 4: end step 4 with its barrier, still run step 5, then steps 6-7, and report "REU image load failed; REU contents are partial".
  - A second timeout at step 5, after a full copy or after a step-4 failure: keep UA held if step 2 set the hold (bit 13 stays 1), skip steps 6-7 and keep the machine paused, also when the menu closes. Report "REU cache did not clear; reload the C64 core". Until the C64 core is reloaded, firmware refuses resume, content loads and preloads [I]. Resuming would let REU reads return up to four stale 32-byte read lines (128 B). Only a hard PL reset, which a core reload gives, clears the read-line valid bits; a guest reset or turning the REU off does not (multitini_c64_reu_cache.sv:9-10, 117-118; adapter:454 [V]). After the reload the user loads the image again; the design does not rely on DDR contents across a core reload [I].
  - A step-5 timeout is not expected [I]. Step 3 saw the cache idle and fault-free (`busy` includes a latched fault, multitini_c64_reu_cache.sv:113 [V]), and nothing issues REU traffic while the machine is paused, so a timeout means a lost X11 handshake or a stopped c64 clock.
- *HDL scope.* `multitini_c64_reu_cache.sv` gains an `invalidate` input that clears the four read-line valid bits. It acts only when the cache is not busy; a request that arrives while busy waits for idle. Stock behaviour is unchanged: the input is idle unless firmware loads an image, and an invalidate while idle only costs a refetch, because write buffers are empty when idle. The request counter and the UA hold cross to the c64 side, and the done counter, REU idle and UA held cross back, by mailbox (X11). In the adapter, UA hold joins Ultimate Audio's inactive condition and the registered `ua_held` echo returns through X11 (section 6.1). Adapter wiring and `C64_EXPANSION_CAPS` bit 4.
- *Why the sequence is coherent.* Paused and idle: no REU transfer runs, and the REU write queue has drained to DDR. UA held: no UA buffered word from the old contents survives, and UA issues no read during the copy. UA reads samples from REU memory through its own buffers and does not snoop PS writes (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.4-2.5), so the hold is required. Non-cacheable aperture: no CPU cache line holds REU data, so the copy writes only the file's bytes, never stale neighbours, and the step-4 barrier puts them in DDR before the invalidate. Invalidate: the REU cache drops its four read lines, so REU reads see the new image. The `invalidate` input stays idle unless firmware preloads, so stock REU behaviour does not change (Q8, Q20). This design owns the hold bit, the UA held bit and the sequence; docs/C64_ULTIMATE_AUDIO_DESIGN.md adds the hold term to its inactive condition, the `ua_held` echo and a bench case (its §1.1, §1.8, §2.5 and §9, T8).
- *Tests.* Host tests: size and offset limits, every refusal, the order pause → UA hold → UA held → idle → copy → barrier (`dsb`, L2 sync) → invalidate → UA release → resume, every timeout and error path, status texts. Mapping: start-up maps all 16 aperture sections `NORM_NONCACHE` (0x11DE2) before CPU1 release, and neither the preload nor `:reudump` calls a cache range function on the aperture (mocked MMU and cache hooks record the calls). Step-5 timeout (mocked done counter that never moves): exactly one retry, then bit 13 stays 1, steps 6-7 are skipped, the status reads "REU cache did not clear; reload the C64 core", and resume, content loads and preloads are refused; the same after an injected step-4 read failure. HDL bench `tb_c64_reu_cache` additions: a stale read line is gone after invalidate; an invalidate while busy waits; the existing REU suites (machine, native, cache, VICE aggregate) pass unchanged with the input idle (Q8). A UA-side bench case (X-T5, section 10.3): the hold empties the buffers and blocks issue; an in-flight read is discarded; `ua_held` rises; release returns UA in its reset state. Board: Doom C64U with `assets.reu` (mapOK and resident blocks, gate below; UA G8); Expand disks 1 and 2 with Ultimate Audio off after a 16 MiB preload (G-REU0); X-PRELOAD (section 10.3).

**(b) C64 RAM readback over UART** (debug feature).
- *Use.* With a debug session on: `:peek ADDR [COUNT]` (COUNT ≤ 256, hex dump); `:ramdump START LEN` (LEN ≤ 65536; hex lines and a CRC32); `:reudump START LEN` (REU memory). gist874, gist839, Amiga64 and Doom leave their results in C64 RAM or REU memory, and Multitini had no C64 RAM peek [V C64_TURBO_BUS_CONTRACT.md §0.4].
- *Safety.* Every command pauses the machine for its duration and resumes it after. A peek reads main RAM through port B, the path the PRG loader writes while paused (multitini_c64_local_ram.sv:10-26; adapter:387-395, 504-505 [V]), with write disabled and the loader taking priority. The c64 side refuses a request, without any RAM access, when debug is off, `core_paused` = 0 or the loader is writing. In turbo the island is halted while paused (port-A EN = 0, section 2.12), so the peek meets no island access (section 4.2). `:reudump` also waits for REU idle, then reads DDR through the aperture's non-cacheable mapping (section 6.4 (a)); it needs no cache maintenance and leaves no CPU cache line that REU DMA could make stale. Colour RAM and I/O registers are not read: an I/O read can have side effects.
- *HDL scope.* `C64_RAM_PEEK`, the X10 mailbox, a read-only peek input on `multitini_c64_local_ram.sv`, and the small c64-side block `multitini_c64_ram_peek.sv`.
- *Tests.* Bench `tb_c64_ram_peek`: data equals RAM for random addresses while paused; refused (no RAM access, no loader disturbance) while running, while the loader writes, and with debug off; no effect on machine RAM traffic after resume. Host tests: command parsing, limits, CRC, refusal when debug is off with no register access.

**(c) Audio capture** (debug feature).
- *Use.* With a debug session on: `:audiocap SECONDS [NAME]` (1-10 s) records the machine audio at 48 kHz into a DDR buffer, then writes a 16-bit stereo WAV file to `0:/multitini/capture/` and prints a summary (samples, overflow, peak). `:audiocap SECONDS tone F1 F2 …` also prints Goertzel energies at the listed frequencies against the median of the ±200 Hz neighbourhood, which gate G-BA1 uses at 1953 and 1967 Hz.
- *HDL scope.* New `hdl/board/multitini_audio_capture.sv` on `control_clk`: a 512-entry FIFO (one RAMB18) written with {left[23:8], right[23:8]} of the machine sample that feeds the S/PDIF path (the `machine_audio_to_control` output, before the UI mute) at each control-domain 48 kHz tick (multitini_audio_test.sv:128-133, 177-185 [V]). `multitini_audio_test.sv` only exports those two signals; its outputs do not change. A full FIFO drops the sample and sets the overflow bit. No new crossing (X12). It works for every core's audio, but this design wires it for C64 images only.
- *What it records.* The captured sample is the summed machine audio, SID plus Ultimate Audio, because UA joins the SID audio at the adapter's 48 kHz point upstream of the tap (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.7). The tap is before the UI mute. Gate G-BA1 runs with Ultimate Audio off.
- *Firmware.* A non-blocking `service_audio_capture()` in the main loop drains the FIFO into the DDR buffer, like `service_screenshots` (main.c:4904-4930). The FIFO holds 10.7 ms. A capture with overflow is reported invalid, and the gate runner repeats it. The WAV file is written after the capture ends.
- *Tests.* Bench `tb_audio_capture`: words equal the sampled values in order; no push and no pop with debug off; overflow sets and clears. Host tests: WAV header and length, Goertzel against synthetic tones, refusal when debug is off with no register access.

**Licences.** Check the licence of every third-party test program before use. Store them in `build/`, never commit them.

### 6.5 Debug facilities (off by default; UART only; Q16, Q19)

**What counts as a debug facility here** (Q16, Q19), for turbo and Ultimate Audio alike: audio capture; C64 RAM and REU readback (`:peek`, `:ramdump`, `:reudump`); the diagnostic sticky STATUS bits 24-26; trace and dump commands (`:turbostat` and its turbo-state log); the benchmark command `:speed N`; the experiment knobs `c64u_top`, IEC holdoff and static-hold release; diagnostic builds (F6 and the other diagnostic settings of section 2.13.2, including Ultimate Audio's `UA_COHERENCE_PROBE` = 1 and its G9 probe register at 0x0FC); and Ultimate Audio's `:uastat` command and debug status fields (`C64_UA_STATUS` bits 1-3, 8-14 and 16-31, `C64_UA_FETCHES`, `C64_REU_MISSES`; docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1). User-facing features stay in the menu: REU image preload, the speed, badline, disk and expansion rows, the Ultimate Audio row, the turbo actions, the visibility texts with STATUS bits 21-23, and the Ultimate Audio "Fault" state from `C64_UA_STATUS` bit 0, which firmware reads without a session.

**Enable mechanism** [I for the session rules]:
- `:debug on` starts a debug session; `:debug off` ends it; `:debug` prints the state. These are extended commands of the existing UART console (`handle_extended_command`, main.c:1730 [V]), and only the normal console path accepts them, not the recovery console.
- The session is a flag in firmware RAM. It is never written to SD or flash. A reboot, a golden update or a power cycle ends it.
- A core switch keeps the session (DL5). Whenever a C64 image becomes active, firmware rewrites `DEBUG_CONTROL` bit 0 from the session flag and `C64_CPU_CONFIG` bit 19 from a separate armed flag, because both registers reset with the PL (RST4). Only `:c64diag on` sets the armed flag; `:c64diag off`, `:debug off` and a reboot clear it. So a core switch never arms the diagnostic fields by itself.
- Nothing else can start a session: not the menu, not the settings file, not C64 software.
- `:debug on` prints a banner that names the facilities of both features that it unlocks. `:debug off` restores every experiment knob to its release value (`c64u_top` = the firmware default, holdoff 500 ms, static release off, diagnostics not armed), ends a `:speed` override (one apply if the stored values differ), clears `DEBUG_CONTROL` and stops the turbo-state log.

**`:speed` override.** Firmware keeps the override in an effective-configuration overlay outside `mt_supervisor_settings_t`; the CONFIG word is built from the overlay while it is active. A save therefore writes only stored values, whatever triggers it. The menu rows show and edit the stored values. A menu change of a turbo row, a turbo action or a profile switch ends the override first, then applies the stored values together with that change, and the actions step from the stored index. `:speed` without an argument ends the override.

**Debug-only commands** (outside a session, each refuses with "debug is off; send :debug on" before it touches a register, a file or the pause state):

| Command | Effect |
|---|---|
| `:turbostat` | prints CONFIG, CAPS, CAPS2 (timing revision, diagnostic-build bit), STATUS decoded (cause, sticky bits), `core_variant_index` (1 on NTSC, top.sv:739-741); starts a UART log of turbo-state changes (apply events, live `$D031`, block cause), polled at about 10 Hz |
| `:speed N` | Manual mode at index N for this session, through the apply path, as an override (above); not saved |
| `:turbo c64u on\|off` | sets `c64u_top` (A/B test of 47/63 against 48/64) |
| `:iec holdoff 500\|100\|50\|20` | sets the IEC holdoff (E-IEC) |
| `:iec release on\|off` | enables the static-hold release (experiment) |
| `:c64diag on\|off` | both features: arms or disarms `C64_CPU_CONFIG` bit 19, which gates the diagnostic STATUS bits 24-26 and every Ultimate Audio debug field; arming clears them |
| `:peek`, `:ramdump`, `:reudump` | RAM and REU readback (section 6.4 b) |
| `:audiocap` | audio capture (section 6.4 c) |
| `:uastat` | Ultimate Audio: prints `C64_UA_STATUS` decoded, `C64_UA_FETCHES`, `C64_REU_MISSES`, `C64_EXPANSION_CONFIG` bits 3-4, `C64_REU_HOST` bits 12-14, and in a probe build the 0x0FC register. Without expansion caps bit 3 it refuses with "Ultimate Audio not available on this image". Its debug fields read 0 unless `:c64diag on` armed them |

The existing `:diag` command prints read-only diagnostics, is ungated and is the one command allowed while a golden update holds CPU1 [V main.c:1746-1754]. It stays as it is, and so do `:hid`, `:display`, `:c64rx` and every other existing console command (DL5). The arming command has its own command word, `:c64diag`, so each command word keeps one gating rule. User commands stay ungated: `:reuload FILE.reu [OFFSET]` (section 6.4) and `:mount`.

**When debug is off, each facility has no effect and no hardware access:**
- Firmware: every debug command refuses before touching a register, a file or the pause state. Host tests check this against the register mock for every command.
- Audio capture: `DEBUG_CONTROL` bit 0 = 0, so the FIFO never takes a sample and a data read pops nothing and returns 0.
- RAM peek: the request word carries debug enable = 0, so the c64 side refuses without a RAM access.
- Diagnostic fields: CONFIG bit 19 = 0, so turbo_regs masks STATUS bits 24-26 to 0 and every Ultimate Audio debug field holds 0 and reads 0, also after a disarm.
- Experiment knobs: at their release values (reset value and `:debug off`), never persisted.
- Diagnostic builds: the build scripts produce them only with `MULTITINI_C64_DIAGNOSTIC_BUILD=1` (F6 also needs `MULTITINI_C64_ISLAND_CYCLE=2`, accepted only then; `UA_COHERENCE_PROBE` = 1 is accepted only then too); the exporter prints `C64_DIAGNOSTIC_BUILD=1` and sets CAPS2 bit 14; the release packaging refuses such an image. If one is loaded, firmware prints "diagnostic build" on the UART at activation; the menu shows only user-facing effects, such as the F6 speed cap.
- User-facing state stays live without a session: the STATUS bit 21-23 hints, `C64_UA_STATUS` bit 0 (Fault) and the preload.

**Board gates enable debug first** (Q19). Every board gate run of either feature, by-ear and menu checks included, follows one procedure:
1. Send `:debug on` and check the reply.
2. Send `:c64diag on` if the gate reads diagnostic fields (turbo STATUS bits 24-26, the Ultimate Audio counters, `playing`).
3. Log `:turbostat` (CONFIG, CAPS, CAPS2, STATUS). For a gate that involves Ultimate Audio or the REU, also log `:uastat`.
4. Run the gate, then log again at the end.
5. Send `:c64diag off`, then `:debug off`.

A session changes nothing a gate measures: it only unlocks debug facilities, and every experiment knob stays at its release value unless the gate sets it. One stock regression run per image also runs without a session, as a control. It is not a gate result and is exempt from the logging rule. It is valid when its results equal those of the session run and a session opened right after it logs the same CONFIG, CAPS and CAPS2 words. Gate results count only from session runs that logged the caps, CAPS2 and status words, plus `:uastat` for gates that involve Ultimate Audio or the REU.

---

## 7. Beyond 64x: no

| Option | Gain | Timing evidence | Verdict |
|---|---|---|---|
| 80x (2.5× clock; PAL 78.82 MHz; NTSC has no exact plan from VCO 720) | RAM-only +25% over 64x. Random φ2 I/O at 0.5%: 47.8 → 56.1x; 1%: 38.4 → 43.6x; 2%: 27.7 → 30.3x; 5%: 15.8 → 16.6x (C64_TURBO_MODELS.md (top_rows_model.py, using the method of perf.py)) | Out of context it met only with the modified T65 + split RAM + extras (+0.120 ns); unmodified extras-split at 80 MHz is −0.213 [V C64_TURBO_T65_TIMING.md §2]. A 5:2 ratio breaks the half-period edge separation used in section 4.2 | No |
| 96x (3× clock) | 1%: 47.9x; 5%: 17.1x | Fails out of context even with the modified T65 (95 MHz −0.065) [V] | No |
| Cycle folding | 1.1-1.3x | — | Forbidden: "Do not fold dummy accesses" (C64_RAM_CPU.md:23-24) |
| Pipelined or look-ahead T65 | about 100+ MHz possible | Not measured; a large rewrite with an NMOS-equivalence burden (C64_TURBO_T65_CORE.md §3b) | No |

All 64 slots at index 15 are now adopted (Q9; D17). v2 rejected them ("Max 64: no") only because the spec copied the C64U table (C64_TURBO_U64_SPEC.md §F, item F11); the user rule Q7 supersedes that for this item (C64_TURBO_U64_ISSUES.md §3(a)). It costs nothing: the slot was free.

Other reasons:
- The U64 register interface ends at index 15.
- Doom C64U is already vsync-locked at 50 fps at 64 MHz [M].

The levers that matter are the I/O paths. Both are in the first release:
- **Fast colour RAM** takes the screen-plus-colour fill loop from 7.5x to about 60x.
- **Fast VIC/SID registers** take VIC- and SID-heavy loops from one φ2 per access to about one slot (section 8).

---

## 8. Performance model

All figures are [I], from C64_TURBO_MODELS.md (final.py, checked against sim.py) for φ2-class rows, C64_TURBO_MODELS.md (lead_model.py, final_model.py) for fast VIC/SID rows, and C64_TURBO_MODELS.md (top_rows_model.py) for the index 14 (48) and 15 (64) columns.

Assumptions: cutoff f = 30, resume f = 4, rotated grants, all 64 slots at index 15, badline timing disabled, screen off unless stated, first-release build (fast VIC/SID with the global write barrier, the side-effect rule of section 2.13.4 and fetch-ordered local writes, `FIO_ACK_EDGES` = 2). µs figures are PAL (1 φ2 = 1.015 µs); NTSC values are 0.963 times as long. With `c64u_top` = 1 the index 14 and 15 columns take the N = 47 and N = 63 values of C64_TURBO_MODELS.md (top_rows_model.py). The RAM loop row gives the delivered cycle rate; the 300-iteration loop model gives 31.98x, 47.95x and 63.88x at indices 12, 14 and 15, because it counts the start-up slot.

| Workload | Index 4 (6) | Index 9 (16) | Index 12 (32) | Index 14 (48) | Index 15 (64) |
|---|---|---|---|---|---|
| RAM `lda abs,x / dex / bne` | 6.0x | 16.0x | 32.0x | 48.0x | 64.0x |
| `lda $dc01,x / dex / bne` (µs per iteration) | 2.0 | 2.0 | 1.0 | 1.0 | 1.0 |
| `sta $de00,x / dex / bne` (µs per iteration; C64U-matching class) | 3.0 | 3.0 | 2.0 | 2.0 | 2.0 |
| Unrolled `lda $d012` or `lda $d419` (φ2 per read) | 0.667 | 0.250 | 0.125 | 0.094 | 0.078 (C64U 0.080 / 0.085 [M]) |
| Unrolled `lda $3000` (RAM, φ2 per read) | 0.667 | 0.250 | 0.125 | 0.083 | 0.0625 (C64U at 63: 0.064 [M]) |
| `sta $d020,x / dex / bne` (φ2 per iteration) | 1.667 | 0.625 | 0.313 | 0.219 | 0.172 |
| Same, φ2 fallback build (`FAST_VIC_IO=0`, µs) | 3.0 | 3.0 | 2.0 | 2.0 | 2.0 |
| `lda $d012 / cmp # / bne` poll (φ2 per iteration) | 1.500 | 0.563 | 0.281 | 0.188 | 0.156 |
| `stx $d020` then poll `$d012` (raster bar, φ2 per iteration) | 2.167 | 0.813 | 0.406 | 0.313 | 0.250 |
| Unrolled `inc $d020` (φ2 per `inc`) | 1.000 | 0.375 | 0.188 | 0.188 | 0.156 |
| Unrolled `lda $d01e`, collision present (φ2 per read) | 0.667 | 0.250 | 0.125 | 0.125 | 0.125 |
| Doom `miPlay` 25-register SID burst, music.asm:348-358, 26 cycles per register (µs) | 110 | 41.2 | 20.6 | 14.3 | 10.7 (φ2 fallback: 50.8) |
| Same burst with a 16-cycle `lda abs,x / sta $d400,x / inx / cpx # / bne` loop (C64_TURBO_FAST_SID.md's assumption for irqtest) (µs) | 67.7 | 25.4 | 12.7 | 8.7 | 6.7 |
| Unrolled `lda $de80` (φ2 per access) | 1 | 1 | 1 | 1 | 1; elsewhere 2 at 2-4 MHz (U64E `meas_c`: 2 at 3-5 MHz [M]; the 2 MHz value is the model's) and 4 at index 0 with badline timing disabled (section 2.7) |
| Fill `$0400,x` + `$D800,x`, colour local (2-slot dummy read) | about 5.6x | about 15x | about 30x | about 45x | about 60x |
| Same fill, colour forwarded (fallback `FAST_COLOUR_RAM=0`) | 3.8x | 5.0x | 7.5x | 7.5x | 7.5x |

Fast-I/O micro-costs at index 15 (fast clocks) [I, C64_TURBO_MODELS.md (top_rows_model.py, using the final_model.py rules); v2's index-63 values in brackets]:
- `STA $D019` plus a 2-cycle instruction: 14.0 [14.2] (12.0 with `FIO_ACK_EDGES` = 1; 6 if both were RAM).
- Unrolled `STA $D018 / STA $0400` (O3 wait): 12.0 [12.0]; a colour register in place of `$D018` does not wait: 8.0 [8.1].
- The raster-bar loop with the precise read barrier (`FIO_RAW_PRECISE` = 1) would take 14.0 instead of 16.0 [14.2 instead of 16.0].
- A `$D41B` read followed by a `$D41D` read (latch readback): 12.0 per pair instead of 10.0 [12.2 instead of 10.1].

Random φ2-class I/O fraction (Amdahl; CIA and I/O1/I/O2 only; VIC/SID accesses cost 1-2 slots and count as local here; C64_TURBO_MODELS.md (top_rows_model.py), seed 3):

| φ2 I/O share | Index 15 (64) | Index 14 (48) | Index 9 (16) |
|---|---|---|---|
| 0.1% | 59.9x | 45.7x | 15.8x |
| 0.5% | 47.9x | 38.4x | 14.9x |
| 1% | 38.6x | 32.3x | 13.9x |
| 2% | 27.8x | 24.3x | 12.3x |
| 5% | 15.8x | 14.5x | 9.2x |
| 10% | 9.5x | 8.9x | 6.6x |

The C64U rows give 59.1x / 44.8x at 0.1%, and values within 0.5x of these at 0.5% and above.

Other effects:
- **Badline timing enabled, screen on:** BA is low for about 5.5% (PAL) / 6.3% (NTSC) of cycles. An index-15 RAM loop drops to about 60.5x PAL / 60.0x NTSC before sprites (59.6x / 59.0x with the C64U row, confirmed by the fidelity model).
- **Badline timing disabled, screen on:** no change for local or fast VIC/SID cycles. A random φ2 forward no longer waits for BA (1.20 µs PAL / 1.38 µs NTSC on average under D5; 4.22 µs PAL with 8 sprites on every line) [I C64_TURBO_MODELS.md (balow_calc.py)].
- **BASIC compute:** near the local rate. The 60 Hz KERNAL IRQ costs about 30-35 forwarded accesses (mostly CIA), about 0.05 ms per 16.7 ms.
- **IRQ-acknowledge class:** about 9 extra fast clocks per acknowledge. Negligible.
- **Doom `irqtest` mode 3** (one 25-register SID burst per interrupt; 1970 IRQ/s as assumed by C64_TURBO_FAST_SID.md): loop rate drops against mode 0 by about 2.1% with the `miPlay` loop (10.7 µs × 1970/s) or 1.3% with the 16-cycle loop; under D1 about 10% (50.8 µs × 1970/s). `src/irqtest.asm`, which fixes both the loop and the IRQ rate, is not in the downloads [O]: the T4 and M5 expectations take both from it at a pinned commit. The Doom music player itself (music.asm) runs 50 times per second, so its in-game cost is about 0.05% of CPU time at index 15.
- **Not helped:** raster- or CIA-paced games, REU-bound code (1 byte per φ2, as on the C64U, until the opt-in M9), and anything under disk auto-slow.

---

## 9. Risks, ranked, with the experiment that retires each

| # | Risk | Experiment | Fallback ladder |
|---|---|---|---|
| R1 | Vivado times the c64/fast pair as inexact or unexpandable (1 ps rounding; the NTSC half-ps tie); every crossing fails | **E2 / E2-NTSC** (minutes): generate clk_wiz with overrides in a scratch project; read back D/M/O from the generated wrapper; `report_clocks`, `report_clock_interaction`, requirement of one fast→c64 probe path, DJ/uncertainty; N1 vs N2 jitter for NTSC | Constraint redefinition (3.1 step 2); then the ps-exact plans (PAL −102 ppm, NTSC −48 ppm N4) |
| R2 | Island half-period paths in the full image. NTSC is tightest (7.64 ns half), with the duty term: out-of-context margin about +1.20 ns PAL / +0.91 ns NTSC before real island logic (−0.3 to −0.8 ns) and placement noise (±0.8 ns) [I from C64_TURBO_T65_TIMING.md, C64_TURBO_IIGS_LESSONS.md §4]. The FIO read muxes load T65 A0-A3 (about 136 + 40 loads), which drive the critical A → BRAM path | **E1** (hours): OOC sweep to failure of the real island RTL (registered port-A EN, class-dependent T65 Enable and port-A WE, full map, DI mux with B0/IO2/CHR, PORTW mux, colour port, FIO read muxes and `io2_q` leg, FIFO accept path) with the 0.2 ns duty uncertainty. **M1:** full-image skeleton, PAL and NTSC, with and without the Pblock, judged by the island gate (section 10.2) | F1 Pblock + address replication / `MAX_FANOUT`. F2 OR data mux with RSTRAM-zeroed outputs (if inference works with inverted clocks). F3 IIgs repair recipe (place sweep, reroute, post-route phys_opt; temporary margins cleared before signoff). `VIC_READ_GAP` = 3 (registered read address; +1 slot at N ≥ 33). F4 T65 generic `FAST_NMOS_PATHS` (default false); it changes the stock CPU too (R13). F5 posted local write with a 1-clock bubble (about 55x); it delivers fewer than 64 cycles per φ2 at index 15, which Q9(a) does not allow, so it needs a user decision before it ships. F7 `FAST_COLOUR_RAM=0` / `FORWARD_PORT_WRITES=1` for the failing image. `VIC_READ_GAP` = 3, F7 and (in both images) F4 are release fallbacks (section 2.13.2). If NTSC still misses 64x, NTSC is held back and PAL ships alone (Q14). If PAL misses 64x at the end of its ladder, the user decides (question 2). F6 `ISLAND_CYCLE=2` (rising-edge BRAM, 32x cap, caps mask `0x1FFF`) remains a diagnostic build only, never shipped (section 13.5) |
| R3 | Stock exactness regression through the proxy, the VIC/SID patches or the buslogic I/O-only edit, including forced-exact periods, reset release and the exact↔turbo seams | **M2** lockstep (section 10) with `FAST_IO=true` idle, including the reset release through `ext_reset` and the exact→turbo seam cases; T1-F0 (`FAST_PORT`=false suites unchanged); assertions A1-A3, A8 | Fix before any turbo work; `FAST_IO=false` |
| R4 | IRQ-acknowledge phases are derived, not simulated; IRQ latency in polling loops; re-entry if the FIO acknowledge wait is too short | **E4 / T7**: index 15/14/12 raster IRQ acked by `STA`, `LSR`, `ASL $D019`, `STA $D01A`; CIA `LDA $DC0D` (normal and I/O-only); CIA2 NMI; REU status; Ultimate Audio acknowledge writes `$DF3F`…`$DFFF` (UA on and off, normal and I/O-only; docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.10); no re-entry after acknowledge + RTI; handler entry within a few native cycles while the main loop polls `LDA $D012/CMP/BNE`, `BIT $D011/BPL`, `LDA $DC01/CMP/BEQ`; sweep `RESUME_PHASE` 1-3, `IRQ_SAFE_PHASE` 3-7, `FIO_ACK_EDGES` 1-3 | Parameters |
| R5 | Fast-I/O ordering bugs: lost, duplicated or reordered writes; double clears; stale reads; stale barrier flags; an I/O-only access with a side effect but no completion | **T3** scoreboard, **ordering fuzz** (O1-O8) at `FIO_DEPTH` 1/2/4 and `FIO_ACK_EDGES` 1-3, with and without `FIO_RAW_PRECISE`; assertions A1-A8 (A5/A7 on pointer indices, so a stale flag cannot hide); mode-switch fuzz | `FAST_VIC_IO=0` / `FAST_SID_IO=0` / `BA_LOW_IO=0` per piece |
| R6 | REU trigger halt and DMA start, now also from BA-low cycles and the `$FF00` snoop; register writes one φ2 apart against a cache qualified for four native cycles | **E5** (`tb_c64_reu_machine`, `reu_test.tas` pattern, index 15 and 1, with the sweep's cases in section 1.8 and the REU start-offset assertion) and **E-BA3** (program inside a badline; triggers in BA-low cycles 1, 3, 5 and 20; fetch, stash, swap and verify with `$D031` = `$00`/`$80`/`$0F`/`$8F`; `FF00_SNOOP` 1 and 0); byte compare against the stock run; prefetch slot count; G-REU0 on the board | `FF00_SNOOP=0` |
| R7 | Forwarded or I/O-only accesses disturb badline fetches | **E6**: badline + sprites while hammering `$D020`/`$D016` at 64x, badline timing on and off; VIC fetch trace equals stock with the same register writes; no `cpuWe_pre` = 1 at any e_15 in turbo. **E-BA1**: VIC fetch trace with I/O-only accesses equals the dummy-frame reference (colour nibble masked in BA-low cycles 1-3, D7) | — |
| R8 | MMCM plan accepted by clk_wiz (override untested in this repo), PAL fractional-M jitter, odd-divide duty | E2 / E2-NTSC | Direct `MMCME2_ADV` outside the BD, with `c64_clk` brought into the BD as an input feeding `S_AXI_HP2_ACLK` and `reset_c64`. This renames the derived clocks away from `*clk_wiz_c64*`, so the fallback must either name the two generated clocks with today's names (`create_generated_clock -name clk_out1_zynq_ps_bd_clk_wiz_c64_0` / `clk_out2_...`) or update every filter that uses them: the exporter's "selected machine root clock" check and C64 gates (build_and_export_xsa.tcl:219-224), `machine_root_clock` (multitini_timing.xdc:54-61), the new C64 turbo XDC, and the repair scripts |
| R9 | BRAM at 91%; M7 may add more | Utilisation in M1 | True dual-port buslogic ROMs (−5) |
| R10 | Text-anchor churn in test runners (test_c64_machine_schedule.py:35-89, test_c64_vice_bus.py:19-32, test_c64_vice_cpu.py:233-236, REU machine, interrupts, test_c64_mame_sid.py:42-76) and in the VIC port script anchors (`replace_once` fails on a non-unique anchor, port_c64_vic.py:30-33); fpga64 lines near 530 and 1039 change | Scheduled into M2 (CPU) and M4 (fast I/O) | Keep the `EXTERNAL_CPU=false`, `FAST_IO=false` text intact |
| R11 | IEC/JiffyDOS with disk auto-slow off (below U64 parity, section 1.9); a shorter holdoff or the static-hold release breaking a loader | G-IEC with the guard Off records the failures for the help text; E-IEC at each holdoff value with a 2× margin | Default On (Q6); Off labelled "disk access may fail"; holdoff and release stay at their shipped values unless E-IEC and telemetry pass |
| R12 | PAL clock change (−5.6 ppm, or −102 ppm on the fallback) | Re-run tb_c64_video_bridge and test_video_settings; audio drift check | — |
| R13 | F4 changes the stock CPU | If F4 is used: pass-through generic in `cpu_6510`; VICE CPU suite, MAME trace, Dormann, SST and the lockstep with the generic true; the drive keeps the default | Do not use F4 |
| R14 | Fast colour RAM and the PORTW DIA mux on the critical half | E1 variants | `FAST_COLOUR_RAM=0`; `FORWARD_PORT_WRITES=1` (D11) |
| R15 | VIC landing semantics: another hazard like H1/H2 that the RTL reading missed (review finding 3 found the calc-cycle e_14 edge; finding 11 found `baCnt`'s any-clock reset); U64 landing behaviour unknown; a T2 reference that loses sticky events and so fails correct RTL (section 14.4) | **T2** long fuzz with pre-edge compares including `baCnt` and `aec`, a per-event scoreboard for the sticky flags with its directed cases (section 10.2, T2), and negative controls (H1 off, H2 off, H2 calc phase off, scoreboard off must each fail); **E7-V1** and **E7-V9** on a C64U; T6 demos | Add a hold; documented detail (D14) |
| R16 | Simulation runtimes (section 10) | Measure in M2/M3 | Per-commit and nightly split |
| R17 | M5 board gates need the host features (REU preload, RAM readback, audio capture), now in the first release (Q15); if one slips, its gates block | Section 6.4 benches and host tests | On-screen printing versions of the result programs; listening for G-BA1 |
| R18 | The `$8F` no-growl gate needs both the fast SID write and the I/O-only CIA acknowledge; either failing brings back #665 jitter | **E-BA2** (8 kHz NMI digi model, screen on, badlines and 8 sprites; `$8F` interval variation ≤ 64 fast clocks and uncorrelated with badline rows; `$0F` control must show stalls); board G-BA1 | Fix the failing piece; no fallback restores parity |
| R19 | The SID edge stage differs from the U64 if the U64 drops sub-µs pulses | **E7-S** on a C64U; music retrigger A/B on the board | `EDGE_STAGE=0` only if the user overrides DL6; D15 |
| R20 | The fetch-affecting set of O3 is incomplete, or O3 costs more than modelled | Ordering fuzz with a VIC fetch-data observer against an in-order reference | Widen to every VIC entry (cost: colour-write loops wait up to about 4 slots) |
| R21 | buslogic is upstream-derived and on the stock path; the I/O-only edit could leak into exact mode | M2 lockstep; A1-A3; port default `'0'` keeps today's logic | `BA_LOW_IO=0` / `FAST_IO=false` |
| R22 | CIA reads complete in BA-low cycles, which no real C64 does; software calibrated on badline stalls runs differently (turbo with badline timing disabled only; approved by Q1; U64-like) | — | Default Badline Timing Enabled; documented in docs/C64_TURBO.md |
| R23 | REU DMA timing in badlines may differ from the U64 with badline timing disabled | **E-BA4** on a C64U/U64E2 (for the record) | None needed: D16 is permanent for native-slot DMA (DL3), so a U64 difference is recorded, not copied |
| R24 | NTSC has never passed on the board, and HP2 at the NTSC clock has never been in a full image | **N0** (stock NTSC build and board test), the first implementation step (Q13) | Fix before NTSC turbo work |
| R25 | VIC patches go through a generated file; drift from upstream; `sid_top.sv` edits in a Multitini port | `port_c64_vic.py --check`; T1-F0; header notes; SID regressions with the fast port idle | `FAST_PORT` = false |
| R26 | Release cost: an extra Vivado run per release for NTSC | — | Accept. No F6 image ships (Q14), so the release matrix stays two images |
| R27 | The full image misses timing outside the island once the island moves placement. The C64 image limiter is the shared 148.5 MHz video clock (c705r2: c64 +7.052 ns, video +0.133 ns, timing_summary.rpt:206-207; the video figure also docs/C64_VICE310_COMPARISON.md:51); recorded C64 images closed at +0.002 to +0.171 ns (CPU1_VIDEO_EFFECTS.md:226-227, 285-286, 481), often only with same-core incremental checkpoints that a new island netlist cannot reuse [V] | M1 full-image builds, judged by the island gate and the existing image gates separately (section 10.2) | The existing repair flow for the failing domain (`finish_video_timing.tcl`, the IIgs reroute repair [V docs/APPLE_IIGS_MMU_AUDIT.md, "FPGA timing closure": the IIgs image closed by new placement plus the 4D000610 post-route repair scripts]). Never the island ladder, which cannot fix a video-domain path |
| R28 | REU image preload leaves stale data: a REU cache line, a stale CPU cache line of the aperture (a partial-line copy writing old neighbouring bytes back), a UA buffer, or a write while a transfer runs | Host tests of the preload order (pause → UA hold → UA held → idle → copy → barrier → invalidate → UA release → resume), its timeouts and error paths (the mocked step-5 timeout included), and the `NORM_NONCACHE` mapping of all 16 aperture sections; `tb_c64_reu_cache` invalidate cases (X-T4); the UA hold bench case (X-T5); Doom C64U mapOK and resident-block compare through `:ramdump`; G-REU0 (Ultimate Audio off) after a 16 MiB preload; X-PRELOAD with 7 voices playing, and its adjacent-byte case | CPU cache, if the mapping cannot be used: invalidate (not clean) the 32-byte-aligned covering range after step 3 and after each `:reudump` read; aligned bounds, because `Xil_DCacheInvalidateRange` cleans unaligned edge lines (xil_cache.c:337-368). The rest: preload only with the C64 held in reset and the REU disabled, then a cold start |
| R29 | Debug facilities of either feature leak into normal use: a debug knob left set, a diagnostic bit or UA debug field read as user status, a debug command acting outside a session, a diagnostic build released | Host tests: every debug command, `:uastat` and `:c64diag` included, refuses with no register access when off; `:debug off` restores release values; nothing persisted. Benches: capture, peek, sticky bits and the UA debug fields inert with the enables at 0 (X-T6). Exporter and packaging refuse diagnostic builds | Fix the leaking path. If a facility cannot be made inert in firmware, also gate it in HDL with `DEBUG_CONTROL` bit 0; `:debug on` may require a confirm token. UART enablement itself stays: Q16 requires it, and the M5 gates run through it |
| R30 | The three turbo-action bindings need storage outside the schema-10 controller record; a companion file adds code and a second journal | Host tests: bindings survive save, load and journal recovery; a downgrade to schema-10 firmware keeps `settings.bin` valid; a bad companion file means "unbound" | Schema 11 only if the user overrides DL2, accepting that a downgrade drops all settings |
| R31 | Software depends on exactly 47x or 63x at the top indices | gist874 and xahmol gates; the T6 demos; titles on the board | `c64u_top` = 1 as the firmware default |
| R32 | Ultimate Audio coupling: its window, acknowledge decode or REU coupling disagrees with this design, or UA registers end up on a fast path and break the detection wait | The cross-review is done: the reconciliation of 2026-10-23 aligned both documents (section 14.3). The checks stay: the T3 case (every CPU access to `$DF00-$DFFF` costs one φ2 at all 16 indices, badline timing on and off, UA on and off; X-T1) and E4/T7 with the UA acknowledge writes (X-T2); X-DET on the board (section 10.3) | UA registers stay φ2 forwards (binding pacing rule, section 1.7) |
| R33 | Programmable holdoff, block cause or static-hold release changes the guard on the stock path | Guard bench (each holdoff value; cause codes; release off = today's behaviour bit for bit); M2 lockstep with the guard tripping | Holdoff field tied to 500 ms and release to 0 in HDL |
| R34 | The combined C64 image (Q17): the island, the Ultimate Audio logic and its HP3 port move placement around the image limiter, the 148.5 MHz video clock (+0.133 ns in c705r2), and `control_clk` (+0.446 ns, c705r2 timing_summary.rpt:202) | M1 is built with UA placed from the first M1 build after UA-M2 passes (release step 2, section 10.3); the island gate and the image gates are judged separately (section 10.2, M1) | The R27 repair flow for a miss outside the island; the island ladder only for an island-gate miss |
| R35 | NMI hold and rearm: a wrong same-clock or mode-switch rule takes an NMI twice or loses one; a lost CIA2 NMI leaves the line low and stops every later CIA2 NMI | T3 NMI cases and the NMI fuzz with mode switches (section 2.9; C64_TURBO_MODELS.md (nmi_rearm.py)) | Fix the rule before M3 exits; steady exact mode never uses it (only the carry after a turbo → exact switch does); badline timing disabled avoids the CIA2 case |

Fallbacks that are diagnostic settings (section 2.13.2: `FAST_VIC_IO=0`, `FAST_SID_IO=0`, `BA_LOW_IO=0`, `FF00_SNOOP=0`, `FAST_IO=false`) serve diagnosis and bring-up. Shipping one would bring back D1, D5 or D5r, which Q1 removed, so it needs a user decision.

Retired:
- BRAM cross-port collisions (old E3), closed by UG473 (section 4.2). Benches still run collision checks.
- The U64 parity gap (v1 R5: D1, D5), closed by Q1. Its implementation risks are R5, R15, R18-R23.
- The F6 NTSC release matrix (v2 R26), closed by Q14.

---

## 10. Verification plan and milestones

No hardware support is claimed until it passes on the board. PAL and NTSC are both release targets (Q2). Each standard has its own go/no-go; PAL does not wait for NTSC.

The plan is staged: exact mode first, then the φ2-only engine, then the fast internal I/O path. The first release (M5) ships with fast VIC/SID and BA-low internal I/O, so D1 and D5 are absent from what ships. The φ2-only engine is an internal stage, never released.

Implementation starts with N0 (Q13). NTSC ships only when it closes at 64x (Q14). The first release also carries the host features (Q15) and Ultimate Audio (Q17), with one ABI step; section 10.3 merges the two plans and lists the cross-feature tests and release gates. Its automated board gates run inside a UART debug session (section 6.5).

### 10.1 Experiments

| ID | What | When |
|---|---|---|
| E1 | OOC sweep to failure of the real island RTL with the duty term, including the FIO read muxes, `io2_q`, the FIFO accept path and the A-bit loads; PAL and NTSC targets | M0 |
| E2 / E2-NTSC | clk_wiz override plan read-back, pair exactness, waveform, probe requirement, DJ; NTSC N1 vs N2 jitter; 1 ps model read-back of N4 and N5 (section 13.3) | M0 |
| N0 | Stock NTSC image of today's tree: record the wizard's plan; board: READY, stable output in display mode 0 (1080p) with a spot check of modes 1-5, SID pitch, TOD 60 Hz against wall time, 1541 load/save, REU in DDR, PAL↔NTSC variant switch persists across reboot. Manual flow with no repository change (the release scripts are PAL-only until M4-FW: build_phos1.py:18-23, 132; package_phos1.py:264; package_firmware_release.py:26 [V]): `create_project.tcl` with `MULTITINI_C64_VIDEO_STANDARD=NTSC`, then `build_and_export_xsa.tcl`, then `make_core_package.py --id c64 --variant "pal\|PAL\|<PAL .bit>" --variant "ntsc\|NTSC\|<NTSC .bit>"` with the tree's ABI minor (make_core_package.py:36-40, 152-162 takes any .bit per variant). All outputs stay under `build/` | First implementation step, before M0 (Q13) |
| E4 | IRQ acknowledge (section 9 R4), forwards and fast writes, including the Ultimate Audio acknowledge writes | M3 (forwards), M4 (fast writes) |
| E5 | REU trigger and DMA start (`reu_test.tas` pattern) | M3 |
| E6 | Forwarded writes vs badline fetches | M3 |
| E-BA1 | `tb_c64_ioonly` (below) | M4 |
| E-BA2 | #665 digi model (below) | M4 |
| E-BA3 | REU programming and triggers inside badlines | M4 |
| E7 | U64 behaviour on a C64U/U64E2, if available: TurboEnable after reset and menu change; `$D030` bits 7-1 and the cc65 C128-detect effect; mirrors; `$00/$01` write timing; colour RAM timing; `$FF00` write timing; the CIA2 NMI case of section 2.9 (T-j, question 26): a held timer-A NMI, the handler's `LDA $DD0D` in the cycle before a refresh-cycle BA fall, timer B firing during the halt; record whether the second NMI is taken and its latency; plus E7-V, E7-S and E-BA4/5 below | M0 onward |
| E-IEC | IEC holdoff shortening: G-IEC at 100, 50 and 20 ms; a code becomes the default only if the whole set also passes at the next shorter code (100 ms needs 50 ms; 50 ms needs 20 ms; 20 ms is a test point only). Telemetry of the "line held" cause on real titles decides the static-hold release (section 1.9) | M5 onward (board, debug session) |
| E-REU-S | Sequential DDR/cache streaming rate for Follow CPU (target ≥ 8 B/µs) | M9 |

**E7-V (VIC, C64U).** Numbering follows C64_TURBO_FAST_VIC.md; V5 is E1 at NTSC and V7 is decision Q-V7.
- V1: landing column of a VIC write: sync on a `$D012` change, n `NOP`s, `STA $D020`; capture the colour-change column against n; compare with T5.
- V2: read-after-write penalty: CIA-timed unrolled `STA $D021 / LDA $D012` vs `STA $D021 / LDA $3000` at index 15.
- V3: VIC RMW cost: unrolled `INC $D020` vs `INC $3000`.
- V4: IRQ after acknowledge at turbo: `STA $D019 / RTI` re-entry counter (expected 0); instructions run between a matching `$D012` write and the IRQ.
- V6: `$D01E` clear window: read `$D01E` twice with a known overlap.
- V8: titles that rely on stock's split landing (RSEL at e_31, XSCROLL at e_7) under turbo: T6 demos on the board.
- V9: sub-cycle sticky pulses (T-i), index 15. (a) With DEN = 0 before line 48, write `$D011` with DEN = 1 and then DEN = 0 back to back on line 48 (`STX $D011 / STY $D011`), then DEN = 1 on line 50: characters (badlines) mean the pulse set `rasterEnable`; idle-state graphics mean it was missed. (b) With the compare not matching and IRST acknowledged, write `$D012` = the current line and then another value back to back; `$D019` bit 0 set means the pulse was caught. 100 runs of each across line phases. This design catches both (T2 directed cases); the C64U result is recorded and settles T-i.

**E7-S (SID, C64U):**
- a. Test-bit pulse: voice 3 sawtooth at `$FFFF`; at index 15 set and clear the test bit back to back; read OSC3 2 µs later; 1000 repeats. About 6-10% "reset seen" means level sampling (pulses lost); about 100% means pulses kept.
- b. The same with the gate bit, read through ENV3, AD = `$00`.
- c. UltiSID at `$DE00`: cost of unrolled `lda $de1b` and `sta $de00` at index 15.
- d. After a write, does a read of a write-only register return `$00` or the written value?

**E-BA4/5 (REU and CIA, C64U/U64E2):**
- E-BA4: REU fetch of 8 KiB to fixed `$D418` and to RAM, screen on, `$8F` vs `$0F`, timed by a CIA timer. Shows whether U64 REU DMA ignores badlines.
- E-BA5: `lda $dc01` and `lda $df00` loops, screen on, `$8F`. Confirms "immune" with a number.

### 10.2 Milestones

**N0: stock NTSC baseline (first step; Q13).** One Vivado build of today's tree as a stock NTSC image, packaged with the `pal` and `ntsc` variants by the manual flow of section 10.1, and the board test listed there. No repository change. No turbo work starts before N0 has a result; a failure becomes the first fix.

**M0: feasibility, no repository change.**
- E1, E2, E2-NTSC; E7 if a C64U/U64E2 is available. N0 has run (Q13).
- Gate:
  - each standard's pair is timed exactly 2:1, or the ladder fix is proven;
  - E1 OOC Fmax of the real island (with the FIO read path) with the duty term is at least 1.1 × target: 69.4 MHz PAL; 72.0 MHz NTSC. Between the target (63.06 / 65.45 MHz) and 1.1 × target, the standard proceeds to M1 with the reduced margin recorded, and the M1 island gate decides. Below the target, that standard applies the RTL steps of its ladder and re-runs E1: PAL the R2 list (F2, `VIC_READ_GAP` = 3, F7, then F4 in both images); NTSC the RTL steps of section 13.4. Placement steps (F1, F3) need the full image and wait for M1.
- AGENTS.md "small tests before a full build" is satisfied here.

**M1: timing skeleton, PAL and NTSC.**
- Real image with the clock plan, island (`cpu_6510`, map, DI mux), true dual-port main RAM, ROM copies, colour port, the VIC and SID read-mux copies feeding `io2_q`, and the FIFO with the fio_port accept path. The proxy is stubbed into registers; FIO write legs end in registers.
- Built with and without the Pblock.
- Gate, in two parts (review finding 12):
  - **Island gate:** worst setup slack ≥ +0.1 ns on `c64_cpu_clk` intra-clock paths (with the 0.2 ns duty uncertainty) and on the c64↔fast paths (X1, X2, X6, X7, X8a, X8b); the TR4 checks pass.
  - **Image gates:** the existing ones, unchanged: routed setup, hold and pulse width ≥ 0 in the exporter (build_and_export_xsa.tcl:600-611), WNS > 0 and zero failing endpoints in packaging (package_cpu1_video_test.py:115-125).
  - A miss outside the island (video clock, `clk_fpga_0`) goes to the existing repair flow (R27), never to the island fallback ladder. Only an island-gate miss moves a standard down its ladder. NTSC go/no-go per section 13.4.

**M2: exact mode only** (`EXTERNAL_CPU=true`, `FAST_IO=true`, fast ports idle).

*Lockstep bench* (`tb_c64_exact_lockstep`, both clocks from one process, PAL and NTSC periods). It compares `EXTERNAL_CPU=false` and `true` at the **consumers**, not raw buses:
- chip selects, `systemAddr`/`ramCE`/`ramWE`/`ramDout`;
- `cpu_bus_*` when valid;
- VIC/CIA/SID inputs while `phi0_cpu` = 1;
- `cpuDi` at R; IRQ/NMI terms; every RAM write;
- `cpuAddr/Do/We/IO` from e_15 to R.

*Scenarios:* badlines, sprites, DMA, `memory_wait`, pause, reset release (the island and proxy on `ext_reset`; the first exact request and the `Res_n` release at end f=0 match stock), RDY holds. Also, with turbo requested at index 15, every forced-exact cause held over the compared window, and the transitions between exact states (a cause starting or ending while another cause or Off keeps exact mode): guard trip, reset window, loader, cartridge, each with `$D031` bit 7 = 1 to prove that forced exact wins (Q9). Transitions between a cause and turbo need the turbo engine and run in M3. Also the index-0 cases (C64_TURBO_U64_ISSUES.md §3(g)2): Manual index 0, Registers live `$00` (preference index 0, or a `$D031` = `$00` write with the menu speed at 64 MHz) and TurboEnable live `$00` (menu speed at 64 MHz), and the transitions among them and Off, must be bit-identical to Off. The frame's return to the dummy at every retiring R is part of every exact cycle here, so the lockstep proves it stock-equivalent; each retired exact request must produce exactly one native access (A8). The exact→turbo seam cases themselves need the turbo engine and run in M3. Assert that port-A EN stays 0, the FIFO stays empty, no fast strobe fires, `fio_cs` stays 0 and `cpuIoOwn` stays 0 in exact mode (T8). NTSC runs include the 6567R8 sprite-0 and sprite-3 bus-steal timing (S2).

*Stimulus:* the 172 VICE CPU pin schedules [V C64_TURBO_VERIFICATION.md] run through an island + proxy wrapper with a stub native side, compared with the stock `cpu_6510` result.

*Runtime:* about 2 M native cycles. It will run several times slower than 3,100 cycles/s [V C64_TURBO_VERIFICATION.md]; restate after measuring.

*Retained suites* pass with updated anchors (S1 in both standards):
- `test_c64_acceleration` cpu/port and `test_c64_mame_port`;
- machine schedule and interrupts;
- REU machine, native and cache;
- the VICE aggregate (5/5, with corruption controls rejected);
- with `FAST_PORT` = false: `port_c64_vic.py --check`, test_c64_vice_vic, test_c64_mame_vic, the VICE bus suite (T1-F0);
- SID with the fast port idle: test_c64_mame_sid.py (updated wrapper), tb_c64_sid_socket, tb_sid_output_schedule, test_audio_path.py.

*Also:* S5 (two-clock generator at NTSC periods, coincident-edge assertion); S8 (NTSC video bridge with `DYNAMIC_DISPLAY=1`, all 6 output modes, multipliers 1-4).

*Board:* gate **G-REU0** (Q8, Q20; the full definition, which also replaces Ultimate Audio's G1, is in section 10.3): Expand disks 1 and 2 (C64_VICE310_COMPARISON.md sequence), with the Ultimate Audio option Off on an image that has it, at stock (Turbo Control Off) and in three states with turbo configured but exact:
- Manual mode, index 0, badline timing enabled;
- U64 Registers mode with the menu speed at index 0 and badline timing enabled: the preference is `$00`, so live is `$00` after the reset window, because Expand never writes `$D031`;
- TurboEnable mode with the menu speed at 64 MHz: live stays `$00` from reset, because Expand never writes `$D030`.

Registers mode with a 64 MHz preference is not a valid case: live becomes `$0F` when the reset window ends (section 1.5), and no forced-exact cause of section 2.11 lasts a whole Expand run. The PAL stock run must match M0.7.0dev5 (font DMA test, disk-2 scroller, ending) [V docs/C64_EXPANSIONS.md:103-108]. Each configured run must be identical to the same image's stock run, strictly, on PAL and NTSC. The NTSC stock run itself is recorded, not judged: the only Expand acceptance is on the PAL image (docs/C64_EXPANSIONS.md:103-108), and whether Expand supports NTSC is not known [O]. The bench side of "forced exact" is the lockstep above, which covers every cause. Also reudiag `reutiming` at stock, expected to match VICE 3.10 (`$D012` = `$33`, DMA at cycle 10, byte 0 at cycle 12), identical in the three configured states, and identical when it runs inside the reset window in Registers mode with a 64 MHz preference (a PRG load resets the C64 and opens the window, which forces exact; C64_TURBO_U64_ISSUES.md §2, #895 row). Expand is not required to pass at turbo.

**M3: φ2-only turbo engine, Manual mode (internal stage; `FAST_VIC_IO = FAST_SID_IO = 0`, `BA_LOW_IO = 0`, `FF00_SNOOP = 0`; local `$00/$01` writes on).**

*`tb_c64_cpu_island`:*
- Dormann from island BRAM at N = 64 and 48 per commit (estimated 8-20 min). N = 63, 47, 16 and 2 nightly (estimated 30-250 min) [I from 208k CPU cycles/s].
- Grant counts per index equal the table, for all 18 rows (16 indices plus the C64U rows).
- Governor unit test (section 2.7) for all 18 rows, with random streams of local, FIO and forward cycles, ACKWAIT, DMA_WAIT, BA halts and pause. Expected: enables in any 64-clock window ≤ N + 1; over any turbo period, enables ≤ grants; `fwd_credit_q` = 0 after every completion and after every change of `turbo_q`, N or the badline mode; at N ≥ 2 no completion waits for a grant; at N = 1 an unrolled `lda $de80` takes exactly 4 φ2 and back-to-back forwards stay one φ2 apart.
- Collision checks on.

*SingleStepTests 6502* (MIT, pinned 2f6980a): on a bare-T65 flat-RAM wrapper, as the IIgs harness does, with a reviewed exception list. Exceptions: JAM as NOP; ANE `$EE`; SHx/TAS models checked against VICE first. They cannot run on the island directly, because `$00/$01` and the bank map overlay memory.

*`tb_c64_machine_island`* (two clocks):
- the turbo ordered access trace equals stock, with the colour nibble masked as tb_c64_machine_schedule.sv:146-148 does;
- exactly one strobe per φ2 I/O access, dummies included;
- E4 (forwards, including the Ultimate Audio acknowledge writes), E5, E6;
- E5 adds an RTL assertion, in place of screen checks, that the REU DMA start offset from the trigger write's retire equals the stock offset, at index 15 and index 1, with badline timing on and off (the #895 check, C64_TURBO_U64_ISSUES.md §2);
- local `$01` writes with the carried port (forwards and DMA decode with the right banking);
- pause and loader;
- mode-switch fuzz (random `$D031`, guard trips, DMA, pause): no lost or duplicated access, architecture equal to stock;
- exact→turbo seam cases: the last exact cycle is a RAM write, a CIA ICR read, a SID write, a `$D019` write, a `$D01E` read, a DMA-held request, and a write in BA-low cycle 3 followed by a turbo forward; each produces exactly one native access, `cpuWe_pre` = 0 at every turbo e_15, and no B0 is captured after the switch (A8);
- assertion: zero turbo local cycles after a guard-tripping `$DD00`/`$DD01` access;
- forced exact with `$D031` bit 7 = 1: no BA-ignoring local cycle, no FIO access and no I/O-only ownership while any cause holds; every transition from turbo into each cause and from each cause back to turbo (guard trip and holdoff end, reset window end, loader start and end, cartridge load and eject) produces no lost or duplicated access;
- NMI hold and rearm (section 2.9 lists each case and its expected result): a one-cycle pulse after a high sample in a BA halt, DMA_WAIT and ACKWAIT, taken once; high-low and high-low-high after a low sample in each stall, taken once, one enable after the stall; the CIA2 acknowledge, BA halt and second CIA2 event, and the underflow on the acknowledge edge, each taken once with the line acknowledged afterwards (badline timing off: taken once, no rearm); a fall on an enable's clock, taken once; two falls in one halt, one NMI; turbo → exact with `force_q` = 1 or a held 0 pending, switched at the next boundary and finished by the carry, no lost or extra NMI; a guard-tripping `$DD00` write whose completion enable is a case-3 enable (a non-CIA2 short pulse): the switch happens at once, the guard assertion and the forced-exact check above hold, and the NMI is taken exactly once, on the exact enables; a forced-exact cause (cartridge attach or loader start) while `force_q` = 1: the switch happens at the next boundary, the forced-exact check above holds, and the NMI is taken exactly once, at that boundary enable or the first carry enable; exact → turbo with B0 low, and during a carry with `force_q` = 1, no lost or extra NMI; the NMI fuzz with mode switches and guard trips, zero missed falls, zero NMIs without a fall and zero turbo enables while a switch to exact is due;
- unmapped addresses: empty I/O1/I/O2 at index 15 returns `lastVicData` through the forward;
- NTSC subset (S3).

*Also:*
- the VICE per-instruction architectural harness (C64_TURBO_VERIFICATION.md §3);
- a delivered-rate bench (the #874 method in simulation; S7: 17,095 CIA ticks per NTSC frame at indices 0, 5, 15) and a `meas_c`-style I/O-class bench against section 8 (φ2 rows; VIC/SID rows at one φ2 here).

**M4: two parallel tracks.**

*M4-IO: fast internal I/O* (all generics at first-release defaults). Benches:
- **T1 `tb_c64_vic_fast_port`** (VIC alone, `FAST_PORT` = true): T1-R1 `fast_rd_data` equals a stock read for all 64 addresses over 10k random states (`$1C` after one pixel edge; `$2F-$3F` = `$FF`); T1-W1 twin VICs give identical register state after stock and fast writes; T1-W2 a fast strobe in each of the 32 phases changes the register at the edge ending that phase; T1-W3 hold windows exact (phases 10-13; 14-29 of the sprite-0 `cycleSpriteA` cycle and 13 of the `cycleCalcSprites` cycle, PAL and the NTSC 6567R8 sequence); T1-C1 clears act at the strobe edge, clear only the bits in the mask, keep bits that a coincident pixel edge sets, and re-arm IMMC/IMBC; T1-I1 `$D019` flags drop one edge after landing, `$D01A` IRQ follows the landing edge; T1-F0 with `FAST_PORT` = false everything passes unchanged.
- **T2 `tb_c64_vic_fast_equivalence`**: random fast writes at random edges through fio_port vs mapped stock writes (landing at e_q of cycle k maps to cycle k−1 if q ≤ 14, else cycle k), checked against two references.
  - (1) The **collapsed reference**, for class-(b) once-per-cycle state only: a stock VIC fed with the mapped writes, several landings to one register mapped to one stock cycle collapsing to the last value.
  - (2) A **per-event scoreboard**, the primary check for the sticky state that the collapse loses (section 2.13.6 (c)) [V VIC:617-623, 899-907, 1584, 1692], evaluated with `rasterY` at each DUT landing edge: `rasterEnable` = OR of every DEN = 1 present on line 48, every landed one included (set on any clock edge), cleared at line 248; IRST set and `rasterIrqDone` re-armed from every landed `rasterCmp`/RST8 value, in landing order, and between landings from each new raster line against the current compare value, as the VIC does on every clock; the `$D019` acknowledge = OR of every landed acknowledge mask in the window (one-clock pulses); an acknowledge and a set event in one window apply in landing order. The scoreboard's `rasterEnable` is forced into the collapsed reference at the stock landing edge of the mapped cycle, so a DEN pulse does not cause false class-(b) mismatches downstream (`badLine`, `baChars`, BA, RC/VC/idle). Feeding every landing to the reference one per edge is not the primary form: it can push the last landing past e_14 and break the class-(b) mapping. Only the bench changes; the RTL, exact mode, stock REU behaviour and G-REU0 are unchanged.
  - Strict compare at every e_15, **of the pre-edge values**, of vicCycle, sprite, rasterX/Y, VC, VCBASE, RC, idle, `rasterEnable` (from the scoreboard), MDMA, MCBase, MCnt, MYE_ff, MActive, MPtr, `fetchBMM`/`fetchECM`, `baLoc`, `ba_reu`, `baCnt`, and `aec` during the following φ2; sprite data (`MPixelStore`) at every s-access; p11/p27 fetch addresses with the two allowed `$D018` data-path differences.
  - Compare IRST and ILP within one cycle against the scoreboard; IMMC/IMBC (`$D019` acknowledges from the scoreboard) and M2M/M2D within one cycle, except after a pixel-level register (MX, `$D010`, `$D011` mode bits, `$D016`, `$D01C`, `$D01D`) or a `$D018` fetch-data difference lands during sprite display: from that landing they are excluded until a `$D01E`/`$D01F` read (M2M/M2D and their re-arm) and a `$D019` acknowledge (IMMC/IMBC) have cleared them in both VICs, or checked against a pixel-accurate reference if one is built (section 2.13.6 (a); question 28 in section 11). A pixel-level landing during sprite display that creates or removes a collision is logged, not failed.
  - One documented exception: two `$D011` landings in one mapped cycle that turn the badline off and on again may restart `baCnt` (T-h); a directed case shows the difference is exactly that.
  - Directed sticky cases [I, C64_TURBO_MODELS.md (vic_t2_sticky.py)], each expected to equal a stock C64 issuing the same writes in separate cycles, and each a mismatch with the scoreboard off (collapsed reference alone): a DEN pulse on line 48 (DEN = 1 then 0, one and three edges apart; `rasterEnable` set and that frame's badlines follow); a temporary `$D012` match (`rasterY`, then another value; IRST set); re-arm by a temporary mismatch after a matched and acknowledged compare (IRST set again); INC `$D012` on a line that the compare does not match (dummy write = `rasterY`, which then matches; IRST set); DEC, LSR and ASL `$D019` with only IRST pending (dummy `$F1` acknowledges, final `$F0`/`$78`/`$E2` does not; IRST cleared, no re-entry). Single-landing controls of each kind match with both references.
  - Negative controls: H1 off must give a `fetchBMM` mismatch; H2 off an MPtr/MActive mismatch; H2's calc-cycle phase off (a `$D015`/`$D001` landing at e_14 of the calc cycle on a matching line) a `baCnt`/`aec`/`MPixelStore` mismatch.
- **`tb_sid_fast_port`** (sid_top, `FAST_PORT` = 1, `DUAL` = 1, one clock):
  - (a) register file vs a stock-timed twin for every offset and chip-select pattern;
  - (b) random read/write/LATCH sequences give the same returned bytes and latches as the twin, including SD3;
  - (c) T-S1c: exhaustive `sid_mode` 0-7 × A11:A0 chip select vs fpga64:815-816;
  - (d) edge stage, with the expected engine samples (gate bit, or the byte, at successive `ce_1m` edges from the edge before the first write) of C64_TURBO_MODELS.md (sid_edge_stage.py). Kept: one window 1→0→1 gives 1 0 1 1 and 0→1→0 gives 0 1 0 0; a test pulse gives `$41 $49 $41 $41`; three writes `$41,$40,$21` give `$40 $41 $21 $21`; a pulse window followed by a window with one write gives 1 0 1 0 0, and followed by one write on the `ce_1m` edge also 1 0 1 0 0; two pulse windows with an empty window between give 1 0 1 0 1 1. Merged: two consecutive windows each 1→0→1 give 1 0 1 1 1 (four 1 MHz writes give 1 0 1 0 1); the same with test pulses gives `$41 $49 $41 $41 $41` (one reset, not two); a pulse window, then `$40` on the `ce_1m` edge and `$41` later, gives 1 0 1 1 1; a pulse window, a one-write window and a 0→1→0 window give 1 0 1 0 0 0. Also: a native write after a pulse window cancels the stage (`$41 $40 $40 $40`), reset (`$41 $00 $00 $00`), a window stretched by `memory_wait` (same samples as unstretched), and `EDGE_STAGE` = 0 losing the pulse (1 1 1 1);
  - (e) audio identity (C64_TURBO_FAST_SID.md 6.1e; phase p = the fast write lands at e_p): a register trace (synthetic 50 Hz player, optionally a VICE dump) at stock timing and through the fast port at random phases gives bit-identical `audio_l/r` at phase 15 for voice control bits 7:4 at 0-30 with single waveforms, else 15-30 (not 31: `norm_dac`, SF11); other voice registers at any phase with single waveforms, else 15-31; chip-0 `$15-$18` at 9-31; chip-1 `$15-$17` and `$18` bits 7:4 at 15-31; chip-1 `$18` bits 3:0 at 15-30 (SF9). Every other `$15-$17` placement is compared against a stock twin whose write the filter first samples at the same edge (the e_29 run, or the stock write one native cycle earlier): bit-identical. No cycle bound is used, because the SVF integrators carry a one-cycle filter difference for hundreds to thousands of native cycles [I C64_TURBO_MODELS.md (svf_persistence.py)]. A `$18` difference stays inside one compare sample. Control bits 7:4 landing at e_31 and combined-waveform voice writes at 0-14 are documented, expected differences: at most one compare sample differs on a voice not routed through the filter; on a filter-routed voice the bench logs the difference and does not fail. A chip-1 volume write landing at e_31 is also expected and changes exactly one compare sample, whatever the routing, because `Mode_Vol` never reaches the integrators.
- **T3 `tb_c64_machine_island`, FIO additions:** scoreboard of VIC and SID writes in program order, no loss or duplication; one clear per `$D01E`/`$D01F` read bus cycle that returned a nonzero byte (none for a zero byte) and one LATCH per SID `$19-$1C` read bus cycle, including `STA $D01E,Y` / `STA $D4FF,X` dummy reads (page cross into the other chip in mode 3) and RMW; RAW pairs (`STA $D020 / LDA $D020`, `STA $D01A / LDA $D019`, `STA $D40E / LDA $D41B`, `STA $D405 / LDA $D41D`, `LDA $D41B / LDA $D41D`, `LDA $D01E / LDA $D01E`); a persistent sprite overlap polled by `LDA $D01E / BEQ`, by `LDA $D01F / BEQ` and by unrolled reads at indices 13, 14 and 15, swept over all 8 loop start phases: every loop exits within one loop period plus one pixel of the overlap's start; forward barrier (a REU stash of `$D000-$D02E` after fast writes holds the new values; `STA $D418 / LDA $DC0D` order); exact↔turbo switches with entries pending; REU fetch into `$D020` followed by a fast write (fast value wins); REU fixed-address DMA to `$D418` during turbo; all 16 VIC mirrors; U64 decode (`$D031` vs `$D071`, `$D07A`/`$D07B`, `$D030` in TurboEnable mode); badline timing on (FIO halts on BA = 0) and off (FIO runs through BA = 0); dual-SID modes 0, 2, 3; pause during queued entries; Ultimate Audio on: `$DF20-$DFFF` accesses are φ2 forwards, I/O-only in BA-low cycles with badline timing disabled, never FIO, and `$DF3F`…`$DFFF` writes take the acknowledge rule; every CPU access to `$DF00-$DFFF` costs one φ2 at all 16 indices, with badline timing on and off and UA on and off (the turbo-side check docs/C64_ULTIMATE_AUDIO_DESIGN.md §1.10 asks for; X-T1, section 10.3); assertion: no turbo VIC/SID access reaches the native slot, no native `cs_sid` in turbo except DMA.
- **T4 rate benches** (`meas_c` and Amiga64 M5 methods in simulation): the Amiga64 M5 sequence (32 unrolled reads, amiga64_mturbo.s:179-184) for `$D012`, `$D419` and `$D41A`: 0.078 ±0.003 φ2 per read at index 15 (0.079 with `c64u_top`) and equal to RAM at ≤ 12; unrolled `$D01E` with and without a collision (0.125 / 0.078 at index 15); section 8 loops within ±1 fast clock; the unrolled `lda $de80` row at every index (2 φ2 at indices 1-3, 1 φ2 at 4-15) and at index 0 with badline timing disabled: 4.00 φ2 per access (3.00 means the completion credit is missing), with `lda $dc01,x / dex / bne` at 9.00 φ2 per iteration, equal to the RAM loop (section 2.7); synthmark-style RAM RMW, zero-page RMW, RAM move and `(zp),Y` loops at 64 slots per φ2 within ±1 fast clock; Doom `miPlay` burst 10.7 ±0.5 µs PAL (10.3 NTSC) at index 15; the irqtest burst at the model value for the loop in `src/irqtest.asm` at its pinned commit, ±0.5 µs.
- **T5 #203 pixel test** (simulation): `colorIndex` trace; index 14 only 1-pixel runs; the C64U row 47 mostly 1-pixel runs with a 2 every about 46 pixels; index 15 per the model.
- **T6 raster splits and an IRQ-driven sprite multiplexer** (project-written) at index 0, 12, 15, badline timing on and off: the change lands on the same line as at 1x, earlier in the line, no extra lines; sprite pixels equal the 1x reference whenever writes land before the cycle 55/56 DMA decisions; differences checked against the T2 mapping.
- **T7 / E4 (fast writes):** `STA`, `LSR`, `ASL $D019`, `STA $D01A`, then `RTI`: no re-entry at indices 15/14/12, including the case where RTI's first cycle would be granted at t+2 after the acknowledge push; handler entry latency while polling `LDA $D012/CMP/BNE`; `FIO_ACK_EDGES` 1-3. The same no-re-entry check runs with the Ultimate Audio clear writes `$DF3F`…`$DFFF`, normal and I/O-only, UA on and off: the IRQ is released before the next enable (X-T2, section 10.3).
- **E-BA1 `tb_c64_ioonly`** (fpga64 with `EXTERNAL_CPU`, `FAST_IO` and a scripted proxy stub; badlines, 0/2/8 sprites; PAL and NTSC VIC): FWD_IO reads and writes to `$DC00-$DC0F`, `$DD00`, `$DD0D`, `$DE00` (SID mode 1), `$DF00-$DF0A` (REU on) and `$DF00` open, registered at every f_r = 0..63 with BA falling and rising at every edge. Checks: completion at R of the load cycle; exactly one CIA `rd`/`wr`, SID `we` or REU `io_valid` per access; B0 = chip data or `lastVicDi` for open bus; no `ramWE`; the VIC fetch trace equals the dummy-frame reference (colour nibble masked in BA-low cycles 1-3, D7). Badline timing enabled: no completion and no chip strobe while BA = 0. Cartridge present: the flag never takes effect.
- **E-BA2 #665 digi model:** CIA2 timer NMI at 8 kHz; handler `STA $D418` + `LDA $DD0D`; screen on with badlines and 8 sprites; index 15. `$8F`: the interval between landed `$D418` writes varies by ≤ 64 fast clocks and does not correlate with badline rows. `$0F`: stalls of up to about 43 µs (the check detects growl). IRQ/CIA1 variant; NTSC run.
- **E-BA3 REU in badlines** (C64_TURBO_U64_ISSUES.md §2, f2a14e51 row): program all registers inside a badline; trigger `$DF01` in BA-low cycles 1, 3, 5 and 20 of a badline; fetch, stash, swap and verify across badlines and sprite DMA with `$D031` = `$00`/`$80`/`$0F`/`$8F`, on the default path (Follow CPU off); `$FF00` variant with `FF00_SNOOP` 1 and 0; bytes and DMA timing compared with the stock run; DMA takeover waits for `ba_reu`; no enable before live hold = 0; prefetch slot count (extends E5).
- **Ordering fuzz (O1-O8):** random mixes of local writes, PORTW, fast VIC/SID writes, fast reads, FWD_IO (both ownership types), FWD_FF00, acknowledge-class accesses, DMA triggers, pause, reset, mode and badline switches, at `FIO_DEPTH` 1, 2 and 4, `FIO_ACK_EDGES` 1-3, and `FIO_RAW_PRECISE` 0 and 1. Per-observer logs vs an in-order reference: VIC register apply order interleaved with port-B-visible RAM writes (and a VIC fetch-data observer for O3); SID write order; CIA and REU access order; IRQ samples at each T65 enable.
- **Assertions:** A1 `cpuIoOwn` ⇒ `phi0_cpu & ~cpuHasBus & ~dma_active`; A2 `cpuIoOwn` ⇒ `ramWE` = 0, `cs_color` = 0, `systemAddr` = `vicAddr` (if `aec`) or `cpuAddr`; A3 `cpuIoOwn` ⇒ eligible address and `cart_present` = 0; A4 every `ioonly` forward completes in its first loaded cycle; A5, stated on pointer indices rather than on the flags: φ2 issue and mode adoption only when `fio_done` = `wp`; an FIO read only when no WR entry and no `dep_ok`-relevant entry lies in [`fio_done`, `wp`); a local write only when no fetch-affecting entry lies there; A6 after DMA, the first FIO access comes after the VIC `we_r` window of every DMA VIC write; no fio_port accept while a stock `myWr_*`/`myWr_phi1`/`myRd` fires; no accept during reset; no native and fast SID access on one edge; A7 no T65 enable of any class between an acknowledge push and `fio_done` reaching that entry; `fio_done` advances by at most 1 per c64 edge, in order, and never passes `wp`; `wp − fio_done ≤ FIO_DEPTH` at every edge; A8 `cpuWe_pre` = 0 at every e_15 in turbo, and each retired exact request produces exactly one native access.
- **SID architectural equivalence:** the same program in exact mode and at index 15 leaves identical SID registers, latches and RAM (no OSC3/ENV3 reads or timing loops).
- **Nightly:** Doom `sidtest` in simulation (the 64 MHz rows equal the 1 MHz rows; about 1 M native cycles); S4 (NTSC: `$D012`/`$D011` bit 7 at turbo through lines 0-262; acknowledge rule; one selective clear per nonzero collision read).
- **Re-runs:** the M2 lockstep (T8) and the M3 suites with the fast generics on.

*M4-FW: U64 registers, firmware, host and debug features, the single ABI step 0.24 (section 6.0), packaging.*
- `tb_c64_turbo_regs`: mode table, exact decode on both snoop sources (native `cpu_bus_valid` and fio_port landing), CPU-only, reset window and latch with the per-standard length (S6), apply counter (no strobe on force-send), the TurboEnable apply rule (live ≠ `$00`; entering TurboEnable sets `$00`), `$D07x`, liveness table 1.11, read values to the island, sticky bits 21-23 (set per mode and register; not set by zero writes; cleared on reset and apply), diagnostic bits 24-26 only while armed and read as 0 when disarmed, the exact-deadline miss condition, `c64u_top` rows, block cause.
- `tb_c64_cpu_turbo_guard` (existing bench, updated; it runs in run_hdl_tests.sh [V scripts/run_hdl_tests.sh:141-143]): its current assertions, with `IEC_HOLDOFF_CYCLES` overridden as today, stay as the "defaults equal today" check of R33; new cases: each holdoff code with shortened per-code parameters; the three cause codes and their priority; static-hold release off = today's behaviour bit for bit; on, with a shortened timeout: release masks both the output and the reload, covers a held IEC line and a held strobe, and any edge re-arms.
- Turbo timing revision regression (`scripts/test_c64_turbo_timing_revision.py`): the grant rows, cutoff, resume edges, `IRQ_SAFE_PHASE`, `FIO_ACK_EDGES`, `FIO_DEPTH`, `VIC_READ_GAP`, `FIO_RAW_PRECISE`, `EDGE_STAGE` and hold windows are hashed per image; a change without a CAPS2 revision bump fails.
- Host-feature and debug benches: `tb_c64_reu_cache` invalidate cases, with Ultimate Audio's read-only cache outputs present (X-T4); `tb_c64_ram_peek`; `tb_audio_capture`, which must see the summed SID plus UA sample (section 6.4; X-T8).
- Mailbox bench; `tb_control_regs` updated (new offsets; pop-on-read for `AUDIO_CAPTURE_DATA`); `generate_registers.py --check`.
- Firmware host tests: settings codec (bytes 243/26/27, byte 26 bits 0-2, reserved bits ignored, byte 27 mode 0, the frozen schema-10 accept predicate, downgrade and upgrade, shared across variants); menu rows editable while Off, the one-step row, arguments 4-7, 9 and 10 routed before the expansion branch and 2, 3, 8 and 11 to `change_c64_expansion()`; the Ultimate Audio and REU apply rules, the restore rule and the UA row states and texts of docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.2-6.4, including a failed save (X-T7); status texts for every cause; the apply counter changes exactly once per real change and never otherwise (C64_TURBO_U64_ISSUES.md §3(e)); turbo actions and the companion bindings file (save, load, journal recovery; a downgrade keeps `settings.bin` valid); the REU preload sequence (including UA hold and UA held), every refusal, timeout and error path; the debug session (every debug command of both features, `:uastat` and `:c64diag` included, refuses with no register access when off; `:debug off` restores release values; nothing persisted; a core switch restores CONFIG bit 19 from the armed flag, not from the session flag); the `:speed` override (never saved by any later save; ended by a menu change, a turbo action or a profile switch, which step from the stored values); ABI gating; caps clamp `0x1FFF` and restore (diagnostic images); `-Wconversion` build.
- Package and build-input host tests with `c64-ntsc` in the matrix (S10); the packaging refuses a diagnostic build and accepts a build with only release fallbacks.

**M5: signoff and board; first release gate.** PAL and NTSC builds signed off separately (section 13.4 go/no-go); NTSC ships only at 64x (Q14). M5's build is the one combined build with Ultimate Audio (release step 4, section 10.3). Every board gate run, by-ear and menu checks included, follows the procedure of section 6.5: `:debug on`; `:c64diag on` when it reads diagnostic fields; `:turbostat`, plus `:uastat` for Ultimate Audio or REU gates; then `:c64diag off` and `:debug off`. Turbo gates run with Ultimate Audio off unless they name it. The Ultimate Audio blocking gates and the cross-feature gates X-DET and X-PRELOAD are listed in section 10.3. Expected values (strict = must pass for release; tolerance "one slot" = 0.016 φ2 per 4-cycle read):

| Gate | C64U/U64 reference [M] | Expected | Strict |
|---|---|---|---|
| gist874 `cpu_rate.tas` (screen blanked) | label per index, except 47.00 at 14 and 63.03 at 15 (C64U, NTSC) | the label at every index: 48.00 at 14 and 64.00 at 15, both standards; 47.00 / 63.00 with `c64u_top` = 1 | yes |
| gist874 `timebase.tas` (NTSC) | 100 frames = 1,709,472 / 1,709,476 CIA ticks (U64E / C64U); 6567R8 prediction 1,709,500 | 1,709,500 within 100 ticks; spread across speeds ≤ 10 | yes |
| CIA/TOD (Amiga64) | 17095.0 ticks per frame; TOD 41-42 tenths per 250 frames (U64E, NTSC) | same on NTSC; 19656 / 50 Hz on PAL | yes |
| xahmol `speed_probe` (RAM loop timed by `$D012`) | 16 lines at index 15; about 21-22 at 14 (PAL) [M] | about 15.8 lines at 15 and 21.0 at 14 on PAL, still classified as 64 and 48 MHz (thresholds below 19 and 19-25); NTSC scaled by 63/65 [I] | yes (PAL) |
| gist839 `meas_c` RAM column | N per φ2 | strict against the model | yes |
| `meas_c` io column (`lda $D012`) | U64E: equal to RAM through 32 MHz; 48 MHz 1765 ticks / 16384 reads | equal to RAM through index 12; at most one slot extra per read at 13-15 | yes |
| `meas_c` cart column (`lda $DE80`) | U64E: 1 φ2 at 6 MHz and above; 2 at 3-5 MHz unrolled; no 2 MHz figure, none at 1 MHz with badline timing disabled | 1 φ2 at indices 4-15; 2 at indices 1-3 (the 2 MHz value from the model only); 4 at index 0 with badline timing disabled (at most one CPU cycle per φ2, section 2.7) | yes |
| Amiga64 M5 `$D012` / `$D41A` / `$D419` | 0.080 / 0.080 / 0.085 φ2 per read at index 15; equal to RAM at 0-12 | 0.078 each (0.079 with `c64u_top`); equal to RAM at 0-12 | yes, within one slot of the C64U value |
| Amiga64 M5 `$DC01` / `$DE00` | 1.040 / 1.045 | about 1.0-1.05 | yes |
| G-BA2: Amiga64 `$DC01`/`$DE00` with the screen on and `$8F` | (immune per #665 [M]) | unchanged from screen off; diagnostic STATUS bit 25 set (armed with `:c64diag on`) | yes |
| VIC on, index 0 (NTSC) | 0.94 | 0.937 | yes |
| `reu_turbo_test` pattern (`reu_test.tas`) at index 15 and index 1 | CPU halts on the `$DF01` write | same; bytes equal to the stock run | yes |
| REU rate | 1 byte/µs | 1 byte per φ2 (1.0227 bytes/µs NTSC) | yes |
| Doom `reubench`/`geobench` (REU preload) | C64U table (georam-vs-reu.md:442-453) | REU and `$DE00` rows | yes |
| G-BA3: Doom `reubench` with the screen on | — | same rows as screen off within the badline share | yes |
| Doom `sidtest` (index 15) | SID burst at turbo | variants 0-3 step 15.6 ±1.5 per tick; control ratios within 8% ([V] tools/sidtest.py:46-48) | yes |
| Doom `irqtest` mode 3 vs mode 0 | — | per-interrupt cost (the tool's "interrupt + SID burst" line minus "interrupt alone") equals the model for the loop in `src/irqtest.asm` within 15%; loop-rate drop 1.3-2.1% if the rate is 1970 IRQ/s and the loop is 16 or 26 cycles [I]; gate ≤ 3% | yes |
| G-BA1: `$8F` digi, screen on (VanessaE MOD player), PAL and NTSC | no growl | no growl by ear; `:audiocap 4 tone 1953 1967` shows no component more than 6 dB above its ±200 Hz neighbourhood, while the `$0F` control run shows one at least 12 dB above [I thresholds] | yes |
| #203 alternating `$D020` writes at index 14 | single pixels, glitch about every 48 [M] | single pixels, no doubles; with `c64u_top` = 1 a double about every 46 | yes (pattern) |
| G-REU0 (Q8, Q20; section 10.3): Expand disks 1 and 2 with Ultimate Audio Off, at stock and in the three configured-but-exact states of M2 (Manual index 0; Registers with preference `$00`; TurboEnable with a 64 MHz preference) | U64 3.15a hangs at 1 MHz (#895) [M] | the PAL stock run matches M0.7.0dev5; each configured run is identical to the same image's stock run | yes (both images; the NTSC stock run itself is recorded) |
| X-DET (section 10.3): the `c64ua` detection PRG after a C64 reset, Ultimate Audio on, Turbo Control Off and Manual at every index 0-15, badline timing enabled and disabled, screen on and off; Doom C64U at index 15 | — | the sampler is found in every case; none is reported with UA off | yes |
| X-PRELOAD (section 10.3): "Load into REU" of a 1 MiB synthetic file with UA on and 7 voices playing, then with UA off. Adjacent-byte case, UA off: a project-written PRG fills REU 0x010000-0x011FFF with `$11`; `:reudump 0x010000 0x2000`; the PRG stashes `$AA` over the same area; `:reuload` of a 4097-byte file at offset 0x010111 (unaligned start, odd length); `:reudump 0x010000 0x2000` | — | after resume the voices are idle, `ua_fault` = 0, `:reudump` equals the file, and a REU fetch of the range returns the file bytes. Adjacent-byte case: the file at 0x010111-0x011111 and `$AA` at all other bytes of the area, including 0x010100-0x010110 and 0x011112-0x01111F, which share a 32-byte line with the file's ends (a flush-only cacheable copy could leave those 31 bytes at `$11`, C64_TURBO_MODELS.md (preload_cache.py)) | yes (PAL) |
| reudiag `reutiming`, stock, the three configured-but-exact states, and inside the reset window (Registers, 64 MHz preference) | VICE 3.10: `$D012` = `$33`, DMA at cycle 10, byte 0 at cycle 12 | same, identical in all | yes |
| G-IEC: the IEC gate set below, guard On, index 15 (index 6 for the #737 loop), PAL and NTSC | the U64 fails #737 at 8 MHz [M] | all pass; with the guard Off, failures recorded for the help text | yes (guard On) |
| PAL/NTSC detect after reset in Manual 64 MHz, Registers and TurboEnable | U64 3.14d misdetected (commit 433c53c8) | PEEK(678) matches the image; then JiffyDOS LOAD/SAVE (user-supplied ROMs) | yes |
| Floating bus at index 15, no REU, no cartridge | broken on the U64 until 3.15 (#205/#304) | Barry McGuigan check and georamtest pass | yes |
| RMW and indirect loops (synthmark "ram rmw", "zp rmw", "ram move", a `(zp),Y` loop), screen blanked | C64U "about 8x" claim, unverified | about 64x at index 15; RMW on I/O (`inc $d020`, `lsr $d019`) per section 8 | yes |
| `c64u_top` A/B | 47 / 63 [M #874] | 47.00 / 63.00 delivered exactly | yes |
| OSC3 read, then a `$D418` write less than 1 µs later, index 15 | #650 | behaves as at 1 MHz; mechanism to be named, because the edge stage acts only on `$x04`/`$x0B`/`$x12`, not on `$D418` or OSC3 reads [O] | yes |
| Doom C64U with REU preload (`assets.reu`) | runs at 64 MHz [M] | mapOK = 1 and every resident block equal to `build/assets.reu` (`:ramdump`, `:reudump`); fps recorded | yes |
| GNW64 / xahmol detection | detects U64 | same | yes |
| Music retrigger A/B (`EDGE_STAGE` 1 vs 0 build) | — | note attacks present with 1 | yes (with 1) |
| T6 raster split and multiplexer demos (board) | — | as in simulation | yes |
| #665/#203 on NTSC | PAL references | checked by eye on both standards | no |

**G-IEC set** (C64_TURBO_U64_ISSUES.md §3(b)): KERNAL LOAD/SAVE/VERIFY; the #737 BASIC print loop sent to a file on drive 8 (there is no printer device); the c64fast ROM pair over the parallel cable; JiffyDOS LOAD/SAVE right after boot (user-supplied ROMs); demos with IRQ loaders (Krill, Bitfire, Spindle) and Ultiverzum (Manual 48 MHz, Bitfire); GEOS boot, disk copy and desktop copy with `$D031` = `$0F` and `$8F`; a turbo program that rewrites the `$DD00` bank bits every frame, with the IEC bits unchanged (turbo must stay on) and toggled (turbo must drop); a drive-started edge during a long local loop; a timing probe started by D64 autostart (record how long it reads 1x). Repeat with the guard Off and record the failures.

Also on the board, both standards:
- stock regression;
- KERNAL LOAD/SAVE with disk auto-slow on and off;
- BASIC benchmark;
- live menu switching, including PAL↔NTSC variant switch and the shared turbo settings;
- dual-SID modes 0, 2 and 3 at turbo;
- capability and status words logged for every gate run;
- the menu: requested and effective speed and the reason for each block cause; the STATUS bit 22 and 23 hints; speed rows editable while Off; the one-step Turbo on/off row;
- turbo actions bound to a gamepad button: one change per press, saved; a downgrade to the previous firmware keeps `settings.bin` valid;
- REU image preload of a 16 MiB image, then G-REU0 again with Ultimate Audio off;
- `:debug off` after every run restores the release values, and a power cycle leaves no debug state.

**M6: retired.** v1's internal-I/O milestone is now M4-IO and part of the first release. The number is not reused, so M7 and M8 keep their v1 meaning.

**M7: cartridge turbo** (C64_TURBO_U64_SPEC.md §D6). M7 stays here, after the first release (Q10). Until then, cartridges force exact mode and the status line shows "(cartridge: 1 MHz)".
- GAME/EXROM in a live crossing and in B1, with map recomputation and a `map_ok` bubble on any change, and forward issue gated on `map_ok`;
- forwarded cartridge windows (FWD_CART, never I/O-only);
- vector-pull forwarding;
- `nmi_ack` stretched to at least two c64 clocks, with the vector read forwarded no earlier than the edge after `freeze_ack`;
- the option;
- the unmapped-address rule for Ultimax-unmapped ranges (section 1.7);
- cartridge suites and board tests.

**M8 (optional):** slot-32 modelling (D6). Its SuperCPU content (`$D0BC`) is removed (Q11).

**M9: Expansion memory speed: Follow CPU** (after the first release; Q9(g); D19; C64_TURBO_U64_ISSUES.md §3(c)).
- Menu row argument 10: "C64U (1 byte per cycle)" (default) or "Follow CPU". Stored in global byte 27 bit 4 (section 6.3); `C64_CPU_CONFIG` bit 18; CAPS2 bit 12. No `$D031` bit is claimed, because the U64 may assign bits 6-4 later (#911).
- Rules [I]:
  - active only while the effective mode is turbo, never in forced exact; the REU is off anyway while a cartridge is loaded (adapter:427);
  - per byte, the fast path applies only when the C64 address auto-increments (`$DF0A` bit 7 = 0) and decodes to RAM (or ROM, for stash) in the island map. Bytes to I/O or colour RAM, and fixed-address transfers, stay on the native φ2 path with the VICE BA rules, so `$D020`/`$D418` streams and digis are unchanged;
  - rate: at most one byte per granted CPU slot at the live index; swap uses two accesses; the cache and DDR may stall it, and that is the only variable;
  - RAM-side bytes use island port A, which is idle because the CPU is halted;
  - with badline timing enabled, fast DMA pauses while BA is low, as the CPU does; with it disabled, it continues, because it never touches the shared bus. This is the one exception to D16 (section 1.12);
  - the halt on the trigger and the first-byte schedule are as in the default. The end state is identical: registers, status, IRQ, final addresses and length, and the post-transfer BA states.
- Structure [I]. The REU byte engine (`c64_address`, `reu_address`, `length` and the per-byte state machine) lives in `multitini_c64_reu.sv` and advances on `dma_ack` from fpga64's native slot [V multitini_c64_reu.sv:56-64; adapter `.dma_ack(reu_dma_ack)`]; the stock cache has one transaction in flight (IDLE/SEND/WAIT) and posts single 8-byte words [V multitini_c64_reu_cache.sv:37]. A faster rate therefore needs changes on the stock REU path:
  - a new block `multitini_c64_reu_fast.sv`, attached in the adapter as an interposer on the REU `dma_*` handshake together with `ext_dma_hold`. With the option on and a qualifying byte, it acknowledges the byte at grant rate through island port A and keeps the native slot idle; otherwise it passes the native handshake through unchanged;
  - a DDR side that runs ahead: either `multitini_c64_reu_cache.sv` gains a second line in flight and stash bursts longer than 8 bytes, or the fast engine gets its own read-ahead requester on the REU DDR path. E-REU-S chooses;
  - `multitini_c64_reu.sv` changes only if its per-byte loop cannot keep up with the interposer.
- So Q8 does not hold by construction. With the option off, the M9 netlist must be functionally identical to stock: G-REU0 and the existing REU suites (machine, native, cache, VICE aggregate) on the M9 netlist prove it.
- Prerequisites: E-REU-S (target ≥ 8 B/µs, the rate the #839 reporter calls enough). Fetch needs at least 2 cache lines in flight; stash needs bursts longer than the 8-byte combine buffers, or it caps near 4 B/µs. The REU-to-port-A path is a new `c64_clk` → `c64_cpu_clk` crossing (related 2:1) and gets its own review per AGENTS.md. Ultimate Audio's fetch bandwidth and coherence rules (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.5-2.6) must still hold.
- Gates: G-REU0 and the REU suites with the option Off, on the M9 netlist; G-REU0 with it On while exact; with it On at index 15, a byte compare against exact references for fetch, stash, swap and verify across badlines and sprite DMA, with `$D031` = `$00`/`$80`/`$0F`/`$8F`; Doom `reubench` throughput. REU-timed effects that stream into RAM change by design, which is why the option is opt-in.

### 10.3 Combined release plan with Ultimate Audio (Q17)

Turbo and Ultimate Audio ship in one release with one ABI step. This section merges the two plans. docs/C64_ULTIMATE_AUDIO_DESIGN.md §9 keeps its own milestones, named UA-M0 to UA-M5, and its own test and gate IDs, which this document writes as "UA T9" or "UA G3". The release steps below are numbered step 0 to step 5, because S1-S10 are the NTSC tests of section 13.6.

**Order**

| Step | Content | Exit condition |
|---|---|---|
| 0 | N0 (section 10.2): stock NTSC build and board test of today's tree. No repository change | result recorded; a failure becomes the first fix |
| 1 | **First implementation commit, before any RTL edit by either feature:** the UA T2 golden trace (UA off), generated on the unmodified RTL. Also UA-M0 (model and option C reference bench) and M0 (E1, E2, E2-NTSC; no repository change) | the M0 gates; the UA-M0 model agrees with the option C bench |
| 2 | The single spec edit (section 6.0). Then both tracks in any order: M1-M4 (M4-IO, M4-FW) and UA-M1 to UA-M3. From the first M1 build after UA-M2 passes, the M1 skeleton includes the UA RTL and HP3, so placement is judged with both (R34) | each track's own bench and host gates |
| 3 | Merged tree: the cross-feature small tests X-T1 to X-T9 below, plus a re-run of both features' suites | all pass |
| 4 | One combined full build per standard; M5 signoff and UA-M4 are this one build. PAL; NTSC per Q14 | the island gate and image gates (section 10.2); TR4; the combined CDC review (section 3.2); UA adds no BRAM; packaging refuses diagnostic builds |
| 5 | Board: the release gates below | every blocking gate passes |
| after | M7 and M9; the UA level row only if UA O3 shows a need | their own gates; G-REU0 and the REU suites re-run |

Step 1 comes first because turbo edits `fpga64_sid_iec.vhd` and the adapter, so a trace generated later would not be a stock reference.

**Cross-feature small tests** (before the full build, as AGENTS.md requires)

| ID | Test | Pass |
|---|---|---|
| X-T1 | T3 Ultimate Audio cases (section 10.2, M4-IO) | every CPU access to `$DF00-$DFFF` is a φ2 forward, never FIO, and costs one φ2 at all 16 indices, with badline timing on and off and UA on and off; I/O-only in BA-low cycles with badline timing disabled; the acknowledge rule on `$DF3F` to `$DFFF` |
| X-T2 | E4/T7 with the UA clear writes | no re-entry; IRQ released before the next enable, normal and I/O-only |
| X-T3 | UA T2 on the merged tree with Turbo Control Off; UA T9 | the golden trace reproduces exactly; UA T9 passes at one access per φ2 and fails at turbo-rate spacing, as specified |
| X-T4 | `tb_c64_reu_cache` with both cache additions (the invalidate input of section 6.4 and Ultimate Audio's read-only outputs) | existing cases pass unchanged with the input idle; invalidate cases pass; UA T5 passes with the input present and idle |
| X-T5 | UA T8 plus the preload hold | the hold empties the buffers and blocks issue; an in-flight read is discarded; `ua_held` rises; release returns UA in its reset state; `reu_fault_latched` untouched |
| X-T6 | Diagnostics arming | with `C64_CPU_CONFIG` bit 19 = 0, turbo STATUS bits 24-26 and every UA debug field read 0 while 7 voices play; arming clears them; `ua_fault` is live in both states |
| X-T7 | Firmware host tests | every row of the UA/REU apply table (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.3), including a failed save; the restore rule (§6.3), including the stored-cartridge case; the UA row states and texts (§6.2); argument routing 0-11; the settings codec (byte 26 bits 0-2, reserved bits ignored, byte 27 mode 0, the frozen schema-10 accept predicate); the preload order and every refusal, timeout and error path; every debug command refuses without register access outside a session |
| X-T8 | Audio | the unchanged `tests/test_audio_path.py` asserts pass; UA T7 and T10f; the capture bench sees the summed sample |
| X-T9 | Build inputs | HP3 ACLK = `clk_out1` and `c64_cpu_clk` = `clk_out2`, C64 image only; non-C64 Tcl and top-level text unchanged; diagnostic settings (including `UA_COHERENCE_PROBE`) refused without the diagnostic variable; the capture list includes both features' runners, models and docs |

**G-REU0, the Expand gate** (Q8, Q20; it replaces this design's earlier G-REU0 text and Ultimate Audio's G1):
- Image: the release image, with the Ultimate Audio option **Off**.
- Program: Expand by Bonzai disks 1 and 2, following the docs/C64_VICE310_COMPARISON.md sequence.
- Runs: Turbo Control Off (stock), then the three configured-but-exact states of section 10.2, M2: Manual index 0; U64 Registers with preference `$00`; TurboEnable with a 64 MHz preference.
- Pass (PAL, strict): the stock run matches M0.7.0dev5 (font DMA test, disk-2 scroller, ending) [V docs/C64_EXPANSIONS.md:103-108], and each configured run is identical to the stock run.
- NTSC, if it ships: the configured runs are strict against the NTSC stock run. The NTSC stock run itself is recorded, not judged.
- Also: three stock runs with `:c64diag on` record the `C64_REU_MISSES` baseline for UA G3; reudiag `reutiming` as in section 10.2, M2; and the whole gate again after a 16 MiB REU image preload.
- With Ultimate Audio **on**, Expand is informational (UA G2 and the Expand part of UA G3). A failure caused only by the missing REU mirrors above `$DF1F` is accepted and documented in docs/C64_EXPANSIONS.md (Q20). Any other failure is a defect.

**Release-blocking gates.** Turbo gates run with Ultimate Audio off unless they name it. Ultimate Audio gates run with Turbo Control Off unless they name a turbo setting. Every run follows the procedure of section 6.5.

| Gate | Content | Images |
|---|---|---|
| G-REU0 | above | PAL strict; NTSC as above |
| M5 | every row marked strict in the M5 table (section 10.2) | PAL; NTSC if it closes at 64x |
| UA G3, readiness part | 7 voices play during Expand: `playing` = `$7F`, the fetch count advanced, `C64_REU_MISSES` within the G-REU0 baseline, `ua_fault` = 0, and in the 48 kHz run no UA underrun after the voices start; run UA G3b if Expand stops the voices | PAL |
| UA G4 | REU board regression with UA off; UA-on results recorded under the G-REU0 rule | PAL |
| UA G5 | project-written PRGs, including the CIA-timed sample period at Turbo Control Off and at index 15 with badline timing disabled (the periods must match), and no UA underrun after the voices start in the 48 kHz seven-voice run | PAL; NTSC if it ships |
| UA G6, G7 | `ua_972_check` shows green; the Pastebin player plays and ends | PAL; NTSC if it ships |
| UA G8 | Doom C64U intro music plays from a preloaded `assets.reu` | PAL |
| X-DET | After a C64 reset, the `c64ua` detection PRG (project-written, following the common 128-read rule) finds the sampler with UA on, at Turbo Control Off and in Manual at every index 0-15, with badline timing enabled and disabled and the screen on and off. It reports no sampler with UA off. Doom C64U detects UA at index 15 | PAL; NTSC if it ships |
| X-PRELOAD | With UA on and 7 voices playing, "Load into REU" of a 1 MiB synthetic file. After resume the voices are idle, `ua_fault` = 0, `:reudump` equals the file, and a REU fetch of the range returns the file bytes. Repeated with UA off. Then the adjacent-byte case of the M5 row (section 10.2): after a `:reudump` and a REU stash over the area, a 4097-byte `:reuload` at offset 0x010111 changes only the file's bytes; every byte outside the file, including the 31 that share a 32-byte line with the file's ends, keeps the stashed value | PAL |
| Debug | Every debug command of both features refuses without a session; `:debug off` restores the release values; a power cycle leaves no debug state | PAL |

**Informational and optional gates**
- UA G2 and the Expand part of UA G3 (the G-REU0 rule above).
- The Freshness MOD player and synth (part of UA G8), with MOD files we have rights to use.
- The NTSC Expand stock run.
- UA G9 (optional): diagnostic build only, never released.

**Failure policy**
- If either feature fails a blocking gate, the whole release waits until it is fixed. Shipping one feature alone needs a new user decision and a second ABI step.
- NTSC follows Q14: if it does not close at 64x, PAL ships alone with both features.

---

## 11. Open questions

Closed by the user (section 0.6): Q1 (fast VIC/SID and BA-low internal I/O), Q2 (NTSC in scope), Q3 (local `$00/$01` writes), Q4 and Q6 (disk auto-slow default On), Q5 (global bytes 26-27), Q9-Q12 (sweep adoptions and rejections, SuperCPU, Ultimate Audio), Q13 (Q-N2: N0 runs first), Q14 (Q-N1: hold NTSC until it closes at 64x), Q15 (host features are in scope), Q16 (debug off by default), Q17 (one release and one ABI step with Ultimate Audio), Q18 (turning Ultimate Audio on turns the REU on), Q19 (the debug facilities of both features off by default, UART only), Q20 (the Expand gate runs with Ultimate Audio off).

Decided by the design lead on 2026-10-23; the user may override each (section 0.6): DL1 (one global UA flag), DL2 (question 18), DL3 (question 25), DL4 (question 19), DL5 (questions 20 and 24), DL6 (question 9). Q17 closes question 21, and the reconciliation with docs/C64_ULTIMATE_AUDIO_DESIGN.md (section 14.3) closes question 22.

Questions. Closed ones keep their number, so references stay valid. A question number is not a decision number: question 18 is not Q18 (section 0.6, IDs).
1. **Clock pair** (E2, E2-NTSC). Does Vivado time each pair exactly 2:1 (PAL 1 ps rounding; NTSC half-ps tie), and does clk_wiz accept the override plan? If not, which ladder step?
2. **Closure** (E1, M1). Does the island, with the FIO read muxes on the address bits, close in the full image with the duty term at each standard? For NTSC the answer decides whether NTSC ships (Q14). If PAL misses 64x after its whole ladder, the remaining steps (F5 at about 55x, or a lower top index) break Q9(a), so the user decides then.
3. **Q-N3.** Label the variant "NTSC" or "NTSC 60 Hz"?
4. **Q-N4.** Keep the old 6567R56A NTSC model out of scope? Recommendation: yes.
5. **Q-N5.** What is the board's PS_CLK oscillator tolerance (it sets the absolute rate; N0 frequency check)?
6. **Q-N6.** Keep 32,727,266 as the NTSC clock constant? Recommendation: keep it (+0.21 ppm from the plan).
7. **IRQ edges** (E4, T7). Where do VIC ERST/IRST and the CIA `irq_n` release land in simulation; minimum safe `RESUME_PHASE`, `IRQ_SAFE_PHASE` and `FIO_ACK_EDGES`?
8. **Q-V7.** Should `$D011`/`$D012` writes join the acknowledge wait (IRQ assertion latency of about 5-6 fast clocks)? Decide after E7-V4; cost about 9 fast clocks per raster-compare write.
9. **Q-S1, `EDGE_STAGE` default. Closed by DL6:** the default is 1 (keeps isolated gate/test-bit pulses, at most one per register per 2 µs, and isolated RMW double writes as at 1 MHz; denser sequences are compressed, section 2.13.7). This follows the sweep's recommendation under Q7 to keep 1 even if E7-S shows that the U64 drops the pulses (C64_TURBO_U64_ISSUES.md §3(g)7). 0 (strict level sampling, probably the U64's behaviour) ships only if the user overrides DL6, for example because U64-targeted software depends on the drop.
10. **O3 scope.** Is the fetch-affecting set (`$D011`, `$D015`, `$D017`, `$D018`, sprite Y) complete? The ordering fuzz with a fetch-data observer decides; the fallback is to wait for every VIC entry.
11. **`FIO_RAW_PRECISE`** after E7-V2: keep the global write barrier or switch to the precise table?
12. **U64 behaviour on real hardware** (E7, E7-V, E7-S, E-BA5): the items in section 10.1.
13. **`$DE00`/`$DF00` SID modes 1 and 4.** If firmware ever offers them, should they join the fast path (needs `sid_mode` in the island map and E7-S part c)?
14. **Doom irqtest reference.** Which burst loop and IRQ rate does `src/irqtest.asm` use? Fetch it at a pinned commit (licence checked, kept under `build/`) and set the T4 and M5 expectations from it; the 1970 IRQ/s figure and the 16-cycle loop are unverified assumptions of C64_TURBO_FAST_SID.md.
15. **Form of the step-2 clock constraint.** Per-standard literal edge shifts in the C64 turbo XDC, or an unmanaged Tcl constraint file? E2 decides; both keep the file out of non-C64 images.
16. **Double `$D011` toggle (T-h).** The design accepts that two `$D011` landings in one mapped cycle can restart the BA→AEC count. If a C64U test or a title shows otherwise, add a hold that limits `$D011` landings to one per mapped cycle.
17. **IEC defaults.** Does E-IEC justify a holdoff shorter than 500 ms, and does "line held" telemetry justify turning the static-hold release on (section 1.9)?
18. **Turbo action bindings storage. Closed by DL2:** settings schema 10 stays and the three bindings live in the companion file (sections 6.2, 6.3), so a downgrade loses only those bindings. Schema 11 would be simpler code, but a downgrade to older firmware would then drop every saved setting.
19. **`$D07A`/`$D07B`/`$D079`** (Q11 interpretation). **Closed by DL4:** they stay as U64 turbo switch registers.
20. **Existing UART commands. Closed by DL5:** Q16 and Q19 cover the facilities this design and the Ultimate Audio design add. `:diag`, `:hid`, `:display`, `:c64rx` and the other existing console commands stay ungated. `:diag` is also the one command allowed while a golden update holds CPU1 (main.c:1746-1754).
21. **ABI order with Ultimate Audio. Closed by Q17:** both features ship in one release with one ABI step, 0.24 (section 6.0).
22. **Coordination items for docs/C64_ULTIMATE_AUDIO_DESIGN.md. Closed by the reconciliation of 2026-10-23 (section 14.3):** turning Ultimate Audio on turns the REU on (Q18); its level row takes argument 11; one offset map (section 6.0), with 0x0FC reserved for its G9 probe; the preload hold with the new UA held acknowledgement (section 6.4); Q19 applied to `:uastat` and its counters through `C64_CPU_CONFIG` bit 19 (section 6.5); the turbo-side checks in E4 and T3 (X-T1, X-T2); expansion caps bits 3 and 4 and byte 26 defined in this release; its §1.10 restates the I/O2 rules of sections 1.7, 2.13.3, 2.13.8 and 5.8.
23. **Colour RAM readback.** Not provided (section 6.4). Add it only if a gate needs it.
24. **Debug session lifetime. Closed by DL5:** a session survives a core switch and ends at reboot, golden update and power-off (section 6.5).
25. **Q-BA1** (C64_TURBO_U64_ISSUES.md §2, f2a14e51 row). **Closed by DL3:** no. D16 is permanent for native-slot REU DMA, which keeps the stock VICE rules in every mode (Q8, Q20); only the opt-in Follow CPU bytes (M9) are exempt. E-BA4 records the U64 behaviour.
26. **NMI after a low sample on the U64** (E7). Does a C64U take a CIA2 NMI that re-asserts during a BA halt right after the ICR read, and with what latency? The design follows stock either way (section 2.9); the answer only records a difference (T-j).
27. **Sub-cycle sticky pulses (T-i).** This VIC lets a DEN or compare value that lasts less than one cycle set `rasterEnable` or IRST (section 2.13.6 (c)). [O] E7-V9 shows whether a C64U does the same. If it does not and a title depends on that, the option is a turbo-only once-per-cycle sample of DEN and the compare in the VIC patch [I]; exact mode stays unchanged either way.
28. **Collision latches in T2.** A pixel-level landing during sprite display can create or remove an M2M/M2D collision (section 2.13.6 (a)), so T2 leaves IMMC/IMBC/M2M/M2D out of its strict compare at those times. [O] Is a pixel-accurate collision reference worth building for T2, or do the T3 collision polls and the T6 demos cover it? Decide when T2 is written (M4-IO).
29. **U64 `meas_c` cart column at 2 MHz and at index 0 with badline timing disabled.** The U64E data covers only 3-5 MHz (2 φ2) and 6 MHz and above (1 φ2) [M, C64_TURBO_U64_SPEC.md §D1]. Multitini's 2 φ2 at index 1 (2 MHz) and 4 φ2 at index 0 with badline timing disabled come from the model [I, C64_TURBO_MODELS.md (governor_credit.py)]. E7 on a C64U/U64E2 can measure both with a CIA-timed unrolled `lda $de80`. A U64 difference is recorded, not copied (section 2.7).

---

## 12. File-level change list (not done now)

### Build, packaging and constraints

| File | Change |
|---|---|
| `scripts/create_project.tcl` (:46-64, :478-494, :607-617) | C64 branch: per-standard pinned MMCM plan (PAL {9, 56.75, 30, 15}; NTSC {5, 24, 22, 11}; overrides, read back), `NUM_OUT_CLKS 2`, clk_wiz `clk_out2` = 2×; new BD port `c64_cpu_clk` → `clk_out2` for C64, `clk_out1` otherwise; new env `MULTITINI_C64_ISLAND_CYCLE` (1 default; 2 = F6, accepted only with `MULTITINI_C64_DIAGNOSTIC_BUILD=1`; C64 only, validated) emitted as generic `C64_ISLAND_CYCLE`; new env `MULTITINI_C64_DIAGNOSTIC_BUILD` (0 default), which allows the diagnostic settings of section 2.13.2 and sets CAPS2 bit 14; release fallbacks need no such variable, each has its own named, validated env value recorded in the build inputs. HP2 and `reset_c64` unchanged (:555-564). Keep `c64_clock_mhz` 32.727266 as the request label. Add `multitini_c64_turbo_timing.xdc` to the hard-coded implementation-only list (`USED_IN_SYNTHESIS false`, `PROCESSING_ORDER LATE`, :161-164) |
| `scripts/build_and_export_xsa.tcl` | TR4 checks keyed on `C64_NTSC_MODE` and `C64_ISLAND_CYCLE`; `require_c64_local_ram` gains no-cascade and port-A-inversion-by-net checks; the duty-uncertainty probe; print `C64_CLOCK_PAIR_PASS standard=...` and the island slack (intra-`c64_cpu_clk` and c64↔fast) separately from the image-wide slack; the release-settings check (release defaults or listed fallbacks; the Ultimate Audio build parameters at their release defaults; diagnostic settings, `UA_COHERENCE_PROBE` = 1 included, only with the diagnostic variable), `C64_DIAGNOSTIC_BUILD=0\|1` and the fallbacks in use. Under the R8 fallback, its clock-name patterns (:219-224) follow the renamed clocks unless the fallback keeps today's names |
| `scripts/check_c64_local_ram_synthesis.tcl` | True dual-port, split, no cascade, write modes |
| `scripts/capture_vectrex_build_inputs.py` (:157) | Record `c64_island_cycle`, `c64_diagnostic_build`, the release fallbacks in use and the Ultimate Audio build parameters next to `c64_video_standard`; the capture list includes both features' runners, models and docs (X-T9) |
| `scripts/package_cpu1_video_test.py` (:213-248) | `verify_hardware(..., ntsc, island_cycle)` checks the generic and the capture field; refuses a diagnostic build; accepts listed release fallbacks |
| `scripts/build_phos1.py` (:18-19, :132) | Add `c64-ntsc` to MATRIX; set the video standard per entry instead of the hard-coded PAL; pass the capture flag and `ntsc=True` |
| `scripts/package_phos1.py` (:83-85, :264, :313) | Verify and package the NTSC variant (only when it closed at 64x, Q14); same ABI for both variants; release text; refuse a diagnostic build |
| `scripts/package_firmware_release.py:26` | Map `ntsc` to `c64-ntsc` |
| New `hdl/board/multitini_c64_turbo_timing.xdc` (in the `[c64]` section of `hdl/hdl_sources.txt`; implementation-only) | Duty-cycle uncertainty on `c64_cpu_clk` rise→fall and fall→rise, with exact clock names; optional Pblock; the c64 clock redefinition only if E2 / E2-NTSC needs it, as per-standard literals or an unmanaged Tcl constraint file (E2 decides, question 15). `multitini_timing.xdc` stays unchanged: it is shared by every image and its machine filters already match (section 3.4 TR1) |

### HDL

| File | Change |
|---|---|
| `hdl/board/multitini_one_top.sv` | `c64_cpu_clk` port and wiring; C64 CPU config/status mailboxes; generic `C64_ISLAND_CYCLE`; caps word (mask `0x1FFF` when `C64_ISLAND_CYCLE` = 2); CAPS2 (timing revision, diagnostic-build bit); the X10/X11 mailboxes; `DEBUG_CONTROL` and the audio capture instance (C64 images); the `C64_EXPANSION_CAPS` parameter `32'h8000001F` (bit 4 here, bit 3 Ultimate Audio) |
| `hdl/common/multitini_control_regs.sv` | 0x0D8-0x0E4 and 0x100-0x110 decode, write list, reset values, clamp; pop-on-read for `AUDIO_CAPTURE_DATA` (the `MEDIA_DATA` pattern); `C64_REU_HOST` bit 14 (UA held) read path. Ultimate Audio adds 0x0F0-0x0F8 and the `C64_EXPANSION_CONFIG` mask `0x1F` with its bit 4 clamp (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1); 0x0FC is decoded only in a UA probe build (section 6.0) |
| `hdl/board/multitini_audio_test.sv` | Export the control-domain 48 kHz tick and the `machine_audio_to_control` sample to the capture block; its outputs do not change |
| `hdl/board/multitini_audio_capture.sv` (new) | 512-entry FIFO (one RAMB18) on `control_clk`; push at the tick while debug enable and run are 1; overflow bit; pop on read; nothing happens with debug enable = 0 |
| `hdl/core/c64/multitini_c64_adapter.sv` | Instantiate island, proxy, fio_port, turbo_regs, colour RAM and ROM copies; `EXTERNAL_CPU=true`, `FAST_IO=true`; `main_ram_i` port A; tie scheduler turbo inputs; guard enable from config; `cart_present` and `ext_cpu_ioonly` wiring; `CORE_CLOCK_HZ` and `ISLAND_CYCLE` to turbo_regs and the island; guard holdoff, cause and release wiring; RAM peek and REU invalidate (X10, X11); `C64_REU_HOST` bit 13 into Ultimate Audio's inactive condition and the registered UA held echo back through X11; `C64_CPU_CONFIG` bit 19 (C64 diagnostics armed) to turbo_regs and to the Ultimate Audio block |
| `hdl/core/c64/ports/fpga64_sid_iec.vhd` | Generics `EXTERNAL_CPU` and `FAST_IO` (default false); generate around the `cpu_6510` instance (984-1008); ext ports (2.3), including `ext_reset` driven from the internal `reset` (433-440); colour RAM export; turbo selection (1065-1092) tied off under `true`; `FAST_PORT => FAST_IO` on the VIC and SID instances, `fast_phase` = `sysCycle` position; SID fast selects (2.13.7) placed after the `sid : sid_top` instance; `cpuIoOwn` (set at CPU0, cleared at R/VIC3/reset); `cpu_bus_valid` includes `cpuIoOwn` (1039); buslogic port map; VHDL asserts A1-A3. Text anchors near 530 and 1039 change (R10). Ultimate Audio adds one observation output, the phase-15 DMA-slot strobe for `C64_REU_MISSES`, with no logic change (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1, §11) |
| `scripts/port_c64_vic.py` → regenerated `hdl/core/c64/ports/video_vicII_656x.vhd` | New `replace_once` patches, all under `FAST_PORT` (default false): P1 generic; P2 ports (`fast_phase`, `fast_rd_addr`, `fast_rd_data`, `fast_wr`, `fast_wa`, `fast_wd`, `fast_clr_m2m`, `fast_clr_m2d`, `fast_hold_d011`, `fast_hold_spr`, `fast_native_busy`); P3 fast write block after `myWr_c` (all bits of every writable register; `$1C` → MCDelay; `$19` reset pulses); P4/P5 selective `$1E`/`$1F` clears appended after the stock collision code (mask from `fast_wd`, applied to the next-state value, hit flags re-armed); P6 generate with the read-mux copy (`$1C` → MCDelay), holds (H1; H2 including phase 13 of `cycleCalcSprites`) and busy, plus a constant-driving `not FAST_PORT` generate; P7 header text. The VIC file is generated ("do not edit directly", VIC:1 [V]); edit only the script |
| `hdl/core/c64/ports/sid_top.sv` (Multitini port of MiSTer 62fc697) | Parameters `FAST_PORT` (0) and `EDGE_STAGE` (1); ports for landing strobes (cs per chip, we, latch, latch source, addr[4:0], data) and the read mux (`fast_rd_cs`, `fast_rd_addr[4:0]`, `fast_rd_data`); register-block source mux (stock first); latch rule; per-chip control-register stages; native control writes cancel stages; assertion `!(cs[i] && fio_cs[i])`; header note. Nothing generated with `FAST_PORT` = 0 |
| `hdl/core/c64/multitini_c64_buslogic.vhd` | Port `cpuIoOwn : in std_logic := '0'` (sensitivity list :197); VIC branch (298-314): with `cpuIoOwn`, VIC RAM select to new `vicRamLoc`, chip selects from `cpuAddr` (CIA1, CIA2, IOE, IOF; VIC/SID only when forwarded by a fallback; never colour); `cs_ram` includes `vicRamLoc` (317) |
| `hdl/core/c64/multitini_c64_local_ram.sv` | True dual-port, 2 × 32 KiB, port A island (negedge, NO_CHANGE), port B machine + loader (WRITE_FIRST); a read-only peek input below the loader; a parameter keeps the single-clock form for `tb_c64_local_ram` |
| `hdl/core/c64/multitini_c64_ram_peek.sv` (new) | c64 side of the peek mailbox: acts on a request-counter change only when debug enable = 1, `core_paused` = 1 and the loader is idle; otherwise refuses with no RAM access |
| `hdl/core/c64/multitini_c64_cpu_turbo_guard.sv` | Holdoff selected by `C64_CPU_CONFIG[17:16]` among per-code cycle parameters (`IEC_HOLDOFF_CYCLES` stays as code 0 with today's default; three new parameters derived from `CORE_CLOCK_HZ`), so benches can still shorten them; block-cause output with the priority of section 1.9; static-hold release (enable input, default 0) with its own counter of at least 26 bits, masking the held-level term in both the output and the reload. With the defaults it behaves as today |
| `hdl/core/c64/multitini_c64_reu_cache.sv` | `invalidate` input: clears the read-line valid bits when not busy. Idle unless firmware preloads an image (Q8, Q20). Ultimate Audio adds read-only outputs to the same file (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.5); X-T4 checks both additions together |
| `hdl/core/c64/multitini_c64_reu.sv` | No turbo change in the first release. Ultimate Audio adds one observation output ("in a DDR request or wait state") for `C64_REU_MISSES`, with no logic change (docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.1, §11); the existing REU suites, UA T2 (X-T3) and G-REU0 check it (section 1.8). Turbo edits it only in M9, and only if needed (M9 row) |
| `hdl/core/c64/multitini_c64_cpu_island.sv` (new) | `cpu_6510`, map and classes, grants (18 rows: 16 indices plus the C64U rows 47 and 63; rows for N ≤ 32 under F6), DI mux with `io2_q`, PORTW, FSM, `turbo_q`, late-acknowledge rule (including the Ultimate Audio `$DFxF` writes), NMI hold and rearm (`nmi_seen_q` with no reset, `fell_q`, `force_q`; turbo → exact at the normal boundary for every cause, with the carry on the first exact enables; section 2.9), request registers with `ioonly`; FIO: FIFO with the `fa` bit, `en_q`, `busy_q`, barrier flags registered from the post-push state (`all_done_q`, `wr_pend_q`, `clr_m2m_pend_q`, `clr_m2d_pend_q`, `latch_pend_q`, `full_q`, `fetch_done_q`, `ack_wait_q` with `ack_end`), `dep_ok`, final read mux with the U64 override; port-A EN from registered terms only; FWD_FF00; generics `FAST_VIC_IO`, `FAST_SID_IO`, `FF00_SNOOP`, `VIC_READ_GAP`, `FIO_RAW_PRECISE`, `FIO_DEPTH`, `ISLAND_CYCLE`; prefix `isl_x_`; completion credit `fwd_credit_q` and the `cmpl_ok` term: a grant while a forward, ACKWAIT or DMA_WAIT is outstanding sets it; the end f=4 completion, the ACKWAIT exit and the DMA_WAIT exit each need the credit or a coincident grant; it clears when used and when `turbo_q`, N or the badline mode changes (sections 2.7, 2.13.2) |
| `hdl/core/c64/multitini_c64_fio_port.sv` (new) | Pop stage: accept, H1/H2 holds, native-busy barrier, registered VIC/SID strobes (VIC clears carry the mask), U64 snoop, `fio_rp`, `fio_done` from an in-order done pipe with the `FIO_ACK_EDGES` delay; reset from `ext_reset`; assertions A6, A7 |
| `hdl/core/c64/multitini_c64_island_rom.sv` (new) | ROM copies |
| `hdl/core/c64/multitini_c64_color_ram.sv` (new) | True dual-port colour RAM |
| `hdl/core/c64/multitini_c64_cpu_proxy.sv` (new) | Frame register (with `ioonly & ~cart_present`; returns to the dummy at every retiring R in exact mode and at every R in turbo), B0/B1, sequence numbers; generic `BA_LOW_IO`; reset from `ext_reset`; turbo resume at end f=4, or end f=5 under F6 (section 13.5); exact-deadline miss output (STATUS bit 24); assertion A8 |
| `hdl/core/c64/multitini_c64_turbo_regs.sv` (new) | U64 registers, live/preference, reset window (2 × `CORE_CLOCK_HZ` / 32), apply counter and the TurboEnable apply rule, policy; second write snoop from fio_port; read values and live flags to the island; `c64u_top` selection; block cause; STATUS bits 21-23 (sticky hints) and 24-26 (armed diagnostics, masked to 0 while CONFIG bit 19 is 0; the exact-deadline miss input from the proxy) |
| `hdl/core/c64/multitini_c64_cpu_config.sv` (new) | Mailbox copy |
| M9 only | New `hdl/core/c64/multitini_c64_reu_fast.sv` (Follow CPU byte engine); edits on the stock REU path: `multitini_c64_adapter.sv` (interposer on the REU `dma_*` handshake and `ext_dma_hold`), `multitini_c64_reu_cache.sv` (second line in flight and longer stash bursts) or a separate read-ahead requester, and `multitini_c64_reu.sv` only if its per-byte loop must run faster (section 10.2, M9) |
| Unchanged | `T65.vhd` and `cpu_6510.vhd` (unless F4); `sid_voice.sv`, `sid_envelope.sv` (third_party, pinned), `sid_filter.sv`, `sid_tables.sv`, `sid_dac.sv`; `multitini_c64_cpu_scheduler.vhd`; video, video_sync, TOD, 1541, HP2 paths; `third_party/appletini/hdl/cdc_handshake_reg.sv`; `multitini_c64_defs_pkg.sv` and `firmware/include/multitini_c64_interface.h` (NTSC constant kept, Q-N6) |
| `hdl/hdl_sources.txt`, `licenses/files.csv` | New files; `multitini_c64_turbo_timing.xdc` goes in the `[c64]` section. `sid_top.sv` stays GPL-2.0-or-later. No UltiSID code is used |

### Tests

| File | Change |
|---|---|
| `hdl/sim/` (new benches) | tb_c64_cpu_island, tb_c64_machine_island, tb_c64_exact_lockstep, tb_c64_turbo_regs, tb_c64_cpu_config, tb_c64_vic_fast_port, tb_c64_vic_fast_equivalence, tb_sid_fast_port, tb_c64_ioonly, tb_c64_ram_peek, tb_audio_capture; bare-T65 SST wrapper; single-process two-clock generator (PAL and NTSC periods) with the coincident-edge assertions. Section 14.4 additions: tb_c64_cpu_island carries the governor unit test (at most N + 1 enables in any 64-clock window; enables never exceed grants; credit 0 after each completion and mode change; 4 φ2 per unrolled `lda $de80` at N = 1); tb_c64_machine_island carries the section 2.9 NMI cases and the NMI fuzz with mode switches; tb_c64_vic_fast_equivalence has two references (collapsed for class (b), a per-event sticky scoreboard for `rasterEnable`, IRST/`rasterIrqDone` and the OR of the `$D019` acknowledge masks), the directed sticky cases, a scoreboard-off negative control, and IMMC/IMBC/M2M/M2D out of the strict compare while pixel-level registers land during sprite display |
| `hdl/sim/` (updated) | tb_c64_machine_schedule, tb_c64_machine_interrupts, tb_c64_reu_machine (`EXTERNAL_CPU`, `FAST_IO`); tb_c64_reu_cache (invalidate); tb_c64_cpu_turbo_guard (per-code holdoff parameters, cause codes, static release; existing assertions kept); tb_audio_test (exports); tb_c64_cpu_scheduler (tie-offs); tb_control_regs; tb_c64_ntsc_video_bridge also with `DYNAMIC_DISPLAY=1` (S8) |
| `scripts/` | Runner anchors in test_c64_machine_schedule.py, test_c64_machine_interrupts.py, test_c64_reu_machine.py, test_c64_vice_bus.py, test_c64_vice_cpu.py; `test_c64_mame_sid.py` wrapper (:42-76) declares the new identifiers (`sid_fio_*`, `ext_fio_*`, a `FAST_IO` constant = false) so the copied text elaborates with the fast port idle; new `scripts/test_c64_turbo.py` (per-commit and nightly sets); new `scripts/test_c64_turbo_timing_revision.py`; `run_hdl_tests.sh` |
| `tests/` | test_build_inputs.py (:198-232 literals; remove the Smart strings; add island, fast-I/O generics, per-standard MMCM plan and island-cycle strings); test_interface_spec.py; host tests test_core_frontend.c (:86-91, :117), test_supervisor_settings.c, test_sd_storage.c, test_video_settings_runtime.c, test_controls.c and test_menu.c (turbo actions), new test_turbo_controls_store.c, new test_debug_service.c, new test_c64_reu_preload.c; the cross-feature host cases X-T7 (section 10.3); package tests (`test_core_package_tool`, `test_core_artifacts`) with `c64-ntsc`. `tests/test_audio_path.py` anchors (:27-33) are unaffected. Section 14.4 additions: test_c64_reu_preload.c covers all 16 REU aperture sections `NORM_NONCACHE` before CPU1 release, no cache range call on the aperture, the barrier order, and the mocked step-5 timeout with exactly one retry, alone and after a step-4 read failure; test_debug_service.c checks that `:reudump` makes no cache call (section 6.4 (a)) |
| `tools/menu_preview.c` | New menu rows in the full Options order of section 6.2, including the one-step row and the visibility texts; capped-row preview |

### Interface, firmware and docs

| File | Change |
|---|---|
| `spec/interface_v0.json` | One ABI step, 0.23 → 0.24, for turbo, Ultimate Audio, REU image preload and the debug registers (section 6.0); registers 0x0D8-0x0E4 and 0x100-0x110 (Ultimate Audio: 0x0F0-0x0F8; 0x0FC reserved for its G9 probe, diagnostic builds only); `C64_EXPANSION_CAPS` bits 3 and 4; `C64_REU_HOST` bit 14; the constants `MT_C64_TURBO_MIN_ABI_MINOR`, `MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR`, `MT_C64_REU_HOST_MIN_ABI_MINOR` and `MT_DEBUG_MIN_ABI_MINOR`, all 24; regenerate `firmware/include/multitini_regs.h` and `hdl/generated/multitini_regs_pkg.sv`; `scripts/package_core_gs.py` ABI |
| Firmware | `firmware/platform/xilinx/main.c` (240-247, 2311-2314, 2481-2482, 3551-3558, 4505-4608; `:reuload`, `:debug` and the debug-only commands in `handle_extended_command` at :1730; the turbo actions next to the screenshot actions at :3256-3257; routing of arguments 4-7, 9 and 10 to the turbo handler and 2, 3, 8 and 11 to `change_c64_expansion()`; the Ultimate Audio/REU apply and restore rules of docs/C64_ULTIMATE_AUDIO_DESIGN.md §6.3-6.4 in `change_c64_expansion()` and `configure_c64_expansion()`; the gating rule and constants of section 6.0); `firmware/common/core_frontend.c` (rows, visibility texts, help, caps clamp; the `.reu` entry in the C64 profile's `browser_types`, core_frontend.c:22); `firmware/include/multitini_core_frontend.h`; `firmware/include/multitini_controls.h` and `firmware/common/controls.c` (three C64 supervisor actions outside `MT_CONTROL_COUNT`); `firmware/common/menu.c` (Controls rows); `firmware/platform/xilinx/supervisor_settings.{h,c}` (global bytes 26/27; reserved bits ignored); new `firmware/platform/xilinx/turbo_controls_store.{c,h}` (companion bindings file); new `firmware/platform/xilinx/debug_service.{c,h}` (session flag, armed flag, `:turbostat`, the `:speed` override overlay, the `:turbo` and `:iec` knobs, `:c64diag`, `:uastat`, peek and dumps, audio capture with WAV and Goertzel); `c64_content_service.c` (caps; REU image preload with UA hold, the UA held wait, timeouts, error paths and the `:reuload` offset); `multitini_version.h`. Section 14.4 additions: `main.c` maps the 16 REU aperture sections 0x25000000-0x25FFFFFF with `Xil_SetTlbAttributes(..., NORM_NONCACHE)` at start-up, next to the shell mapping (:5059) and before `mt_video_worker_start()` (:5073), and adds the 'reload the C64 core' state after a second step-5 timeout (machine stays paused; resume, content loads and preloads refused until the core is reloaded); `c64_content_service.c` copies through that mapping with no per-block flush, ends step 4 (also on failure) with `dsb`, a PL310 Cache Sync register write and `dsb`, retries step 5 once and adds the step-5 error path; the aperture base and size constants go in `c64_content_service.h`; `debug_service.c` `:reudump` reads through the same mapping with no invalidate |
| `docs/C64_RAM_CPU.md` | Replace :66-68 (Q3: local `$00/$01` writes in turbo; native in exact mode) and :68-69 (Q1 rule text, section 0.6); replace :97 "No new clock or clock-domain crossing is added" with the section 3 review; retire the Smart 2x text (:59-63); island rules; NTSC section |
| New `docs/C64_TURBO.md` | Section 1, D1-D19 (D10 removed), SD1-SD4, the cc65 C128-detect note, the fast-I/O landing rule and micro-timing (2.13.12), BA-low behaviour and R22, the IEC guard (stricter than the U64's slowdown; Off is risky), the menu visibility texts and turbo actions, NTSC plan and its 64x release rule, gates, F6 as a diagnostic build, µs scaling, and the statement that turbo is not cycle-exact to the U64 (timing revision); the speed contract (no code runs faster than the label at any index; index 0 with badline timing disabled runs at most one CPU cycle per φ2); T-i and T-j in the micro-timing list |
| `MULTITINI_ONE_PLAN.md:6` | Image set gains C64 NTSC (Q2), shipped only at 64x (Q14) |
| `docs/C64_EXPANSIONS.md` | :20 ("Both chips retain the native SID clock") stays true; add turbo SID access timing, the edge stage, φ2 timing for the `$DE00`/`$DF00` modes, REU programming under turbo and inside badlines, REU image preload and the cache invalidate, Expansion memory speed (M9), and a cross-reference to the Ultimate Audio window; the Expand result with Ultimate Audio on, if it fails only because the REU mirrors above `$DF1F` are gone (Q20); the edge-stage contract: isolated gate/test pulses are kept (at most one per voice control register per 2 µs), denser sequences are compressed, exact mode is unchanged |
| Other docs | `spec/core_interface_v0.md` section for the new ABI minor (section 6.0); `docs/ARCHITECTURE.md` (clock table :121, two outputs per standard); `docs/VIDEO_SETTINGS.md:31-37` (PAL and NTSC variants, NTSC Auto sizes); `docs/VIVADO_HANDOFF.md` (:125-126, :176-184: plans, gates, build steps); `docs/C64_SD_STORAGE.md` (UART commands `:reuload`, `:debug` and the debug-only commands; the capture folder); `docs/MULTICORE_RUNTIME.md`; `docs/SETTINGS_HARDWARE_TEST.md`; `docs/CONTROLLERS_AND_SCREENSHOTS.md` (the three turbo actions next to the screenshot actions, :70; `turbo_controls.bin`; the downgrade note); `docs/HARDWARE_RESULTS.md` only when results exist. `docs/CPU1_VIDEO_EFFECTS.md` historical text stays unchanged |

---

## 13. NTSC image (Q2)

### 13.1 Verified facts (how PAL/NTSC works today)

- **Build selection** [V]: `MULTITINI_C64_VIDEO_STANDARD` selects PAL (31.527954 MHz) or NTSC (32.727266 MHz) (create_project.tcl:46-64); the generic `C64_NTSC_MODE` is written at :607-609; clk_wiz gets only a requested frequency (:480-494), so no NTSC plan is on record. `build_and_export_xsa.tcl:171-172` passes `--c64-video-standard NTSC` to input capture; `package_cpu1_video_test.py:220-221, 248` refuses a mismatched image.
- **Machine RTL** [V]: the adapter has `NTSC_MODE` and `CORE_CLOCK_HZ` (adapter.sv:8, 118; defs_pkg.sv:7-8). `ntscMode` drives only the VIC model (6567R8: 65 cycles × 263 lines; sprite-0 BA at cycle 56) and TOD (fpga64:80, 720-722, 956-979). 32 native phases in both standards. SID `ce_1m` once per native cycle; audio resampler and IEC guard use `CORE_CLOCK_HZ`; the 1541 runs at 1 MHz in both standards (1541.sv:213-229); HP2 is clocked from `c64_clk`, with an NTSC DDR bench (tb_c64_ddr.sv:549).
- **Video** [V]: NTSC active area 404 × 250 (video_sync.vhd:80-96; video_bridge.sv:46-53; top.sv:671-679, 2064-2067). Every NTSC Auto size fits (display_geometry.sv:46-60): 4× 1616×1000 on 1080p; 2× 808×500 on 1024×768/1200×800/1360×768; 3× 1212×750 on 1280×1024/1680×1050. DVI output has six display modes, all about 60 Hz [V docs/VIDEO_SETTINGS.md:47-58]: mode 0 1920×1080 at 148.5 MHz (the power-on default), mode 1 1024×768 at 65 MHz, mode 2 1200×800 at 67.5 MHz, mode 3 1280×1024 at 108 MHz, mode 4 1360×768 at 85.5 MHz, mode 5 1680×1050 at 119 MHz. `tb_c64_ntsc_video_bridge.sv:319` passes, but with `DYNAMIC_DISPLAY=0` (line 38) while production uses 1 (top.sv:1271): a gap closed by S8. The NTSC frame rate is 59.826 Hz, so the output repeats a frame at an interval that depends on the mode's exact refresh rate: about every 5.75 s for mode 0 (exactly 60.000 Hz) [I].
- **Firmware and packaging** [V]: `core_variant_index` reads 1 on C64 NTSC (top.sv:739-741); variants are saved per profile and host tests already use `pal`/`ntsc` for `c64`; no C64 firmware code reads the variant. Blockers: `build_phos1.py:18-19` (MATRIX has `c64-pal` only) and `:132` (PAL hard-coded); `package_phos1.py:83-85, 264, 313`; `package_firmware_release.py:26` maps only `pal`. Commit d6ccbb2 removed NTSC from packaging as a scope decision; CPU1_VIDEO_EFFECTS.md:376-379.
- **Timing history** [V]: stock NTSC images closed before the HP2/REU-DDR change: 20260915 +0.338/+0.048 ns (C64_LAUNCH_20260915.md:87); VID2 +0.031/+0.035 (CPU1_VIDEO_EFFECTS.md:227); VID3 +0.171/+0.030 (:286). No NTSC board result exists (HARDWARE_RESULTS.md:180, 276). Today's PAL c64 domain has +7.052 ns slack; the video clock (+0.133 ns) limits the image (c705r2 timing_summary.rpt:206-207).
- **Simulation coverage** [V]: VICE VIC comparison (65-cycle `cycle_tab_ntsc`), MAME VIC comparison (zero differences over 441,012 native cycles in 24 PAL/NTSC scenarios), machine schedule, interrupts, REU machine and native, DDR, TOD pause and the NTSC video bridge already run in both standards.
- **Third-party references on NTSC hardware** [M]: gist874 `timebase.tas` and `cpu_rate.tas` (C64U, "NTSC only"), gist839 `meas_c` and #839 DMA (U64E at 1.0227 MHz), Amiga64 CIA/TOD (U64E) and M5 per-read costs (C64U). PAL references: #203, the xahmol probe, the #665 reporter.
- **U64** [V]: switches PAL/NTSC at run time by reprogramming its C64 PLL (u64_config.cc:348, 1107-1152 at d43546f), because it has no full-PL reload path.

### 13.2 Decision: separate image, not a runtime switch [I]

| Criterion | (a) Separate NTSC image | (b) Runtime switch by MMCM reconfiguration |
|---|---|---|
| Existing support | Complete build-time path | None for C64 (the video clock's AXI reconfiguration is only a precedent) |
| Clock change | None at run time | Divider, fractional feedback (24 ↔ 56.75), both output divides and lock/filter tables [V XAPP888 p.1-3]; reset and relock up to 100 µs |
| Reset | Existing core change through a full PL reload | New: hold the core, drain HP2, reconfigure, relock, re-arm the bridge and mailboxes |
| Timing | Each image closes at its own clocks; PAL keeps its 0.29 ns half-period advantage | One image must close at NTSC rates |
| Video | Geometry fixed at elaboration | Runtime geometry; frame banks for 404×270, about +2.5 RAMB36 with BRAM at 91% |
| Fallbacks | NTSC-only fallbacks F1-F7 (F6 only as a diagnostic build, Q14) | One structure for both |
| User effect | Variant switch = PL reload; a C64 reset is needed anyway (VIC model change, KERNAL PAL/NTSC detect) | Faster switch; still a C64 reset |
| Release cost | +1 Vivado run and signoff; both variants rebuilt together | One image |

**Verdict: (a).** The NTSC image is variant `ntsc` of package `c64`, label "NTSC" (Q-N3), file `c64-ntsc.bin`. It is the same RTL as PAL except `C64_NTSC_MODE=1` and the NTSC clock plan. It ships only when it closes at 64x (Q14); a diagnostic F6 build (`C64_ISLAND_CYCLE=2`) is never released. Software sees identical turbo behaviour: the same speed table, slots per φ2 and fast-I/O rules. The exception is an image that needs a release fallback (`VIC_READ_GAP` = 3 or F7, section 2.13.2) and the other does not: that image reports its own timing revision or CAPS bits, and the release notes list the difference. Absolute times scale by 0.963 (for example `sta $de00,x / dex / bne` takes 1.956 µs per iteration on NTSC against 2.030 µs on PAL). Turbo settings are shared between the variants (Q5).

### 13.3 Clock plan

**N1 (adopted)** [V legal for -2 under DS187; I for the model]:

| Item | Value |
|---|---|
| D | 5 → phase detector 30 MHz |
| M | 24 (integer) → VCO 720 MHz |
| `c64_clk` | O0 /22 = 32.727273 MHz (30.556 ns) |
| `c64_cpu_clk` | O1 /11 = 65.454545 MHz (15.278 ns, half 7.639 ns) |
| Duty on /11 | EDGE bit: 5.5/5.5 VCO cycles [V XAPP888]; ±0.20 ns duty precision |
| Error | 0 ppm against 360/11 MHz (14.318182 MHz × 16/7); +0.21 ppm against 32,727,266 |
| Fast clocks per native cycle | exactly 64 |

**Alternatives** (C64_TURBO_MODELS.md (ntsc_plans.py)):

| Plan | Settings | Error | Notes |
|---|---|---|---|
| N2 | D=5, M=48, VCO 1440, /44 /22 | 0 ppm | Even divides; VCO exactly at the -2 limit; same tie |
| N3 | D=11, M=48, VCO 654.5, /20 /10 | 0 ppm | Low PFD and VCO; same tie |
| N4 | D=6, M=47.125, /36 /18 | −48.2 ppm | Fractional M; no tie; ps-exact (models 30.554 = 2 × 15.277 ns under every tie rule). Ladder step 3 |
| N5 | D=7, M=48.875, /32 /16 | +46.5 ppm | Fractional M; no tie, but not ps-exact (models 30.551 vs 2 × 15.276 ns); rejected |
| fractional /16.5 on the fast clock | — | 0 ppm | Rejected: duty and jitter of a fractional divider on the half-period clock |

**Fix ladder** (E2-NTSC): (1) read the plan back; check the fast waveform {0 7.638}, the pair timed together, a fast→c64 probe with a requirement of 15.276 ns, and N1 vs N2 jitter; as a 1 ps model check, also read back N4 (expect c64 30.554 = 2 × 15.277 ns, the pair timed, a probe requirement of 15.277 ns) and N5 (expect c64 30.551 vs fast 15.276 ns, not 2:1, and `check_timing` reporting the pair as unexpandable) [I]; any other result means the model is wrong, so fix the model and recheck N4 before step 3 relies on it; (2) if the tie resolves to 30.553 ns, redefine the c64 clock with exact 2× edge shifts (section 3.1 step 2); (3) if refused, use N4. If clk_wiz rejects the pinned plan, instantiate the MMCM directly (R8).

### 13.4 Timing risk and the NTSC go/no-go [I]

| | PAL 63.056 MHz | NTSC 65.455 MHz |
|---|---|---|
| Half period | 7.929 ns | 7.639 ns |
| After duty precision (0.20) and clock uncertainty (~0.10) | 7.63 ns | 7.34 ns |
| Out-of-context path (from +0.052 ns at 75 MHz, C64_TURBO_T65_TIMING.md) | 6.43 ns | 6.43 ns |
| **Out-of-context margin** | **+1.20 ns** | **+0.91 ns** |
| Real island logic not in the harness | −0.3 to −0.8 ns | same |
| Placement noise [V C64_TURBO_IIGS_LESSONS.md] | ±0.8 ns | ±0.8 ns |

Other NTSC paths: c64↔fast crossings and the FIO read path (X8a, about 10-11 ns est.) within 15.278 ns, 0.58 ns less than PAL; the c64 domain is expected at about +5.9 ns slack.

The full-image criterion is the island gate of section 10.2 (worst setup slack ≥ +0.1 ns on `c64_cpu_clk` intra-clock and c64↔fast paths, with the duty term) plus the existing image gates. Recorded C64 NTSC images closed at +0.031 to +0.338 ns image-wide (C64_LAUNCH_20260915.md:87; CPU1_VIDEO_EFFECTS.md:227, 286) [V], and in today's PAL image the limiter is the shared video clock, not the c64 domain (c705r2 timing_summary.rpt:206-207) [V]. A whole-image +0.1 ns requirement could therefore cap NTSC for no island reason (review finding 12).

| Result | Action |
|---|---|
| OOC Fmax ≥ 72.0 MHz **and** the island gate and image gates pass | Go at 64x |
| 65.45 ≤ OOC Fmax < 72.0 MHz **and** the island gate and image gates pass | Go at 64x; record the reduced margin in the release timing audit and re-check it on every rebuild |
| Island gate passes, image gates fail (video clock, `clk_fpga_0`) | The existing repair flow for that domain (R27); never the island ladder |
| OOC Fmax ≥ 65.45 MHz but the island gate fails | F1 → F2 → F3 → (`VIC_READ_GAP` = 3 if the read path limits) → F7, then rebuild |
| OOC Fmax < 65.45 MHz | The RTL steps first (F2, `VIC_READ_GAP` = 3, F7), then re-run E1; at or above 65.45 MHz, continue with the rows above |
| The whole ladder fails (OOC still < 65.45 MHz after the RTL steps, or the island gate still fails after F7) | **Hold NTSC** (Q14): no NTSC release; PAL ships alone. A diagnostic F6 build may be made to keep NTSC turbo testable; it is never packaged for release |

### 13.5 NTSC fallback ladder (only the NTSC image changes)

| Step | Change | Effect on behaviour |
|---|---|---|
| F1 | Pblock plus address replication | none |
| F2 | OR data mux with RSTRAM-zeroed outputs, if inference works with inverted clocks (as in R2) | none |
| F3 | IIgs repair recipe: placement sweep, reroute, post-route phys_opt [V C64_TURBO_IIGS_LESSONS.md §4] | none |
| — | `VIC_READ_GAP` = 3 on NTSC only (a release fallback) | up to +1 slot per VIC/SID read at N ≥ 33 (0.094 φ2 at index 15 and 0.125 at index 14; still turbo rate); NTSC then reports its own timing revision |
| F7 | NTSC-only `FAST_COLOUR_RAM=0` and `FORWARD_PORT_WRITES=1` (a release fallback) | same results; slower colour-RAM fills and `$01`-flipping code (D11 on NTSC); CAPS bits 22 and 30 clear |
| F6 | `ISLAND_CYCLE=2` | **Diagnostic or experiment build only; never shipped (Q14).** 32 slots per φ2; indices 12-15 deliver 32x; caps mask `0x1FFF`; `$D031` reads back what was written |

`VIC_READ_GAP` = 3 and F7 are release fallbacks (section 2.13.2): an NTSC image that closes at 64x with them ships. If F1, F2, F3, `VIC_READ_GAP` = 3 and F7 together do not close NTSC at 64x, NTSC is held back and PAL ships alone (Q14). The description of F6 below serves the diagnostic build.

How F6 works [I]:
- **Turbo:** the CPU steps at fast edges ending an odd f (coincident with c64 rising edges). The forward resume moves from end f=4 to end f=5; the acknowledge-class exit stays at the first odd edge at or after about f=13; forward requests are registered at odd edges, so the f ≤ 30 cutoff becomes f ≤ 29.
- **Exact mode is unchanged:** its enables stay at end f=0, the port tick at end f=2 and `nmi_ack` sampling at e_0, so the 2.10 argument holds as written. Exact mode makes no port-A access, and every T65 and B0 path is timed at one fast period in both island cycles, so an even enable edge is safe. Mode adoption at an enable boundary works the same; only the next turbo enable is odd.
- The island's RAM port runs on the non-inverted fast clock and is enabled only at edges ending an even f, which are the c64 falling edges. The address half and the data half each get 15.28 ns instead of 7.64 ns.
- Each island RAM edge falls half a c64 period (15.28 ns) from every machine-side RAM edge, more than the 6.8 ns of the normal scheme, so the collision argument of section 4.2 still holds. (C64_TURBO_NTSC.md stated 7.64 ns here; 15.28 ns follows from the edge positions.) The TR4 check flips: the island RAM clock must *not* be inverted. A bench assertion confirms the port is never enabled on an edge shared with the c64 clock.
- Fast I/O under F6: a 2-clock read fits inside one CPU step (address after the enable at the end of odd f, `io2_q` at the end of f+1, enable at the end of f+2), so VIC/SID reads cost no extra slot. The CPU pushes at most one entry per 2 fast clocks and the pop stage accepts one per c64 edge, so the FIFO only absorbs RMW pairs and holds, as at 64x. Pushes fall on c64 edges, so acceptance is the next c64 edge and landing comes 4 fast clocks after the write. S9 covers this.

Not used: F4 for NTSC only (the two images would run different CPUs; F4 in both images stays an R2 step); F5 (posted local writes; below 64x, Q9(a)); a non-2:1 NTSC multiple (breaks f = 2p + h, the proxy edges and the edge separation); changing `c64_clk`; a 1× island on `c64_clk`.

### 13.6 Tests and gates

| # | Test | Milestone | Pass condition |
|---|---|---|---|
| N0 | Stock NTSC image of today's tree, built and packaged by the manual flow of section 10.1 (no release-script change) | first implementation step (Q13) | board checks pass; wizard plan recorded |
| S1 | Existing NTSC suites with the island in exact mode | M2 | unchanged results |
| S2 | Exact-mode lockstep in NTSC | M2 | matches stock, including 6567R8 sprite-0 and sprite-3 bus-steal timing |
| S3 | Island machine bench, NTSC subset (E4, E5, E6, mode-switch fuzz) | M3 | same assertions as PAL |
| S4 | Fast VIC path in NTSC | M4-IO | `$D012`/`$D011` bit 7 correct at turbo through line 262; acknowledge rule; one selective clear per nonzero collision read; H2 calc-cycle hold in the 6567R8 sequence |
| S5 | Two-clock generator at NTSC periods from one process | M2 | coincident-edge assertions hold |
| S6 | Turbo register bench | M4-FW | reset window 2,045,454 cycles NTSC, 1,970,497 PAL |
| S7 | Delivered-rate bench | M3 | 17,095 CIA ticks per frame at indices 0, 5, 15; rate table matches |
| S8 | NTSC video bridge with `DYNAMIC_DISPLAY=1`, 6 output modes, multipliers 1-4 | M2 | passes |
| S9 | Diagnostic F6 builds only: the S2 exact-mode lockstep and the S3 machine subset with `ISLAND_CYCLE` = 2; Dormann at N = 32 and 16; FIO reads and FIFO under F6 | if a diagnostic F6 build is made | lockstep matches stock; resume at end f=5; grant counts; port A never enabled on a shared edge; collision checks on |
| S10 | Package, menu and build-input host tests | M4-FW | pass with `c64-ntsc`; the `0x1FFF` clamp is tested for diagnostic images; the packaging refuses a diagnostic build |

Build gates: E2-NTSC; OOC Fmax ≥ 72.0 MHz with the duty term (or ≥ 65.45 MHz with the reduced margin recorded, section 13.4); the island gate on the full-image skeleton, with and without the Pblock; the existing image gates; the per-standard clock gate (TR4); a `c64-ntsc` row in the release timing audit.

Board gates: the M5 table (section 10.2) and the release gates of section 10.3 that name NTSC. The NTSC devices are the direct references for gist874, gist839 and Amiga64.

### 13.7 NTSC timing constants [I, C64_TURBO_MODELS.md (ntsc_plans.py); the index-15 row from C64_TURBO_MODELS.md (top_rows_model.py)]

| Item | PAL | NTSC |
|---|---|---|
| `c64_clk` | 31.527778 MHz | 32.727273 MHz (Vivado model 30.552 or 30.553 ns, tie) |
| `c64_cpu_clk` | 63.056 MHz, half 7.929 ns | 65.455 MHz, half 7.639 ns |
| VCO / PFD | 945.83 / 16.67 MHz (fractional M) | 720 / 30 MHz |
| φ2 | 985,248.6 Hz | 1,022,727.3 Hz |
| Frame | 19,656 cycles, 50.125 Hz | 17,095 cycles, 59.826 Hz |
| Badline share (screen on) | 5.47% | 6.29% |
| Index-15 RAM loop, badline timing on (64 slots; C64U row 63 in brackets) | 60.50x [59.55x] | 59.98x [59.04x] |
| 2.0 s reset window | 1,970,497 cycles | 2,045,454 cycles |
| BRAM | 128.0 / 140 | same |

NTSC-specific risks are in section 9: R1 (tie), R2 (closure), R8 (plan accepted), R24 (no board baseline), R26 (release cost), R27 (image-wide misses outside the island). Also, with no fallback needed: the board's PS oscillator tolerance sets the absolute rate (N0 frequency check, Q-N5); programs that detect PAL/NTSC by counting cycles are covered by the 2.0 s exact window at reset (the KERNAL check), otherwise documented; the SID filter model at 1.0227 MHz is inherited from MiSTer.

---

## 14. Review findings and their resolution

### 14.1 Review of the v2 draft

Three reviews checked the v2 draft against the RTL and the build scripts: the fast VIC/SID path, bus ordering and IRQ, and clocking/NTSC/resources. Every finding below was re-checked against the cited files before it was accepted. None was a blocker; none was rejected. Nothing was built or simulated; the numbers come from C64_TURBO_MODELS.md (final_model.py) and the reviewers' models.

| # | Lens | Finding | Verdict | Fixed in |
|---|---|---|---|---|
| 1 | VIC/SID | LATCH entries inside the global read barrier made unrolled `$D419`/`$D41A` reads cost 0.125 φ2 at index 15, not 0.079, failing T4 and the M5 gate | Fixed: side-effect entries block only the reads they change (`dep_ok`); WR entries keep the global barrier; reads that may push need a free slot. Model: 0.079 φ2 again | 2.13.1 CF2, 2.13.2, 2.13.4, 2.13.9 O2/H16, 5.9, 5.11, 8, 10.2 T4 |
| 2 | VIC/SID | The all-bits collision clear had a blind window that aliases with the pixel clock: a `LDA $D01E / BEQ` loop at index 15 could spin forever | Fixed: the CLR entry carries the returned bits; only those are cleared, on the next-state value; nothing is pushed for a zero byte | 2.13.2 (entry D), 2.13.4, 2.13.12 T-e, D14, 10.2 T1/T3, 12 (P4/P5) |
| 3 | VIC/SID | A `$D015`/`$D001` landing at e_14 of the `cycleCalcSprites` cycle drops sprite-0 BA one cycle late; the first sprite byte reads `$FF` | Fixed: H2 also holds acceptance phase 13 of that cycle; T2 compares pre-edge `baLoc`, `baCnt`, `aec` and sprite data, with a negative control | 2.13.5 rule 5, 2.13.6, 10.2 T1/T2, 12 |
| 4 | VIC/SID | The precise read-after-write table said SID write-only registers depend only on themselves; they read the bus latch | Fixed: explicit SID rule (every SID WR or LATCH entry for a selected chip) | 2.13.9 table, 2.13.4 |
| 5 | VIC/SID | "Native priority" contradicted appending the fast blocks after the stock VIC code | Fixed in the text: VIC fast wins, SID stock wins, coincidence excluded by construction and asserted (A6) | 2.13.5 rule 8 |
| 6 | VIC/SID | The Doom burst used a 16-cycle loop; Doom's `miPlay` loop is 26 cycles (10.9 µs, not 6.8) | Fixed: both loops listed; the irqtest loop and IRQ rate are taken from `src/irqtest.asm` (open question 14) | 0.3, 8, 10.2 T4 and M5, 11 |
| 7 | Bus | `all_done_q` was registered from the pre-push `wp`, so `ack_wait_q` dropped one clock after it was set and reopened IRQ re-entry after `LSR $D019 ; RTI`; out-of-order done events could hang the queue | Fixed: every flag is registered from the post-push state; `ack_wait_q` clears on `fio_done` reaching `ack_end`; the done pipe is in order; A5/A7 assert on indices | 2.13.2, 2.13.5, 5.8, 5.11, 10.2 |
| 8 | Bus | The retired exact request stayed in the frame and re-ran at the next e_15 after an exact→turbo switch (also in v1) | Fixed: the frame returns to the dummy at every retiring R; an unretired (DMA-held) request stays | 2.6, 2.10, 2.11, 2.13.10, 5.1, 10.2 (M2, M3, A8), 12 |
| 9 | Bus | `full_q` lagged a push; depth 1 and `FIO_RAW_PRECISE` = 1 could overflow | Fixed: post-push `full_q`; free-slot term on pushing reads; depth range 1-8 | 2.13.2, 2.13.4, 2.13.9 |
| 10 | Bus | RST1/RST2 need fpga64's phase-aligned `reset`, which no port exported | Fixed: new `ext_reset` output | 2.3, 2.10, 3.3, 12, 10.2 M2 |
| 11 | Bus | `baCnt` clears on any clock with `baLoc` = 1, not only at e_15; T2 did not compare it | Fixed: text corrected; T2 compares `baCnt`/`aec`; the double-toggle case is accepted as turbo behaviour (T-h, open question 16) | 2.13.6, 2.13.12, D14, 10.2 T2, 11 |
| 12 | Clock/NTSC | The +0.1 ns WNS gate covered the whole image, which the video clock has missed before; the NTSC ladder would then cap NTSC for no island reason | Fixed: separate island gate; existing image gates unchanged; non-island misses go to the existing repair flow (R27); new 13.4 row for 65.45-72 MHz | 3.4 TR4, 9 R27, 10.2 M0/M1, 13.4, 13.6 |
| 13 | Clock/NTSC | Same root cause as 7 and 9 (registered status lags a push) | Fixed with 7 and 9 | 2.13.2 |
| 14 | Clock/NTSC | A live class decode had moved into port-A EN, on the half-period path | Fixed: EN from registered terms only; WE takes the class, `map_ok` and `fetch_done_q` | 2.8, 5.7, 9 R2, 12 |
| 15 | Clock/NTSC | F6 did not say when exact-mode and resume enables happen; S9 lacked the exactness tests | Fixed: exact mode stays at f=0; turbo resume at f=5; S9 runs S2 and S3 under F6 | 13.5, 13.6 S9, 12 |
| 16 | Clock/NTSC | Placement and wiring of the C64-only timing rules were unspecified; the R8 fallback renames clocks | Fixed: new `[c64]` XDC with exact names, added to the implementation-only list; uncertainty probe in TR4; R8 lists the name updates | 3.4 TR1/TR4, 9 R8, 11 (question 15), 12 |
| 17 | Clock/NTSC | N0's variant-switch check needs packaging that arrives only in M4-FW | Fixed: manual N0 flow with `make_core_package.py` and two variants, outputs under `build/` | 10.1 N0, 13.6 |
| 18 | Clock/NTSC | "DVI output is always 1080p60" was stale | Fixed: six-mode list; frame-repeat interval depends on the mode | 13.1, 10.1 N0 |

### 14.2 Review of the v3 draft

Three reviews checked the v3 draft: one against the user decisions, one against the repository RTL, firmware and models, and one for document consistency. They raised 37 issues; findings that two or three reviews raised are merged below. Each was re-checked against the cited files and against docs/C64_ULTIMATE_AUDIO_DESIGN.md as it stood on 2026-10-23. None was rejected. Nothing was built or simulated.

| # | Review | Finding | Resolution | Fixed in |
|---|---|---|---|---|
| 1 | decisions, document | The host and debug registers used 0x0F0-0x0F8, the offsets of Ultimate Audio's three registers, while the text said nothing collides | One offset map: UA keeps 0x0F0-0x0F8; host and debug registers move to 0x100-0x110 | 6.0, 6.1, 11 (question 22), 12 |
| 2 | all three | "Any non-default generic is a diagnostic build" made the timing fallbacks (`VIC_READ_GAP` = 3, F7) unshippable | Three sets: release defaults, release fallbacks, diagnostic settings; a per-image timing revision | 2.7, 2.13.2, 3.4 TR4, 6.1, 9 R2, 12, 13.2, 13.5 |
| 3 | decisions | G-REU0 was strict on PAL only | Configured runs strict against the same image's stock run on both images; the NTSC stock run is recorded, with the reason | 10.2 (M2, M5) |
| 4 | decisions, document | Ultimate Audio's `:uastat` and observation counters were outside the `:debug` gate | Named as debug facilities; coordination item | 0.6 (Q16), 6.5, 11 (question 22) |
| 5 | decisions | Q-BA1 was filed as closed by the user | Open as question 25; D16 "proposed permanent" | 0.6, 1.8, 1.12, 9 R23, 11 |
| 6 | decisions | The sweep's REU gates were only partly carried | E-BA3 trigger cycles 1, 3, 5, 20 and the transfer matrix on the default path; `reutiming` inside the reset window; an E5 start-offset assertion | 9 R6, 10.2 (M2, M3, M4-IO, M5) |
| 7 | decisions, document | UA's turbo-side requests were missing, and neither document owned UA quiescence during preload | UA acknowledge writes in E4; the one-φ2 `$DF00-$DFFF` check at all 16 indices in T3; this design owns the UA hold bit and the preload sequence | 6.1, 6.4, 9 (R4, R32), 10, 11 |
| 8 | decisions, document | `:diag arm` overlapped the existing ungated `:diag`; the Q16 scope reading was not tagged | `:turbo diag on\|off`; the Q16 row carries the scope interpretation | 0.6, 6.5, 11 (question 20) |
| 9 | decisions, technical | Gates allowed without a debug session could not log the caps words their results need | Every gate run opens a session; one sessionless stock run shows the session has no effect | 6.5, 10.2 M5 |
| 10 | decisions | R29's fallback removed UART debug from release firmware | The fallback keeps UART enablement | 9 R29 |
| 11 | technical | The G-REU0 Registers case with a 64 MHz preference runs Expand at 64x, not exact | Registers with preference `$00`; TurboEnable at 64 MHz; Manual index 0 | 10.2 (M2, M5) |
| 12 | technical | M9 claimed "stock by construction" although its prerequisites change the REU cache and need a hook on the REU byte engine | Interposer and DDR read-ahead named; identity with the option off proven by G-REU0 and the REU suites on the M9 netlist | 1.8, 10.2 M9, 12 |
| 13 | technical | D16 ("every mode") contradicted Follow CPU bytes ignoring BA | D16 limited to native-slot DMA; M9 named as the exception | 0.6, 1.8, 1.12, 10.2 M9 |
| 14 | technical | tb_c64_cpu_turbo_guard already exists and overrides the parameter the design removed | Per-code holdoff parameters; the bench is updated, and its assertions stay as R33's check | 1.9, 10.2 M4-FW, 12 |
| 15 | technical | The static-hold release masked only the output, used the 25-bit counter and left the cause priority open | Masks output and reload; own ≥ 26-bit counter; cause priority; strobes covered | 1.9, 6.1, 12 |
| 16 | technical | The 2-bit holdoff field could not test half of 20 or 50 ms | Next-shorter-code rule; 20 ms is a test point only | 1.9, 10.1 |
| 17 | technical | Diagnostic bits stayed set after a disarm, and a core switch armed them | Masked while disarmed; a separate armed flag | 6.1, 6.5, 10.2 |
| 18 | technical | M2 asked for transitions into turbo, which needs the M3 engine | M2 keeps exact-state transitions; cause↔turbo transitions move to M3 | 1.3, 10.2 (M2, M3) |
| 19 | technical | The STATUS bit 22 hint was wrong for `$D030` writes | Bit 22 for `$D031`/`$D07x` in Off/Manual; new bit 23 for `$D030`; zero writes set neither | 6.1, 6.2, 10.2, 12 |
| 20 | technical | A `:speed` override could be saved by a later menu change or action | Overlay outside the settings; ended by menu, action or profile changes | 6.2, 6.3, 6.5, 10.2 |
| 21 | document | M0 sent a standard down its ladder while section 13.4 held NTSC at once | RTL steps before a hold; PAL to R2, NTSC to section 13.4 | 10.2 M0, 13.4 |
| 22 | document | A `\|` inside a code span broke a table row | Escaped | 12 |
| 23 | document | D16 still called provisional in CF7 | Updated | 2.13.1 |
| 24 | document | Stale N = 63 figures for `VIC_READ_GAP` = 3; 64.0x shown although the loop model gives 63.88x | Re-quoted from vicread_model.py and top_rows_model.py | 0.3, 2.13.2, 2.13.4, 8, 13.5 |
| 25 | document | References to v1/v2 sections and question numbers a repository reader cannot resolve | Earlier rules stated in place; the drafts noted as not kept | header, 0.3, 0.6, 8, 9 R22, 11 |
| 26 | document | Short IDs had two or three meanings | Prefixes: RST (reset rules), TR (timing rules), CLK (clocks), CF (sub-design conflicts), SD (stock deviations), SF (SID facts), T1-xx (T1 sub-cases) | whole document |
| 27 | document | F2 was in R2 but not in the NTSC ladder | Added | 13.4, 13.5 |
| 28 | document | `:speed` was missing from the apply sources of section 1.5 | Added; "debug setting" narrowed to the debug knobs | 1.5 |
| 29 | document | STATUS bit 24 was never defined | Defined | 6.1, 12 |
| 30 | document | The docs and firmware change lists had a duplicate and two gaps | Fixed | 12 |
| 31 | document | top_rows_model.py loaded its dependencies from a sibling design-stage folder and printed design-stage file names | It now loads its siblings from its own folder first; those names are removed; output regenerated with identical numbers | Model scripts list |

### 14.3 Reconciliation with docs/C64_ULTIMATE_AUDIO_DESIGN.md

On 2026-10-23 both designs were aligned to one shared interface for one release. The user decided Q17-Q20, and the design lead decided DL1-DL6, which the user may override (section 0.6). Where a 14.2 resolution names `:turbo diag`, question 22 or question 25 as open, the rows below supersede it. Rows 16-19 come from a cross-review of the two reconciled documents. Nothing was built or simulated.

| # | Item | Before | Now | Where applied |
|---|---|---|---|---|
| 1 | Release and ABI (Q17) | One ABI step per image that adds registers; Ultimate Audio expected first and turbo then at 0.25; preload in whichever image ships first | One release and one step, 0.23 → 0.24, for turbo, UA, preload and the debug registers; four constants of 24 (adds `MT_DEBUG_MIN_ABI_MINOR`); the gating order package ABI → live ABI → caps; neither feature ships alone | header, 0.6, 6.0, 6.1, 10, 10.3, 11 (question 21), 12 |
| 2 | UA and REU coupling (Q18, DL1) | "Turning UA on enables the REU", owned by the UA design, whose draft greyed the row with "Needs REU" | UA on turns the REU on in the same apply; REU off turns UA off; a cartridge blocks both. One global UA flag (byte 26 bit 2) with the per-profile REU flag, in one save, then one register write. The HDL clamp clears `C64_EXPANSION_CONFIG` bit 4 unless bit 3 is set | 0.1, 1.4, 1.8, 1.10, 6.1, 6.2, 6.3 |
| 3 | Debug scope (Q19) | `:turbo diag` armed only turbo STATUS bits 24-26; the UA draft had free-running counters and an ungated `:uastat` | `C64_CPU_CONFIG` bit 19, "C64 diagnostics armed", gates both features' diagnostic fields and is set by `:c64diag on` inside a session; `:uastat` is debug-only; `ua_fault` stays live; one gate procedure for both features | 0.1, 3.2, 3.3, 6.1, 6.5, 10.2, 10.3 |
| 4 | Expand gate (Q20) | G-REU0 here and G1 in the UA design, neither naming the UA state | One G-REU0, with UA off, judged against M0.7.0dev5 on PAL. An Expand failure with UA on caused only by the missing mirrors is accepted and documented | 1.8, 10.2, 10.3 |
| 5 | I/O2 rules | The UA design's §1.10 cited the v1 text (I/O2 waits for BA = 1) and a retired M6 | I/O2 completes in BA-low cycles through I/O-only ownership with badline timing disabled: one rule for every internal φ2 target, benched by E-BA1 and approved by Q1. The pacing rule (one φ2 per access, never FIO) is stated in section 1.7 | 1.7, 2.13.3, 2.13.8, 5.8 |
| 6 | Preload acknowledgement | "Wait for one mailbox round trip" after setting the UA hold, which firmware cannot observe | New read bit `C64_REU_HOST` bit 14, UA held, with a 100 ms timeout; timeouts for every wait; error paths; a "content load in progress" refusal | 3.2 (X11), 6.1, 6.4 |
| 7 | `$DF00`-mirror reads with UA on | Acknowledge class by address, with no stated reason for UA on | Kept: the address-only decode keeps UA state out of the island path; with UA on the cost is a few fast clocks | 5.8 |
| 8 | UA build parameters and the G9 probe | No release rule | UA parameters at their release defaults unless the UA design's experiment for each settles otherwise; `UA_COHERENCE_PROBE` = 1 is a diagnostic setting (CAPS2 bit 14; packaging refuses it); 0x0FC reserved for the probe | 2.13.2, 3.4 (TR4), 6.0, 6.1 |
| 9 | Golden trace | UA T2 "before any RTL edit", written without turbo's RTL edits in view | Before any RTL edit by either feature (release step 1), because turbo edits fpga64 and the adapter | 10.3 |
| 10 | IDs | Both documents used M0-M5 and T1-T7 | UA milestones UA-M0 to UA-M5; "UA T9", "UA G3" across documents; one G-REU0; cross-feature tests X-T1 to X-T9; board gates X-DET and X-PRELOAD; release steps 0-5, because S1-S10 stay the NTSC tests | 10.3 |
| 11 | Citations | The UA design cited this document by line number | It cites section numbers. Neither document renumbers a section the other cites; new material here went into new subsections (10.3, 14.3) | whole document |
| 12 | UA level row | Argument 9 in the UA draft | Argument 11, not in the first release | 6.2 |
| 13 | Options order | Ended at "the existing expansion rows" | The full 12-row order, with arguments 2, 3, 8 and 11 routed to `change_c64_expansion()` | 6.2 |
| 14 | Open items | Questions 9, 18, 19, 20, 21, 22, 24 and 25 open | Closed by DL6, DL2, DL4, DL5, Q17, this reconciliation, DL5 and DL3 | 0.6, 1.2, 1.12, 2.13.2, 6.2, 9, 11 |
| 15 | Combined image | Separate budgets; no placement risk for the pair | Combined resources (section 4.3); M1 placed with UA; one CDC review including UA C3-C5; new risk R34 | 0.5, 3.1, 3.2, 4.3, 9, 10.3 |
| 16 | Global UA flag on other changes | Every SID, REU or UA change wrote the global flag from the effective UA bit, so a SID change in a profile restored with its stored cartridge cleared the global flag | Only a change that flips the UA bit writes the global flag | 6.3 |
| 17 | REU-side file list | `multitini_c64_reu.sv` listed as unchanged; Ultimate Audio's two observation outputs for `C64_REU_MISSES` not listed | Both listed as observation-only, with their checks (existing REU suites, UA T2 and X-T3, G-REU0) | 1.8, 12 |
| 18 | Board run without a session | Counted under the logging rule, which it cannot meet | A control, not a gate result; valid when the session opened right after it logs the same CONFIG, CAPS and CAPS2 words | 6.5 |
| 19 | Editorial | Decision and question numbers could be confused; build-report figures cited as if tracked; the UA design cited as [V] evidence; the pre-reconciliation text called "v3" | ID note (section 0.6); build-report note in the header with the tracked +0.133 ns record; firmware lines cited directly; "pre-reconciliation draft" | header, 0.6, 4.3, 6.0, 6.5, 9, 11, 15 |

### 14.4 Independent review of 2026-10-23

An independent review of this design and docs/C64_ULTIMATE_AUDIO_DESIGN.md raised seven findings. Each was checked against the documents and the RTL, reproduced with a model where it made a numeric or sequence claim, and confirmed by a second, adversarial check. All seven were accepted and fixed on 2026-10-24. None of the fixes changes steady exact mode, stock REU behaviour or G-REU0. New figures for findings 1-6 come from the models listed in the header (C64_TURBO_MODELS.md). Finding 7's figures are [I] closed forms in docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.4.5, checked by a design-stage model that is not kept; `scripts/ua_model.py` regenerates them in UA-M0. Finding 7 is fixed in docs/C64_ULTIMATE_AUDIO_DESIGN.md (its section 0.8); this design changes only the UA G3 and G5 rows of section 10.3.

| # | Finding | Fix | Sections changed |
|---|---|---|---|
| 1 | The v3 NMI set/clear latch loses an NMI when the T65's last sample was low and the line rises and falls during a stall. CIA2 can trigger this with badline timing on: an ICR read, a BA halt from e_0 of the next cycle, then a second CIA2 event in the halt. The line then stays low, so every later CIA2 NMI is lost. The TD claimed turbo halts cannot drop a CIA2 NMI. | Replaced by hold and rearm (`nmi_seen_q`, `fell_q`, `force_q`). After a low sample, the T65 is shown 1, then 0. An enable counts a fall on its own clock and then clears it. Turbo → exact keeps its boundary for every cause; a pending rearm or held fall is finished on the first exact enables (the carry), so sections 1.9 and 2.11 hold. Exact → turbo starts from `nmi_seen_q`. Steady exact mode, stock REU behaviour and G-REU0 do not change; with turbo never enabled the exact path is bit-identical. Letting the completion enable ignore the halt is rejected as a fix on its own. Overclaims corrected, T3 benches extended, model nmi_rearm.py added. | 0.2, 1.5, 2.1, 2.8, 2.9, 2.10, 2.11, 2.13.9 (O7), 2.13.10, 2.13.12 (T-j), 3.3 (RST4), 4.3, 5.1, 5.8, 5.9, 6.1 (STATUS bit 26, CAPS2 bit 11), 9 (R35), 10.2 (M3), 11 (question 26), 12; C64_TURBO_U64_ISSUES.md 1.3 (item 10), 3(g)5; C64_TURBO_FAST_SID.md 3.6; MODELS (nmi_rearm.py) |
| 2 | Preload step 4 copied through the cacheable DDR mapping and flushed after each block. A clean line left by `:reudump`, prefetch or speculation went stale under REU DMA, and a partial-line copy wrote up to 31 old bytes back on each side of the range. A step-5 invalidate timeout had no path. | Map the REU aperture 0x25000000-0x25FFFFFF Normal non-cacheable (`NORM_NONCACHE`) once at CPU0 start-up, before CPU1 release. Drop the per-block flush and the `:reudump` invalidate. End step 4 with dsb, PL310 cache sync, dsb (Early BRESP is on). Step 5 retries once; if it still times out, firmware keeps UA held and the machine paused and asks for a core reload, because only a PL reset clears the REU read lines. Add host tests (mapping, mocked step-5 timeout) and the X-PRELOAD adjacent-byte case; C64_TURBO_MODELS.md (preload_cache.py). No HDL change; stock REU behaviour and G-REU0 are unchanged | 6.4 (a), 6.4 (b), 9 (R28), 10.2 (X-PRELOAD), 10.3 (X-PRELOAD), 12 |
| 3 | SID edge stage: the contract promised the first and last value of every window and said the stage removes every lost sub-µs gate/test pulse. In fact two consecutive pulse windows lose the second pulse (engine samples 1 0 1 1 1, against 1 0 1 0 1 at 1 MHz). Bench (e) also listed e_31 landings of control bits 7:4 (norm_dac) and of chip-1 volume as bit-identical, and bounded filter differences to one native cycle | Kept the one-byte stage and narrowed the contract: isolated pulses are kept (at most one per register per 2 µs; precisely, a pulse whose window starts without applying a stage), and denser sequences are compressed. Keeping every pulse (a SID hold in the shared FIFO) is rejected as a partial paced queue that also delays VIC writes. Bench (d) gains consecutive-window, test-pulse, edge-write and isolated cases with literal expected samples (sid_edge_stage.py). Bench (e) now has control bits 7:4 at phases 0-30 and chip-1 $18 bits 3:0 at 15-30; $15-$17 writes are compared against a stock twin (svf_persistence.py); e_31 landings are documented as expected differences. Exact mode, stock REU and G-REU0 are unchanged | 1.12 (D15), 2.13.7 (SF9, SF11, edge stage effect, why safe), 10.2 (M4-IO tb_sid_fast_port), 11 (question 9), 12 (C64_EXPANSIONS.md row); C64_TURBO_FAST_SID.md header, 1, 2.1 (F9, F11), 3.4, 3.5, 6.1d, 6.1e, 8; C64_TURBO_U64_ISSUES.md 3(g)7; MODELS (sid_edge_stage.py, svf_persistence.py) |
| 4 | T2's collapsed (last-value) reference loses sticky VIC events, so T2 would fail correct RTL: a one-edge DEN = 1 on line 48, a one-edge `$D012` match or mismatch, and `$D019` acknowledge pulses such as the `$F1` dummy write of DEC `$D019`. 2.13.6 (c) and FAST_VIC 3.7 (c) said several landings "reach the same state". Class (a) said no hidden state can get stuck, but the collision latches can | Fixed in the bench and docs only; the RTL, exact mode, stock REU behaviour and G-REU0 are unchanged. The collapse now covers class-(b) state only. A per-event scoreboard is T2's primary check for `rasterEnable`, IRST/`rasterIrqDone` and the OR of the `$D019` acknowledge masks, and its `rasterEnable` is forced into the reference. New directed cases (DEN pulse, temporary match, re-arm, INC `$D012`, DEC/LSR/ASL `$D019`) are each expected to equal a stock C64 with the writes in separate cycles (C64_TURBO_MODELS.md (vic_t2_sticky.py)). The wording now says the DUT behaves like stock issuing the writes in separate cycles and that the sticky effects are expected. Collision latches leave the strict compare during pixel-level landings. New T-i, E7-V9 and open questions 27 and 28 | 1.12 D14, 2.13.6, 2.13.12, 9 R15, 10.1 E7-V, 10.2 T2, 11, 12; FAST_VIC header, 3.7, 6 (T2); MODELS |
| 5 | Below N = 64 the end f=4 forward-completion enable used no grant, so index 0 with badline timing disabled ran I/O code faster than its 1 MHz label (unrolled lda $de80 in 3 φ2; I/O loops at 1.11-1.33x). Separately, '2 at 2-4 MHz' cited a U64E measurement that covers only 3-5 MHz | Added a one-bit completion credit fwd_credit_q. Grants while a forward, ACKWAIT or DMA_WAIT is outstanding set it. Every completion enable needs the credit or a coincident grant, and clears the credit. A mode change also clears it. At N ≥ 2 the credit is always set, so no N ≥ 2 figure changes; at N = 1 an unrolled lda $de80 takes 4 φ2. Stated the speed contract: delivered cycles per φ2 never exceed the label. Rejected the own-grant rule because it breaks the measured 6 MHz meas_c row. Marked the 2 MHz value as the model's. Added the governor unit test, a T4 case and the M5 row | 0.1, 1.3, 1.6, 1.7, 2.7, 2.13.2, 5.2, 5.5, 5.9, 6.2, 8, 10.2 (M3, T4, M5), 11 (question 29), 12; C64_TURBO_NTSC.md §6.4; C64_TURBO_MODELS.md (final.py and rot.py descriptions; new governor_credit.py) |
| 6 | ntsc_plans.py's 'robust 2:1' test only measured distance from a ps tie, so N5 (D=7 M=48.875 /32 /16) passed although it models as 30.551 vs 2 × 15.276 ns; NTSC.md called N5 ps-robust and offered it as alternative step 3 | Predicate replaced by a ps-exact test (frac(fast) < 0.23 or > 0.77, which implies round(c64) = 2 × round(fast) under every tie rule, with a 0.02 ps margin) and output regenerated. N5 is now marked tie-free but not ps-exact, and rejected. N4 is named as the only ps-exact NTSC plan; plan_ps.py already agreed. N4/N5 read-back with expected periods added to E2-NTSC. Added a note that the N1 tie may be decided by Vivado's floating point. Ladder (N1, step-2 constraint, N4) unchanged | 3.1, 9 (R1 fallback text), 10.1, 13.3; NTSC 3.3, 8; MODELS (ntsc_plans.py) |
| 7 | UA underrun rule used the latency of one DDR read; seven voices share one outstanding read with a 32-clock gap, so the limit is aggregate (docs/C64_ULTIMATE_AUDIO_DESIGN.md §2.4.5) | UA design restates the rule as an aggregate deadline with first-sample and mid-stream envelopes against the 64- and 90-clock budgets, adopts earliest-deadline issue, fixes T4, adds T4b, and adds a 48 kHz underrun count to UA G3 and G5; stock REU, the REU cache, HP2 and G-REU0 unchanged (UA is Off there) | 10.3 (UA G3 readiness row, UA G5 row) |

---

## 15. v3 changes and where they land

v3 changes the v2 design only where the user decisions of 2026-10-23 require it. Everything else in v2, including the 18 review fixes of section 14.1, is kept. The fixes from the v3 review are in section 14.2; the rows below include them. The last ten rows record the reconciliation with docs/C64_ULTIMATE_AUDIO_DESIGN.md (section 14.3). The fixes from the independent review are listed in section 14.4; of the rows below, only the NMI row changed with them.

| Change | Decision | Sections |
|---|---|---|
| Indices 14 and 15 deliver 48x and 64x; `c64u_top` restores 47/63 (CONFIG bit 7, CAPS bit 19, STATUS bit 5); an 18-row grant table; index 14/15 figures from C64_TURBO_MODELS.md (top_rows_model.py) | Q9(a) | 0.1, 0.3, 0.4, 1.3, 1.12 D17, 2.7, 2.13.4, 2.13.6, 6.1, 7, 8, 10.2 (M3, T4, T5, M5), 13.7 |
| Disk auto-slow: a stricter form of the U64's IEC slowdown; Off labelled "disk access may fail"; block cause (STATUS [7:6]); programmable holdoff (CONFIG [17:16]); static-hold release designed, shipped off (CONFIG [11]); E-IEC; G-IEC | Q6, Q9(b) | 1.4, 1.9, 1.12 D3, 6.1, 6.2, 9 (R11, R33), 10.1, 10.2, 12 |
| Forced exact overrides `$D031` bit 7 | Q9(c) | 0.1, 1.6, 1.11, 2.11, 10.2 (M2, M3) |
| TurboEnable apply raises live only if live ≠ `$00`; the apply counter moves only on real value changes; SCPU Detect left the apply set | Q9(d) | 1.2, 1.5, 1.12 D18, 6.1, 10.2 (M4-FW) |
| Menu visibility (requested and effective speed, reason), sticky hints (STATUS 21-23), rows editable while Off, help texts, the one-step Turbo on/off row (argument 9), three turbo actions with a companion bindings file | Q9(e), Q9(f) | 0.1, 6.1, 6.2, 6.3, 9 (R30), 10.2, 11 (question 18), 12 |
| Expansion memory speed: Follow CPU (M9; argument 10; CONFIG bit 18; byte 27 bit 4; CAPS2 bit 12), an interposer on the REU DMA handshake, proven identical to stock with the option off; D16 for native-slot DMA (now permanent by DL3; the pre-reconciliation draft left Q-BA1 open as question 25) | Q9(g) | 0.1, 0.6, 1.4, 1.8, 1.12 (D16, D19), 6.1-6.3, 10.1 (E-REU-S), 10.2 (M9), 11, 12 |
| NMI edge hold and rearm | Q9(h); section 14.4 | 0.2, 1.5, 2.1, 2.8, 2.9, 2.11, 2.13.9 (O7), 2.13.10, 2.13.12 (T-j), 3.3 (RST4), 4.3, 5.8, 5.9, 6.1 (CAPS2 bit 11, STATUS bit 26), 9 (R35), 10.2 (M3), 11 (question 26), 12 |
| No fast or local path answers an unmapped address | Q9(i) | 0.1, 1.7, 1.10, 2.5, 10.2 (M3, M5, M7) |
| Turbo timing revision in CAPS2 | Q9(j) | 2.7, 6.1, 10.2 (M4-FW), 12 |
| Sweep gates: G-REU0 (three configured-but-exact states, strict on both images), `reutiming` (including the reset window), the E5 cases and start-offset assertion, E-BA3 trigger cycles and transfer matrix, G-IEC, PAL/NTSC detect after reset, floating bus, RMW loops, `c64u_top` A/B, OSC3 then `$D418`, the index-0 lockstep cases | Q9(k), Q8 | 1.8, 10.2 |
| No per-title settings; M7 stays after the first release; "(cartridge: 1 MHz)" status | Q10 | 1.4, 1.10, 6.2, 6.3, 10.2 (M7) |
| SuperCPU removed; `$D07x` kept as U64 turbo switch registers; freed bits reclaimed (CONFIG bit 7, CAPS bit 19, byte 27 bit 4); D10 removed; M8 trimmed | Q11 | 0.1, 1.1, 1.2, 1.4, 1.12, 6.1, 6.3, 10.1 (E7), 10.2 (M8), 11 (question 19) |
| Ultimate Audio: the `$DF20-$DFFF` I/O2 window, acknowledge writes (also in E4), the one-φ2 check at all indices, argument 8, byte 26 bit 2, expansion caps bits 3 and 4, the shared offset map (UA at 0x0F0-0x0F8), the UA hold for preload, the ABI step (one step since Q17) | Q12 | 0.1, 1.7, 1.8, 2.5, 2.13.3, 2.13.8, 5.8, 6.0-6.5, 9 (R4, R32), 10.2, 11 (questions 21, 22), 12 |
| N0 is the first implementation step | Q13 | 0.5, 10, 13.6 |
| NTSC held until it closes at 64x; F6 a diagnostic build only; release fallbacks (`VIC_READ_GAP` = 3, F7) may ship, with a per-image timing revision; F2 in the NTSC ladder; RTL steps before a hold | Q14 | 0.1, 0.5, 1.3, 3.1, 3.4 (TR4), 4.2, 4.3, 6.1, 6.2, 9 (R2, R26), 10.2, 13.2, 13.4-13.7 |
| Host features: REU image preload (`C64_REU_HOST` with the UA hold bit, expansion caps bit 4, cache invalidate, optional `:reuload` offset), C64 RAM readback (`C64_RAM_PEEK`), audio capture (`DEBUG_*`, `AUDIO_CAPTURE_DATA`); crossings X10-X12; reset RST4; BRAM +0.5 | Q15 | 0.1, 0.5, 1.8, 2.4, 3.2, 3.3, 4.1-4.3, 6.1, 6.4, 9 (R17, R28), 10.2, 12 |
| Debug facilities off by default: UART `:debug`, debug-only commands (an arming command, now `:c64diag` by Q19), the `:speed` override overlay, HDL enables, armed diagnostic bits masked while disarmed, every board gate run inside a session, diagnostic settings refused by packaging | Q16 | 0.1, 1.4, 3.4 (TR4), 6.1, 6.5, 9 (R29), 10.2, 11 (questions 20, 24), 12 |
| Evidence cited by repository document names instead of session paths | — | whole document |
| One release and one ABI step (0.24) for turbo, Ultimate Audio, REU image preload and the debug registers; four ABI constants and the gating order; the full offset map with 0x0FC reserved; the combined release plan (release steps 0-5, X-T1 to X-T9, X-DET, X-PRELOAD, merged G-REU0, failure policy); combined resources, CDC review and risk R34 | Q17 | header, 0.1, 0.5, 0.6, 3.1, 3.2, 4.3, 6.0, 6.1, 9 (R32, R34), 10, 10.3, 11 (question 21), 12, 14.3 |
| Turning Ultimate Audio on turns the REU on; turning the REU off turns it off; a cartridge blocks both; the full Options order and routing; byte 26 and the per-profile REU flag written in one save; the `C64_EXPANSION_CONFIG` bit 4 clamp | Q18 | 0.1, 1.4, 1.8, 1.10, 6.1, 6.2, 6.3, 10.2 (M4-FW), 12 |
| Debug facilities of both features off by default and UART-only: `C64_CPU_CONFIG` bit 19 renamed "C64 diagnostics armed" and shared with Ultimate Audio; `:c64diag` replaces `:turbo diag`; `:uastat` debug-only; `UA_COHERENCE_PROBE` a diagnostic setting; one board gate procedure | Q19 | 0.1, 1.4, 2.13.2, 3.2 (X3), 3.3 (RST4), 3.4 (TR4), 6.0, 6.1, 6.5, 9 (R29), 10.2, 10.3, 12 |
| G-REU0 with Ultimate Audio off, judged against M0.7.0dev5 on PAL and merged with UA G1; Expand with UA on informational, a mirror-only failure documented | Q20 | 1.8, 10.2 (M2, M5), 10.3, 12 |
| The Ultimate Audio preference is one global flag (byte 26 bit 2) | DL1 | 6.3 |
| Turbo action bindings in the companion file; settings schema 10 kept | DL2 | 6.2, 6.3, 9 (R30), 11 (question 18) |
| D16 permanent for native-slot REU DMA; Q-BA1 answered no | DL3 | 0.6, 1.8, 1.12, 2.13.1 (CF7), 2.13.3, 9 (R23), 11 (question 25) |
| `$D07A`/`$D07B`/`$D079` kept as U64 turbo switch registers | DL4 | 0.6 (Q11), 1.2, 11 (question 19) |
| A debug session survives a core switch and ends at reboot, golden update and power-off; existing console commands stay ungated | DL5 | 0.6 (Q16), 6.5, 11 (questions 20, 24) |
| `EDGE_STAGE` default 1 | DL6 | 0.6 (Q7), 1.12 (D15), 2.13.1 (CF7), 2.13.2, 9 (R19), 11 (question 9) |
