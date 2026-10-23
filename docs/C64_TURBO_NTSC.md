# NTSC C64 for the CPU-island turbo design (sub-design v2)

Sub-design written on 2026-10-23 for the v2 draft of [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). The current design (v3) merges it; where the two differ, C64_TURBO_DESIGN.md wins. Design stage only: no HDL, firmware, script or build file was changed for it, no build or simulation was run, and nothing here is board-tested. Paths to throwaway session files were removed. `issueN.txt` means a downloaded copy of GideonZ/1541ultimate issue #N with its comments; other bare file names that are not in this repository also name downloaded copies read during the design. Those copies are not kept.

Models:
- `ntsc_plans.py` (MMCM plan search and NTSC constants), kept in [C64_TURBO_MODELS.md](C64_TURBO_MODELS.md).
- Its output, also in [C64_TURBO_MODELS.md](C64_TURBO_MODELS.md).
- AMD XAPP888 v1.8 was read from a downloaded copy (text extracted only), which is not kept.

Tags:
- **[V]** verified in the repository (file:line) or a cited document.
- **[M]** measured by a third party on U64/C64U hardware.
- **[I]** inference or design choice.
- **[O]** open. The experiment that settles it is named.

No hardware support is claimed. The C64 NTSC image has never passed a board test.

---

## 1. Summary

**Recommendation: (a), a separate NTSC image built with NTSC constants.** It is the `ntsc` variant of the existing `c64` package. A runtime PAL/NTSC switch (b) is rejected.

Reasons:
1. The stack already builds NTSC end to end from one generic. This includes create_project, the adapter, VIC, video_sync, TOD, the 1541, audio, the video bridge, geometry, `core_variant_index`, build-input capture, package verification and firmware variants [V, section 2]. NTSC was dropped from packaging in d6ccbb2 as a scope decision, not for a technical fault [V]. Restoring it is mostly build-matrix and packaging work.
2. Each image closes timing at its own clock. PAL keeps its 0.29 ns half-period advantage. NTSC can take an NTSC-only fallback without touching PAL.
3. Changing standard needs a C64 reset anyway. The VIC model changes, and the KERNAL detects PAL/NTSC at boot. The firmware already switches variants with a full PL reload and remembers the choice per profile [V].
4. (b) needs C64 MMCM DRP with fractional-M lock/filter tables, a new clock-change reset sequence, runtime video geometry and larger frame banks at 91% BRAM. Both standards would also have to close at NTSC rates in one image. The only gain is a faster switch.

**NTSC clock plan:** D=5, M=24, VCO 720 MHz, PFD 30 MHz.
- `c64_clk` = /22 = 32.727273 MHz.
- `c64_cpu_clk` = /11 = 65.454545 MHz (period 15.278 ns, half 7.639 ns).
- The c64 clock is exactly 360/11 MHz = 14.318182 MHz × 16/7: 0 ppm against the ideal crystal ratio, +0.21 ppm against `MT_C64_NTSC_HZ` = 32,727,266.
- Legal for -2 under DS187 [V].
- Every exact NTSC plan puts Vivado's 1 ps model on a 0.5 ps tie (30552.5 ps), so E2 decides whether the pair is exactly 2:1. The baseline fix ladder covers both outcomes.

**Timing risk:** NTSC is the tightest target. Out-of-context margin is about +0.9 ns with the duty term, against about +1.2 ns for PAL [I from C64_TURBO_T65_TIMING.md]. In-context placement noise is about ±0.8 ns [V C64_TURBO_IIGS_LESSONS.md].

**NTSC fallback ladder** (changes only the NTSC image):
1. Pblock and the IIgs repair recipe.
2. The `FAST_COLOUR_RAM=0` / `FORWARD_PORT_WRITES=1` generics.
3. F6, `ISLAND_CYCLE=2`: 32 slots per φ2, capped at 32x, caps mask `0x1FFF`.

A lower non-2:1 NTSC multiple is rejected.

**First step:** rebuild today's stock NTSC image (N0). It is the first NTSC board test ever, and the baseline the clocking reviewer asked for.

---

## 2. Verified facts (how PAL/NTSC works today)

### 2.1 Build selection

- [V] `scripts/create_project.tcl:46-64`. `MULTITINI_C64_VIDEO_STANDARD` selects PAL (31.527954 MHz, `c64_ntsc_mode 0`) or NTSC (32.727266 MHz, `c64_ntsc_mode 1`). Other values fail.
- [V] `create_project.tcl:607-609` writes the generic `C64_NTSC_MODE=1'b<0|1>`. `:614-617` prints the standard and the requested clock.
- [V] `create_project.tcl:480-494`. clk_wiz_c64 is given only a requested frequency today, so the wizard picks D/M/O. For PAL it chose D=5, M=43.875, O0=41.75 (−32 ppm) [V C64_TURBO_BUS_CONTRACT.md §5b]. No NTSC plan is on record: no NTSC build is under `build/` [V find].
- [V] `scripts/build_and_export_xsa.tcl:171-172` passes `--c64-video-standard NTSC` to the input capture when the generic is set.
- [V] `scripts/capture_vectrex_build_inputs.py:157` accepts the flag.
- [V] `scripts/package_cpu1_video_test.py:220-221, 248` refuses an image whose capture standard or generic does not match.

### 2.2 Machine RTL

