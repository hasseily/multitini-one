# C64 turbo evidence: lessons from the IIgs 57 MHz CPU

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: IIgs 57.27 MHz CPU design: what a 63–66 MHz C64 6510 can reuse

Bottom line: the IIgs runs its 65816 at one bus cycle per 57.27 MHz clock only when the read hits a 64-byte LUTRAM cache (L0) or completes inside the fast domain. Other reads take 2 clocks (L1 BRAM hit), 3 clocks (Mega II slow-RAM BRAM) or about 7–10 clocks (I/O through the native bridge). Its CPU loop path is 17.06 ns over 27 logic levels, and it closed at +0.10 ns only after a placement sweep, a reroute and phys_opt. The same path depth cannot close at 2x C64 (15.858 ns PAL, 15.278 ns NTSC). A fast C64 CPU must have a much shallower read path. The clocking, the native-bridge handshake and the cache, write-staging and decode patterns all carry over.

All IIgs sources used below are unchanged since commit 5e3a20c, which the gs611 build used, so the reports match the tree. My scratch scripts are in (session scratch, not kept) (mmcm.py, mmcm2.py).

## VERIFIED FACTS

### (1) Clocks, and clocks per CPU cycle

- **Primary clock.** The IIgs reuses the clk_wiz_c64 MMCM, set to 57.272728 MHz (scripts/create_project.tcl:83-88, 481-487). Vivado achieves 17.459 ns, 57.278 MHz (gs611 clocks.rpt).
- **HP2 AXI clock.** HP2's ACLK is this machine clock in the C64 and IIgs images only (create_project.tcl:555-556). The IIgs DDR master is a single-clock AXI3 master on core_clk with no crossing (multitini_apple_iigs_ddr_path.sv:5-11). Only the drain status crosses, through cdc_bit_sync (:186-194).
- **14.318 MHz native clock.** It is not a separate clock. It is a BUFGCE gate of the primary clock: `BUFGCE clk14_buffer_i(.I(core_clk),.CE(phase==3))` (multitini_apple_iigs_adapter.sv:40-49).
  - The 2-bit phase counter also makes the CE ticks byte_tick, lookup_tick and request_tick (phases 0/1/2), each marked DIRECT_ENABLE and KEEP (:55-57).
  - The XDC declares it with `create_generated_clock -edges {1 2 9}` (hdl/board/multitini_apple_iigs_timing.xdc:20-25): rising edge on the primary phase-3 edge, high for half a primary period, every 4 periods.
- **One CPU instance, on the primary clock, at every speed.** iigs.sv:2232-2240 has `.CLK(EXTERNAL_CPU ? CPU_CLK : CLK_14M)`, `.CE(EXTERNAL_CPU ? CPU_CE : bus_phi2)`. The adapter passes CPU_CLK=core_clk and CPU_CE=cpu_ce (adapter:646-648). P65C816.sv:144 has `EN = RDY_IN & CE & ~WAI & ~STP`, so cpu_ce is the clock enable for every CPU register.
- **Speed control.** The speed code loads a cooldown counter on each CPU retirement (adapter:309-321). Codes 1–5 load 15/11/7/3/1; code 6 loads 0, meaning no minimum. The CPU still advances only on a cpu_ce edge.
- **Clocks per CPU cycle at speed 6 (57 MHz):**

| Access | Primary clocks | Mechanism |
|---|---|---|
| L0 hit, internal cycle, ZipGS local read | 1 | async L0; internal cycles are `!cpu_valid`; ZipGS reads are `cpu_local_read` |
| L1 hit in the same 8-byte word as the previous access | 1 | registered lookup address already matches |
| L1 hit, other word | 2 | synchronous BRAM lookup |
| E0/E1 slow-RAM (Mega II) read | 3 | iigs.sv:1886-1921 |
| Slow-RAM write | 1 | staged; deferred on a native edge |
| Fast-RAM write | 1 | posted 4-entry buffer |
| L1 miss | ~25+ | DDR round trip; doc estimate, cache bypass mode |
| I/O | ~7–10, plus 4 for two-edge slot devices | native bridge, see (3) |

- **Simulated results (docs/APPLE_IIGS_ACCELERATION.md:640-672).** Stock-to-max ratios: read loop 22.0x (one tick per CPU advance), register-only loop 20x, JSR/PHA/PLA/RTS 15.8x, MVN 12.5x, keyboard poll 12x. The ideal is about 20–22x.
- **Board result (owner-confirmed, M0.5.6).** HDD boot completion: 4.7 s at 57 MHz against 24.6 s stock, about 5.2x end to end.

### (2) How memory reads reach the CPU within a cycle

