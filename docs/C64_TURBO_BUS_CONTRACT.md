# C64 turbo evidence: machine-bus contract for a fast CPU island

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: Report: the C64 machine-bus contract for a fast CPU island

Scope and method: I read every file named in the task in full, plus T65.vhd, cpu_6510.vhd, the 6510 port, the cpu scheduler, the turbo guard, and the C64 section of the top-level. I also read the c705r2 PAL reports and the generated clk_wiz_c64 source. I made no changes to the repository. The only scratch file is (session scratch, not kept) a search over MMCM settings.

Labels: [V] means verified in the source or reports. [I] means my inference.

## 0. Main findings

1. [V] The machine port is used at two fixed points in each native cycle: VIC0 (phase 12), and the CPU slot that issues at phase 28 and retires at 31 (fpga64_sid_iec.vhd:1021-1023). The VIC's phi2 fetches (character pointers on badlines, sprite data) need the CPU scheduler to issue that phase-28 slot while cpuHasBus=0 (ramCE line 1023; scheduler lines 42-43). An island design therefore cannot drop the native slot. It must issue every native cycle, either as a proxy transaction or as a harmless dummy RAM read.
2. [V] If no cartridge is loaded, memory_wait is always 0. cart_stall exists only when cart_external_select is true, which needs cart_loaded (adapter.sv:397-399, 504). Smart turbo is already disabled when a cartridge is loaded (adapter.sv:515). An island that is limited to the no-cartridge case avoids all cartridge, PSRAM and memory_wait logic.
3. [V] The worst c64_clk path (24.5 ns, 27 levels) starts at the T65 address and runs through the PLA, cartridge mapping and cartridge cache compare, then back into T65 DI, all in one clock. The main-RAM enable path from the T65 is 22.68 ns over 15 levels. In a cartridge-free island both paths become private: T65, a small decode, the BRAM, then T65.
4. [V] The C64 has no hardware CPU-state readout and no RAM peek path. The C64 "snapshot" is the frozen-frame video test (docs/C64_SNAPSHOT_HARDWARE_TEST.md). T65's Regs and DEBUG outputs are not connected in cpu_6510.vhd:72-90. The only CPU-address observer in production is the content loader's $FFCF check (content_loader.sv:503).
5. [V] BRAM use in c705r2 PAL: 114 RAMB36 + 17 RAMB18, which is 122.5 of 140 RAMB36 equivalents. About 17.5 RAMB36 are free. Converting the 64 KiB RAM, the BASIC/KERNAL ROM, the character ROM and colour RAM to true dual-port with independent clocks costs no extra BRAM.
6. [I] A synchronous related clock is clearly better than an unrelated clock, for exactness and for CDC risk. Integer MMCM settings exist that also improve the current PAL frequency error:
   - PAL 2x or 3x: −5.6 ppm, against −32 ppm today.
   - NTSC 2x, 3x or 4x: +0.2 ppm.
   - PAL 4x: −143 ppm at best.
7. [I] Once local code runs fast, the forwarded-I/O rate sets the effective speed. Each forwarded access costs roughly 0.5–1 µs. With 2–5% I/O accesses and no write posting, the effective speed is about 20–33x even if local code runs at 64x (arithmetic in section 5). Posted I/O writes would be needed to get close to 64x.

## 1. Producers and consumers of the bus signals

All lines are in hdl/core/c64/ports/fpga64_sid_iec.vhd unless another file is named.

**cpuAddr_pre, cpuDo_pre, cpuWe_pre (raw CPU outputs)**
- Produced by the cpu_6510 instance at 999-1001 (`addr <= localA(15:0)`, `do`, `we = not R_W_n`; cpu_6510.vhd:110-112).
- Consumed by:
  - the DMA mux at 1094-1096;
  - cpu_bus_addr/wdata/write at 1040-1042;
  - cpu_extra_allowed at 1047-1048, which blocks $0000/$0001;
  - the cpuHasBus decision at VIC3, which uses cpuWe_pre at 530 and 553.
- [V] The address is held stable from the retire edge (phase 31) until the next retire. Nothing uses the CPU address in phases 0–15 except cpuWe_pre at the VIC3 edge (phase 15). The `cs_io` test at EXT1/EXT5 (line 1080) is always true, because cpuHasBus is 0 there (cleared at 557) and buslogic only decodes I/O when cpuHasBus=1. So the deadline for presenting the next CPU request is the phase-15 edge, 16 clocks (about 507 ns PAL) after retire.

