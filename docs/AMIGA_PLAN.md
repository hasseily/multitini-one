# Amiga ECS integration

## Target

The `amiga` core has two PAL variants. `pal` selects Amiga 600 ECS PAL and
`aga-pal` selects Amiga 1200 AGA PAL.

The Amiga profile targets an expanded A600 with ECS, 2 MiB chip RAM, a
1 MiB Kickstart ROM aperture, 8 MiB fast RAM, two DD floppy drives and two
IDE hard disks. Stock mode uses the upstream FX68K 68000 with native bus
phase enables. Accelerated mode uses the upstream TG68K 68020. Keep chipset,
CIA, floppy and audio timing independent of CPU acceleration.

The A1200 uses the same pinned Minimig source, PAL clock and memory adapter.
It starts with the 68EC020 stock bus throttle and offers the 28 MHz TG68K
execution profile as an option. Both modes use 2 MiB chip RAM and offer
4 or 8 MiB Fast RAM. Auto IDE uses Classic in stock mode and Fast IDE with
acceleration when available. CPU changes flush media and restart the guest.
The variants share the media directory and store separate CPU, drive and IDE
preferences. The AGA guest needs its own user-supplied A1200 Kickstart 3.1
ROM at `roms/kick-a1200.rom`.

The imported source is MiSTer's Minimig-AGA core at commit
`e47c3457b486d09daaf47c8934bda90cf7723045`. Its ECS configuration retains the
upstream custom-chip implementation. The source grant is GPL-3.0-or-later;
TG68K files retain their LGPL-3.0-or-later notices, and FX68K retains its
GPL-3.0-or-later grant in `rtl/fx68k/fx68k.txt`. Exact imported files and
hashes are recorded in `provenance/amiga.import.json` and `licenses/files.csv`.

## Platform

Machine selector 7 and core ID `0x414D4947` identify this port. The common
shell retains ARM ownership of USB input, menus, storage and core switching.
The board adapter replaces Intel primitives and MiSTer HPS services.

The optional [RTC transport](AMIGA_RTC.md) uses the existing host mailbox
to seed and refresh the native clock from the board's cached time. The
seconds fallback must keep advancing if that feed is unavailable. This is
installed as ARM 805 / FPGA 806 after offline qualification. The user
confirmed that SysInfo works on this installed pair.

The adapter and native core use sibling 28 MHz clocks from the existing
150 MHz PS clock. The reviewed MMCM ratio is M=60.25, D=13, O=24.5, giving
28.375196232 MHz, about +1.28 ppm from PAL 28.375160 MHz. Retain the achieved
PS and MMCM clock reports with each image. The native clock gate follows
the MMCM output, keeping raw core buses in the 28 MHz service domain. CPU
phase enables come from the original C1/C3 state; their sampled behavior
matches the original fast-clock generator in directed hold/reset tests.
Accelerated CPU modes need separate timing and compatibility evidence. An
advertised CPU speed must reflect the implemented clock and wait behavior.

HP2 services machine memory on the service clock. HP0/HP1 retain common
video duties; HP3 remains on the control clock. Memory capacity alone does
not establish that DDR service meets chipset deadlines. The port must prove
the memory scheduler, cache coherence, bounded waits, reset and drain
behavior before hardware qualification.

CPU-only memory waits use DTACK or the native CPU clock enable while chipset
time continues. A new chip request receives one service-clock lookup slot;
an unresolved request then holds the next native consumer edge. ROM preload
and memory faults also hold native time. Simulations verify ordering and
coherence under delayed responses. They do not establish real-time memory
throughput or the maximum sustainable CPU performance on this board.

The accepted standard crop captures 640 by 256 PAL pixels and line doubles
noninterlaced screens to a 640 by 512 canvas. Native Denise timing determines
the crop origin. This fits an integer 2x image (1280 by 1024) inside 1080p.
See [video geometry](AMIGA_VIDEO_GEOMETRY.md) for the correction to build 802
and the passing build 804 timing and desktop-alignment observations.

