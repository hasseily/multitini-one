# C64 local RAM and CPU scheduling

## Reference and scope

MAME is the behavioral reference for the C64. The reference is pinned to
`mamedev/mame` commit `122719e8d13e2fdb7dd33886455484207de59fc8`.
Exact test-only source files and SHA-256 hashes are listed in
`third_party/mame_reference/import_manifest.json`; the source pin, import lock
and per-file license ledger also record them. No system ROMs are imported.

Stock mode must retain the native NMOS 6510 bus sequence and C64 device
timing. Accelerated execution is an explicit extension to MAME's stock C64:
the CPU may complete extra local-memory transactions, but VIC, CIA, SID and
drive clocks must not accelerate. Do not fold dummy accesses or NMOS
read/modify/write cycles. A passing CPU trace is not a claim that the complete
machine, every peripheral or every software title conforms to MAME.

This checkpoint changes local RAM, CPU transaction scheduling, the existing
Smart 2x mode connection and CPU bus discrepancies exposed by MAME tests.
It does not add REU, expose new SID settings, change
the machine clock or advertise a new maximum MHz.

## Local memory

`multitini_c64_local_ram.sv` holds all 64 KiB in synchronous block RAM.
CPU, VIC and DMA use one scheduler-owned machine port. PRG installation has
priority while the machine is paused. The write address, data and enable are
multiplexed before the memory array, so Vivado sees one write port rather than
separate writes to independently indexed array elements.

Reads produce their result after one clock. Machine writes return the driven
bus byte, including writes that the cartridge mapper blocks from backing RAM.
Idle clocks and loader writes preserve the previous machine response. Reset
does not erase RAM. No simultaneous loader/machine read-write collision is
permitted by the port arbitration.

The isolated synthesis gate requires exactly 16 RAMB36 equivalents and no
distributed RAM. The C64 image build enforces the same requirement after
synthesis and implementation. This replaces the retained October 14 image's
12,288-LUT distributed main-RAM implementation; whole-image resource and
timing results must be measured independently.

## CPU scheduler

`multitini_c64_cpu_scheduler.vhd` separates issuing a memory transaction from
retiring the matching CPU cycle. Stock requests remain at phase 28 (CPUC),
with retirement at phase 31 (CPUF), in the existing 32-clock machine frame.
Native video, audio, CIA, IEC and drive clock generation is unchanged.

The existing 2x, 3x and 4x scheduling factors retain their four-clock slots.
An internal, unexposed setting uses six two-clock local transactions followed
by the native slot: at most seven CPU bus opportunities per native frame.
This is approximately 6.90 MHz PAL or 7.16 MHz NTSC before contention and
barriers, not a 16 MHz or 32 MHz CPU. Public settings still select Smart 2x.

Extra cycles require local-memory permission and CPU bus ownership. They are
disabled when a cartridge is installed and cannot override DMA. Accesses to
the 6510's `$0000/$0001` processor port remain in native slots, completing the
port-register update before another accelerated request. I/O accesses use
native slots. VIC bus arbitration and NMOS RDY behavior remain authoritative.

Memory waits hold pending retirements and suppress repeated memory or I/O
strobes. The existing cartridge path can still stretch the native machine
frame; this checkpoint does not turn it into a latency-independent DDR path.
Future REU integration must not reuse whole-machine waits as its normal DMA
backpressure policy.

The former adapter selected bit 0 of the upstream turbo mode, which requires
the C128-style `$D030` software enable, despite the menu saying Smart 2x.
The adapter now selects the automatic Smart bit. The existing IEC activity
guard still suspends acceleration around drive traffic.

## CPU bus conformance corrections

The local T65 port preserves NMOS taken-branch dummy-read addresses. The first
dummy read uses the next sequential PC; a page crossing then reads the target
low byte on the original page before fetching from the corrected address.
Architectural PC updates and other CPU modes retain their upstream behavior.
The unchanged imported T65 remains available for provenance.

NMOS interrupt polling uses the current IRQ level and pending NMI edge at
accepted CPU bus accesses. The existing status-register update order preserves
CLI/SEI/PLP interrupt-mask behavior. BRK snapshots pending NMI at the status
push to select the NMI vector while preserving the stacked B flag.

Selected NMI consumption follows MAME's vector-completion boundary, including
RDY holds and a second edge. A fresh edge can also discard the first handler
opcode under MAME's retained interrupt sample. The production wrapper supplies
an explicit bus-start strobe so that this decision is fixed when the scheduler
issues the transaction. Later changes cannot alter its address or opcode
effects between issue and retirement. Standalone CPU tests retain a combined
issue/retire clock by default. No new clock or clock-domain crossing is added.