| Item | NTSC handling today | Source |
|---|---|---|
| Adapter | `parameter bit NTSC_MODE`; `CORE_CLOCK_HZ = NTSC_MODE ? MT_C64_NTSC_HZ : MT_C64_PAL_HZ` | [V] multitini_c64_adapter.sv:8, 118; defs_pkg.sv:7-8 |
| FPGA64 | `ntscMode` used only for the VIC model and TOD | [V] fpga64_sid_iec.vhd:80, 720-722, 956-979 (grep) |
| VIC | `mode6569 = not ntscMode`, `mode6567R8 = ntscMode`, `mode6567old = 0`. 6567R8: 65 cycles, 263 lines; sprite 0 BA at cycle 56, AEC at 59 | [V] fpga64:720-722; video_vicII_656x.vhd:46-49, 365-402; C64_RAM_CPU.md:118-124, 241-244 |
| Native phases | 32 phases per native cycle in both standards. `ntscMode` does not touch `sysCycle` | [V] grep: no other `ntscMode` use in fpga64 |
| CIA TOD | Fractional divider from the core clock: NTSC +120 per 32,727,266 clocks (60 Hz), PAL +100 per 31,527,954 (50 Hz). Paused with `sysEnable` | [V] fpga64:956-979 |
| SID | `ce_1m = enableSid`, one per native cycle: 1.0227 MHz NTSC | [V] fpga64:828 |
| Audio | 48 kHz phase accumulator against `CORE_CLOCK_HZ` | [V] adapter.sv:659-674 |
| 1541 | 16 MHz enable from a fractional accumulator against `CORE_CLOCK_HZ`, so the drive runs at 1 MHz in both standards | [V] multitini_c64_1541.sv:213-229 |
| IEC guard | `CORE_CLOCK_HZ` parameter, 0.5 s hold | [V] multitini_c64_cpu_turbo_guard.sv:11-12; adapter.sv:556 |
| Run injector | cycle count, about 39 ms NTSC | [V] multitini_c64_run_injector.sv:7 |
| REU and HP2 | HP2 ACLK = `c64_clk` for C64 | [V] create_project.tcl:555-556. NTSC DDR bench: `tb_c64_ddr_ntsc` (CORE_HALF 15.2775) [V tb_c64_ddr.sv:549; test_c64_ddr.py:58] |

### 2.3 Video path

- [V] `video_sync.vhd:80-96`. NTSC sync and blanking: active dots 112-516 (404 pixels), lines 13-262 (250 lines).
- [V] `multitini_c64_video_bridge.sv:46-53`. `NTSC_MODE` selects SOURCE 404×250 and offsets 152/40. The bridge is a whole-frame ring: it repeats a frame when no new one is ready.
- [V] `multitini_one_top.sv:671-679, 2064-2067`. Capture and display geometry take 404×250 under `C64_NTSC_MODE`. `:1176-1199, 1270-1271` pass `C64_NTSC_MODE` to the adapter and bridge.
- [V] `multitini_display_geometry.sv:46-60`. C64 Auto uses fixed multipliers per output mode (4/2/2/3/2/3). For NTSC every Auto size fits without cropping:

  | Output | Auto | NTSC size |
  |---|---|---|
  | 1920×1080 | 4× | 1616×1000 |
  | 1024×768, 1200×800, 1360×768 | 2× | 808×500 |
  | 1280×1024, 1680×1050 | 3× | 1212×750 |

- [V] DVI output is always 1080p60 at 148.5 MHz and 2200×1125, whatever the source rate (CPU1_VIDEO_EFFECTS.md:18-21).
- [V] `tb_c64_ntsc_video_bridge.sv:319` passes "404x250 NTSC capture, centered 4x scan, 59.8-to-60 Hz repeat, and vblank ownership". It instantiates the bridge with `DYNAMIC_DISPLAY` at its default 0 (:38). Production uses 1 (top.sv:1271). **Gap.**
- [I] The 59.826 Hz source on a 60.000 Hz output repeats one frame every 5.75 s.

### 2.4 Firmware and packaging

- [V] `multitini_one_top.sv:739-741`: `core_variant_index` = 1 for C64 NTSC.
- [V] Firmware variants:
  - persisted per profile: main.c:2213-2262;
  - up to eight variants per package: MULTICORE_RUNTIME.md:90;
  - host tests already use `c64` variants `pal`/`ntsc`: test_core_package.c:16-17; test_menu.c:48-50; test_supervisor_settings.c:410.
  - No C64 firmware code reads the variant or `MT_C64_NTSC_HZ` (grep).
- [V] `scripts/build_phos1.py:18-19` MATRIX has `c64-pal` only. `:132` forces `MULTITINI_C64_VIDEO_STANDARD = "PAL"`. `:161` inspects without `ntsc`.
- [V] `scripts/package_phos1.py:83-85` verifies every MATRIX image with no `ntsc` argument. `:264` lists only `("pal", "PAL", "c64-pal")`. `:313` requires every variant of a core to have the same ABI.
- [V] `scripts/package_firmware_release.py:26` maps `"c64": {"pal": "c64-pal"}`. `core_artifacts.py:200-205` already takes `ntsc`.
- [V] d6ccbb2 ("Ship one PAL C64 variant in the VID4 bundle") removed `c64-ntsc` from the package scripts and the plan. CPU1_VIDEO_EFFECTS.md:376-379: "The NTSC attempt is excluded from the package and is no longer a release requirement."
- [V] `MULTITINI_ONE_PLAN.md:6` lists the image set without C64 NTSC. VIDEO_SETTINGS.md:35-37 says one C64 variant, PAL.

### 2.5 History and timing evidence

