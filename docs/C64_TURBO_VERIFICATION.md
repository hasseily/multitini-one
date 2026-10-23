# C64 turbo evidence: verification assets

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: C64 verification infrastructure for a new accelerator: findings for the lead

Summary:
- **All checks pass on the current tree.** I ran every requested script that takes under 5 minutes and also ran the whole VICE aggregate. Output went to the ignored `build/accel-probe-verif/`. `git status` is still clean.
- **The current turbo tests compare against stock mode, not against a reference emulator.** They require the accelerated run to produce the same accepted bus trace (address, read/write, data) as the stock run, with the cycle number ignored. Anything that keeps the NMOS bus sequence and drives the CPU with more clock enables can reuse that check.
- **VICE can also check results per instruction, so it works for a CPU with different bus timing.** I built a scratch harness on the repo's own unchanged VICE extract. After the first 15 instructions (where the RTL's A and Y are still undefined after reset), 199,985 instruction-boundary records from the production T65 match VICE exactly.
- **Klaus Dormann's 6502_functional_test passes on the production T65 in xsim.** It reaches the success trap at $3469 after 96,241,381 cycles in 462 s. The VICE core reaches the same trap after 96,241,364 cycles in 0.30 s.
- **One scheduler-free stock-mode difference from VICE.** The unstable ANE opcode ($8B) uses magic constant $EE in T65 but $EF in VICE. This is from reading the code only; no simulation shows it yet.

---

## Runs performed

All runs used fresh directories under `build/accel-probe-verif/`. Several ran in parallel on the 24-thread machine, so wall times are approximate.

| Script | Result | Wall time |
|---|---|---|
| `test_c64_acceleration.py` | PASS. Local RAM 65,553 checks; 258 accepted bus accesses match MAME under both clock schedules; 6510 bus hold_checks=1291, rdy_checks=67, write_rdy_checks=43; scheduler 376 checks | 20.6 s |
| `test_c64_machine_schedule.py` | PASS, 12 PAL/NTSC cases (stock, 2x, 3x, 4x, dense, guarded) | 26.0 s (about 18 s compile, 6.8 s simulate) |
| `test_c64_machine_interrupts.py` | PASS, 4 interrupt cases plus 4 late-NMI cases | 44.3 s (25 s simulate) |
| `test_c64_vice_cpu.py` | PASS 344/344; negative control rejected | 46.0 s (344 oracle processes about 31 s, xsim 7.3 s) |
| `test_c64_vice_comparisons.py` (all 5 suites) | PASS 5/5 | 202.9 s: cpu ~44 s, vic ~107 s, reu ~25 s, memory ~5 s, bus ~21 s |
| `test_c64_mame_port.py` | PASS, 44 events across 2,100,072 native ticks | 14.5 s |
| `test_c64_reu_cache.py` | PASS, 800 random ops | 11.4 s |
| `test_c64_reu_native.py` | PASS, 4 configurations | 49.5 s |
| `test_c64_reu_machine.py` | PASS, PAL/NTSC stock and dense | 57.2 s (38.8 s simulate) |
| `test_c64_reu.py` | PASS. 19-bit: 49 cases with 4 declared MAME disagreements; 24-bit: 2 cases; transport: 85 checks | 13.1 s |
| `tests.test_c64sim` | OK, 42 tests, 1 slow test skipped | 19.6 s |
| `tests.test_c64fast` | OK, 16 tests, 1 skipped | 18.5 s |

Not run: the MAME aggregate `test_c64_mame_comparisons.py`. The dev5 run of it took about 6.7 minutes.

Measured simulator throughput:
- **Whole machine in xsim:** about 100,000 fabric clocks per second. That is about 3,100 native C64 cycles per second, roughly 1/300 of real time.
- **CPU alone in xsim:** about 208,000 CPU cycles per second with clock enable on every clock (from the Dormann run).

---

## (1) What each script checks

