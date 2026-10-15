# NES build and validation

The `core-nes` candidate uses firmware M0.5.0, build ID `0x4D000500`, ABI 0.22.
The NES FPGA identifies machine 6 and core `0x4e455320`. The upstream pin and
supported cartridge formats are in [NES integration](NES_PLAN.md).
[Board instructions](NES_HARDWARE_TEST.md) cover installation and acceptance.

The local delivery is `build/firmware-core-nes.zip`, with an unpacked copy at
`build/firmware-core-nes/`. It contains `FIRMWARE-core-nes.BIN`, the identical
`sd/FIRMWARE.BIN`, eight selected FPGA images, source archives, notices,
checksums and validation reports. NES cartridges are supplied by the user.

## Firmware and retained images

Vitis 2025.2 compiled and linked CPU0 firmware and the embedded CPU1 video
worker. `build/core-nes/arm-audit.json` records the actual ELF and CPU1 blob
hashes; `arm-settings.json` and `arm-build.log` record the selected workspace,
platform and build. The FSBL's PS7 initialization matches the retained images:
`7f046da603b96ed565e1519564e8687cf702d7a34d0219f145b45468ddcc04b4`.

The selected supervisor, C64 PAL, PSRAM GBC, Vectrex and Genesis NTSC/PAL
images retain build ID `0x4D000300`, ABI 0.21. Apple IIgs retains build ID
`0x4D000400`, ABI 0.22. Their bitstreams, XSA files, reports, settings and
source archives were audited individually. See
`build/core-nes/retained-image-audit.json`. Only NES requires a new FPGA image.

## Host and video checks

The complete native suite passed 86 CTest tests. The Python suite ran 262
tests successfully, with two environment-dependent skips. The NES runtime
runner also passed content validation, durable save handling and actual USB
input routing. Reports are under `build/core-nes/host/Testing/Temporary/`,
`build/core-nes/python-regressions.log` and
`build/core-nes/runtime-tests/validation.json`.

`build/nes-video-tests/validation.json` records both native-video simulations.
The normalizer checked 245,765 exact RGB565 pixels, sparse enables, raster
ordering, truncated frames and readiness recovery. The DDR bridge checked
245,760 displayed pixels, backpressure, slot ownership, accepted-transaction
drain, stopped display clocks and AXI failure recovery. Shared geometry passed
1,680 cases across seven machines, sixteen mode IDs and fifteen dimensions.

`scripts/audit_nes_import.py` verifies twenty exact pinned upstream files,
deterministic portability patches and GPL-3.0-only/BSD-3-Clause notices.
The source and tests contain no NES game, BIOS or logo assets.

## Machine simulation

`build/nes-2x-release-tests/validation.json` records the final loader,
memory, save, audio and real-machine simulations, with hashes matching the delivered
source. The machine run executes the original T65 VHDL and imported PPU/APU
against an original generated cartridge. It checks the reset vector, full
256x240 raster and colour output, both serial pads and strobe, coherent pause,
signed audio extrema and switching from a battery to a nonbattery cartridge.

The fixture changes tile data while delaying cartridge memory replies by
80 service clocks. Eight measured stress frames had zero cumulative timing
drift over 5,717,856 service clocks. The complete run recorded 25,995 ROM misses,
peak delayed work of 2,148 native ticks, below the unchanged test limit of
4,096, and twelve blanking intervals clearing
delayed work.
This measures the fixture's machine cadence; it does not establish the memory
working set or compatibility of every cartridge.

An independent mapper fixture passed 85 mapped-address assertions across all
eleven accepted IDs. It checks PRG/CHR bank changes, mirroring, master-enable
holds, MMC1 serial writes and RAM flags, MMC3 SRAM protection and IRQs, and
MMC2/MMC4 PPU latches. Reproduce it with `make nes-mapper-test`, or
`python tests/rtl/run_nes_mappers.py`. Its source and runner live in
`tests/rtl/`; logs and input hashes are recorded in
`build/nes-mapper-tests/validation.json` and accompany the package evidence.
The fixture models only the disabled savestate register interface; it does
not test savestates or broad title compatibility.

