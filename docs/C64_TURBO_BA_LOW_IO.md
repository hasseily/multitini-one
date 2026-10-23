# C64 turbo v2: internal I/O while BA is low, and ordering across the fast I/O path (sub-design)

Sub-design written on 2026-10-23 for the v2 draft of [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). The current design (v3) merges it; where the two differ, C64_TURBO_DESIGN.md wins. Design stage only: no HDL, firmware, script or build file was changed for it, no build or simulation was run, and nothing here is board-tested. Paths to throwaway session files were removed. `issueN.txt` means a downloaded copy of GideonZ/1541ultimate issue #N with its comments; other bare file names that are not in this repository also name downloaded copies read during the design. Those copies are not kept.

Arithmetic: `balow_calc.py`, kept with its output in [C64_TURBO_MODELS.md](C64_TURBO_MODELS.md).

Tags:
- **[V]** verified in the repository (file:line) or in a cited document or commit.
- **[M]** measured or observed by a third party on U64/C64U hardware.
- **[I]** inference or design choice.
- **[O]** open. The experiment that settles it is named.

No hardware support is claimed. Baseline = the v1 turbo design, an earlier draft of C64_TURBO_DESIGN.md that is not kept. Phase notation is the baseline's: native phases p = 0..31, e_p = the c64 edge ending phase p, R = e_31, fast clock f = 2p + h.

User decisions applied: Q1 (VIC/SID at turbo rate; internal I/O proceeds while BA is low with badline timing disabled), Q2 (NTSC in scope), Q3 (local `$00/$01` writes), Q4 (disk auto-slow default On), Q5 (global settings bytes 26-27).

---

## 1. Summary

1. **I/O-only ownership.** With badline timing disabled, a forwarded access to an internal device gets a second ownership type when `baLoc` = 0. The CPU side gets the chip selects, decoded from its own address and carried port. The VIC keeps the shared address bus, RAM, colour RAM and character ROM. The access runs in the unchanged phase-28/31 native slot and completes at R of the same native cycle. That holds in BA-low cycles 1-3 (AEC still high) and in cycles 4+ (VIC owns φ2), for badlines and for sprite DMA.
2. **Eligible targets:** CIA1, CIA2, I/O1 and I/O2 with no cartridge present (REU registers, a `$DE00`/`$DF00` SID, open bus), and the `$FF00` REU-trigger snoop (section 3.5). VIC and SID are eligible only in a build that forwards them (fallback `FAST_FIO=0`). In the target build they use the fast path, which never touches the native bus.
3. **Never eligible:** colour RAM (shares the address bus), cartridge windows and cartridge I/O (cartridge.sv uses the shared address and RAM write strobe), forwarded `$00/$01` writes (fallback `FORWARD_PORT_WRITES=1`), and any REU DMA.
4. **D5 is retired.** No internal target waits for BA when badline timing is disabled. Deviation D7 (FLI colour nibble in turbo) is extended by one case.
5. **Badline timing enabled:** nothing new. The island halts every class while BA = 0, and the I/O-only flag is never set. One requirement goes to the fast-path design: its request issue must include `~halt_q`.
6. **REU:** the triggering write may now land in a BA-low cycle. DMA takeover still honours BA and the VICE post-access checks, unchanged. The island stays halted until `ext_dma_hold` falls.
7. **Ordering:** eight rules (O1-O8, section 3.8) cover local RAM/colour/port writes, fast VIC/SID writes through any queue, fast reads, φ2 forwards of either ownership type, DMA, pause, reset and mode switches. With a depth-0 (non-posted) fast write path, rules O2-O4 need no logic.
8. **#665:** the U64 bug was external-bus (real SID socket) accesses blocked during badlines. Internal devices were already unaffected [M]. The fix let the bus bridge start one slot late and complete in the same φ2. Multitini has no external bus, and its internal φ2 accesses complete at R of the same native cycle whether BA is high or low. So a timer-driven digi with `$8F` and the screen on should not growl [I]. Bench E-BA2 and board gate G-BA1 decide this.
9. **Cost:** about +45 LUT and +10 FF. No new clock and no new crossing type. One new frame bit on the existing X2 path.

---

## 2. Verified facts

### 2.1 Native ownership in FPGA64 (all `hdl/core/c64/ports/fpga64_sid_iec.vhd` unless named)

- [V 468-533] At the VIC3 edge (e_15), with no DMA: `cpuHasBus <= '1'` if `baLoc = '1' or cpuWe_pre = '1'` (530-531). DMA branches take priority (510-524). `cpuHasBus` and `phi0_cpu` clear at R (555-558).
- [V 535-554] The DMA verify-peek release can set `cpuHasBus` in CPU0..CPUB, only after a DMA.
- [V 1039] `cpu_bus_valid <= enableCpu and not dma_active and cpuHasBus and not memory_wait_q`.
- [V scheduler:42-43; fpga64:1021-1023] The native slot issues at phase 28 every cycle (`io_enable` = 1 in the CPU half, 1069-1072). With `cpuHasBus` = 0, `ramCE` serves the VIC's φ2 fetch.
- [V 1018; local_ram.sv:22-26] `ramWE = systemWe` from CPU0. A RAM write needs `machine_ce & machine_we & machine_store`.
- [V 563-587] `enableCia_n` is high in phase 29 (CIA read/write act at e_29). `enableCia_p` and `enableSid` act at e_0.
- [V 666-676] `pulseWr_io` = `cpuWe` at CPUC with `cpu_cyc` and `bus_access_valid`. It does not depend on `cpuHasBus`. It writes the SID (828-833) and colour RAM (`cs_color and pulseWr_io`, 599).
- [V 681-698] `vicBus <= cpuDo` when `cpuWe & cs_vic` during φ2, else `$FF`. `vicDiAec = vicBus` and `colorDataAec = cpuDi(3:0)` when `aec` = 0.
- [V video_vicII_656x.vhd:762-768] FPGA64's `aec` is the VIC's `addrValid`: 1 in φ1, or in φ2 once `baCnt(2)` = 1. It is the inverse of the hardware AEC pin.
- [V video_vicII_656x.vhd:341-353] `baCnt` counts φ1 data edges (e_15) while `baLoc` = 0 and saturates at 4. So φ2 has `aec` = 0 in BA-low cycles 1-3 and `aec` = 1 from cycle 4.
- [V video_vicII_656x.vhd:300-315] The VIC latches `cs`, `we`, address and data on every clock while `phi` = 1 (e_16..e_31). `myWr_a/b/c` need `we_r` = 1. `we_r` is re-latched from e_16 of the next cycle, so a native VIC write is applied by e_15 of the following cycle at the latest (the last pixel edge before e_16) [V structure; I that all three strobes fall in the window, which the passing VICE suites imply].

