# C64 comparison with VICE 3.10

Date: 2026-10-23. Production baseline: `3f7b2a41d7ac39a7f1a732fff2638b732c846a72`,
M0.7.0dev4 / C64 `4D00C704`. The diagnosis below describes that baseline.
The dev5 corrections are recorded separately; baseline failures are retained
as evidence and must not be read as results for the corrected sources.

## Dev5 implementation and qualification

M0.7.0dev5 uses C64 image `4D00C705`. The local ports correct interrupt
polling and the demonstrated VIC/REU differences against VICE 3.10. Imported
reference files remain unchanged and are excluded from the product image.
The 16 MiB REU remains in DDR at `0x25000000-0x25FFFFFF`.

The permanent comparison entry point is:

```powershell
python scripts/test_c64_vice_comparisons.py --build-dir build/c64-vice-dev5-final
```

It runs CPU interrupt/RDY traces, dynamic VIC IRQ/fetch/BA tests, REC transfer
and register tests at both capacities, DMA writes to the CPU-port backing
addresses, and native REU arbitration. Each child must return success and
publish a passing receipt with the accepted source pin and unchanged input
hashes. Missing receipts, source changes and mismatches fail the aggregate.
Deliberate corruption controls test the comparison paths. The historical MAME
suite remains secondary; named disagreements must retain the original oracle
traces rather than alter MAME expectations to imitate VICE.

Separate integration tests cover machine scheduling, interrupts, REU/cache/DDR
transport and native cadence. Routed FPGA timing and package provenance are
recorded with the selected delivery. Component tests do not establish complete
machine equivalence. The board result for the three Expand observations is
recorded below.

## Dev5 board acceptance and delivery

On 2026-10-23, the user reported that Expand by Bonzai "now works perfectly"
after testing the dev5 delivery. This closes the reported initial disk-1 font
DMA failure, vertical shaking in the identified first disk-2 scroller, and
gray diagonals at the top of the ending for this delivery. This is user-reported
board acceptance of Expand; the retained diagnosis does not assign each visual
symptom to an individual RTL change.

All five VICE comparison suites passed before the build. The seven historical
MAME qualification suites also passed with their declared VICE disagreements
preserved. Machine scheduling, interrupt, REU/cache/DDR and host checks passed.
The related CheckChar diagnostic matches all sixteen VICE collision values and
keeps the DMA command phase stable across nineteen transfers.

C64 `4D00C705` passed routed signoff with setup `+0.133 ns`, hold `+0.052 ns`
and pulse width `+0.265 ns`. The full package contains freshly built dev5 ARM
CPU0/CPU1, this C64 image, and seven FPGA images and thirteen ROM payloads
retained exactly from dev4. Independent package byte audits passed.

| Artifact | SHA-256 |
| --- | --- |
| `Multitini-070dev5.zip` | `d090780b6a848cc6dc7f6c182292ebd7d604d9b5c0e3a051829ba24c4f6d6286` |
| `c64-pal.bin` (`4D00C705`) | `a889c107d0484aceb07dfb80302cfda31dc509557f1f8b5a59568ebde4670790` |

The sealed packages retain the exact build-time source snapshot and receipts.
This acceptance record was added after the board test; the packages were not
rewritten. Their earlier pending-board wording records their delivery-time state.

## Reference and scope