The display supports woven PAL interlace, maximum 724 by 566 HiRes
overscan with 16 colours, and 1280 by 512 SuperHiRes with four colours.
SuperHiRes samples native 28 MHz pixels and explicitly selects larger video
slots; CPU/chip clocks and machine RAM stay unchanged. FPGA 811 passed strict
routed timing and CDC gates. ARM 812 with normalized helper07 passes all four
on-card layout, origin and restore checks; external physical DVI output was
not separately measured. Both variants fix PAL and reject NTSC. The AGA
variant uses the shared RGB565 display path. See
[Workbench display options](AMIGA_DISPLAY_OPTIONS.md) for guest helper,
capture geometry, memory layout and compatibility contracts.

## User content

The package directory is `multitini/cores/amiga/`. Place a user-owned
Kickstart image at `roms/kick.rom`. The loader accepts exact 256 KiB, 512 KiB
or 1 MiB images and mirrors smaller images across the 1 MiB ROM aperture.
A600 Kickstart 3.1 revision 40.063 (512 KiB) has passed board boot tests;
other ROMs require compatibility testing. Amiga system ROMs are never included
in the distributable SD data. The canonical
`sd/multitini/cores/amiga/roms/readme.txt` records the user-supplied filename
and sizes. Proprietary ROM bytes stay outside Git and source archives.

Place DD ADF floppy images and HDF hard disk images under `media/`. The
firmware must preserve dirty data and recovery journals when a write fails,
and flush before eject, reset, core switching or USB SD export.

This target does not promise RTG, CD32/CDTV boot profiles, HRTmon, physical or flux
floppy interfaces, Ethernet, shared-folder or MiSTer Linux services. The
retained upstream README describes the upstream product and is not the
feature contract for this port.

## Build and package

Run the small Amiga import, loader, input, memory and media tests before
Vivado. Use a fresh build directory and an explicit build ID:

```powershell
python scripts/build_core_amiga.py --plan --build-id 4D000806
python scripts/build_core_amiga.py --build-id 4D000806 --jobs 1
```

The first command validates selections without running Vivado. The second
builds only Amiga, captures exact sources and settings, and exports the XSA
only after the existing routed timing, CDC, DRC and coverage gates pass.
The Amiga service-clock ratio is checked before export.

Select `--image amiga-aga` and a fresh build ID and output directory to build
the A1200 variant. The AM01 loader validates byte 11 against the synthesized
chipset (0 for ECS, 1 for AGA). The native configuration readback reports AGA
in bit 13. The A1200 variant requires AGA-aware ARM firmware.

`scripts/package_core_gs.py --include-amiga` adds an explicitly selected,
qualified Amiga image to the shared package with the names above and only
`readme.txt` in its ROM directory. Supply Kickstart separately on the SD card;
the packager does not accept or embed an Amiga system ROM. Existing image reuse
retains each artifact's original identity and source archives. These source and
SD-data labels do not rebuild or relabel retained FPGA or ARM binaries.
Use `--include-amiga-aga` to package both variants.

Use `--image amiga-aga-040` to build the AP68040 image and
`--include-amiga-aga-040` to package it as a third Amiga variant. Select
**Amiga 1200 AGA + 68040** under Amiga. It uses the shared A1200 Kickstart
and Amiga media directory. It saves its disk mounts and options separately
in `media/amiga-040-settings.bin`. Use a Workbench disk with 68040 support.
Install firmware that recognizes the `aga-040-pal` variant with the package.
The CPU uses direct 32-bit reads for Kickstart. On dual-clock AP68040 images,
**Options → CPU speed** toggles **28 MHz** (28.375 MHz) and **56.75 MHz**.
Changing speed flushes disks and restarts Amiga. The chipset and the CPU
external bus run at 28.375 MHz in both modes.

On 68040 images that report the new Fast RAM capability, **Options → Fast
RAM** also offers **16 MiB**. The 16 MiB setting presents one contiguous
Zorro III memory bank to Kickstart. Its backing store is
`0x26000000–0x26FFFFFF` in board DDR. The 4 and 8 MiB settings use Zorro II
and the original DDR aperture. Changing the size flushes disks and restarts
Amiga. With newer firmware and an older FPGA image, a saved 16 MiB selection
runs at 8 MiB until an image with the capability is loaded again.

The 68040 bridge supports four-word read bursts for cache-line fills from
Fast RAM. It returns the requested longword first and wraps within the
16-byte line. Chipset accesses and writes use their existing transfers.
With 16 MiB selected, the bridge can return the four words on consecutive
bus edges from a registered reply. After two sequential DDR lines, the
memory controller can fetch one line ahead. Speculative reads use a separate
buffer so chip writes can still use the shared cache. A chip miss that arrives
during a speculative burst uses the normal native clock hold until DDR is
available. Physical timing and game speed require board tests.

