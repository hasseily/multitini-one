# Vectrex overlay PSRAM burst engine

`multitini_vectrex_psram_burst.sv` transfers 64 logical bytes through the two
existing PSRAM chips. Chip A holds even bytes; chip B holds odd bytes. Each
operation starts at a logical 64-byte boundary and transfers 32 bytes per
chip. Physical addresses are logical addresses divided by two. The engine
initializes both chips, enables QPI and 32-byte wrapping, and runs at 96 MHz.
It leaves the shared GBC PSRAM driver unchanged.

The protocol reference is the manufacturer's
[LY68L6400 revision 1.2 datasheet](https://www.lyontek.com.tw/pdf/ddr/LY68L6400-1.2.pdf),
especially Figures 13/14 and Table 9. Reads issue `EB`, a 24-bit address, six
dummy clocks, and 64 data clocks. Writes issue `02`, an address, then 64 data
clocks. Initialization attempts quad reset first, then SPI reset, SPI QPI
entry, and quad wrap selection. This recovers chips left in different modes.

Data appears after a falling PSRAM clock edge. Eight input FFs in the I/O
blocks sample it using a second 96 MHz output of the same MMCM, at 279 degrees.
The selected nibble is sampled 13.281 ns after its logical clock-fall launch;
the main 96 MHz engine consumes the FF output 2.344 ns later. Both paths are
synchronous and timed. The receive path has no IDELAY or IDDR. The forwarded
clock uses FAST slew and DRIVE12; data and CE retain their board settings.

An earlier falling-edge IDDR version failed routed timing: clock-output and
input-buffer delays left the selected receive edge too early. A model with
ideal FPGA buffers did not expose this. The revised pin test includes routed
launch/capture skew and IBUF delays, independently from device tACLK and board
flight. The 279-degree phase is a static choice, not a calibration procedure.
It must pass the final routed setup and hold checks before an image is usable.

`hdl/board/multitini_vectrex_psram_timing.xdc` gives Vectrex its own input
timing contract. It references the forwarded ODDR clock's falling edge with
minimum 2 ns and maximum 8 ns input delay, covering the device's 2–6 ns plus
an assumed 0–2 ns external clock-forward/data-return path. FPGA input-buffer
and capture-register delays are then analyzed explicitly. This file
replaces the generic PSRAM XDC for Vectrex; that generic file's input false
path is unsuitable for this fixed capture phase. No second launch edge is
used, so ordinary min/max input delays cover both data transitions.

The input setup-only multicycle of two associates each captured nibble with
the preceding clock period. With a 10.416 ns period, setup must pair launch
fall at 5.208 ns with capture rise at 18.488 ns. Hold must still check the
following nibble launched at 15.624 ns against that same capture, a 2.864 ns
relationship. There is deliberately no matching hold relaxation. The exception
targets only the eight input FF D pins; the capture-clock to main-clock FF
paths remain ordinary synchronous paths.

After routing, inspect both edge pairs, both process corners, and the eight
FF placements in ILOGIC. Confirm that no clock-group or false-path rule has
disabled input or capture-to-main paths. The 2 ns external allowance needs
board validation. Passing pin simulation does not establish routed timing.
Output-delay checks cover data setup/hold and CE edge skew; the separate
20 ns last-clock-to-CE hold requirement is enforced by command sequencing
and the pin-level protocol assertions.

The forwarded-clock gate updates on the falling edge. Driving asynchronous
ODDR reset directly from the rising-edge command tape would truncate the last
clock pulse. Chip-select tail and rest logic also enforce the hold interval
after the last rising edge and prevent early acceptance at a CE transition.

## Validation

Run `scripts/test_vectrex_psram.py` with Python. It launches Icarus and VVP
through `run_native_tool.py`, including when invoked directly on Windows.
The independent pin model implements command parsing and byte-addressed
memory; it does not use the driver's internal command or tape signals.

The five positive cases cover 2, 4, and 6 ns device delays, additional clock/data
path delay, cold SPI, warm QPI, and mixed chip modes. Checks include input setup
and hold, complete clock pulses, CE high/low/hold intervals, command/address
and dummy phases, even/odd chip striping, the first and last word, full memory
address bits, page boundaries, changing request payload after acceptance,
misaligned requests, and reset during both a read and a write. The primitive
models include asynchronous ODDR reset, FPGA clock/data/CE output propagation,
IBUF propagation, and the independently phased capture clock. Two negative
controls deliberately move capture outside the early and late boundaries;
both must fail the data or input-timing checks.

The phase tests use the v4 routed capture-clock skew, corrected by the
measured DRIVE8-to-12 change: effective clock delay 1.000–3.505 ns and IBUF
delay 0.212–1.430 ns. The v4 reports show 0.245 ns clock uncertainty, including
the second MMCM output's phase error; the extreme cases shift capture by that
amount in each direction. The observed shortest setup interval is 0.101 ns
and shortest hold interval 0.102 ns, checked against 0.012/0.068 ns primitive
limits. These narrow simulation margins use the predicted drive correction;
the final routed clock placement and actual input timing must pass separately.

Measured cycles from the accepting rising edge are:

| Event | Cycles |
| --- | ---: |
| First read word | 17 |
| Read done | 80 |
| Write done | 73 |
| Read request-ready becomes available | 88 |
| Earliest following request acceptance | 89 |

Observed CE high time is at least 83.328 ns, CE low time at most 843.696 ns,
and CE hold after the final rising edge at least 25.456 ns. The test runner
records the exact source hashes, cases, and log hash in
`build/vectrex-psram-tests/manifest.json`; it marks the result incomplete
before starting.

This test validates digital protocol and the selected delay cases. It does
not model voltage, temperature, signal integrity, or substitute for FPGA
timing and board testing.
