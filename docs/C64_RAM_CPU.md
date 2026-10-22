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

## Delivery boundary

The changed RTL requires a rebuilt C64 FPGA image. The public control ABI and
ARM menu remain unchanged for this checkpoint. A qualified delivery must use
the explicit new C64 image and its source/timing evidence; do not relabel a
retained image. No firmware installation or hardware acceptance follows from
source tests or isolated synthesis alone.