**cpuAddr, cpuDo, cpuWe (after the DMA mux, 1094-1096)**
- cpuAddr goes to:
  - buslogic cpuAddr (628) → PLA and kernel ROM read address (buslogic:147, 220);
  - VIC aRegisters(5:0) (735);
  - the SID chip-select decode using bits 5 and 8 (815) and the SID address (831);
  - CIA1 and CIA2 rs (876, 906).
- cpuDo goes to:
  - colour RAM data (601);
  - buslogic cpuData (629), which is not used inside buslogic;
  - the vicBus latch (686);
  - VIC diRegisters (736), SID data_in (832), CIA db_in (877, 907);
  - ramDout (1016).
- cpuWe goes to buslogic (627, which feeds systemWe at buslogic:297), pulseWr_io (670), the vicBus latch (685), VIC we (732), and CIA rw (874, 904).
- [V] The VIC latches cs, we, addr and data on every clock while phi=1, i.e. phases 16–31 (video_vicII_656x.vhd:300-309). A forwarded address must stay stable for the whole CPU half.

**cpuDi (buslogic dataToCpu, 640)**
- Goes to:
  - T65 DI through the cpu_6510 localDi mux (998; cpu_6510.vhd:93-94, where $0000/$0001 reads come from the port and writes loop back DO);
  - colorDataAec when aec=0 (698): the VIC takes colour nibbles from the CPU data bus in the three cycles after BA falls (the FLI effect);
  - dma_din, the REU read data (1100).
- Its sources, in priority order (buslogic:153-193): char ROM > BASIC/KERNAL ROM > RAM > VIC > SID > colour RAM (low nibble only; the high nibble is lastVicData) > CIA1 > CIA2 > cartridge RAM/ROM paths > io_data. The default is lastVicData, the floating bus.

**cpuIO (6510 port output pins, 1007)**
- Formula (6510_port.vhd:39): `(data & dir) | (PULLUP & ~dir)`, with PULLUP = $17.
- Goes to buslogic bankSwitch = cpuIO(2:0) at 617. That bankSwitch serves both CPU and DMA decoding.
- cass_motor and cass_write (1010-1011) are unconnected in the adapter (553).
- The port's decay tick is enableSid & ~memory_wait_q (1006), the native 1 MHz tick.
- The port's external input is the constant "000" & cass_sense & "0111"; cass_sense is tied to 1 in the adapter (1005; adapter:553).

**cpu_bus_valid/addr/wdata/write (1039-1042)**
- `valid = enableCpu & ~dma_active & cpuHasBus & ~memory_wait_q`. It marks one completed CPU transaction in the native slot. A RDY-held read gives 0.
- Only consumer: the REU (adapter:437-439).
  - `io_valid = cpu_bus_valid & reu_io_select`.
  - `io_addr = cpu_bus_addr[4:0]`, so the registers mirror every $20 bytes across $DF00-$DFFF.
  - `ff00_write = valid & write & addr==$FF00`, regardless of mapping.

**cpuHasBus**
- Producers:
  - reset at 449;
  - at the VIC3 edge: 518, 524 (DMA owns), 531 (`baLoc | cpuWe_pre`);
  - the DMA verify-peek release in CPU0–CPUB: 553 (`baLoc | cpuWe_pre`);
  - cleared at phase 31: 557.
- Consumers:
  - DMA post-access latch (536);
  - buslogic CPU/VIC branch select (614; buslogic:217 vs 298);
  - ramCE (1023), dma_ack (1026), the peek strobes (1030-1034);
  - cpu_bus_valid (1039), cpu_extra_allowed (1047), dma_cycle (1099; unused output, adapter:532).

**baLoc, aec, ba_reu, ba_dma**
- Produced by the VIC: `baLoc` (video_vicII_656x.vhd:756, port 715), `ba_reu` (755), `ba_dma` (757, unused in fpga64), and `aec = addrValid` (762-768: 1 when phi=0 or when BA has been low for at least 3 cycles; port 742).
- baLoc consumers: DMA takeover (511), cpuHasBus (530, 553), T65 rdy (996).
- ba_reu consumers: REU post-access BA checks at 480, 496, 506, 512.
- aec consumers: buslogic currentAddr select (buslogic:300-304), vicDiAec and colorDataAec (697-698).

**dma_\* (fpga64 ports 111-128)**
- Produced by the REU (multitini_c64_reu.sv:76-83): req, valid, idle, peek, last_write, addr, write, wdata. Connected at adapter 528-534.
- In fpga64:
  - dma_req sets dma_active at EXT1/EXT5 (466), is used at VIC3 (510, 521) and in the peek release (547), and suppresses turbo (1080);
  - dma_valid/idle/peek/last_write are latched at VIC3 (470-473);
  - dma_we feeds the post-write latch (538);
  - dma_addr/dout/we enter the CPU mux (1094-1096);
  - dma_ack is produced at 1026; dma_din = cpuDi (1100).