**`scripts/test_c64_acceleration.py`** (xsim, iverilog, and a C++ MAME oracle)
- `--test local-ram`: iverilog runs `tb_c64_local_ram`, which covers the whole 64 KiB RAM address space.
- `--test cpu`: xsim runs the production T65 and `cpu_6510` (`tb_c64_6510_bus.sv`) on an original synthetic program (lines 19-50). The program covers SLO, NMOS read-modify-write, decimal mode, indexed page crossing, zero-page wrap, NOP abs,X, branch page crossings, 6510 port reads, and the NMOS JMP indirect wrap.
  - Case 0 enables the CPU every 4 clocks. Case 1 enables every 2 clocks with extra CE and RDY gaps (`tb_c64_6510_bus.sv:73-75`).
  - It requires the two accepted traces to be identical (line 141) and equal to the pinned MAME trace (line 158).
  - The `cpu_6510.vhd` wrapper gets an observation-only `test_data` port added in a build copy.
- `--test scheduler`: xsim runs `tb_c64_cpu_scheduler.sv` directly against `multitini_c64_cpu_scheduler.vhd`.

**`scripts/test_c64_machine_schedule.py`** (xsim, production `fpga64_sid_iec` plus CIA, SID and local RAM)
- `instrument()` (lines 35-79) adds debug ports to a copy of `fpga64_sid_iec.vhd` and checks that removing them restores the exact source.
- Synthetic program and synthetic ROMs, no Commodore assets.
- The bench runs 6 modes × PAL/NTSC. It checks:
  - accepted bus trace equal to the stock run;
  - native SID period of exactly 32 clocks, and CIA, VIC and pixel cadences;
  - one write strobe per I/O write;
  - ROM banking and 6510 port backing writes;
  - memory wait, pause and DMA perturbations.
- Measured clocks for the same 141 accepted bus cycles:

| Mode | Clocks | Speedup vs guarded stock |
|---|---|---|
| Guarded stock (mode 5) | 4,719 | 1.00x |
| 2x | 2,687 | 1.76x |
| 3x | 1,911 | 2.47x |
| 4x | 1,599 | 2.95x |
| Dense | 1,170 | 4.03x |

- Mode 0 PAL measured 6,366 clocks, but it is the first run after power-up and includes start-up delay. I used mode 5 (turbo requested but blocked) as the baseline.
- The program includes I/O and `$0000/$0001` accesses, which always use native slots.

**`scripts/test_c64_machine_interrupts.py`** (xsim, same machine)
- Reuses `instrument()` and adds observation of VIC X/Y and CPU SYNC.
- Four PAL/NTSC stock/dense cases inject an IRQ during a real badline BA-low read and an NMI during a sprite BA-low read. They check one handler entry and RTI each, resume of the main loop, RDY-low writes, and that an issued address, direction and data stay stable until retirement.
- Four PAL late-NMI cases inject the edge before or after bus issue and check the expected stack contents.

**`scripts/test_c64_vice_cpu.py`** (gcc VICE oracle plus xsim)
- 172 stimulus schedules, each repeated at the other CE cadence, for 344 cases:
  - IRQ/NMI around CLI, SEI, PLP, BRK, RTI, NOP and three branch kinds;
  - RDY holds, including that RDY does not stop a store;
  - one-tick IRQ pulses and double-NMI cases.
- Compares the whole trace: relative CPU tick, address, read/write, data and SYNC (`tb_c64_6510_boundaries.sv`). Includes a negative control that corrupts the BRK vector address.

**`scripts/test_c64_vice_bus.py`** (g++ VICE REU methods plus xsim)
- Extracts the production PHI0 process and DMA acknowledge logic from `fpga64_sid_iec.vhd` into a standalone arbiter, then compares 130 cases and 565 events of REU and VIC-BA timing against the VICE REU methods.
- Also checks a delayed-peek case, a VIC early-peek case, and a negative control that removes the write-grace logic.

