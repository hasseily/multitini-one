# CodeSprite execution adapter

`lowlevel.cpp` executes each live CodeSprite byte array as 6809 instructions.
It imports only the CPU chip from `jhawthorn/vecx`, pinned to
`9b2894cacf73bdacaba32cb505289c658d981fd9`. The memory, VIA, analogue integration,
callback and execution-control code is original Multitini code. Build this one
C++ file; it includes the CPU privately, so do not compile `e6809.c` separately.
The imported files and local changes are recorded in
`../../third_party/vectrex32/e6809/PROVENANCE.json` and the adjacent
`lowlevel-imported-files.csv` fragment. No BIOS is included.

The API follows the inline-byte-array and Peek behavior in sections 7.11 and
12.10 of the [Vectrex32 1.29 manual](https://vectrex32.com/wp-content/uploads/2023/07/Vectrex32.pdf).
Execution stops when PC reaches the first byte after the supplied array; the
array needs no RTS. JSR, RTS, branches, RAM writes and calls into a caller-supplied
BIOS execute through the CPU. Registers, RAM, shared RAM and VIA state survive
successive sprites and frames. The caller places CodeSprite in drawing order,
sets its current pen before execution, and retrieves the resulting pen afterward.
Coordinates use signed native vector units, with one DAC delta per emulated
1.5 MHz CPU cycle. An ordinary timer-controlled vector has scale plus two cycles.
Intensity is 0..127. Blanking bits create separate lit segments rather than a
second pattern applied by the renderer. Pending lines are emitted before later
PSG writes to preserve callback order.

## Memory and execution bounds

| CPU address | Adapter behavior |
| --- | --- |
| `$0000..$7FFF` | 2 KiB shared RAM mirrored every `$800` bytes |
| `$C800..$CFFF` | 1 KiB machine RAM mirrored every `$400` bytes |
| `$D000..$D7FF` | 16 VIA registers, mirrored |
| `$E000..$FFFF` | Read through the external BIOS callback |
| Other addresses | Explicit memory error |

Each frame begins allocating code at shared-RAM offset zero. Arrays are copied
consecutively, with a total 2 KiB limit; the remaining shared RAM persists.
This is an explicit adapter layout. It does not reproduce the unavailable
Vectrex32 compiler's generated shared-RAM layout or its incidental register
values. Reset establishes DP=`$D0`, S=`$CBEA`, CC=`$50`, dot dwell 5, refresh
`$7530`, and one nonzero random-seed byte. Other machine RAM starts at zero.
Programs needing the BIOS's complete OS state can call its initialization code.

Synchronous Peek reads shared-RAM offsets 0..2047. Deferred CPU Peek reads
1..16 bytes without wrapping at `$FFFF`; VIA reads retain register side effects.
CPU writes to BIOS and missing BIOS reads report errors. Caller cancellation,
unsupported opcodes, invalid arguments and exhausted cycle budgets have distinct
status codes. The requested budget is capped at ten million cycles and checked
between instructions, with at most one instruction of overshoot. Service runs
at least every 256 virtual CPU cycles. Callbacks may service the host but may
not throw or re-enter the adapter. Use one host thread. An operation guard
covers execution and deferred BIOS Peek; callback attempts to create, mutate,
destroy or execute a context are rejected. A rejected destruction leaves the
object alive, and its owner must destroy it after the outer call returns.

## Compatibility limits

This is an instruction-cycle approximation, not a cycle-exact Vectrex. Memory
accesses advance one cycle each and remaining instruction cycles follow them.
The VIA implements ordinary port/DDRs, T1 one-shot/free-run PB7, T2 interval,
internally clocked shift modes, IRQ flags, and direct/pulse control outputs.
It has no external CA1/CB1 event source, T2 pulse-count source, FIRQ or NMI source.
Analogue controls use a fixed 13-cycle delay, sample/hold values and ideal linear
integration; physical drift, clipping and calibration variations are omitted.
Interleaved high-level sprites provide pen state but do not reproduce the
original compiler's complete sequence of VIA writes or register changes.

AY register writes, including repeated envelope restarts, go to the existing PSG
callback. Port A exposes the previous frame's controller buttons, with analogue
axes driving the comparator. A nonzero write through the separate external DAC
audio mux reports an explicit unsupported-memory error: this API has no PCM/DAC
callback. This backend does not claim general cartridge compatibility; the HDL
Vectrex core has separate native-game acceptance tests.

## Validation

Run `scripts/run_vectrex_lowlevel_tests.py`, or build the CMake target
`vectrex32_lowlevel_tests` and run CTest `vectrex32_lowlevel`. The script uses
the required guarded native launcher. The tests cover fallthrough,
registers/RAM across sprites and contexts,
synthetic external BIOS calls and stack return, VIA timer/shift waits, vector
sample/hold and signed slope, AY readback, repeated envelope writes, callback
ordering, bounds, invalid instructions, runaway loops and cancellation.
Callback tests reject nested execution, context creation, reset, destruction
and memory reads from service and deferred BIOS callbacks, including attempts
to execute another context. These tests and the external-BIOS checks pass
ASan plus UBSan with leak detection on Ubuntu/GCC 13.3.

An optional `--bios <local-8192-byte-file>` executes the real BIOS's Init_VIA,
Intensity_7F, Moveto_d and Draw_Line_d routines. It checks the exact signed
endpoint for two scales: `(12336,0)` at scale 255 and `(6192,0)` at scale 127.
The BIOS is supplied only as a local test asset and is not shipped. These are
host execution checks; ARM builds and physical hardware remain separate gates.
