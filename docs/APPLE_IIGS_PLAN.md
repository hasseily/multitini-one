# Apple IIgs integration

The current source corrects IRQ vector mapping when `SHADOW.IOLC` selects RAM.
Real-CPU simulation verifies the RAM-vector path used by Doom GS. A separate
ADB correction completes empty keyboard polls used by the game's input loop.
See [Doom GS diagnosis](APPLE_IIGS_DOOM_GS.md) for the live observations,
regression evidence and hardware-validation boundary.

M0.6.1 restores legacy graphics lost during Vivado synthesis by declaring
the VGC's three graphics-mode nets before their first use. It rebuilds the
IIgs FPGA and ARM release; the other seven images retain their original
identities. See [legacy video diagnosis and validation](APPLE_IIGS_LEGACY_VIDEO.md).

M0.6.0 adds the eight-drive SmartPort card, extended IIgs calls and banked
fast DMA. See [SmartPort support and validation](APPLE_IIGS_SMARTPORT.md).
It requires the matching ARM firmware, IIgs FPGA image and source-built
2304-byte `hdd.rom`. The other seven FPGA images are retained.

The owner requested branch `core-gs`, the complete MiSTer IIgs port, all required
builds for a board test, and an efficient selectable accelerated 65816.
Upstream is `MiSTer-devel/Apple-IIgs_MiSTer` at
`9adbba8622f378253765b7d438b6cbcc4d03fc57`. The import retains 49 exact source
files and explicit per-file licenses. Original machine code remains separate
from reproducible Multitini ports. Apple ROM bytes are runtime user inputs.

M0.5.9 removes the completed ARM HDD timing diagnostic, including its stream
callback wrapper and slow-baseline switch. The shared USB polling fix remains.
The earlier instrumented hardware measurements below are retained as evidence;
removing the probes is not itself a new board speed measurement.

M0.5.7 shares the last completed full USB poll time between the main loop and
IIgs transfers. Short transfers avoid a redundant poll; long transfers retain
the one-millisecond deferred-service fallback. Normal runtime polls remain
unthrottled. This is an ARM-only follow-up with the same FPGA/video-worker
inputs. After replacing an older core/configuration found on the actual SD
card, the owner confirms that M0.5.7 boots Finder. The load log matches the
selected FPGA CRC32 `D3BAF34E`. New packages use `FIRMWARE-MULTITINI.BIN`;
M0.5.6's timed results below remain the performance baseline.

M0.5.6 fixes an ARM disk-transfer bottleneck: every four-byte FIFO pause
previously invoked a full USB poll. Pace that callback to one poll per
millisecond, correct the USB time conversion, and retain an opt-in UART
profiler for same-image comparisons. The board's final HDD request at 57 MHz
falls from 36.573 to 4.735 seconds; this timestamp is distinct from a visual
Finder-ready check. See the [acceleration record](APPLE_IIGS_ACCELERATION.md).
The owner has also confirmed a usable Finder in roughly five seconds at
57 MHz, working mouse/keyboard and normal window drawing on M0.5.6.
All eight FPGA images and the video worker are retained.

M0.5.5 changes accelerator-card selection in ARM firmware. None forces stock
speed, including older saved None configurations. Card changes use the same
safe save/reset/reload/remount path as ROM changes, clearing all guest ZipGS
state before applying the saved menu configuration. CPU speed changes within
ZipGS remain live. All eight qualified FPGA images are reused.

The acceleration rework on branch `iigs-accel-fixes` removes the emulated
1 MHz Mega II sync while accelerated, posts DDR writes, fills 32-byte cache
lines by burst, clocks the IIgs HP2 port from the machine clock, holds sound
GLU accesses while the GLU is busy and corrects the IWM mode-register decode.
Stock timing is retained. See the [acceleration record](APPLE_IIGS_ACCELERATION.md).

M0.5.3 fixes three acceleration holdoffs: only actual `$C070` paddle accesses
trigger the paddle delay, fast shadow-RAM reads use the fast path, and floppy
motor holds require mounted media of the matching drive family. Native shadow
writes and actual IWM access delays retain their timing. New firmware settings
default IRQ delay to Off and preserve saved compatibility choices. IIgs mouse
sensitivity is saved and applied in firmware without changing the FPGA input
protocol. See the [M0.5.3 record](APPLE_IIGS_M053_BUILD.md) for current evidence.

