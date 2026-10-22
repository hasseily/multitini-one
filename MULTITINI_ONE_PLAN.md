# Multitini One: Architecture and Working Guide

Multitini One runs the Appletini ONE board as a standalone FPGA system.
It shares board services, firmware and menus across six machine cores:
C64, Game Boy Color, Vectrex, Genesis / Mega Drive, NES and Apple IIgs.
The image set contains the supervisor, C64 PAL, GBC, Vectrex, Genesis NTSC
and PAL, NES and IIgs. Machine compatibility and hardware acceptance are
recorded in each core's documentation and the selected package's evidence.

This file describes the current design and working rules. Keep release
history, diagnostic logs and measured results in their dedicated documents
and build receipts; do not append a chronological work log here.

## Board and execution model

The board uses a Zynq-7020 (XC7Z020-2CLG484I), 1 GB DDR3L, 16 MB QPI PSRAM,
16 MB QSPI flash, SD, a TFP410 DVI transmitter, an AK4493 stereo DAC, two
USB3300 PHYs, RTC, temperature sensor and UARTs. The standalone 5 V jumper
fix is assumed present. The Apple connector supplies no machine clocks,
reset or bus services; its outputs remain in safe idle states.

Golden Boot provides recovery and installs `FIRMWARE-MULTITINI.BIN`.
The firmware image contains the FSBL, supervisor FPGA image and ARM program.
Machine FPGA images live on SD. The supervisor replaces the entire PL image
through PCAP while ARM remains in DDR. Every machine image includes the
common shell and compatible PS setup. Full-PL loading avoids the floorplan
and interface constraints of partial reconfiguration.

Core changes flush media, quiesce transfers, isolate PL, load the selected
image, verify its identity and ABI, then start its service. A failed switch
attempts rollback. ROM-load or compatibility failures hold the machine in
reset and report an actionable error.

## Ownership and interfaces

| Layer | Responsibility | Reason |
| --- | --- | --- |
| ARM CPU0 | SD files, USB input, menus, settings, media requests, updates and core selection | One owner for storage and board policy |
| ARM CPU1 | Prepare video-effect tables and controls | Keeps effect preparation separate from foreground control and media service |
| Common FPGA shell | Pins, clocks, resets, command/data interface, frame capture/processing/scanout, audio output and memory connections | Board behavior stays consistent across cores |
| Machine adapter | Translate the shell interface into machine signals and services | Keeps imported machine logic close to its pinned source |
| Machine core | CPU, chipset, native timing and machine-visible peripherals | Preserves software compatibility |

Ports reuse machine HDL rather than MiSTer's Linux/HPS platform or binary
images. Replace platform PLLs, RAM primitives and host services through
reproducible port scripts. Add common services only when machines need them;
keep machine-specific behavior in its adapter and ARM service.

The versioned interface carries commands, framed direct loads, media
requests/completions, input and status. Validate lengths, ranges, request IDs
and mount generations. Cross clock domains with explicit handshakes or
synchronizers; hold multi-bit payloads stable until accepted.

## Memory, video and timing

Use BRAM for state requiring predictable latency. Use PSRAM where its measured
service time and the machine's wait rules fit. Use DDR for large stores and
frame buffers, with explicit arbitration, caching and backpressure. Capacity
alone does not establish that a memory path meets a machine's timing needs.

MAME is the C64 behavioral reference. Pin the reference source and compare
observable CPU bus transactions and memory mapping against it. Stock mode
must preserve native machine timing. CPU acceleration is an explicit extension;
it must preserve NMOS 6510 instruction and bus semantics while VIC, CIA, SID
and drive timing remain native. Keep local-RAM inference, scheduler tests,
MAME differential coverage, routed timing and board acceptance as separate
gates. See [C64 RAM and CPU scheduling](docs/C64_RAM_CPU.md).

The IIgs uses a coherent DDR cache, posted writes and a fast DMA path. Its
65816 runs at selectable stock or accelerated rates up to the image's
advertised maximum. Native video, audio and peripheral timing remain defined
independently. Accelerator None means stock; changing accelerator cards resets
guest accelerator state. Preserve DMA/cache coherence and shadow-memory rules.
See [IIgs integration](docs/APPLE_IIGS_PLAN.md) and
[SmartPort](docs/APPLE_IIGS_SMARTPORT.md) for the eight-drive standard/extended
interface, banked buffers and wide disk addresses. New SmartPort hardware
acceptance is tracked separately from implementation and simulation.