**`scripts/test_c64_vice_comparisons.py`** runs five suites (cpu, vic, reu, memory, bus) and checks every child receipt's source pin and file hashes. Child counts from this run: vic 32 cases, memory 4,096 cases, bus 130 cases and 565 events.

**`scripts/test_c64_mame_port.py`** (C++ MAME oracle plus xsim) compares the 6510 port module against MAME's port methods. `tb_c64_mame_port.sv:42` checks that extra FPGA clocks never age the floating port bits without a native tick.

**`scripts/c64_vice_oracle.py`** checks the VICE pin and file hashes. **`scripts/c64_vice_cpu_oracle.py`** extracts the VICE CPU source fragments. Both are described under (3).

**REU scripts**
- `test_c64_reu.py`: MAME MOS8726 methods compiled with g++, plus iverilog for `tb_c64_reu` and `tb_c64_reu_transport`.
- `test_c64_reu_cache.py`: xsim, random cache and store correctness.
- `test_c64_reu_machine.py`: xsim, whole machine with a 16 MiB REU, stock and dense, delayed memory model.
- `test_c64_reu_native.py`: iverilog, native DMA cadence through the cache and DDR bridge, 19/24-bit address × PAL/NTSC.
- `test_c64_vice_reu.py`: runs inside the VICE aggregate.

**`scripts/c64sim` and the two Python test files**
- c64sim is a cycle-approximate model built on py65 and needs the private ROMs. It is not usable as a CPU oracle:
  - the installed py65 implements only 151 opcodes, so there are no undocumented opcodes;
  - it has no cycle-exact bus.
- `tests/test_c64sim.py` covers GCR, D64, chips, the parallel cable and stock-ROM acceptance.
- `tests/test_c64fast.py` covers the fast-disk ROM pair.

**Testbenches in `hdl/sim`, one line each** (runner in brackets)

| Bench | Covers | Runner |
|---|---|---|
| `tb_c64_1541_head_reset` | 1541 head reset and menu pause at all PH2 phases | `run_hdl_tests.sh`, iverilog |
| `tb_c64_6510_boundaries` | VICE/MAME interrupt and RDY boundary stimulus on the production CPU | `test_c64_vice_cpu`, `test_c64_mame_cpu_boundaries` |
| `tb_c64_6510_bus` | Synthetic program, CE4 vs CE2 with gaps, NMOS RMW, port | `test_c64_acceleration` cpu |
| `tb_c64_cart_memory` | Cartridge memory bridge | `run_hdl_tests.sh` |
| `tb_c64_content_loader` | PRG/CRT loader | `run_hdl_tests.sh` |
| `tb_c64_cpu_scheduler` | Phase-exact issue/retire for every mask and dense mode | `test_c64_acceleration` scheduler |
| `tb_c64_cpu_turbo_guard` | IEC and parallel-cable suspend, holdoff | `run_hdl_tests.sh`, iverilog |
| `tb_c64_d64_buffer_alignment` | D64 cache byte alignment | `run_hdl_tests.sh` |
| `tb_c64_d64_track` | Transactional D64 track service | `run_hdl_tests.sh` |
| `tb_c64_ddr` | REU DDR AXI3 master | `test_c64_ddr` |
| `tb_c64_expansion_config` | Expansion configuration crossing | `test_c64_expansion_infrastructure` |
| `tb_c64_g64_track` | G64 raw-track service | `run_hdl_tests.sh` |
| `tb_c64_input` | Input bridge | `run_hdl_tests.sh` |
| `tb_c64_loader` | ROM loader | `run_hdl_tests.sh` |
| `tb_c64_local_ram` | Full 64 KiB RAM | `test_c64_acceleration` |
| `tb_c64_machine_interrupts` | Described above | `test_c64_machine_interrupts` |
| `tb_c64_machine_schedule` | Described above | `test_c64_machine_schedule` |
| `tb_c64_mame_memory` | Decoder and memory handlers with supplied PLA outputs | `test_c64_mame_memory` |
| `tb_c64_mame_port` | 6510 port vectors | `test_c64_mame_port` |
| `tb_c64_memory_arbiter` | PSRAM arbiter | `test_c64_expansion_infrastructure` |
| `tb_c64_ntsc_video_bridge` | NTSC video capture and scaling | `run_hdl_tests.sh`, `test_video_settings` |
| `tb_c64_parallel_cable` | DolphinDOS-style cable, needs xsim | `run_xsim_tests.sh` |
| `tb_c64_reu` | REU registers and transfers | `test_c64_reu`, `test_c64_vice_reu`, iverilog |
| `tb_c64_reu_cache` | REU cache | `test_c64_reu_cache` |
| `tb_c64_reu_machine` | Whole machine with REU, stock and dense | `test_c64_reu_machine` |
| `tb_c64_reu_native` | Native DMA cadence | `test_c64_reu_native` |
| `tb_c64_reu_transport` | REU transport faults | `test_c64_reu`, `test_c64_vice_reu` |
| `tb_c64_run_injector` | PRG RUN key injection | `run_hdl_tests.sh` |
| `tb_c64_sid_socket` | SID socket routing | `test_c64_mame_sid` |
| `tb_c64_tod_pause.vhd` | TOD keeps phase across pause | `run_xsim_tests.sh` |
| `tb_c64_vic_bus` | MAME VIC BA/AEC frames | `test_c64_mame_vic` |
| `tb_c64_vice_vic_ba`, `_fetch`, `_irq` | VICE VIC comparisons | `test_c64_vice_vic` |
| `tb_c64_video_bridge` | Live video ring and DVI edges | `run_hdl_tests.sh`, `test_c64_view_bounds`, `test_video_settings` |
| `tb_c64_view_bounds` | Viewport arithmetic | `test_c64_view_bounds` |

