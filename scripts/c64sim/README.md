# c64sim

Cycle-approximate Python model of a PAL C64 wired to a 1541-II over the
IEC serial bus. It exists to test patched KERNAL and 1541 ROMs before
they go into the FPGA build. Both CPUs are py65 6502 cores; the chip
models follow the HDL in this repository (`c1541_logic.sv`,
`c1541_gcr.sv`, `iecdrv_via6522.vhd`, `mos6526.v`, `fpga64_sid_iec.vhd`).

Requires Python 3.12 and `py65` (`pip install py65`). ROMs are read from
`third_party/c64/ROMs` by default.

## Usage

```
python scripts/c64sim --d64 disk.d64 --type 'LOAD"$",8' --type LIST
python scripts/c64sim --kernal my_kernal.bin --drive-rom my_dos.bin \
    --d64 disk.d64 --type 'LOAD"GAME",8' --trace-bus 200 --save-d64 after.d64
```

From Python (with `scripts/` on `sys.path`):

```python
from c64sim import D64, System, basic_program

image = D64.blank("TESTDISK", "TD")
image.add_file("HELLO", basic_program([(10, b"\x8fHELLO")]))
system = System(d64=image, kernal_path="my_kernal.bin")
system.boot_to_ready(5.0)            # runs until the KERNAL waits for a key
system.type_line('LOAD"HELLO",8')
seconds = system.run_until_ready(60)  # simulated seconds the command took
print(system.screen_text())
print(system.stats())
after = system.disk_image()           # raw GCR tracks decoded back to a D64
```

`System(...)` options: `kernal_path`, `basic_path`, `chargen_path`,
`drive_rom_path` or the `*_bytes` equivalents, `d64` (path, `D64` or
`GCRDisk`, `None` for no disk), `pal`, `badlines`, `parallel_cable`,
`extram`, `device`, `write_protected`.

Helpers: `run_for(seconds)`, `run_until_pc(cpu, pc, max)`,
`run_until(callback, max)`, `peek`/`peek_ram`/`poke`/`ram_bytes`,
`press_stop()`/`release_keys()`, `enable_bus_trace()` + `bus_trace(n)`,
`enable_instruction_trace(cpu, depth)` + `format_instruction_trace`,
`add_breakpoint(cpu, pc, callback)`, `drive_state()`, `snapshot()`
(deep copy, used by the tests to boot once), `insert_disk()`,
`drive_extram()`. `c64.mem` is the CPU-visible memory, `drive.mem` the
drive memory. Replacement ROMs can be passed as `kernal_bytes`,
`basic_bytes`, `chargen_bytes` and `drive_rom_bytes`.

The bus trace lists ATN, CLK and DATA levels, PC2 pulses (every `$DD01`
access), the drive CA2 strobe as `FLAG2/PA2` and the parallel data
lines as `PAR`.

## Model

- C64: 64 KiB RAM, `$01` banking with write-through under ROM, VIC-II
  raster counter and raster interrupt, badline stalls (40 cycles per
  badline, on by default), CIA1 keyboard matrix and timers, CIA2 IEC
  lines, user port with PC2 pulse and the drive strobe on both FLAG2
  (edge) and PA2 (level, `$DD00` bit 2). Port reads follow the
  hardware: output bits read the CIA level wired-AND with external
  pull-downs, input bits read the external line. SID registers are
  stored only. 985248 Hz (1015 ns per cycle).
- Drive: 1 MHz 6502, LS42 decoding with RAM mirrors, 16 KiB ROM mirrored
  at `$8000` unless `extram` maps RAM at `$8000-$9FFF`. VIA1 carries the
  serial bus with the ATN acknowledge on DATA and the parallel port; VIA2
  drives stepper, motor, LED, bit rate, SYNC, byte-ready (also the 6502
  SO pin while SOE is high) and write mode.
- Disk: raw GCR byte streams per half-track built from the D64 exactly
  like `c1541_gcr.sv`; 26/28/30/32 us per byte by speed zone; writes
  replace bytes in the stream; `GCRDisk.to_d64()` decodes them back.
- Scheduling: the two CPUs run in lockstep by time; chip events (timers,
  disk bytes, raster) are scheduled by cycle number and evaluated at
  instruction boundaries.

Known limitations: no VIC graphics, sprites or SID; TOD clocks are
static; no shift registers; the VIA CA2/CB2 handshake output mirrors the
VHDL (low on the CA1/CB1 edge, high on the port access), which is the
opposite of the 6522 data sheet; the drive does not model half-track
data or unformatted noise (empty tracks read as zeros); NTSC timing
is present but untested against real behaviour.

## Tests

```
python -m unittest discover -s tests -p 'test_c64sim.py'
MULTITINI_SLOW_TESTS=1 python -m unittest tests.test_c64sim -k slow
```

Measured on the development machine: boot to READY in 2.17 simulated
seconds, a 2 KiB LOAD in 5.3 simulated seconds, a 202 block LOAD in
127 simulated seconds, at about 2 million C64 cycles per wall second.