### 2.2 The shared address bus (multitini_c64_buslogic.vhd)

- [V 217-297] With `cpuHasBus` = 1, `currentAddr = cpuAddr` and the full PLA runs, including I/O chip selects and `systemWe = cpuWe`.
- [V 298-314] With `cpuHasBus` = 0: `currentAddr = vicAddr` if `aec` = 1, else `cpuAddr`. Only the VIC RAM select (`cs_ramLoc`), `vicCharLoc` or Ultimax are set. No I/O select, `systemWe` = 0.
- [V 153-193] `dataToCpu` priority is char ROM > ROM > RAM > VIC > SID > colour > CIA1 > CIA2 > cartridge paths > `io_data`. The default is `lastVicData`. In the VIC branch `cs_ramLoc` = 1, so today `dataToCpu` = RAM data.
- [V 317-324] `cs_ram` includes `cs_ramLoc`. Chip selects are ANDed with `io_enable`.
- [V fpga64:600; buslogic:132] Colour RAM and character ROM are addressed by `systemAddr`/`currentAddr`, the shared bus.

### 2.3 Devices

- [V mos6526.v:90-91] CIA `rd`/`wr` = `phi2_n & !cs_n & rw`. Address `rs = cpuAddr(3:0)` and data `db_in = cpuDo` (fpga64:876-877). These do not depend on `systemAddr`.
- [V mos6526.v:132, 179, 517-527] `pa_out` updates at `phi2_p` (e_0). `pcr` clears on any clock with `cs_n` = 0 and `rs` = 1. An ICR read sets `int_reset`, and `irq_n` is released at the next `phi2_p`. ICR writes never release `irq_n`.
- [V fpga64:815-833] SID chip select from `cs_sid`, `ioe_i` or `iof_i` per `sid_mode`. SID address is `cpuAddr(4:0)` and data `cpuDo`.
- [V adapter:427-439] REU: `reu_active = reu_enable & !cart_loaded & !cart_loading`. `reu_io_select = reu_active & !reu_dma_req & cart_iof`. Register access is `cpu_bus_valid & reu_io_select`. The `$FF00` trigger is `cpu_bus_valid & write & addr == $FF00`, regardless of mapping.
- [V adapter:520-521] REU read data returns through `io_ext`/`io_data` into `dataToCpu`.
- [V multitini_c64_reu.sv:76, 155-170, 213-220] `dma_req` is combinational from `active`. `active` is set at the R edge of the `$DF01` (bits 7 and 4) or armed `$FF00` write. A `$DF00` read clears status at R.
- [V cartridge.sv:105-123, 157-161; adapter:492] The cartridge takes `addr_in = systemAddr` and `mem_write = ramWE`, and strobes on IOE/IOF edges. With `cart_id` = `$FF` no mapper case runs.

### 2.4 Real C64 and U64 rules

