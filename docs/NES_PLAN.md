# NES integration

Branch `core-nes` adds the MiSTer NES machine at upstream commit
`49a0a662e244469ca77b2155746a066df704ffae` from
<https://github.com/MiSTer-devel/NES_MiSTer>. Exact imported originals, per-file
license records and deterministic portability changes accompany the port.
User cartridges and Nintendo system/game images are excluded from source.

## Machine and shared services

NES uses core ID `0x4e455320` and machine selection 6. Its requested service
clock is 42.954544 MHz, with native 21.477272 MHz master enables every two
service clocks in the absence of stalls. The machine
retains the upstream CPU, PPU, APU and selected cartridge mapper logic.
Cartridge storage uses the existing 64-bit PSRAM service. Local machine RAM
retains its synchronous timing; external cartridge reads hold the complete
machine when data is unavailable.

A bounded scheduler records native master ticks delayed by a ROM miss and
repays them at the service-clock cadence once data arrives. CPU, PPU, APU and
mapper state advance together. Menu and save pauses accrue no delayed work.
An exhausted timing budget reports a load fault instead of wrapping its
counter. The clock remains on one MMCM/BUFG root throughout; no fabric gate
or asynchronous clock is introduced. Cartridge SRAM uses eight byte lanes
with one write port each, so 64-bit save snapshots do not require a large
register array. The build checks RAM inference as well as resource use.

The complete 256x240 NTSC source raster enters the shared capture and display
services through an HP2 DDR ring at `0x23000000..0x233fffff`. The input bridge
checks raster order and preserves both edge columns and all rows. It arms
capture before pixel (0,0). Incomplete frames do not publish. Accepted AXI
transactions drain before a display-clock change or FPGA replacement.

The existing menu, USB controller mapping, two-player input, DAC, output modes,
video size, ordered effects, screenshots and full-PL core loader serve NES.
Auto video size is 4x at 1920x1080 and 1680x1050, and 3x in the other modes.
Manual sizes retain the common centered clipping behavior.

The unsigned APU mixer enters a DC blocker at the 48 kHz audio sample cadence,
using the upstream pole of 511/512 and gain of 1023/1024. The filter holds during
menu and save pauses. Signed 24-bit conversion retains both full-scale extrema
without overflow; CPU/PPU catch-up does not accelerate the audio filter.

## Content and saves

The SD package lives at `multitini/cores/nes/`. User `.nes` cartridges go in
its `media/` directory. The firmware validates the cartridge before replacing
running content. The accepted iNES format, mapper IDs and size limits are
defined by the parser and loader together; unsupported features fail before
execution. Battery saves live beside the cartridge as `<name>.nes.sav` and use
the shared durable journal, sync and recovery process.

The first port targets standard NTSC cartridges and two ordinary controllers.
It accepts iNES 1 cartridges for mapper IDs 0, 1, 2, 3, 4, 7, 9, 10, 11, 66
and 71, with power-of-two PRG/CHR sizes and mapper-specific size checks.
Cartridge SRAM is limited to 8 KiB. NES 2.0 headers, trainers, four-screen
mirroring, PAL and unsupported mapper IDs are rejected explicitly.
Famicom Disk System, VS/PlayChoice, light guns, expansion peripherals and
save states are outside this port. No compatibility claim extends beyond the
implemented cartridge formats and mapper set.

## Build and verification

Generate and build only the NES image with:

```powershell
python scripts/build_core_nes.py
```

The runner captures the exact source bytes before implementation, checks clock
and reset constraints, and requires the established strict timing and CDC
gates. `scripts/package_core_nes.py` creates the SD tree from the new NES image
and explicitly selected retained images. Retained images keep their original
ABI/build IDs, reports and source archives; a firmware version does not rename
their hardware inputs.

Focused checks include `scripts/test_nes_video.py` and
`scripts/test_display_geometry.py`. NES native video exercises complete RGB565
rasters, sparse enables, malformed/truncated frame rejection and readiness
recovery. The DDR test exercises ring ownership, backpressure, AXI failure,
display-clock stop/restart and source-reset drain. Geometry covers seven
machine selections, all mode IDs and manual clipping.

`make nes-test` runs portable loader, memory, save and audio simulations.
Audio checks include signed extrema, DC removal, pause and frequency response.
`make nes-machine-test` requires Vivado XSim to execute the original T65 VHDL
with the imported PPU/APU and mappers. Its original generated test cartridge
checks reset-vector execution, both serial controllers, the complete raster,
pause, and frame cadence with changing tile data and delayed PSRAM replies.
The native firmware checks are `scripts/test_nes_runtime.py`; the broader
validation includes the existing 86 CTest and 257 Python checks.

Build and simulation results do not establish physical support. Board tests
must cover cold boot, loading, two controllers, audio, native edge pixels,
all output modes and effects, battery save/reload, core switching and recovery.
No board flash or gameplay test is implied by this branch.
