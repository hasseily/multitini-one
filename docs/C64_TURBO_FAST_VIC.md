# Fast VIC-II register path for the C64 CPU island (sub-design v2)

Sub-design written on 2026-10-23 for the v2 draft of [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). The current design (v3) merges it; where the two differ, C64_TURBO_DESIGN.md wins. Design stage only: no HDL, firmware, script or build file was changed for it, no build or simulation was run, and nothing here is board-tested. Paths to throwaway session files were removed. `issueN.txt` means a downloaded copy of GideonZ/1541ultimate issue #N with its comments; other bare file names that are not in this repository also name downloaded copies read during the design. Those copies are not kept.

Baseline: the v1 turbo design, an earlier draft of C64_TURBO_DESIGN.md that is not kept ("baseline §n"). This document replaces baseline §2.13 for the VIC, the VIC rows of §1.7 and §5.8, and deviation D1 (VIC part) and D5 (VIC part).

Models (Python arithmetic only), kept with their outputs in [C64_TURBO_MODELS.md](C64_TURBO_MODELS.md):
- `vicread_model.py`: read cost per speed index against the C64U and U64E measurements.
- `vicpath_model.py`: loop costs with posted writes, barriers, and the #203 pixel pattern.

Tags: **[V]** verified (file:line or URL). **[M]** measured by a third party on U64/C64U hardware. **[I]** inference or design choice. **[O]** open, with the experiment that settles it.

No hardware support is claimed. Nothing here has run on the board.

Notation (baseline §5): 32 native phases p = 0..31 per CPU cycle; e_p is the c64 rising edge that ends phase p; fast clock f = 2p + h; e_p coincides with the fast edge that ends f = 2p + 1. "Clock t" is an absolute fast clock; "enable at t" is a T65 Enable on the edge ending t.

---

## 1. Summary

**What software sees** (user decision Q1, binding):
- VIC register reads and writes run at turbo rate, not one per φ2.
- With badline timing disabled, VIC register accesses proceed while BA is low.
- Function is unchanged. Every VIC register, mirror and side effect behaves as on stock.

**Reads.**
- The island reads a combinational copy of the VIC read mux and captures it into a fast register (`io2_q`) on every fast rising edge.
- A VIC read completes on a granted clock whose previous clock had no T65 enable: address stable for one clock, data captured, then the enable. This is a "2-clock read".
- Cost: the same as RAM at indices 0-12; about one extra slot at 13-15. Model: 0.0790 φ2 per unrolled `lda $d012` at index 15. The C64U measures 0.080 [M], and reports the same "0-12 equal, 13-15 +15-30 %" pattern [M].

**Writes.**
- Writes are posted (1 slot) into an ordered 4-entry FIFO in the island.
- The c64 side accepts one entry per c64 edge and the VIC register takes the value one c64 edge later: 3-4 fast clocks after the T65 write, at most half a pixel.

**Landing rule** (section 3.6):
- A write lands at the c64 edge after acceptance, in program order.
- It is never accepted while the VIC's native write latch is busy.
- Two holds keep stock VIC invariants:
  - H1: a `$D011` write never lands at e_11..e_14.
  - H2: a `$D015` or sprite-Y write never lands at e_15..e_30 of the sprite-0 p-access cycle.
- Every other register may land at any c64 edge, because all its consumers sample at fixed edges.

**Side effects.** A completed read of `$D01E`/`$D01F` (any mirror) pushes one "clear" entry into the same FIFO. The clear lands like a write: exactly one clear per read bus cycle, after the data was sampled.

**Barriers.**
- A VIC read waits until every earlier FIO write has landed and settled (read-after-write).
- Every φ2 forward and every switch to exact mode waits for the same condition.
- After a `$D019`/`$D01A` write, the next T65 enable waits until the IRQ term has been re-registered (ACK barrier, about 7 fast clocks). This replaces the VIC rows of the baseline's late-acknowledge rule (§5.8).

**Slots.** VIC register accesses use ordinary CPU grants. The VIC's reserved slot (the gap at f = 4 at index 15) is untouched. The C64U's "+1 slot" comes out of the 2-clock read rule, not from a reserved slot.

**Decode.**
- `$D000-$D3FF`, A5..A0, so all sixteen `$40`-byte mirrors work.
- `$D02F-$D03F` read `$FF`.
- The U64 registers (`$D030`/`$D031` read override, `$D030`/`$D031`/`$D079`/`$D07A`/`$D07B` write snoop, exact A9..A0) sit outside the VIC as in baseline §1.2.

**RTL.**
- The VIC gains an additive generic `FAST_PORT` (default false) through new `replace_once` patches in `scripts/port_c64_vic.py`.
- `fpga64_sid_iec.vhd` gains pass-through ports.
- New: a c64-side FIFO pop stage (`multitini_c64_fio_port.sv`) and island-side FIO logic.

**Cost and timing.**
- About 385 LUT, 150 FF and no BRAM [I].
- Every new path is a single fast period, either rise to rise or c64 rise to fast rise: 15.28 ns NTSC, 15.86 ns PAL. No half-period paths are added.
- The open timing risk is the extra load on the T65 address bits (R-V1, E1).

---

## 2. Verified facts

### 2.1 Native frame and VIC enables

| Signal | Active during phases | Acts at edge | Source |
|---|---|---|---|
| `phi0_cpu` (VIC `phi`) | 16-31 | set at e_15, cleared at e_31 | fpga64_sid_iec.vhd:468-469, 555-557 [V] |
| `enaData` (`enableVic`) | 15, 31 | e_15 (phi = 0), e_31 (phi = 1) | fpga64:573-576 [V] |
| `enaPixel` (`enablePixel`) | 3, 7, 11, 15, 19, 23, 27, 31 | 8 pixel edges per cycle | fpga64:790-807 [V] |
| `rasterX(2:0)` | 0 in p16-19, 1 in p20-23, 2 in p24-27, 3 in p28-31, 4 in p0-3, 5 in p4-7, 6 in p8-11, 7 in p12-15 | reset at e_15, +1 per pixel edge | video_vicII_656x.vhd:858-876 [V] |
| CPU slot | issue at 28, retire at 31 (R = e_31) | — | scheduler.vhd:42-43, 61 [V] |
| VIC phi1 fetch address | `vicAddrLoc` during p11 → `vicAddrReg` at e_11 → RAM read at e_12 (VIC0) | e_11/e_12 | VIC:453-540; fpga64:1021-1023; multitini_c64_local_ram.sv:34-38 [V] |
| VIC phi2 c-access address | during p27 → RAM at e_28 when `cpuHasBus` = 0 | e_27/e_28 | VIC:516-518; fpga64:1021-1023 [V] |

- Phase numbering is the `sysCycleDef` order: EXT0 = 0, VIC0 = 12, VIC3 = 15, CPUC = 28, CPUF = 31 (fpga64:196-205) [V].
- The scheduler already receives the phase as `to_unsigned(sysCycleDef'pos(sysCycle), 5)` (fpga64:1055) [V].
- `memory_wait` stretches only phases 13 and 29 (fpga64:410-416 comment) [V]. Its only source is `cart_stall` (adapter:504) [V].

### 2.2 Stock write path

- **Latch.** While `phi` = 1 (edges e_16..e_31) the VIC samples `we_r <= cs and we`, `rd_r`, `addr_r <= aRegisters`, `di_r <= diRegisters` (VIC:300-310) [V]. After e_31 these hold through phases 0-15 of the next cycle.

- **Strobes** (VIC:313-315) [V]:

| Strobe | Condition | Fires at | Registers (VIC lines) |
|---|---|---|---|
| `myWr_a` | `we_r` & pixel & rasterX = 3 | e_31 of the write cycle (= R) | `$D011` RSEL; `$D019` acknowledge pulses; `$D01D` MXE (1684-1696) |
| `myWr_b` | `we_r` & pixel & rasterX = 4 | e_3 of the next cycle | `$D011` RST8/ECM/BMM/DEN/YSCROLL; `$D012`; `$D015`; `$D016` RES/MCM/CSEL; `$D017`; `$D018`; `$D01A`; `$D01B`; `$D01C` → MCDelay (1697-1727) |
| `myWr_c` | `we_r` & pixel & rasterX = 5 | e_7 of the next cycle | MX/MY `$D000-$D00F`; `$D010`; `$D016` XSCROLL; `$D030` turbo_reg (1728-1759) |
| `myWr_phi1` + pixel (NMOS) | live `cs and phi and we`, every pixel edge in phi = 1 | e_19, e_23, e_27, e_31 of the write cycle | `$D020-$D02E` colours (1662-1682) |
| HMOS grey dot | variant "01" only | e_16..e_18 | colours := `$F` (1640-1660) |

- **Variant.** Multitini fixes `vic_variant` = "00", NMOS (adapter:516) [V]. The grey-dot path is dead here.
- **Why these phases** [I]. The source gives no reason. They place each register's effect at a fixed pixel column inside the 8-pixel cycle, tuned upstream against test programs. All stock landings are on pixel edges.
- `$D019` writes set registered pulses `resetX <= di_r(n)`. The flags clear one c64 edge later, e.g. `IRST` at e_0 (VIC:1581-1584, 1689-1692, 899) [V].

### 2.3 Stock read path

- **When.** `myRd` fires only at e_23: `cs` & phi & not we & pixel & rasterX(1:0) = "01" (VIC:317-318) [V]. The REU peek variant only fires during DMA.
- **Read mux** (VIC:1767-1826) [V]:
  - `$D011` = RST8 from `rasterY(8)` & ECM & BMM & DEN & RSEL & YSCROLL.
  - `$D012` = `rasterY(7:0)`.
  - `$D016` = "11" & RES & MCM & CSEL & XSCROLL.
  - `$D018` = VM & CB & '1'.
  - `$D019` = IRQ & "111" & flags.
  - `$D01A` = "1111" & enables.
  - `$D01C` = MC.
  - Colours = "1111" & value.
  - `$D030` = "1111111" & (turbo_reg or not turbo_en).
  - Others = `$FF`.
- `turbo_en` = `turbo_mode(0)` (fpga64:1077), and the adapter ties `turbo_mode` = {cpu_turbo_active, '0'} (adapter:513). So `turbo_en` = 0 and `$D030` reads `$FF` from the VIC [V].
- **Collision clears** [V]:
  - `$D01E`: `myRd` sets `M2MClr` (1479-1481). M2M is cleared at each pixel edge while `M2MClr` = 1 (1525-1528). `M2MClr` drops at the next phi = 0 pixel edge on which the native latch no longer shows a `$D01E` read (1482-1484). Stock therefore keeps clearing from e_27 of the read cycle until about e_3 two cycles later.
  - `$D01F`: `myRd` clears M2D, M2DDelay and M2Dhit at the read edge itself (1561-1566).

### 2.4 Readable VIC state that changes without a CPU write

| Register | Source | Changes at | Lines [V] |
|---|---|---|---|
| `$D011` bit 7, `$D012` | rasterY | e_31 of the last cycle of a line | 877-885 |
| `$D013`/`$D014` | lpX/lpY | any c64 edge (`lp_n` = CIA1 PB4), once per frame | 926-952 |
| `$D019` IRST | raster compare | any edge where rasterY = rasterCmp and not done; normally e_0, or one edge after a compare write | 892-910 |
| `$D019` IMMC/IMBC | collisions | pixel edges | 1520-1523, 1555-1558 |
| `$D019` ILP | light pen | any edge | 941 |
| `$D019` bit 7 | combinational IRQ | with any flag or enable | 1573 |
| `$D01C` (read MC) | MCDelay → MC | first pixel edge after an MCDelay write | 1306-1313 |
| `$D01E`/`$D01F` | collision OR | pixel edges | 1498-1529, 1543-1559 |

All other readable registers change only on CPU (or REU DMA) writes. Every read-mux input is a c64 flop, or for IRQ one LUT on flops [V].

### 2.5 Who samples each writable register

| Register | Consumers and sampling edges | Lines [V] |
|---|---|---|
| MX, `$D010` | sprite X compare, pixel edges | 1265 |
| MY, ME (`$D015`) | MDMA/MCBase/MYE_ff at e_15 (sprite cycles 55-56); `baSprite04` phi0 chain (cycle 55); `reuSprite0Grace` phi0 chain; **MActive_next → MActive at e_19..e_31, and MPtr at e_15, in the sprite-0 `cycleSpriteA` cycle** | 1179-1191, 1224-1230, 711-713, 741-754, 1202-1213, 1231-1234, 565-583 |
| `$D011` YSCROLL | badLine (comb) → `baChars` at phi0 edges e_0..e_15; RC/VC/idle at e_15 | 329-336, 652-670, 792-847 |
| `$D011` DEN | `rasterEnable` every edge on line 48; borders at pixel edges | 612-625, 1016, 1036 |
| `$D011` RSEL | borders, pixel edges | 1013-1016 |
| `$D011` ECM/BMM | **fetch address sampled in p11; `fetchBMM`/`fetchECM` at e_15**; display delays at pixel edges | 453-520, 523-532, 1095-1097 |
| `$D011` RST8, `$D012` | raster compare, every edge | 902 |
| `$D016` | MCM/XSCROLL in the shifter, CSEL in borders: pixel edges; RES unused | 1005, 1065, 1077 |
| `$D017` | MYE_ff and MCBase at e_15 | 1134-1172 |
| `$D018` | p-access and g-access address (p11), c-access address (p27) | 506, 463-466, 517 |
| `$D019`, `$D01A` | IRQ flags, next edge; IRQ combinational | 899, 1494, 1539, 1573 |
| `$D01B`, `$D01C`, `$D01D` | colour mux, MC flip-flop, X-expand flip-flop: pixel edges | 1357, 1306-1313, 1281 |
| `$D020-$D02E` | video output, pixel edges | 1455-1468 |

Two facts about the remaining consumers [V]:
- The only processes that sample CPU-written registers at phi = 1 edges are MActive (pixel edges) and the pixel-pipeline processes.
- The control state machines (vicCycle, sprite, rasterX/Y) use no CPU-written register. The other state registers sample only at e_15, or at phi0 edges whose last sample is e_15 (VIC:381-448, 652-737, 792-887).

### 2.6 Hookup

- **Decode.** `cs_vic` = `$D000-$D3FF` with I/O mapped, qualified by `io_enable` (buslogic:236-239, 318) [V].
- **VIC inputs.** The VIC sees `aRegisters` = cpuAddr(5:0), `we` = cpuWe, `diRegisters` = cpuDo (fpga64:729-736) [V]. Mirrors are therefore implicit.
- **CPU IRQ.** The CPU IRQ term is `irq_cia1 and irq_vic and irq_n and irq_ext_n` (fpga64:995) [V].
- **vicBus.** `vicBus` takes cpuDo only during a native VIC write and is `$FF` otherwise (fpga64:681-692) [V].

