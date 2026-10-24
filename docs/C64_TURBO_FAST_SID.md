# Multitini One C64 turbo: fast SID register path (v2 sub-design)

Sub-design written on 2026-10-23 for the v2 draft of [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). The current design (v3) merges it; where the two differ, C64_TURBO_DESIGN.md wins. Design stage only: no HDL, firmware, script or build file was changed for it, no build or simulation was run, and nothing here is board-tested. Paths to throwaway session files were removed. `issueN.txt` means a downloaded copy of GideonZ/1541ultimate issue #N with its comments; other bare file names that are not in this repository also name downloaded copies read during the design. Those copies are not kept. The models are `sid_fio_model.py` and, for the edge stage and the audio-identity bench, `sid_edge_stage.py` and `svf_persistence.py`, all in [C64_TURBO_MODELS.md](C64_TURBO_MODELS.md); the Doom sources that `sid_fio_model.py` cites were downloaded copies (data only, never run).

Tags: **[V]** verified (file:line or URL); **[M]** measured by a third party; **[I]** inference or design choice; **[O]** open, with the experiment that settles it. Nothing here has run on the board, and no hardware support is claimed.

Baseline: the v1 turbo design, an earlier draft of C64_TURBO_DESIGN.md that is not kept (sections 1.7, 2.5, 2.6, 2.13, 5). Binding decisions: Q1 (VIC/SID at turbo rate; internal I/O proceeds during BA low when badline timing is disabled), Q2 (NTSC in scope), Q3 (local `$00/$01` writes).

---

## 1. Summary

- **What changes.** In turbo mode, a CPU access to `$D400-$D7FF` (I/O visible) no longer uses the native φ2 slot. The island sends it through the shared fast-I/O (FIO) request to the c64 domain. The SID executes it at the first `c64_clk` edge after the request, with no stall. The island retires the bus cycle one fast clock later. A SID read or write costs 3 or 4 fast clocks instead of one φ2. This retires D1 and D5 for the SID.
- **Reads.** The read value is computed at the execute edge from the same sources as stock: POTX/POTY, OSC3, ENV3, or the per-chip "last value" latch. It is loaded into the proxy's B0 data register and given to the T65 one fast clock later. Reads have no side effect on the SID engine. The only state they touch is the per-chip bus latch, which they update exactly as stock does.
- **Writes.** Writes are not posted and not queued. Each write lands directly in the existing register file at its execute edge. The engine samples registers at fixed edges, once per native cycle, so several writes inside one µs collapse to the value present at the sample edge. One addition (generic `EDGE_STAGE`, default 1): a second write to a voice **control** register (`$x04/$x0B/$x12`) inside one engine sample window is held and applied at the next engine step. Isolated gate and test pulses (at most one per register per 2 µs) and isolated RMW double writes are therefore seen by the engine as they would be at 1 MHz. Denser sequences are compressed (3.4).
- **Second SID at `$DE00`/`$DF00`** (HDL modes 1 and 4, not offered by the firmware menu): stays a φ2-paced I/O1/I/O2 forward, like every other I/O1/I/O2 access and like the U64's measured I/O1 timing.
- **Ordering.** One island access is in flight at a time, and every FIO access completes before the next bus cycle. Program order with RAM, VIC, CIA, I/O1/I/O2 and REU DMA follows without a queue or barriers.
- **Cost.** About 180 LUT and 65 FF, all in the c64 domain. No BRAM, no new clock, no new XDC. The new paths are single-fast-period paths of the existing X1/X2 kind.
- **Expected timing [I].** Unrolled `lda $d419` costs 0.095 φ2 per read at index 15 (C64U: 0.080-0.085 [M]). It costs the same as a RAM read through index 9. The Doom 25-register SID burst takes about 8.6 µs at index 15, against about 56 µs in the engine build (D1).

---

## 2. Verified facts

### 2.1 The SID implementation in this repository

| # | Fact | Source |
|---|---|---|
| F1 | `sid_top` holds one register file per chip, written on **any** `clk` edge where `cs[i]` and `we` are both high. `we` is not qualified by `ce_1m` | [V] sid_top.sv:179-230 |
| F2 | The bus latch `bus_data[i]` takes `data_in` on every write. On a read (`cs[i]` with `we` = 0) it takes POTX, POTY, OSC3 or ENV3 for `$19-$1C` and is otherwise unchanged. `data_out = cs[0] ? bus_data[0] : bus_data[N-1]` | [V] sid_top.sv:201-202, 231-238, 344 |
| F3 | Reads of write-only registers return the last byte written to that chip, or the last `$19-$1C` value read from it. The latch never decays | [V] sid_top.sv:89, 201-238 (no timer) |
| F4 | A read has no other effect. No register clears on read, and no engine block has a read input | [V] sid_top.sv:231-238; sid_voice.sv:3-27; sid_envelope.sv:2-13 |
| F5 | Engine step: `ce_1m = enableSid and not memory_wait_q`. `enableSid` is registered at CPUF, so it is high during phase 0 and the engine steps at e_0 | [V] fpga64_sid_iec.vhd:563-587 (581), 828 |
| F6 | Oscillator, test, sync, waveform, OSC3 (`wave_out`) and DCA update only under `ce_1m` | [V] sid_voice.sv:66-74, 82-131, 175, 179-195 |
| F7 | The envelope samples `gate` only under `ce_1m` and detects edges against the previous sample (`gate_edge`) | [V] sid_envelope.sv:69-106 (100-105) |
| F8 | Shared filter scheduler: `state` resets to 0 at `ce_1m`, counts to 15 and holds. Chip 0 uses states 0-7 and chip 1 uses states 8-15 (`n = state[3]`) | [V] sid_top.sv:245, 268-299 |
| F9 | Filter register sample edges. Chip 0: `Res_Filt`, `Mode_Vol[7]` and F0 at e_3 (state 2); `Mode_Vol[6:4]` at e_6 (state 5); `Mode_Vol[3:0]` at e_8 (state 7). Chip 1: e_11, e_14, then every edge e_16..e_0 while `state` holds at 15. Chip 1 publishes its sample one edge later, so the sample left at e_0 carries `Mode_Vol[3:0]` as sampled at e_31: a chip-1 volume write landing at e_31 is heard one native cycle later than a stock e_29 write | [V] sid_filter.sv:136-215 (210); sid_top.sv:274, 303-318, 334-340 |
| F10 | `Fc` reaches F0 through one register stage (`f0`, every clock). F0 is combinational from `f0` because `Fc_offset` = 0 and the model is 6581 | [V] sid_tables.sv:783, 1315; adapter:538-541 |
| F11 | Combined-waveform lookups sample `acc_t` (oscillator, control bits 5 and 2) at states 1-11. The waveform select (control 7:4) is applied at `ce_1m` for `comb`, the waveform-on test and OSC3. The single-waveform output reaches `dac_out` through `norm_dac`, which registers it on every clock, so `dac_out` uses control 7:4 from one edge before `ce_1m`: a write landing at e_31 misses one `dac_out` step | [V] sid_top.sv:277-297; sid_tables.sv:693-726; sid_voice.sv:76, 138-154, 163-164, 175, 184-187 |
| F12 | Audio leaves the core point-sampled at 48 kHz | [V] adapter:655-673 |