- **Microcode BRAM, read ahead of time.** mcode.sv:38 holds a 2048x53 microcode ROM as block ROM. mcode.sv:2374-2375 reads it synchronously on the same EN edge that updates IR and STATE. Its address is NextIR/NextState (P65C816.sv:289), so the next cycle's microcode word sits at the BRAM output about 2.1 ns after the CE edge.
- **Combinational address.** A_OUT is combinational from the microcode word and registers, with 24-bit adders (P65C816.sv:858-1001).
- **L0.** `(* ram_style="distributed" *) reg [63:0] l0_data[0:7]` (adapter:216): direct-mapped, 8 lines of 8 bytes, asynchronous read.
  - Tags are 8 registered 18-bit tags compared in parallel with the raw logical CPU address bits [23:6] (adapter:219-227). This keeps MMU decode off the hit path.
  - The L0 hit (adapter:228) drives the last 2:1 stage of the CPU data mux (adapter:275-279). Every other select ahead of it is a register.
  - All valid bits clear on any CPU write cycle, DMA, loader activity, configuration, cache disable, or any native retirement, because I/O can change the memory map (adapter:240-242). Vector pulls bypass L0.
  - L0 is filled from the L1 word on `access_valid && !cache_stall`, not on cpu_ce (adapter:243-246).
- **L1** (multitini_apple_iigs_cache.sv): 32 KiB, 32-byte lines.
  - Data is in BRAM (:65), tags in LUTRAM (:68). In non-DMA mode lookup_tick=1 (adapter:338), so lookup_data and lookup_addr capture the live address on every rising edge (:174-176, :185-189).
  - hit requires the registered lookup_addr to equal the live address (:103, :110). That gives 1 extra clock, except for same-word repeats.
  - Writes go to a retire slot that loads its address and byte every cycle; only rs_valid depends on the retire edge (:215). Behind it is a 4-entry merging write buffer.
- **Slow RAM.** Mega II RAM is a dual-port BRAM (ports/bram.sv:46-69). Port A is on the CPU clock with enable always on; port B is the video port (clk_vid = core_clk, enable = ce_pix).
  - CPU writes are staged and written only on edges that are not video read edges: `slowram_wren_a = slowram_wr_pending & ~NATIVE_EDGE` (iigs.sv:1866). This avoids same-edge port collisions.
  - Reads take 3 clocks so that neither the BRAM output nor MMU decode reaches CPU_DIN (iigs.sv:1886-1918).
- **Edges.** Every IIgs CPU, cache and slow-RAM path uses rising edges of core_clk. There is no falling-edge logic on the CPU path.
- **Pipeline in one primary period.** Microcode BRAM output (registered by the previous CE edge) → combinational address → in parallel: L0 tag compare and LUTRAM read → CPU_DIN → ALU → registers; MMU classification → cpu_ce. The live address also feeds the L1 BRAM address, which is consumed the next cycle.

### (3) I/O and the slow side, seen from the fast CPU

- **Crossing type.** No asynchronous CDC and no FIFOs. Primary and CLK14 are timed as related synchronous clocks, with a single primary period for primary→clk14 paths.
- **Hold and commit.** The adapter holds the CPU bus by keeping CE low.
  - At phase 1 (lookup_tick) it marks the access as native-prepared.
  - At phase 0 it publishes readiness, only after one CLK14 edge has passed with the bus held, so that synchronous-read devices have their output ready.
  - At phase 2 it sets native_commit_q.
  - The phase-3 edge is the CLK14 rise. There the native side commits once (`bus_phi2 = NATIVE_COMMIT`), captures native_read_data and flips native_return_toggle (iigs.sv:3325-3336).
  - The adapter sees the toggle differ from native_return_seen on core_clk and fires cpu_ce (adapter:250-253, 283-311).
  - Slot devices whose output is registered twice wait for a second preparation edge (cpu_native_two_edge, iigs.sv:3377).
- **What the accelerator changes.** While accelerated, it removes the 1 MHz Mega II sync by feeding the upstream clock_divider gated inputs (iigs.sv:3289-3297). Every I/O access still makes exactly one native commit, so side effects are unchanged.
- **Peripheral holds.**
  - The sound GLU busy hold (iigs.sv:3384) exists because without the 1 MHz sync, accesses outran the GLU. GS/OS then livelocked on stale DOC reads.
  - IWM uses captured request/response registers (adapter:166-196). Further holds cover speaker, paddle, counter, IRQ and IWM for software that times itself.
