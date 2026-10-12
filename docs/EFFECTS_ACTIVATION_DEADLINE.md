# Effects lost after core activation

## Report

The owner reports that Effects controls such as Scanlines and Blur change the
Genesis / Mega Drive picture but have no visible result on C64, Vectrex and
Game Boy Color. This is a physical observation with the current firmware.

## Established facts

All six PHO5 images capture identical sources for the board top, the output
effects stage, the native frame pipeline and the scanout path. Nothing in that
path depends on the active machine. The firmware effect, frame and CPU1
services have no core-specific branch. The difference must therefore come from
per-core runtime state.

`restore_selection_settings()` calls `configure_selection_effects()` and then
restores the core's saved media synchronously: C64 cartridge and drive 8, Game
Boy cartridge, Vectrex cartridge and overlay, Genesis cartridge. These loads do
not poll the effect service. The service measured its 3 s apply deadline from
configure. When the first poll came 3 s or more later, it latched FAULT before
submitting any work to CPU1. A host test reproduces this with a 5 s gap.

FAULT persists until a resolution change, Video size change or core switch. A
freshly loaded core image then has no committed effect tables, so the output
stage bypasses every pixel. The native frame pipeline never starts, so Blur,
Glow and persistence are also absent. The Effects page disables its controls
and shows `Video update failed`. Genesis avoids this when its profile restores
no cartridge or when a later Video size or resolution change reconfigures
effects.

## Change

The apply deadline now counts polled time without progress. Each poll adds at
most 100 ms. Submitting to CPU1, collecting its tables and each upload batch
reset the count. A CPU1 job or FPGA commit that stalls while the service is
polled still faults after about 3 s. Firmware only; no FPGA image changes.

## Test results

`tests/host/test_video_effect_service.c` adds four checks:

- a 5 s gap between configure and the first poll still applies effects;
- a 10 s gap during the table upload still completes the commit;
- poll intervals of 82, 100, 250 and 1000 ms apply when CPU1 and the FPGA
  respond at once;
- a stalled CPU1 job and a missing FPGA commit each fault after 2.9–3.1 s of
  regular polling.

The first check fails on the previous service. The cadence check fails without
the progress resets. All 61 host tests pass with the MSYS2 UCRT toolchain.

## Delivery

EFX1 firmware, build ID `0x45465831`, packages this change. It retains the
PHO5 FSBL, supervisor bitstream and machine cores byte for byte; only the
CPU0 program differs. Versus PHO5, CPU0 symbols change only in the effects
service, and the CPU1 worker is unchanged. `FIRMWARE.BIN` SHA-256 is
`09880b3c8ac4cd78f53e1cbfaef7079c7db16eba30f4acf660e3470592b0e83d`.

## Open questions

Board load times are not measured, so it is not established that the owner's
restores take 3 s or more. A second runtime state produces the same symptom:
Enable effects is saved separately for each core and defaults Off. The
Effects page status distinguishes them: `Effects off; ...` means the master
switch, `Video update failed ...` means this deadline fault. No board test of
this change has been made.