- The REU consumes dma_ack (reu.sv:229, 260, 305) and dma_rdata (230).
- dma_req also gates the REU register window off (adapter:430), as in MAME.

**memory_wait (cart_stall)**
- Produced by cart_memory core_stall (cart_memory.sv:67-69), adapter:504. memory_wait_q is at 410.
- Consumers: preCycle hold (415), the phi0/DMA process (463), the enables (566), VIC enaPixel/enaData (710-711), read_early (731), lastVicDi (763), the VIC bank latch (780), pixel enable (793), SID ce (828), CIA phi2 (870-871, 900-901), CPU enable and bus_start (989, 992), port_tick (1006), ramCE (1021), dma_ack (1026), cpu_bus_valid (1039), scheduler hold (1054), io_enable/turbo (1068).

**systemAddr, ram_addr (buslogic currentAddr)**
- Goes to:
  - local RAM machine_addr (adapter:393);
  - content loader core_addr for the $FFCF check (adapter:337; content_loader:503);
  - cartridge addr_in (adapter:492);
  - colour RAM address (fpga64:600).
- ramDout goes to RAM machine_data and cartridge data_in. It is lastVicDi when a CPU write hits $0000/$0001 (1015).

**Other facts in this area**
- [V] multitini_c64_memory_arbiter.sv is listed in hdl_sources.txt:57 but is not instantiated in production. Only the testbenches use it (tb_c64_reu_machine.sv:157, tb_c64_memory_arbiter.sv).
- [V] T65 BusStart is accepted but unused ("interrupt decisions use Enable only", T65.vhd:143). T65 samples IRQ_n, NMI_n and Rdy, and loads DI, only on Enable edges (391-476, 730-797). Reset is asynchronous on Res_n with a 2-clock internal delay (355-364).

## 2. ROMs, colour RAM and the PLA

**BASIC/KERNAL ROM**
- `kernel_c64` is a dprom with 14-bit address (16 KiB), in buslogic:136-149.
- BASIC is at 0x0000–0x1FFF and KERNAL at 0x2000–0x3FFF (loader.sv:57).
- Read address: `cpuAddr(14) & cpuAddr(12:0)` (147). Only the CPU and DMA read it (cpuAddr is dma_addr during DMA); the VIC never does.
- [V] It is written only by multitini_c64_loader (rom_wr when bios="00", buslogic:119) from MT_LOAD_C64_BASIC/KERNAL objects. core_enable is forced to 0 while rom_wr is active (adapter:378-385), so the machine is held in reset during ROM writes.
- dprom has one write port on wrclock and one registered read port on rdclock, both tied to clk32 (dprom.vhd:39-51). There is no INIT file.

**Character ROM**
- `chargen` is a dprom with 12-bit address (4 KiB), in buslogic:122-134. It is written when bios="01" (bios = {0, rom_bank}, adapter:499).
- [V] It has one read port, addressed by currentAddr(11:0), shared in time by:
  - VIC fetches (vicCharLoc when the VIC owns the bus and vicAddr(14:12)="001", buslogic:306-307 and 329);
  - CPU reads with I/O off (cs_CharLoc, 257-258);
  - DMA reads through the same path.

**Colour RAM**
- `spram` 1K×4, one RAMB18 (fpga64:592-603).
- addr = systemAddr(9:0), shared by VIC and CPU/DMA.
- Written by `cs_color & pulseWr_io`. pulseWr_io is registered from phase CPUC (666-676), so writes happen only in the native slot.
- Read data goes to the VIC (diColor when aec=1, 698) and to the CPU (dataToCpu(3:0); the upper nibble is lastVicData, buslogic:163 and 175).
- On a write, q returns the written data.

**PLA per access (buslogic:197-315)**
- When cpuHasBus=1, the CPU or DMA address is decoded with bankSwitch = {CHAREN, HIRAM, LORAM} and exrom/game. With exrom = game = 1, i.e. no cartridge:
  - $E000-$FFFF: KERNAL on reads if HIRAM; otherwise RAM. Writes always go to RAM.
  - $D000-$DFFF:
    - HIRAM=0 and LORAM=0: RAM.
    - CHAREN=1: I/O. $D0-$D3 VIC, $D4-$D7 SID, $D8-$DB colour RAM, $DC CIA1, $DD CIA2, $DE IO1, $DF IO2.
    - Otherwise: char ROM on reads, RAM on writes.
  - $A000-$BFFF: BASIC on reads if HIRAM and LORAM; otherwise RAM.
  - $8000-$9FFF and every other area: RAM.
  - $0000-$0FFF: always RAM in the PLA. The 6510 port intercepts $0000/$0001 for CPU reads only (cpu_6510.vhd:92-94).