- **Stock mode.** Every cycle takes the native path (`native_path = ... || cpu_speed==0`, adapter:252), and the clock divider receives its original inputs. The CPU still sits on the primary clock and retires on the native response.
- **IRQ.** IRQ goes straight into the CPU from native logic as a timed clk14→primary path (iigs.sv:2199, 2238).
- **Phase-published registers.** Registers published on a specific phase get scoped multicycle constraints:
  - loader config_bytes and phase-0 tuples: 3 setup / 2 hold to clk14 (XDC:40-60);
  - native_stall_q: 3/2 from clk14 (XDC:65-70).
  - CLK14 gating cells carry DONT_TOUCH (XDC:8-11).

### (4) Routed margins and what limited them (gs611 = 4D000611, the latest)

**Final result.** build/gs611/apple-iigs/reports/timing_summary.rpt, after repair:

| Group | Setup | Hold |
|---|---|---|
| Overall | WNS +0.101 ns | WHS +0.053 ns |
| Machine clock (17.459 ns) | +0.101 ns, 24,919 endpoints | +0.064 ns |
| machine → clk14 | +0.154 ns, 6,740 endpoints | +0.379 ns |
| clk14 → machine | +2.543 ns, 10,863 endpoints | |
| clk_fpga_0 | +0.259 ns | |
| video | +0.208 ns | |

**Path to the closing result.** The initial route was -0.813 ns (TNS -540.7, 2,013 failing; initial-diagnosis.json). Recipe:
- p1: place_design ExtraTimingOpt, then phys_opt AggressiveExplore, route Explore, phys_opt. Result -0.370.
- q2: reroute = route_design -unroute, phys_opt AggressiveExplore, route Explore, phys_opt. Result -0.045.
- s5: post-route phys_opt. Result +0.101, 0 failing.

Temporary setup-uncertainty margins (fabric, machine, video, and a machine→clk14 margin given with -from/-to) are used during optimization and cleared before signoff (build/gs611-sweep/place_sweep.tcl, repair_routed.tcl). The fixed two-step recipe in build/gs611-repair alone stopped at -0.209 (final-diagnosis.json). Each sweep stage takes about 8 minutes.

**Worst path, 0.101 ns:** MCode/rom_word_reg_2 (RAMB36) → AddrGen carry chain → L0 tag/address logic → hdd/top_addr → shadow/IO decode → iwm_bus_select → IWM/fast-floppy strobes → zipgs → WOZ track RAM ENARDEN. 26 levels, 16.80 ns, 67.8% routing.

**CPU loop path, 0.107 ns:** rom_word_reg_0 → address → L0 tag compare → CPU DI → DR mux → 16-bit ALU with BCD → Z flag (P_reg[1]/D). 27 levels, 17.06 ns, 71% routing. The microcode BRAM's clock-to-out alone is 2.125 ns.

**History (docs/APPLE_IIGS_ACCELERATION.md and memory notes).**

| Build | Setup slack | How it closed |
|---|---|---|
| 4D000505 | +0.007 | no repair |
| 604 | +0.100 | replication + reroute |
| 605 | +0.008 | no repair |
| 606 | +0.002 | replication + reroute |
| 607 | +0.004 | |
| 609 | +0.027 | reroute; the RTL-tweak variant reached -0.984 and never closed |
| 610 | +0.129 | reroute + crossing margin; the limiter was a shared video DSP path |
| 611 | +0.101 | sweep + reroute + phys_opt |

Recorded lessons:
- Slack swings about ±0.8 ns with placement, so small RTL timing tweaks are noise.
- Fast-floppy enables that depend on the CPU's same-cycle address pull WOZ/IWM logic into the CPU critical paths. This is still an open item.
- cpu_ce must not drive BRAM/LUTRAM write pins or wide CE fan-outs in the same cycle. The first build of the rework failed by -1.782 ns until the retire slot, staged slow-RAM writes and a reordered data mux were added.
- Control-set reduction folded phase CEs into D logic; DIRECT_ENABLE fixed it.

### (6) Resources (xc7z020: 53,200 LUT, 106,400 FF, 140 RAMB36, 220 DSP)

| Image | LUTs | LUTRAM | FFs | Block RAM | DSP |
|---|---|---|---|---|---|
| IIgs gs611 | 37,007 (69.6%) | 1,080 | 25,677 (24.1%) | 124 RAMB36 + 8 RAMB18 = 128 equivalent (91.4%) | 24 |
| C64 c705r2 PAL | 24,763 (46.5%) | 365 | 22,998 (21.6%) | 114 RAMB36 + 17 RAMB18 = 122.5 equivalent (87.5%) | 26 |