- [V] Stock NTSC images closed earlier, all before the HP2/REU-DDR change:

  | Build | Setup | Hold | Source |
  |---|---|---|---|
  | 20260915 | +0.338 | +0.048 | C64_LAUNCH_20260915.md:87 |
  | VID2 | +0.031 | +0.035 | CPU1_VIDEO_EFFECTS.md:227 |
  | VID3 | +0.171 | +0.030 | CPU1_VIDEO_EFFECTS.md:286 |

  "Earlier failed NTSC attempts are archived" (:301).
- [V] No NTSC board result exists. HARDWARE_RESULTS.md:180 and :276 list NTSC as not tested. C64_LAUNCH_20260915.md:113-115 asked for NTSC tests and records no result.
- [V] Today's PAL c64 domain: WNS +7.052 ns at a 31.716 ns modelled period. The image limiter is the video clock, +0.133 (build/c705r2/c64-pal/reports/timing_summary.rpt:206-207).

### 2.6 Existing NTSC simulation coverage

[V] All of these run PAL and NTSC:
- `test_c64_vice_vic.py` (:81, :193);
- `test_c64_mame_vic.py` (:266);
- machine schedule, 12 PAL/NTSC cases (tb_c64_machine_schedule.sv:319-325);
- interrupts (tb_c64_machine_interrupts.sv:256-263);
- REU machine (tb_c64_reu_machine.sv:351-354);
- REU native (test_c64_reu_native.py:68-88);
- DDR (test_c64_ddr.py:58);
- TOD pause (tb_c64_tod_pause.vhd:84);
- the NTSC video bridge (run_hdl_tests.sh:63-67).

[V] MAME VIC ownership: all 24 PAL/NTSC scenarios agree, with zero differences over 441,012 native cycles (C64_RAM_CPU.md:227).

### 2.7 Clocking references

- [V] FCLK2 = 33.333333 MHz × 36 / 4 / 2 = 150.000 MHz (PS7 xci: `PCW_IOPLL_CTRL_FBDIV` 36, divisors 4 and 2, `PCW_ACT_FPGA2_PERIPHERAL_FREQMHZ` 150.000000).
- [V] Vivado's master `clk_fpga_2` is 6.666 ns. Generated MMCM clocks are edges {1 2 3} with 1 ps shifts (clocks.rpt:28, 33, 69-74).
- [V] The video clk_wiz already uses AXI dynamic reconfiguration (create_project.tcl:433-443, `USE_DYN_RECONFIG`; clocks.rpt:35, `CLK_CORE_DRP_I`). It is timed only at 148.5 MHz and run slower in other modes.
- [V] DS187 v1.21 Table 72, -2 grade:
  - VCO 600-1440 MHz; PFD 10-500 MHz;
  - `MMCM_TOUTDUTY` 0.20 ns, including the BUFG;
  - static phase offset 0.12 ns; lock time at most 100 µs.

  The text extraction is column-shifted. These readings match the clocking reviewer's (v2 clocking review record, not kept).
- [V] XAPP888 v1.8:
  - The MMCM "must be held in reset during dynamic reconfiguration or must be reset after" (p.1).
  - The EDGE bit "adds half a VCO clock cycle to the High Time". An odd divide gets a 50/50 duty cycle (p.2).
  - Lock and filter settings come from lookup tables indexed by CLKFBOUT_MULT (p.3).
  - Fractional CLKOUT0/CLKFBOUT use 0.125 steps (p.3).
- [V] The U64 switches System Mode (PAL/NTSC/PAL-60/NTSC-50) at runtime by reprogramming its C64 PLL, which is also the HDMI reference (1541ultimate d43546f `u64_config.cc:275, 348, 1107-1152`; local copy `design/u64cfg_head.cc`).

### 2.8 Third-party measurements on NTSC hardware

[M] Most U64/C64U references are NTSC:

| Reference | Device | Result |
|---|---|---|
| gist874 `timebase.tas` | U64E fw 3.15 and C64U fw 1.1.0 | 100 frames = 1,709,472 and 1,709,476 CIA ticks (6567R8 prediction 1,709,500) |
| gist874 `cpu_rate.tas` | same | labels exact to index 13; 47.00 at 14 and 63.03 at 15 on the C64U ("NTSC only", gist874 README) |
| gist839 `meas_c` | U64E | "Our runs were NTSC at 1.0227 MHz PHI2" (README:99, 111) |
| #839 DMA | U64E | issue839.txt:30 |
| Amiga64 CIA/TOD | U64E | 17095.0 CIA ticks per frame at 1 and 48 MHz; TOD 41-42 tenths per 250 frames (amiga64_HARDWARE.md:655-700) |
| Amiga64 M5 per-read costs | C64U fw 1.1.0 | :1295-1310 |

[M] PAL references: #203 pixel-level `$D020` (issue203.txt:14), the xahmol speed probe (xahmol_speed_probe.c:2; TURBOCONTROL_MANUAL.md:320-322), and the #665 reporter.

---

## 3. Design

### 3.1 Decision: separate image (a), not a runtime switch (b)

