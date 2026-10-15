# NES build and validation

M0.5.1 corrects NES 2.0 cartridge loading, mapper 2 submapper 2 AND bus
conflicts and deterministic NTSC CPU timing. It also adds the About tab.
The package is `build/firmware-core-nes-m051.zip`; install its
firmware and NES core together. Its current validation is recorded separately
under `build/core-nes-m051/`. The owner's five files are local test inputs;
game payloads are excluded from source and delivery. See [NES integration](NES_PLAN.md)
for the bounded header contract and [board instructions](NES_HARDWARE_TEST.md).

## M0.5.1 cartridge correction

The owner's Contra (USA), Zelda (USA Rev 1), Super Mario Bros. (World),
Super Mario Bros. 2 (USA Rev 1) and Super Mario Bros. 3 (USA Rev 1) all contain
NES 2.0 headers. The previous parser returned unsupported-version before
resetting or streaming the core. Their mapper IDs are 2, 1, 0, 4 and 4.
Contra additionally specifies submapper 2, meaning ROM/CPU AND bus conflicts.

The corrected parser validates the supported NES 2.0 fields and produces a
canonical 16-byte transport header. The shared stream engine overlays that
prefix on the wire. It retains the caller's original buffer and every PRG/CHR
byte. The source file is never rewritten. Unsupported RAM, timing, input and
board variants now produce specific error messages.

`build/core-nes-m051/private-file-tests/validation.json` records the exact
private file hashes and original/canonical headers. The native probe reproduced
the old rejection against commit `e7e4234`, then passed each complete original
file through the production parser and boot code in both command and burst
transport modes. It checked byte-for-byte payload and input preservation,
unchanged SD files, and acceptance of the emitted header by the production
HDL validator. Reproduce it without modifying media:

```text
python scripts/test_nes_files.py --media <media-directory> --baseline-ref e7e4234
```

The new NES FPGA carries canonical mapper 2 byte-10 bit-5 through to the
mapper subtype. Its existing ROM-read path provides the old byte before a
conflicting bank write. The real T65 regression proves that CPU write 3 against
ROM byte 1 selects bank 1 with AND conflicts and bank 3 without conflicts.
An 82-service-clock cold read held CPU and PPU state, and exactly one mapper
write committed after the reply.

The imported PAL tick counter had no deterministic reset in the fixed NTSC
configuration. The derived port now resets that counter and qualifies PPU
cycle skipping for PAL only. The real CPU fixture checks exactly 12 native
master ticks between CPU enables and 24 service clocks when unstalled. Its
generated cartridge exercises JSR/RTS, OAM DMA resume and repeated NMI/RTI,
including stack-pointer restoration. No additional clock or timing exception
is introduced.

The optional private-cartridge smoke test runs the production parser and boot
stream, then the original T65/PPU/APU with delayed PSRAM replies. Use 60 frames
to allow cold initialization before checking the picture:

```text
python scripts/test_nes_cartridge_smoke.py --directory <media-directory> --frames 60 --build-dir build/core-nes-m051/private-smoke60
```

It requires complete 256x240 rasters, continued CPU execution, at least three
nonuniform frames and no unknown visible pixels after the first three frames.
It has no controller input or physical peripherals. All derived cartridge
copies and final images stay under ignored `build/` directories.
The legacy `cpu_fetches` metric counts T65 `Instrnew` instruction-completion
events; it does not identify opcode addresses or instruction traces.

The final exact-file run passed for all five cartridges at
`build/core-nes-m051/private-smoke60/validation.json`. Each completed 60 full
rasters with 80-service-clock ROM replies, no load/timing fault, no unknown
visible pixels after startup and zero final timing debt. Nonuniform frame
counts were Contra 17, Zelda 21, Mario 1 28, Mario 2 54 and Mario 3 50.
Visual inspection found complete recognizable Mario 1, Zelda and Mario 2 title
screens, Contra's scrolling opening logo/menu edge, and Mario 3's opening red
curtain and checkerboard stage. These are initialization and opening-screen
checks, without controller gameplay, save reload or physical audio evidence.
`private-smoke60/independent-verification.json` checks all 25 machine/harness
and 12 production parser/boot source hashes, exact private file hashes and
unchanged canonical payloads independently. Private frame images and cartridge
copies are excluded from delivery.

The complete loader, memory, save, audio and machine suite passed at
`build/core-nes-m051/release-clockfixed/validation.json`, with all 28 source hashes
matching the final inputs. Existing stress retained zero drift over 5,717,856
service clocks, peak debt 2,148 and twelve blanking recoveries. The current
mapper suite passed all eleven IDs and 85 mapped-address assertions at
`build/core-nes-m051/mapper-current-tests/validation.json`.

