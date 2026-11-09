# Amiga 68EC030 execution island

This is a historical record of the 68EC030 prototype. Its images failed
routed setup timing. The shipped A1200 68040 variant uses AP68040. See the
[Amiga plan](AMIGA_PLAN.md) for the supported CPU options.

## Goal

Run the AGA A1200 guest's CPU far faster than the existing 28 MHz TG68K
68020 profile without touching chipset, CIA, floppy, audio or memory timing.

## Design

The 28 MHz machine keeps its upstream TG68K kernel, CPU wrapper, chip bus
state machine, fast-chip IDE path, memory router and coherent DDR cache.
A second TG68K kernel (`CPU=11`, `CPU030=1`, `EC030=1`, no MMU translation)
runs in `hdl/core/amiga/multitini_amiga_cpu_island.sv` on an ungated
56.75 MHz clock, the exact 2x sibling of the service clock. The wrapper
selects the island when `cpucfg[1:0]` is `2'b10` and holds its own kernel
in reset; every other CPU profile is unchanged and keeps its own kernel.

Clock plan (`scripts/create_project.tcl`, checked by
`scripts/build_and_export_xsa.tcl`): one MMCM, M=56.75, D=10, VCO 851.25 MHz,
CLKOUT0 /30 = 28.375000 MHz (-5.6 ppm from PAL 28.375160; the previous plan
was +1.28 ppm), CLKOUT1 /15 = 56.75 MHz. The island `RATIO` parameter and
the multicycle counts in the XDC must match the divider pair. Both are integer outputs of one
VCO, so Vivado times the crossings as related clocks.

Crossing rule. The island derives its phase from a toggle flop on the
ungated service clock. Registers prefixed `ac_` capture service-domain
values only on the fast edge that coincides with a service edge; the shadow
bus registers `so_*` and `sb_*`, the only island outputs the wrapper reads,
change only on that edge. `hdl/board/multitini_amiga_timing.xdc` gives both
directions a two-fast-period setup window (one service period) and checks
hold at the launch edge, matching the IIgs phase-register pattern already in the repository.
No clock-group cut, no false path.

Shadow bus. A kernel bus cycle the cache cannot answer is published to the
wrapper on the next aligned edge as a TG68K-shaped request. The island
withdraws it on the aligned edge where the wrapper's clkena level is seen
and the memory adapter will accept that native edge (`native_enable`), so
the wrapper never sees a completed request for an extra cycle and the
existing retirement-gap logic in `multitini_amiga_machine.sv` still applies.
Writes are posted: the kernel continues after one fast cycle while the
wrapper performs the write; any later request that needs the shadow bus
waits for that acknowledgement, which keeps program order on the wrapper
side. Interrupt level, reset, the memory-router configuration and the
kernel's CACR/VBR/reset-out travel through the same aligned registers.

Cache. 16 KiB direct-mapped, one 32-bit line per entry with a valid bit per
word, block RAM tags and data. Cacheable: Zorro II/III fast RAM and
Kickstart reads, as decided by a second `memory_router` on the island
address with the wrapper's own configuration. Chip RAM (DMA shared), the
RTG and DD windows, custom registers, CIA, autoconfig and the fast IDE path
are never cached. Even-word reads of Zorro RAM are upgraded to the
wrapper's 32-bit transfer so one miss fills the whole line; Kickstart reads
fill one word. Cacheable writes update a resident line through byte lanes.
Tags are swept on every CPU reset, so the loader's ROM rewrite under reset
cannot leave stale words. The 68030 cache-control instructions are not
interpreted; the cache is coherent with all CPU writes by construction, and
no DMA engine writes the cached regions.

Latencies in fast cycles (17.62 ns): internal kernel step 1; cache hit 3;
posted write 2 plus a later shadow transaction; shadow read about one
service period for publication, one for the adapter lookup, then one fast
cycle, so roughly the same as the 28 MHz profile's two periods plus
retirement gap. The gain comes from the 2x internal rate, cache hits and
posted writes, not from the shadow path.

## Selection

The prototype firmware used `MT_AMIGA_CPU_68030_ISLAND` ("68EC030 57 MHz")
for AGA. The native CPU byte was `0x1e`. In that prototype, `userio.v`
maps `2'b10` to `cpucfg 5'h06`. Host status bit 24 names the variant's
optional profile: 68020 14 MHz on ECS, the island on AGA; older AGA images
report 0 and the firmware falls back to 68020 28 MHz with a message, as it
already does for 14 MHz. The console command is `:amigacpu 030`; the menu
cycles stock, 28 MHz, 68EC030 (when available), stock.

