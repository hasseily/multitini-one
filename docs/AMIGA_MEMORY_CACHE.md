# Amiga memory cache and posted writes

FPGA 823 replaces the Amiga memory front end measured on FPGA 822. It pairs
with ARM 824 (M0.8.0dev13). Runtime ABI remains 0.27. The owner reported
SysInfo DH0 at 1,980,937 bytes/s; a matched profiler capture remains pending.

## Why

The FPGA 822 DH0 capture ([IDE profiling](AMIGA_IDE_PROFILING.md)) recorded
2.93 seconds of accepted CPU RAM waits within 10.154 seconds of native
closed intervals. Native clock holds occupied another 53.5% of those
intervals. CPU backend requests averaged about 11.5 service cycles in
SEND and WAIT. These are wall-clock observations, separate from SysInfo's
virtual-time rate.

The 512-byte cache had 64 lines of 8 bytes and was shared with display DMA.
In a 640 by 256 by 16-colour display, the bitplanes fetch 320 bytes per
scanline and compete with CPU code and stack in that small shared cache.
The capture did not measure individual cache evictions. Every write waited
for the DDR acknowledgement.

## Design

`hdl/core/amiga/multitini_amiga_memory.sv` keeps its native interface and
priorities: chip, then CPU, then loader. It adds `memory_burst`, which the
shared Amiga DDR path already supported.

- **Capacity and lines.** 32 KiB, direct mapped, 1,024 lines of 32 bytes.
  Line data is block RAM. Tags are distributed RAM.
- **Fills.** A read miss fetches its whole line with one 4-beat burst. The
  requested word is published on the beat that carries it.
- **Posted writes.** A chip or CPU write retires on its lookup edge when the
  4-entry write buffer has room. It no longer waits for DDR.
- **Merging.** A write merges into the youngest buffered entry for the same
  8-byte word. Four sequential 16-bit stores become one DDR write.
- **Write policy.** Write-update, no write-allocate, as before.
- **Ordering.** A read miss waits until no buffered or retiring write touches
  its line. No write is accepted while a fill is in flight. A read of the word
  that the retire slot is writing waits one service cycle.
- **CPU handshake.** A CPU fill's word is published only to the request that
  launched it. A withdrawn request, or a newer CPU hit or write, cancels that
  publication. A DMA write makes a still-asserted CPU read look up again, but
  never repeats a CPU write that has already retired.
- **Loader.** Loader writes wait for every posted write to drain, write DDR
  directly and invalidate their line.
- **Reset and faults.** Reset sweeps the tags in 1,024 service cycles.
  A sticky DDR fault drops posted writes, as in the IIgs cache.

The deadline contract is unchanged. A chip hit or accepted chip write is ready
before the next native edge. Any other chip outcome holds the virtual machine
from the edge after its first lookup. A hit's block-RAM word drives the read
data for one service period, then a register holds it.

These are the changes in timeline behaviour:

| Situation | FPGA 822 | FPGA 823 |
| --- | --- | --- |
| Chip or CPU write | Waits for DDR acknowledgement | Retires on lookup unless the buffer is full or a fill is in flight |
| Chip hit during a CPU miss | Holds the machine | Served without a hold |
| Chip hit during a posted-write drain | Not applicable | Served without a hold |
| Read miss | One 8-byte beat | One 32-byte burst, early word |

## Diagnostic counter meanings

The native observer and ARM 821 are unchanged. The memory taps keep their
labels, but some definitions follow the new design:

| Counter | FPGA 823 definition |
| --- | --- |
| `gap_cpu_read_tx` | CPU reads served: a hit, or the start of a CPU line fill |
| `gap_cpu_write_tx` | CPU writes accepted into the write buffer |
| `gap_cpu_DDR_send`, `gap_cpu_DDR_wait` | DDR time for CPU fills and for posted writes holding CPU bytes |
| `gap_chip_DDR_send`, `gap_chip_DDR_wait` | DDR time for chip fills and for posted writes holding only chip bytes |
| `gap_cpu_DDR_read_tx` | CPU 32-byte line fills |
| `gap_cpu_DDR_write_tx` | DDR writes holding CPU bytes, after merging |

The two request counters count served lookups. A CPU read that a DMA write
forces to look up again is counted again.

The four DDR classes remain mutually exclusive. Loader writes belong to none.
The exclusive gap buckets are unchanged: a CPU RAM write wait now means a full
buffer or a fill in flight. Do not compare these counters with FPGA 822 values
without these definitions.

## Validation

Directed simulations cover burst lines and early words on every beat, posted
and merged writes, chip hits during CPU fills and drains, chip holds during
CPU fills, reads ordered after buffered writes, write refusal during fills,
buffer backpressure, a write retiring as its entry launches, abandoned CPU
fills, held CPU writes across DMA writes, loader coherence, reset and faults.
Eight mutants each remove one ordering or handshake rule; the fixture rejects
all of them (`scripts/test_amiga_memory_mutations.py`).

An independent review of the first version found that an abandoned CPU fill
could overwrite a newer CPU hit, and that a DMA write could repeat a held CPU
write (also present in FPGA 822). Both are fixed and have directed checks and
mutants.

The real-CPU stock, 68020 and 14 MHz fixtures, display MMIO, RTC and Fast IDE
paths run against the production module with four-beat DDR models.

## Build and package

FPGA 823 passed final nominal setup at +0.214 ns (video clock), hold at
+0.013 ns and native clock enable at +14.409 ns. The AXI control clock had
+0.238 ns and the native Amiga clock +0.976 ns. The cache data uses 8 block
RAMs. The route, CDC, bus-skew, DRC, coverage and PS7 gates passed. The
payload is 4,045,568 bytes, CRC32 ECA36FF7, SHA-256
`bb5e11151d65d8d1f4b29e4dbe54bbb2d75badd5ef1aa57ed5a342f293ad0779`.

ARM 824 is ARM 821 with one menu change. While an Amiga ScreenMode request
is pending, the menu key does not reopen the overlay; the menu dispatcher
no longer changes menu visibility itself. Its official 133 host tests and
nine focused Amiga host tests pass. The firmware image SHA-256 is
`7b115a5aa8d5563fb24e3a5dec796daaaeef26d84cad2120a7a2d242ce5e0b39`.

The pair package is `build/amiga-ecs/amiga-cache-824-823.zip`, SHA-256
`9b37e2bc9ff6656261a1844efeab03d393eb3c5c86e551642d9e20b01d050ab9`. Its SD
payload is the same five files as earlier pairs. It contains no ROM, OS,
HDF, user media or settings. The sealed package keeps its packaging-time
copy of this document.

## Board result

On 2 November 2026 the owner reported SysInfo DH0 at 1,980,937 bytes/s on
ARM 824 / FPGA 823. ARM 821 / FPGA 822 measured 1,370,090 bytes/s, so the
reported rate is 44.6% higher. The setup is assumed to match the 822 capture;
no profiler capture was recorded with this result. SysInfo uses guest
virtual time and does not show remaining native clock holds.

## Status

Pending: a matched DH0 profiler capture using the procedure in
[IDE profiling](AMIGA_IDE_PROFILING.md), to locate the remaining time.
