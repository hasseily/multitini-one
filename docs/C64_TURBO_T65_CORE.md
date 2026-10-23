# C64 turbo evidence: the local T65 as a fast CPU

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: Topic: the local T65 port as the engine for a fast C64 CPU (investigation only; no repository files changed)

## Bottom line

- **Interface.** T65 is a Moore-style bus master. A, DO, R_W_n, Sync and NMI_ack depend only on registered state. No input reaches them combinationally, and every state change happens on a rising Clk edge with Enable high.
- **Measured headroom.** I built the stock local T65 against a private 64 KiB block RAM (16 RAMB36) out of context:
  - With the RAM clocked on the falling edge (one bus cycle per clock), it met 14.0 ns, which is 71.4 MHz.
  - With registered addresses (two clocks per bus cycle), it met 8.0 ns, which is 125 MHz or 62.5 M CPU cycles/s.
- **What 64x needs.** 64x PAL is 63.06 M cycles/s, exactly 2x c64_clk. In isolation the stock T65 has roughly 10–15% margin over that. A full-image placement with ROM/I-O decode added will eat most of it. 64x is a ceiling before stalls, so "64x or more" in sustained throughput is not credible with T65 at one MCycle per bus cycle.
- **Dummy cycles.** T65 cannot skip dummy cycles without microcode and datapath changes. The project rules also currently forbid folding them.
- **Drive CPU.** The 1541 has a separate T65 instance built from the same VHDL file. It must stay native.

## 1. Per-cycle protocol (verified from source)

**Enable and RDY**
- Every T65 state register updates only on a rising Clk edge with Enable='1': hdl/core/c64/ports/T65.vhd:391, 490, 575, 731.
- Within that, most updates also need `really_rdy` (399, 491, 576, 739). `really_rdy <= Rdy or not(WRn_i)` (283), so RDY stalls read cycles only. Write cycles proceed, which is correct NMOS/6510 behaviour.
- A held read leaves A, DO and R_W_n unchanged, so the same read is repeated.
- During a held read, Enable still advances:
  - `rdy_mod` (393-397), used for SHA/SHX/SHY/SHS at MCycle 3, or 4 for $93
  - `read_was_held` (738)
  - NMI pin sampling and edge latch (784-789)
  - IRQ pin sampling (773-783), except while a SEI read is held

**A, R_W_n and DO timing**
- Bus cycle k runs from Enable edge E(k-1) to E(k).
- R_W_n comes straight from the WRn_i flip-flop (288, 400: `WRn_i <= not Write or RstCycle`, so reset cycles never write).
- A (683-688) and DO (693-709) are combinational from registers. They settle a few logic levels after E(k-1) and stay stable until E(k).
- The microcode row for MCycle n sets the address of bus cycle n+1, because Set_Addr_To is registered into Set_Addr_To_r at 437-441.
- DI-capture controls (LDDI, LDAD, LDBAL, LDBAH, Jump="10", IR load) act on the current cycle's DI.
- The exception is SO: `SO_n_o` and P(V) update every Clk, not gated by Enable (549-552). The C64 ties SO_n='1'; the drive uses it.

**When DI is sampled**
- DI is captured at E(k) into:
  - IR (419, MCycle=0 only, forced to $00 when IRQReq/NMIReq)
  - BusB, every cycle (579)
  - BusB_r = DI+1 (583)
  - BusA_r via the BusA mux (578, 661-673)
  - DL (640), AD (646), BAL (649), BAH (652)
  - PC = DI&DL (463)
- ALU inputs are registered (BusA_r and BusB; ALU port map at 341-351). Results are written back at the next instruction's MCycle 0 (492-505), which reproduces the 6502 fetch/execute overlap.
- In fpga64 the native slot works like this:
  - It issues at phase 28: `request` drives `ramCE` (multitini_c64_cpu_scheduler.vhd:42-48, 60; fpga64_sid_iec.vhd:1021-1023, 1035).
  - It retires at phase 31: `retire` = pending_slow(2) (scheduler:61, 70; fpga64:1036). T65 Enable = `enableCpu and not dma_active and not memory_wait_q` (fpga64:989).
  - The 2x/3x/4x slots issue at 16/20/24 and retire at 19/23/27.
  - Dense mode issues at 16, 18, …, 26 and retires one clock later, at 17, 19, …, 27. The address settles in the issue clock and the BRAM data reaches T65 in the next.