The controller bindings provide Red, Blue, Green and Yellow on each port
through the CD32 pad protocol when the image reports that capability. Red
and Blue also serve as the two direct joystick fire buttons.

## Qualification

The host build/packaging tests, coherent-memory, loader and host-transport
HDL regressions pass. The loader tests three complete 1 MiB ROM streams,
including abort and memory-fault races at the final write acknowledgement.
The real native IDE management FIFO preserves a complete 256-word sector
with net delays, every request-arrival phase and clock-gating pauses.
Mutations restoring late data capture or unsafe strobe timing reproduce
skipped words. Encoded IDE request states cross as coherent six-bit snapshots.

The video normalizer regression checks complete PAL fields, including
adjacent 312-line PAL fields whose VSYNC arrives before the last doubled
row finishes. It also checks blanking, padding, irregular native clock
enables, line-bank overrun suppression and readiness recovery. A throttled
NTSC fixture checks centering and padding; full-rate NTSC is unsupported.
The input FIFO test uses a
controlled native-machine stub to verify reset retention, keyboard pacing
in native time, mouse sample hold, queue ordering and backpressure.

The Amiga test scripts retain exact source hashes in ignored build receipts.
Vivado 2025.2 accepts the service-clock MMCM ratio and elaborates the complete
shell with the imported mixed-language core. Real stock and accelerated CPU
fixtures execute an original synthetic ROM against the production memory
adapter, including chip and fast RAM, repeated fast-RAM overwrites, a
one-word blitter DMA copy with CPU readback and a palette-register write.

The earlier `hardware02` attempt used a 113 MHz service domain and FPGA
build ID `4D000801`. Placement failed setup timing by about 13 ns, including
native-to-service buses and the native clock-enable path. Implementation
was stopped after retaining its reports; it produced no qualified image.
The sibling 28 MHz architecture was built as `4D000802` with a fresh source
capture. Final routed setup is +0.264 ns, hold is +0.046 ns and the native
clock-enable path is +13.073 ns. Strict clock, CDC, DRC, timing coverage and
export gates pass. The exact FPGA source archive remains immutable.

The accepted desktop-alignment test used firmware `M0.8.0dev2`, build `4D000803`,
with FPGA `4D000804`, ABI 0.27. Build 804 passes routed setup +0.259 ns and
hold +0.041 ns; native clock-enable margin is +13.331 ns. The user accepted
its corrected standard PAL desktop alignment. Kickstart 3.1 booted in both
stock and accelerated modes on build 802.
The later RTC/IDE correction is installed as firmware `M0.8.0dev3` /
`4D000805` and FPGA `4D000806`. Its timing gates pass (setup +0.244 ns,
hold +0.008 ns, native clock enable +13.748 ns), and the board reports
ready with zero faults. The user confirmed SysInfo operation. Benchmark
accuracy and post-update USB export remain open; see the hardware report.
Both DD drives have performed reads. Both IDE disks passed an original
guest diagnostic covering writes, flushes, readback and restoration; their
whole-file hashes match their pre-test values. USB SD export and restart
and board-generated screenshots were exercised repeatedly.

This is a development port with selected board tests, not full machine
acceptance. See [board qualification](AMIGA_HARDWARE_TEST.md) for the tested
configuration and remaining work. In particular, maximum performance,
hardware audio/input, interlace and application compatibility need further
evidence. The downloaded AROS ROM reaches its splash screen, but hard-disk
boot has not been established.

The AGA acceptance pass must cover A1200 Kickstart boot, stock and accelerated
CPU timing, AGA bitplane and sprite DMA under chip RAM contention, and PAL
interlace on the board. ECS board results apply to the tested ECS images.

On FPGA 826, A1200 Kickstart 40.068 could halt before requesting DF0 in either
CPU mode. The boot trace showed a refresh response replacing the lower word
of an AGA 32-bit chip RAM read after the CPU bridge had sampled the upper word.
The bridge now latches both words on the same CPU read phase. The real-CPU
fixture checks the returned longword when another DMA response takes over
the raw RAM outputs immediately after that phase.