- I/O chip selects are ANDed with io_enable (318-324). That signal is `io_enable and bus_access_valid` (fpga64:643), set at EXT0 and cleared on any retire (1069-1072).
- When cpuHasBus=0, the address is vicAddr if aec=1, otherwise cpuAddr. Only RAM or char ROM is selected; there is no I/O decode (298-314).
- [V] The PLA decodes DMA accesses with the CPU's live cpuIO banking. Examples: REU transfers to $D000 hit I/O chips when $01 maps I/O, and DMA reads of $A000 or $E000 return ROM when it is mapped. tb_c64_reu_machine checks "Character-ROM DMA read uses chargen rather than RAM or color I/O" (line 251).

## 3. REU DMA, the $FF00 trigger and register snooping

**Registers**
- [V] REU registers sit in IO2: `reu_io_select = reu_active & !reu_dma_req & cart_iof` (adapter:430).
- Reads come back through io_ext/io_data into dataToCpu (adapter:520-521; buslogic:188-191).
- Writes are taken from the completed CPU transaction (reu.sv:153-211).
- Reading status ($DF00) clears it (157). It is a read side effect, so it must be executed exactly once.
- reu_active requires that no cartridge is loaded or loading (adapter:427).

**Start of a transfer**
- Writing $DF01 with bits 7 and 4 set starts at once (reu.sv:163-170). active is set on the edge after the retire, and dma_req is combinational from active (76).
- Writing $DF01 with bit 7 set and bit 4 clear arms the $FF00 trigger (162).
- Any later completed CPU write to $FF00 then starts the transfer (213-220). The write itself goes to RAM; writes to $E000-$FFFF always go to RAM without a cartridge (buslogic:221-231).

**Native DMA sequence**
- At EXT1/EXT5: `dma_active <= dma_req | post_pending | wait_vic` (465-466).
- At VIC3:
  - initial takeover obeys VIC BA (510-517);
  - each access uses the native CPU slot, and dma_ack fires at retire (1026);
  - after each access, VICE-style BA checks run with one cycle of write grace (479-509);
  - the final peek after a penultimate verify failure uses the CPU0–CPU3 sub-slot without advancing time (1029-1034, 541-554), and drives the VIC/CIA read_early strobes (730-731, 871, 901).
- A slot with dma_valid=0 holds the CPU while consuming native time, used while the DDR cache misses (1025; reu.sv:77).

**What the CPU does during DMA**
- [V] Nothing. T65 Enable and bus_start are gated by dma_active (989, 992). No IRQ or NMI is sampled, because T65 samples only on Enable. The CPU's issued address stays on cpuAddr_pre while the mux shows dma_addr. On release, the CPU resumes in the next native slot after the post-access BA checks.
- [V] REU completion raises IRQ through irq_ext_n (adapter:534; reu.sv:84, 315-316).

**Existing timing contract**
- [V] docs/C64_EXPANSIONS.md (~line 96) records a budget of at most 64 fabric clocks per DDR transaction, "at least four native cycles between register writes (stock 6510 absolute stores)", and the unchanged phase-15 readiness deadline. It states that "accelerated register programming remain[s] unqualified".
- [I] An island that programs REU registers on consecutive native slots may miss the prefetch window. The result is extra invalid slots: slower native-time DMA, not wrong data. This needs a test.

## 4. Pause, menu, snapshot, PRG load, run injection and reset

**Pause**
- [V] fpga64 samples `pause` once every four native cycles, at phase 7 with rfsh_cycle=0. It sets sysEnable=0, which forces sysCycle=EXT4, and pause_out = ~sysEnable (404-431).
- Sources: `prg_install_pause | pause_request` (adapter:504). The menu pause is a level synchronised into c64_clk (top:1159-1174).
- core_paused stops the 1541 (adapter:572), video_sync (621) and the run injector (371). TOD also pauses (963).
- [V] The content loader does not wait for the pause acknowledgement before its first PRG write (prg_install_pause is combinational in STATE_STREAM, content_loader:377-381). Loader writes have priority in local RAM (local_ram:22-26).

**PRG load**
- [V] Sequence: STATE_PRG_RESET holds core reset for 255 clocks (374, 586); STATE_PRG_WAIT_BASIC lets the KERNAL run until systemAddr == $FFCF (503); then RAM is written directly; then the BASIC pointer writes at $2B-$32 and $AC-$AF (504-525); then the RUN request.