**BusStart and SeparateBusStart**
- They are dead. T65.vhd:143-144 and 151 declare them ("Retained for wrapper compatibility; interrupt decisions use Enable only") and nothing reads them.
- fpga64 still wires `bus_start` (992) with SEPARATE_BUS_START=>true (985).
- In commit 347f8a7 `bus_start_i` latched a MAME-style late-NMI takeover at bus issue. Commit 5341812 removed that when the reference moved to VICE.

**IRQ and NMI sampling (Mode "00", local VICE 3.10 changes)**
- **Pin sampling.** IRQ_n is sampled into IRQ_n_o and IRQ_n_oo on Enable (773-783); IRQ_n_oo is forced to '1' at MCycle 0.
- **IRQ request.** IRQReq is recomputed every accepted cycle except a taken-branch Jump="11" cycle (753): `(IRQ_n_o=0 or IRQ_n_oo=0) and (I=0 or (IR=$58 and read_was_held))` (763-768). This gives VICE's two-cycle latency and the held-CLI case.
- **NMI.** An NMI edge is latched when NMI_n_o='1' and NMI_n='0' between Enable samples (787-789). `NMIReq <= NMIAct and IR/=$00` (754-758).
- **Dispatch.** At MCycle 0 the CPU does not increment PC and forces IR=$00 (412-428).
- **BRK/IRQ to NMI vector redirection.** Lines 626-630; NMIAct is consumed before the low vector read (742-745, 792-796). B is preserved for a real BRK through PwithB (691).
- **Turbo consequence.** All sampling is relative to CPU cycles. A turbo CPU sees CPU-relative latency, which is correct. An NMI pulse that rises and falls entirely between two Enables is missed, which matters only at native cadence.

## 2. Registered vs combinational outputs (verified)

- **Registered:** R_W_n (WRn_i), plus the internal Set_Addr_To_r, Write_Data_r, ALU_Op_r and MCycle.
- **Combinational from registers only:**
  - **A** (683-688) is a 4:1 mux on Set_Addr_To_r:
    - SP = "01"&S
    - ZPG = DBR&"00"&AD
    - BA = BAH&BAL
    - PBR = PBR&pc_bus_addr
  - **pc_bus_addr** (679-681) selects PC at branch MCycle 2, `branch_dummy_addr` at branch MCycle 3 (a register loaded at 456-458), otherwise PC(15:8)&PCAdder(7:0).
  - **PCAdder** (480-481) is PC(7:0) + sign-extended DL when PCAdd='1'. PCAdd is a combinational microcode output (T65_MCode.vhd:822, branch Cycle_2).
  - For Mode "00", PCAdd='1' only when pc_bus_addr selects PC. The PCAdder adder therefore never reaches A functionally. Synthesis still keeps it in the A path: the OOC A path runs through `branch_dummy_addr…CARRY4 ×2`.
  - **DO** (695-709) is a 12:1 mux on Write_Data_r of DL, A, X, Y, S, PwithB, PCL, PCH, and AND terms with BusB_r.
  - **Sync** (284) and **NMI_ack** (280) are decodes.
- **Inputs used combinationally:** DI into the register D pins; Rdy into the CE logic through really_rdy (fpga64 drives `rdy => baLoc`, :996). Neither reaches A, DO or R_W_n.
- **cpu_6510.vhd adds two things:**
  - `accessIO` decode of localA, plus the localDi mux (DO on writes, port read at $00/$01, else di), at 92-93. This lies in front of DI.
  - Port writes on the Enable edge (103).

## 3. Running one bus cycle per clock against synchronous block RAM

### Measured: full image build/c705r2/c64-pal

These numbers are from a relaxed 31.716 ns constraint, so routing is not optimized. There are no C64 multicycle constraints, so every CPU path is timed as single-cycle.

| Path | Data path delay | Logic levels | Route | Slack |
| --- | --- | --- | --- | --- |
| T65 reg → main_ram ENARDEN | 22.68 ns | 15 | 19.9 ns | +8.39 ns |
| main_ram BRAM → T65 BAL[8] | 13.59 ns | 12 | — | — |
| T65 reg → T65 reg, worst (MCycle → … → cart → ramDin → BAL[8]) | 24.53 ns | 27 | — | +7.05 ns |
| BusA_r → ABC (ALU, internal) | 10.5 ns | 10 | — | — |
| ALU_Op_r → Y | 9.5 ns | 10 | — | — |
| IR → Set_Addr_To_r | 6.4 ns | 4 | — | — |
| IR → IRQReq | 8.3 ns | 6 | — | — |

