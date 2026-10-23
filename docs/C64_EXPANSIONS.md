# C64 dual SID and 16 MB REU

The dev3 C64 image adds independently addressed SID chips and a 16 MiB REU.
MAME remains the behavioral reference. Its dual-SID adapter defines the SID
decode; its MOS8726 methods define the REU registers and transfer behavior.
The pinned reference is `122719e8d13e2fdb7dd33886455484207de59fc8`.
No system ROM or game is part of these tests.

## Settings and storage

The C64 menu offers Single, Dual $D420 and Dual $D500 for SID audio, and Off
or 16 MB for REU. The new image defaults to dual 6581 chips at $D400/$D500,
with chip 1 on the left and chip 2 on the right, and REU enabled. The first
SID retains the paddle inputs; the second chip's unconnected pots read $FF.
Both chips retain the native SID clock at every CPU speed. Single mode sends
the same register accesses to both existing engines. SID waveform/filter
generation remains the imported MiSTer implementation; the adapter comparison
does not establish MAME waveform equivalence.

REU uses a dedicated 16 MiB DDR aperture at `0x25000000-0x25FFFFFF` through
the Zynq HP2 port. PSRAM is not its backing store. The board's parallel PSRAM
arrangement provides only 8 MiB of usable capacity. The earlier dev3 image
incorrectly placed REU in PSRAM.

CRT images retain their PSRAM path. REU and CRT remain mutually exclusive
pending a simultaneous IO2 mapping policy. Turn REU off to attach
a CRT, or eject the CRT before enabling REU. Menu and direct content-service
loads enforce this rule; the HDL also disables REU while a CRT is loaded or
being installed. PRG and disk use remains available with REU.

Settings schema 10 appends the expansion byte without moving existing fields.
Older settings migrate to the new defaults. Firmware requires ABI 0.23 and
the installed C64 image's capability bits before exposing these controls;
retained older FPGA images keep their existing behavior.

## REU behavior and timing

The controller implements C64-to-REU, REU-to-C64, swap and verify commands,
immediate or $FF00-triggered execution, fixed addresses, autoload, status and
interrupt masks. IO2 register mirrors follow MAME. Registers and transfer
state reset with the C64; reset does not erase DDR. A small cache preserves
accepted writes across guest reset and disable, and drains them to DDR.
Only hard PL reset clears the cache. Firmware must wait for DDR quiescence
before PCAP replacement or storage handoff. Capability `REU_DDR` distinguishes
this contract from older ABI 0.23 images, independently of the REU enable bit.

MAME's MOS8726 model supports up to 512 KiB. The comparison builds the same
controller with a 19-bit address counter. The production 24-bit bank register
and counter are an explicit capacity extension; separate tests check high
banks and wrapping from $FFFFFF to $000000. This is not a claim that MAME
implements a 16 MiB cartridge.

The cache holds four 32-byte read lines, prefetches the current and following
line, and combines writes in four 8-byte buffers. Resident lines and returning
fill beats observe the newest accepted writes. Read-ahead takes priority over
queued writes, whose bytes overlay the returning fill. A physical write already
in flight completes before a read starts, because AXI read and write channels
are not mutually ordered. The transport reuses the IIgs approach to response validation,
accepted-transaction draining and quiescence, on the C64 machine clock.

REU asserts DMA ownership while waiting for data. The CPU remains held, but
VIC, CIA, SID and drive clocks continue at native cadence. A ready DMA
transaction is admitted before the CPU half of the native cycle and retires
only when the VIC grants the bus. Unready CPU/DMA slots issue no RAM or I/O
operation; VIC-owned RAM reads must continue in both halves of the cycle.
Each accepted transaction keeps its address, direction and data through
retirement. The DDR bridge keeps its response owner through backpressure and
soft reset. The required rate is one byte per available native cycle for
stash, fetch and verify, and two for swap, including fixed-address transfers.
Cache tests must separately check cold startup, line boundaries, BA holes,
completion and reset draining. An arbitrary unbounded DDR stall cannot meet
this timing contract; bounded simulation is not a physical-board latency proof.
The native-cadence qualification budget is at most 64 C64 fabric clocks per
physical DDR transaction, with at least four native cycles between register
writes (stock 6510 absolute stores). The production phase-15 readiness deadline
is unchanged. A separate approximately 90-clock stress profile can miss the
first slot after a cold bank change; it is outside this qualification budget,
even though ordinary streaming transfers pass. Physical DDR latency under
concurrent video traffic and accelerated register programming remain unqualified.

## Known compatibility failure: Expand

The user reports that Expand by Bonzai detects and tests REU memory but fails
its REU-to-font test on dev3 at stock CPU speed. This is a compatibility failure,
not evidence that more REU capacity or CPU Turbo is needed.

[The author explains](https://csdb.dk/release/?id=192886) that this test observes
REU-to-character streaming through sprite/background collisions. His published
[REUTools 1.2](https://csdb.dk/release/?id=198460) contains a related `CheckChar`
test: it streams sixteen bytes to one character-row address, then checks the
collisions at sixteen horizontal sprite positions. It is not a character-ROM
readback checksum. The later utility is not asserted to be byte-identical to
the test inside Expand.

Investigation found that dev3's DMA-readiness gate also suppressed VIC-owned
phi2 RAM reads while REU memory was pending. The source correction limits that
gate to CPU/DMA-owned slots. A production-machine regression reproduces the
failure before the correction and checks VIC read strobes and returned data.

The dev3 path also made each DMA byte wait for an
expansion-memory transaction. MAME charges one available native cycle per
stash, fetch or verify byte, and two for swap. Ordered transfer and memory
tests do not qualify this raster-visible timing. An RTL timing probe using
the production REU, arbiter, CDC, line service and PSRAM driver measured one
fetch/stash byte every two native cycles (2.030 us at the PAL build clock),
even with BA held high. That is half the required steady transfer rate;
the probe uses modeled PSRAM pins and is not a board measurement.

The author's later CheckChar program was also exercised with the corrected
wrapper at PAL stock speed. A fast memory model gave one-cycle byte spacing
and the expected collision sum of 16 (pass). A variable-latency stress model
gave a sum of 14 (fail). That stress model is not the production PSRAM timing
model; this experiment demonstrates sensitivity to stretched DMA, not an
exact replay of Expand or a physical-board result. Private ROMs and author
diagnostic files remain outside the tracked test suite.

Native DMA cadence, first and last transfer timing, and the collision tests
must pass before treating Expand as supported. The source correction alone
is not a new qualified
firmware image or a physical-board pass.

## Validation

Run the focused and existing checks from the repository root:

```powershell
python scripts/test_c64_mame_sid.py
python scripts/test_c64_reu.py
python scripts/test_c64_ddr.py
python scripts/test_c64_reu_cache.py
python scripts/test_c64_reu_native.py
python scripts/test_c64_reu_machine.py
python scripts/test_c64_machine_schedule.py
python scripts/test_c64_machine_interrupts.py
python scripts/test_c64_mame_comparisons.py
```

REU comparisons check ordered bus transfers and register/status/IRQ results,
including memory backpressure. The production-machine test uses the real CPU,
VIC, CIA, SID, scheduler and RAM with synthetic ROMs, delayed memory responses,
high-bank transfers, DMA writes to SID registers, delayed $FF00 character-ROM
reads, writes under character ROM, and VIC RAM fetches during REU waits.
Host tests cover settings
migration, capability gates and cartridge conflicts. These are separate from
routed FPGA timing, full-machine MAME equivalence and physical-board acceptance.
