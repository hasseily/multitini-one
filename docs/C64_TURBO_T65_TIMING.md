# C64 turbo evidence: T65 against block RAM, out-of-context timing

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: T65 one-bus-cycle-per-clock vs block RAM: OOC timing experiment on xc7z020clg484-2 (Vivado 2025.2)

**Answer:** 64x works out of context. The real cpu_6510/T65 wrapper runs one 6502 bus cycle per clock against block RAM at 63.06 MHz (64x PAL) and 65.45 MHz (64x NTSC) in every harness configuration I tried. The best plain-T65 result met 85 MHz. One small T65 change, not yet simulated, raises the ceiling to about 95 MHz. 128x single-cycle is not reachable with T65. The limit is the memory round trip (address logic, then the BRAM, then data logic, inside one clock), not the T65 itself. Use BRAM port A on the inverted CPU clock (V1). A phase-shifted BRAM clock (V2) gave no gain.

All numbers below are out-of-context results: there is no surrounding design, so no congestion. Nothing in the repository was edited. 43 implementation runs plus one synthesis check, all fully routed (0 routing errors) with hold ≥ +0.056 ns.

## 1. Harness design (verified)

**Sources.** Copies of the repository files at commit 534181278ee46da97999d660544973d5ca604c4d:
- T65_Pack.vhd, T65_ALU.vhd, T65_MCode.vhd from `third_party/c64/upstream/rtl/t65/`
- `hdl/core/c64/ports/T65.vhd`
- `hdl/core/c64/multitini_c64_6510_port.vhd`
- `hdl/core/c64/ports/cpu_6510.vhd`

All compiled as VHDL-2008. The top level is plain Verilog-2001 because the VHDL port is named `do`, which is a SystemVerilog keyword.

**CPU connections.**
- `enable` and `bus_start` come from a registered input pin. Tying enable to a constant 1 made no material difference (+0.106 vs +0.046 ns at 75 MHz).
- `SEPARATE_BUS_START` is left at its default. The local T65 declares `SeparateBusStart`/`BusStart` but never reads them (`T65.vhd:144,151`).
- rdy, irq_n, nmi_n, reset, port_tick and diIO all come from registered inputs.

**Memory map.** A C64-style PLA decode uses the live CPU address and the 6510 port pins LORAM/HIRAM/CHAREN:
- BASIC at $A000–$BFFF, KERNAL at $E000–$FFFF, $D000 as I/O or CHAR ROM.
- Writes go to RAM except in the I/O area.

**Memories.**
- RAM: 64Kx8 true dual-port. Port A is the CPU. Port B is an external read/write port on clk with a registered output, so it cannot be optimized away.
- ROM: 16Kx8. Port A is the CPU read; port B is a loader write port.
- io_sel, rom_sel and A15 are registered on the BRAM edge. The return mux picks the I/O byte register, ROM or RAM. All outputs are registered.

**Options.**
- `BRAM_CLOCKING`:
  - 0 = negedge clk (V1)
  - 1 = posedge of a phase-shifted clk_b (V2)
  - 2 = same rising edge (V4, the two-clocks-per-cycle comparison)
  - 3 = DI from a register, no BRAM in the CPU loop (V3)
- `RAM_SPLIT=1`: two 32Kx8 arrays instead of one 64Kx8.
- EXTRAS file adds:
  - a registered DMA address/data/WE mux in front of RAM port A
  - a 16 KiB cartridge ROM (ROML/ROMH) in BRAM
  - 1Kx4 color RAM in LUT RAM
  - GAME/EXROM in the decode
  - a wider registered-select return mux

**Utilization (base, 64K).** 729 LUTs (T65 647, 6510 port 82), 336 FFs (T65 167, which matches the 167 in the full build `build/c705r2/c64-pal/reports/utilization.rpt:67`), 20 RAMB36. EXTRAS: 24 RAMB36 and 64 LUT-RAM.

**Constraints and flow.**
- `create_clock` on the clock ports, with `HD.CLK_SRC` set to BUFGCTRL sites.
- `set_clock_uncertainty -setup 0.150`, giving 0.185 ns total per path. The real MMCM path has 0.103 ns.
- Flow mirrors the repository profile: synth OOC, `opt_design` Explore, `place_design` Default, `phys_opt_design` Explore, `route_design` Explore.
- The "explore" flow uses synth PerformanceOptimized, place ExtraTimingOpt, phys_opt AggressiveExplore, and a post-route phys_opt.