- In the A path, the address leaves T65 after about 3 LUTs. The other ~11 levels are PLA, cartridge, ez_rom and the local RAM write enable. This logic is in the path even with no cartridge loaded (runtime_addr, cart_game, ez_rom).
- In the DI path, BRAM clock-to-out with cascade is 2.50 + 0.38 = 2.88 ns. Then come machine_q, cart_memory and cartridge muxes (3 LUTs), buslogic/localDi (1 LUT), and inside T65 the BusA mux, then the BAL 9-bit adder (2 CARRY4) and a final LUT.
- T65 uses 1051 LUTs / 167 FFs (cpu_6510 total 1115 LUTs). The whole image uses 24,763 LUTs.

### Measured: OOC experiment (my wrapper, stock T65 sources)

The wrapper is the stock local T65 plus a private 64Kx8 RAM: 16 RAMB36, READ_FIRST, one address, with DI = RW_n ? q : DO. Part xc7z020clg484-2, placed and routed.

**Falling-edge RAM, Enable='1' every clock, 14.0 ns: WNS +0.179 ns (met)**

| Path | Budget | Data path delay | Levels | Slack | Detail |
| --- | --- | --- | --- | --- | --- |
| rise → RAM address (A) | 7.0 ns (half period) | 5.87 ns | 7 | +0.567 | MCycle → LUT3 → LUT6 (PCAdd decode, fo38) → LUT3 → 2 CARRY4 (PCAdder) → LUT5 → LUT6 → a[5], fanout 17 to the BRAMs |
| RAM (fall) → BAL[8] (DI) | 7.0 ns | 6.78 ns | 9 | +0.179 | cascade clock-to-out ≈ 2.9 ns, then DI mux LUT3 → LUT5 → LUT6 → LUT6 → 3 CARRY4 → LUT6 |
| internal | 14 ns | 9.44 ns | 12 | — | IR → BusA select → BAL adder |

From these, the minimum period is about 2 × 6.82 = 13.6 ns, about 73 MHz.

**Rising-edge RAM, Enable every second clock, 8.0 ns: WNS +0.652 ns (met)**
- A path 6.30 ns, DI path 7.27 ns, internal 7.31 ns.
- That is about 136 MHz, so about 68 M CPU cycles/s at two clocks per cycle.

Both OOC results are optimistic: an empty device, no ROM/I-O/VIC sharing, no stall logic.

### Inference: the options

**(a) BRAM on the opposite edge, or on a phase-shifted MMCM output.**
- This works without touching T65 because A, DO and R_W_n depend only on state.
- At 2x c64_clk (63.06 MHz PAL, half period 7.93 ns), OOC slack would be about +2.1 ns on A and +1.15 ns on DI.
- In the real machine the DI side gains a source mux (RAM / KERNAL / BASIC / char / I-O / port) of about 1–2 LUTs. Margin is then thin but plausible. NTSC 2x c64_clk is 65.45 MHz (7.64 ns half period).
- A phase shift gains little because the A and DI budgets are nearly balanced (5.9 vs 6.8 ns).

**Two semantics-preserving T65 edits would cut both critical paths:**
- Make pc_bus_addr NMOS-only: A = (branch & MCycle=3) ? branch_dummy_addr : PC. This removes the LUT3 + 2 CARRY4 PCAdder hop from A, roughly 1 ns.
- Feed the BAL "BA Add" adder (593) from a dedicated X/Y index mux instead of BusA.
  - Every BAAdd="10" row selects X or Y explicitly (MCode 853, 969, 1019, 1060-1062). These assignments win over the DI default at 226-232.
  - So the measured worst DI → BusA → BAL-adder path is functionally false, and so is the IR → BusA → BAL internal path.
  - I estimate the DI path would then fall to about 5 ns. That is not measured.

**(b) Next-address look-ahead (BRAM driven from the D side of the state registers).**
- This allows a single-edge, one-cycle-per-clock design. The BRAM also needs EN = advance, so stalls hold its output.
- A_next = mux(Break ? PBR : Set_Addr_To; S_next; AD_next; BAH/BAL_next; PC_next or branch_dummy_next).
- Its DI dependence is the AD/BAL/BAH/PC load paths only: DI → 1–2 LUTs → address mux → BRAM.
- I estimate the loop at BRAM 2.9 ns + 4–5 levels, about 7.5–9 ns. This is the highest ceiling.
- It needs T65 restructured into explicit next-state signals: lines 399-475, 443-448 and 586-653 duplicated or refactored. That is a sizable, error-prone edit with an equivalence-check burden.