The ARM firmware compiled and linked CPU0 and CPU1 as M0.5.1, build
`0x4D000501`, ABI 0.22. The focused runtime report and full native suite are
under `build/core-nes-m051/runtime-tests/`: four focused checks and all 86
CTest cases passed. The Python suite passed 263 tests, with two environment
skips; its final log is `build/core-nes-m051/python-regressions-clockfixed.log`.
The runner uses the UCRT GCC compiler and a checkout-local temporary directory.
Exact ARM
binary, FSBL and platform hashes for the selected About-enabled build are in
`build/core-nes-m051/arm-about-audit.json`.

The main firmware adds About after System. It moves exactly these existing
rows, preserving order, help, values and colors: Firmware build, FPGA build,
Interface, Uptime, Board temperature, USB devices, USB errors, SD core catalog,
Saved data and Audio output. All remaining System rows retain their relative
order. Live status and temperature continue to refresh while About is visible.
Native menu/system/USB navigation checks and the menu-runtime Python checks
cover the move and refresh behavior. Production software previews of both pages
and the current source hashes are recorded under
`build/core-nes-m051/about-tests/`; preview status values are synthetic fixtures.

These results verify the reported rejection and the corrected transport and
mapper behavior. They do not establish physical gameplay, saves or audio for
the five private cartridges. The M0.5.1 firmware and NES image must be installed
together; the other seven FPGA images retain their original identities.

## M0.5.1 routed build

The selected NES image is under `build/core-nes-m051/hardware/nes/`. Its normal
strict synthesis, implementation and export gates passed. Final setup, hold
and pulse-width slack are +0.246 ns, +0.040 ns and +0.265 ns, with zero failing
endpoints. All 43,130 nets routed. The bus-skew minimum slack is +5.400 ns;
the eight critical CDC entries are the eight reviewed external PSRAM waivers.
No new timing exception was added for the counter correction.

The image uses 30,218 LUTs, 19,593 flip-flops, 13 RAMB36, 7 RAMB18 and 12 DSPs.
Cartridge SRAM inference remains 1,536 RAM primitives for 98,304 bits and zero
storage flip-flops. Clock ratio and the 5,083-register-pin census passed.
`build/core-nes-m051/fpga-audit.json` records the complete artifact identities.
The selected bitstream SHA-256 is
`82e4d46845d87be6be96168e49a6caebb85462bf166127bae90de5725f1858fc`.

The clock correction reran full top synthesis and implementation using the
eight unchanged completed IP netlists from the initial M0.5.1 build. The reuse
contract checked all 180 IP input, setting, log and checkpoint files before
and after the build. The normal export script and strict gates ran unchanged.
All 130 captured source inputs match current bytes. The final artifact includes
`reports/ip-reuse-receipt.json`; the 302 superseded initial files remain in
`build/core-nes-m051/superseded-initial/` with a verified hash inventory.

## M0.5.0 build record

The `core-nes` candidate uses firmware M0.5.0, build ID `0x4D000500`, ABI 0.22.
The NES FPGA identifies machine 6 and core `0x4e455320`. The upstream pin and
supported cartridge formats are in [NES integration](NES_PLAN.md).
[Board instructions](NES_HARDWARE_TEST.md) cover installation and acceptance.

The local delivery is `build/firmware-core-nes.zip`, with an unpacked copy at
`build/firmware-core-nes/`. It contains `FIRMWARE-core-nes.BIN`, the identical
`sd/FIRMWARE.BIN`, eight selected FPGA images, source archives, notices,
checksums and validation reports. NES cartridges are supplied by the user.

### Firmware and retained images

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

### Host and video checks

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

### Machine simulation

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

### FPGA clock selection

NES uses a 42.954544 MHz service clock with native 21.477272 MHz master ticks
every two service clocks. Delayed work can advance on successive service
clocks. The stress test retains 80-service-clock replies, approximately
1.862 microseconds at this rate. The PSRAM clock is independent and unchanged.

The earlier 85.909088 MHz service attempt failed placed timing through native
CPU/PPU combinational paths: the worst path required 15.187 ns against an
11.639 ns requirement. Its source archive, checkpoint and critical-path report
were retained under `build/core-nes/hardware-final2/`. The service rate was
reduced while preserving native timing; no multicycle exception was added.

### Routed FPGA result

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

### Tool settings

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