### 2.7 The VIC file is generated

- The header says "Generated by scripts/port_c64_vic.py; do not edit directly" (VIC:1) [V].
- The script applies `replace_once` patches to the pinned upstream file. Each patch fails if its anchor is not unique (port_c64_vic.py:30-33).
- `--check` compares the output with the committed file (port_c64_vic.py:208-212) [V].
- `test_c64_vice_vic.py` and `test_c64_mame_vic.py` import `port()` [V].

### 2.8 U64 / C64U evidence

| Fact | Source |
|---|---|
| C64U fw 1.1.0, φ2 per unrolled 4-cycle read at index 15: RAM 0.064, `$D419` 0.085, `$D41A` 0.080, `$D012` 0.080, `$DC01` 1.040, `$DE00` 1.045 | [M] amiga64_HARDWARE.md:1301-1308 |
| "Through indices 0-12 a `$D419` read costs exactly what a RAM read does; at 13-15 it costs 15-30 % more (about 1 extra turbo cycle)" | [M] amiga64_HARDWARE.md:1311 |
| U64E fw 3.15 (48 slots per φ2), 16384 `lda` reads in φ2: 32 MHz RAM 2062 / `$D012` 2064; 40 MHz 1692 / 2064; 48 MHz 1404 / 1765 | [M] gist839/meas_c_external.json |
| At 48 MHz, alternating `$D020` writes give alternating pixels with a glitch "every 48:th or so". GideonZ: "The system is in sync with the DOT clock … The original half cycle where PHI2 = 0 is always a VIC cycle" | [M] issue203.txt:9, 17-21 |
| "always one time slot offered to the VIC, even when the screen is off" | [V] issue874_comments.txt:4 |
| The U64 FPGA source is not public. The d43546f clone holds only binaries (`external/u64.sof`, `external/u64e2_*.bit`) | [V] `git ls-tree` of the clone |

---

## 3. Design

### 3.1 Data flow

```
 CPU island (c64_cpu_clk)                          c64_clk domain
 +----------------------------------+             +------------------------------------------+
 | T65 A, DO, R_W_n -> page map:    |  A[9:0]     | fpga64 -> VIC fast read mux (comb)       |
 |   class FIO ($D000-$D7FF, I/O)   |------------>| turbo_regs: $D030/$D031 read values      |
 |                                  |             | SID read data (SID sub-design)           |
 | io2_q (2-slot source: FIO, COL)  |<------------| read data (comb)                         |
 |                                  |             |                                          |
 | FIO FIFO 4 x {tgt,kind,A9:0,D}   |------------>| multitini_c64_fio_port: accept <=1/edge, |
 |  wp, full_q, all_done_q,         |             |   H1/H2 holds, native-busy barrier       |
 |  ack_wait_q, en_q, busy_q        |<------------|   -> vic_wr/wa/wd/clr (registered)       |
 +----------------------------------+  rp, done   |   -> VIC lands at next edge; done at +1  |
                                                  +------------------------------------------+
```

### 3.2 FIO class in the island

- FIO = `$D000-$D7FF` while I/O is mapped (baseline §2.5). VIC = FIO with A10 = 0; SID = FIO with A10 = 1.
- Exact mode never uses the FIO logic. Exact VIC accesses stay on the native slot and stock `myRd`/`myWr`.

Enable conditions [I]. All terms are registered except the class decode, as in baseline §5.7:
```
base      = turbo_q & RUN & grant_q & run_q & ~halt_q & map_ok & ~ack_wait_q
fio_rd_ok = base & ~en_q & all_done_q & ~busy_q     // 2-clock read + RAW barrier
fio_wr_ok = base & ~full_q & ~busy_q                // posted write
```
- `en_q`: the T65 was enabled on the previous edge.
- `all_done_q`: registered `fio_done == wp`.
- `full_q`: registered `(wp - fio_rp) >= 4`.
- `busy_q`: the VIC's native latch `we_r`, registered.
- `ack_wait_q`: set by an ACK-class write, cleared by `all_done_q`. It also gates the local-cycle and forward-completion enables of baseline §5.7 and §5.2, so no T65 cycle of any class runs during the wait.

### 3.3 Fast reads

**Read mux.**
- The VIC exports `fast_rd_data`, a combinational copy of `readRegisters` (VIC:1771-1823) indexed by `fast_rd_addr` = island A5..A0.
- One difference: `$1C` returns MCDelay instead of MC. MC equals MCDelay except during the one pixel after a write (VIC:1306-1313). A stock read always comes at least 16 phases after the e_3 landing, so it never sees the difference [I].
- Reading MC could still return the old value for up to 8 fast clocks after the RAW barrier releases (C3), so the copy reads MCDelay.

**Final mux in the island** [I]:
```
fio_rd = u64_hit ? u64_byte : (A10 ? sid_rd : vic_rd)
u64_hit  = (A9..A0 = $031 & d031_live) | (A9..A0 = $030 & d030_live)
io2_q   <= col_sel ? {lastVicDi_q[7:4], col_q[3:0]} : fio_rd    // every fast rising edge
```
- `io2_q` is shared with the 2-slot colour RAM read (baseline §4.1). The T65 DI mux therefore keeps its six sources.
- Capture is unconditional. No side effect depends on it.

**Read timeline**

| Edge | Action |
|---|---|
| end of clock X−2 or earlier | previous T65 enable; the FIO read address appears |
| end of X−1 | `io2_q` ← `fio_rd`(A). The VIC state is sampled here, pre-edge |
| end of X | T65 enable with DI = `io2_q` if `fio_rd_ok`. A clear entry is pushed for `$1E`/`$1F` (section 3.4) |

**Cost model** (`vicread_model.py`, Bresenham grants with the first grant at f = 5, gap at f = 4):

| Index (N) | RAM φ2 per lda | VIC φ2 per lda | Extra slots per read |
|---|---|---|---|
| 0-12 (1-32) | — | equal to RAM | 0 |
| 13 (40) | 0.1000 | 0.1250 | 1.00 |
| 14 (47) | 0.0851 | 0.1000 | 0.70 |
| 15 (63) | 0.0635 | 0.0790 | 0.97 |
| C64U index 15 [M] | 0.064 | 0.080 (`$D012`) | about 1 |

- The rule matches the C64U shape: indices 0-12 cost the same as RAM, and 13-15 cost 15-30 % more [M amiga64:1311].
- A 3-clock read (registered address) would give 0.0952 at index 15 and +25 % at index 12. That contradicts the C64U data, so the 2-clock read is required.
- In the 48-slot U64E frame the model gives 32 MHz equal (measured 2062/2064) and 48 MHz ratio 1.237 (measured 1.257).
- At 40 MHz the model gives 1.083 against a measured 1.22 [M]. The U64E's 39-of-48 grant pattern is evidently not Bresenham. It is not our target.

**Live sampling.** The read returns the register value at the end of X−1, at any phase. A `$D012` read can see a new line up to 0.25 µs earlier than a stock read in the same native cycle would. This is inherent to a turbo CPU [I].

### 3.4 Read side effects (`$D01E`, `$D01F`)

- **Trigger.** Only the completing T65 enable of a VIC-class read with A5..A0 = `$1E` or `$1F` pushes an entry `{VIC, CLR_M2M | CLR_M2D}`.
- **Counting.** One entry per read bus cycle: dummy reads, indexed-store dummy reads and the RMW read each count, as on stock, because T65 issues every NMOS bus cycle [V C64_TURBO_T65_CORE.md §5].
- **Free FIFO slot.** The RAW barrier makes the FIFO empty before any FIO read, so the push always has a free slot.
- **Landing.** The pop stage drives `fast_clr_m2m`/`fast_clr_m2d` for one c64 clock. At C2 the VIC clears:
  - `$1E`: M2M and M2Mhit (new, additive).
  - `$1F`: M2D, M2DDelay and M2Dhit (copy of VIC:1561-1566).