## Status

Known: all five existing xsim CPU profiles pass with the modified wrapper.
The island profile (`tb_amiga_cpu_aga_island`, CPU_MODE=2) found three
defects before it passed:

1. The testbench generated the service clock from the island clock's edge,
   so the slow domain sampled new island outputs in the same time step.
   Both clocks now advance from one process with nonblocking updates.
2. The prepared request `q_*` was reloaded on every kernel request even
   while an earlier posted write was still waiting for its aligned edge, so
   that write was replaced by the next request (first write lost, second
   written twice). IDLE now refuses any request while `sb_pending`.
3. Longword reads arrived word-swapped. TG68K forms `data_in_hi & data_in`,
   so `data_in_hi` must carry the lower-address word, while the wrapper's
   memory path returns that word on `cpu_din` and the next word on
   `ramdout_hi`. The island swaps long replies and long hits. The wrapper's
   own 32-bit path, used only by the never-enabled 68030 profile of the slow
   kernel, has the same inversion and would need the same correction if that
   profile were ever offered.

Timing: the first attempt, 4D000831 at 3x (85.125 MHz), failed routed setup
by -8.922 ns on 3115 endpoints. The 2x image, 4D000832, failed routed setup
by -1.704 ns (TNS -117.8 ns, 180 endpoints); hold passes, the clock-ratio
and native-gate checks pass, and every path covered by the aligned-edge
multicycle has more than +10 ns (see the constraint note below). All failing
endpoints are inside the TG68K kernel: RDindex_A (register-file read index)
through the LUTRAM register file, the address adder, the micro-state decode
and back into the register-file and memmask writes, 30 to 35 logic levels
and 18.8 to 19.1 ns of data path, about 0.4 ns of routing per level. The
kernel therefore needs a period near 19.3 ns, a clock of about 52 MHz, and
neither image was exported.

Placement sweeps from the 832 post-opt checkpoint (throwaway scripts and
reports in `build/amiga-aga/pblock832`) did not change that. The flow had
already placed the kernel in the two upper clock regions with the failing
loop inside about twenty slice columns. Results, routed, island clock:
place directive ExtraTimingOpt -1.994 ns; ExtraNetDelay_high -1.630 ns;
kernel alone in a 65 by 45 slice Pblock with ExtraTimingOpt -2.145 ns (84 %
LUT use inside the block); the whole island in a 70 by 45 slice Pblock with
its DSP and block RAM columns -2.955 ns (83 %). Compacting the island adds
congestion faster than it shortens the loop.

Constraint note from the 832 image: the wrapper's clkena level reaches the
`ac_*`, `so_*` and `sb_*` registers through their CE and synchronous R/S
pins as well as D. The capture regex matched D pins only, so 138 such paths
were timed at one island period (17.62 ns) instead of the service period
and passed by +0.5 ns. The regex now covers D, CE, R and S (555 pins on the
832 netlist; the phase sampler `tog_q` keeps the single-period check). This
changes no logic; no build has run with it yet.

Clock options. The island clock must be a sibling of the 28.375 MHz service
clock. Integer ratios give 28.375 or 56.75 MHz only. Rational ratios from
the same 851.25 MHz VCO are 3:2 (42.56 MHz, divider 20) and 5:3 (47.29 MHz,
divider 18); 9:5 (51.08 MHz) needs a 1276.9 MHz VCO (M=85.125, dividers 45
and 25) and leaves 3.9 ns edge windows, too small to use. With a rational
ratio an aligned edge comes only every 3 or 5 island cycles, so the present
scheme does not carry over. The shadow bus would instead need a small
service-clock proxy that holds the request toward the wrapper, withdraws it
on clkena and registers the reply, so that every crossing is a plain
register-to-register path inside the 7.05 ns (5:3) or 11.75 ns (3:2) worst
edge window, which Vivado times without exceptions. The wrapper's response
path alone is 16 ns and the island's reset-out fan-in 23 ns, so without the
proxy no rational ratio closes. The proxy adds about two service cycles to
each shadow transaction: uncached chip RAM and register accesses get slower
than on the 28 MHz profile while cached code runs 1.67x faster at 5:3.
Pipelining RDindex_A in TG68K remains the way to reach 2x; it changes the
kernel's cycle timing and needs its own instruction-level test.

Open: a closing island build, board
boot, SysInfo comparison against the 28 MHz profile, behaviour of software
that relies on 68030 cache control, and whether `CACR` cache-off should be
honoured.

The island experiment used TG68K, which implements 68000/010/020/030.
The later AP68040 port uses a separate 68040 core.