M0.5.2 corrects the external CPU RTC selection: `$C033` is data and `$C034`
is control. The former adjacent-pair decode selected `$C032/$C033`, so RTC
commands written to `$C034` were lost even at stock speed. The executed-CPU
regression now writes and reads PRAM through both registers and verifies
that `$C032` cannot change RTC control. The 65816 and microcode remain unchanged.
The ROM-backed interrupt dispatch bytes at `$C071–$C07F` also need DDR reads,
although they lie in the I/O address range. The external CPU must fetch those
bytes before the native commit, preserving the existing I/O timing. Treating
all I/O as local data supplied stale vector bytes on the first System 6 IRQ.
The native IWM ports also retain fractional-timer carry and run the byte-latch
expiry counter independently of debug logging.
See the [M0.5.2 record](APPLE_IIGS_M052_BUILD.md) for the startup/media update;
the M0.4.0 build and timing details below remain its historical baseline.

## Architecture

The fifth machine uses the same full-PL loader, menu, USB input, audio DAC,
display modes, ordered effects, firmware update and multiboot services.
Its core identity is `0x49494753`; shell machine selection is 5.

The machine clock is 57.272727 MHz. A synchronous BUFGCE passes one complete
primary-clock high pulse every four cycles for the native 14.318182 MHz clock.
Its rising edge remains on phase 3; the constraints describe its shorter high
pulse explicitly. The reserved 28.636364 MHz port retains its divided BUFG.
The video pixel enable is 14.318182 MHz. CPU and peripheral timebases remain
related. The 65816 now runs on the primary clock with an
execution enable; native peripherals keep their 14.318182 MHz clock. The CPU
runs at stock 2.86 MHz, 3.58 MHz, 4.77 MHz, 7.16 MHz, 14.32 MHz, 28.64 MHz
or 57.27 MHz. While accelerated, no access waits for the 1 MHz Mega II sync:
E0/E1 RAM and shadowed writes use a primary-clock slow-RAM port, and I/O
commits through the native bridge without the sync. Sound GLU accesses wait
while the GLU is busy. Stock speed, and the speaker, paddle, counter, IRQ and
CPS compatibility holds and floppy activity with mounted media, restore the
upstream timing rules. Firmware exposes ZipGS acceleration or None at stock
speed. Software ZipGS percentages scale against the installed maximum,
including 100% above the former 14.32 MHz host-only ceiling. Its millisecond
timer remains on the native clock. A TransWarp ROM is not supplied.
ABI 0.22 advertises supported speed codes in `IIGS_ACCEL_CAPS`; firmware uses
the legacy range for older images and clamps saved faster settings to it.

The retained 28.636364 MHz upstream port has no active clock consumers; VGC
runs on the primary video clock with a pixel enable. The build gate verifies
zero sequential clock loads for that reserved port. Any future use must first
define and qualify its phase relative to the native clock.

Fast RAM (8 MiB expansion) and ROMs occupy a dedicated DDR aperture on HP2 at
`0x24000000..0x24ffffff`. A coherent 32 KiB cache with 32-byte lines shares CPU
and HDD DMA accesses and fills a line with one four-beat burst. Writes are
posted into a four-entry merging buffer behind a one-cycle retire slot. Read
misses, bypass reads and loader requests wait until it drains, because AXI does
not order reads against writes. An eight-word asynchronous L0 serves warm fast
reads at the primary cadence.
L1 uses physical tags. L0 uses raw CPU address tags to keep MMU decoding off
warm reads. It invalidates on writes, DMA, loading, reset, configuration,
cache bypass and each native CPU retirement, including mapping soft switches.
Vector pulls bypass L0 so forced-ROM vector bytes cannot alias ordinary RAM.
CPU-only MMU and ZipGS read ports keep the HDD DMA address out of the CPU data
selection; native and DMA accesses retain their shared physical path.
Misses and a full write buffer hold the complete bus cycle and all side
effects. In the IIgs image the HP2 port runs on the machine clock; Genesis and
NES keep it on FCLK0. The DDR path is a synchronous AXI3 master that respects
independent AW/W acceptance, reports write completion only after B, propagates
errors and completes an issued handshake through a machine reset. Video RAM
(128 KiB), DOC RAM (64 KiB) and floppy track buffers remain in BRAM. Cache
writes use byte-lane write enables in the standard inference template.

DDR drain status uses a retained source-clock register before the control-clock
synchronizer. It covers posted writes, request acceptance and outstanding
completion, then requires 15 quiet control cycles counted only after the core
reset request has reached the machine domain. After a sticky fault the held
machine's posted writes are dropped so the drain still completes. A regression injects a decode pulse between
source edges: the registered design suppresses it; the previous captured design
fails. The final CDC gate must confirm the source register and synchronizer.

The four primary-clock phases publish related-clock transfers explicitly.
Native requests and their hold condition publish on phase 2; the peripheral
edge follows on phase 3. A returned toggle and data acknowledge the committed
native cycle before the primary-clock CPU advances. DMA preserves its native
lookup/request/retirement phases. CPU/cache paths keep ordinary primary-clock
single-cycle timing. Global reset, warm/cold reset and loader/configuration
publication are checked separately.