The processor port is separate from backing RAM. MAME resets its direction,
data and retained-drive registers to `$FF`. CPU reads use external pins and
retained floating charge; external output pins use the configured pull-ups.
The C64 configuration uses pull-ups `$17`, floating bits `$C8`, and decay of
bits 6 and 7 after 350,000 native machine ticks following release. Turbo and
RDY holds do not advance that native time faster. Host pause and whole-machine
memory waits stop it with the rest of the machine.

CPU writes to `$0000/$0001` still update the internal direction/data registers,
but the RAM beneath those addresses receives the last VIC bus byte, as in
MAME's C64 memory-write handler. DMA uses that same backing-memory rule.

The local RGB palette compatibility port only adds coverage for VHDL
metavalues so the real VIC/color path can participate in strict simulation.
Every defined binary palette value remains unchanged.

## VIC sprite timing

The production manifest selects the local `ports/video_vicII_656x.vhd` port.
For the 6567R8 NTSC model, the second idle cycle moves from before sprite 0
to cycle 10 after sprite 7. Sprite 0 starts at cycle 59, and sprite 3 spans
cycles 65/1. The raster transition moves with that schedule; sprite 7 releases
BA before the added idle. PAL timing and the 65-cycle NTSC line length remain
unchanged.

`python scripts/port_c64_vic.py --check` verifies deterministic regeneration
from the locked upstream source. The source snapshot retains both versions,
the generator and their provenance. Bus ownership comparisons do not establish
pixel rendering or mid-cycle register-write compatibility.

## Validation and evidence

The pinned MAME opcode generator, execution methods and processor-port code
are compiled into a test harness by `scripts/c64_mame_oracle.py`. The harness
is explicitly bounded; it must not be presented as a full MAME executable.
Generated code, fixtures, traces and tool output stay under ignored `build/`.
Missing tools or changed reference hashes must fail rather than count as
successful conformance.

Run the CPU/RAM checks with:

```powershell
python scripts/test_c64_acceleration.py
python scripts/test_c64_mame_port.py
python scripts/test_c64_machine_schedule.py
```

The flat CPU oracle deliberately isolates CPU-visible bus behavior; its port
profile is distinct from the C64 port-event differential test. The separate
MAME memory-handler executable uses supplied PLA outputs and peripheral test
doubles. It checks handler mechanics, not the PLA truth table or complete
VIC/CIA/SID behavior. The integrated RTL test exercises the real machine
modules with original synthetic ROM data. None of these substitutes for a
complete-machine MAME comparison or physical-board acceptance.

Run the isolated resource check from a dedicated build directory with Vivado
2025.2 and `scripts/check_c64_local_ram_synthesis.tcl`. Use
`scripts/run_native_tool.py` on Windows. The ordinary C64 build also checks
RAM inference before allowing image export.

Required evidence is separate for RAM function, RAM inference, actual CPU bus
traces, native peripheral cadence, memory mapping, complete-image timing and
physical-board behavior. Retain exact source hashes with each test/build.

## Expanded MAME comparisons

Run the complete bounded differential suite with:

```powershell
python scripts/test_c64_mame_comparisons.py --build-dir build/c64-mame-comparisons-fixed
```

The runner checks the reference pin before starting, runs every selected
comparison even when another reports a mismatch, and retains the individual
logs and receipts under the selected build directory. Its `validation.json`
records source hashes, child receipt hashes and any source changes during the
run. A comparison failure, missing receipt, changed source or tool failure
returns a nonzero exit status. A completed run is not automatically a pass.

| Comparison | What is compared | Boundary |
| --- | --- | --- |
| CPU baseline | Accepted accesses from the existing synthetic program, including decimal arithmetic, NMOS RMW, undocumented instructions, branches and addressing wraps | Flat memory; two CPU service schedules |
| CPU boundaries | IRQ/NMI arrival around CLI, SEI, PLP, BRK, RTI and a page-crossing branch; short IRQ pulses; repeated NMI at vector/handler boundaries; RDY holds and writes | Address, direction, data, SYNC and relative CPU attempt tick; every stimulus runs under both enable schedules |
| 6510 port | Reset, direction/data writes, external pins and floating-charge expiry | Actual pinned MAME port methods; native-tick integration is checked separately |
| Memory handlers | Read data, addresses, peripheral selects and RAM/color backing writes from actual MAME C64 handlers and production RTL | Conditional on supplied PLA outputs; no independent decoder proof or observation of peripheral register writes |
| VIC ownership | PAL and NTSC BA/AEC traces across complete frames with display/badline and sprite configurations | Static registers after warm-up; no pixel, collision, lightpen or register-write-edge proof |

The new CPU, memory and VIC comparisons include deliberate trace corruptions
that must be rejected. Observation copies add outputs only. Expected traces
come from the unchanged pinned MAME source; production corrections are made
in local RTL ports and checked against those same expectations.
Synthetic programs, generated C++, vectors and traces
remain under `build/`; no private ROM or PLA dump is required.

