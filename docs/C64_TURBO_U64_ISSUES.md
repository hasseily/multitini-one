# U64/C64U issue sweep: design guidance for the Multitini C64 turbo

Issue sweep written on 2026-10-23 for [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). The user's decisions on it are recorded in section 0.6 of the design; where they differ from a recommendation here (for example SuperCPU detection, per-title settings, the Follow CPU settings bit), the design wins. Bare file names that are not in this repository (for example `u64_olderfirmware.txt`, `gnw64_main.asm`, `doom_release_test.py`) name downloaded copies read during the sweep; those copies are not kept.

**Inputs.** Three sweeps fed this result:
- gh-issues: 638 issues, 377 PRs and `ultimate_releases`.
- forums.
- releases and commits.

It also uses 69 per-item assessments.

**Design references.**
- v1 = the first draft of the turbo design (not kept).
- v2 = the second draft (not kept). [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md) (v3) applies this sweep; its section 15 lists where each change landed.
- Section numbers below are v2's. Sections 1.x have the same numbers in v1.

**Markers.** [V] = verified (file:line or URL). [M] = measured or reported by a third party. [I] = inference.

**User rule (binding).** Where U64/C64U users report a real turbo problem, Multitini solves it rather than copying the U64. The fix must keep U64/C64U turbo software working.

---

## 1. Summary

### 1.1 Problems Multitini should solve

