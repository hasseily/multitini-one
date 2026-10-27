# C64 CIA timer reload correction

The local CIA port corrects writes to a timer latch around an underflow or
force load. Both CIA instances and both timers use the same corrected module.
The imported MiSTer source remains unchanged and hash locked.

## Report and reproduction

On PAL M0.7.0 at stock speed, TSUIT 2.15 CIA1TA and CIA2TA were reported to
show the same first failing row:

```text
INIT   06 14 11 00
AFTER  0E 00 00
RIGHT  10 81 00
```

The reported settings were fast serial ROMs off, CPU turbo off, badline timing
enabled, disk auto slow off, SID audio on, RAM expansion off, Ultimate Audio
off, SuperCPU detect off, and Command Interface off.

INIT gives the initial low latch, replacement low latch, initial control,
and replacement control. AFTER and RIGHT give the timer low counter, interrupt
status, and control register. The expected interrupt status is `$81` for Timer A
and `$82` for the equivalent Timer B test.

The exact CIA1TA program extracted from the supplied Disk2 image has SHA-256
`7dd8c04afc6734f93659f043ed13f1895359e2c4ad33258a9cfc5923da3a5bdd`.
Its register accesses are: start/force-load at cycle 0, low-latch write at +8,
stop at +12, then timer, interrupt-status, and control reads at +16, +20, +24.
Unchanged M0.7.0 CIA RTL reproduces `$0E/$00/$00` at the native access/tick
phases without a CPU, cartridge, or optional expansion in the simulation.

## Cause and correction

The original low-latch write condition used the combinational *next* overflow.
When the counter was one, a CPU write replaced it before the timer tick could
complete the underflow. That also removed the interrupt event. The write
occurred in the wrong reload window.

The local port records the reload completed at the native timer tick. A low-byte
write can bypass into the counter only during that registered window. A
high-byte write loads the full counter when stopped or during the same window.
Timer A and Timer B use identical rules. The correction retains the existing
start/stop, counting, one-shot, interrupt, and Timer B cascade pipelines.

This follows the distinction in the pinned VICE 3.10 timer implementation:
[`ciat_set_latchlo` and `ciat_set_latchhi`](https://github.com/VICE-Team/svn-mirror/blob/4d283a2e7dd59b7e378524878e81ecc7826b700c/vice/src/core/ciatimer.h#L392-L413)
test the completed `CIAT_LOAD` state. VICE updates the timer and interrupt
state before applying a latch write. The local implementation preserves the
Multitini register-access and timer-tick phases rather than moving CPU accesses.

The port adds no clock, reset domain, asynchronous crossing, or timing exception.
Two reload flags use the existing CIA clock, reset, and native tick enable.
Declaration moves only make existing timer signals visible before their port-B
uses in strict SystemVerilog tools.

`python scripts/port_c64_cia.py --check` verifies the local port against its
locked source and deterministic transformation. The product source manifest,
machine regression runners, and lint script all select this local port.

## Validation boundary

After the M0.7.1dev1 package was delivered, the user reported on 2026-10-26
that the previously failing test suite now works on the board, resolving the
reported CIA failure.

The correction passes all 20,832 CIA1TA and 20,832 CIA1TB combinations, using
expected results executed from the supplied programs' own oracle code. The
unchanged implementation fails 577 combinations per timer, beginning with the
reported row. These are register-protocol simulations, not execution of the
complete disk test on the board.

An independent comparison with unchanged pinned VICE timer code checks 3,328
directed cases on both timers over 123,136 native cycles. It covers low and high
latch writes before, during, and after reload; zero/one and byte-boundary latches;
continuous and one-shot modes; force load; and stopping. The production port has no
counter/start-state mismatches; the original implementation has 6,595 mismatching
cycles. This comparison conditions the imported unreset pipeline state before
resetting the compared counters. It does not test the complete VICE CIA interrupt
model or CPU NMI dispatch.

CIA2TA and CIA2TB each pass all 20,832 combinations with the production
T65/6510 CPU, the supplied program's critical instruction sequence and RTI
handler, and a synthetic `SEI; JMP ($0318)` NMI entry. Each suite produces
17,448 acknowledged NMIs. All 778 cases per timer whose expected result differs
from CIA1 because of NMI timing pass. This is CPU-aware replay with flat memory;
it does not execute the full disk loader, screen routines, or entire C64.

The directed cascade case also checks that Timer B receives the preserved
Timer A underflow: its counter decrements from three to two. The unchanged
implementation loses that event and leaves Timer B at three.

Run the reproducible timer regression with the original user-supplied disks:

It requires Vivado/XSim and Python with `py65==1.2.0`; use a fresh build directory.

```text
python scripts/test_c64_cia_timers.py --build-dir build/cia-timers-check --disk2 <Disk2.d64> --disk3 <Disk3.d64>
```

The runner pins the expected program hashes and oracle engine, generates
vectors from the programs' own expected-result code, and rejects failed result
markers even when the simulator exits successfully. Separate machine benches
exercise CIA1 IRQ/CIA2 NMI delivery, REU accesses, and accelerated audio timing.
Final FPGA timing and physical-board retests are separate gates. These results
do not establish that all TSUIT or VICE CIA tests pass.

The supplied test media remain outside tracked source and the firmware package.
Evidence records their hashes and the exact tested implementation.