- [V 1541ultimate commit 08c8033d, slot_master_v4.vhd comment quoting the PLA equations] On a real C64 the PLA's I/O select for **reads** includes BA. In BA-low cycles 1-3 (AEC still high), CPU reads do not select I/O. Writes do. Stock FPGA64 matches: no I/O decode without `cpuHasBus`, and writes take `cpuHasBus`.
- [V turbo_mode.rst:84, 86-95, 167-176] Badline timing does not change BA. External-bus accesses, including real SID sockets, wait for BA = 1. With badline timing disabled, "the CPU can continue even when BA=0, given that the access is to an internal address."
- [M issue665.txt, comment of 2026-03-23T20:47 quoting the reporter] On fw 3.14 with badline timing disabled, "REU, UltiSID, CIA's all seem to be immune to BA shenanigans". Only real SIDs were affected.
- [V GideonZ, issue #710, 2026-06-20] "the U64 sources are not public". The U64 fix is visible only through comments, commit titles and bitstreams.

### 2.5 #665: symptom, cause, fix

- **Symptom** [V issue665.txt]: with real SIDs enabled, `$8F` behaved like `$0F`. A timer-driven digi growled with the screen on. Blanking the screen or using UltiSID removed it.
- **Cause** [V issue665.txt, GideonZ 2026-03-30]: the external-bus bridge could start a cycle only in slot 0 or 32 of a φ2 period. In badline cycles the VIC owns both, so the bridge never started.
- **Fix** [V issue665.txt]: let the bridge start one slot late (slot 1/33). This is "a jitter of 1/64 of a cycle, which still completes at the end of the very same PHI2 cycle."
  - Commits [V clone history]: 7b8de2d0 and 15ae6a8a ("[ISSUE-665] ... Theoretical fix"), 98d3b429 ("Badline elimination (simulated)"), f2a14e51 ("Fixed eDMA during badlines when they are turned off"), 77f3b381 ("Updated bridge timing and REU in U64/U64E2"). All are bitstream-only.
  - The issue was closed "Fixed" on 2026-05-05. Release 3.15 says "VIC-II badline and external-DMA timing has been corrected" (release_3.15.md:190).
- [O] Whether the final fix also changed BA towards the cartridge port, as GideonZ floated on 2026-03-23, is unknown. So is what "eDMA during badlines" changed.

---

## 3. Design

### 3.1 Classes and the eligibility flag

Island classes (baseline 2.5, refined) [I]:

| Class | Addresses | Path | `ioonly` eligible (badline timing disabled) |
|---|---|---|---|
| L | RAM, ROM, CHR, COL write/read, PORT read, PORTW | port A | n/a (local; ignores BA) |
| FIO | `$D000-$D7FF` with I/O visible | fast VIC/SID path (Q1) | n/a (never on the native bus). Fallback `FAST_FIO=0`: forwarded and eligible |
| FWD_IO | `$DC00-$DFFF` with I/O visible | φ2 forward | yes, if no cartridge |
| FWD_FF00 | `$FF00` writes | local write + φ2 snoop forward (3.5) | yes |
| FWD_RAM | fallback forwards: COL (`FAST_COLOUR_RAM=0`), PORTW (`FORWARD_PORT_WRITES=1`) | φ2 forward | no (needs the shared bus) |
| FWD_CART | M7 cartridge windows and cartridge I/O | φ2 forward | no, permanently |

Island flag, registered with the request:

`isl_x_req.ioonly = turbo_q & ~badline_en_q & (cls ∈ {FWD_IO, FWD_FF00} | (!FAST_FIO & cls == FIO))`

Proxy frame bit, loaded at e_15 with the frame:

`frame.ioonly <= isl_x_req.ioonly & ~cart_present`, where `cart_present = cart_loaded | cart_loading`. The bit clears at every R, along with the return to the dummy.

`cart_present` is redundant before M7 (a cartridge forces exact mode, D2). It is kept so that M7 cannot inherit the rule by accident.

### 3.2 The rule

On the edge ending CPU0 (e_16), fpga64 sets `cpuIoOwn` when all of these hold: `phi0_cpu` = 1, `cpuHasBus` = 0, `dma_active` = 0, and `ext_cpu_ioonly` (= `frame.ioonly`) = 1. `cpuIoOwn` clears at R and at reset.

In turbo, `cpuHasBus` = 0 after e_15 with no DMA means `baLoc` was 0 at e_15, because the frame showed the dummy's we = 0 [V 530; baseline 2.6]. So `cpuIoOwn` uses the same BA sample as the native decision and needs no BA logic of its own [I].

While `cpuIoOwn` = 1 [I]:
- buslogic keeps its VIC branch: `currentAddr` (RAM, colour, char ROM address), `dataToVic`, `systemWe` = 0.
- The VIC's RAM select drives only `cs_ram`, as a new `vicRamLoc`. It is not a CPU data source.
- Chip selects are decoded from `cpuAddr`, using `bankSwitch` = the frame's carried port and `ultimax` = 0. No colour-RAM select is ever decoded.
- `dataToCpu` takes the chip, I/O1/I/O2 `io_data`, or open-bus `lastVicData` by the existing priority chain.
- `cpu_bus_valid` = `enableCpu & ~dma_active & (cpuHasBus | cpuIoOwn) & ~memory_wait_q`. So the REU snoop, B0 capture, `done_seq` and the turbo-register snoop all see the access once.
- CIA `phi2_p`/`phi2_n`, `enableSid` and `pulseWr_io` are unchanged.

Why e_16 and not e_15 [I]:
- Nothing needs a chip select in phase 16. CIA acts at e_29. SID and colour RAM act at e_29. The VIC register latch keeps its last value at R. `myRd` falls on pixel edges at e_19 or later. `pcr` only needs `cs_n` low on some clock.
- Deciding at e_16 avoids a new combinational fast→c64 path into the e_15 decision. The flag rides the frame register, which is already loaded at e_15 on the X2 path.

`cpuIoOwn` = 1 always completes at R [I]. The retire fires every native cycle. DMA is excluded by the guard. `memory_wait` needs a cartridge. The pause point is phase 7/8. So no access can half-happen: the chip select is asserted only in a cycle that retires.

### 3.3 Cycle by cycle

**Read**, e.g. `LDA $DC0D`, index 15, badline timing disabled, issued in a native cycle with `baLoc` = 0:

| Edge | Domain | Action |
|---|---|---|
| end f_r | fast | T65 presents `$DC0D`, Enable = 0. Class FWD_IO, `ioonly` = 1, ack class. The island waits for `fvq_empty` (O2), then registers `isl_x_req` and flips `seq` |
| e_15 (end f=31) | c64 | Proxy loads the frame (needs f_r ≤ 30), with `frame.ioonly` = 1. fpga64: `cpuHasBus <= baLoc or 0` = 0, `phi0_cpu <= 1`. VIC φ1 data edge; `lastVicDi <= vicDi` |
| e_16 (end f=33) | c64 | `cpuIoOwn <= 1` |
| p17-p31 | c64 | buslogic VIC branch. `systemAddr` = `vicAddr` (BA-low cycle ≥ 4) or `cpuAddr` (cycles 1-3). `cs_cia1` from `cpuAddr`; `systemWe` = 0; `dataToCpu` = `cia1Do` |
| e_28 | c64 | Native slot issues. `ramCE` from `cs_ram` = the VIC's select, so the VIC's φ2 fetch reads RAM at `systemAddr`. `enableCia_n <= 1` |
| e_29 | c64 | CIA `rd`: `db_out <= ICR`, `int_reset <= 1` |
| R | c64 | `cpu_bus_valid` = 1. B0 ← `cpuDi` (CIA data); `done_seq` ← `seq`; frame ← dummy; `cpuIoOwn`, `cpuHasBus`, `phi0_cpu` ← 0. The VIC latches its own φ2 fetch. `enableCia_p <= 1` |
| e_0 | c64 | CIA `irq_n <= 1` |
| e_1 | c64 | B1 capture (hold, blockers, mode, pause) |
| end f=4 | fast | Ack class → ACKWAIT |
| about end f=13 | fast | e_5 marker registered → T65 enable, DI = B0, live IRQ |

**Write**, e.g. `STA $DD00`, `STA $DF01`, `STA $DE1F` (SID at `$DE00`): same edges. CIA `wr` at e_29, with `pa_out` at e_0. SID write via `pulseWr_io` at e_29. REU register update at R. `vicBus` stays `$FF` because `cs_vic` = 0. Writes to `$DD00` still trip the IEC guard, captured at e_1 (baseline 1.9).

**Per native cycle of a BA-low window** (badline or sprite DMA; cycle numbers relative to BA falling):

| Cycle | `baLoc` at e_15 | FPGA64 `aec` in φ2 | VIC in φ2 | Stock FPGA64, CPU read | Stock FPGA64, CPU write | Engine build (D5) | This design (`ioonly`) |
|---|---|---|---|---|---|---|---|
| BA high | 1 | 0 | nothing | normal | normal | normal | normal (`cpuHasBus`) |
| BA-low 1-3 | 0 | 0 | c-accesses only if the badline started mid-line: pointer = `vicBus`, colour = `cpuDi(3:0)` | RDY hold, no chip select, retried (matches the real PLA) | `cpuHasBus` via `cpuWe_pre`, full decode (legal for a 6510) | waits | Chip select from `cpuAddr`. Native RAM read at the forward address (no effect). A mid-line c-access takes its colour nibble from the forwarded data (D7 extension) |
| BA-low 4+ | 0 | 1 | c-access (badline) or s-access (sprite) at `vicAddr` | RDY hold | would take the bus and corrupt the fetch; a 6510 never writes 4 cycles in a row [V C64_TURBO_BUS_CONTRACT.md §5c.2] | waits | VIC keeps `systemAddr`. Chip select from `cpuAddr`. VIC fetch unchanged |
| BA rises | 1 | 0 | nothing | normal | normal | normal | normal |

Throughput:
- One I/O-only access per native cycle, the same φ2 rule as BA-high cycles: cutoff f = 30, resume f = 4 or about f = 13 for the ack class. There is no BA-dependent jitter.
- [I, balow_calc.py] Under D5, a random φ2 forward waited on average 1.20 µs PAL / 1.38 µs NTSC with the screen on, and 4.22 µs PAL with 8 sprites on every line (35.6% of cycles BA-low). With I/O-only the extra wait is 0.

### 3.4 REU registers, triggers and DMA start

- Register reads and writes at `$DF00-$DFFF` are FWD_IO and complete in BA-low cycles. `$DF00` reads clear status once, because completion is `cpu_bus_valid` and sequence numbers prevent replay.
- `$DF01` execute in a BA-low cycle:
  - R: `active <= 1`, `dma_req` = 1.
  - e_1: `dma_active <= 1`, and B1.hold = 1, so the island enters DMA_WAIT without another cycle.
  - Next e_15: `dma_started` = 0 and `baLoc` = 0 → `blocked`, `dma_wait_vic <= 1` [V 510-517].
  - DMA takes the slot at the first VIC3 with `ba_reu` = 1, then runs at 1 byte per φ2 with the VICE post-access checks [V 479-533].
  - The island completes the trigger write only after a live `ext_dma_hold` = 0.
  - Stock differs only in where the trigger can land (BA-high or cycles 1-3). Transfer timing and data are unchanged [I].
- Faster register programming during badlines may start a transfer before the DDR prefetch is ready. That costs extra `dma_valid` = 0 slots (throughput only; C64_EXPANSIONS.md ~96). Covered by baseline E5, now also run inside badlines.
- REU DMA is never I/O-only, including DMA to an I/O address such as a fixed `$D418`. `cpuIoOwn` is set only with `dma_active` = 0, and DMA keeps stock, VICE-matched BA rules.
  - [O] On the U64 with badline timing disabled the REU uses "direct DMA" [V GideonZ #710]. Whether it ignores badlines is unknown. E-BA4 measures it. A difference would become provisional deviation D14.

### 3.5 `$FF00`: local write plus snoop forward [I]

The baseline forwards every `$FF00` write as a RAM-bus access, which waits for BA = 1. To retire D5 fully:
- At the granted enable that would retire the cycle, the island writes RAM[`$FF00`] through port A (the local-write rules apply, including O3), but withholds the T65 enable.
- It registers a FWD_FF00 forward with `ioonly` set when badline timing is disabled.
- The forward completes once:
  - with `cpuHasBus` (BA high): the native RAM write repeats the same byte, which is harmless and collision-free (baseline 4.2);
  - or with `cpuIoOwn` (BA low): no chip select is decoded, because the address is not in `$Dxxx`, so it is a pure snoop.
- `cpu_bus_valid` at R triggers the REU if it is armed. The T65 enable follows normal forward completion, including DMA_WAIT.
- The RAM write precedes R, so a transfer that reads `$FF00` sees the new byte, as on stock [I].
- Fallback generic `FF00_SNOOP=0` restores the baseline behaviour. `$FF00` writes then wait for BA when badline timing is disabled (residual D5r, `$FF00` only).
- U64 `$FF00` timing is [O]; added to E7.

### 3.6 Cartridge I/O1/I/O2: confirmed exact or forwarded [V/I]

- Engine and Q1 builds: `cart_loaded | cart_loading` forces exact mode (D2; adapter:515 rule kept). Exact requests load at e_1 and never carry `ioonly`. The REU is off with a cartridge [V adapter:427].
- M7 (cartridge turbo): FWD_CART is never eligible.
  - cartridge.sv uses `addr_in = systemAddr` and `mem_write = ramWE`. Under I/O-only these would carry the VIC's address and 0.
  - `IO_rom` reads return `ramData` from the shared address.
  - The U64 rule is the same: external-bus accesses wait for BA = 1 [V turbo_mode.rst:174].
- The `frame.ioonly` qualifier `~cart_present` enforces this in hardware.

### 3.7 Badline timing enabled: confirmed, nothing new [I]

- Baseline 1.6/2.8: `halt_q = ~ba_q` halts every island cycle while BA = 0. `isl_x_req.ioonly` = 0, because `badline_en_q` = 1. A forward issued before BA fell is held by the e_15 rule until the first BA-high cycle.
- **Requirement on the fast-path design:** FV-R and FV-W issue must include `~halt_q`. Baseline 2.8 states the gate only for port-A EN.
- `ba_q` lags `baLoc` by up to 2 fast clocks. So up to 2 island cycles may run after BA falls, and up to 2 slots are lost after it rises.
  - This is harmless. BA falls 3 native cycles before the VIC needs φ2, and island cycles do not use the native bus.
  - A fast VIC/SID write 2 fast clocks after BA falls corresponds to a real 6510 write inside its 3-cycle grace.
  - A fast read that late differs only by about 32 ns of timing.

### 3.8 Ordering across all classes

Agents that can observe CPU effects concurrently:
- VIC: port-B RAM and colour fetches, and its registers.
- SID audio.
- CIA ports and IRQs.
- REU: registers and the `$FF00` snoop.
- DMA: only while the island is halted.
- IRQ/NMI lines back to the island.

The T65 issues one bus cycle per enable, in program order. Reordering can only come from different path latencies.

Rules [I]:

| # | Rule | Implementation |
|---|---|---|
| O1 | At most one non-local operation is outstanding, except posted FV-W entries. φ2 forwards (either ownership), FV-R and ack-class FV-W block the island until done | Baseline FSM (FWD, ACKWAIT) plus a FIO wait state |
| O2 | Before a φ2 forward, an FV-R, an ack-class FV-W, or adopting a mode/N/badline change, the island waits for `fvq_empty` | `fvq_empty` crosses c64→fast, registered once (X1). The island registers `isl_x_req` only after it |
| O3 | A local RAM or colour-RAM write waits while a VIC-targeted FV-W is queued. Local reads, PORTW and SID-only queues do not wait | `fvq_vic_empty` (a tag bit per entry). Minimal variant: wait for `fvq_empty` |
| O4 | FV-W entries are applied in FIFO order, at most one per c64 edge, and only on edges where the native frame runs (`frame_run`, not `memory_wait_q`) | c64-side queue |
| O5 | Ack-class accesses complete only after the IRQ term reflects them (3.9) | e_`IRQ_SAFE_PHASE` marker (φ2 forwards); `fio_ack_tgl` (FV-W) |
| O6 | DMA starts only from a φ2 trigger (`$DF01`, `$FF00`), so by O2 the queue is empty. The island stays in DMA_WAIT until live `ext_dma_hold` = 0. Every native-path write made by DMA is applied before resume: SID/CIA at their e_29 strobe; VIC by e_15 of the next cycle (2.1), while `dma_active` clears no earlier than VIC3 of the cycle after the last access | Baseline 2.8 plus bench assertion A6 |
| O7 | Pause: the island stops at a boundary. FV-W apply follows O4. A loaded φ2 forward, including I/O-only, retires at R before the phase 7/8 pause point. Reset clears the queue, proxy, `cpuIoOwn` and island together (≥32 c64 clocks). Mode switch (turbo↔exact, badline setting, N): adopted at an island enable boundary with no forward in flight and `fvq_empty` | Baseline 2.11-2.12 plus `fvq_empty` |
| O8 | `$FF00`: the local RAM write precedes the snoop forward (3.5). Carried port: every forward and DMA decodes with the port current at issue (baseline 5.4) | Island FSM |

With a non-posted FV-W path (the island enables the write cycle only after the c64 side has applied it), O2-O4 hold with no extra logic. The fast-path design chooses its depth. These rules bound any depth.

Hazard check:

| # | Hazard | Removed by |
|---|---|---|
| H1 | FV-W queued, then FV-R reads stale data. This includes causal pairs across addresses: `STA $D40E` then `LDA $D41B`; `STA $D031` then `LDA $D031` (U64 detection) | O2 (wait for empty queue, not an address match) |
| H2 | FV-W queued, then a CIA/REU forward lands first (`STA $D418` then `STA $DD0D`; `STA $D01A` then `LDA $DC0D`) | O2 |
| H3 | `STA $D018` (buffer flip) queued, then a RAM write to the old buffer visible to the VIC first: a one-character glitch | O3 |
| H4 | Local write, then FV-W | none needed; local lands first |
| H5 | Local read after FV-W | none needed; disjoint storage. The class is fixed by the port at issue |
| H6 | `$01` change vs forwards, DMA and queued FV-W | carried port (baseline 5.4); a queued FV-W was issued while I/O was visible and stays valid |
| H7 | Stale IRQ after an acknowledge lets local enables re-enter the handler | O5 (3.9) |
| H8 | DMA reads VIC/SID registers with an FV-W still queued | O6 (trigger is φ2, so O2 applies) |
| H9 | DMA writes VIC registers, then FV-R after resume reads stale data | O6 timing; assertion A6 |
| H10 | Queued FV-W across pause, reset or a mode switch, then an exact-mode native VIC access overtakes it | O4, O7 |
| H11 | An I/O-only attempt that does not complete leaves a side effect | impossible: `cpuIoOwn` is set only in a retiring cycle (3.2). A BA-blocked, non-eligible attempt asserts no chip select [V buslogic:298-314] |
| H12 | BA changes on the e_15 edge itself | both ownership types use the pre-edge sample, as stock. BA falling there leaves 3 cycles of grace; BA rising gives a normal or I/O-only cycle |
| H13 | Eligibility changes between retries (e.g. `cart_loading` rises) | re-evaluated at every e_15 load; each attempt either completes once or does nothing |
| H14 | `isl_x_req` changes at end f=31, coincident with e_15 | the proxy sees the old `seq` (baseline T7 assertion) |

### 3.9 IRQ-acknowledge class (replaces baseline 5.8)

| Access | Path | Effect on the IRQ term | Completion |
|---|---|---|---|
| CIA1/CIA2 ICR read (`$DC0D`/`$DD0D` mirrors) | φ2, normal or I/O-only | `irq_n` released at e_0 [V mos6526.v:524-526] | after the e_`IRQ_SAFE_PHASE` marker (default 5) |
| `$DF00` read (REU status mirrors) | φ2, normal or I/O-only | status cleared at R; `irq` is combinational [V reu.sv:84, 157] | same |
| VIC `$D019` write (mirrors in `$D000-$D3FF`), including both RMW writes (`INC`/`LSR $D019`) | FV-W | apply edge A: `resetRasterIrq` set at A; `IRST` cleared at A+1; IRQ combinational [V video_vicII_656x.vhd:899, 1573, 1684-1692] | island waits for `fio_ack_tgl`, toggled at A + `IRQ_SAFE_FIO_EDGES` (default 2) |
| VIC `$D01A` write | FV-W | `ERST` etc. at A [V :1717-1721] | same |
| `$D019`/`$D01A` in fallback `FAST_FIO=0` | φ2 | e_31 / e_3 (baseline, [I]) | e_5 marker |

- Ack-class FV-W writes are never posted. O2 drains the queue first.
- At A+2 the IRQ term has been stable for one c64 edge. It and the toggle each cross with one fast register. So when the island sees the toggle, its IRQ copy already shows the acknowledge [I].
- E4 sweeps `IRQ_SAFE_FIO_EDGES` from 1 to 3, as well as `IRQ_SAFE_PHASE` from 3 to 7.

### 3.10 #665 reproduced: no growl with `$8F` and the screen on [I]

A timer-driven digi:
- CIA2 timer NMI (or CIA1 IRQ) at 4-11 kHz.
- Handler: push, sample fetch, `STA $D418`, `LDA $DD0D` (or `$DC0D`), pull, `RTI`.

With badline timing disabled:
1. NMI entry, stack pushes, vector fetch, sample fetch and pointer update are local. They ignore BA.
2. `STA $D418` is FV-W. It never touches the native bus.
3. `LDA $DD0D` is FWD_IO with I/O-only ownership. It completes at R of the native cycle it loads in, BA high or low, then waits for the e_5 marker.
4. The timer runs at the native rate, so the NMI arrives at a fixed native phase. The grant table is locked to φ2. So the handler's latency is the same every time. It varies only with the instruction the main loop is in, and with a main-loop φ2 forward in flight (at most about 1 µs). It does not correlate with badlines.

Engine build (D5) for comparison:
- The ICR read stalls up to 43 µs on every 8th raster line, at 1953 Hz PAL / 1967 Hz NTSC [I, balow_calc.py].
- At 8 kHz that is up to 34% of a sample period, against at most 0.8% with I/O-only. That modulation is the growl.

The U64 post-fix behaviour, "completes at the end of the very same PHI2 cycle", is what I/O-only does for every internal φ2 target.

What still growls, by design:
- `$0F` (badline timing enabled), as on the U64 and a real C64.
- Digis fed by REU DMA (E-BA4).
- Digis that wait on a BA-gated fallback class.

### 3.11 Changes to the baseline text

| Baseline section | Change |
|---|---|
| 0.1, 1.6 | Badline disabled: internal φ2 targets (CIA, I/O1/I/O2 without cartridge, `$FF00` snoop) get I/O-only ownership; FIO ignores BA; only fallback RAM-bus classes (COL forwarded, PORTW forwarded) and cartridge classes wait for BA = 1 |
| 1.7 | CIA, I/O1/I/O2 rows: "one per φ2; proceeds during BA low with badline timing disabled" becomes the shipping behaviour. `$FF00` row: local write plus φ2 snoop |
| 1.12 | D5 deleted (D5r only under `FF00_SNOOP=0`). D7 adds: "or the low nibble of a forwarded I/O read that lands in BA-low cycles 1-3". D14 provisional (REU DMA vs badlines, after E-BA4) |
| 2.8 | "a forwarded access owns the bus only if `baLoc` = 1 at e_15" becomes: owns the shared bus only if `baLoc` = 1; with badline timing disabled an eligible forward gets I/O-only ownership at e_16 |
| 2.13 | The BA-independent CIA/REU bullet is replaced by sections 3.1-3.5. The queue bullets are replaced by O1-O8 |
| 5.2 | Add the I/O-only row (e_16) and the `cpu_bus_valid` change |
| 5.5 | Add "trigger may land in a BA-low cycle; takeover still waits for BA" |
| 5.8 | Replaced by 3.9 |
| 9, 10, 11, 12 | R5 closed for D5; risks, tests and files per sections 4-8 below |
| docs/C64_RAM_CPU.md:65-69 | "I/O accesses use native slots. VIC bus arbitration and NMOS RDY behavior remain authoritative" becomes: in turbo, VIC/SID registers use the fast path; φ2 targets use the native slot; with badline timing disabled, internal φ2 targets may complete while BA is low without the shared address bus; VIC arbitration of RAM, colour RAM and char ROM stays authoritative in every mode; stock mode keeps NMOS RDY behaviour (Q1) |

---

## 4. RTL changes (not done now)

All are behind `EXTERNAL_CPU` (fpga64) or port defaults that keep today's logic when unused [I].

**`hdl/core/c64/ports/fpga64_sid_iec.vhd`**
- New input `ext_cpu_ioonly : in std_logic := '0'` (frame bit from the proxy). New output `ext_cpu_ioown` (bench observation).
- New signal `cpuIoOwn : std_logic := '0'`. In the phi0/DMA process:
  - clear it in the reset branch (447-462), at the VIC3 entry (468) and at `sysCycle'high` (555-558);
  - set it at CPU0:
    ```vhdl
    if EXTERNAL_CPU and sysCycle = CYCLE_CPU0 and phi0_cpu = '1' and
       cpuHasBus = '0' and dma_active = '0' and ext_cpu_ioonly = '1' then
        cpuIoOwn <= '1';
    end if;
    ```
- `cpu_bus_valid <= enableCpu and not dma_active and (cpuHasBus or cpuIoOwn) and not memory_wait_q;` (1039).
- buslogic port map gains `cpuIoOwn => cpuIoOwn`.
- Simulation assertions A1-A3 (section 6).
- Text anchors used by test runners change near 530 and 1039 (baseline R10).

**`hdl/core/c64/multitini_c64_buslogic.vhd`**
- New port `cpuIoOwn : in std_logic := '0'`, added to the sensitivity list (197).
- VIC branch (298-314): when `cpuIoOwn` = 1, the VIC RAM select goes to a new `vicRamLoc` instead of `cs_ramLoc`. Then:
  ```vhdl
  if cpuIoOwn = '1' and ultimax = '0' and cpuAddr(15 downto 12) = X"D" and
     bankSwitch(2) = '1' and bankSwitch(1 downto 0) /= "00" then
      case cpuAddr(11 downto 8) is
          when X"0" | X"1" | X"2" | X"3" => cs_vicLoc  <= '1'; -- FAST_FIO=0 only
          when X"4" | X"5" | X"6" | X"7" => cs_sidLoc  <= '1'; -- FAST_FIO=0 only
          when X"C" => cs_cia1Loc <= '1';
          when X"D" => cs_cia2Loc <= '1';
          when X"E" => cs_ioELoc  <= '1';
          when X"F" => cs_ioFLoc  <= '1';
          when others => null; -- colour RAM is never I/O-only
      end case;
  end if;
  ```
- `cs_ram <= cs_ramLoc or vicRamLoc or ...` (317). `dataToCpu` is unchanged: the chip select wins, because `cs_ramLoc`, `cs_CharLoc` and `cs_romLoc` are 0.
- With `cpuIoOwn` = '0' the netlist is today's. The M2 lockstep proves it.

**`hdl/core/c64/multitini_c64_cpu_proxy.sv`** (new in the baseline)
- Frame field `ioonly`, loaded at e_15 with `isl_x_req.ioonly & ~cart_present` and cleared at R. Output `ext_cpu_ioonly`.
- Generic `BA_LOW_IO` (default 1; 0 ties the bit to 0 for debug, bringing back D5).
- Optional status counter of I/O-only completions.

**`hdl/core/c64/multitini_c64_cpu_island.sv`** (new in the baseline)
- `ioonly` bit in `isl_x_req` (3.1).
- FWD_FF00 handling (3.5), with generic `FF00_SNOOP` (default 1).
- O2/O3 waits on registered `fvq_empty`/`fvq_vic_empty`.
- Ack-class FV-W wait on registered `fio_ack_tgl`, with generic `IRQ_SAFE_FIO_EDGES` (default 2).
- FV issue gated by `~halt_q` (3.7).

**Fast VIC/SID path** (other sub-design): must export `fvq_empty`, `fvq_vic_empty` (or declare depth 0) and `fio_ack_tgl`, and gate apply strobes with `frame_run` (O4).

**`hdl/core/c64/multitini_c64_adapter.sv`**: `cart_present` into the proxy; `ext_cpu_ioonly` wiring.

**Docs**: docs/C64_RAM_CPU.md:65-69 rule text (3.11); docs/C64_TURBO.md D5/D7/D14 and the BA-low behaviour; docs/C64_EXPANSIONS.md REU programming during badlines.

---

## 5. Timing and resources

**Clocks and crossings**
- No new clock. No new crossing type.
- `isl_x_req.ioonly` → frame register (e_15) is one more bit on X2. It needs one fast period, ≤2 LUT including the load enable and `~cart_present`. Add it to the T4 requirement check.
- `fvq_empty`, `fvq_vic_empty` and `fio_ack_tgl` are X1 signals, re-registered on the fast rising edge before use (baseline X1 rule).

**c64→c64 paths** [I]
- `frame.ioonly` / `cpuHasBus` / `dma_active` → `cpuIoOwn`: 1-2 LUT.
- `cpuIoOwn` and `cpuAddr`/`bankSwitch` → `cs_*` → CIA `rd`/`wr` and `db_out` (flops), → `iof_i` → `reu_io_select` → `io_ext` → `dataToCpu` → B0 at R. Each is single-cycle in a 31.7/30.6 ns period. They start at c64 flops (frame register, VIC `vicAddrReg`).
- The worst c64 path today (24.5 ns, T65 → PLA → cartridge → T65 DI [V C64_TURBO_BUS_CONTRACT.md §0.3]) leaves with the island.
- `vicRamLoc` adds one input to the existing `cs_ram` → `ramCE` → BRAM enable path.

**Resources** [I]

| Block | LUT | FF |
|---|---|---|
| buslogic I/O-only decode and `vicRamLoc` | 15-20 | 0 |
| fpga64 `cpuIoOwn`, `cpu_bus_valid` | 4 | 1 |
| proxy frame bit and qualifier | 3 | 1 |
| island `ioonly`, FWD_FF00, O2/O3 waits, ack-class FV-W wait | 20 | 4 |
| c64-side `fio_ack_tgl` generator | 5 | 3 |
| **Total** | **about 45-50** | **about 10** |

BRAM: none.

---

## 6. Tests

All before any Vivado run (AGENTS.md). Benches use the baseline's single-process two-clock generator.

**E-BA1 `tb_c64_ioonly`** (fpga64 with `EXTERNAL_CPU=true` and a scripted proxy stub)
- Setup: badlines (YSCROLL forcing), 0, 2 and 8 sprites, PAL and NTSC VIC.
- Stimulus: FWD_IO reads and writes to `$DC00-$DC0F`, `$DD00`, `$DD0D`, `$DE00` (SID mode 1), `$DF00-$DF0A` (REU on), and `$DF00` open (REU off). Registered at every f_r = 0..63, with BA falling and rising at every possible edge.
- Checks:
  - completion at R of the load cycle;
  - exactly one CIA `rd`/`wr`, SID `we` or REU `io_valid` per access;
  - B0 = chip data, or `lastVicDi` for open bus;
  - no `ramWE`;
  - the VIC fetch trace (`systemAddr` at VIC0 and CPUC, `ramCE`, `dataToVic`, colour address, `vicDiAec`, `colorDataAec` with `aec` = 1) equals a reference run with the dummy frame. BA-low cycles 1-3 compare with the colour nibble masked (D7).
- Badline timing enabled (flag 0): no completion and no chip strobe while BA = 0; completion in the first BA-high cycle.
- Cartridge present: the flag never takes effect.

**E-BA2 #665 digi model** (`tb_c64_machine_island`)
- CIA2 timer NMI at 8 kHz; handler `STA $D418` + `LDA $DD0D`; screen on with badlines and 8 sprites; index 15.
- `$8F`: the interval between applied `$D418` writes varies by ≤ 64 fast clocks, and the variation does not correlate with badline rows.
- `$0F`: stalls of up to about 43 µs, which shows that the check detects growl.
- Also an IRQ/CIA1 variant and an NTSC run.

**E-BA3 REU in badlines**
- Program all registers inside a badline; trigger `$DF01` in BA-low cycle 5; `$FF00` variant, with `FF00_SNOOP` = 1 and 0.
- Byte compare against an exact-mode reference.
- DMA takeover waits for `ba_reu`. The island makes no enable before live hold = 0. Count prefetch slots (extends E5).

**Ordering fuzz (O1-O8)**
- Random mixes of local writes, PORTW, FV-W (VIC and SID), FV-R, FWD_IO (both ownership types), FWD_FF00, ack-class accesses, DMA triggers, pause, reset, mode and badline switches. FV queue depth set to 0, 1 and 4.
- Per-observer logs compared with an in-order reference:
  - VIC register apply order, interleaved with port-B-visible RAM writes;
  - SID write order;
  - CIA and REU access order;
  - IRQ line samples at each T65 enable.

**E4 extension**
- FV-W `$D019` (`STA`, `INC`, `LSR`) and `$D01A`; CIA ICR reads under I/O-only inside badlines.
- No re-entry after acknowledge + `RTI`. Handler entry latency while polling `LDA $D012`, `BIT $D011` and `LDA $DC01`.
- Sweep `IRQ_SAFE_FIO_EDGES` 1-3 and `IRQ_SAFE_PHASE` 3-7.

**Assertions** (fpga64 VHDL asserts and island/proxy SVA)
- A1: `cpuIoOwn` implies `phi0_cpu` & `~cpuHasBus` & `~dma_active`.
- A2: `cpuIoOwn` implies `ramWE` = 0, `cs_color` = 0, and `systemAddr` = `vicAddr` (if `aec`) or `cpuAddr`.
- A3: `cpuIoOwn` implies the address is in the eligible set and `cart_present` = 0.
- A4: every `ioonly` forward completes in its first loaded cycle.
- A5: φ2 issue, FV-R, ack-class FV-W and mode adoption each imply `fvq_empty`; a local RAM/colour write implies `fvq_vic_empty`.
- A6: after DMA, the first FV-R/FV-W comes after the VIC's `we_r` window of every DMA VIC write.

**Retained**
- M2 lockstep with `EXTERNAL_CPU=false` vs `true`. The buslogic and fpga64 edits must not change any consumer in exact mode.
- REU machine, interrupts and VICE aggregate suites.

**Board gates (after M5)**
- G-BA1: `$8F` digi, screen on, PAL and NTSC: no growl by ear, and no audio-capture component at 1953/1967 Hz.
- G-BA2: Amiga64 M5 `$DC01`/`$DE00` rows with the screen on and `$8F`: about 1.04 φ2 per read, unchanged from screen off.
- G-BA3: Doom `reubench` with the screen on.

**Hardware references, if a C64U/U64E2 is available**
- E-BA4: REU fetch of 8 KiB to fixed `$D418` (and to RAM), screen on, `$8F` vs `$0F`, timed by a CIA timer. Shows whether U64 REU DMA ignores badlines.
- E-BA5: `lda $dc01` and `lda $df00` loops, screen on, `$8F`. Confirms "immune" with a number. Add `$FF00` write timing to E7.

---

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| RB1 | Behaviour that no real C64 shows: CIA reads complete in BA-low cycles, where the PLA blocks I/O reads. Software calibrated on badline stalls runs differently. Turbo with badline timing disabled only; approved by Q1; U64-like | Default Badline Timing Enabled; documented in C64_TURBO.md |
| RB2 | buslogic is upstream-derived and on the stock path | Port default `'0'` keeps today's logic; M2 lockstep; A1-A3 |
| RB3 | VIC write-apply window (by e_15 of the next cycle) is inferred from structure | A6 in the REU-to-VIC DMA bench |
| RB4 | `IRQ_SAFE_FIO_EDGES` depends on where the fast-path strobe lands (other sub-design) | E4 sweep; parameter |
| RB5 | FWD_FF00 changes the REU trigger path | E-BA3 with both `FF00_SNOOP` values; fallback to the baseline |
| RB6 | O3 and O2 cost cycles with a deep FV queue | Measured in the fuzz bench; depth 0 removes them; expected cost is small because the T65 needs ≥3 cycles between a store and the next store |
| RB7 | REU DMA timing in badlines may differ from the U64 | E-BA4; D14 if it differs |
| RB8 | Test-runner text anchors around fpga64 lines 530 and 1039 | Scheduled with baseline R10 |
| RB9 | FLI colour nibble from forwarded data (D7) | Cosmetic, turbo only, needs a mid-line badline plus a forward in cycles 1-3 |

---

## 8. Open questions

1. [O E-BA4] Does U64/C64U REU DMA proceed during badlines when badline timing is disabled ("Fixed eDMA during badlines when they are turned off", f2a14e51)? If yes, should Multitini copy it, which leaves VICE timing for REU DMA in that mode?
2. [O E-BA5, E7] U64 timing of CIA and REU-register accesses with the screen on and `$8F` (a number, not a user report), and `$FF00` write timing.
3. [O, other sub-design] FV-W path depth and the landing edge of its apply strobe. Depth 0 removes O2-O4 logic. The landing edge sets `IRQ_SAFE_FIO_EDGES`.
4. [O E4] Minimum safe `IRQ_SAFE_PHASE` and `IRQ_SAFE_FIO_EDGES`.
5. [I, decide in review] Adopt FWD_FF00 (D5 fully retired) or keep the baseline `$FF00` forward (D5r: `$FF00` only)? This document recommends FWD_FF00.
6. [O, other sub-design] Should the `$DE00`/`$DF00` SID (`sid_mode` 1/4) join the fast SID path? That needs `sid_mode` in the island map. Under this design it is I/O-only eligible either way.
7. [O] What exactly the final U64 #665 fix changed beyond the one-slot-late bridge start (BA on the cartridge port). The sources are closed; only a hardware probe with a real SID or a cartridge DMA device could tell.