Individual comparisons can be run with `--test cpu-baseline`,
`--test cpu-boundaries`, `--test port`, `--test memory` or `--test vic` on the
aggregate runner. Repeating `--test` selects several checks. Receipts identify
a partial selection separately from the complete suite.

The memory-handler check supplies PLA decisions explicitly because the pinned
C64 implementation obtains its truth table from an external PLA fuse dump.
Sweeping bank and cartridge inputs is useful handler coverage but is not an
independent MAME-versus-RTL decode proof. Full-machine comparisons and a
separately supplied, verified PLA reference remain additional work.

Memory vectors exercise active VIC address ownership. Their BA variation
checks MAME's PLA input packing only; inactive VIC address fallback and RTL
BA/AEC arbitration are outside this test. Peripheral write addresses, data
and register effects are also outside the memory bench's observation scope.

### Corrected comparison results

The first expanded run exposed 22 failing CPU boundary runs and eight failing
NTSC sprite scenarios. Its receipts and traces remain in
`build/c64-mame-comparisons/`. The local CPU and VIC corrections above address
those failures without changing the pinned MAME reference or trace alignment.
The CPU matrix also includes new discriminating cases for one-tick IRQ pulses
and repeated NMI arrivals through vector completion and first-handler prefetch.

| Check | Result |
| --- | --- |
| CPU baseline | 258 accepted accesses agree with MAME under both service schedules |
| CPU interrupt/RDY boundaries | All 188 runs agree under both schedules, including the original 126 cases and 62 additional IRQ-pulse/double-NMI cases |
| 6510 port | 44 events over 2,100,072 native ticks agree |
| Conditional memory handlers | 7,118 transactions agree: 5,134 CPU reads, 1,632 CPU writes and 352 VIC reads; five corruption controls rejected |
| VIC ownership | All 24 PAL/NTSC scenarios agree, with zero differences across 441,012 native cycles |

The full corrected run on October 22, 2026 passes all five suites. Its receipt
is `build/c64-mame-comparisons-fixed/validation.json`, with `complete_suite`
true and no changed sources. Aggregate and child source fingerprints were
also verified against the final working tree. The original failing run is
preserved separately.

For the original IRQ-before-SEI discrepancy, RTL now agrees with MAME's
stacked PC `$080E` and status `$20`. NMI on the BRK status push selects
`$FFFA/$FFFB`, with the original BRK B flag preserved. Repeated-NMI tests
check consumption through the vector and recognition after it, including
RDY-held reads and cancellation of the first handler opcode.

The NTSC sprite-0 case now lowers BA at cycle 56 and AEC at cycle 59 on
raster line 80, matching MAME. The correction also covers sprite 3 across
the raster boundary and sprite 7's release before refresh. PAL traces retain
their original alignment and match.

Run the separate production integration checks with:

```powershell
python scripts/test_c64_machine_schedule.py
python scripts/test_c64_machine_interrupts.py
```

These use the actual CPU, VIC, scheduler and local RAM with synthetic ROMs.
They check peripheral cadence, waits, DMA and pause; interrupt delivery during
real badline/sprite steals; and transaction stability around late NMI. They
are integration assertions, not a whole-machine MAME oracle.

The machine-scheduling check passes all 12 PAL/NTSC mode combinations. The
interrupt check passes eight cases: four PAL/NTSC stock/dense cases with real
VIC steals, plus four PAL stock/dense cases around the late-NMI issue boundary.
The latter verify that pre-issue cancellation stacks `$0B00`, whereas a
post-issue edge allows the already-started PHA to complete and stacks `$0B01`.
Both paths complete exactly two NMI vector pairs and RTIs, resume the main
program, and keep the issued address, direction and write data stable.
The specifically RDY-held first-handler late-NMI boundary is covered by the
standalone CPU comparison; the machine tests do not combine that exact edge
with a real VIC hold. Their receipts are
`build/c64-mame-fix-machine-schedule/validation.json` and
`build/c64-machine-interrupts-fixed/validation.json`.

Routed FPGA timing, pixel rendering and board acceptance remain separate gates.

## Delivery boundary

The later dev3 checkpoint adds dual SID controls and a 16 MB REU using the
native DMA slots established here. See [C64 expansions](C64_EXPANSIONS.md)
for its memory exclusion rule, MAME reference boundary and validation.

The changed RTL requires a rebuilt C64 FPGA image. The public control ABI and
ARM menu remain unchanged for this checkpoint. A qualified delivery must use
the explicit new C64 image and its source/timing evidence; do not relabel a
retained image. No firmware installation or hardware acceptance follows from
source tests or isolated synthesis alone.
