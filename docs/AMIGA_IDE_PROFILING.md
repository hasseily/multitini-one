# Amiga IDE timing measurements

The host profiler separates SD reads and ARM-to-FPGA transfers from the
combined interval between DRQ publication and the next observed request.
On ARM 819 / FPGA 817 that combined interval was 10.335538 seconds, 84.5%
of a 12.224623-second active DH0 read window. It does not identify CPU stalls
by itself. See the [read-ahead measurements](AMIGA_M080DEV11.md).

The optional FPGA 822 / ARM 821 diagnostic adds native counters and ARM
polling bounds. The first matching board capture is recorded below.

FPGA 823 replaces the memory front end with a 32 KiB burst-line cache and
posted writes. Some memory counter definitions change with it; see
[memory cache](AMIGA_MEMORY_CACHE.md) before comparing captures.

## Board measurement, 2 November 2026

ARM 821 / FPGA 822 reproduced SysInfo's DH0 result of 1,370,090 bytes/s.
The setup was 68020 at 28 MHz, Fast IDE, 2 MiB chip RAM, 8 MiB Fast RAM,
640 by 256 with 16 colours, and the existing ClassicWB FULL v28 HDF.
Both profilers were valid with no reset, configuration change or overflow.
The workload was 120 READ commands of 128 sectors: 7,864,320 bytes, 480
read-ahead fills, no writes and no controller-reset requests.

The ARM active READ window was 12.227219 seconds. SD reads took 0.546036
seconds and FIFO transfers took 1.255682 seconds. The combined ARM-observed
DRQ-to-next-request intervals took 10.336310 seconds, or 84.54% of that
window. Whole-capture time includes user idle and is not a throughput basis.

The native observer closed 15,359 intervals and counted 3,931,904 PIO read
words, exactly 256 words per closed sector. The final open sector tail was
marked and dropped. Its exclusive partition was:

| Native state during closed intervals | Seconds | Share of native gap |
| --- | ---: | ---: |
| Native clock held | 5.433924 | 53.51% |
| Accepted edge, CPU RAM write waiting for readiness | 1.525324 | 15.02% |
| Accepted edge, CPU RAM read waiting for readiness | 1.404928 | 13.84% |
| Accepted edge, IDE guest I/O active | 0.279327 | 2.75% |
| Other accepted work | 1.510551 | 14.88% |
| Total | 10.154054 | 100.00% |

ARM polling's control-visible delay was bounded by 0 to 0.344642 seconds
over 15,357 intervals, 99.987% coverage. That upper bound is 3.33% of all
ARM-observed gap time. Two intervals had no qualifying zero observation;
their polling delay is unknown. This is a bound for the covered intervals,
not an exact total or a measurement of native-to-control CDC latency.

The inclusive memory observations support the next target:

| Memory controller state | Seconds |
| --- | ---: |
| CPU SEND | 0.307413 |
| CPU WAIT for backend completion | 3.229409 |
| Chip SEND | 0.271947 |
| Chip WAIT for backend completion | 3.647664 |

Those four controller states are mutually exclusive and occupy 7.456432
seconds, 73.43% of the native gap. They overlap the primary partition above.
CPU RAM-not-ready occupied 4.905633 seconds in wall periods, including
1.975380 seconds also classified as native clock held. This intersection
does not prove that CPU DDR traffic caused those particular clock holds.

Source inspection connects `memory_i.run_enable` to the native clock gate.
With no load or fault during this test, chipset deadlines and outstanding
chip memory work can hold the whole virtual machine. CPU-only misses use
the CPU readiness path, but an in-flight CPU request can delay a chip
deadline because the memory controller handles one request at a time.
FPGA 822's shared cache was 64 entries of 8 bytes (512 bytes). Writes went
through to DDR and completed only after the backend acknowledgement. Its
Amiga path used single-beat transfers, with one transaction outstanding.
The CPU issued 2,991,715 external reads and 5,731,203 external writes in the
closed intervals; writes were 65.70% of its external transaction count.

These results identified the FPGA memory path as the next optimization
target. FPGA 823 adds coherent posted/merged writes and larger burst-line
caching; see [memory cache](AMIGA_MEMORY_CACHE.md). Chip DMA ordering,
read-after-write visibility and reset/drain behaviour remain requirements.
The counters in this capture do not separate cache
conflicts, chip reads versus writes, individual AXI-channel handshakes or raw DDR response
latency. They identify the subsystem but do not predict a speedup from a
particular change. Further ARM polling work has a much smaller measured
opportunity over the covered intervals.

The independent ARM timer measured about 643,181 bytes/s over its active
window. SysInfo's virtual timer reported 1,370,090 bytes/s. These are
different timing bases; native holds can stop guest time. No fixed CPU
frequency correction is inferred from their ratio.

Raw evidence is retained under
`build/amiga-ecs/board-tests/native821822/sysinfo01-{context,start,result,analysis,post-status}.json`.
The FPGA payload CRC32 was B19FF72E, SHA-256
`4c1f30b22120ab98af006278b63ed54c791ef720b2e66c543422563bc7fe97cd`.
The matching packaged ARM firmware SHA-256 was
`64f953e93a1919c6fdb53604161d5e7a755b81f43252db64a5d58f0c440ac122`.
After capture the core remained ready, with no memory, video AXI, underrun
or engine errors. The profiler was stopped and the serial port closed.