**Path classes.**
- addr = CPU registers → BRAM, or → bank-select register
- data = BRAM or select register → CPU registers
- int = register → register, not through BRAM
- "need" = requirement − slack

## 2. Results (WNS in ns; ≥ 0 means met)

Native CPU rate: PAL = 31.527954/32 = 0.98525 MHz; NTSC = 1.02273 MHz.

| Config | 63.06 MHz | 65.45 | 70 | 75 | 76.9 | 80 | 85 | 90 | 95 | 100 |
|---|---|---|---|---|---|---|---|---|---|---|
| V1, 64K RAM (cascaded) | +0.672 | +0.489 | +0.099 | −0.186 (explore +0.041) | | | | | | |
| V1, split RAM | | | | +0.046 | +0.050 | +0.033 (explore −0.124) | +0.008 (explore −0.120) | −0.283 (explore −0.323) | −0.722 | |
| V1, EXTRAS, 64K | +0.090 | | | | | | | | | |
| V1, EXTRAS, split | +0.388 | | | +0.052 | | −0.213 | −0.556 | | | |
| V1, split, modified T65 | | | | | | | +0.234 | +0.071 | −0.065 | −0.097 |
| V1, EXTRAS + modified T65, 64K | +0.597 | | | | | | | | | |
| V1, EXTRAS + modified T65, split | | | | | | +0.120 | | | | |

**V2, phase-shifted BRAM clock:**

| Config | Phase | WNS (addr / data) |
|---|---|---|
| 64K at 65.45 MHz | 90° | −1.109 (addr) |
| 64K at 65.45 MHz | 120° | −0.045 (addr) |
| 64K at 65.45 MHz | 150° | +0.259 |
| split at 75 MHz | 150° | −0.006 |
| split at 75 MHz | 165° | +0.268 / +0.445 |
| split at 75 MHz | 180° | +0.098 |
| split at 75 MHz | 195° | +0.026 |

The best phase is about 165–195°, which is effectively 180°, the same as V1.

**V3, T65 internal ceiling (no BRAM in the loop):**

| Period | WNS |
|---|---|
| 10 ns | +0.541 |
| 9 ns | +0.924 |
| 8 ns | +0.028 |
| 7 ns | −0.041 (explore −0.055) |

So the T65 alone reaches about 140 MHz.

**V4, BRAM on the same rising edge (needs two clocks per 6502 cycle):**

| Config | Clock | WNS |
|---|---|---|
| 64K | 100 MHz | +0.746 |
| 64K | 125 MHz | +0.171 |
| split | 125 MHz | +0.166 |
| split | 142.9 MHz | −0.238 |
| EXTRAS, split | 126.1 MHz (= 64x at two clocks per cycle) | +0.270 |

V4 times internal paths single-cycle, so it is pessimistic.

Run-to-run and flow noise is about ±0.15 ns. Explore was not better than the default flow here.

## 3. What limits it (verified from path reports)

**Address half** (`runs/v1_p15858`, 7 levels, 6.29 ns): `MCycle_reg` → microcode decode (LUT3, LUT6) → PCAdder/branch adder (two CARRY4) → `pc_bus_addr` / A mux (`T65.vhd:480,679,683`) → `cpu_addr` net with fanout 22 (1.2–1.75 ns of routing) → BRAM address setup 0.490 ns.
- With EXTRAS, the worst address path becomes the write-enable decode: address → io_sel → DMA mux → WEBWE (setup 0.476 ns).

**Data half** (64K, 6.80 ns, 9 levels): BRAM CLK→CASCADEOUT 2.504 + CASCADEIN→DO 0.376 → about 1.2 ns of routing → bank mux LUT → `localDi` mux LUT (`cpu_6510.vhd:92-93`, which uses `accessIO`) → BusA mux → BAL adder (three CARRY4, `T65.vhd:593`) → `BAL_reg[8]`.
- A non-cascaded BRAM has CLK→DO of 2.125 ns. Cascading costs about 0.8 ns.

**Internal** (V3 at 7 ns, 6.87 ns, 13 levels): `instruction_reg` → microcode Set_BusA_To → BusA mux (MUXF7) → BAL adder → BAL.

**Repository RAM.** I ran `hdl/core/c64/multitini_c64_local_ram.sv` through OOC synthesis: it builds 16 RAMB36, all cascaded (RAM_EXTENSION_A=LOWER/UPPER), with no DO register. So the current RAM pays the cascade penalty.

