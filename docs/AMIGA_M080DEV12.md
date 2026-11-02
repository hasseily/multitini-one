# Amiga M0.8.0dev12 diagnostics

This candidate pairs ARM 821 with FPGA 822 to measure the remaining IDE read
interval. It adds optional native counters and ARM polling brackets. Guest
CPU clocks, CPU choices, IDE transfers, read-ahead, machine RAM and video keep
their existing behaviour. Runtime ABI remains 0.27.

D000 capability bit 22 gates operations 12 and 13. They control capture and
read a frozen 128-word snapshot with 28 counters at a recorded service-clock
frequency of 28,375,196 Hz. Older cores retain ARM-only profiling. Native and
ARM measurements have distinct boundaries; neither the combined DRQ interval
nor its overlapping observations alone establish pure guest PIO time.
See [IDE profiling](AMIGA_IDE_PROFILING.md) for the measurement contract.

## Validation status

Nine focused C tests, strict main/service compilation and the official
133-test suite pass. Directed native diagnostics, all five real CPU/IDE
paths, ordinary CPU/RAM/DMA and the legacy transport/input/block/AXI checks
pass, including their negative controls. HDL and firmware peer reviews and
the fresh ARM build pass.

FPGA 822 passed final nominal setup at +0.203 ns, hold at +0.036 ns and
pulse width at +1.250 ns. Native clock-enable margin was +14.274 ns and
bus-skew margin was +6.077 ns. Critical CDC crossings were zero. The route,
DRC, coverage and PS7 checks passed with nominal constraints restored.

The user installed the pair. Live UART identified ARM 821 and FPGA 822;
PCAP reported the matching core CRC32 B19FF72E. A valid matched DH0 capture
reproduced 1,370,090 bytes/s in SysInfo. Of the 10.154054-second native closed
gap, 53.51% was native clock held and another 28.86% was accepted CPU RAM
read/write waits. The covered ARM polling upper bound was 0.344642 seconds
over 99.987% of intervals; two intervals were unbracketed. These are distinct
scopes, with inclusive memory detail documented in
[IDE profiling](AMIGA_IDE_PROFILING.md#board-measurement-2-november-2026).
This is measurement instrumentation, with no claimed performance gain.

## FPGA 820 timing failure

FPGA 820 failed final setup at -0.120 ns on `clk_fpga_0` (17 endpoints). It
was not packaged. The failing path ran from live PS7 `ARADDR` through the
early AXI read address, the 128-entry IDE staging `filled[]` select, and the
register read mux to `s_axi_rdata`, with 11 logic levels.

The IDE block added this path in FPGA 813. `clk_fpga_0` setup slack fell from
+0.350 ns (811) to +0.123 ns (813), +0.017 ns (817) and -0.120 ns (820). The
worst path in 813, 817 and 820 was the same path. The path is false in
operation. The early address is selected only while AR is accepted, and
`s_axi_rdata` loads only on the next edge. `filled[]` now uses the registered
read address, which holds the same value on that edge. The live address
drives only the staging BRAM address pins. FPGA 822 includes this change.

## Retained baseline and delivery

Preserve ARM 819 / FPGA 817 and its original source and audit archives:

| Retained component | SHA-256 |
| --- | --- |
| ARM 819 firmware, 5,624,556 bytes | `66d1231768523df46f74cc288b3c699aa12a8fb25ea8c78a264ccb46e185e8c0` |
| ARM 819 package | `c8c6cf71ff45cb524bfbfb67d690a4fa42b081b518d204ae937344b55db94d45` |
| FPGA 817 payload, CRC32 01D619BD | `4350860c4e451afd5d6a652960af5154e9248bdc140bd1c7dd5f3da072c4350f` |
| FPGA 817 package | `3e302f6fca1277486616a1f2f74dae630f0ab47895a6c14f91401e7368c55da4` |

The matching package retains supervisor 300, the qualified FSBL/BSP and
PS7 initialization
`7f046da603b96ed565e1519564e8687cf702d7a34d0219f145b45468ddcc04b4`.
The existing helper07 installer is unchanged. No HDF update is required.
The package remains ROM-free and contains no OS, HDF, user media or settings.
Supply Kickstart separately as described in
`sd/multitini/cores/amiga/roms/readme.txt`.

The FPGA 822 payload SHA-256 is
`4c1f30b22120ab98af006278b63ed54c791ef720b2e66c543422563bc7fe97cd`.
The matching packaged ARM 821 firmware SHA-256 is
`64f953e93a1919c6fdb53604161d5e7a755b81f43252db64a5d58f0c440ac122`.
The sealed package retains its original pre-board status and source snapshot;
the later board evidence is saved separately under
`build/amiga-ecs/board-tests/native821822/`.