**Run injector**
- [V] It counts 1,280,000 c64_clk cycles between key events while not paused (run_injector:7, 50) and feeds ps2_key to the keyboard and CIA1. It does not depend on the CPU.

**Reset**
- [V] core_reset_n (adapter:306-309) goes into fpga64; `reset` is registered at phase 31 (433-440).
- It resets the T65 (asynchronous Res_n), the 6510 port (synchronous), the VIC, CIAs, SID and the phi0/DMA state.
- RAM is never cleared (local_ram:20).
- ROM loads, CRT streaming and fatal errors all hold reset.

**Snapshot and debug**
- [V] No C64 CPU register readout and no memory peek path exist in RTL or firmware. The "peek" in build/ and test names is the REU verify peek, or simulator-only RAM peeks (scripts/c64sim).

## 5. What a CPU island must change, and the risks

### 5a. Where a proxy fits and what it must guarantee

[I] Put a proxy inside fpga64 in place of the cpu_6510 instance (984-1011). It drives cpuAddr_pre, cpuDo_pre and cpuWe_pre from a register clocked by c64_clk. That keeps every existing native path c64→c64, which matters because the PLA paths are about 22 ns today.

The proxy must guarantee:

1. **Request timing.** The request is present by the phase-15 edge and held stable through phase 31.
2. **Completion.** A transaction completes when cpu_bus_valid fires. A RDY-held read does not complete and is retried; with DMA active nothing completes.
3. **Captured data.** At the retire edge it captures dataToCpu (cpuDi), the IRQ term `irq_cia1 & irq_vic & irq_n & irq_ext_n` (995), the NMI term `irq_cia2 & nmi_n` (993), and Rdy = baLoc (996). It returns them to the island as one bundle.
4. **No replay.** Each forwarded read executes exactly once. Read side effects exist in CIA ICR $DC0D/$DD0D, VIC collision registers $D01E/$D01F (VIC myRd:317-318) and REU status. Idle cycles must never repeat the last I/O address.
5. **Idle cycles.** When the island has nothing to forward, the native slot must still issue (VIC phi2, finding 1). Use a dummy RAM read at an address that is RAM in every mapping, e.g. $0002–$0FFF. The alternative is to suppress the slot only when cpuHasBus=1.

**Exact mode (stock)**
- [I] Every CPU cycle is forwarded. The island T65 gets exactly one Enable per bundle, with the native-sampled DI, IRQ, NMI and Rdy, plus the native port decay tick in the same bundle.
- T65 state depends only on its inputs at Enable edges and on Res_n. SO_n is tied to 1 and BusStart is unused. The island T65 therefore follows the same state sequence as today's T65 even though its Enable comes a little later.
- This only holds if every request meets the phase-15 deadline (one native cycle round trip). A missed slot loses a CPU cycle. Add a sticky "missed slot" diagnostic.
- Exact mode lets the VICE/MAME suites test that equivalence directly, with cpuAddr_pre as the observation point.

**Accelerated mode: what is forwarded**
- I/O space when mapped: CHAREN=1 and (HIRAM or LORAM) at $D000-$DFFF. That covers VIC, SID, colour RAM, CIA1, CIA2, IO1, and IO2 with the REU registers.
- All writes to $0000/$0001.
- Writes to $FF00 while the REU is enabled (or always; it is a rare address).
- Everything, when a cartridge is loaded or loading. Simpler: disable acceleration and use exact mode, as Smart turbo does now.

**Accelerated mode: what stays local**
- All RAM, BASIC/KERNAL, char ROM, and $0000/$0001 reads (the port is internal; external input is constant).

### 5b. Places that must change

1. **Clocking.**
   - scripts/create_project.tcl:481-494 (clk_wiz_c64; only CLKOUT0 is used today).
   - The block-design port and a reset block for the new clock (mirror reset_c64 / c64_aresetn).
   - Top-level ports and the machine-clock group in hdl/board/multitini_timing.xdc:51-60.
   - [V] Generated MMCM today: D=5, M=43.875, O0=41.75 (fractional), i.e. 31.52695 MHz nominal, −32 ppm (build/c705r2/.../zynq_ps_bd_clk_wiz_c64_0_clk_wiz.v:126-134). O0/2 = 20.875 is not an integer, so CLKOUT1 cannot give exactly 2x without a new plan. Integer options from the scratch search (Fin 150 MHz):