### 2.2 How CPU accesses reach the SID today (stock)

| # | Fact | Source |
|---|---|---|
| F13 | The CPU slot issues at phase 28 and retires at phase 31 | [V] scheduler.vhd:42-44, 60-61 |
| F14 | `cpuHasBus` is set at e_15 and cleared at e_31. `cs_sid = cs_sidLoc and io_enable`, so `cs` is high during phases 16-31 | [V] fpga64:518-533, 555-558, 1065-1073; buslogic:217-241, 319 |
| F15 | `pulseWr_io` is high during phase 29 only, so a SID write lands at **e_29** | [V] fpga64:666-676, 829-830 |
| F16 | Stock write to `$19-$1C`: `bus_data` takes `data_in` at e_29. The read branch then reloads the live value at e_30 and e_31, so the latch ends holding the live value, not the written byte | [V] sid_top.sv:200-238 with F14/F15 |
| F17 | Stock read: the latch updates at each edge e_16..e_30. The CPU samples `cpuDi` = `sid_do` at R (e_31), which gives the value as of e_30 | [V] buslogic:172-173; F2; F14 |
| F18 | A stock write is first seen by the voices at e_0 (two c64 clocks later), and by the filter at e_3-e_8 (chip 0) or e_11-e_16 (chip 1) of the next native cycle | [V] F5, F9, F15 |
| F19 | REU DMA writes use the same native path (`cpuAddr/Do/We` are muxed from `dma_*`) | [V] fpga64:1094-1096, 666-676 |
| F20 | Pots: `pot1..4` are tied to `$FF` in the adapter, so POTX/POTY are `$FF` or `$00` depending on CIA1 PA6/PA7. Chip 1 pots are `$FF` | [V] adapter:536; fpga64:819-822, 834-839 |

### 2.3 Dual-SID addressing that Multitini supports

| `sid_mode` | Left chip select | Right chip select | Offered by firmware |
|---|---|---|---|
| 0 Single | all `$D400-$D7FF` | same as left (both chips written; reads return the left latch) | yes [V] supervisor_settings.c:328-331; core_frontend.c:130-132 |
| 1 | all `$D400-$D7FF` | `$DE00-$DEFF` (I/O1, `ioe_i`) | no (HDL only) |
| 2 Dual `$D420` | A5 = 0 | A5 = 1 | yes |
| 3 Dual `$D500` (**default**) | A8 = 0 | A8 = 1 | yes [V] supervisor_settings.h:21 |
| 4 | all `$D400-$D7FF` | `$DF00-$DFFF` (I/O2, `iof_i`) | no |
| 5-7 | all `$D400-$D7FF` | never | no |

- Source of the table: [V] fpga64:815-817.
- Registers mirror every 32 bytes (A4-A0) within each chip's range.
- `sid_mode` is `expansion_config[2:0]`, crossed into `c64_clk` by an existing mailbox [V] multitini_one_top.sv:1187-1206; multitini_c64_expansion_config.sv:1-45. The fast path needs no new crossing for it.
- In modes 1 and 4, a cartridge's `io_ext` data has priority over the right SID on reads [V] fpga64:817.

### 2.4 What the U64/C64U does

| # | Fact | Source |
|---|---|---|
| U1 | C64U, index 15, φ2 ticks per read: RAM 0.064, `$D419` 0.085, `$D41A` 0.080, `$DC01` 1.040, `$DE00` 1.045. Through index 12, a `$D419` read costs exactly what a RAM read costs; at 13-15 it costs about 1 extra turbo cycle | [M] amiga64_HARDWARE.md:1301-1313 |
| U2 | Nobody has published a SID **write** timing measurement. That is what "writes untimed" means in the spec. The Doom repository has the sidtest and irqtest sources and checkers but publishes no result table | [V] C64_TURBO_U64_SPEC.md:112; `gh api` search of slesinger/doom (tools/sidtest.py, tools/irqtest.py, IMPLEMENTATION_PLAN.md contain no result) |
| U3 | GideonZ: internal accesses can use any of the 64 slots; only external-bus accesses, such as real SID sockets, go through the 1 MHz bus bridge and wait for BA = 1. VanessaE: "REU, UltiSID, CIA's all seem to be immune to BA shenanigans when badlines are disabled" | [V] issue665.txt (GideonZ 2026-03-23; VanessaE 2026-03-23) |
| U4 | In the public UltiSID source, a write is one `wren` strobe into a register array with a one-clock pipeline. There is no queue and no rate limit | [V] 1541ultimate d43546f `fpga/sid6581/vhdl_source/sid_regs.vhd`:152-202 |
| U5 | The UltiSID ADSR reads `gate` as a **level** once per voice iteration (attack/decay plus gate = 0 → release; release plus gate = 1 → attack). The oscillator applies `test` as a level per iteration | [V] adsr_multi.vhd:166-200; oscillator.vhd:59-90 |
| U6 | The UltiSID register block returns `$00` for every read except OSC3 and ENV3. It has no bus latch. Pots are served elsewhere in the U64 core | [V] sid_regs.vhd:204-209; [M] amiga64:1301 ("served from the core") |
| U7 | The U64/C64U FPGA top level is not public. The clone holds only bitstreams | [V] `git ls-tree` of d43546f: external/u64.sof, external/u64e2_*.bit, recovery/u64ii/u64_mk2_artix.bit |
| U8 | UltiSID addresses can be set to `$DE00-$DFE0` on the U64 | [V] u64_config.cc:218-226, 412-417 |
| U9 | The UltiSID sources say they "are not supposed to be used in other projects without written permission". They are used here only as behavioural evidence. No code is copied | [V] sid_regs.vhd:9-10 |