- **Order.** Data was sampled at the end of X−1 and the clear lands at least 4 fast clocks later, so the data always precedes the clear. Collisions in the gap (at most one pixel edge) are lost. Stock loses about 9 pixel edges after a read (section 2.3). Benign [I].
- **No stock-style tail.** The fast clear is a single event and does not set `M2MClr`. That keeps it deterministic and independent of phase [I].
- **Back-to-back reads.** In `LDA $D01E / LDA $D01E` the second read waits for the first clear (RAW barrier), so it cannot report the same bits twice.

### 3.5 Fast writes

**Island push.**
- At the T65 enable of a FIO write (posted), the island stores `{tgt, WR, A9..A0, DO}` in `isl_x_fio_mem[wp]` and increments `wp`. A and DO are T65 outputs that have been stable since the previous enable.
- Each entry is 21 bits. The depth is 4, with 3-bit pointers.

**Pop stage** (`multitini_c64_fio_port.sv`, c64_clk):
```
head   = isl_x_fio_mem[fio_rp[1:0]]
spr    = head.a[5:0] in {01,03,05,07,09,0B,0D,0F,15}
held   = head.tgt==VIC & head.kind==WR &
         ((head.a[5:0]==6'h11 & vic_hold_d011) | (spr & vic_hold_spr))
accept = (fio_rp != isl_x_fio_wp) & ~vic_busy & ~held
on each c64 edge:
  vic_wr <= accept & VIC & WR;  vic_wa <= head.a[5:0];  vic_wd <= head.d
  vic_clr_m2m/m2d <= accept & VIC & CLR_*
  u64_wr <= accept & VIC & WR;  u64_a <= head.a[9:0];   u64_d <= head.d
  s1 <= accept;  s2 <= s1;  fio_done <= fio_done + s2;  fio_rp <= fio_rp + accept
```

**Write timeline**

| Edge | Domain | Action |
|---|---|---|
| end of t (T65 enable) | fast | write complete for the CPU; entry pushed |
| C1 = first c64 edge after t (t+1 if t is even, t+2 if t is odd) | c64 | accepted if not busy or held; strobes registered |
| C2 = C1 + 1 c64 clock | c64 | **landing**: VIC register(s) take the value; turbo_regs snoop |
| C3 = C2 + 1 c64 clock | c64 | pulse effects settled (`$D019` → IRST/IMMC/IMBC/ILP cleared); `fio_done`++ |
| C3 + 1 fast | fast | island registers `done`; `io2_q` samples settled state |
| C3 + 2 fast | fast | earliest dependent enable |

- Landing latency is 3-4 fast clocks when nothing is held. A pixel is 8 fast clocks.
- **Throughput.**
  - Pops run at one per c64 edge (2 fast clocks).
  - The T65 can write faster than that only in RMW pairs (dummy write and write on consecutive cycles). Depth 4 absorbs those.
  - It also absorbs short holds (H1 at most 4 c64 clocks; H2 at most 16, once per line) before the island stalls on `full_q`.

### 3.6 The landing rule

1. **Edge.** An accepted entry lands at the next c64 edge (C2).
2. **Order.** Entries are accepted in FIFO order, one per c64 edge, VIC and SID entries in one stream. A held head blocks later entries.
3. **Native barrier.**
   - No entry is accepted while `we_r` = 1. `we_r` is 1 from e_16 of a native VIC write cycle until e_16 of the next cycle, so it covers that write's colour landings (e_19..e_31), `myWr_a` (e_31), `myWr_b` (e_3) and `myWr_c` (e_7).
   - In turbo a native VIC write only comes from REU DMA, or from the tail of exact mode. A fast write can therefore never land before an older native write's late strobes.
4. **H1.** A `$D011` write (any mirror) is not accepted when the acceptance phase is 10..13, so it never lands at e_11..e_14.
5. **H2.** A write to `$D015` or MY (`$D001..$D00F` odd, any mirror) is not accepted when the acceptance phase is 14..29 in the cycle where `vicCycle = cycleSpriteA and sprite = 0`, so it never lands at e_15..e_30 of that cycle.
6. **Not gated** by pause or memory waits. During pause `sysCycle` is frozen at EXT4 (fpga64:404). A landing there is the same as a landing just after e_7. Memory-wait stretches (phases 13 and 29) only move a landing earlier inside the same phase, which never enters an H1 or H2 window. Cartridges force exact mode anyway (baseline D2).

### 3.7 Why the rule keeps stock VIC internals

The VIC is fully synchronous on c64_clk. A multi-bit register update at one edge is atomic, and each consumer sees it at its next sampling edge. The question is whether a landing at a non-stock edge can leave a state element in a state no stock write sequence produces. Consumers fall into four classes (section 2.5) [I, from the RTL]:

**(a) Pixel consumers.**
- Covers colours, MX, `$D010`, XSCROLL, MCM, CSEL, RSEL, MPRIO, MC, MXE, and display mode delays.
- They see the new value from the first pixel edge after landing. This is the intended turbo behaviour (section 3.8).
- These elements are shift registers and comparators. Stock writes already move them at 8-pixel granularity, for example side-border and sprite-stretch tricks, so no hidden state can get stuck.

**(b) Once-per-cycle state consumers.**
- Covers e_15 samples and phi0 chains whose last sample is e_15: MDMA, MCBase, MCnt, MYE_ff, RC/VC/idle, `fetchBMM`, MPtr, `baChars`/`baSprite`/`reuSprite0Grace`.
- A landing anywhere in (e_15(k−1), e_14(k)] gives the same state at e_15(k) as a stock write in cycle k−1, which lands at e_31(k−1), e_3(k) or e_7(k).
- A landing at e_15(k) or later gives the same state as a stock write in cycle k.
- With several fast writes in one window, only the last matters to these consumers. A BA chain set low earlier in phi0 (sticky `baSprite04`) ends in a state stock also reaches.

**(c) Every-clock consumers.**
- **Fetch address** (p11 and p27) has no stored state. Each fetch uses a legal register value. A `$D018` change between p11 and p27 makes the c/g pair at the switch cleaner than stock. That is data, not control state.
- **Flags.** The raster-compare flags (IRST, `rasterIrqDone`) and `rasterEnable` are set or cleared at the first edge after their condition. Their timing can move by up to one cycle relative to the mapped stock write, but they reach the same state. A raster IRQ can therefore be raised in mid-cycle.

**(d) One register feeding two state elements, sampled at different phases of one cycle, with a stock invariant:**

| Hazard | Invariant broken by an unrestricted landing | Window | Hold |
|---|---|---|---|
| H1 `$D011` ECM/BMM | `fetchBMM`/`fetchECM` must hold the mode used by the last phi1 fetch (comment at VIC:522). A landing at e_11..e_14 lets the p11 fetch use the old mode while e_15 records the new one, so the NMOS transitional fetch is skipped | land e_11..e_14 | H1 |
| H2 ME/MY | In the sprite-0 `cycleSpriteA` cycle, MPtr (e_15, VIC:572) and MActive (e_19..e_31, VIC:1233) must come from the same ME/MY. A landing at e_15..e_30 gives MPtr = `$FF` with MActive set, or MActive values that differ for 1-3 pixel edges | land e_15..e_30 of that cycle | H2 |