| Criterion | (a) Separate NTSC image | (b) Runtime switch (MMCM DRP) |
|---|---|---|
| Existing support | Complete build-time path (section 2) | None for C64. Video DRP is a precedent only |
| Clock change | None at run time. Each image has a pinned plan | DRP rewrites DIVCLK 5↔9, CLKFBOUT 24↔56.75 (fractional), CLKOUT0 22↔30, CLKOUT1 11↔15, and the lock and filter groups. MMCM reset and relock, ≤100 µs [V XAPP888; DS187]. Outputs re-align from one VCO [I] |
| Reset sequencing | Existing PCAP core change | New. Hold the core in reset, drain HP2 (its ACLK is `c64_clk`), DRP, relock; `reset_c64` drops on lock loss [I]; then re-arm the bridge and mailboxes. A new clock/reset review item (AGENTS.md) |
| Timing | Each image at its own clocks. PAL keeps 7.929 ns halves | One image must close at NTSC rates (7.639 ns halves), so PAL users carry NTSC's margin |
| Video | Elaboration-time geometry (top.sv:671-679; geometry.sv evaluates "at elaboration" to avoid carry chains) | Runtime WIDTH/HEIGHT and offsets. Frame banks sized 404×270 (+~2.5 RAMB36 at 127.5/140) [I] |
| Other constants | Parameters | `CORE_CLOCK_HZ` users (audio, 1541, guard, reset window) become runtime muxes [I] |
| Fallbacks | NTSC-only F6 possible | One structure for both |
| Verification | NTSC runs of the same benches | Plus switch sequencing and both geometries in one image |
| User effect | Variant switch = PL reload (existing). Standard change needs a C64 reset anyway | Faster switch. Still a C64 reset |
| Release cost | +1 Vivado run and +1 timing signoff per release. Both variants rebuilt together (same ABI, package_phos1.py:313) | One image |

**Verdict: (a)** [I]. The U64 switches at run time because it has no full-PL reload path; Multitini has one.

### 3.2 What the NTSC image is

- Same RTL and the same generics as PAL, except:
  - `C64_NTSC_MODE=1'b1`;
  - the NTSC MMCM plan;
  - optionally `C64_ISLAND_CYCLE=2` (F6) if NTSC fails 64x.
- Package `c64`, variant `ntsc`, label "NTSC" (Genesis already uses "NTSC"/"PAL"), file `c64-ntsc.bin`.
- Software-visible turbo behaviour is identical to PAL, with the same registers, menu and speed table.
  - Slot counts per φ2 are the same, so absolute µs figures scale by 985,249/1,022,727 = 0.963 [I].
  - Example: `sta $de00,x / dex / bne` = 2 φ2 per iteration = 1.956 µs NTSC, against 2.030 µs PAL.
- Turbo settings are shared:
  - Per-profile byte 243 belongs to package `c64`, which covers both variants.
  - Global bytes 26-27 are shared by all C64 packages (Q5).
  - Switching standard keeps Turbo Control and speed [V settings layout; I].

### 3.3 NTSC clock plan

**N1 (adopted).** One MMCM gives both clocks: CLKOUT0 for `c64_clk`, CLKOUT1 for `c64_cpu_clk`.

| Item | Value |
|---|---|
| Input | FCLK2 150.000 MHz (PS reference crystal accuracy, [O]) |
| DIVCLK_DIVIDE D | 5 → PFD 30.000 MHz (limits 10-500 MHz) [V] |
| CLKFBOUT_MULT_F M | 24.000 (integer) → VCO 720.000 MHz (limits 600-1440 MHz) [V] |
| CLKOUT0_DIVIDE_F | 22 → `c64_clk` 32.727273 MHz, period 30.5556 ns |
| CLKOUT1_DIVIDE | 11 → `c64_cpu_clk` 65.454545 MHz, period 15.2778 ns, half 7.6389 ns |
| Odd /11 duty | EDGE bit: high 5.5 / low 5.5 VCO cycles [V XAPP888 p.2]. Silicon duty precision ±0.20 ns [V DS187] |
| Error | 0.000 ppm vs 360/11 MHz (= 315/88 MHz × 4 × 16/7); +0.206 ppm vs `MT_C64_NTSC_HZ` 32,727,266 [I script] |
| Fast clocks per native cycle | 64.000000 exactly; f = 2p + h as in the baseline |
| Effects of +0.2 ppm | TOD, audio and 1541 accumulators use 32,727,266, so they run 0.2 ppm fast. Negligible; keep the constant (Q-N6) |

**Vivado 1 ps model** [I, `ntsc_plans.py`]:
- Modelled periods are 6.666 × D × O / M. Fast = 15,276.25 ps, which rounds to 15.276 ns. c64 = 30,552.5 ps, exactly half a ps.
- With round-half-even or round-down, c64 = 30.552 = 2 × 15.276: exact, expandable.
- With round-half-up, c64 = 30.553: unexpandable, and every crossing fails visibly.
- **Every exact NTSC plan gives the same tie.** The modelled period depends only on the frequency ratio (D·Oc/M = 4.5833), not on the plan. Changing plans cannot avoid it without leaving 0 ppm.

**Alternatives:**

| Plan | D / M / VCO / PFD | Divides c64/fast | Error | Notes |
|---|---|---|---|---|
| N1 | 5 / 24 / 720 / 30 | 22 / 11 | 0 ppm | Adopted. Highest PFD of the exact plans |
| N2 | 5 / 48 / 1440 / 30 | 44 / 22 | 0 ppm | Even divides (no EDGE bit). VCO exactly at the -2 limit, no margin. Higher VCO may lower jitter [I]. Same ps tie |
| N3 | 11 / 48 / 654.5 / 13.6 | 20 / 10 | 0 ppm | Even divides. Low PFD and low VCO (more jitter likely [I]). Same tie |
| N4 | 6 / 47.125 / 1178.1 / 25 | 36 / 18 | −48.2 ppm | Fractional M. ps-robust (fast 15,276.99 ps). Ladder step 3 |
| N5 | 7 / 48.875 / 1047.3 / 21.4 | 32 / 16 | +46.5 ppm | Fractional M. ps-robust. Alternative step 3 |
| Fast on fractional CLKOUT0 /16.5 | 5 / 36 / 1080 | 33 / 16.5 | 0 ppm | Rejected: fractional output divider on the half-period clock (duty and jitter) [I] |