**Reading of U2-U6 [I].** On the U64/C64U, an UltiSID write most likely lands directly in the register file at turbo rate, with no pacing. Pulses shorter than one engine iteration are then lost. Whether the U64 does this is [O] (E7-S).

### 2.5 reSID reference for the bus latch

- [V] reSID `sid.cc` (VICE svn-mirror, `vice/src/resid/sid.cc`, https://raw.githubusercontent.com/VICE-Team/svn-mirror/main/vice/src/resid/sid.cc):
  - `write()` sets `bus_value = value` for every register, including `$19-$1C`;
  - `read()` updates `bus_value` only for `$19-$1C`;
  - the value decays to 0 after `databus_ttl` = `0x1d00` cycles (6581) or `0xa2000` cycles (8580).
- Two stock MiSTer differences are inherited unchanged and are not caused by turbo:
  - **S3:** after a write to `$19-$1C`, the latch holds the live value, not the written byte (F16).
  - **S4:** the latch never decays (F3).

---

## 3. Design

### 3.1 Access classes after this change

| Address (I/O visible) | Exact mode | Turbo, badline timing enabled | Turbo, badline timing disabled |
|---|---|---|---|
| `$D400-$D7FF` (both internal chips, every mode) | native slot, unchanged | FIO; the island halts while BA = 0 | FIO; proceeds during BA low (Q1) |
| `$DE00`/`$DF00` SID (modes 1 and 4) | native | φ2 forward (I/O1/I/O2 class) | φ2 forward. BA-low behaviour follows the I/O1/I/O2 rule in the companion I/O-only-ownership design |
| REU DMA to SID registers | native | native, CPU halted | native, CPU halted |

- The island class is the baseline FIO class: page D, I/O visible, A11 = 0 [V baseline 2.5].
- The c64 side separates VIC (A10 = 0) from SID (A10 = 1). The island needs neither `sid_mode` nor A10.

### 3.2 FIO request (shared with the VIC fast path)

The SID needs only the following from the shared FIO port. If the VIC sub-design names it differently, only the names change.

**Island → c64.** These fields are registered on the fast rising edge. They reuse the baseline `isl_x_req` register, with one added class bit.

| Field | Meaning |
|---|---|
| `isl_x_req.addr[15:0]`, `.we`, `.wdata[7:0]` | the T65 bus cycle |
| `isl_x_req.fio` | 1 = FIO class. The frame register ignores FIO requests, so they are never loaded at e_15 |
| `isl_x_req.seq` | flips once per island bus cycle that leaves the island (forward or FIO) |

**c64 side (proxy, at every `c64_clk` edge):**
- `fio_exec = isl_x_req.fio and (isl_x_req.seq /= done_seq)`.
- On `fio_exec`:
  - `done_seq <= seq`;
  - `B0.data <= ext_fio_rdata`.
- Forwards and dummy retires never load B0 while an FIO access waits.
- **The SID target never stalls.** It executes at the first c64 edge E after the request edge, whatever the state of `memory_wait`, pause or BA. Its register file is not gated by any of them today (F1).

**Island completion.**
- Static schedule: the T65 enable that retires the FIO bus cycle comes at the first fast edge at or after E+1 (fast) that is granted, `~halt_q` and `run_q`. DI comes from B0.
- E is known from the request edge's parity h: E = end `f_a + 1 + h`. So the cycle takes 3 fast clocks (h = 0) or 4 (h = 1).
- If the VIC target can stall, the island instead waits for `done_seq` re-registered on the fast edge. That costs one more fast clock per FIO access, VIC and SID alike.

Timeline (V-R, the default):

| Edge | Domain | Action |
|---|---|---|
| end f_a−1 | fast | T65 enable for cycle k−1. Cycle k's A, R/W and DO settle |
| during f_a | fast | Class decode = FIO (registered page map, A15:A10). No enable |
| end f_a | fast | `isl_x_req` ← {A, we, DO, fio = 1}; `seq` flips |
| E = end f_a+1 (h = 0), or end f_a+2 (h = 1) | c64 | `fio_exec`. SID: write → register file (or stage), latch rule; read → latch rule. Proxy: `B0.data` ← `ext_fio_rdata`; `done_seq` ← `seq` |
| end E+1 (fast) | fast | T65 enable for cycle k, if granted, `~halt_q` and `run_q`; DI = `B0.data` |

**Exactly once.**
- The island issues one `seq` flip per FIO bus cycle. Indexed-store dummy reads, both RMW writes and page-cross dummy reads each count as their own bus cycle.
- The island never re-issues while it waits for a grant, BA or `run_q`.
- The c64 side executes on `seq ≠ done_seq` only.
- B0 holds the read value until the T65 consumes it, because nothing else can load B0 while one access is in flight.

**Variant V-C [O, E1].**
- The c64 side captures the T65's combinational address and class directly at the first c64 edge at or after f_a. Exactly-once comes from an island cycle toggle that flips on every T65 enable.
- That gives 2 or 3 fast clocks per FIO cycle.
- The cost is a full-fast-period path from the T65 internals through the address logic and SID decode to a c64 flop, plus extra load on the T65 address nets.
- Adopt it only if E1 shows positive margin at NTSC. In the unrolled-read steady state it gains little: the parity locks to the 3-clock case (section 5.3).

### 3.3 SID fast port (c64 domain)

Select, computed in fpga64 on the FIO address:
```
fio_sid   = ext_fio_exec and A(15..10) = "110101"
left_term = (sid_mode(0) = '0' and A(5) = '0') or (sid_mode(0) = '1' and A(8) = '0')
fio_sel_l = fio_sid and (sid_mode(2..1) /= "01" or left_term)
fio_sel_r = fio_sid and (sid_mode = 0 or (sid_mode(2..1) = "01" and not left_term))
                                              -- modes 1, 4, 5-7: right chip never selected here
```
This equals fpga64:815-816 for every address in `$D400-$D7FF` with `ioe_i` = `iof_i` = 0. An exhaustive bench checks it (T-S1c).

**Execute at E, per chip i with `fio_sel[i]`, register offset a = A4..A0:**

| Access | Register file | Latch `bus_data[i]` after E | Value returned (`fio_rdata`) |
|---|---|---|---|
| write, a ∈ `$00-$18` | same field update as stock (sid_top.sv:203-229), except staged control writes (3.4) | `wdata` | — |
| write, a ∈ `$19-$1C` | none | live value (POT/OSC3/ENV3 at E), as stock ends (F16, S3) | — |
| write, a ∈ `$1D-$1F` | none | `wdata` | — |
| read, a ∈ `$19-$1C` | none | live value at E | the same live value |
| read, other a | none | unchanged | `bus_data[i]` |

- `fio_rdata` uses the left chip when `fio_sel_l`, else the right chip. That is the stock `data_out` rule (F2), and in Single mode it returns the left latch.
- `fio_rdata` is combinational from the FIO address and SID flops. The proxy registers it into B0 at E.
- Semantics match stock: the value at the instant of the access. Stock returns the e_30 value (F17); the fast port returns the value at E. OSC3 and ENV3 change only at e_0, so a read anywhere in phases 1-31 sees that cycle's value either way.
- Architectural results are therefore identical in exact and turbo mode for every access sequence; only the timing differs. The latch rule copies the stock end state, including S3. Section 2.5 explains why that is kept rather than switched to reSID.

### 3.4 Write landing rule

**Rule [I]:**
1. A fast write executes at its E, in program order, one at a time. It is never posted or queued.
2. It updates the register file at E. The engine sees whatever value a register holds at that register's sample edge (F5-F11).
3. **Edge stage** (`EDGE_STAGE` = 1, default). This applies to each voice control register (`$x04`, `$x0B`, `$x12`) of each chip.
   - The sample window runs from one `ce_1m` edge to the next. A fast write executing on a `ce_1m` edge belongs to the new window.
   - The first fast write to the register in a window applies at E.
   - Any later write in the same window goes to a one-byte stage instead; the latest write wins. The stage is applied at the next `ce_1m` edge and counts as that window's first write.
   - A window that starts by applying a stage therefore sends all its own writes to the stage (latest wins), and the engine sees only its last value. In a window that starts without applying a stage, the engine sees the first and the last value, each for at least one engine step. A window starts with a stage when the window before it ended with one pending; one window with no write to the register ends the chain.
   - Contract: isolated pulses are kept, that is at most one gate or test pulse per register per 2 µs, with no other write to that register in the µs after the pulse (precisely: a pulse whose writes fall in one window is kept when that window starts without applying a stage; a pulse split across two windows is kept when each of its writes is the last write in its window). Denser sequences are compressed: two consecutive windows that each write gate 1→0→1 give engine gate samples 1 0 1 1 1, where four 1 MHz writes give 1 0 1 0 1 [I C64_TURBO_MODELS.md (sid_edge_stage.py)].
4. A native write (exact mode or REU DMA) to a control register cancels a pending stage for it, because the native write is later in program order. Reset clears all stages.
5. Native accesses keep the unchanged stock path. Native and fast accesses never meet in one edge (3.6). An assertion checks this, and RTL priority goes to native.

Stage logic, per control register r of chip i (pseudo-RTL; `w` = fast write to r executing at this edge):
```
if reset:              pend<=0; flag<=0
elif native_wr(r):     pend<=0; flag<=0             // the stock branch writes r itself
elif ce_1m:                                         // window boundary: the engine samples the old r here
    if pend and w:     r<=stage; stage<=wdata; pend<=1; flag<=1
    elif pend:         r<=stage;               pend<=0; flag<=1   // applied stage = first write of the new window
    elif w:            r<=wdata;               pend<=0; flag<=1
    else:                                      pend<=0; flag<=0
elif w:
    if flag:           stage<=wdata; pend<=1        // latest write in the window wins
    else:              r<=wdata;     flag<=1
```

### 3.5 Why the rule is safe for the engine and for digis

**Engine state machines** [V F5-F11; I]:
- Every engine consumer reads a register at one defined edge.
  - Voice registers are read at e_0 (`ce_1m`).
  - Filter registers are read at the state-2/5/7 edges, plus the chip-1 state-15 hold.
  - Fc passes through a single register stage (F10). A write landing mid-pipeline gives the old or the new F0, never a mixture.
- No consumer combines two samples of the same register taken at different edges, apart from three cases:
  - **Combined waveforms** (F11). The lookup uses the control bits from states 1-11, and the select uses bit 7:4 at e_0. Stock already mixes these on every control write: writes land at e_29, after every lookup. A turbo write landing before the lookup is consistent.
  - **Filter routing vs volume** (chip 0). A `$D418` write between e_3 and e_8 gives one µs with new volume and old routing. That µs has a 1-in-21 chance of being the one picked by the 48 kHz point sample (F12). Inaudible [I].
  - **Waveform DAC** (F11). `dac_out` takes the single-waveform output from `norm_dac` (control 7:4 from one edge before `ce_1m`) and `comb` and the waveform-on test from the live control. A fast control write landing at e_31 gives one engine step that mixes old and new; stock e_29 writes never do. One µs; inaudible [I].
- The 16-bit freq and pw pairs and the 11-bit Fc pair are not written atomically in stock either.
- No illegal state is reachable. The envelope's states change only on `ce_1m` from the sampled gate level (F7). The oscillator and noise LFSR react to levels at `ce_1m` (F6).
- **Lasting effects.** The only lasting effect of turbo landing is a lost sub-µs gate or test pulse. A lost pulse changes the envelope state, the oscillator phase or the LFSR for the rest of the note. The edge stage keeps isolated pulses (at most one per register per 2 µs) and compresses denser sequences (3.4):
  - A gate 1→0→1 inside one µs becomes one sample of release, then attack. That is the same envelope path as the 4-µs pulse that `sta`/`sta` gives at 1 MHz, including the rate-counter behaviour behind the ADSR delay bug.
  - An isolated RMW double write to a control register (old value, then new value) becomes one sample of each. That is sample for sample what stock does, where the two writes land one native cycle apart.
  - Denser sequences: a pulse can still be merged. Of two consecutive pulse windows, the second pulse is lost. The U64 most likely drops every sub-µs pulse (U5; E7-S), so this is no worse than the reference [I].
- **Stage latency.** The stage delays a write by at most one µs, and only when another write to the same control register landed less than 2 µs before. In exact mode the stage never triggers, because native writes cancel stages and never stage. Stock SID behaviour, stock REU DMA writes to the SID and the Expand gate (G-REU0 of C64_TURBO_DESIGN.md) are unchanged.

**Digi playback** [I unless marked]:
- **4-bit `$D418` digis** (timer or NMI driven, one write per sample at 4-22 kHz):
  - chip 0 samples the write at the next e_8;
  - chip 1 follows it within two c64 clocks during phases 16-0 (F9).
  - The added jitter is under 1 µs, against a 45-250 µs sample period and a 20.8 µs output sampling grid (F12).
- **BA.** With badline timing disabled, the write proceeds during BA low, so the #665 growl does not occur [V issue665: the U64 fix].
  - The NMI acknowledge `lda $dd0d` is a CIA forward. Without the companion BA-independent CIA access, an NMI handler that acknowledges first and writes `$D418` second still jitters on badlines.
  - The `$8F` gate therefore needs both sub-designs.
- **Badline timing enabled.** The island halts for BA, as on a stock C64 and on the U64 [V turbo_mode.rst:164-176 via C64_TURBO_U64_SPEC.md D2].
- **REU fixed-address digis** (DMA to `$D418`): native path, 1 byte per φ2, unchanged (F19).
- **Mahoney-style 8-bit digis.** Voice set-up through test bits is a one-time step; playback is `$D418` writes. Both are covered.
- **Test-bit pulse DAC techniques.** The edge stage keeps an isolated test pulse (at most one per voice per 2 µs, 3.4). A denser sequence is compressed: two consecutive pulse windows give one oscillator reset instead of two.
- **Lost value.** A level-register value overwritten inside the same µs is lost, for example the first write of `inc $d418`. At 1 MHz it would have lasted one µs, and the point sample at 48 kHz cannot resolve it.

**Rejected alternatives:**
- **Paced queue (one write per µs).** Stalls the CPU (25 µs per player burst), diverges from the untimed U64 writes, and moves t = 0 in sidtest.
- **Keep every gate/test pulse.** Needs backpressure. In the merged design SID and VIC writes share one FIFO (C64_TURBO_DESIGN.md 2.13.5), so a hold that keeps a control write at the FIFO head while that register's stage is pending also delays the VIC writes queued behind it, by up to 1 µs per held write, and a burst stalls the island on `full_q`. That is a partial paced queue (above). The narrower contract of 3.4 is kept instead.
- **Posted writes or a depth-4 queue.** Saves about 1 µs per 25-write burst at index 15. It adds ordering hazards for the latch, read-after-write, mode switches and DMA.
- **Island-side read shadow (2-slot reads).** Needs a half-period path from SID flops into the island, which is the R2 risk area.
- **Fast-clocked register file.** Changes the stock path.

### 3.6 Ordering with other accesses

| Pair | Why the order holds |
|---|---|
| FIO vs local RAM/ROM/colour | The island runs one bus cycle at a time; FIO retires before the next cycle. The SID cannot observe RAM |
| SID FIO vs VIC FIO | Same request register, one at a time |
| FIO vs φ2 forward (CIA, I/O1/I/O2, `$FF00`, `$DE00` SID) | A forward loads at e_15 and completes at R, always after the preceding FIO's E. Example: `sta $d418 / lda $dc0d` |
| FIO vs REU DMA | DMA starts only after a forwarded trigger. The island is in DMA_WAIT and issues no FIO. DMA SID writes are native; they cancel stages for the registers they write |
| FIO vs IRQ/NMI | The SID raises no interrupt, and SID accesses are not in the IRQ-acknowledge class. The completion enable samples live IRQ, and NMI through the hold-and-rearm rule of C64_TURBO_DESIGN.md §2.9, as any enable does |
| FIO vs exact/turbo switch | The island switches only at an enable boundary with no access in flight. FIO is non-posted, so the boundary is clean. A pending stage is internal SID timing: it applies at the next `ce_1m`, or a later native write cancels it |
| FIO vs pause, menu, loader | The SID executes at E regardless. Completion waits for grants, which stop while the frame is paused. The loader forces exact mode. Stages wait for `ce_1m`, which stops with the frame |
| Native and fast in one edge | Impossible. In turbo, the native slot carries the dummy (`$0000`), forwards (never `$D4xx`) or DMA (island halted). A simulation assertion and native-first priority back this up |

### 3.7 Second SID at `$DE00`/`$DF00`: decision

**Decision.** Keep it as a φ2-paced I/O1/I/O2 forward. Reasons:
1. U64 evidence covers I/O1 as an address range: one access per φ2 [M U1]. GideonZ describes the split as internal vs bus-bridge by address [V U3]. Whether an UltiSID mapped at `$DExx` (U8) is paced is [O] (E7-S part c).
2. The native path keeps the cartridge's `io_ext` read priority over the SID (fpga64:817) and the IO2 decode shared with the REU. A fast path would have to repeat both and would bring `sid_mode` into the island map.
3. The firmware does not offer modes 1 or 4 (section 2.3). The difference is timing only.

**Interface note.** In mode 1 or 4, the companion BA-independent I/O-only ownership must also cover I/O1/I/O2 SID writes (`sid_sel_r = ioe_i/iof_i` with `pulseWr_io`). Otherwise those writes wait for BA = 1 with badline timing disabled.

**If** firmware later offers these modes and E7-S shows the U64 runs them at turbo, add a `sid_io_fast` bit to B1 and the map. That is a small additive change.

### 3.8 NTSC

There is no SID-specific change:
- `ce_1m` follows the NTSC frame;
- the fast clock is 65.455 MHz (one fast period 15.28 ns);
- the 48 kHz resampler already uses `CORE_CLOCK_HZ` (adapter:655-673).

---

## 4. RTL changes (additive; generics keep today's text and behaviour by default)

| File | Change |
|---|---|
| `hdl/core/c64/ports/sid_top.sv` (Multitini port of MiSTer 62fc697) | Parameters `FAST_PORT` (default 0) and `EDGE_STAGE` (default 1). Ports `fio_cs[N-1:0]`, `fio_we`, `fio_addr[4:0]`, `fio_wdata[7:0]`, output `fio_rdata[7:0]`. In the register block (179-240): effective `addr`/`data`/`we` muxed from stock or fast (stock first); the fast write-to-`$19-$1C` latch rule; per-chip control-register stage (3 × {8-bit stage, pend, flag}); native control writes clear the stage; combinational `fio_rdata`. With `FAST_PORT` = 0, all of this is generated out. Simulation assertion `!(cs[i] && fio_cs[i])`. Header comment lists the change |
| `hdl/core/c64/ports/fpga64_sid_iec.vhd` | Under `EXTERNAL_CPU` (baseline 2.3): ports `ext_fio_exec`, `ext_fio_addr[15:0]`, `ext_fio_we`, `ext_fio_wdata[7:0]`, out `ext_fio_rdata[7:0]` (VIC/SID mux by A10, VIC side from its sub-design). `fio_sid` and `fio_sel_l/r` (3.3) are placed **after** the `sid : sid_top` instance. The component and instance gain the generic (`FAST_PORT => EXTERNAL_CPU`) and the `fio_*` maps. With `EXTERNAL_CPU = false` the `fio_*` inputs are tied idle |
| `hdl/core/c64/multitini_c64_cpu_proxy.sv` (new in baseline) | `isl_x_req.fio` handling: never loaded into the frame; `fio_exec` and `done_seq`; `B0.data` load from `ext_fio_rdata` at E |
| `hdl/core/c64/multitini_c64_cpu_island.sv` (new in baseline) | FIO state: one request per bus cycle; static completion at end f_a+2+h; DI = B0; completion gated by grant, `halt_q` and `run_q`; FIO counts as "in flight" for the mode-switch rule. Shared with VIC; nothing SID-specific |
| Unchanged | `sid_voice.sv` and `sid_envelope.sv` (third_party, pinned), `sid_filter.sv`, `sid_tables.sv`, `sid_dac.sv`, buslogic, scheduler, adapter SID wiring |
| `scripts/test_c64_mame_sid.py` | Its wrapper extracts fpga64 text from `--\tRight SID Port:` to the end of the instance (:42-76). Declare the new instance identifiers there (`sid_fio_sel_l/r`, `ext_fio_*`, `sid_fio_rdata`, an `EXTERNAL_CPU` constant = false) so the extracted span still elaborates with the fast port idle. Part of R10 anchor churn |
| `tests/test_audio_path.py` | Its anchors (:27-33) are unaffected; keep the state-15 publish text unchanged |
| `hdl/sim/tb_sid_output_schedule.sv` | Unchanged (`FAST_PORT` = 0 default) |
| `licenses/files.csv` | No new third-party file. `sid_top.sv` keeps GPL-2.0-or-later. No UltiSID code is used (U9) |
| Docs | `docs/C64_EXPANSIONS.md:20` "Both chips retain the native SID clock" stays true; add turbo access timing, the edge stage, and φ2 for the `$DE00`/`$DF00` modes. `docs/C64_TURBO.md`: SID section, S3/S4, D14/D15 (section 7). `docs/C64_RAM_CPU.md`: the Q1 rule text |

---

## 5. Timing and resources

### 5.1 Clocks, crossings and resets

- No new clock and no new reset. The SID uses `fpga64.reset`; the island uses R1.
- No synchroniser and no XDC change.

| Path | Kind | Requirement | Logic [I] |
|---|---|---|---|
| `isl_x_req` (fast rise) → `fio_exec`, select, `sid_top` register CE/D, stage | X2 class | one fast period (15.28 ns NTSC) | about 5 LUT levels: seq compare, A15:A10 and select, A4:A0 decode, CE OR, D mux; fan-out to about 25 bytes per chip |
| `isl_x_req.addr` → `fio_rdata` mux → `B0.data` D | X2 class | one fast period | about 4 levels: live 4:1, latch/live, chip select, B0 source mux |
| `B0.data` → island DI → T65 | existing X1 | one fast period | unchanged [V C64_TURBO_T65_TIMING.md §3, about 8 levels] |
| Stage, `ce_1m` | c64 → c64 | 31.7 ns | trivial |

- Hold at the coincident fast/c64 edge (h = 1 requests) is ordinary related-clock hold (baseline T6).
- The `isl_x_` prefix keeps these flops out of the CDC regex (baseline T3).
- The baseline T4 check "every c64↔fast path has a one-fast-period requirement and positive slack" covers the new paths.
- V-C, if adopted, adds the path T65 → c64 flop with a one-fast-period requirement. E1 must time it.

### 5.2 Resources [I]

| Block | LUT | FF |
|---|---|---|
| Effective addr/data/we mux, 2 chips | 28 | 0 |
| Stage: 6 registers × {stage, pend, flag} plus control D mux | 84 | 60 |
| `fio_rdata` and latch rule | 56 | 0 |
| Select (`sid_mode`, A5/A8, A15:A10) | 6 | 0 |
| Proxy B0 source and FIO exec (SID share) | 8 | 2 |
| **Total** | **about 180** | **about 62** |

No BRAM and no DSP. This is inside the baseline's "M6 adds 300-500 LUT" for all fast I/O.

### 5.3 Expected access cost (model, [I])

| Index (N) | RAM `lda abs` (φ2/read) | `lda $d419` V-R | V-C | C64U `$D419` [M] |
|---|---|---|---|---|
| 1-9 (2-16) | 4/N | same as RAM | same as RAM | same as RAM |
| 10 (20) | 0.200 | 0.250 | 0.200 | (same as RAM) |
| 11 (24) | 0.167 | 0.188 | 0.167 | (same as RAM) |
| 12 (32) | 0.125 | 0.156 | 0.156 | same as RAM |
| 13 (40) | 0.100 | 0.125 | 0.125 | about +1 slot |
| 14 (47) | 0.085 | 0.125 | 0.111 | about +1 slot |
| 15 (63) | 0.064 | **0.095** | 0.094 | **0.085** |

- At index 15 in unrolled code, the parity locks to 6 fast clocks per `lda $d419` in both variants. In mixed code V-R averages 3.5 clocks per FIO cycle and V-C 2.5.
- Doom irqtest mode-3 burst (25 × `lda abs,x / sta $d400,x / inx / cpx / bne`, 50 FIO cycles):

| Case | Burst time |
|---|---|
| V-R, index 15 | 8.6 µs |
| V-C, index 15 | 7.3 µs |
| V-R, index 14 | 10.3 µs |
| V-R, index 12 | 14.1 µs |
| If FIO cost one slot (C64U-like) | 6.4 µs |
| Engine build (D1) | about 56 µs |

---

## 6. Tests

All tests are small benches before any full build (AGENTS.md).

### 6.1 Unit: `tb_sid_fast_port` (new; one clock; `sid_top` `FAST_PORT` = 1, `DUAL` = 1)

- **a. Register file.** For each offset `$00-$1F`, each select pattern and a set of values: write through the fast port into DUT A, and the same write through the stock timing into a twin B (32-phase frame driver as in tb_c64_sid_socket.sv, `cs` in phases 16-31, `we` in phase 29). After every access, compare all register fields and both latches.
- **b. Latch table.** Random read/write sequences over `$00-$1F` (section 3.3). Returned bytes and latches equal twin B, including S3 after writes to `$19-$1C`.
- **c. Select.** Exhaustive over `sid_mode` 0-7 × A11:A0 (A11:A10 = 01): `fio_sel_l/r` equal fpga64:815-816 with `ioe_i` = `iof_i` = 0.
- **d. Edge stage**, each case checked on envelope state, ENV3, OSC3 and LFSR. The expected engine samples (gate bit, or the whole register, at successive `ce_1m` edges, the first being the edge before the first write) are the literal results of C64_TURBO_MODELS.md (sid_edge_stage.py):
  - kept, gate 0→1→0 inside a window: one ATTACK sample, then RELEASE (0 1 0 0);
  - kept, gate 1→0→1: one RELEASE sample, then ATTACK with `hold_zero` cleared (1 0 1 1);
  - kept, a test pulse `$41`→`$49`→`$41`: oscillator 0 for at least one sample (`$41 $49 $41 $41`);
  - kept, three writes `$41`, `$40`, `$21` in a window, from `$40`: first and last only (`$40 $41 $21 $21`);
  - kept, a pulse window, then a window with one write `$40`: 1 0 1 0 0; with `$40` alone on the `ce_1m` edge instead: 1 0 1 0 0;
  - kept, two pulse windows with an empty window between: 1 0 1 0 1 1;
  - merged, two consecutive windows, each 1→0→1: 1 0 1 1 1 (the second pulse is merged; four 1 MHz writes give 1 0 1 0 1);
  - merged, two consecutive windows, each test 0→1→0: `$41 $49 $41 $41 $41` (one oscillator reset instead of two);
  - merged, a pulse window, then `$40` on the `ce_1m` edge and `$41` later in that window: 1 0 1 1 1;
  - merged, a pulse window, a one-write window (`$40`), then a 0→1→0 window: 1 0 1 0 0 0 (the third window's pulse is merged);
  - a native write `$40` after a pulse window cancels the stage: `$41 $40 $40 $40` (the native value wins);
  - reset clears the stage: `$41 $00 $00 $00`;
  - a `memory_wait`-stretched window: the same samples as without the stretch;
  - `EDGE_STAGE` = 0, one window 1→0→1: 1 1 1 1 (the pulse is lost), which documents the U64-like behaviour.
- **e. Audio identity.** Replay a register trace (a synthetic 50 Hz player stream; a VICE-dumped stream optional), with at most one write per register per native cycle, so the stage stays idle. Run it once stock (e_29) and once fast, at random phases in the same native cycle; phase p means the fast write lands at e_p. Compare `audio_l/r` once per native cycle at phase 15.
  - These placements must give **bit-identical** results:
    - voice control bits 7:4 at phases 0-30 with single waveforms, otherwise at phases 15-30. Not phase 31: `dac_out` takes the single-waveform output from `norm_dac`, registered one edge before `ce_1m` (F11);
    - other voice registers at any phase if the trace uses single waveforms, otherwise at phases 15-31 (F11);
    - chip-0 `$15-$18` at phases 9-31 (F9);
    - chip-1 `$15-$17`, and `$18` bits 7:4, at phases 15-31 (F9);
    - chip-1 `$18` bits 3:0 at phases 15-30. Not phase 31: the sample published at e_0 carries the volume sampled at e_31 (F9).
  - Other placements:
    - Every other `$15-$17` placement: compare against a stock twin whose write the filter first samples at the same edge (F9, F10): the e_29 run if the fast write lands after that edge, otherwise the stock write one native cycle earlier. Expected: bit-identical. No cycle bound applies: the SVF integrators (sid_filter.sv:91-93, 162-192) carry a one-cycle difference in F0, routing or resonance for hundreds to thousands of native cycles (above one output LSB for up to about 5,200 native cycles at a 200 Hz cutoff and about 1,100 at 1 kHz), and at a low cutoff a residue of a few LSB can stay [I C64_TURBO_MODELS.md (svf_persistence.py)].
    - A `$18` write: the difference stays inside one compare sample per write, because `Mode_Vol` never reaches the integrators (sid_filter.sv:116-125, 155-160, 210).
    - Documented, expected differences: control bits 7:4 landing at e_31 and voice writes at phases 0-14 with combined waveforms (F11): at most one compare sample differs when the voice is not routed through the filter; on a filter-routed voice the difference decays as above, and the bench logs it and does not fail. Chip-1 `$18` bits 3:0 landing at e_31 (F9): exactly one compare sample differs, whatever the routing.

### 6.2 Regressions with the fast port idle

- `test_c64_mame_sid.py` (with the updated wrapper);
- `tb_c64_sid_socket`;
- `tb_sid_output_schedule`;
- `test_audio_path.py`;
- the M2 lockstep (baseline section 10). The SID inputs must match while `phi0_cpu` = 1, and `fio_cs` must stay 0 in exact mode.

### 6.3 Machine: `tb_c64_machine_island` (two clocks)

Scenarios run at index 15, 14, 12 and 2, with badline timing on and off, and with the screen on, badlines and sprites:
- exactly one `fio_exec` per FIO bus cycle, including `sta $d4ff,x` page-cross dummy reads into the other chip in mode 3 and RMW double writes;
- no native `cs_sid` in turbo except under DMA;
- FIO cycle length is 3 or 4 fast clocks (V-R);
- FIO during BA low only when badline timing is disabled;
- ordering of `sta $d418 / lda $dc0d`;
- REU fixed-address DMA to `$D418` during turbo;
- pause during FIO;
- exact↔turbo switch with a pending stage.

### 6.4 Architectural equivalence

The same program in exact mode and at index 15 must give identical final SID registers, latches and RAM. Programs avoid OSC3/ENV3 and timing loops.

### 6.5 Simulated timing benches (nightly)

| Bench | Expected |
|---|---|
| Unrolled `lda $d419` | 0.095 ±0.005 φ2 at index 15; equal to RAM through index 9 |
| irqtest mode-3 burst | 8.6 ±0.5 µs at index 15 |
| Doom sidtest | 64 MHz rows equal 1 MHz rows. About 1 M native cycles, so nightly |

### 6.6 Board gates (M5/M6 column; no support claimed until they pass)

| Gate | Expected value |
|---|---|
| Doom `sidtest` (index 15) | Variants 0-3 per-tick step 15.6 ±1.5; control ratios within 8% ([V] tools/sidtest.py:46-48). Results sit at `$0C00`, so this needs the on-screen variant or a result dump (baseline 6.4) |
| Doom `irqtest` mode 3 vs mode 0 (1970 IRQ/s) | Loop-rate loss about 1.7% (gate ≤ 3%); about 11% under D1 |
| Amiga64 M5 `$D419`/`$D41A` | about 0.095 φ2 (C64U 0.080-0.085). Timing-only deviation D14 |
| `$8F` MOD player, screen on (VanessaE) | No growl. Needs the CIA I/O-only path too |
| Music retrigger A/B | Player with same-call gate off/on at index 15, `EDGE_STAGE` 1 vs 0 build: attacks present with 1 |
| Regression | Stock SID behaviour in exact mode unchanged; dual-SID modes 0/2/3 at turbo |

### 6.7 E7-S (C64U, if one is available) [O]

- **a. Test-bit pulse.** Voice 3 saw at freq `$FFFF`. At index 15, set and clear the test bit back to back (`sta $d412` × 2, about 0.1 µs apart). Read OSC3 2 µs later. Repeat 1000 times.
  - About 6-10% "reset seen" means level sampling: the U64 loses pulses.
  - About 100% means the U64 preserves edges.
- **b. Same with gate and ENV3** (AD = `$00`).
- **c. UltiSID at `$DE00`.** Unrolled `lda $de1b` and `sta $de00` cost at index 15: 1 φ2 or turbo?
- **d. Read of a write-only register** after a write: `$00` (bare UltiSID, U6) or the written value?

---

## 7. Risks

| # | Risk | Effect | Retire or mitigate |
|---|---|---|---|
| RS1 | The edge stage deviates from the U64 if the U64 loses sub-µs pulses (U5 suggests it does) | Turbo music keeps retriggers that the U64 drops (**D15**, only if E7-S shows loss) | E7-S. Generic `EDGE_STAGE` = 0 gives strict level-sampling parity; the user picks the default (Q-S1) |
| RS2 | FIO costs 2-3 extra fast clocks against about 1 on the C64U | Amiga64 M5 reads +12-19% at index 15; extra cost from index 10 instead of 13 (**D14**, timing only) | V-C if E1 allows. No software is known to depend on it |
| RS3 | Edits to a Multitini-ported upstream file (`sid_top.sv`) | Drift from upstream | Generic-guarded blocks, `FAST_PORT` = 0 default, header note; engine files untouched |
| RS4 | Native and fast in the same edge through an unforeseen path | Lost or duplicated write | Assertion in every bench; native-first priority |
| RS5 | Text anchors in `test_c64_mame_sid.py` | Runner breaks | Fast-path assignments placed after the instance; wrapper update scheduled with R10 |
| RS6 | VIC target needs a stall, so the static schedule is unusable | +1 fast clock per FIO access | Done-toggle completion (3.2) |
| RS7 | Software timing SID reads at turbo (SID-type detection through OSC3/ENV3 delays) | Results depend on the turbo timing, as on the U64 | None; users run such code at 1 MHz, which the 2 s reset window already does |
| RS8 | `$8F` growl gate fails because the CIA NMI acknowledge still waits for BA | #665-style jitter | Companion I/O-only-ownership design; gate both together |
| RS9 | Pre-existing S3/S4 (latch after a `$19-$1C` write; no decay) vs VICE 3.10 | Rare readback differences | Unchanged by turbo; documented |

---

## 8. Open questions

1. **Q-S1.** Default of `EDGE_STAGE`: 1 (keep isolated gate/test pulses, at most one per register per 2 µs, and isolated RMW double writes as at 1 MHz; denser sequences are compressed, 3.4) or 0 (strict level sampling, which is probably the U64 behaviour)? Design default 1; revisit after E7-S.
2. **Q-S2.** Does V-C close at NTSC with margin (E1)? If not, V-R stays.
3. **Q-S3.** Does the VIC fast target execute without stalls, so that one static FIO schedule serves both? This is settled by the VIC sub-design.
4. **Q-S4.** U64 behaviour for an UltiSID at `$DE00`/`$DF00`, and for reads of write-only registers (E7-S c, d). It matters only if firmware later offers modes 1 and 4.
5. **Q-S5.** Are the C64U sidtest and irqtest numbers obtainable? They would turn the model expectations in 6.6 into references.