The audio fixture checks the real 48 kHz converter with an accelerated logical
service clock. Measured amplitude gains at 20, 50, 100 and 440 Hz were
0.801239, 0.958166, 0.989029 and 0.999425, matching the analytic response within
one percent. It also checks DC-step decay, full signed excursions, equal
stereo channels, output cadence, between-sample state holds, pause and reset.
This verifies the digital filter separately from the unperformed DAC test.

## FPGA clock selection

NES uses a 42.954544 MHz service clock with native 21.477272 MHz master ticks
every two service clocks. Delayed work can advance on successive service
clocks. The stress test retains 80-service-clock replies, approximately
1.862 microseconds at this rate. The PSRAM clock is independent and unchanged.

The earlier 85.909088 MHz service attempt failed placed timing through native
CPU/PPU combinational paths: the worst path required 15.187 ns against an
11.639 ns requirement. Its source archive, checkpoint and critical-path report
were retained under `build/core-nes/hardware-final2/`. The service rate was
reduced while preserving native timing; no multicycle exception was added.

## Routed FPGA result

The selected NES image is `build/core-nes/hardware-final4/nes/`. Final routed
setup slack is `+0.219 ns`, hold slack `+0.048 ns` and pulse-width slack
`+0.265 ns`, with zero failing endpoints. The earlier `+0.019 ns` routed
repair value included the temporary implementation margin. The final reports
use the ordinary restored constraints.

The complete image uses 30,209 LUTs, 19,624 flip-flops, thirteen RAMB36s,
seven RAMB18s and twelve DSP blocks. The 8 KiB battery SRAM retains 1,536
physical distributed RAM primitives, 98,304 allocated bits and zero storage
flip-flops. Synthesis and implementation both passed that inference gate.

Export checks passed the single NES root clock, exact MMCM ratio, I/O packing,
bus skew, all 32 FIFO pointer crossing bounds, 54 shell first-stage exceptions
and 24 mailbox first-stage exceptions with timed second stages. The eight
external PSRAM input crossings retain the shared shell's existing waiver;
unexpected critical CDC findings remain errors.

The first export stopped on two checker omissions: comparing rounded clock
periods to an unrounded nominal value, and excluding machine 6 from the
capture-FIFO census. `scripts/revalidate_nes_export.py` permits only the exact
reviewed corrections and reruns the normal export gates. It checks the
MMCM's `M=31.5`, `D=5`, `O=22` against its `clk_fpga_2` parent, then bounds
the generated-period rounding to one picosecond. No timing bound is relaxed.

The original 130-file synthesis capture and archives remain unchanged. The
export receipt at `reports/nes-export-signoff/receipt.json` binds the original
and corrected checker bytes, export script, logs and XSA. The package manifest
binds every final signoff report and the receipt. The receipt also verifies
that the bitstream and routed checkpoint stayed byte-identical and that the
XSA contains that exact bitstream. No RTL, constraints or routed design were
changed during this export recovery.

## Tool settings

The FPGA build uses Vivado 2025.2, serial IP jobs and explicit forward-slash
`TEMP`/`TMP` paths. The NES synthesis hook sets `synth.maxThreads` to 1 while
retaining `general.maxThreads` at 8 for implementation. These settings and
the hook's exact source bytes accompany the image archive.

The earlier parallel synthesis worker stopped making progress. A full-design
serial diagnostic completed with zero critical warnings and zero errors.
The selected fallback uses public thread controls; no hidden helper-disable
option is applied. Superseded attempt logs remain separate from final gates.

Builds and simulations do not establish NES board behavior or cartridge
compatibility beyond the tested fixtures. No NES board flash or gameplay
test was performed as part of this port.