**Fix ladder** (the baseline's, applied to NTSC):
1. **E2-NTSC** in a scratch project:
   - generate clk_wiz with N1 pinned and read D/M/O back;
   - `report_clocks`: expect fast {0 7.638}, c64 {0 15.276};
   - `report_clock_interaction`: the pair is timed;
   - a fast→c64 probe path has a requirement of 15.276 ns;
   - record DJ and uncertainty for N1 and N2.
2. If the tie resolves to 30.553: redefine the c64 clock under its own name on CLKOUT0 from CLKIN1, `-edges {1 2 3}`, with shifts from the derived fast clock: {0, 11.943, 23.886}, period 30.552. This is a 0.5 ps model change. The Tcl computes the shifts from `get_property PERIOD`, so one script serves both standards.
3. If step 2 is refused: N4. This needs a recheck of TOD/audio drift (−48 ppm, under 0.2 s/hour) and of the video bridge, which already tolerates any source rate.

### 3.4 Timing risk at 65.45 MHz and the NTSC go/no-go

Half-period budget [I; arithmetic from C64_TURBO_T65_TIMING.md §2]:

| | PAL 63.056 MHz | NTSC 65.455 MHz |
|---|---|---|
| Half period | 7.929 ns | 7.639 ns |
| Less duty precision 0.20 ns and MMCM uncertainty ~0.10 ns | 7.63 ns | 7.34 ns |
| OOC path, EXTRAS + split RAM, registered enable. Derived from +0.052 ns at 75 MHz with 0.185 ns uncertainty | 6.43 ns | 6.43 ns |
| **OOC margin** | **+1.20 ns** | **+0.91 ns** |
| Real island additions: class-dependent Enable on about 200 CE pins, WE compares, 6-input DI mux, PORTW mux [I] | −0.3 to −0.8 ns | same |
| Full-image placement noise [V C64_TURBO_IIGS_LESSONS.md §4] | ±0.8 ns | ±0.8 ns |

Other NTSC paths:
- **c64↔fast crossings:** requirement is one fast period, 15.278 ns (−0.58 ns against PAL). The B0 data path (about 8 levels) fits [V baseline 3.2].
- **c64 domain:** 30.556 ns period (−1.16 ns against PAL). Today +7.05 ns at PAL [V], so about +5.9 ns expected [I]. The 24.5 ns T65→cartridge→DI path leaves this domain under `EXTERNAL_CPU`.
- **M6 fast VIC/SID path (Q1):** island request → VIC/SID read mux → c64 register within one fast period, 15.278 ns at NTSC. Expected to fit (mux about 4-5 ns) [I; O E1-M6].
- **HP2:** 32.73 MHz, slower than the IIgs HP2 clock of 57.27 MHz [V create_project.tcl:87, 555-556].

**NTSC go/no-go** (after M0/M1; PAL is decided separately):

| Result | Action |
|---|---|
| E1-NTSC OOC Fmax ≥ 72.0 MHz (1.1 × 65.45) **and** M1-NTSC routed WNS ≥ +0.1 ns with the duty uncertainty | Go at 64x |
| E1-NTSC ≥ 65.45 MHz but M1-NTSC fails | Ladder F1 → F3 → F7 below, then re-run M1-NTSC |
| E1-NTSC < 65.45 MHz, or the ladder fails | F6 (32x cap) for NTSC only |

### 3.5 NTSC fallback ladder (only the NTSC image changes)

| Step | Change | Function | Cost |
|---|---|---|---|
| F1 | Island Pblock (one BRAM column segment, at least 22 RAMB36) + address replication | none | build effort |
| F3 | IIgs repair recipe: place sweep, reroute, post-route phys_opt. Temporary margins cleared before signoff [V C64_TURBO_IIGS_LESSONS.md §4] | none | build effort |
| F7 | NTSC-only generics `FAST_COLOUR_RAM=0` (colour RAM forwarded) and `FORWARD_PORT_WRITES=1` (D11) | identical results; slower colour-RAM fills (7.5x) and `$01`-flipping code | small. Removes the COL input and the PORTW DIA mux from the half-period paths [I, R14] |
| F6 | `ISLAND_CYCLE=2` | 32 slots per φ2. Indices 12-15 deliver 32. Caps mask `0x1FFF`. `$D031` reads back what was written | performance cap only; zero fidelity risk |

F6 detail [I]:
- T65 Enable fires at fast rising edges ending an odd f, which are also c64 rising edges.
- BRAM port A uses the **non-inverted** fast clock with EN only at rising edges ending an even f. These are the c64 *falling* edges.
- Each half (address, then data) gets a full fast period, 15.28 ns, instead of 7.64 ns.
- Every port-A access is still half a fast period (7.64 ns) from every port-B edge, so the UG473 collision argument of baseline 4.2 still holds.
- Grant table rows exist for N ≤ 32. The resume edge moves from f=4 to f=5.
- T4 gate flips for F6 builds: the port-A clock pin is *not* inverted. A bench assertion checks that port-A EN is never high at a fast edge that coincides with a c64 rising edge.

Not used:
- **F4** (T65 `FAST_NMOS_PATHS`) for NTSC only. It would give the two images different CPUs. Use it only if adopted for both images after the full CPU verification (R13).
- **F5** (posted local writes): ordering complexity for about 55x.
- **A lower NTSC multiple with a non-2:1 ratio** (for example 1.5×): it breaks f = 2p + h, the e_15/R proxy edges and the half-period edge separation.
- **Changing `c64_clk`:** NTSC native timing must stay exact.
- **A 1× island on `c64_clk`:** the same 32x cap as F6, but structurally different from PAL.

### 3.6 Firmware behaviour

- No variant plumbing changes. Selection, persistence and auto-boot already handle `pal`/`ntsc` [V].
- The turbo menu reads `C64_CPU_CAPS` from the loaded image:
  - On an F6 NTSC image, the CPU Speed row cycles only the indices allowed by mask `0x1FFF` (`mt_c64_next_speed(index, mask)` in the baseline).
  - The stored global index is not rewritten. Switching back to PAL restores 64.
  - The status suffix shows the cap, for example "(max 32 MHz on this image)". ASCII only (ui.c:9-11).
- Labels stay U64 nominal MHz in both standards. The help text says "multiples of the C64 clock".
- `core_variant_index` (= 1 on NTSC) needs no C64 consumer. The UART `:speed` and status prints may show it.

---

## 4. RTL and script changes (files; none made now)

### 4.1 Build and packaging

| File | Change |
|---|---|
| `scripts/create_project.tcl` (:46-64, :480-494, :607-617) | Per-standard pinned MMCM plan: PAL {9, 56.75, 30, 15}, NTSC {5, 24, 22, 11}. `NUM_OUT_CLKS 2`, `CLKOUT2_REQUESTED_OUT_FREQ` = 2 × `c64_clock_mhz`. New env `MULTITINI_C64_ISLAND_CYCLE` (1 default, 2 = F6; C64 only, validated), emitted as generic `C64_ISLAND_CYCLE` and printed. Keep `c64_clock_mhz` 32.727266 as the request label |
| `scripts/build_and_export_xsa.tcl` (after :229-256, NES gate pattern) | C64 MMCM gate keyed on `C64_NTSC_MODE`: D/M/O0/O1 equal the reviewed values; parent `clk_fpga_2` at 6.666 ns; c64 period − 2 × fast period = 0 ps; fast waveform {0 7.638} (NTSC) / {0 7.929} (PAL); probe path requirement equals one fast period. Port-A clock inversion check depends on `C64_ISLAND_CYCLE`. Print `C64_CLOCK_PAIR_PASS standard=...` |
| `scripts/capture_vectrex_build_inputs.py` (:157) | Record `c64_island_cycle` in `configuration` next to `c64_video_standard` |
| `scripts/package_cpu1_video_test.py` (:213-248) | `verify_hardware(..., ntsc, island_cycle)` checks the generic and the capture field |
| `scripts/build_phos1.py` (:18-19, :132, :140-142, :161) | MATRIX gains `c64-ntsc`. Give each entry a C64 standard (a fourth field or a dict). Set `MULTITINI_C64_VIDEO_STANDARD` per entry instead of the hard-coded "PAL". Pass `--c64-video-standard NTSC` to the capture and `ntsc=True` to `inspect_artifact` |
| `scripts/package_phos1.py` (:83-85, :264, :349) | Pass `ntsc` per MATRIX entry. C64 variants `[("pal","PAL","c64-pal"), ("ntsc","NTSC","c64-ntsc")]`. Release text "C64 PAL/NTSC". The `:313` same-ABI rule stays |
| `scripts/package_core_gs.py` | ABI 24 (baseline) |
| `scripts/package_firmware_release.py` (:26) | `"c64": {"pal": "c64-pal", "ntsc": "c64-ntsc"}` |
| `scripts/core_artifacts.py` | None. `parse_reuse_images` follows MATRIX; `inspect_artifact` has `ntsc` (:200-205) |

### 4.2 HDL

| File | Change |
|---|---|
| `hdl/board/multitini_one_top.sv` | Generic `C64_ISLAND_CYCLE` (1). `C64_CPU_CAPS` index mask = `C64_ISLAND_CYCLE==2 ? 16'h1FFF : 16'hFFFF`. Pass `NTSC_MODE` (already) and `ISLAND_CYCLE` to the adapter |
| `hdl/core/c64/multitini_c64_adapter.sv` | Pass `CORE_CLOCK_HZ` to `multitini_c64_turbo_regs` and `ISLAND_CYCLE` to the island |
| `hdl/core/c64/multitini_c64_turbo_regs.sv` (new, baseline) | `RESET_WINDOW_CYCLES = 2 * CORE_CLOCK_HZ / 32`: 1,970,497 PAL, 2,045,454 NTSC |
| `hdl/core/c64/multitini_c64_cpu_island.sv` (new, baseline) | `ISLAND_CYCLE` generic (1 or 2) with the F6 enable/EN phase rule and N ≤ 32 grant rows. No NTSC-specific logic: the island sees only phases, BA, IRQ and data |
| `hdl/core/c64/multitini_c64_cpu_proxy.sv` (new, baseline) | None for NTSC. With `ISLAND_CYCLE=2`, `RESUME_PHASE`/cutoff use odd-f enable edges |
| Island timing Tcl/XDC (baseline T1) | Duty uncertainty and the conditional c64 redefinition, computed from derived periods. Standard-agnostic |
| Video, VIC, video_sync, TOD, SID, 1541, REU, HP2 path | None [V section 2] |
| `multitini_c64_defs_pkg.sv`, `firmware/include/multitini_c64_interface.h` | None. Keep 32,727,266 (Q-N6) |

### 4.3 Firmware

| File | Change |
|---|---|
| `firmware/common/core_frontend.c`, `firmware/platform/xilinx/main.c` | Caps-mask clamp and display (baseline 6.2) tested against a `0x1FFF` image. No variant code |
| `tools/menu_preview.c` | Capped-row preview |

### 4.4 Docs

| File | Change |
|---|---|
| `MULTITINI_ONE_PLAN.md:6` | Image set gains C64 NTSC |
| `docs/VIDEO_SETTINGS.md:31-37` | Package supplies PAL and NTSC. NTSC Auto sizes (section 2.3) |
| `docs/ARCHITECTURE.md:121` | Clock table: two outputs per standard |
| `docs/VIVADO_HANDOFF.md:125-126, 176-184` | NTSC plan, gates and build steps |
| `docs/C64_RAM_CPU.md`, new `docs/C64_TURBO.md` | NTSC section: plan, gates, F6 status, µs scaling |
| `docs/HARDWARE_RESULTS.md` | NTSC results when they exist |
| CPU1_VIDEO_EFFECTS.md | Historical text unchanged |

---

## 5. Timing and resources

| Item | PAL | NTSC |
|---|---|---|
| `c64_clk` | 31.527778 MHz, 31.718 ns (model 31.715) | 32.727273 MHz, 30.556 ns (model 30.552/30.553, tie) |
| `c64_cpu_clk` | 63.055556 MHz, 15.859 ns, half 7.929 | 65.454545 MHz, 15.278 ns, half 7.639 |
| VCO / PFD | 945.833 / 16.67 MHz (fractional M) | 720 / 30 MHz (integer M) |
| OOC island margin with duty term [I] | +1.20 ns | +0.91 ns |
| BRAM [I] | 127.5 / 140 | same. Frame bank 101,000 px vs 103,140, same RAMB count |
| LUT / FF | baseline (+700 / +450) | same. F6 adds about 20 LUT [I] |
| Frame | 63 × 312 = 19,656 cycles, 50.125 Hz | 65 × 263 = 17,095 cycles, 59.826 Hz |
| Badline share (screen on) | 5.47% | 6.29% |
| Index 15 RAM loop with badline timing on | 59.55x | 59.04x |
| 2.0 s reset window | 1,970,497 native cycles | 2,045,454 native cycles |

Notes:
- PAL VCO/PFD values are from the baseline, not re-derived here.
- Badline and window figures are [I] from `ntsc_plans.py`.

---

## 6. Tests and gates

### 6.1 Before turbo: N0, a stock NTSC baseline

- Build today's tree (ABI 0.23) with `MULTITINI_C64_VIDEO_STANDARD=NTSC`.
- Signoff gates as PAL. Record the wizard's own NTSC plan and ppm (the reviewer's request, item 7 of the v2 clocking review, not kept).
- Board (first NTSC board test):
  - READY screen; stable 1080p60 with one repeat per about 5.75 s;
  - SID pitch; TOD at 60 Hz (CIA TOD against wall time over 10 minutes);
  - 1541 LOAD/SAVE; REU DDR stash/fetch;
  - menu PAL↔NTSC switch, which persists across reboot.