---

## (2) Tests that encode the current turbo design

**Must be rewritten or retargeted if the scheduler is replaced:**
- **`tb_c64_cpu_scheduler.sv`** (whole file). Hard-codes the masks (1/2/4/7 give issue at phases 16/20/24 and retire 3 clocks later), dense issue at 16-26 in steps of 2 with retire 1 clock later, the native slot 28→31, `extra_allowed` gating, hold behaviour, mode change while in flight, and reset cancellation.
- **`tb_c64_machine_schedule.sv`**
  - Drives `turbo_mode`/`turbo_speed`/`local_turbo_allowed` as modes 0-5 (lines 222-224).
  - Counts retirements away from phase 31 as "extra" (line 141) and requires `extra_retires > 0` in turbo and `== 0` in stock/guarded (lines 297-298).
  - Its PASS string names stock, 2x, 3x, 4x and dense.
- **`test_c64_machine_schedule.py:instrument()`** hard-wires FPGA64 internal names and text anchors: `sysCycle`, `cpuAddr_pre`, `cpuDo_pre`, `cpuIO`, `lastVicDi`, `cpuWe_pre`, `cpu_cyc`, `enableCpu`, `dma_active`, `memory_wait_q`, `baLoc`, `cpuHasBus`, the enable signals, the chip selects, `pulseWr_io`, `"port(\n"`, `"\twe => cpuWe_pre,\n"`. A refactor of `fpga64_sid_iec.vhd` breaks the machine schedule, interrupt and REU-machine runners.
- **`tb_c64_machine_interrupts.sv`** sets dense with `turbo_mode=2'b10`, `turbo_speed=3` (lines 166, 213) and requires `extra_cycles > 0` in dense (line 195).
- **`tb_c64_reu_machine.sv:300`** uses the same dense setting.
- **`test_c64_vice_bus.py:31`** instantiates `multitini_c64_cpu_scheduler` with a positional port map (hold `'0'`, mask `"000"`, dense `'0'`, allowed `'0'`). It also regex-extracts `cpu_cyc <=`, `enableCpu <=`, `dma_peek_* <=`, `dma_ack <=`, `bus_access_valid <=` and the `-- PHI0/2-clock emulation` process. Any change to the scheduler's ports or to this FPGA64 text breaks it.
- **`tests/test_build_inputs.py:198-232`** checks literal strings:
  - `.turbo_mode({cpu_turbo_active, 1'b0})` must appear in the adapter, and `.turbo_mode({1'b0, cpu_turbo_active})` must not;
  - `turbo_en <= turbo_mode(0);` and the Smart condition must appear in `fpga64_sid_iec.vhd`;
  - `.turbo_request(cpu_turbo_enable)` must appear in the adapter, and "warp" must not.