Video uses the shared FPGA frame/output path and CPU1-prepared effect controls. Output stays at
nominal 60 Hz across the six selectable display modes; 1080p is the reset
default. Fit, centering and border rules preserve the complete machine image.
The IIgs frame includes 42 native border pixels on each side and 24 source
lines above and below SHR, giving 724x496 after line doubling. Auto scaling
keeps the full border visible. Machine frame rates remain independent. Audio feeds the shared I2S/SPDIF
output with controlled mute/reset behavior. USB input is translated to stable
machine events. Axis-bearing HID devices share controller bindings while
composite keyboard/mouse reports retain their native functions; relative
motion expires instead of latching. See [USB input](docs/USB_INPUT.md),
[video settings](docs/VIDEO_SETTINGS.md) and
[shared front end](docs/SHARED_FRONTEND.md).

## Storage and user flow

F12 opens the shared core/media menu. Firmware starts at standby unless
Autoboot is enabled. Core options, media paths and saves belong to that core.
About shows firmware build, FPGA build and Interface, followed by Golden Boot.

SD layout is `multitini/cores/<core>/`: `core.cfg` and FPGA payloads describe
the core, `roms/` contains boot ROMs, and `media/` contains user content.
Browsers support nested relative paths without escaping the media root.
Validate media before loading; reject unsupported formats with a clear error.

Writable media uses synced journals or the service's durable save mechanism.
Flush before eject, reset, core switching or storage handoff. A failed write
must preserve recovery data and return a defined error or explicit retry state.
Retain saved-setting compatibility when extending a core's drive list/options.

USB0 exports the SD card to a PC; USB1 remains the input host. Export is
exclusive: close local files, flush media, quiesce the core and unregister
FatFs before connecting. Stop USB and SD DMA before remounting locally, then
restart the core. Failures retain exclusive ownership until safe recovery.
See [USB SD access](docs/USB_SD_MOUNT.md).

Keep transfer hot paths free of periodic logging and timing probes. IIgs
transfers share normal USB polling and retain the paced fallback needed during
long operations. Use existing counters first. Measure any diagnostic overhead
and remove temporary instrumentation once the diagnosis is complete.

## Source and content rules

Original Multitini code is GPL-3.0-only. Preserve imported notices and record
exact commits in [source pins](provenance/sources.lock.json) and per-file
licenses in [licenses/files.csv](licenses/files.csv). Board support originated
in `../appletini-one`; copy only what is needed and do not change that sibling
as part of Multitini work unless requested. Official product imports require
compatible redistribution terms; NonCommercial code needs an explicit grant.

User system ROMs remain external inputs. Private packages may include the
explicitly supplied ROM collection under each core's `roms/`, with input,
conversion and output hashes. Keep these bytes out of Git and source archives.
Include media loaders and empty media directories; add games or OS disks only
when specifically requested. Source-built licensed assets retain their notices.
See [content policy](docs/CONTENT_POLICY.md).

## Build, package and validation

Use Vivado/Vitis 2025.2 and source-generated projects. Keep vendor output,
ROM assets, logs and packages under ignored `build/`. HDL selection comes from
`hdl/hdl_sources.txt`; Tcl creates the project and enforces export gates.
Vitis captures actual application and BSP inputs, including the CPU1 worker
embedded in CPU0. On Windows, use `scripts/run_native_tool.py` for guarded
native tool launches.

1. Run relevant host tests, deterministic port/import checks and small HDL
   simulations before a full build. Test malformed content, interrupted I/O,
   reset and failure paths alongside normal operation.
2. Build only affected FPGA images. `scripts/build_core_gs.py --image apple-iigs`
   selects IIgs alone. Use a fresh output directory and explicit build ID.
   ARM-only changes can reuse compatible FPGA images. Common-shell changes
   require reviewing every affected image.
3. Pass routed setup/hold timing, clock/constraint coverage, CDC, bus-skew,
   DRC and resource checks. Exceptions must describe proven clock-enable or
   crossing behavior; never hide failing paths with broad exceptions.
4. Build ARM against the selected XSA and qualified BSP. Verify compiled
   sources, linked storage behavior and the embedded CPU1 image. Freeze inputs
   during builds and retain exact source archives, settings and reports.
5. Package explicit artifact selections with `scripts/package_core_gs.py`
   (use `--include-nes` for the complete image set). Reused images retain their
   own build IDs, ABI, source archives and timing evidence. Verify platform
   compatibility; never substitute an arbitrary bitstream or relabel old output.
6. Audit the final ZIP, manifests, hashes, source bundles, ROM placement and
   firmware payload. Install matching ARM, IIgs FPGA, `core.cfg` and slot ROM
   together when their machine interface changes.
7. Test the actual package on hardware: boot/reset, core switching, video,
   audio, input, media writes/eject and recovery. Host tests, simulation and
   timing closure are separate evidence from board acceptance.

Use [artifact reuse](docs/CORE_ARTIFACT_REUSE.md),
[Vivado handoff](docs/VIVADO_HANDOFF.md), and each core's build/hardware guide
for commands and qualification details. A hardware-test delivery contains all
required build artifacts and matching evidence; claim hardware validation only
for the exact package and behavior actually tested.