### 6.2 Simulation (all standard-parameterized benches run PAL and NTSC)

| # | Test | Gate |
|---|---|---|
| S1 | Existing NTSC suites with `EXTERNAL_CPU=true`, exact mode: VICE VIC, MAME VIC, machine schedule, interrupts, REU machine/native, DDR, TOD, NTSC video bridge | Unchanged pass counts and summaries. VIC NTSC traces identical. The VICE harness uses VICE's 65-cycle `cycle_tab_ntsc` [V tests/c64_vice/vic_ba_reference.cpp:18, 29] |
| S2 | `tb_c64_exact_lockstep` in NTSC | Consumer-level equality. Includes the NTSC sprite-0 BA at cycle 56 across the line boundary and sprite 3 across cycles 65/1 |
| S3 | `tb_c64_machine_island` NTSC subset: E4 (raster IRQ acks), E5 (REU), E6 (badline + sprites with 64x `$D020`/`$D016` writes), mode-switch fuzz | Same assertions as PAL |
| S4 | M6 fast VIC path, NTSC | `$D012`/`$D011` bit 7 readable at turbo through 0..262; `$D019`/`$D01A` late-ack rule; collision-register read clears once |
| S5 | Two-clock generator at NTSC periods (15.27778 / 7.63889 ns) from one process | Coincident-edge assertion (baseline T7) |
| S6 | `tb_c64_turbo_regs` | Window length per standard (bench uses a scaled parameter; elaboration check of 2,045,454) |
| S7 | Delivered-rate bench (#874 method) | NTSC: CIA ticks per frame = 17,095 at indices 0, 5, 15; delivered N per table |
| S8 | `tb_c64_ntsc_video_bridge` with `DYNAMIC_DISPLAY=1` across the 6 display modes and multipliers 1-4 | No crop under Auto; clipping only for oversized multipliers (closes the gap in 2.3) |
| S9 | F6 builds only: Dormann at `ISLAND_CYCLE=2` (N = 32, 16) | Grant counts for N ≤ 32; port-A EN never high at an edge coincident with a c64 rising edge; collision checks on |
| S10 | Package/host tests | `test_core_package_tool` / `test_core_artifacts` with `c64-ntsc` in MATRIX; menu host test for caps `0x1FFF` clamp and restore on PAL; `test_build_inputs.py` literals for the per-standard MMCM plan |

### 6.3 Build gates (NTSC)

- **E2-NTSC:** section 3.3 step 1.
- **E1-NTSC:** OOC Fmax ≥ 72.0 MHz with 0.20 ns duty uncertainty.
- **M1-NTSC:** routed WNS ≥ +0.1 ns with and without the Pblock.
- **T4** per standard (section 4.1).
- **Release signoff:** `c64-ntsc` row in `timing-audit.json` (setup, hold, pulse width, bus skew, CDC rows = 8 PSRAM waivers).

### 6.4 Board gates (NTSC); the NTSC devices are the direct reference

| Gate | Reference | Multitini NTSC expectation |
|---|---|---|
| gist874 `timebase.tas` | 1,709,472 / 1,709,476 [M]; 6567R8 1,709,500 | 1,709,500 within 100 ticks at indices 0, 5, 15; spread across speeds ≤ 10 |
| gist874 `cpu_rate.tas` | exact to index 13; 47.00 at 14; 63.03 at 15 [M C64U] | same within quantisation. F6 image: 32.00 at indices 12-15 |
| Amiga64 CIA clock | 17,095.0 ticks/frame at 1 and 48 MHz; TOD 41-42 tenths per 250 frames [M U64E] | same at indices 0 and 15 |
| Amiga64 M5 per-read (φ2 ticks) | RAM 4.30 at Off, 0.064 at index 15; `$D419`/`$D41A`/`$D012` 0.080-0.085; `$DC01`/`$DE00` 1.040-1.045 [M C64U] | strict for RAM and CIA/I/O1. VIC/SID at turbo rate (M6, Q1) about 0.08 |
| gist839 `meas_c` | RAM N per φ2; io turbo rate (U64E 48 MHz: 1765 ticks per 16384 reads); cart 1 φ2, 2 at 2-4 MHz unrolled [M U64E] | strict for RAM and cart; io near the U64E value after M6 |
| #874 VIC-on, index 0 | 0.94 [M] | 0.937 (stock exact; 6.29% badline share) |
| xahmol `speed_probe` | PAL: 16 / 21 / 92 lines [M] | NTSC scaled by 63/65: about 15-16 / 20-21 / 89 lines [I] |
| #203 `$D020` at 48 MHz, #665 `$8F` digi | PAL [M] | qualitative on both standards |
| REU rate | C64U flat 1 B/µs [M; standard not recorded] | 1 byte per φ2 = 1.0227 B/µs |
| Stock regression | — | N0 list (6.1), plus disk auto-slow on/off LOAD |

---

## 7. Risks

| # | Risk | Retired by | Fallback |
|---|---|---|---|
| RN1 | NTSC island fails closure (7.639 ns halves, about 0.29 ns behind PAL) | E1-NTSC, M1-NTSC | F1 → F3 → F7 → F6 (section 3.5) |
| RN2 | 0.5 ps tie: Vivado models the pair as 30.553 vs 2 × 15.276, so it is unexpandable | E2-NTSC | c64 redefinition (step 2); then N4 (−48 ppm) |
| RN3 | clk_wiz refuses or overrides the pinned N1 plan (shared with PAL, baseline R8) | E2 | Direct `MMCME2_ADV` outside the BD (baseline R8) |
| RN4 | NTSC has never passed on the board, and HP2/REU DDR at the NTSC clock has never been in a full image | N0 | Fix before turbo work |
| RN5 | Release cost: one more Vivado run and signoff, and both variants must be rebuilt together (same ABI) | — | Accept; artifact reuse when inputs are unchanged |
| RN6 | An F6 NTSC image differs from PAL in island structure, which enlarges the test matrix | S9 | Avoid F6 if F1/F3/F7 close |
| RN7 | PS reference oscillator tolerance sets the absolute NTSC (and PAL) rate, not the MMCM plan | Board frequency check in N0 | None needed |
| RN8 | Programs that detect PAL/NTSC by counting cycles at turbo misdetect (U64 3.14d note) | The 2.0 s exact window covers the KERNAL boot check | Document |
| RN9 | Digital SID filter model at 1.0227 MHz, as inherited from MiSTer, unchanged | — | Pre-existing |

---

## 8. Open questions

1. **Q-N1.** If PAL closes at 64x and NTSC does not, ship NTSC with F6 (32x cap, caps mask `0x1FFF`) or hold NTSC? The recommendation is to ship F6.
2. **Q-N2.** May N0 (one Vivado run of today's stock NTSC image, then a board test) run before turbo M0? It gives the first NTSC board result and the wizard's own NTSC plan.
3. **Q-N3.** Variant label: "NTSC" (matches package_phos1 and Genesis) or "NTSC 60 Hz" (old docs)?
4. **Q-N4.** 6567R56A (old NTSC, 64 cycles × 262 lines; the VIC has `mode6567old`) stays out of scope. It would need new video_sync/bridge geometry.
5. **Q-N5.** What is the board's PS_CLK oscillator tolerance? It bounds the absolute rate of every image.
6. **Q-N6.** Keep `MT_C64_NTSC_HZ` = 32,727,266 (+0.2 ppm vs the plan) or change it to 32,727,273? The recommendation is to keep it, to avoid churn in TOD, audio, 1541 and gates.
7. **[O] E2-NTSC.** Does Vivado round the c64 period to 30.552 or 30.553? Does N2 (VCO 1440) show lower DJ than N1 enough to adopt it?