- **Speed setting interface and firmware**
  - `tests/test_interface_spec.py:121` checks `C64_CPU_TURBO_ENABLE == 5`.
  - `tests/host/test_supervisor_settings.c` stores a `c64_cpu_turbo` boolean (profile byte 243).
  - `tests/host/test_core_frontend.c` uses `cpu_turbo_enabled`; `tests/host/test_sd_storage.c` also references the setting.
  - A multi-speed setting changes all of these and `firmware/platform/xilinx/main.c:240-245, 2313, 4594-4607`.
- **Docs:** `docs/C64_RAM_CPU.md:59-63` describes 2x/3x/4x/dense.

**Survive any scheduler, as long as T65 keeps a clock-enable interface:**
- `tb_c64_6510_bus` (CE cadence independence plus the MAME trace).
- `tb_c64_6510_boundaries`, through the VICE CPU suite. Stimulus advances on CPU clock attempts, independent of CE spacing.
- `tb_c64_mame_port`, which checks that the port does not decay without a native tick. A new design must keep `port_tick` tied to native time.
- `tb_c64_cpu_turbo_guard`, as long as the guard stays.

**Limitation to keep in mind.** The machine-level equality check works only because the synthetic programs do not depend on timing. The bench already masks the color-RAM upper nibble because the VIC floating bus changes with CPU speed (`tb_c64_machine_schedule.sv:146-148`). Code that polls `$D012`, CIA timers or the floating bus will legitimately differ when accelerated.

---

## (3) The VICE CPU oracle, and whether it can check a fast CPU

**How it is built (from the code):**
- `third_party/vice_reference/import_manifest.json` pins 15 files at commit `4d283a2e…` (tag 3.10.0), each with a SHA-256.
- `c64_vice_oracle.verify_reference()` checks the repository URL, commit, tag and every file hash.
- `c64_vice_cpu_oracle.prepare(out)` copies unchanged source into the build directory:
  - spans of `interrupt.h`;
  - five functions from `mainc64cpu.c` (`interrupt_delay`, `maincpu_steal_cycles`, `check_ba`, `interrupt_check_nmi_delay`, `interrupt_check_irq_delay`);
  - the fetch and `CLK_INC` sections of `c64/c64cpusc.c`;
  - exact copies of `6510dtvcore.c` and `6510core.h`;
  - an `oracle-source.json` listing line ranges and hashes.
- `tests/c64_vice/cpu_harness.c` supplies flat RAM, a per-cycle pin table (ready/irq/nmi), and a trace of `cycle,address,rw,data,sync`. Its loop runs one instruction (plus any interrupt dispatch) per pass.
- Built with gcc `-O1`; each case runs as its own process, about 90 ms each.
- Limits of the current harness:
  - `controls[4096]` and a hard 2,048-cycle cap (`cpu_harness.c:24, 35, 43, 132`);
  - it stops when $A5 is written to $0204 (line 53);
  - dummy accesses come through `load(a,1)` but the flag is discarded.

