# Amiga M0.8.0dev11 sequential read-ahead

This ARM-only update targets sequential HDF reads. ARM 819 uses the exact
qualified FPGA 817, supervisor 300 and existing helper07. Runtime ABI 0.27,
CPU execution enables, native IDE protocol, machine RAM and video stay unchanged.

## Scope and status

The earlier Amiga service requested one 512-byte HDF sector at a time.
The retained FatFs and SD backend already support multi-sector reads. A
32-sector, 16 KiB read-only buffer fetches consecutive file sectors within one
explicit ATA READ command while continuing to publish one sector at a time
through the existing IDE interface. It does not speculate into another command.

The implementation passes eight focused host tests, strict main/service
compilation and the official 132-test suite. ARM 819 was built, independently
audited and installed through Golden on 2026-11-01. Workbench reached the
guest display helper, with the retained FPGA 817, Fast IDE at 28 MHz and
Standard 640 by 256 with 16 colours. Core and sampled video faults are zero.
The HDF performed reads and writes. The subsequent profiled SysInfo DH0 test
reports **1,370,090 bytes/s**, versus **952,096 bytes/s** on ARM 818, a 43.9%
increase. The matching host-timer measurements follow below.

The firmware is 5,624,556 bytes, SHA-256
`66d1231768523df46f74cc288b3c699aa12a8fb25ea8c78a264ccb46e185e8c0`.
The 27,753,308-byte `build/amiga-ecs/firmware-test819.zip` has SHA-256
`c8c6cf71ff45cb524bfbfb67d690a4fa42b081b518d204ae937344b55db94d45`.
Golden verified matching file/flash CRC32 `3DFD7901` and booted firmware
M0.8.0dev11 / 4D000819. Only the firmware SD file was replaced; the installer
verified the retained FPGA, configuration, helper, ROMs and media metadata.

These installation notes are a post-build documentation update. The sealed
785-input source capture and its archives remain unchanged. A packaging-only
correction excluded unrelated historical logs from the qualification bundle;
the final independent audit verifies 99 exact package files and the same
firmware bytes. The original packager and correction receipt are retained.

Cache correctness requires file and mount identity, bounded reads within the
HDF, invalidation around writes and journal recovery, and safe reset, remount,
error and USB-storage handoff. Reads must retain exact short-read and error
handling. A short or failed refill invalidates the whole cache and fails the
command. It never publishes a valid-looking prefix, silently falls back to
single-sector reads or clears a foreign sticky error. This can abort that same
explicit command earlier than a sequence of individual reads would. Floppy
handling and durable write ordering keep their existing owners.

After a synchronous refill or cache hit, the service rechecks for native reset
request 6 before publishing the sector to the FIFO. A pending reset discards
the old cache and command without publishing their data or acknowledgement.

The existing profiler structure and command remain unchanged. SD call, time
and byte counters describe physical batch read attempts; successful sector,
ACK byte and drive activity counters describe logical sectors delivered to the
guest. Compare these separate meanings when measuring read-ahead.

Auto and Classic IDE policies retain their existing meanings. A policy change
restarts Amiga; save guest work first. The read-ahead cache does not add guest
ATA DMA, new ATA commands or a new FPGA transfer format.

## Retained artifacts and rollback

FPGA 817 remains a 4,045,568-byte payload with CRC32 `01D619BD` and SHA-256
`4350860c4e451afd5d6a652960af5154e9248bdc140bd1c7dd5f3da072c4350f`.
Its original timing, source archives and qualification remain unchanged.
PS7 initialization must remain
`7f046da603b96ed565e1519564e8687cf702d7a34d0219f145b45468ddcc04b4`.