| # | Problem (evidence) | Decision |
|---|---|---|
| S1 | Fast loaders, JiffyDOS, GEOS disk access and CMD FD traffic fail at turbo (#737, ultimate_releases#15, Lemon64 t=75567, U64 1.34 known issue). The U64 has its own IEC slowdown (FPGA 1.34, commit 3034a3b4), but it is undocumented and still misses cases | Keep disk auto-slow default On. Reword D3: it is a stricter form of the U64 slowdown, and Off is **below** U64 parity, not equal to it. Forced exact also overrides `$D031` bit 7. Add telemetry that shows when a held line causes the block |
| S2 | Settings silently cap turbo: the C64U Bus Operation Mode turned 64x into about 8x; menu rows are greyed with no reason; software "sets 48 MHz" and nothing happens | Show requested speed, effective speed and the blocking reason. Keep the speed rows editable while Off. Add a sticky hint bit: "software tried to set the speed" |
| S3 | A menu reset in TurboEnable mode makes turbo fast again (#153). A config load rewrites the live speed (#787) | Keep D4. In TurboEnable mode, a menu apply raises live only if live is already non-zero. The apply counter fires only when a value really changes |
| S4 | There is no quick way to change speed or to set it per game (#223, #150, "games too fast") | Add Turbo on/off, Faster and Slower actions. Add a per-title sidecar `.cfg` that reads the U64 3.15 turbo keys |
| S5 | The top two indices deliver one φ2 multiple less than their labels (#874) | Deliver 48x and 64x. Keep 47/63 selectable by a firmware-only bit |
| S6 | Cartridge software now expects turbo (#897: AGI ports on Magic Desk 2) | Move M7 cartridge turbo ahead of M8, limited to the 17 accepted ROM-only types |
| S7 | REU DMA does not scale with turbo (#839, #204, ultimate_releases#62, Lemon64) | Add an opt-in "Expansion memory speed: Follow CPU". The default stays 1 byte per φ2 |
| S8 | Software cannot read the SuperCPU speed state (#666; GEOS mouse "wobble"; `$D0B8` missing). SuperCPU detection depends on speed (#654) | Add read-only `$D0B8`/`$D0B9` and make `$D0BC` speed-independent. Both sit behind SuperCPU Detect, default Disabled |
| S9 | Digis growl at the badline rate with badline timing off (#665) | Already solved by v2 decision Q1, which matches the U64 fix |

### 1.2 U64/C64U behaviour to keep unchanged

- Register semantics, exact decode, readback, the four Turbo Control modes, default Off, and the U64E2/C64U speed labels.
- One access per φ2 for CIA, I/O1/I/O2 and cartridge.
- REU: the CPU halts on the triggering write; the start schedule is unchanged; DMA is 1 byte per φ2 by default.
- No filtering of junk writes to `$D030`/`$D031`/`$D07x` (#287; Dawnfall in #665). Defaults and visibility handle that case.
- `$D07A`/`$D07B`/`$D079` stay live in register modes whatever the SuperCPU Detect setting (Image BBS needs this).
- No `$D032` or model-ID register until upstream defines one (#911). No 65816, no SuperRAM, no faked C128 detection (#301).
- The reset window, live cleared on every reset, TurboEnable booting with live = 0, and writes latched during the window (D4).

### 1.3 Net design changes (apply to v2; also to v1 if v1 is kept)

1. **Top slots** (§0.3, 1.3, 2.7, 7, 8, 6.2 help): index 14 gives N = 48 and index 15 gives N = 64. A hidden config bit restores 47/63. Delete the "Max 64: No" row in §7.
2. **Disk auto-slow** (§1.9, 1.12 D3, 6.2):
   - Correct "Off gives U64 parity" (v1:258, v2:281 [V]).
   - Label Off as risky.
   - Add a STATUS field for the block cause.
   - Make the holdoff programmable, with an experiment to shorten it.
   - Design a static-hold release, but ship it disabled.
3. **Forced exact** (§1.11, 2.11): it runs the stock path whatever `$D031` bit 7 says. No FIO and no I/O-only ownership happen in forced exact.
4. **Apply rules** (§1.5, 6.1): a TurboEnable apply raises live only if live ≠ 0. The apply counter changes only when one of the four turbo settings changes value.
5. **Menu** (§6.1-6.2):
   - effective speed plus reason;
   - two sticky STATUS bits;
   - speed rows editable while Off;
   - toggle, Faster and Slower actions;
   - help texts;
   - rename the SuperCPU option.
6. **New §6.x: per-title sidecar `.cfg`.** Turbo keys only, speed matched by MHz label, not persisted.
7. **M7 earlier** (§1.10, 10): ROM-only mappers first, with snoop and φ2-gating rules and a bank-switch stress gate at every speed.
8. **Expansion memory speed** (§1.8, new D17, new M9):
   - opt-in "Follow CPU" REU DMA;
   - make D16 permanent;
   - close Q-BA1 with "no".
9. **SuperCPU registers** (§1.2, D10, M8):
   - `$D0BC` is visible at any speed, with value $01 unless E7 measures something else;
   - add `$D0B8`/`$D0B9`, with the same gate as `$D0BC`.
10. **NMI** (§2.9): latch NMI edges that arrive during island holds. The design implements this as a hold and rearm, because a plain latch misses a high-then-low after a low sample (C64_TURBO_DESIGN.md §2.9, §14.4).
11. **Unmapped I/O** (§2.5, 2.13, M7): no fast or local path may answer an unmapped address.
12. **Timing revision** (§6.1): report a versioned "turbo timing revision" in CAPS.
13. **Gates** (§10): new gates, listed under each item below.

---

## 2. Issues that matter (high and medium)

"Reproduces?" means: does the v1 or v2 design show the same failure as the U64 report?

| Issue | Problem | Reproduces? v1 / v2 | Multitini solution | Risk for U64 software, and mitigation | Sections |
|---|---|---|---|---|---|
| [#737](https://github.com/GideonZ/1541ultimate/issues/737) (high) | JiffyDOS printing to the U64 virtual IEC printer at 8 MHz with badline timing on loses bytes and lines; GEOS output is corrupt. The stock KERNAL prints correctly [M] | **No** with the guard On. The guard forces exact mode on any IEC or parallel edge from either side, while any line is low, and for 0.5 s after [V multitini_c64_cpu_turbo_guard.sv:37-50, 70-73]. **Yes, and worse than the U64**, with the guard Off | Keep disk auto-slow default On. Label Off. Run the gate set in 3(b) | Low. Turbo software loses speed only during IEC traffic and for 0.5 s after. Register detection is unaffected (§1.11). A timing probe run right after a disk LOAD sees 1x. The U64 already imposes the same kind of 1x window after reset (#874, PR#811). xahmol's probe retries | 1.4, 1.9, 1.12 D3, 6.2, 10 |
| [Lemon64 t=75567](https://www.lemon64.com/forum/viewtopic.php?t=75567), Forum64 104828 (high) | "Custom fast loaders tend to crash" at U64 turbo. On the SuperCPU all fast loaders crashed, and GEOS had to be patched [M] | **No** (guard On) | Same as #737. Gate set at index 15 with the guard On; record the failures with it Off and use them in the help text | Same as #737 | 1.9, 6.2, 10 |
| U64 1.34 known issue [V u64_olderfirmware.txt:217-219], Lemon64 t=76762 (high/medium) | Some loaders corrupt disk access when badlines are disabled and the bus bridge is not "Quiet". GEOS MP3 is affected. No fix note was found up to 3.15 | **No.** There is no bus bridge, and forced exact runs the stock path. The design does not yet *state* that forced exact overrides `$D031` bit 7 | Add to §1.11/2.11: forced exact ignores bit 7 and turns off FIO and I/O-only ownership; the stored bit 7 still reads back. Help text for Off: "disk writes can be corrupted". Gate: GEOS boot and disk copy with `$D031` = `$8F` at index 15 | None; this only clarifies the rule | 1.6, 1.9, 1.11, 2.11, 6.2, 10 |
| [commit 3034a3b4](https://github.com/GideonZ/1541ultimate/commit/3034a3b4) (high) | The U64 FPGA has an undocumented IEC slowdown. Until 1.34 it missed the first transition. It is still incomplete (#737) [V commit message; M #737] | **No.** A C64-side edge trips the guard before the next local cycle [V v2 §1.9]. But the design text "Off gives U64 parity" is wrong [V v1:258, v2:281] | Reword §1.9 and D3 as "a stricter form of the U64's own IEC slowdown". Off label: "Off (disk access may fail)" | None | 1.9, 1.12 D3, 6.2 |
| U64 3.14d notes / [commit 433c53c8](https://github.com/GideonZ/1541ultimate/commit/433c53c8) (high) | With turbo live right after reset, the KERNAL misdetects PAL/NTSC (`$02A6`), and JiffyDOS transfers then fail | **No.** A 2.0 s exact window counted in native cycles (1,970,497 PAL / 2,045,454 NTSC) [V v2 §1.5] | Keep it. Gate on PAL and NTSC images in Manual 64 MHz, Registers and TurboEnable: PEEK(678) must match the image. Then JiffyDOS LOAD/SAVE right after boot (user-supplied ROMs) | None. The window is no longer than the U64's (1 to 2.6 s observed) | 1.5, 10 |
| [PR#811](https://github.com/GideonZ/1541ultimate/pull/811) / [commit 59594060](https://github.com/GideonZ/1541ultimate/commit/59594060) (high) | The CPU kept running at turbo after the `$DF01` write. Its next-transfer register stores corrupted the transfer in progress, and Doom's REU check failed | **No.** The island halts on the trigger (`dma_req` into `ext_dma_hold` at B1), and the REU ignores register writes while active [V reu.sv:155, per assessment] | Keep. The `$FF00` snoop must stay non-posted (§2.13). Gates: `reu_test.tas` at index 15 and index 1 against stock, plus Doom C64U | None | 1.8, 2.13, 5.5, 10 |
| U64 3.15 "REU stale data at turbo" [V release_3.15.md:189] (high) | Same defect class as above | **No** for data. One timing gap: at turbo, register writes arrive 1 φ2 apart, but the cache is qualified only with at least 4 native cycles between writes [V C64_EXPANSIONS.md:96-101] | State the gap in §1.8. Add E5 cases at index 15 and index 1, with badline timing on and off: back-to-back register writes then trigger; a bank change on the last write; fetch right after stash to the same address; stash right after CPU writes to the source; an `$FF00`-armed trigger. Assert exact bytes and no island enable between trigger and completion | None. A late first byte at turbo cannot break software, because U64 turbo REU start timing is not fixed either | 1.8, 9, 10 |
| [#665](https://github.com/GideonZ/1541ultimate/issues/665) (medium) | With real SIDs fitted, `$8F` behaved like `$0F`, so a timer-driven digi growled at the badline rate. Separately, Dawnfall glitched in Registers mode | **v1: partly** (D5 growl). **v2: no** for the growl (Q1: internal SIDs and the VIC on the fast path; CIA and I/O1/I/O2 get I/O-only ownership in BA-low cycles). Dawnfall-style junk writes still switch turbo, **by design** | Keep v2. Add no register heuristic. Defaults, status display, toggle and per-title settings cover Dawnfall. Gate E-BA2: a `$8F` digi with the screen on must not growl | Low. I/O-only ownership applies only with badline timing disabled. REU DMA keeps the VICE BA rules | 0.1, 1.6, 1.7, 1.12 (D1/D5 retired), 2.13.8, 10 |
| [commit f2a14e51](https://github.com/GideonZ/1541ultimate/commit/f2a14e51) (medium) | U64 external DMA misbehaved during badlines when badline timing was off | **No.** But v2 opens a path stock never sees: a `$DF01`/`$FF00` trigger can land in a BA-low cycle through I/O-only ownership | The badline bit gates only the CPU. Make D16 permanent and close Q-BA1 with "no" (the opt-in RAM side in 3(c) is the exception). Gates E-BA3/E-BA4: trigger at BA-low cycles 1, 3, 5 and 20; fetch, stash, swap and verify across badlines and sprite DMA with `$D031` = `$00`/`$80`/`$0F`/`$8F`; compare with the stock run | Negligible. If the U64 skips badlines at `$8F`, Multitini transfers there are a few % slower | 1.6, 1.8, 1.12 D16, 2.13.8, 10, 11 |
| [#895](https://github.com/GideonZ/1541ultimate/issues/895) / [PR#906](https://github.com/GideonZ/1541ultimate/pull/906) (medium) | Expand hangs at 1 MHz on U64 3.15a: the REU start is one cycle late. Still open | **No.** Expand passes on the board with dev5 [V C64_EXPANSIONS.md:105-108] | No change. Gates: Expand and reudiag `reutiming` with Turbo Off and with Registers, live = 0 (results must be identical), and with the reset window active. At index 15, use an RTL offset assertion instead of screens. Do not require Expand to pass at turbo | None | 2.10, 5.5, 10 |
| [#839](https://github.com/GideonZ/1541ultimate/issues/839) (+ #204, ur#62, Lemon64 p=1099075) (medium) | REU DMA does not scale. The U64E plateaus at about 5.1 B/µs stash and 3.5 B/µs fetch and is not monotonic; the C64U is flat at 1 B/µs [M]. Crypto and big-memory games are bound by it | **Yes** (1 byte per φ2) [V v2 §1.8] | Opt-in "Expansion memory speed: Follow CPU", default C64U rate (see 3(c)) | None by default. With the option on, REU-timed effects that stream into RAM change. I/O-targeted bytes keep φ2 timing, so `$D020`/`$D418` streams are safe. Opt-in only, as #839 commenters asked | 1.8, 1.12 D17, 2.4, 4.1, 6.1-6.3, 7, 10, 11 |
| [#205](https://github.com/GideonZ/1541ultimate/issues/205) / [#304](https://github.com/GideonZ/1541ultimate/issues/304) (medium) | Reads of the empty `$DE00-$DFFF` must return the VIC's last byte (floating bus). U64 turbo broke this until 3.15 (Barry McGuigan, Dig Dug V-MAX) | **No.** I/O1/I/O2 are forwarded, and buslogic returns `lastVicData` when nothing is selected [V multitini_c64_buslogic.vhd:157-163] | Rule: no fast or local path may answer an unmapped address. FIO reads return exactly the native mux value. M7 keeps the rule. Gate at index 15 with no REU and no cartridge: the Barry McGuigan check and georamtest | None. With the REU on, `$DF00` is REU space, as in VICE | 2.5, 2.13, 10 |
| [#897](https://github.com/GideonZ/1541ultimate/issues/897) (medium) | New AGI cartridges in Magic Desk 2 format "support Turbo mode (48/64 Mhz) to work properly". The U64 lacks the mapper | **Partly.** The mapper is accepted (type 85) [V C64_IMPORT.md:183], but D2 forces exact mode whenever a cartridge is loaded | Move M7 ahead of M8 for the accepted ROM-only types. Show "(cartridge: 1 MHz)" until then | Low. This restores U64 parity: the U64 runs turbo with emulated cartridges, with ROM at φ2. Per-title "Cartridge forces 1 MHz" covers mappers that break | 1.4, 1.10, 1.12 D2, 6.2, 10 |
| [#153](https://github.com/GideonZ/1541ultimate/issues/153) (medium) | In TurboEnable mode, a reset or launch from the menu makes turbo fast again. That crashes software that uses `$D030` in only some places | **No.** Live is cleared on reset, and apply fires only on a user change [V v2 §1.5]. Residual: a menu apply in TurboEnable sets live := preference even when software had `$D030` = 0 | A TurboEnable apply raises live only if live ≠ 0 (see 3(e)) | None. Software cannot see menu events | 1.2, 1.5, 6.1 |
| Bus Operation Mode cap ([Lemon64 t=89802](https://www.lemon64.com/forum/viewtopic.php?t=89802), dreisechzig.net) (medium) | 64x was about 8x for months, and the cause was hidden in a menu [M] | **Partly.** There are no bus modes, but D2, the F6 cap, the reset window, the loader and disk auto-slow all lower the speed, with only a planned suffix to show it [V v2 §6.2] | Show requested and effective speed plus the reason, for every block bit and for the F6 cap. Show it on the status line when the menu opens. Log it on UART | None | 1.11 note, 6.1, 6.2 |
| Turbo rows greyed ([Lemon64 t=87886](https://www.lemon64.com/forum/viewtopic.php?t=87886), Lemmings Reloaded on itch.io) (medium) | Speed rows are greyed while Off, with no reason given. Software that sets its own speed silently fails | **Yes.** The rows are "disabled (muted, value shown) when Off" [V v2:1406] | Keep the rows editable while Off: they edit the stored preference. Help text names the mode that software needs. A sticky STATUS bit, "speed register written while not live", drives a status hint. Keep Off as the default | None. Liveness and readback are unchanged | 6.1, 6.2, 10 |
| Games too fast; per-title speed (Lemon64 t=75247 and t=88691; U64 3.15 per-title `.cfg`) (medium) | Driller, Stunt Car Racer and Hard Drivin' become unplayable at a fixed turbo speed. Users want per-game and on-the-fly speed | **Yes.** The design has no such feature | Sidecar `.cfg` plus actions (see 3(e) and 3(g)) | None. Speed is matched by MHz label, so U64 MK1 and E2 files both work | 6.2, 6.3, new 6.x, 12 |
| Manual 1 MHz ≠ Off ([c64u.org wiki](https://c64u.org/dokuwiki/tips_tricks)) (medium, unverified) | The community wiki says Manual mode at 1 MHz breaks demos. This conflicts with the Amiga64 measurement | **Partly.** By design, index 0 with badline timing enabled takes the exact path, but this is not yet proven | State the guarantee. Add M2 lockstep scenarios: Manual index 0, Registers `$00`, TurboEnable 0, and the transitions in and out of each. Label index 0 with badline timing disabled as "1 MHz, badlines off (not cycle-exact)" | None | 1.3, 2.11, 6.2, 10 M2 |
| "SuperCPU" naming and missing `$D0B8` ([Lemon64 t=87516](https://www.lemon64.com/forum/viewtopic.php?t=87516&start=285), 8bitboyz, #666, #654) (medium) | 65816 software does not run. GEOS SCPU code reads `$D0B8` to decide whether to slow down and restore; on a U64 it reads `$FF`. Image BBS cannot read the state | **Yes.** `$D0B8` is missing and `$D0BC` is deferred | See 3(f) | Low. Default Disabled means nothing changes for U64 software | 1.1, 1.2, 1.12 D10, 6.2, 10 M8 |

---

## 3. Recommendations

### (a) Top indices: deliver 48 and 64

**Decision.**
- Index 14 grants N = 48 slots per φ2 and index 15 grants N = 64, matching the labels.
- Keep the two C64U rows (47, 63) in `TABLE`, selected by a firmware-only `C64_CPU_CONFIG` bit. The bit has no menu row; it serves a board A/B test and the fallback.

**Why.**
- [V C64_TURBO_FAST_VIC.md:414] Multitini's VIC fetches through main-RAM port B and never uses island slots. The gap at f = 4 exists only to copy the C64U's 63.
- [M #874] Both the U64E and the C64U deliver one multiple less than labelled. The reporter says the loss matters for long runs. GideonZ closed the issue as an observation, not as a reason.
- Removing the gap costs nothing.
- v2 §7 rejected "Max 64" only because spec F11 says "C64U table only" [V v2:1452]. For this item, the user rule supersedes F11.

**Changes.**
- §1.3 row: N = 48 and 64.
- §2.7: rows 14 and 15. The forward-completion enable at f = 4 uses that slot, and `grant_q` is already one deep.
- §0.3 and §8: 64.0x RAM; about 60.5x PAL and 59.9x NTSC with the screen on and badline timing enabled [I: 59.6 × 64/63, 59.0 × 64/63].
- §6.2 help text: "48 and 64 MHz give 48x and 64x".
- Delete the §7 "Max 64" row.

**Gates.**
- gist874 `cpu_rate.tas`, screen blanked: delivered rate = label at every index.
- xahmol probe: still classifies correctly (per the #874 assessment: 16.1 and 21.4 lines against thresholds below 19 and 19-25).
- The hidden bit gives 47/63 exactly.

**Risk and mitigation.**
- Software gets +2.1% at index 14 and +1.6% at index 15. No known software calibrates to exactly 63x.
- U64-turbo cycle-exact demos are already unsupported (D8, D14).
- If a board gate finds a dependency, the hidden bit restores 47/63.

### (b) Disk auto-slow (the IEC guard)

**Decision 1: keep default On.**
- Correct §1.9 and D3. The U64 has an IEC slowdown of its own, undocumented, since FPGA 1.34 [V commit 3034a3b4], and it still fails #737 [M].
- So Multitini's guard is a stricter form of U64 behaviour, and Off sits *below* U64 parity.
- Menu values: "On" / "Off (disk access may fail)".
- Help text for Off: "Fast loaders, JiffyDOS and GEOS may crash, and disk writes can be corrupted."

**Decision 2: forced exact overrides `$D031` bit 7** (U64 1.34 known issue).
- While any forced-exact cause is active (guard, reset window, cartridge, loader), the island runs the stock path. There is no BA-ignoring local cycle, no FIO access and no I/O-only ownership.
- The stored bit 7 still reads back (§1.11).

**Decision 3: no device-address filtering. Printer device 4 needs no special case.**
- [V] Multitini has no virtual printer and no physical IEC port.
- Device 4 traffic is ATN + LISTEN 4 with nobody answering. It trips the guard briefly, and the KERNAL times out with "device not present".
- If a virtual printer is added later, its traffic is IEC and the guard covers it.
- User-port printing is not IEC and needs no guard: #737 says it printed correctly [M].
- A device filter would add decode logic and risk for no benefit, because all IEC devices on Multitini have authentic timing.

**Decision 4: holdoff stays 0.5 s for now. Make it programmable and run a shortening experiment.**
- Replace the `IEC_HOLDOFF_CYCLES` parameter [V turbo_guard.sv:12] with a `C64_CPU_CONFIG` field. Values: 500, 100, 50 and 20 ms, in native cycles.
- The 0.5 s value was copied from MiSTer c64.sv:1170-1188, not derived (per the fast-loader assessment). MEGA65 uses about 1 ms.
- Experiment E-IEC: run the full gate set below at each shorter value.
- Adopt a shorter default only if the whole gate set also passes at half that value (a 2× margin).

**Decision 5: reject "an explicit `$D031`/`$D030`/`$D07x` write ends the holdoff".** [I]
- Main-loop loaders (Bitfire style) run while IRQ code may write the turbo registers every frame. The 2026 RoF and Koronis patches toggle `$D030` outside the visible frame [M dreisechzig].
- A write that lands while a byte with constant data lines is being received would resume turbo mid-protocol.
- What the rule would buy is a speed probe right after a disk load. The U64 already imposes a 1x window of the same kind after reset (#874; PR#811 needed a 5 s settle). U64 software therefore copes already, and xahmol's probe retries.

**Decision 6: software that keeps IEC lines busy.**
- [V turbo_guard.sv:45-50, 70-71] Today the guard blocks for as long as any line is held low. A program or drive code that parks a line low therefore cuts turbo silently and permanently.
- Step 1, now: STATUS reports the block cause, one of "line held", "recent edge" or "parallel strobe". The menu shows "(disk: line held)" or "(disk: 1 MHz)". This applies the Bus Operation Mode lesson (S2).
- Step 2, designed but shipped disabled: a static-hold release.
  - If no IEC or parallel signal has changed for 2.0 s (native cycles), the "line held" condition stops blocking.
  - Any edge re-arms the block and reloads the holdoff.
- Safety argument [I]:
  - every IEC handshake step starts with an edge, and every edge re-trips the guard;
  - after the timeout, the guard is therefore exactly as safe as today's "lines idle" state, where turbo already runs until the next edge.
- Turn it on by default only if both conditions hold:
  - board telemetry shows real titles stuck in "line held";
  - the gate set passes with the release enabled.
- I do not know whether the KERNAL or common loaders leave a line low when idle. That is why telemetry comes first.
- Disk-streaming titles (constant traffic) stay at 1x while streaming, by design. The per-title sidecar can turn the guard off for a title the user has verified.

**Decision 7: cassette.** When TAP support lands, add motor and sense to the guard. `cass_motor` is unconnected today (§2.10).

**Gates** (index 15, plus index 6 for the #737 loop; PAL and NTSC; guard On):
- KERNAL LOAD/SAVE/VERIFY.
- The #737 BASIC print loop, sent to a file on drive 8, because there is no printer device.
- The c64fast ROM pair over the parallel cable.
- JiffyDOS LOAD/SAVE right after boot, with user-supplied ROMs.
- Demos with IRQ loaders (Krill, Bitfire, Spindle), and Ultiverzum (Manual 48 MHz, Bitfire).
- GEOS boot, disk copy and desktop copy, with `$D031` = `$0F` and `$8F`.
- A turbo program that rewrites the `$DD00` bank bits every frame, with the IEC bits unchanged (turbo must stay on) and with them toggled (turbo must drop).
- A drive-started edge during a long local loop.
- A timing probe started by D64 autostart; record how long it reads 1x.
- Repeat the set with the guard Off and record the failures for the help text.

### (c) REU DMA rate under turbo

**Decision.**
- The default stays at 1 byte per φ2, which is C64U parity. Doom budgets for 1 byte/µs, and Expand's REU timing must not move.
- Add an opt-in menu option, "Expansion memory speed": "C64U (1 byte per cycle)" (default) or "Follow CPU".
- Persist it in global byte 26 bit 2. Allow it in per-title sidecars.
- Do **not** claim a `$D031` bit: the U64 may assign bits 6-4 later [V #911, VanessaE's proposal of value $10].
- Schedule it as a new milestone M9, after the first release.

**Follow CPU rules** [I].
- Active only while the effective mode is turbo, never in forced exact. The REU is off anyway while a cartridge is loaded [V v2 §1.10, adapter:427].
- Per byte, the fast path applies only when the C64 address auto-increments (`$DF0A` bit 7 = 0) and decodes to RAM (or ROM, for stash) in the island map.
- Bytes to I/O or colour RAM, and fixed-address transfers, stay on the native φ2 path with the VICE BA rules. So `$D020`/`$D418` streams and digis are unchanged.
- Rate: at most one byte per granted CPU slot at the live index ("the REU follows the clock", #839). Swap uses two accesses. The cache and DDR may stall it, and that is the only variable.
- RAM-side bytes use island port A, which is idle because the CPU is halted.
- With badline timing enabled, fast DMA pauses while BA is low, as the CPU does. With it disabled, fast DMA continues, because it never touches the shared bus.
- The halt on the trigger and the first-byte schedule are as in the default.
- End state must be identical: registers, status, IRQ, final addresses and length, and the post-transfer BA states.

**Prerequisites.**
- Experiment E-REU-S measures sequential DDR/cache streaming. The target is at least 8 B/µs, the rate the #839 reporter calls enough.
- [I, assessment] Fetch needs at least 2 cache lines in flight. Stash needs bursts longer than the 8-byte combine buffers, or it caps near 4 B/µs.
- The REU-to-port-A path is a new crossing (c64_clk to c64_cpu_clk, related 2:1). Review it per AGENTS.md.

**Risk.** None by default. With the option on, REU-timed RAM effects (the Expand font test) change, and that is why the option is opt-in.

### (d) GeoRAM

**Fact.** [V] Multitini has no GeoRAM. `docs/C64_EXPANSIONS.md:1-3` covers dual SID and the 16 MiB REU only. CRT type 99 is not in the accepted list [V C64_IMPORT.md:166-183]. No plan to add it was found.

**Decision.** No work now. #609 does not apply.

**If GeoRAM is added later:**
- Put it under the same "Expansion memory speed" switch.
- "C64U": window and page registers at φ2.
- "Follow CPU": a 256-byte BRAM page buffer read and written at turbo rate.
  - A `$DFFE`/`$DFFF` write holds the CPU until the page is written back and refilled from DDR.
  - Window accesses have no side effects, so this cannot break C64U software functionally.
- The unmapped part of I/O2 stays forwarded (the #205 rule).

### (e) Turbo state across reset; menu and hotkeys

**Keep D4 unchanged.**
- Every C64 reset (menu, PRG, CRT, D64 autostart, power-on, hotkey) sets live := `$00`.
- Exact mode for 2.0 s, counted in native cycles.
- Writes during the window are latched.
- TurboEnable boots with live = 0.
- This covers #153, the 3.14d JiffyDOS case and the #874 finding that writes are lost for about 2 s.

**Change 1: TurboEnable apply (#153 residual).**
- In TurboEnable mode, a menu apply updates the preference. Live becomes the preference only if live ≠ 0.
- Entering TurboEnable from another mode leaves live = 0.
- Registers mode is unchanged: apply sets live := preference ("uses settings from U64", turbo_mode.rst).

**Change 2: apply counter (#787).**
- It increments only when Turbo Control, CPU Speed, Badline Timing or SCPU Detect actually changes value. The source does not matter: menu, toggle, hotkey, profile switch (byte 243) or sidecar.
- It never increments for:
  - boot restore, migration or CRC repair;
  - re-selecting the same value;
  - a change to disk auto-slow, cartridge or expansion speed.
- Host test: each real change produces exactly one apply; everything else produces none.

**Change 3: actions.**
- Add supervisor actions "Turbo on/off", "CPU faster" and "CPU slower". They are unassigned by default and can be bound in Controls like the screenshot actions.
- "Turbo on/off" toggles per-profile byte 243 between Off and the stored mode. Byte 27 keeps the mode and index while Off [V v2 §6.3].
- "Faster" and "Slower" step the Manual preference. "Faster" while Off switches to Manual.
- All three go through the apply path.
- A one-step "Turbo on/off" row also goes into the menu.
- There is no separate "clear turbo" item. Turning turbo Off, or a C64 reset, clears it.

**Change 4: visibility (#150; the S2 lesson).**
- The turbo row and the menu header show: requested speed, live `$D031`, effective speed, and the reason. Examples: "64 MHz, live $8F", "64 MHz, now 1 MHz: disk", "(cartridge: 1 MHz)", "(max 32 MHz on this image)", "reset window".
- Two sticky STATUS bits, both cleared on reset and on apply:
  - "speed changed by software since reset", which exposes the #287/Dawnfall junk-write trap;
  - "speed register written while not live", which drives the hint "Software tried to set the CPU speed; select U64 registers".
- No LED: the board has no free LED (per the #150 assessment, BOARD_FACTS.md).
- Help text is ASCII only [V v2 §6.2, ui.c:9-11].

### (f) SuperCPU detect and toggle semantics

**`$D07A`/`$D07B`/`$D079`: keep as designed.**
- Live in Registers and TurboEnable modes, independent of SuperCPU Detect (U64 parity; Image BBS).
- On a real SuperCPU, `$D079` acts like `$D07B` [V VICE scpu64mem.c store: `case 0xd079: /* same as 0xd07b */`], so its decode is correct.

**`$D0BC` (M8, D10).**
- Visible when SuperCPU Detect = Enabled and Turbo Control ≠ Off, at any speed. This fixes #654's dependence on speed.
- Off stays fully off, as GideonZ requires.
- Value: E7 measures the U64's value. If it is not measured, use `$01`, not the `$00` in v1/v2 §1.2.
  - Bit 7 = 0 is the SuperCPU presence convention.
  - GNW64 compares `$D0BC` with `#$01` [V spec-downloads/gh/code/gnw64_main.asm:123-125].

**`$D0B8` and `$D0B9` (new; read-only; writes ignored).**
- Visible exactly when `$D0BC` is visible; otherwise the VIC's `$FF`.
- Bit 7 = bit 6 = 1 when the *requested* speed is 1x: live index 0 in the register modes, menu index 0 in Manual. Otherwise both bits are 0.
- Bits 5-3 = 0 and bits 2-0 = 1. VICE ORs in `optim & 7`, and `optim` resets to `$C7` [V scpu64mem.c:1520, read path]. So the register reads `$C7` at 1x and `$07` at turbo.
- Forced exact is not reflected (§1.11 rule).
- In Manual mode, `$D07A`/`$D07B` stay ignored (U64 parity). Manual exists to force a speed.

**Why gate on SuperCPU Detect.**
- Nothing on Multitini reads `$D0B8` today:
  - there is no 1351 mouse (pots are tied to `$FF` [V multitini_c64_adapter.sv:536]);
  - there is no modem for Image BBS.
- Gating keeps every default-visible behaviour equal to the U64.
- Revisit when mouse emulation lands. If SuperMouse reads `$D0B8` without checking `$D0BC`, consider ungating it in the register modes.
- The U64-patched SuperMouse variants already work through `$D030`/`$D031` [V gmakeboot64_README.md:15-16].

**Not exposed:**
- `$D0B0-$D0B7`, including the `$D0B5` switch bit, because exposing them widens the SuperCPU illusion and invites 65816 code;
- `$D07E`/`$D07F` (hardware registers);
- SuperCPU RAM at `$D200`.

**Hazard.**
- GEOS SCPU64Patch writes `$D07E`, copies code to `$D2A6-$D2FF`, then calls it with I/O banked in [V gmakeboot64-src.d81, offsets 327780 and 345159].
- On Multitini, as on the U64, that code runs from VIC mirror bytes and crashes.
- Document it. Do not emulate SuperCPU RAM.

**Naming.**
- Menu row: "SuperCPU speed registers".
- Help: "Not a SuperCPU: no 65816 or SuperRAM; SuperCPU-only programs will not run."

### (g) Other high-value items

1. **M7 earlier (#897).**
   - Cartridge turbo for the 17 accepted ROM-only types [V C64_IMPORT.md:166-183].
   - Rules:
     - a type may run at turbo only if its state depends only on ROML/ROMH/I/O1/I/O2 accesses, GAME/EXROM and forwarded vector pulls;
     - types that snoop other CPU cycles force exact (#154, commit 2ce7bb2b);
     - mappers that gate GAME/EXROM with φ2 must present their CPU-half map (#334).
   - [I] Check that capacitor-timed types (Epyx FastLoad, type 10) count native cycles.
   - Gate: a bank-switch stress test at all 16 speeds, modelled on PR#945/#950, plus a Magic Desk 2 bank switch and ROM read at index 15.
2. **Index 0 equals Off** (the wiki claim).
   - With badline timing enabled, index 0 is bit-identical to Off in timing.
   - Readback differs only as §1.2 states: `$D031` live in register modes; `$D030` reads `$FE` in TurboEnable, which cc65 takes for a C128.
   - Proof: M2 lockstep scenarios, plus help text.
3. **Floating bus** (#205/#304).
   - No fast or local path answers an unmapped address.
   - FIO reads equal the native mux value, including VIC unused registers (`$FF`) and SID write-only and mirror reads.
4. **REU start invariants** (PR#811, #895/#906, reudiag).
   - The halt on the trigger stays inside the island and never moves REU or BA timing.
   - The `$FF00` snoop is not posted.
   - `reutiming.prg` runs as a stock gate and is expected to match VICE 3.10: `$D012` = `$33`, DMA at cycle 10, byte 0 at cycle 12.
   - Keep test binaries in `build/` after a licence check.
5. **NMI edge latch** (commit 57bfb741).
   - In the c64 domain, latch any NMI_n falling edge that occurs while the island has no enable (forward wait, BA halt, DMA_WAIT, pause).
   - Present it at the next enable, then clear it.
   - Exact mode is unchanged. Bench: a 1-cycle NMI pulse during a BA halt and during DMA_WAIT.
   - Design note (C64_TURBO_DESIGN.md §2.9, §14.4): a plain latch shows the T65 0 at the next enable, so it misses a high-then-low when the T65's last sample was low, for example a CIA2 acknowledge followed by a BA halt and a second CIA2 event. The design holds the fall and, in that case, re-arms it (shows 1, then 0). Its benches add that case, a fall on an enable's clock and mode switches.
6. **Turbo timing revision** (CSDb 243862: turbo demos broke between U64 firmware versions).
   - Version the grant table, cutoff, resume phases and FIO constants, and report the revision in CAPS (not visible to the C64).
   - A regression pins delivered slots and phases, and fails on any change that does not bump the revision.
   - User docs say that turbo is not cycle-exact to the U64.
7. **SID edge stage** (#650; v2 Q-S1).
   - Keep `EDGE_STAGE` = 1 even if E7-S shows that the U64 drops sub-µs gate/test pulses. Under the user rule, dropping them is a U64 imperfection. The stage keeps isolated pulses (at most one per control register per 2 µs); denser sequences are compressed (C64_TURBO_DESIGN.md §2.13.7).
   - Switch to 0 only if U64-targeted software is found that depends on the drop.
   - Add a gate: an OSC3 read followed by a `$D418` write less than 1 µs later, at index 15, behaves as at 1 MHz.
8. **RMW and indirect speed** (C64U Freescape about 8x claim, unverified).
   - The design gives every bus cycle one slot.
   - Add synthmark "ram rmw", "zp rmw" and "ram move" and a `(zp),Y` matrix loop as acceptance tests: about 64x at index 15 with the screen blanked (after (a)).
   - Add RMW on I/O (`inc $d020`, `lsr $d019`).
9. **Firmware time base** (commit 21adfd11). Firmware that touches C64 I/O must use wall-clock waits and force exact mode; it must never count C64 cycles as delays.
10. **Help texts.**
    - TurboEnable: "For C128-aware games; try CPU speed 2 MHz for titles that time border code for a C128."
    - Keep SuperCPU Detect default Disabled: SMB64 took its SuperCPU path when detection was on.
    - Reject the proposed "TurboEnable boots at preference" mode, which would bring back #153-style crashes. Users who want stock software fast use Manual.
11. **Per-title sidecar** (new §6.x).
    - When the content loader starts a D64/G64/PRG/CRT, it looks for a matching `.cfg` next to it.
    - It applies only these keys:
      - Turbo Control, accepting both "U64 Turbo Registers" and "C64U Turbo Registers" [V doom_release_test.py:35-41];
      - CPU Speed, matched by MHz label (" 48" → index 14);
      - Badline Timing;
      - SuperCPU Detect;
      - and the Multitini keys for disk auto-slow, Cartridge forces 1 MHz and Expansion memory speed.
    - Unknown keys are ignored.
    - It applies before the load's reset, through the value-change apply path, and is not persisted.
    - The next load without a sidecar restores the global settings.
    - Add a menu action "Save turbo settings for this title".

### (h) Conflicts between sweep inputs, and how they were resolved

| Topic | Inputs said | Decision | Reason |
|---|---|---|---|
| Top slots | gh-issues: keep 47/63, because #874 was closed as an observation. #874 assessment: 48/64 | 48/64, with a hidden bit | The VIC never uses island slots; the gap was only a copy; matching the label is free |
| `$D0B8` gate | d0b8 assessment: ungated, live with `$D07A`/`$D07B`. #666 and naming assessments: gate on Detect | Gate on Detect | Nothing on Multitini reads it today; this keeps default-visible behaviour equal to the U64; revisit with mouse emulation |
| `$D0B8` bit 6 | VICE: 1x from any source. One assessment: reflect forced exact | Bit 6 = bit 7 (requested speed) | Keeps §1.11: forced exact never changes readback |
| `$D0B8` low bits | 0 or 7 | 7 | VICE ORs in `optim & 7`, reset `$C7` [V] |
| `$D0BC` value | v1/v2: `$00`. #654 assessment: `$01` | E7 value, otherwise `$01` | GNW64 compares with `$01`; bit 7 = 0 is the SuperCPU convention |
| `$D0B5` bit 6 | #654 assessment: read 0. Naming assessment: do not expose | Do not expose | Widens the SuperCPU illusion; no software needing it was named |
| Holdoff ended by a register write | gh-issues proposal | Reject | IRQ code writes turbo registers while main-loop loaders run |
| D16 | v2: provisional, pending E-BA4. f2a14e51 assessment: permanent | Permanent (Q-BA1 = no) | Native-slot DMA cannot skip badlines without taking c-accesses from the VIC |
| Fast REU conditions | Several variants | Merged rule set in 3(c) | — |
| "TurboEnable boots at preference" mode | forums: optional new mode | Reject | Brings back #153 crashes; Manual covers the need |
| GeoRAM, if added | Start at φ2, or a fast window | Under the Expansion memory speed switch | Consistent with the REU option |
| `EDGE_STAGE` | v2: provisional, may follow the U64 (D15) | Keep 1 unless software depends on the drop | User rule |

---

## 4. Issues judged not to matter, not applicable, or user error

### 4.1 Low priority; no new mechanism beyond the items above

- [ultimate_releases#15](https://github.com/GideonZ/ultimate_releases/issues/15): CMD FD-Tools fails to set the RTC at 20 MHz. Multitini has no CMD FD. Supporting evidence for S1 only.
- [#287](https://github.com/GideonZ/1541ultimate/issues/287): Krestology's junk `$D030` writes switch turbo on. User error. No heuristic; defaults plus the sticky status bit cover it.
- [#301](https://github.com/GideonZ/1541ultimate/issues/301): pseudo C128 mode. Not done. E7 measures `$D030` bits 7-1.
- [#150](https://github.com/GideonZ/1541ultimate/issues/150): LED for turbo status. There is no free LED; the status row and sticky bit replace it (e).
- [#223](https://github.com/GideonZ/1541ultimate/issues/223) / ultimate_releases#18: quick toggle. Adopted (e).
- [#874](https://github.com/GideonZ/1541ultimate/issues/874): top indices one short; `$D031` writes lost for about 2 s. Adopted (a); D4 already latches writes.
- [#911](https://github.com/GideonZ/1541ultimate/issues/911): model or speed readback, 77 MHz encoding. Wait for upstream and adopt it exactly. Map a future 77 MHz request to index 15.
- [#654](https://github.com/GideonZ/1541ultimate/issues/654), [#666](https://github.com/GideonZ/1541ultimate/issues/666), forum `$D0B8` item: see (f).
- [#204](https://github.com/GideonZ/1541ultimate/issues/204), [ultimate_releases#62](https://github.com/GideonZ/ultimate_releases/issues/62): covered under #839 (c). Cartridge ROM stays at φ2 after M7, as on the U64.
- [#609](https://github.com/GideonZ/1541ultimate/issues/609): GeoRAM at full speed. No GeoRAM (d).
- [#154](https://github.com/GideonZ/1541ultimate/issues/154), commit 2ce7bb2b: write-snooping BI-80 and the Quiet bus mode. No physical port. M7 snoop rule (g1).
- [#334](https://github.com/GideonZ/1541ultimate/issues/334): BB9 toggles GAME on I/O1 reads; φ2-gated carts. M7 rule (g1).
- [#650](https://github.com/GideonZ/1541ultimate/issues/650): OSC3 read less than 1 µs before a `$D418` write. The edge stage does not cover it (it acts only on the voice control registers); the mechanism is still to be named [O]; one gate (g7).
- Freescape about 8x on the C64U (dreisechzig): unverified and probably confounded by bus mode. Tests only (g8).
- C128-aware games glitch at a fixed speed (Lemon64 t=75247): help text only (g10).
- Turbo demos break between firmware versions (CSDb 243862): timing revision (g6). No claim of U64 cycle parity.
- Commit c4be69a2, Compatibility bus mode: this maps to the M7 "Cartridge forces 1 MHz" option, which applies only while a cartridge is loaded, not to the whole machine.
- Commit 271f801c, split speed tables: keep the single E2/C64U table.
- reudiag `reutiming`: a gate (g4). GideonZ disputes its analysis.
- [#787](https://github.com/GideonZ/1541ultimate/issues/787): config load re-applied turbo. Value-change rule (e).
- U64 3.15 per-title `.cfg`: adopted as a sidecar (g11).
- Commit 57bfb741, NMI pulse lost in a badline: NMI edge latch (g5).
- Commit 21adfd11, firmware SID detection timed by C64 cycles: firmware rule (g9).
- 3.8 turbo UX rework (97df0810): already copied.
- Commit fa3abc4e, U64E2 CIA timing: not caused by turbo. Stock CIA gates only.

### 4.2 Not applicable, explained, or user error

- [#203](https://github.com/GideonZ/1541ultimate/issues/203): pixel pattern at 48 MHz. Explained (VIC slot) and cosmetic. Becomes regular at N = 48.
- [#279](https://github.com/GideonZ/1541ultimate/issues/279), [#39](https://github.com/GideonZ/1541ultimate/issues/39), [#431](https://github.com/GideonZ/1541ultimate/issues/431), changes_3.11 Issue-279: C128 2 MHz with UII+ cartridges. No C128; EasyFlash rejected [V C64_IMPORT.md:190].
- [#38](https://github.com/GideonZ/1541ultimate/issues/38): C128 freeze at 2 MHz. No freezers. The Multitini pause is an internal halt, not a bus-pattern freeze.
- [#242](https://github.com/GideonZ/1541ultimate/issues/242): IP parameters at 48 MHz. A UCI software wait bug; no UCI.
- [#710](https://github.com/GideonZ/1541ultimate/issues/710): host DMA halts the CPU. No host streaming. Note: dual-port RAM could make host writes transparent later.
- [#230](https://github.com/GideonZ/1541ultimate/issues/230): fake VIC alias registers. Not turbo, and it conflicts with exact decode.
- [#626](https://github.com/GideonZ/1541ultimate/issues/626): Image BBS Swiftlink. No ACIA. The turbo point is handled through (f).
- [#193](https://github.com/GideonZ/1541ultimate/issues/193): HOME works only at 2 MHz or more. Not reproduced upstream.
- [ultimate_releases#61](https://github.com/GideonZ/ultimate_releases/issues/61): bus trace at turbo. No trace stream. Its 47.0x figure is consistent with the MK1 table.
- [#152](https://github.com/GideonZ/1541ultimate/issues/152): Little Computer People. The cause was an RTC failure.
- [#653](https://github.com/GideonZ/1541ultimate/issues/653): UCI Hyperspeed throughput. No UCI.

### 4.3 Excluded after reading (turbo incidental or ruled out)

- **gh-issues:** #159, #160 (SpeedDOS VIA fix), #200, #243, #297, #306, #437, #575, #655, #687 (speed-dependent protocol, "faulty by design"), #697, #744, #807, #866, #889, #932, #952, #977, #995.
- **Commits:** ad89bb4b, 34a02c64, 5f8f64e8, fab83cf4.
- **c52e9244** ("ANE magic constant changed", value not stated) is not turbo-related, but it matters for the stock deviation S1 (ANE `$EE` vs `$EF`). Follow up outside the turbo work.
- **Not assessed:** PR #844 (Magic Desk Plus). Check it at M7. PRs #945/#950 serve only as the model for the M7 stress gate.

---

## 5. Coverage gaps

**Sources not searched or not reachable**
- Reddit (refused by the search tool), the MiSTer forum (403; only summaries), Gideon's Facebook group, Discord, YouTube comments, and Commodore's C64U support channels. Many U64 and C64U reports go there.
- Lemon64 and Forum64 were partly read through Wayback only. Several 2026 Lemon64 threads are cited from search-engine summaries:
  - "Games exclusively for the C64U?" pp.6-7;
  - "Using extended memory in Turbo-mode";
  - "Limiting the Overall Speed";
  - MegaPatch page 2;
  - the 3.15 thread.
- The Forum64 firmware and floppy-speeder threads were not archived.
- PR review threads other than #811 and #906.
- C64U changelogs other than 1.1.0 (403). There are no public 3.12/3.12a or C64U 1.0 notes.

**Facts unknown because the U64 core is private** (only `.sof`/`.bit` binaries and commit messages are public)
- The trigger, duration and badline handling of the U64 IEC slowdown.
- The `$D0BC` value.
- Whether `SPEED_UPDATE` applies in TurboEnable mode.
- Whether REU DMA skips badlines at `$8F` (E-BA4).
- C64U SID write timing (E7-S).
- These need E7 measurements on real hardware, which nobody has scheduled.

**Unresolved claims**
- The c64u.org wiki "Manual 1 MHz ≠ Off" against Amiga64's equal timing.
- The Freescape and Synthmark "RMW slow" numbers, which were not read and may be confounded by bus mode.
- Whether SuperMouse reads `$D0B8` without checking `$D0BC`.
- Whether the KERNAL or common loaders leave an IEC line low when idle; decision 6 in (b) depends on this.
- Whether the 0.5 s holdoff can be shortened (E-IEC).

**No reports found** for printers (except #737), BASIC compilers, freezer cartridges, or tape loading at turbo. The absence may reflect where users post rather than a lack of problems.

**Not tested:** none of the new gates (reutiming, Expand in Registers mode with live = 0, the GEOS copy at `$8F`, the IEC gate set) has run on Multitini. Only the stock Expand result is board-verified [V C64_EXPANSIONS.md:105-108].