**Demonstrated: per-instruction architectural comparison works.** I wrote a variant harness in scratch using the same `prepare()` output and no changes to VICE code:
- no cycle cap and no interrupts;
- prints PC, A, X, Y, S and P at the top of each loop pass;
- stops at a `JMP *` trap.

Result on Dormann's test: success at $3469 after 30,646,176 instructions and 96,241,364 cycles, in 0.30 s.

On the RTL side, T65 already has a `Regs` output (`T65.vhd:302`: PC, S, P, Y, X, A) and a `Sync` output; the production wrapper leaves both unconnected. An observation-only copy of `cpu_6510.vhd`, made the same way the repo runners do it, exposes them.

Sampling rule: take the address bus on the SYNC cycle as PC, and take Regs on the next enabled cycle. That is the state after the previous instruction's write-back, which matches VICE's instruction-entry state.

Result over the first 200,000 Dormann instructions:
- 15 mismatches, all at records 0-14, and all caused by A or Y being 'xx' (undefined) in the RTL after reset.
- The remaining 199,985 records match exactly, with P compared under mask $CF.
- Clock enable every clock and every third clock produced byte-identical RTL traces.
- Took 6.4 s in xsim.

The repo already warns that RTL register snapshots taken just before retirement are not VICE instruction-entry registers (`docs/C64_VICE310_COMPARISON.md:182-185`). The rule above avoids that.

**Inference: how to use it for a fast CPU with different bus timing.**
- **Compare per instruction:**
  - (a) the architectural record above;
  - (b) the ordered list of writes (address, data), with no cycle numbers;
  - (c) the ordered list of all accesses to I/O, $D000-$DFFF, including dummy accesses, since those have side effects. VICE already marks dummy accesses through `LOAD_DUMMY`, so the harness can log them.
- **Keep the existing trace check where possible.** If the accelerator keeps the NMOS bus sequence (which the plan currently requires), compare the full accepted bus trace without cycle numbers, as `test_c64_acceleration` and the machine schedule test already do. That is stronger than an architectural check.
- **Interrupts cannot be compared at cycle level in accelerated mode.** They need an instruction-level stimulus (assert at the SYNC of instruction N and hold until acknowledged). The expected result is the NMOS polling rule in instruction terms, including the CLI/SEI/PLP and taken-branch delays. The stock-mode VICE cycle matrix stays as it is.
- **Work needed:** a new harness file (no cycle cap, optional sparse pin events, register and write logging), plus an RTL observation copy that exposes Regs and Sync.

---

## (4) Running standard 6502 suites against the RTL CPU

**Klaus Dormann 6502_functional_test**

Verified:
- Source: `Klaus2m5/6502_65C02_functional_tests` at commit `7954e2dbb49c469ea286070bf46cdd71aeb29e4b` (2020-01-05).
- License: GPL-3.0-or-later (header of `6502_functional_test.a65`).
- Prebuilt `bin_files/6502_functional_test.bin`: 65,536 bytes, SHA-256 `fa12bfc7…53fd`.
- Configuration in the source: start $0400, zero page at $0A (so it avoids the 6510 port at $00/$01), data at $0200, decimal mode tested, success trap at $3469 (from the listing).
- Production T65 and `cpu_6510` in xsim with flat memory and enable every clock: SUCCESS at $3469 after 96,241,381 cycles, 461.9 s. That is about 208,000 cycles per second. The extra 17 cycles over VICE are the reset sequence plus trap detection.

Gaps:
- It covers documented opcodes only. It has no undocumented-opcode or bus-cycle checks.
- There is no repo harness; I wrote a small scratch bench of about 60 lines.
- Licence compatibility with this GPL-3.0 repository looks fine. Whether to keep the binary in the repo or fetch it into `build/` like the IIgs vectors is a policy choice.

**SingleStepTests/65x02, 6502 directory**