The WOZ controller runs on its original 14.318182 MHz clock. Its track RAM
read-modify-write and lookahead accesses retain upstream edge ordering.
Phase-0 publication carries the loader sector bytes and coherent media state
to that clock; native eject returns through a phase-1 register. Reset holds the
controller long enough to observe native clock edges without discarding media
on a CPU warm reset.

HP3 holds the canonical 724x496 source ring at `0x23000000`. The adapter
preserves the 640x200 SHR image with 42 pixels of native border on each side
and 24 source lines above and below, then duplicates each row. The legacy
560x192 display retains its existing position within that image. Border RGB
comes from the VGC, including its line-latched guest border color. Capture
uses the 24 lines immediately preceding SHR in each standard: NTSC wraps
from 511 to 250, while PAL uses 232 through 255. Guest video timing is unchanged.
The 724-pixel rows use a 1536-byte DDR stride; the 44 padding pixels are never
displayed. Auto uses 2x at 1920x1080 and 1680x1050, and 1x at the other modes,
so all four borders fit. Manual sizes retain centered clipping.
Each scanout cache keeps three rows. IIgs rows contain 181 DDR words and use
256-word internal cache slots, making bank selection simple address bits.
The padding stays inside BRAM; it adds no DDR reads or pixel latency.
Other core widths retain their existing cache layout.
SHR prefetch starts a pixel earlier so its first
visible column contains image data and its final column is retained. Fill
history uses the same first byte as the RGB bypass. The common nominal 60 Hz DVI path consumes these
complete frames. Its output clock remains independent of the machine.

The PS configuration enables HP3. IIgs needs the matching platform and FSBL;
an older FSBL cannot activate this port. Subsequent ARM-only updates can retain
that verified platform and the independently versioned FPGA images. Reuse
checks preserve each image's original sources and require a compatible ABI and
the selected platform's exact PS initialization hash.

## Content and persistence

ROM1/ROM3, character and ADB ROMs are supplied by the user and uploaded while
the machine is reset. The slot-7 SmartPort ROM is assembled from source and
included with provenance. No Apple system or TransWarp ROM bytes
are embedded in the FPGA image or source bundle.

All runtime ROMs belong in `multitini/cores/apple-iigs/roms/`. Disk images,
journals and PRAM belong in the separate `multitini/cores/apple-iigs/media/`
directory. The core package's content root continues to name `media`.
The browser enters nested folders and keeps validated relative paths for
mounts, remembered selections and their adjacent recovery journals. Parent
navigation stays inside the media root.
The stored relative image path, including folders and filename, is limited to
95 characters by the existing settings format. Longer selections report an
explicit error rather than truncating a path or mounting another file.

Eight SmartPort HDDs accept raw HDV/PO and ProDOS-ordered 2MG payloads. The 3.5-inch drive
also accepts exactly 800 KiB PO and ProDOS 2MG payloads; the 5.25-inch drive
accepts exactly 140 KiB DOS-order DSK files. Both drives retain native WOZ2.
The firmware presents sector images as generated WOZ2 GCR tracks, preserving
2MG headers, trailing container bytes and write protection. A `.gsraw` sidecar
retains changed tracks through interrupted writes; keep it beside its image.
NIB and nonrepresentable custom tracks require a native WOZ2 image. The
imported WOZ controller retains both 3.5-inch side buffers, track writes,
flux data and quarter-track mapping.

The firmware serves bounded sector requests with independent per-drive mount
generations. Writes use a synced journal before changing the target image and
are acknowledged only after target sync. A failed write retains the original
sector and operation for retry. Eject forces dirty-track flush while the file
is still open. PRAM saves independently for each ROM revision.

## Validation gates

Import/port reproducibility, repository content policy, host services, real CPU
instruction and interrupt results, coherent cache/DDR transport, complete
loader transactions, native video geometry, full-machine HDD DMA and every
acceleration setting precede final FPGA implementation. Final builds require
resource fit, setup/hold/pulse-width timing, routing, constrained related
clocks, CDC and bus-skew signoff. The bundle audits all Bootgen partitions,
CPU1 embedding, per-core payload hashes, source captures and PS initialization.

Vivado 2025.2 report processing was observed to drop three original checkpoint
MCP selectors. A control probe reapplied the same four setup/hold pairs with
the original positional clock query and preserved all eight effective
requirements, exceptions and slacks through reports and checkpoint reopen.
This records the observed behavior without assigning a vendor cache cause.

Reproduce the signoff repair with:

```powershell
python scripts/repair_phos1_timing.py build/core-gs3/hardware/apple-iigs --signoff-only --iigs-constraint-replay
```

The replay retains the original 174-input FPGA capture, HDL, clocks and numeric
MCP policy. It reserializes the four existing pairs at their exact scopes,
then reruns the normal full export gates and post-report phase, equivalence
and timing-summary checks. Exact repair-tool, helper and XDC copies and the
before/after receipts are published under the build's `reports/repair/`.
Vivado can repeat `Constraints 18-612` warnings for three superseded original
records; the replacement eight effective setup/hold scopes and their coverage
are checked explicitly. Replay qualification completes only when all normal
gates pass.

The selected M0.4.0 IIgs image passes those gates with `+0.040 ns` setup,
`+0.049 ns` hold and `+1.250 ns` pulse-width margin. It uses 35,813 LUTs,
24,529 flip-flops, 124 equivalent block-RAM tiles and 24 DSPs on
`xc7z020clg484-2`. The new ARM firmware and six retained FPGA images are
audited separately for the combined hardware-test package.

The owner confirms M0.5.6 System 6 HDD boot, usable Finder, mouse/keyboard and
normal window drawing at 57 MHz. The broader board matrix still covers both
ROM revisions, legacy and SHR graphics modes, audio, gamepads, disk
write/retry, PRAM, core switching, every display mode/effect and sustained
accelerated operation.
See `APPLE_IIGS_HARDWARE_TEST.md` for installation and the test procedure.

## Reproduce the hardware package

Use Vivado/Vitis 2025.2 and the installed ARM compiler. From this checkout:

```powershell
python scripts/build_core_gs.py --image apple-iigs --output build/core-gs3/hardware --plan
python scripts/build_core_gs.py --image apple-iigs --output build/core-gs3/hardware
$env:MULTITINI_VITIS_WORKSPACE = "$PWD/build/core-gs/v4"
$env:MULTITINI_XSA = "$PWD/build/core-gs/hardware/supervisor/multitini_one.xsa"
$env:MULTITINI_PLATFORM_XPFM = "$PWD/build/core-gs/v2/multitini_platform/export/multitini_platform/multitini_platform.xpfm"
$env:MULTITINI_SKIP_GOLDEN = '1'
$env:XILINX_VITIS = 'C:/Xilinx/2025.2/Vitis'
python scripts/run_native_tool.py -- C:/Xilinx/2025.2/Vitis/bin/vitis.bat -s scripts/create_vitis_workspace.py
python scripts/port_apple_iigs_core.py --build-hdd-rom
python scripts/package_core_gs.py --hardware build/core-gs/hardware --selection-manifest build/core-gs3/hardware/core-selection.json --reuse-image supervisor=build/core-gs/hardware/supervisor --reuse-image c64-pal=build/core-gs/hardware/c64-pal --reuse-image gbc=build/core-gs/hardware/gbc --reuse-image vectrex=build/core-gs/hardware/vectrex --reuse-image genesis-pal=build/core-gs/hardware/genesis-pal --reuse-image genesis-ntsc=build/core-gs/hardware/genesis-ntsc --workspace build/core-gs/v4 --suffix core-gs3 --output build/firmware-core-gs3
```

The M0.4.0 example builds the new IIgs image at ABI 0.22 and explicitly retains
the six compatible M0.3.0 images at ABI 0.21. The firmware uses workspace v4.
Run `--plan` first; the new IIgs selection must pass every normal export gate
before packaging. See [artifact reuse](CORE_ARTIFACT_REUSE.md) for the original
M0.3.1 example, explicit selections and audit rules.

Set `MULTITINI_ROM_SOURCE` to the complete private boot-ROM collection, or pass
`--rom-source PATH` to the packager. Its optional `--iigs-mame-zip PATH` accepts
an owner-supplied archive inside that collection. Validated ROMs are copied only
into the generated private SD package; they are excluded from source archives.

New hardware output and Vitis directories must be fresh. Keep the Vitis directory
short: the vendor Windows compiler cannot write some deep BSP dependency paths
when the workspace name is longer. The example explicitly retains the qualified
supervisor XSA and v2 platform/FSBL. The packager requires their exact matching
PS initialization and each image's timing signoff; it selects no arbitrary
bitstream fallback. Reused images keep their exact original
source ZIP/TAR captures and build settings; the current `source.zip` describes
the new firmware and tools. A firmware release ID does not invalidate a
compatible image's original build ID or interface version.
Use the packager's `--check-only` option to repeat the input and timing audit.
The output contains a source archive, per-build source/tool evidence, checksums,
the matching update image and SD core packages. Golden B2 is retained.