No other register has two state consumers in one cycle (section 2.5).

**Conclusion** [I, to be proven by test T2]. Under the rule:
- the VIC control state at every e_15 equals that of a stock VIC fed with a mapped stock write sequence (the IRQ flags within one cycle);
- non-stock behaviour exists only in pixel-level and fetch-address data paths, which is what a turbo CPU should cause.

**Per register**

| Register | Any c64 edge safe? | Rule |
|---|---|---|
| MX, `$D010`, `$D016`, `$D017`, `$D018`, `$D01B`-`$D01D`, colours, `$D030` | yes | next edge |
| `$D011` | yes except e_11..e_14 (ECM/BMM) | H1 |
| `$D012` | yes (IRQ may set mid-cycle) | next edge |
| MY, `$D015` | yes except e_15..e_30 of the sprite-0 p-access cycle | H2 |
| `$D019`, `$D01A` | yes | next edge + ACK barrier (3.10) |
| `$D01E`/`$D01F` clears | yes | next edge |

Stock lands `$D011` RSEL (e_31) and `$D016` XSCROLL (e_7) separately from the other bits. The fast path lands all bits of a register together. This has no stock-visible meaning at turbo [I].

### 3.8 Pixel-level behaviour (#203)

- A colour write lands at C2, 3-4 fast clocks after the T65 write. It shows from the first pixel edge after C2.
- Resolution is one pixel. Of two writes inside one pixel only the later is visible, as on any pixel-clocked VIC.
- **Model** (`vicpath_model.py`, unrolled `LDA #0 / STA $D020 / LDA #1 / STA $D020`, 12 cycles per pair):
  - Index 14 (47 grants per 64 fast clocks, so 47 CPU cycles per 8 pixels, the same ratio as the U64 at 48 MHz): runs of 1 pixel with a 2-pixel run every about 46 pixels (772 single runs, 17 doubles). The U64 shows "alternating pixels … a glitch every 48:th or so" [M issue203.txt:9]. The match is qualitative [I].
  - Index 15: each colour lasts about 0.76 pixel, so colours are sometimes skipped.
- Writes cost 1 slot. With 2 slots per write, the U64 pattern would drift by about 0.33 pixel per pair instead of 0.02. So the U64's VIC writes are also effectively 1 slot [I].

### 3.9 Ordering and barriers

| Before | After | Rule |
|---|---|---|
| FIO write | FIO write (VIC or SID) | FIFO order |
| FIO write or clear | FIO read | read waits for `all_done_q` (global RAW barrier) |
| FIO read | FIO write | none needed: the read is complete at its enable |
| FIO write | φ2 forward (CIA, I/O1/I/O2, `$FF00`, REU trigger) | forward issue waits for `all_done_q`. A REU stash from `$D000` sees every earlier VIC write |
| FIO write | switch to exact mode | waits for `all_done_q` |
| native VIC write (exact tail, DMA) | FIO read or write | `busy_q` (`we_r`) |
| `$D019`/`$D01A` write | any next T65 enable | `ack_wait_q` until `all_done_q` |

- **Precise barrier (option).** A precise RAW barrier would wait only for pending writes that affect the read value, with this dependency table:
  - `$D019` depends on `{$11, $12, $19, $1A}`;
  - `$1E`/`$1F` depend on their clears;
  - `$30`/`$31` depend on `{$30, $31, $39, $3A, $3B}`;
  - `$12`, `$13`, `$14` depend on nothing;
  - every other register depends on itself.
- **Choice.** It saves about 1.8 fast clocks per iteration in a raster-bar loop at index 15 (14.22 vs 16.01). It needs per-entry addresses kept until done and is a correctness risk if the table is incomplete. The global barrier ships. The precise one stays a generic, enabled only if O2 shows that the C64U has no read-after-write penalty [I].

### 3.10 IRQ acknowledge

- **Stock reference.** A `$D019` write retires at R(k). The CPU's IRQ sample at that enable still sees IRQ; IRST clears at e_0(k+1); the next enable, R(k+1), sees it released (sections 2.2 and 2.5).
- **Turbo, posted.** The write's own enable samples `irq_q` (still asserted, as on stock). `ack_wait_q` then blocks every enable until `all_done_q`. That is C3 + 2 fast clocks: IRST cleared at C3, the comb IRQ term settled, `irq_q` registered at C3 + 1.
- **Result.** The next enable sees the released line. The T65's two-sample IRQ history (T65.vhd:753-789) gets the same sample sequence as stock, so `STA $D019 / RTI` cannot re-enter.
- **`$D01A`.** Writes change IRQ at C2 and use the same barrier, both to release and to assert.
- **RMW** (`ASL $D019`): both writes are ACK class, so there are two barriers.
- **Cost** (`vicpath_model.py`): `STA $D019` plus a 2-cycle instruction takes 12.2 fast clocks at index 15 instead of 6, under 0.1 µs per interrupt.
- **Interplay with baseline §5.8.** The VIC rows (forwarded `$D019`/`$D01A` completing after the e_5 marker) are deleted. §5.8 keeps only forwarded CIA ICR reads and `$DF00` reads. D13 then covers only those.
- **Not ACK class.** Writes to `$D011`/`$D012` that make the raster compare match assert IRQ about 5-6 fast clocks after the write, not on the next enable. This is assertion latency, never a spurious interrupt [I]. O7 decides whether to add them to the barrier.
- **Polling** `LDA $D019`/`$D012` uses ordinary fast reads.

### 3.11 Slot accounting (the reserved VIC slot)

- **U64.** The rule "one slot per φ2 always goes to the VIC" is the VIC's memory slot [V issue203.txt:21; issue874_comments.txt:4].
- **Multitini.** The VIC's memory fetches use main-RAM port B and never compete with the island. The gap at f = 4 at index 15 exists only to copy the C64U's 63 [V baseline §1.3, §2.7].
- **FIO accesses** consume ordinary CPU grants:
  - A write uses one grant.
  - A read uses one grant. When the previous clock had an enable, it also forfeits that one grant, because the 2-clock rule cannot use an adjacent grant.
- **Effect by index.** Grants are adjacent only for N ≥ 33. Hence 0 extra at indices ≤ 12 and about +1 at 13-15. At index 15, a read whose previous enable fell at f = 3 uses the f = 4 gap for free. This is why 0.0790 is slightly under 5/63.

### 3.12 BA and badline timing

- **Badline timing enabled, index ≥ 1.** `halt_q = ~ba_q` gates every island enable, FIO included (baseline §2.8). Timing stays C64-compatible.
- **Badline timing disabled.** FIO reads and writes proceed during BA = 0. The VIC register port uses no bus (Q1, approved). This retires the VIC part of D5.
- **BA in mid-cycle.** A fast `$D011` write can move `baChars` in mid-phi0. The proxy, REU and `baCnt` sample BA only at e_15, so they see a stock-equivalent value. Only the island's halt logic sees the intermediate levels, and only in turbo [I].

### 3.13 Mode switches, DMA, pause, reset