IIgs breakdown:
- P65C816: 1,758 LUT, 262 FF, 3 RAMB36 (microcode).
- cache_i: 1,148 LUT (448 LUTRAM), 8 RAMB36.
- Adapter top level including L0: 767 LUT (64 LUTRAM).
- iigs machine total: 17,380 LUT, 56 RAMB36.

C64 breakdown:
- T65: 1,051 LUT, 167 FF, 0 BRAM. The 1541's T65: 710 LUT.
- Video bridge frame banks: 64 RAMB36.
- The hierarchical report hides some rows: about 16 RAMB36 of the adapter's 39 are not listed and are presumably main_ram_i.

Both images are BRAM-tight. The C64 has about 28k LUTs free.

## INFERENCE: reuse for a C64 6510 at 2x the C64 clock

### A. Clocking options

**Option 1: copy the IIgs pattern.**
- Primary clock = 2x (63.06 MHz PAL / 65.45 MHz NTSC), with the C64 native clock as a BUFGCE gate every 2nd primary edge and a generated clock with `-edges {1 2 5}`.
- This gives an exact 2:1 ratio and a single CPU on the primary clock with CE.
- Pitfall found: ports/video_sync.vhd:50 uses `falling_edge(clk32)`, driven by core_clk (adapter:620). With a gated clock its falling edge moves from 15.86 ns to 7.93 ns after the rise, so it needs review.
- Primary→native paths become single-period (15.86 ns) unless the publishing registers are phase-qualified and given scoped multicycle constraints, as in IIgs XDC:37-70.
- HP2 ACLK, which is c64_clk today, should stay on a free-running clock (the primary), not the gated one.

**Option 2: one MMCM with two outputs.** My brute-force search, not run through Vivado, assuming VCO 600–1440 MHz:
- PAL: D=9, M=56.75, O0=15, O1=30 gives 63.0556 / 31.5278 MHz (-5.6 ppm).
- NTSC: D=5, M=24, O0=11, O1=22 gives 65.4545 / 32.7273 MHz (+0.2 ppm).
- For comparison, today's achieved PAL clock is 31.716 ns, i.e. 31.5298 MHz.
- This leaves existing C64 logic and constraints on an unchanged 31.5 MHz BUFG. Crossings become ordinary related-clock paths with BUFG-to-BUFG skew.

### B. Timing budget

- IIgs CPU loop at 57.27 MHz: 17.06 ns, 27 levels, about 0.63 ns per level under pressure. A fast C64 loop needs roughly 20 levels or fewer for PAL (15.858 ns) and fewer for NTSC (15.278 ns) to close without heroic repair.
- Segments of today's C64 worst path (c705r2; 79% routing because of +7 ns slack):
  - T65 MCycle → address: about 4 levels. T65's A output is a mux of registers selected by the registered Set_Addr_To_r, plus the PCAdder 9-bit add (T65.vhd:480, 676-687). That is shallower than the 65816's 2.1 ns BRAM start plus 24-bit adders.
  - ramDin → BAL_reg: about 8 levels.
  - The cartridge/PLA/IOE/cart_memory decode in between adds about 13 levels. It must be kept off the 1-clock path.
- T65 already pipelines its ALU (DI → BusB register; the ALU runs on BusA_r/BusB the next cycle), so the 8-bit ALU is not in the DI path.

### C. Memory options for one bus cycle per clock

- **(a) IIgs model.** An async LUTRAM L0 with raw-address tags handles 1-clock hits with no PLA decode. The BRAM-backed 64 KiB RAM and ROMs give 2-clock reads, read in parallel with a registered select.
  - Estimate: hit path about 4 + 6 + 8 ≈ 18–20 levels, plausible at 63 MHz.
  - 64 bytes is too small for zero page, stack, code and data together, so expect about 1.3–1.7 clocks per cycle, roughly 37–48x PAL. A larger direct-mapped LUTRAM L0 (0.5–2 KiB, a few hundred to about 1k LUTs) adds mux levels.
  - Coherence rule copied from IIgs: invalidate on CPU writes (or update), $00/$01 writes, any native I/O retirement (covers cartridge bank switches), REU/DMA writes and loader activity.
- **(b) True 1-clock BRAM reads via next-address lookahead.** Present the BRAM address from the next-state (D-side) values of T65's AD/BAL/BAH/PC/S and the next Set_Addr_To, the same trick the 65816 uses for its microcode BRAM.
  - Path: BRAM clock-to-out 2.1 ns → DI → next-address logic (≈5–8 levels) → BRAM address setup. Estimate 10–13 ns.
  - Needs a restructured T65 address unit, and NMOS semantics must be revalidated: undocumented opcodes, BAQuirk, page-crossing dummy reads.