**(c) Registered-address two-clock cycle.**
- This is today's dense mode: about 2 clocks per CPU cycle.
- At c64_clk it is capped at 15.76 MHz even if all 16 slots were used. The current mask gives only 7 per frame, about 6.9 MHz.
- 64x this way needs a CPU clock of about 126 MHz. The OOC run shows that is reachable in isolation only.

**(d) Precedent on this board.** The IIgs P65C816 does one CPU cycle per 57.27 MHz clock on hits, which the repo measures at 145,151 ticks for 145,151 advances (docs/APPLE_IIGS_CPU_VALIDATION.md:229-239).
- It serves those hits from an 8×64-bit asynchronous LUTRAM L0 (multitini_apple_iigs_adapter.sv:216-279).
- Its routed WNS is about 0 and needed repair flows. Treat about 57–65 MHz as a realistic one-cycle-per-clock ceiling for a 65xx in the full image.

## 4. NMOS completeness and local modifications (verified unless marked)

**Local changes vs upstream (diff in my scratch t65.diff):**
- Taken-branch dummy-read addresses (679-681, 456-458).
- VICE 3.10 IRQ/NMI changes: IRQ_n_oo, read_was_held, nmi_clear_pending, nmi_dispatch_seen, the SEI-hold freeze and BRK redirection (626).
- NMI_ack also asserts when NMICycle is set (280).
- An unused BusStart port and generic.
- T65_MCode, T65_ALU and T65_Pack are unchanged upstream files (hdl_sources.txt:20-22).

**Decimal mode is NMOS-correct:**
- ADC: Z from the binary sum, N and V from the intermediate result, C after adjust (T65_ALU.vhd:100-126).
- SBC: flags from the binary result (157-179).
- ARR has its decimal case (226-242).
- RRA and ISC reach ADC/SBC through ALUmore.

**Undocumented opcodes:**
- SLO/RLA/SRE/RRA/DCP/ISC, LAX, SAX, LAS, SBX, ANC, ALR, ARR, the NOP variants with page-cross timing, and SHA/SHX/SHY/TAS with RDY and page-cross quirks (rdy_mod, BAQuirk) are implemented.
- The header claims all Lorenz tests pass (Ver 313) and that VICE sha/shs/shxy behaviour is followed (Ver 315). The repo has no Lorenz run. docs/C64_RAM_CPU.md calls the existing matrix "not exhaustive".

**Differences from VICE x64sc** (third_party/vice_reference/upstream/vice/src/6510dtvcore.c):
- **ANE ($8B).** T65 computes (A|$EE)&X&imm (T65.vhd:671). VICE uses $EF, or $EE when RDY steals after the first fetch (6510dtvcore.c:825-826, 864-876). Bit 0 can differ.
- **LXA ($AB).** Both use $EE (T65.vhd:670; VICE 1280). They match.
- **JAM/KIL** ($02, $12, $22, …, $F2). T65 runs them as 2-cycle, 1-byte NOPs (MCode 571-589: the "KIL !!!" branch does nothing). VICE jams (1022-1029, 1854-1874).

**Corner cases that look correct:**
- Reset with stack decrement and writes suppressed (400, 446).
- JMP ($xxFF) page wrap: BAAdd "01" changes BAL only (MCode 698).
- Zero-page index and (zp),Y pointer wrap (8-bit AD).

**Turbo relevance.** ALU and decimal behaviour are combinational and clock-independent. The RDY quirks only fire when RDY is low, and the current extra slots require BA high.

## 5. Skipping dummy cycles

**How sequencing works (verified).** MCycle only increments, or resets to 0 at LCycle or Break (746-751). Break is the only early exit: page cross not needed, or branch with no crossing (659).
- The microcode is indexed by the registered IR, MCycle, P and Rdy_mod (MCode:111).
- IR is loaded only at the end of MCycle 0, so every instruction takes at least 2 cycles. The implied-op second cycle is a dummy PC read: default lCycle=Cycle_1 (MCode:113).