**Modified T65 experiment** (`src_mod/T65.vhd`, scratch only): the BAL adder takes `Y when Set_BusA_To = Set_BusA_To_Y else X` instead of BusA.
- Every microcode state with `BAAdd = "10"` sets Set_BusA_To to X or Y (`T65_MCode.vhd:853-854, 969-970, 1019-1020, 1060-1064`). So the change should preserve behavior. **Not simulated.**
- It takes DI out of the adder cone and is worth about 1 ns.
- The next data-half limiters are the BusB_r `DI+1` incrementer (`T65.vhd:583`) and the bank/localDi/BusA mux chain.

## 4. Inference and recommendation

- **Target 63.06 / 65.45 MHz (64x)**, one 6502 cycle per CPU clock, BRAM port A on the inverted CPU clock (V1). The BRAM's built-in clock inversion is used: the timing report shows "CLKBWRCLK (IS_INVERTED)", so there is no extra clock buffer or MMCM output. It met timing in every configuration, with +0.090 ns worst case (EXTRAS with cascaded RAM). Expect the in-context margin to be smaller.
- **Split the RAM** (two 32K arrays, A15 registered with the bank select) to avoid the cascade. Consider the BA-add operand change after it passes the VICE CPU tests. Together these give roughly 1.5–2 ns of headroom at 64x.
- **80x (78.82 MHz PAL)** met OOC only with the modified T65 and split RAM (+0.120 with EXTRAS). There is no realistic in-context margin, so it should not be the first target.
- **Practical ceiling** for any T65-based single-cycle design is about 85 MHz unmodified and about 95–100 MHz with targeted T65 or mux work. 128x would need a different CPU microarchitecture that computes the address a cycle early (pipelined); I did not explore that.
- **V2 brings nothing.** The address half needs at least about 5.1–5.8 ns, roughly as much as the data half, so moving the BRAM edge away from 180° only fails one side. It also adds an inter-clock relationship and phase-error uncertainty.
- **V4** (a 126.1 MHz clock, two clocks per 6502 cycle) gives the same per-half budget as V1 at 63 MHz and also met 64x. But it doubles the clock rate for everything on that clock and needs enable logic, and possibly multicycle constraints. V1 is simpler.

## 5. Caveats and risks

- **No surrounding design.** The full C64 image uses 24,763 LUTs (47%) and 114/140 RAMB36 (81%). The harness uses about 900 LUTs and 20–24 RAMB36. Scattered BRAM sites will lengthen the address fan-out and BRAM→LUT routes, which are already 1.2–1.75 ns.
- **Clock modeling.** OOC port clocks use estimated insertion delay (reports show "net ... unset"); real BUFG/MMCM skew and duty cycle will differ. I used 0.185 ns uncertainty per path versus 0.103 ns for the real MMCM.
- **The real data path is heavier.** The worst c64_clk path in `build/c705r2` (address → cartridge mapping → cart_memory cache hit → ramDin → `BAL_reg`, 24.5 ns, 27 levels) cannot stay combinational at 63 MHz. Cartridge ROM must come from BRAM with a registered select, or stall on a registered miss. VIC, CIA and SID I/O must stall to native timing; effective speed will be below the clock ratio for I/O-heavy code.
- **V3 and V4 are timing-only constructs.** V4 reads data one clock late; V3 ignores memory.

## 6. Open questions

- **Clock plan.** The current MMCM is VCO = 150 × 43.875 / 5 = 1316.25 MHz with a fractional CLKOUT0 divide of 41.750 (`build/c705r2/.../zynq_ps_bd_clk_wiz_c64_0_clk_wiz.v:126-134`). A 2x output would need 20.875, and only CLKOUT0 can take a fractional divide, so a synchronous 2x clock cannot come from the same VCO. Arithmetic search (not run through the clock wizard):
  - PAL: D=9, M=56.75, VCO 945.83 MHz → /30 = 31.5278 MHz (−5.6 ppm; the current setting is −32 ppm), /15 = 63.056 MHz, /12 = 78.82 MHz (80x).
  - NTSC: D=5, M=24, VCO 720 MHz → /22 = 32.7273 MHz, /11 = 65.4545 MHz.
- **Cross-port collisions.** The CPU on port A (inverted 2x clock) and VIC/DMA on port B (c64_clk) need a collision analysis for same-address accesses. With clocks from one MMCM, the edges sit at least about 7.9 ns apart.
- **In-context confirmation.** Confirm in a full build with the real cartridge, color RAM and DMA muxes.

## Files

The harness, run scripts and Vivado reports were throwaway session files and are not kept. Experiment E1 in the design rebuilds an out-of-context harness from the real island RTL.