VICE 3.10 cycle-exact `x64sc` replaces MAME as the acceptance reference.
The [official release](https://github.com/VICE-Team/svn-mirror/releases/tag/3.10.0)
is pinned to commit `4d283a2e7dd59b7e378524878e81ecc7826b700c`, SVN r45923.
The Windows executable SHA-256 is
`9a5fb20348f41cce263a359e7daed2691a211afb9d39399ee45af8092e8da87e`.
The existing MAME suite remains secondary regression evidence.

Full-emulator runs use PAL old C64, stock CPU, a 512 KiB or 16 MiB REU,
and true 1541 emulation. The exact supplied Expand disks are mounted from
private read-only copies. Disk 1 SHA-256 is
`689983da1647f9f65b6003f826e12b7a7073cb6cf703b1eccb9a070cf2f3e195`;
disk 2 is `5e26127cafc55a35c14f81e0a7960e2fafb579f28252a9012c4229dfbcfdd664`.
Warp changes wall time, not the emulated clock.

Separate executable probes compare unchanged VICE functions with production
RTL. These are bounded component comparisons, not substitute full emulators.
Observation ports added to private RTL copies do not change their behavior.
The explicitly labeled IRQ counterfactual is the sole behavior-changing
experiment and uses a separate ignored copy.
Receipts record source hashes, commands, observations and limitations.
Private disks, ROMs, diagnostic binaries and emulator snapshots remain under
ignored `build/c64-vice310`; none are imported into tracked source.

## Dev4 symptoms and diagnostic attribution

| Board observation | Measured comparison | Attribution |
| --- | --- | --- |
| Initial disk-1 REU font test fails | Exact Expand font collision reads in VICE give the expected sixteen-position vector. A related author diagnostic exposes one-cycle start jitter and four wrong collision positions in production RTL. Changing only IRQ input sampling in a private simulation copy restores its exact VICE result. | IRQ sampling causes the diagnostic's stabilization fault. No exact Expand execution in RTL or board bus trace has yet established the complete board failure. |
| First scroller after "Are you ready for more fancy scrollers?" shakes up and down | The exact scene is captured in VICE. Its pink band deliberately changes vertical position. Native raster timing discrepancies are independently demonstrated. | Extra vertical shaking is not yet assigned to a particular instruction or display stage. Sparse reference screenshots cannot establish smooth motion or rule out frame-cadence effects. |
| Ending has gray diagonals across roughly the top sixteen lines | Captured VICE ending frames do not show that band. Bounded scene traces are retained for further comparison. | No particular VIC or display-path defect is yet demonstrated to cause this artifact. The isolated mode-transition discrepancy must not be presented as its cause. |

These were user-reported dev4 board faults, not captured hardware traces.
The dev5 board result above closes them; the following evidence preserves
the original diagnosis and its attribution limits.

The additional scene probes observe bounded CPU stores and instruction
execution in restored VICE snapshots. They are not scene replays in RTL.
CPU watchpoints do not exclude DMA writes to VIC registers, so absence of
a CPU-store pattern is not proof that a register never changes that way.

Retained disk-2 snapshots 40, 60 and 80 execute the same form of raster
stabilizer: `LDA $D012 / CMP $D012 / BEQ`. Their 13, 14 and 22 observed paths
start at cycle 55 or 56 and converge to the next line's cycle 3 in VICE.
This makes the demonstrated IRQ timing defect relevant to techniques used
by the real demo, without proving either visual fault's cause.

The visible-row BMM changes in snapshot 40 occur at cycle 58, outside the
active graphics-fetch interval of the isolated failing probe. Snapshots 60
and 80 keep BMM clear. No repeated `$D012` CPU store occurs in the same
absolute raster line in these bounded windows. The two demonstrated VIC
discrepancies therefore are not attributed to these scene failures.

Reconstructing capture 46 from snapshot 40 did not reproduce the saved
machine state: it ended 941 clocks late, at a different PC, with 81 differing
bytes in the CPU-visible memory dump. That trace is a nearby scene sample,
not an exact replay of the identified first scroller. The original scene
screenshots remain valid reference captures.

## Font timing: a stronger test than the collision sum

The author's later CheckChar utility checks sprite/background collisions
while REU streams character data to a fixed C64 address. It is related to,
but not assumed byte-identical to, Expand's initial font test.

Original CheckChar passes in VICE with both REU capacities. Running VICE
with the same RAM, BASIC/KERNAL/character ROM bytes, reset trampoline and
slow-loop patch used by the retained production-RTL fixture also passes.
Peripheral power-on state and initial frame phase are not asserted identical.

The sixteen scored collision bytes are:

```text
VICE: 02 00 02 00 00 02 02 00 00 00 02 02 00 02 02 00
RTL:  02 00 02 00 02 02 00 00 00 00 02 00 02 02 02 00
```

Both sum to 16. Positions 4, 6, 11 and 12 differ, counting from zero.
The earlier sum-only pass therefore did not establish correct raster timing.
Three warmup iterations are explicitly discarded by the diagnostic; the
sixteen scored values are known, not uninitialized collision data.

In VICE, all nineteen REU-command opcode fetches occur at raster line 54,
cycle 10, exactly 19,656 CPU cycles apart. In the production-RTL trace,
the command and first DMA acknowledgement alternate between phases separated
by 32 fabric clocks: one native CPU cycle. From command-write acceptance to
first acknowledgement, the delay remains exactly 32 fabric clocks. The timing
variation already exists before REU ownership, so variable DDR response time
cannot explain that particular variation.

The full baseline vector and nineteen-transfer timing come from the retained
dev4 run, whose recorded hashes identify the pre-correction sources. The
new observation-only CPU/VIC run covers eight transfers: three warmups and
five scored positions.

The stabilizing sequence reads `$D012` twice and takes a conditional branch.
VICE's two arrival classes take three or two cycles for that branch and
converge before starting DMA. In the fresh production-RTL trace, both reads
return `$34` in both classes; both branches take three cycles. The one-cycle
variation survives. Instruction tracing places the discrepancy before the
stabilizing reads, at entry to the second raster interrupt.

A controlled counterfactual changes only T65's IRQ polling expression in
an ignored simulation copy: it uses the previous accepted-cycle IRQ sample
instead of the live input. All six traced instruction phase sets then match
VICE, including both interrupt-entry classes and the stabilized DMA command.
All nineteen command/transfer phases become constant, with 628,992 fabric
clocks between frames, and all sixteen collision bytes match VICE exactly.
The branch now takes three cycles in twelve iterations and two in seven.
No DDR/cache or VIC behavior was changed for this experiment.

This establishes a causal IRQ-sampling fault in this diagnostic. It is a
single bounded counterfactual, not a complete proposed CPU fix: interrupt
priority, edge retention, CLI/SEI, branch polling and stalled cycles still
require comparison before applying a production correction.

The VICE opcode-fetch trace and RTL retirement/acknowledgement trace observe
different bus events. The phase-stability result does not require an assumed
absolute offset between them. RTL register snapshots immediately before
retirement also must not be equated with VICE instruction-entry registers.

The exact unmodified Expand disk-1 run independently reaches the font reads
at PC `$0B42`. Its three warmups and sixteen scored reads occur at line 54,
cycle 54, with the same expected sixteen-position vector shown above.
Both 512 KiB and 16 MiB runs reach the actual success return at `$08A8` with
accumulator `$10` and the zero flag set. Captured code sums `$0088-$0097`,
compares with `$10`, and branches to the font-failure message on inequality.
There is no separate result byte for this routine.

This ties the reference result to the supplied demo, while the production
RTL comparison still uses the related private diagnostic. In particular,
that diagnostic's baseline RTL sum is already 16 despite its wrong vector.
At the diagnosis checkpoint, removing its IRQ timing defect did not prove that
the exact board failure would disappear. The subsequent dev5 board test closes
Expand acceptance without making this diagnostic an exact-demo RTL replay.

## Confirmed differences in the dev4 baseline

| Area | VICE 3.10 | Dev4 RTL | Executable evidence |
| --- | --- | --- | --- |
| VIC same-line raster IRQ | Changing the comparator away from and back to the current line can reassert IRQ after acknowledgement. | `rasterIrqDone` prevents another IRQ until the line changes. | PAL and NTSC, actual register bus; initial assertion, acknowledgement and next-line controls match. |
| VIC bitmap-to-character transition | NMOS previous-mode/address behavior affects the first graphics fetch after clearing BMM. | The first address uses the new mode immediately. | With `$D011=$3B->$1B`, next tested fetch is `$1539` in VICE versus `$1529` in RTL, in PAL and NTSC. Other fourteen sampled fetches match. |
| REU write-side BA handling | A write stream tolerates the first asserted BA check, then waits on a second consecutive assertion. Reads wait immediately. | DMA ownership blocks all directions immediately. | Six fetch writes with BA low on cycles 1-3: VICE `0,1,4,5,6,7`; RTL `0,4,5,6,7,8`. Six read/swap/verify controls match. |
| 16 MiB REU increment and bank reads | Increment wraps within the selected 512 KiB window; upper bank bits remain selected. Bank reads OR `$F8`. | Increment is linear across 24 bits and exposes the upper bank bits. | Different payloads across `$07FFFF`, `$87FFFF`, `$FFFFFF`; independent readback comparisons. |
| REU partial address programming | Low/high address writes reload the other low-16 shadow byte but preserve the current bank. Bank writes preserve the current low-16 address. | Address writes also reload shadow bank; bank writes also reload shadow address. | Transfer from `$02FFFF`, then a partial register write. |
| REU verify near completion | A penultimate mismatch inspects the last byte and can also set end-of-block. | Stops at the mismatch. | Three-byte case: VICE three C64 reads/status `$70`; RTL two/status `$30`. |
| REU mask enabled after completion | Matching retained status immediately raises IRQ. | Mask changes without raising IRQ. | Completed end-of-block: VICE IRQ 1/status `$D0`; RTL IRQ 0/status `$50`. |
| Armed `$FF00` execution | Arming is a separate latch that survives clearing the command execute bit. | Trigger requires the current command execute bit. | Write command `$81`, then `$01`, then access `$FF00`: VICE transfers, RTL does not. |
| DMA writes to `$0000/$0001` | DMA payload reaches backing RAM independently of the CPU port. | Last VIC bus data replaces the payload. | 4,096 zero-page cases: 24 differences, all at these two addresses; 4,072 matches. |

The REU functional probe has 29 cases at each of 19 and 24 address bits,
with 25 of 58 exact trace matches. Many 24-bit differences are bank readback
alone; access order, readback and other registers/IRQ are compared separately.
Ordinary fixed-address transfers, stash/fetch, swap, autoload and normal
`$FF00` execution agree in the tested cases apart from that readback.
A 64-byte fixed-C64-address stream to `$0083` has matching accepted bytes
and order; this does not establish its raster phase.

The BA probe executes the complete production ownership clock process with
an already granted first cycle and an always-ready memory backend. It does
not involve DDR latency. VICE's extra terminal cycle after write BA delay,
and its first sprite-0 DMA-cycle exception, still need integrated coverage.
BA grace cannot explain a font transfer whose entire interval has BA high.
The zero-page mismatch does not affect the font target `$0083`.

## Correction order and acceptance gates

The diagnosis used the following work order. Dev5 implementation, qualification
and the subsequent Expand board acceptance are recorded above.

1. Correct CPU/VIC interrupt entry and raster stabilization against VICE.
   Require a stable command phase and exact sixteen-position collision
   agreement, with IRQ/NMI/BRK, branches, RDY and CLI/SEI regression coverage.
   Do not add an unconditional delay without checking these cases.
2. Correct REU ownership around BA, startup and completion, including sprite
   contention. Preserve the DDR backend and its native-rate cache contract.
3. Correct the demonstrated VIC comparator and mode-transition behavior;
   correlate traces from the exact scroller and ending before claiming those
   visual faults are explained.
4. Correct REU register/address/verify semantics and DMA writes to the CPU-port
   backing addresses. Any 512 KiB wrap correction must also update cache
   read-ahead addresses and high-bank tests.
5. Run the expanded VICE comparisons and retained regressions, then build the
   FPGA and verify routed setup, hold and pulse-width timing. Build/package
   ARM firmware separately only if its inputs change.
6. Test the resulting SD image on the board with the same two disks at stock
   speed. Require the initial font test, the identified scroller and the
   ending to pass. Measure DDR service under concurrent traffic if a remaining
   DMA discrepancy cannot be reproduced in the bounded simulation.

The diagnosis checkpoint introduced no new clock, reset, crossing or timing
constraint and generated no FPGA image or firmware package. Dev5 qualification
is separate from these retained diagnosis receipts.

## Local evidence index

All paths below are relative to ignored `build/c64-vice310`. The aggregate
`comparison.json` records hashes for the retained receipts.

| Evidence | Path |
| --- | --- |
| Source/binary acquisition and extracted inventory | `reference/reference.json`, `reference/extracted-files.json` |
| Exact disks, original diagnostic and scene captures | `runtime/runtime-receipt.json` |
| Exact initial Expand font reads and actual success return, both capacities | `expand-font-analysis-final/receipt.json`, `expand-font-analysis-final/analysis.txt` |
| Matched diagnostic inputs and collision vector | `fixture-comparison/receipt.json` |
| Nineteen-frame command timing | `fixture-comparison/phase-analysis.json` |
| VICE IRQ vector/handler trace | `fixture-comparison/irq-phase-trace/monitor.log` |
| Fresh production CPU/VIC trace | `vic-comparison/checkchar-phase/receipt.json` |
| IRQ-only counterfactual and phase comparison | `vic-comparison/checkchar-irq-delay-counterfactual/receipt.json`, `vic-comparison/checkchar-phase-comparison.json` |
| Independent fixture/phase review | `reu-comparison/phase-review.json` |
| VIC IRQ and graphics-fetch probes | `vic-comparison/irq-validation.json`, `vic-comparison/fetch/fetch-validation.json` |
| REU registers, payload and BA probes | `reu-comparison/receipt.json`, `reu-comparison/ba-receipt.json`, `reu-comparison/FINDINGS.md` |
| DMA zero-page comparison | `memory-comparison/receipt.json` |
| Bounded exact-disk scene CPU I/O and stabilization traces, reconstruction limits | `scene-analysis/receipt.json`, `scene-analysis/trace-receipt.json` |

Some probe receipts use status `FAIL` for a successful experiment that found
a reference/RTL mismatch. This means conformance failed, not that compilation
or simulation failed. Comparator corruption controls also verified that
altered traces are rejected. Failed acquisition/tool attempts remain in the
local evidence but are not counted as completed comparisons.