| Standard | Fast clock | D | M | VCO (MHz) | c64 divide | fast divide | c64 error |
|---|---|---|---|---|---|---|---|
| PAL | 2x 63.056 MHz | 9 | 56.75 | 945.83 | 30 | 15 | −5.6 ppm |
| PAL | 3x 94.58 MHz | 9 | 56.75 | 945.83 | 30 | 10 | −5.6 ppm |
| PAL | 4x 126.09 MHz | 5 | 33.625 | 1008.75 | 32 | 8 | −143 ppm |
| NTSC | 2x 65.45 MHz | 5 | 24 | 720 | 22 | 11 | +0.2 ppm |
| NTSC | 3x 98.18 MHz | 5 | 36 | 1080 | 33 | 11 | +0.2 ppm |
| NTSC | 4x 130.9 MHz | 11 | 48 | 654.5 | 20 | 5 | +0.2 ppm |

   - The VCO, PFD and multiply limits I used are the 7-series -2 MMCM limits as I remember them (VCO 600–1440 MHz, PFD 10–450 MHz, M 2–64 in 0.125 steps). Check them against DS187.
   - Alternative with a repo precedent: derive c64_clk from the fast clock with a SYNC BUFGCE, as the IIgs does (multitini_apple_iigs_adapter.sv:49; apple_iigs_timing.xdc:17-25, `-edges {1 2 9}`). Risk: the duty cycle drops to half a fast period. video_sync.vhd:50 uses `falling_edge(clk32)`, so those paths would get half a fast period.
2. **fpga64_sid_iec.vhd.**
   - Replace the CPU instance (984-1011) with the proxy. Export cpuIO from a native shadow port updated by forwarded $0000/$0001 writes; it only needs direction and data registers (6510_port:39, 74-90).
   - Send port_tick (1006) to the island, inside the exact-mode bundle and as a synchronised pulse in accelerated mode.
   - Remove or keep the turbo extra-slot logic (1047-1048, 1065-1092, scheduler 48-58). The island replaces Smart 2x.
   - Keep the `ramDout <= lastVicDi` rule for $0000/$0001 (1015) by forwarding those writes.
   - In accelerated mode, add a forwarded-access BA rule (risk 2).
3. **Scheduler.** The native slot must still issue every cycle (42-43). Remove the extra and dense slots, or keep them only for fallback.
4. **buslogic.** Keep the native copy unchanged for VIC, DMA and forwarded access. The island needs its own decode for the no-cartridge map (subset of lines 217-297) and the forward rules above, using the island's port pins.
5. **ROMs.** Make kernel_c64 and chargen true dual-port. Port A is the island's read port, enabled only on island reads. Port B is native: the existing read address plus loader writes (dprom.vhd:39-51; buslogic:122-149). The alternative is separate island copies of the ROMs (+5 RAMB36) with loader writes fanned out.
6. **RAM.** Change multitini_c64_local_ram.sv to true dual-port.
   - Port A: the island, read and write.
   - Port B: today's machine port (VIC0, the native slot, DMA, forwarded writes) plus the loader.
   - Instance at adapter:387-395. The 16-RAMB36 build gate (scripts/build_and_export_xsa.tcl:38-47 and check_c64_local_ram_synthesis.tcl) still applies, and the cell name main_ram_i must still match.
7. **Colour RAM.** Keep it native and forwarded; no change. Optionally make it true dual-port with island writes in accelerated mode only. Reads cannot be local exactly, because the high nibble is the floating bus.
8. **REU.**
   - The $FF00 snoop (adapter:439) depends on forwarding $FF00 writes.
   - The island must stop from a trigger write until the DMA is fully finished.
9. **Content loader.** core_addr == $FFCF (content_loader:503; adapter:337) needs a flag exported from the island, synchronised and sticky, or the island must run in exact mode during STATE_PRG_WAIT_BASIC.
10. **Pause.**
    - The island must stop on the synchronised core_paused and acknowledge it.
    - The content loader should wait for that acknowledgement before writing RAM (it does not wait today).
    - Disable island BRAM port enables while paused or in reset, not only T65 Enable.
11. **Reset.** Use a reset synchroniser into the island domain. Reset the proxy request/ack state in both domains together, so a stale ack cannot complete a new request.
12. **Interrupts.** Route irq_n and nmi_n to the island and include them in the ack bundle (risk 3).
13. **Turbo guard and mode control.**
    - Guard: adapter:556-568, used at 513.
    - Cartridge gate: local_turbo_allowed (adapter:515).
    - Control ABI: MT_CONTROL_C64_CPU_TURBO_ENABLE is a single bit (firmware/include/multitini_regs.h:90; main.c:244, 2482, 4594). A speed field needs new ABI space, for example in c64_expansion_config, where only bits 3:0 are used.