- **Turbo → exact.** The island adopts exact mode only at an enable boundary with no forward in flight and `all_done_q` (an addition to baseline §2.11).
- **Exact → turbo.** The first FIO access waits for `busy_q` = 0, at most until e_16 of the cycle after the last exact VIC write.
- **REU DMA.** The trigger forward waits for `all_done_q`. The island is halted during DMA. After DMA, `busy_q` covers the last DMA VIC write's late strobes.
- **Pause.** The island halts at the next boundary (baseline §2.12). The pop stage keeps draining (rule 6).
- **Reset.** The island FIFO (`wp`, `ack_wait_q`) resets with R1. `fio_rp`, `fio_done` and the strobes reset with R2. The VIC's reset branch has priority over the fast write block. Reset lasts at least 32 c64 clocks, so both sides are in reset together (baseline §3.3).

### 3.14 U64 registers alongside the VIC

- **Writes.**
  - Every fast VIC-target write still reaches the VIC: `$D031` becomes a VIC no-op, `$D030` sets turbo_reg (no effect, since `turbo_en` = 0), and `$D07A` becomes the no-op `$3A`.
  - turbo_regs also snoops the pop stage (`u64_wr`, A9..A0, data) at C2 with the exact decode of baseline §1.2.
  - Native exact-mode writes keep the `cpu_bus_valid` snoop. The two sources are disjoint.
- **Reads.**
  - `u64_hit` overrides the VIC byte only for exact `$D031` (Registers/TurboEnable live) and `$D030` (TurboEnable live).
  - `$D071`, `$D131` and similar addresses read the VIC (`$FF`).
  - `$D07A`/`$D07B`/`$D0BC` read VIC mirrors (`$FF`). `$D0BC` stays deferred (baseline D10).
- **Read after write.** A `$D031` read after a `$D031` write sees the new value, because the RAW barrier waits for C3.
- **Speed change** still takes effect at the next φ2 boundary through B1 (baseline D9).

### 3.15 Expected speeds (model [I], `vicpath_model.py`; fast clocks / φ2 per iteration)

| Loop | Index 9 (16) | 12 (32) | 13 (40) | 14 (47) | 15 (63) |
|---|---|---|---|---|---|
| unrolled `lda $d012` | 16.0 / 0.250 | 8.0 / 0.125 | 8.0 / 0.125 | 6.4 / 0.100 | 5.05 / 0.079 |
| unrolled `lda $3000` (RAM) | 16.0 / 0.250 | 8.0 / 0.125 | 6.4 / 0.100 | 5.45 / 0.085 | 4.07 / 0.064 |
| `sta $d020,x / dex / bne` | 40 / 0.625 | 20 / 0.313 | 16 / 0.250 | 14.9 / 0.233 | 11.1 / 0.174 (engine build with D1: 2.0 µs) |
| `lda $d012 / cmp # / bne` | 36 / 0.563 | 18 / 0.281 | 16 / 0.250 | 12.8 / 0.200 | 10.1 / 0.158 |
| `stx $d020` then poll `$d012` | 52 / 0.813 | 26 / 0.406 | 21.3 / 0.333 | 18.8 / 0.294 | 14.2 / 0.222 |
| unrolled `inc $d020` | 24 / 0.375 | 12 / 0.188 | 12 / 0.188 | 11.3 / 0.176 | 10.1 / 0.158 |
| unrolled `sta $d020 / sta $d021` | 32 / 0.500 | 16 / 0.250 | 12.8 / 0.200 | 10.9 / 0.170 | 8.1 / 0.127 |
| `sta $d019` + 2-cycle instruction | 28 / 0.438 | 18 / 0.281 | 16 / 0.250 | 14.2 / 0.222 | 12.2 / 0.191 |

Badline timing is disabled and no BA halts are modelled. `inc $d020` is about 1.7x slower than a RAM `inc` at index 15, because its read waits for the previous instruction's writes (O3).

### 3.16 Timing details that may differ from the U64 (none known to software)

| # | Detail | Size | Settled by |
|---|---|---|---|
| T-a | Read-after-write wait | 0-3 slots for a read within about 7 fast clocks of a write | O2 |
| T-b | ACK barrier after `$D019`/`$D01A` writes | about 7 fast clocks | O4 |
| T-c | H1/H2 holds | at most 4 / 16 c64 clocks for those registers | not observable in practice |
| T-d | Landing latency | 3-4 fast clocks (0.05-0.06 µs); U64 unknown | O1 |
| T-e | Single collision clear, without stock's multi-pixel tail | at most 1 pixel of lost collisions vs about 9 on stock | O6 |

D1 (VIC part) and D5 (VIC part) are retired. D7 (FLI colour nibble) is unchanged: `vicBus` was already `$FF` in turbo.

---

## 4. RTL changes (not done now)

### 4.1 `video_vicII_656x.vhd` via new patches in `scripts/port_c64_vic.py`

Every patch is additive under `FAST_PORT` (default false). With the default, the logic is unchanged and the existing VICE/MAME VIC suites and `--check` still apply. Anchors are checked by `replace_once`. Exact whitespace is taken from the pinned upstream file.

| # | Anchor (upstream text) | Change |
|---|---|---|
| P1 | `emulateGraphics : boolean := true` (end of generics) | add `FAST_PORT : boolean := false` |
| P2 | `addrValid : out std_logic` (end of ports) | add ports, inputs with defaults: `fast_phase : unsigned(4 downto 0)`, `fast_rd_addr : unsigned(5 downto 0)`, `fast_rd_data : out unsigned(7 downto 0)`, `fast_wr`, `fast_wa(5:0)`, `fast_wd(7:0)`, `fast_clr_m2m`, `fast_clr_m2d`, `fast_hold_d011 : out`, `fast_hold_spr : out`, `fast_native_busy : out` |
| P3 | the `myWr_c` case end `when "110000" => turbo_reg <= diRegisters(0); … end case; end if;` | append, inside the non-reset branch: `if FAST_PORT and fast_wr = '1' then case fast_wa is …` covering every writable register with all bits at once (MX/MY, `$10`, `$11` all bits including RSEL, `$12`, `$15`, `$16` all bits including XSCROLL, `$17`, `$18`, `$19` reset pulses, `$1A`, `$1B`, `$1C` → MCDelay, `$1D`, `$20`-`$2E`, `$30` turbo_reg) |
| P4 | end of `spriteSpriteCollision` (`if M2MClr = '1' then … end if; end if;`) | append `if FAST_PORT and fast_clr_m2m = '1' then M2M <= (others => '0'); M2Mhit <= '0'; end if;` |
| P5 | end of the `myRd`/`"011111"` block in `spriteBackgroundCollision` | append `if FAST_PORT and fast_clr_m2d = '1' then M2D <= 0; M2DDelay <= 0; M2Dhit <= '0'; end if;` |
| P6 | `end architecture;` | insert `fastPort: if FAST_PORT generate` with `fastRead: process(all)` (copy of the read case, `$1C` → MCDelay), the holds and the busy output, plus a `not FAST_PORT` generate that drives constants (below) |
| P7 | generated header | "Local changes: …, fast register port (FAST_PORT)" |

```vhdl
fastPort: if FAST_PORT generate
    fastRead: process(all) begin
        case fast_rd_addr is
        -- identical to readRegisters (VIC:1772-1822) except:
        when "011100" => fast_rd_data <= MCDelay;   -- MC lags MCDelay by <= 1 pixel
        ...
        end case;
    end process;
    -- accepted at phase p -> lands at e_(p+1)
    fast_hold_d011 <= '1' when fast_phase >= 10 and fast_phase <= 13 else '0';          -- H1
    fast_hold_spr  <= '1' when vicCycle = cycleSpriteA and sprite = 0
                           and fast_phase >= 14 and fast_phase <= 29 else '0';          -- H2
    fast_native_busy <= we_r;
end generate;
noFastPort: if not FAST_PORT generate
    fast_rd_data <= (others => '1'); fast_hold_d011 <= '0';
    fast_hold_spr <= '0'; fast_native_busy <= '0';
end generate;
```
The file already builds as VHDL-2008, and `process(all)` is used in this repository (create_project.tcl:173; multitini_c64_6510_port.vhd:41) [V].