The rollback firmware is ARM 818, 5,626,540 bytes, SHA-256
`8ad006e81d039fc05a1313e3eb0de91c7f5c7b3429c32a5a2468d893b8ec7f5b`.
Its retained package is `build/amiga-ecs/amiga-fastide-818-817.zip`, SHA-256
`ddfdda163c17f4940dd3b463aff1c2ee9457282af21b50135fee7998901ba3ba`.
Preserve firmware, core configuration, ROMs, disks, settings and journals when
preparing rollback. Existing Classic-policy settings need compatible firmware.

## Validation and delivery gates

Focused checks cover 1, 31, 32, 33, 128 and 256-sector commands, exact data and
FIFO ordering, 32-byte cache alignment, drive/command isolation, HDF bounds,
short/error refills, write-journal replay and lifecycle invalidation. Injected
native reset requests cover both a blocking refill and a cache hit.
The 128-sector case uses four 16 KiB backend reads and 128 logical sector
deliveries. The official full host rerun passed all 132 tests after the final
request guard, with source hashes unchanged during the run.

Before the ARM build, freeze the reviewed service, tests, version and scripts.
Require focused cache and lifecycle tests, real main/service compilation, and
source and license checks. Then verify the fresh CPU0/CPU1 compiler records,
embedded CPU1 bytes, unchanged BSP/platform and boot partitions, matching PS7,
and complete reproducibility sources. Record test results and artifact hashes
after those gates pass.

The standalone output is `build/amiga-ecs/firmware-test819`. Its only SD
payload is `FIRMWARE-MULTITINI.BIN`; retain the installed `core.cfg`,
`amiga-pal.bin`, helper installer and HDF. No ROM, OS, HDF, private media or
settings are distributed. Supply Kickstart separately as described in
`sd/multitini/cores/amiga/roms/readme.txt`.

Root owns any card update and the subsequent SysInfo comparison. Compare the
same CPU profile, IDE policy, HDF and workload, and keep the profiler's SD,
FIFO and combined DRQ-to-request intervals distinct. The combined interval
includes guest execution, CDC visibility and ARM polling; it is not pure PIO
time. Build, simulation, internal capture and physical output observations
remain separate evidence.

## Matched board measurement

Both captures use the same FPGA 817, 68020 28 MHz, Fast IDE, 2 MiB chip RAM,
8 MiB fast RAM, Standard 640 by 256 with 16 colours and installed Workbench
HDF. Each issues 120 READ commands of 128 sectors, totaling 15,360 successful
sectors / 7,864,320 bytes. Neither capture records writes or controller resets.

| Measurement | ARM 818 | ARM 819 |
| --- | ---: | ---: |
| User-reported SysInfo bytes/s | 952,096 | 1,370,090 |
| Backend read calls | 15,360 | 480 |
| Backend seek/read time | 5.521869 s | 0.549231 s |
| ARM-to-FPGA FIFO time | 1.118397 s | 1.252618 s |
| Combined DRQ-to-next-request interval | 10.186280 s | 10.335538 s |
| Active host read window | 16.894232 s | 12.224623 s |
| Bytes per real second over that window | 465,503 | 643,318 |

The cache reduces backend calls by exactly 32 times and backend read time by
90.1%. The active host window shrinks by 27.6%, giving 38.2% greater host
throughput. Profiler validity and exact tick accounting pass. Post-test HDF
activity advances by exactly 15,360 reads, with no writes; sampled core and
video error counters remain zero. Profiling is stopped and both UARTs closed.

The combined request interval now occupies 84.5% of the active window, FIFO
10.2% and SD reads 4.5%. The request interval still combines guest PIO/copy,
native memory/clock holds, CDC visibility and ARM polling. These contributions
are not individually measured. Whole-capture FDD and outside-service totals
overlap that interval and include human idle; they must not be added to it.
SysInfo and the independent ARM timer use different timing bases/windows;
their ratio does not prove a fixed clock-divider error.

The raw captures, calculations and independent comparison receipt are private
development evidence under `build/amiga-ecs/board-tests/readahead819/`.
These results are post-build documentation; the sealed runtime artifacts and
source archives remain unchanged.