14. **Tests that patch the RTL as text.** Each needs updating after a refactor:
    - scripts/test_c64_machine_schedule.py:35-89 (markers `port(\n`, `\nbegin\n`, `\twe => cpuWe_pre,\n`, and signals cpuAddr_pre, cpuIO, lastVicDi, cpu_cyc, enableCpu, …);
    - test_c64_reu_machine.py:93-137;
    - test_c64_machine_interrupts.py:63-77;
    - test_c64_vice_bus.py:19-32 and 81 (regex slices of `cpu_cyc <=`, `enableCpu <=`, `elsif dma_write_delay = '0' then`);
    - test_c64_vice_cpu.py:233-236 (cpu_6510 `R_W_n   => localWe,` marker);
    - the hdl/sim/tb_c64_*.sv benches that instantiate fpga64_sid_iec and local_ram directly.
15. **Docs.** docs/C64_RAM_CPU.md states "No new clock or clock-domain crossing is added". AGENTS.md requires a review of every new clock, reset, crossing and timing rule.

### 5c. Risks and hazards

1. **BRAM collisions.**
   - [V] The XPM 2025.2 model treats only common-clock READ_FIRST as collision-safe. For independent clocks it warns that "data outputs and memory content may be corrupted" (xpm_memory.sv:221-223, 3684-3723).
   - [I] Only one case can occur at run time: an island write on port A while port B reads the same address. DMA and loader writes happen only while the island is halted or paused.
   - Port B is enabled only at phase 12 and phase 28 (plus DMA/loader). With related clocks, the island can write only on fast edges that do not coincide with a c64 edge (or stall writes on the edges where port B is enabled). That avoids collisions deterministically.
   - With an unrelated clock this needs synchronised phase hints with margins. Use WRITE_FIRST or NO_CHANGE on the island port and confirm against UG473 "Conflict Avoidance".
2. **More than 3 write cycles while BA is low.**
   - [V] A native write gets the bus whenever cpuWe_pre=1, even if BA is low (530, 553). The real 6510 never writes more than 3 cycles in a row, so this never happens while AEC is low.
   - [I] Forwarded accelerated writes can arrive back to back. With aec=1 in phi2 (baCnt(2)=1), the CPU address would replace the VIC's character-pointer fetch and corrupt the badline.
   - Rule: in accelerated mode, forwarded accesses need baLoc=1. The native rule stays only in exact mode.
3. **Re-entering an IRQ after its acknowledge.**
   - [I] After a forwarded LDA $DC0D or STA $D019, the island may run RTI and CLI in a fraction of a µs and still see the old asserted IRQ, because of synchroniser lag or the source's own one-cycle delay. Handlers that do not check the source would then run twice.
   - Fix: the ack carries an IRQ/NMI sample taken after the access has taken effect, and the island uses that sample until its synchroniser has caught up. With related clocks this is deterministic.
4. **DMA start window.** The island must not run local cycles between a $DF01 or $FF00 write and DMA takeover. Code like `STA $DF01 / LDA target` would read stale data.
   - [V] reu.active is set on the clock after io_valid (reu.sv:168, 76).
   - [I] So the ack for these writes should be sampled at retire+1 and held until `dma_req=0 & dma_active=0` (including the post-access BA states 479-509).
5. **IEC guard latency.**
   - [V] The guard is combinational on IEC/parallel transitions with a 0.5 s holdoff (turbo_guard:12, 37-50).
   - [I] At 64x the island could run more than 100 CPU cycles before switching to exact mode.
   - Fix: every forwarded ack carries turbo_allowed, and the island drops to exact mode before its next cycle. The ack for a CIA2 write should be sampled after the guard has seen the new line state. Transitions started by the drive are seen at the next forwarded access, since the CPU can only observe the lines through forwarded $DD00 reads.
6. **Banking seen by DMA and forwarded access.** The native PLA decodes forwarded accesses and DMA with the native cpuIO copy. All $0000/$0001 writes must be forwarded in program order, or each forwarded access must carry the island's bankSwitch. Otherwise a forwarded I/O access could decode as RAM.
7. **Floating bus.** [I] Exact for forwarded I/O (unmapped $DExx, colour RAM high nibble, RAM under $0000/$0001). In accelerated mode the FLI colour (colorDataAec from cpuDi at 698) comes from proxy dummy reads instead of opcode fetches. This is visible only in FLI pictures, and only when accelerated.
8. **Cartridge.**
   - [V] Acceleration is off with cart_loaded. The REU is off with cart_loaded or cart_loading.
   - [I] Mode changes on CRT load or detach must happen in reset or in exact mode. CRT_DETACH clears cart_loaded without a core reset (content_loader:555-561), and cartridge.sv resets on a cart_id change (161).