Verified through the GitHub API:
- MIT license; commit `2f6980a2d95757486c7bee24355c360e40e2a224`.
- 256 JSON files, one per opcode, 10,000 tests each: 2.56 million vectors, 1,081,886,574 bytes in total.
- Each test gives initial and final registers and RAM, plus a cycle-by-cycle bus list. All undocumented opcodes are included.

Existing precedent:
- `scripts/test_apple_iigs_cpu.py` and `hdl/sim/apple_iigs_single_step.cpp` run the same project's 65816 vectors under Verilator.
- They write registers directly through `--public-flat-rw` and keep the 2.87 GB of JSON in `build/`.
- That run reached 5,119,956 exact passes out of 5,120,000, with 44 reviewed exceptions (`docs/APPLE_IIGS_CPU_VALIDATION.md`).

Inference for T65:
- T65 is VHDL, so Verilator and iverilog cannot simulate it, and GHDL is not installed. Available: Verilator 5.050, Icarus 13.0, xsim.
- In xsim, each vector needs registers loaded through a short preamble program, because writing into VHDL internals from SystemVerilog is not practical. That costs roughly 30-40 cycles per vector, about 100 million cycles in all, which is about 8-10 minutes of simulation plus converting the JSON to a compact form first.
- If the new accelerator CPU is written in SystemVerilog, the IIgs harness pattern applies directly and the full set should run in minutes.
- The bus-cycle lists check stock bus exactness per opcode with flat RAM. A sequence-preserving accelerator can use the same lists with cycle numbers ignored.

**Wolfgang Lorenz C64 test suite (2.15)**

Verified:
- The pinned VICE mirror commit contains only `.github`, `svn-instructions.txt` and `vice/`; there is no `testprogs/`.
- VICE keeps a rebuilt Lorenz-2.15 in its separate SourceForge SVN at `trunk/testprogs/general/Lorenz-2.15` (HEAD r46291 when I looked).
- T65.vhd's own history says all Lorenz CPU tests pass except timing on $B3/$BB (`T65.vhd:18-48`).
- The suite's license is unverified. Treat it as a private external input until confirmed.

Inference:
- It is a C64 program set. It needs KERNAL calls (print through $FFD2, chained LOAD of the next test) and CIA timers for the timing, IRQ and NMI tests.
- At the measured whole-machine xsim rate of about 3,100 native cycles per second, a full run is impractical, and a single test lasting a few C64 seconds takes 15-30 minutes.
- The practical options:
  - run it on the board, or on a Verilator model of the whole machine if one is ever made;
  - or run the CPU-only tests (adc/sbc/undocumented-opcode groups) in a flat CPU harness with KERNAL traps. That needs the private KERNAL or a shim, and leaves out the CIA and timing tests.
- `cputiming` and similar tests are expected to fail in accelerated mode.

**Undocumented-opcode coverage today** is only the synthetic programs (SLO, NOP abs,X).
- Verified in code: T65 uses magic $EE for both ANE and LXA (`T65.vhd:670-671`). VICE uses ANE $EF (and $EE in an RDY cycle) and LXA $EE (`6510dtvcore.c:825-826, 1280`).
- Inference: a VICE architectural sweep of ANE would show stock-mode differences in bit 0 when A bit 0 is clear and X and the immediate both have bit 0 set.
- The SingleStepTests constants for ANE/LXA/SHA/SHX/SHY/TAS also need checking against VICE before adopting them as the reference.

---

## (5) Gates that apply to any CPU change

From `docs/C64_RAM_CPU.md`, `MULTITINI_ONE_PLAN.md:65-75` and `docs/C64_VICE310_COMPARISON.md`:

- **Stock mode** must keep the native NMOS 6510 bus sequence and device timing, with VICE 3.10 as the reference (RAM_CPU 5-25; plan 65-69).
- **Accelerated mode is an explicit extension.** The plan says it must preserve "NMOS 6510 instruction and bus semantics" (plan 69-70). The RAM_CPU doc adds: "Do not fold dummy accesses or NMOS read/modify/write cycles" (RAM_CPU 23-24). VIC, CIA, SID and drive clocks must not speed up. A design that drops dummy cycles would break the current written rule and needs the user's explicit approval.
- **Extra cycles** need local-memory permission and CPU bus ownership. They are off with a cartridge, never override DMA, and keep `$0000/$0001` and I/O in native slots. VIC arbitration and RDY stay authoritative. Memory waits freeze pending retirements without repeating strobes (RAM_CPU 65-75).
- **Interrupt polling** stays checked against VICE. Keep the CPU matrix receipt with the exact T65 sources. An issued address, direction and write data must stay stable until retirement. "No new clock or clock-domain crossing is added" is stated for the current checkpoint (RAM_CPU 90-97). AGENTS.md requires a review of every new clock, reset, crossing and timing rule.
- **The 6510 port's floating bits decay on native ticks only**; turbo and RDY must not advance that time (RAM_CPU 102-105).
- **Local RAM** must stay at exactly 16 RAMB36 with no distributed RAM, checked by `scripts/check_c64_local_ram_synthesis.tcl` and again in the image build (RAM_CPU 46-50, 156-159).
- **Evidence rules:**
  - missing tools or changed reference hashes must fail, never pass;
  - generated files stay in `build/`;
  - keep separate evidence for RAM function, RAM inference, CPU bus traces, native peripheral cadence, memory mapping, whole-image timing and board behaviour, each with source hashes (RAM_CPU 136-163).
- **Commands to run:**
  - `test_c64_acceleration.py`, `test_c64_mame_port.py`, `test_c64_machine_schedule.py`, `test_c64_machine_interrupts.py`;
  - the MAME aggregate (secondary, with named disagreements kept);
  - the VICE aggregate `test_c64_vice_comparisons.py --build-dir <new>`. Every child must pass with the accepted pin and unchanged hashes, and the corruption controls must be rejected (VICE310 15-28).
- **Delivery sequence** (VICE310 231-254, RAM_CPU 271-283):
  1. Pass the VICE comparisons and retained regressions.
  2. Rebuild the FPGA image and confirm routed setup, hold and pulse width.
  3. Test on the board at stock speed with Expand disks 1 and 2.
  - Never relabel an old image, and never claim hardware acceptance from source tests.

---

## Open questions

1. Will the new accelerator keep the full NMOS bus sequence? If yes, the existing equality-with-stock and MAME/VICE trace checks extend naturally. If no, the plan wording must change and the architectural-plus-I/O-sequence oracle becomes the main check.
2. Will it use T65 (VHDL, xsim only, about 208k cycles/s) or a new SystemVerilog CPU (Verilator, IIgs precedent)? This decides whether Dormann (96M cycles) and SingleStepTests (2.56M vectors) are quick per-commit checks or slow, occasional ones.
3. What does "correct" mean for interrupt latency in accelerated mode? The VICE cycle matrix applies to stock only.
4. Should the ANE magic constant be brought in line with VICE in stock mode? It is a separate fidelity question from acceleration.
5. Is the Lorenz suite's licence acceptable, and would it be stored as a private input like the ROMs?

## Risks

- A faster CPU clock or a second clock domain breaks the single-`clk32` structure every machine bench assumes, and needs new two-clock benches and crossing review.
- A refactor of `fpga64_sid_iec.vhd` silently breaks the text-anchor instrumentation in four runners. They fail loudly ("anchor changed"), but each needs rework.
- Speed measurements need a timing-independent benchmark program. The current `MACHINE_SUMMARY` clocks come from a 141-cycle synthetic program dominated by I/O and port barriers, which is why dense measures 4.03x rather than about 7x.
- Whole-machine xsim runs at about 1/300 of real time, which rules out game-length or demo-length checks in simulation. Board tests remain the gate for those.

The Dormann bench, the VICE architectural harness and the comparison runs were throwaway session files and are not kept.