### 4.2 `fpga64_sid_iec.vhd`

- New generic `FAST_IO : boolean := false`, kept separate from `EXTERNAL_CPU` so a D1 fallback build is possible.
- VIC instance: `FAST_PORT => FAST_IO`, `fast_phase => to_unsigned(sysCycleDef'pos(sysCycle), 5)`.
- New entity ports, all inputs with defaults: `ext_fio_vic_rd_addr(5:0)` in, `ext_fio_vic_rd_data(7:0)` out, `ext_fio_vic_wr`, `ext_fio_vic_wa(5:0)`, `ext_fio_vic_wd(7:0)`, `ext_fio_vic_clr_m2m`, `ext_fio_vic_clr_m2d` in, `ext_fio_vic_hold(1:0)` out, `ext_fio_vic_busy` out.
- Pure wiring otherwise.

### 4.3 New `hdl/core/c64/multitini_c64_fio_port.sv` (c64_clk)

- The pop stage of section 3.5: accept, holds, native barrier, registered strobes, `fio_rp`, `fio_done`, turbo_regs snoop.
- The SID sub-design adds its target, busy and holds to the same stream.
- Assertions: no `vic_wr` while the VIC's stock `myWr_*` or `myWr_phi1` fires; no accept during reset; `fio_done` never passes `wp`.

### 4.4 `multitini_c64_cpu_island.sv` (new in the baseline)

- FIO class rules of section 3.2: the `isl_x_fio_mem` FIFO, `wp`, `en_q`, `busy_q`, `all_done_q`, `full_q`, `ack_wait_q`.
- `io2_q` shared with COL.
- The FIO leg of `fio_rd` with the U64 override.
- `all_done_q` gating of forward issue and of the switch to exact mode.
- Register prefix `isl_x_` (baseline T3).

### 4.5 Adapter, turbo_regs, docs

- Adapter: wire island ↔ fio_port ↔ fpga64; `FAST_IO => 1`.
- turbo_regs: second write snoop from fio_port; outputs `d030_rd`, `d031_rd`, `d030_live`, `d031_live` for the island.
- `C64_CPU_CAPS` bit 23 "fast internal I/O" = 1.
- `docs/C64_RAM_CPU.md:68`: replace "I/O accesses use native slots" with the Q1 rule and the landing rule.
- New `docs/C64_TURBO.md`: section 3.16 details.

### 4.6 Build generics and fallbacks

| Generic | Default | Fallback |
|---|---|---|
| `FAST_VIC_IO` (island) | 1 | 0 = forward VIC accesses at φ2 (old D1), for bring-up |
| `VIC_READ_GAP` | 2 | 3 if E1 fails on the read path (index 15: 0.095 φ2 per read; still turbo rate) |
| `FIO_RAW_PRECISE` | 0 | 1 after O2 |
| `FIO_DEPTH` | 4 | 2-8 |

---

## 5. Timing and resources

### 5.1 New paths and crossings (AGENTS.md review)

| ID | Path | Requirement | Logic estimate [I] |
|---|---|---|---|
| X5 | island `isl_x_fio_mem`/`wp` (fast) → fio_port accept → strobes, `fio_rp` (c64) | 1 fast period (15.28 ns NTSC) | 4:1 mux, 6-bit compare, hold AND: about 4 LUT levels, 4-5 ns |
| X6 | `fio_rp`, `fio_done`, `we_r`, `d03x_rd/live` (c64) → island registers (fast) | 1 fast period | direct or 1 LUT |
| X7a | island T65 A (fast) → fpga64 → VIC read mux → `u64`/SID select → `io2_q` (fast, rising) | 1 fast period | T65 A about 3 LUT + carry (C64_TURBO_T65_CORE.md §3 [V]), mux 3 levels with F7/F8, select 1-2 levels, two chip-crossing routes: about 10-11 ns, slack about +4 ns |
| X7b | VIC registers (c64) → read mux → `io2_q` (fast) | 1 fast period (c64 rise → next fast rise) | mux and select: about 5 ns |
| X8 | `io2_q` → DI mux → T65 (BAL adder) | full fast period | same as the existing B0 path (fits [V C64_TURBO_T65_TIMING.md §3]) |
| — | `vic_wr/wa/wd/clr` (c64) → about 270 VIC register CE/D | 1 c64 period (31.7 ns) | 2 levels, fanout about 9 per decode |

- No new clock.
- No new reset: the FIFO and the pop stage use R1/R2.
- No synchroniser, false path or multicycle.
- No new half-period path, so the duty-cycle term (baseline §3.1) does not apply.
- Baseline T4 ("every c64↔fast path has a one-fast-period requirement and positive slack") covers these paths. No new XDC is needed.

**Load risk.**
- The read mux puts about 136 extra loads on T65 A0/A1 and about 40 on A2/A3.
- A is also the critical half-period path to port-A BRAM: 5.87 ns, 7 levels OOC [V C64_TURBO_T65_CORE.md §3].
- Vivado `phys_opt` normally replicates the driver LUTs. E1 must include the read mux to confirm that the BRAM path is unchanged (R-V1).

### 5.2 Resources [I]

| Block | LUT | FF |
|---|---|---|
| VIC read-mux copy (8 × 64:1, constants folded) | ~100 | 0 |
| VIC fast write legs and decode | ~110 | 0 |
| VIC holds, clears, busy | ~15 | 0 |
| fio_port | ~45 | ~40 |
| Island FIFO (4 × 21), pointers, barriers, ready terms | ~70 | ~100 |
| `fio_rd` select and override (`io2_q` shared with COL) | ~20 | ~8 |
| turbo_regs fast snoop and read values | ~25 | ~5 |
| **Total** | **~385** | **~150** |

No BRAM. LUT use goes from about 48 % to about 49 % of the baseline's 25,450. The SID share of the FIO port is not counted.

---

## 6. Tests

All benches use one process for both clocks (baseline T7). PAL and NTSC (6567R8) modes are tested. NTSC is in scope (Q2).

**T1 `tb_c64_vic_fast_port`** (VIC entity alone, `FAST_PORT` = true)
- **R1** For 10k random register states loaded by stock writes, `fast_rd_data` equals a stock read (`do` at e_23) for all 64 addresses. `$1C` is compared after one pixel edge. `$2F-$3F` read `$FF`; `$30` reads `$FF` with `turbo_en` = 0.
- **W1** Twin VICs: a stock write and a fast write of each address with random data give identical user-register state once both have landed.
- **W2** A fast strobe in each of the 32 phases changes the register exactly at the edge that ends that phase.
- **W3** `fast_hold_d011` is high exactly for phases 10-13. `fast_hold_spr` is high exactly for phases 14-29 of the sprite-0 `cycleSpriteA` cycle (PAL cycle 58, NTSC 6567R8 sequence).
- **C1** `$1E`/`$1F` clears act at the strobe edge, data sampled one fast clock earlier is intact, M2Mhit/M2Dhit re-arm, and IMMC/IMBC rise again on a later collision.
- **I1** `$D019` acknowledge: the flags drop one edge after landing. `$D01A`: IRQ follows the landing edge.
- **F0** `FAST_PORT` = false: `port_c64_vic.py --check`, test_c64_vice_vic, test_c64_mame_vic and the VICE bus suite pass unchanged.