9. **REU readiness under fast programming.** See section 3: it lowers DMA throughput, but correctness is unaffected.
10. **Forwarded-I/O cost.** [I] An isolated forwarded access waits on average about 0.5 µs for phase 28. Back-to-back forwarded accesses cost 1 µs each.
    - Example at 64x local speed: per 100 accesses with 2% I/O, 98/64 + 2×0.75 ≈ 3.0 µs, i.e. about 33x effective.
    - With 5% I/O: about 5.2 µs, i.e. about 19x.
    - Badlines and sprites also block forwarded accesses (risk 2).
    - Posted writes through an ordered FIFO would help, because most game I/O is writes (SID, VIC, colour RAM). REU-trigger writes, IRQ acknowledges (risk 3) and $0000/$0001 still need ordering barriers.
11. **Island speed versus clock.**
    - [I] With a 1-clock synchronous BRAM read and T65's combinational address, one CPU cycle takes 2 island clocks (issue, then retire), like today's dense 2-clock slots. So 2x c64_clk ≈ 31.5 MHz CPU ≈ 32x; 3x ≈ 48x; 4x (126 MHz) ≈ 64x.
    - Reaching 64x needs a 126 MHz island, or one clock per cycle (opposite-edge BRAM or a pipelined T65).
    - [V] T65 is 1,051 LUTs and 167 FFs. In the current worst path, the T65 part (ramDin to BAL) is about 4 ns. Its fmax as an island is not measured here.

## 6. BRAM count and dual-port feasibility

**Measured (build/c705r2/c64-pal/reports/utilization.rpt)**
- Whole design: 114 RAMB36 + 17 RAMB18 = 122.5 of 140 RAMB36 equivalents. About 17.5 RAMB36 equivalents (35 RAMB18) are free.
- C64 adapter: 39 RAMB36 + 10 RAMB18. Breakdown:
  - main RAM: 16 RAMB36. It is flattened into the adapter; cells are ram_reg_{0..1}_{0..7}, i.e. 8 bit columns × 2-deep cascade (32K×1 pairs).
  - core_i: 7 RAMB36 + 1 RAMB18 = KERNAL/BASIC 4 + char ROM 1 + SID tables 2, plus colour RAM as one RAMB18.
  - drive: 16 RAMB36 + 9 RAMB18.
- Outside the adapter: video bridge frame banks 64 RAMB36, text overlay 9, framebuffer 2 + 4 RAMB18, video test 3 RAMB18.
- Logic: 24,763 LUTs and 22,998 FFs in total (46.5% / 21.6% of the 7020).

**Feasibility**
- [V] The main RAM is inferred today with port A read-only (ENARDEN) and port B write-only (ENBWREN) on one clock (timing_summary.rpt paths to ram_reg_1_4/ENARDEN and ENBWREN).
- [I] 7-series RAMB36E1 and RAMB18E1 support true dual-port with independent port clocks, up to 36 or 18 bits per port. The 64K×1 cascade per bit works in true dual-port mode (per-port RAM_EXTENSION settings). An 8-bit, two-port 64 KiB array therefore still uses 16 RAMB36. The 16 KiB ROM (4 RAMB36), 4 KiB char ROM (1 RAMB36) and 1K×4 colour RAM (1 RAMB18) also need no extra BRAM.
- Inference needs the two-clock true dual-port template, or XPM_MEMORY_TDPRAM with CLOCKING_MODE="independent_clock", which also allows setting WRITE_MODE per port.
- Optional extras fit in the 17.5 free: separate ROM copies for the island (+5 RAMB36), a colour RAM shadow (+1 RAMB18), a posted-write FIFO (LUTRAM).

## 7. Open questions for the lead

1. Should the island replace the native CPU completely, with exact mode as proxy-every-cycle (one T65 and no state transfer), or should a second T65 be added for accelerated mode? A second CPU would need a register-state transfer path, and T65 has no load interface.
2. Related clock (MMCM plan above, or a BUFGCE-derived c64_clk) or unrelated clock? The related option allows deterministic collision avoidance, IRQ ordering and exact-mode equivalence without metastability arguments.
3. Is PAL at −143 ppm acceptable for a 4x related clock, or should 4x use the BUFGCE approach?
4. Is accelerated mode allowed to differ in floating-bus, FLI-colour and REU-throughput details? Exact mode keeps all of them.
5. Should posted I/O writes be in scope? Without them, I/O-heavy software probably cannot get past about 20–35x however fast the island runs.

Not verified in this step: T65 fmax in an island, the exact UG473 collision rules, the MMCM limits against DS187, and any hardware behaviour.