**Dummy cycles per instruction class in T65 (from MCode):**
- Implied: 1
- PHA/PHP: 1 (332-365)
- PLA/PLP: 2 (366-403)
- JSR: 1 (271-292)
- RTS: 3 (314-331)
- RTI: 2 (293-313)
- BRK: 1 (243-270), plus the discarded opcode fetch for IRQ/NMI
- zp,X and (zp,X): 1 (889-950, 486-530)
- RMW: 1 dummy write, plus 1 dummy read for indexed forms (614-645, 732-765, 1007-1043)
- abs,X/Y and (zp),Y: 1 on page cross for reads, always for writes
- Taken branch: 1, plus 1 on page cross

My rough estimate is 13–20% of executed cycles. It is not measured.

**Without editing the microcode** there is no way to remove an MCycle. You can only make dummy cycles cheaper in a multi-clock memory scheme, by retiring a read whose data is unused without waiting for memory.
- That needs a "DI consumed" predicate exported from T65: LDDI, LDAD, LDBAL, LDBAH, Jump="10", MCycle 0, or a Set_BusA_To_DI-class select.
- BusB loads DI every cycle (579) and the ALU uses it at the next MCycle 0, so the predicate is delicate.
- It gives nothing in a one-cycle-per-clock design.

**Editing the microcode to fold cycles would require:**
- Merging rows and shortening LCycle per opcode.
- New datapaths on the DI critical path: DI+X for zp,X, a 16-bit BAH:BAL+index add for abs,X, an opcode predecode of DI at MCycle 0 for implied ops, and an ALU fed from DI for RMW.
- Re-qualifying the VICE interrupt rules and rdy_mod, which are tied to cycle numbers (395).
- Keeping stock mode bit-identical, which needs a separate turbo entity or mode.

The current rules forbid it: "Do not fold dummy accesses or NMOS read/modify/write cycles" (docs/C64_RAM_CPU.md:23-24), and acceleration "must preserve NMOS 6510 instruction and bus semantics" (MULTITINI_ONE_PLAN.md:69-71).

## 6. The drive CPU

- It is a separate T65 instance: c1541_logic.sv:90-105, `.enable(ph2_f)`, `.rdy(1'b1)`, `.nmi_n(1'b1)`, `so_n = byte_n | ~soe` (88).
- ph2_f is a 1.000 MHz pulse: 16 drive_ce ticks from a 16 MHz fractional accumulator (multitini_c64_1541.sv:217-245).
- The routed instance is drive_i/drive_logic/cpu, module T65__parameterized0: 710 LUTs, 159 FFs.
- It is built from the same hdl/core/c64/ports/T65.vhd (hdl_sources.txt:23; c1541 instantiates the entity "T65"). Any T65.vhd edit changes the drive CPU too, unless it goes in a generic or a new entity.
- It must stay native: its VIAs, GCR byte timing and disk rotation share ph2 and ce. C64 turbo is already suspended on IEC or parallel activity plus 0.5 s (multitini_c64_cpu_turbo_guard.sv:12, 36-50).

## Open questions

- Will the full image close a ~63–65 MHz CPU domain? This depends on placement and on the added data-source mux, banking, stall and Enable fanout logic. It has not been tested.
- The local RAM is currently simple dual-port and shared through one machine address. A fast CPU needs its own port: true dual-port with port A on the CPU clock or inverted clock, and port B for VIC, DMA and loader. Cross-port collision behaviour with different clock edges needs checking against UG473.
- KERNAL, BASIC and char ROM (dprom, sync read from cpuAddr on c64_clk) would need a CPU-clock read port.
- Should the turbo mode match VICE ANE ($EF) and JAM?
- Will the user accept relaxing the "no folding" rule for RAM-only dummy cycles if more than 64x is wanted?

## Risks

- OOC margins (+0.18 ns at 71 MHz) are for the CPU alone. In the full image, PLA, cartridge and REU logic currently sits on the CPU address path, as the 15-level ENARDEN path shows. Fast accesses would need a registered or precomputed bank map, with all I/O, $00/$01 and cartridge accesses diverted to native slots.
- Edits to T65.vhd also affect the 1541 CPU and the VICE-qualified stock path. Equivalence checks or a separate entity are needed.
- A CPU on a separate 2x clock needs reviewed related-clock crossings for IRQ/NMI/RDY, the bus and the port tick. This is a new clock, so the AGENTS.md review rule applies.