## Native observer

Counters run on the ungated service clock. The reviewed MMCM ratio is
150 MHz times 60.25 divided by 13 and 24.5, approximately 28,375,196 Hz.
Counts describe service-clock periods, not CPU instructions or nominal
68020 execution opportunities. Native clock holds must remain observable.

For each closed READ DRQ-to-request interval, these five buckets partition
the counted periods in priority order:

1. Native clock held.
2. Native edge accepted while a CPU RAM write request waits for readiness.
3. Native edge accepted while a CPU RAM read request waits for readiness.
4. Native edge accepted with IDE guest I/O active.
5. Other accepted work.

Their sum must equal the closed-interval total. CPU request waits describe
observable bus readiness, including memory lookup, arbitration and backend
completion. They are not a count of lost instructions. IDE activity includes
the guest register interface. Other work includes execution and waits not
identified by these taps.

Additional counters count logical CPU RAM requests, CPU external-memory
reads/writes, CPU/chip memory SEND and WAIT occupancy, guest PIO words and
CPU request waits in wall periods. These are inclusive diagnostics that can
overlap the five buckets. Never add them to the exclusive partition.

Scratch counts are committed only when a subsequent native REQ closes an
interval. The last DRQ commonly has no subsequent READ request; its open
tail is marked and excluded. Whole-capture accepted/held counts are separate
and include human idle before and after the test. They cannot substitute
for the active interval partition.

DRQ and PIO events are qualified by accepted native edges. Registered REQ
changes are observed on every ungated service edge, including a hold just
after assertion. The observer accounts for its one-period registered-REQ
observation boundary. Endpoint and STOP-boundary tests must retain that
contract. Native reset, configuration change, overflow or invalid event
order makes the native capture invalid; invalid snapshots remain readable.

## ARM polling bound

The firmware timestamps used here already exist in the host profiler.
For each DRQ, it remembers the pre-read timestamp of the last actual
`ata_poll` observation of REQ zero. The first subsequent positive observation
uses its existing post-read timestamp. Their difference is an upper bound
on control-visible readiness to software detection, with lower bound zero.

The bound includes the last read bracket and work until the next poll. It
does not locate the native assertion or measure CDC latency. An interval
without a post-DRQ zero observation is explicitly unbracketed, not zero
polling delay. Sum and maximum bounds must be accompanied by the bracketed
and unbracketed interval counts. No extra timer read or diagnostic transaction
is added to each idle poll.

FDD host transactions also read D000 but do not interpret its IDE request.
The first arbitrary D000 read therefore cannot be treated as the ATA service's
detection timestamp. Native and ARM measurements have different publication
and observation endpoints. Do not subtract their absolute timestamps or
claim their difference is exact ARM polling time.

## Optional interface

Runtime ABI remains 0.27. D000 capability bit 22 advertises this diagnostic.
Existing ARM firmware ignores it. New ARM firmware uses ARM-only profiling
on older FPGA images and reports native measurements unavailable.

Opcode 12 uses payload `(sequence << 8) | action`, where sequence is a
nonzero 16-bit value and action is ARM=1 or STOP=2. Its response echoes the
sequence after the operation commits. Opcode 13 reads frozen 16-bit words
by index. Both use the existing coherent command/response mailbox; there is
no new top-level MMIO readback mux or FIFO hot-path operation.

The frozen bank contains 128 words: a 16-word header and 28 64-bit counters,
least-significant 16-bit word first. Header fields include DG signature,
schema, bank size, sequence, validity/reason bits, beginning/end configuration,
service frequency, epoch, layout, observation-boundary metadata and count.
The exact order is defined in `multitini_amiga_interface.h` and the observer.

ARM validates the complete header, nominal frequency, identifying fields
after readout and exclusive counter sums before publishing a snapshot.
A failed or malformed native snapshot never becomes plausible timing data.
Native validity remains separate from host profiler validity. Diagnostics
are armed before ARM timing starts; ARM timing stops before native freeze,
readout and UART output. Failed diagnostic sessions require normal restart
before retry, without changing storage settings or media.

## Board procedure and interpretation

Use the same FPGA, ARM, CPU speed, IDE policy, display, memory configuration,
HDF and SysInfo test for comparisons. Open SysInfo and select DH0 before
arming; keep the Multitini menu closed and avoid other disk activity.
Start `:amigaideprof start`, run the test, then stop immediately after completion.
Status also stops a capture. Freeze and read all results before printing.

Require valid host/native results, matching closed request counts, expected
READ histogram and bytes, unchanged configuration, no writes or resets and
zero faults. Check the exclusive sums before comparing time shares. Report
polling as a bound with its coverage. Explain inclusive DDR counters within
the exclusive categories rather than presenting overlapping totals as a sum.

The independent ARM timer and SysInfo use different timing bases/windows.
Native clock holds can stop virtual CIA time, but a rate ratio alone does
not measure that duty cycle or prove a fixed clock-divider error. A single
paired run is also not a repeatability guarantee. Save exact image identities,
source and artifact hashes and raw captures with any reported measurement.
