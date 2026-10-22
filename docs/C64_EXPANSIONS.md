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

REU occupies the complete 16 MiB PSRAM address space. CRT images also use
PSRAM, so they cannot be attached while REU is enabled. Turn REU off to attach
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
state reset with the C64; reset does not erase PSRAM. Reset or disable drains
an accepted memory operation before another command may use its completion.

MAME's MOS8726 model supports up to 512 KiB. The comparison builds the same
controller with a 19-bit address counter. The production 24-bit bank register
and counter are an explicit capacity extension; separate tests check high
banks and wrapping from $FFFFFF to $000000. This is not a claim that MAME
implements a 16 MiB cartridge.

REU asserts DMA ownership while waiting for PSRAM. The CPU remains held, but
VIC, CIA, SID and drive clocks continue at native cadence. A ready DMA
transaction is admitted before the CPU half of the native cycle and retires
only when the VIC grants the bus. Unready slots issue no RAM or I/O operation.
Each accepted transaction keeps its address, direction and data through
retirement. The memory arbiter keeps its response owner through backpressure
and soft reset. Storage latency can reduce DMA throughput; a one-byte-per-
native-cycle hardware rate is not claimed.

## Validation

Run the focused and existing checks from the repository root:

```powershell
python scripts/test_c64_mame_sid.py
python scripts/test_c64_reu.py
python scripts/test_c64_reu_machine.py
python scripts/test_c64_machine_schedule.py
python scripts/test_c64_machine_interrupts.py
python scripts/test_c64_mame_comparisons.py
```

REU comparisons check ordered bus transfers and register/status/IRQ results,
including memory backpressure. The production-machine test uses the real CPU,
VIC, CIA, SID, scheduler and RAM with synthetic ROMs, delayed memory responses,
high-bank transfers and DMA writes to SID registers. Host tests cover settings
migration, capability gates and cartridge conflicts. These are separate from
routed FPGA timing, full-machine MAME equivalence and physical-board acceptance.