- **(c) BRAM on the falling edge, or at a 4x clock with the CPU every 2nd edge.** About 11–12 ns until DI is valid, plus about 4 ns from DI to registers, so about 16 ns. Marginal at 63 MHz; not recommended.
- **(d) A full 64 KiB LUTRAM shadow copy.** About 8.2k storage LUTs plus a 1024:1 read mux and a huge address fan-out, with a BRAM copy kept for the VIC. Costly and still marginal.

### D. I/O cost model (applying the IIgs native bridge)

- To keep VIC, CIA and SID native, every I/O cycle has to commit in the native CPU slot (phase 28 issue / 31 retire), so it costs up to about 1 µs. That includes RMW double writes and dummy reads to I/O, which have side effects.
- Amdahl estimate: stock N µs becomes about k×1 µs + (N−k)/64 µs:
  - 1 I/O per 10 cycles: about 8.8x;
  - 1 per 20: about 15x;
  - 1 per 100: about 39x.
- This is the C64 version of the IIgs "exactly 2.0x" wall.
- Copy the IIgs fix for memory that the video chip can see: put colour RAM ($D800–$DBFF) and main RAM on a dual-port BRAM. The fast port takes staged CPU writes, deferred off VIC read edges as in iigs.sv:1866. The native port serves the VIC. Then screen and colour stores never wait for the native slot.
- Dual-porting the 64 KiB RAM needs no extra BRAM: it is still 16 RAMB36 in true dual-port mode. The loader would mux onto the native port.

### E. Patterns to copy

1. CPU on the fast clock with CE in every mode. Stock mode routes every cycle through the native commit, and the scheduler gets its original inputs.
2. Native bridge: phase-qualified commit, a toggle response, the read result registered at the native edge, and one commit per I/O cycle.
3. Capture the CPU request (addr/data/we) in registers before it reaches native logic. The IIgs did this only for IWM; elsewhere the live held bus left CPU→peripheral paths critical (ZipGS, sector RAM, WOZ RAM ENARDEN).
4. No MMU/PLA decode and no BRAM output on the 1-clock data path. Order the mux so the latest select is the final 2:1 stage.
5. Retire-slot rule: address and data load every cycle; only valid depends on cpu_ce. cpu_ce never drives RAM write enables or wide CE fan-outs directly.
6. DIRECT_ENABLE and KEEP on phase ticks. Scoped multicycle constraints only on exact publishing registers. DONT_TOUCH on clock gating.
7. Holds for peripherals with native access rates. Keep the existing IEC turbo guard.
8. Cooldown counter for selectable speeds, e.g. 1/2/4/…/64x as a minimum clocks-per-cycle setting.

### F. Pitfalls to avoid

1. Peripheral enables (IEC turbo guard, cartridge, REU) that combinationally depend on the fast CPU's live address.
2. The current C64 already drives main_ram ENARDEN from the CPU address through mapping (MCycle → main_ram_i ENARDEN, 22.68 ns). At 2x this must become registered.
3. Expecting RTL micro-tweaks to fix closure (±0.8 ns placement noise).
4. Per-clock margins alone: crossings need -from/-to margins.
5. Same-edge BRAM port collisions between the CPU and VIC ports.

## OPEN QUESTIONS

- Can stock mode stay cycle-exact against VICE 3.10 with the T65 moved to the 2x clock and CE'd on the primary edge that coincides with the native retirement? IRQ, NMI, RDY and BA sampling would have to be registered at that native edge. The IIgs did not have this exactness requirement. This needs the VICE comparison suite rerun.
- The exact 7-series dual-port collision behaviour (READ_FIRST on the write port with a common clock) was recalled from memory, not checked against UG473.
- MMCM settings come from my own search, not from a Vivado clk_wiz run.
- The 2x CPU island depth estimates come from netlist segments of a relaxed 31.5 MHz build, not from a 63 MHz implementation.
- Which shell logic currently on c64_clk (mailboxes, REU cache with 1,883 LUTs, content loader, cartridge, video bridge) would move to the primary clock under option 1?

## RISKS

- **Timing:** The IIgs needed a placement sweep plus reroute even at a period 9–12.5% longer than 2x C64. A design with the IIgs's path depth fails at 63–65 MHz.
- **Speed in real use:** Real programs will be I/O- and VIC-bound well below 64x. The IIgs reached about 20x on synthetic loops but about 5x on a real boot.
- **CPU rework:** Option (b) touches the core's instruction semantics, so a full 6510 test pass would be needed.
- **BRAM and LUTs:** BRAM is at 87.5%, so the cache must live in LUTRAM; about 28k LUTs are free.