**T2 `tb_c64_vic_fast_equivalence`** (proof of section 3.7)
- DUT: a VIC driven by random fast writes, at random edges, through fio_port with the holds.
- REF: a VIC driven by the mapped stock writes. A landing at e_q of cycle k becomes a stock write in cycle k−1 if q ≤ 14, else in cycle k, with the last write per register per window winning.
- Compare strictly at every e_15:
  - vicCycle, sprite, rasterX/Y;
  - VC, VCBASE, RC, idle, `rasterEnable`;
  - MDMA, MCBase, MCnt, MYE_ff, MActive, MPtr;
  - `fetchBMM`/`fetchECM`, `baLoc`, `ba_reu`.
- Compare IRST, IMMC, IMBC and ILP with a one-cycle tolerance after a `$D011`/`$D012`/`$D019`/`$D01A` landing.
- Compare the p11 fetch address and the p27 c-access address of every cycle. Two data-path differences are allowed by 3.7(c):
  - the p11 fetch of cycle k after a `$D018` landing at e_11..e_14 of cycle k;
  - the p27 c-access of cycle k after a `$D018` landing at e_15..e_26 of cycle k.
- Coverage: badlines, sprites, sprite multiplexing and `$D011`/`$D015`/MY storms.
- **Negative controls.** H1 off must give a `fetchBMM` mismatch. H2 off must give an MPtr/MActive mismatch.

**T3 `tb_c64_machine_island`, FIO additions**
- Scoreboard of VIC register writes in program order, with no loss or duplication.
- One clear per `$D01E`/`$D01F` read bus cycle, including `STA $D01E,Y` dummy reads and RMW.
- RAW: `STA $D020 / LDA $D020`, `STA $D01A / LDA $D019`, `LDA $D01E / LDA $D01E`.
- Forward barrier: a REU stash of `$D000-$D02E` right after fast writes contains the new values.
- Exact↔turbo switches with entries pending.
- REU fetch into `$D020` followed by an immediate fast write to `$D020`: the fast value wins.
- All 16 mirrors (`$D040`, `$D3C0`, …).
- U64 decode: `$D031` vs `$D071`, `$D07A`/`$D07B` writes, `$D030` in TurboEnable mode.
- Badline timing on (FIO halts on BA = 0) and off (FIO runs through BA = 0).
- Assertion: no turbo VIC access ever reaches the native slot.

**T4 Rate benches.** The `meas_c` and Amiga64 M5 methods in simulation. Expected: the section 3.3 table (0.0790 at index 15; equal to RAM at ≤ 12), and the section 3.15 loops within ±1 fast clock.

**T5 #203 pixel test.**
- Simulation: `colorIndex` trace at pixel edges for the unrolled `$D020` loop.
- Expected run lengths: index 14 mostly 1, with a 2 every about 46 pixels; index 15 per the model.
- Board: the same program, read through the frozen-frame capture.

**T6 Raster splits and sprite multiplexers at turbo.**
- **IRQ-driven splits** (border/background change at a raster line; `$D011` YSCROLL per line): at index 0 (exact), 12 and 15, with badline timing on and off. Expected: the change on the same line as at 1x, earlier in the line, and no extra lines.
- **IRQ-driven multiplexer** (project-written, rewriting MY and pointers in raster IRQs): frame captures equal to the 1x reference for sprite pixels whenever the writes land before the cycle 55/56 DMA decisions. Any difference is checked against the T2 mapping.
- Licence check for any third-party demo used (baseline §6.4).

**T7 E4 (IRQ).** At indices 15/14/12: `STA $D019`, `LSR $D019`, `ASL $D019`, `STA $D01A` then `RTI` give no re-entry. Handler entry latency while polling `LDA $D012/CMP/BNE`.

**T8 M2 lockstep** with `FAST_IO` = true and idle: identical to `FAST_IO` = false. Assert that `fast_wr`, the clears and the FIFO stay idle in exact mode.

**Board gates** (after all simulation passes):

| Gate | C64U | Expected |
|---|---|---|
| Amiga64 M5 `$D012` at index 15 | 0.080 | about 0.079-0.080 |
| Amiga64 M5, indices 0-12 | `$D012` = RAM | same |
| `meas_c` io column | RAM-equal to 32 MHz | same |
| #203 pattern at index 14 | single pixels, glitch about every 48 | same |
| GNW64 / xahmol detection | detects U64 | same |

---

## 7. Risks

| # | Risk | Experiment | Fallback |
|---|---|---|---|
| R-V1 | The read path (X7a) or the extra load on T65 A bits slows the half-period A → BRAM path, worst at NTSC 65.45 MHz | E1 OOC sweep including the read mux, the U64 override and the `io2_q` leg; M1 full image | Driver replication / `MAX_FANOUT`; `VIC_READ_GAP` = 3 (+1 slot at N ≥ 33) |
| R-V2 | U64 landing behaviour is unknown (closed FPGA); H1/H2 are Multitini invariants | O1 on a C64U; T2 | Adjust the holds; documented detail |
| R-V3 | FIFO or barrier bugs (lost or reordered writes, double clears, stale reads) | T3 scoreboard, assertions, mode-switch fuzz | `FAST_VIC_IO` = 0 |
| R-V4 | IRQ re-entry if `ack_wait_q`/done tracking is wrong | T7 | Wider barrier (wait for one more c64 edge) |
| R-V5 | Stock regression through the VIC patches | F0, T8, VICE/MAME VIC suites | `FAST_PORT` = false |
| R-V6 | A third hazard like H1/H2 that section 2.5 missed | T2 long fuzz with negative controls | Add a hold |
| R-V7 | The SID sharing the FIFO needs other landing rules (e.g. SID register sampling at `ce_1m`) | SID sub-design | Per-target holds in fio_port |
| R-V8 | Read-after-write or ACK wait costs differ from the C64U | O2, O3, O4 | `FIO_RAW_PRECISE` |
| R-V9 | A future HMOS (8565) option: the grey dot is not emulated for fast writes | — | Document; variant is fixed "00" today |

---

## 8. Open questions

| # | Question | Experiment |
|---|---|---|
| O1 | Where does a C64U VIC write land relative to its slot (pixel offset)? | C64U at index 15: sync on a `$D012` change, run n cycles of `NOP`, then `STA $D020`. Capture the colour-change column against n, and compare with the T5 simulation |
| O2 | Does the C64U add a read-after-write penalty? | CIA-timed unrolled `STA $D021 / LDA $D012` vs `STA $D021 / LDA $3000` at index 15 |
| O3 | Cost of VIC RMW on the C64U | Unrolled `INC $D020` vs `INC $3000` |
| O4 | IRQ after acknowledge at turbo on the C64U | `STA $D019 / RTI` re-entry counter (expected 0); count instructions run between a `$D012` write that matches and the IRQ |
| O5 | Does the read path (full period) close at NTSC, and does the half-period A → BRAM path still close, with the duty term, once the read mux loads the address bits? | E1, M1 |
| O6 | `$D01E` clear window at turbo on the C64U | Read `$D01E` twice with a known overlap; see whether new collisions appear |
| O7 | Should `$D011`/`$D012` writes join the ACK barrier (IRQ assertion latency)? | Decide after O4; cost about 7 fast clocks per raster-compare write |
| O8 | Does any title rely, under turbo, on stock's split landing (RSEL at e_31 vs the other `$D011` bits at e_3; XSCROLL at e_7)? | T6 demos; board |
