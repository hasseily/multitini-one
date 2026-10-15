# Apple IIgs integration

The owner requested branch `core-gs`, the complete MiSTer IIgs port, all required
builds for a board test, and an efficient selectable accelerated 65816.
Upstream is `MiSTer-devel/Apple-IIgs_MiSTer` at
`9adbba8622f378253765b7d438b6cbcc4d03fc57`. The import retains 49 exact source
files and explicit per-file licenses. Original machine code remains separate
from reproducible Multitini ports. Apple ROM bytes are runtime user inputs.

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
See the [M0.5.2 record](APPLE_IIGS_M052_BUILD.md) for the current update;
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
execution enable; native peripherals keep their 14.318182 MHz clock. Only
the CPU's fast bus cycles accelerate: stock 2.86 MHz, 3.58 MHz, 4.77 MHz,
7.16 MHz, 14.32 MHz, 28.64 MHz and 57.27 MHz. Slow RAM, I/O,
shadowed video writes and disk compatibility windows retain the upstream
timing rules. ZipGS registers and an optional host-only accelerator are
supported. Software ZipGS percentages scale against the installed maximum,
including 100% above the former 14.32 MHz host-only ceiling. Its millisecond
timer remains on the native clock. A TransWarp ROM is not supplied.
ABI 0.22 advertises supported speed codes in `IIGS_ACCEL_CAPS`; firmware uses
the legacy range for older images and clamps saved faster settings to it.

The retained 28.636364 MHz upstream port has no active clock consumers; VGC
runs on the primary video clock with a pixel enable. The build gate verifies
zero sequential clock loads for that reserved port. Any future use must first
define and qualify its phase relative to the native clock.

Fast RAM (8 MiB expansion) and ROMs occupy a dedicated DDR aperture on HP2 at
`0x24000000..0x24ffffff`. A coherent 32 KiB cache shares CPU and HDD DMA accesses;
an eight-word asynchronous L0 serves warm fast reads at the primary cadence.
L1 uses physical tags. L0 uses raw CPU address tags to keep MMU decoding off
warm reads. It invalidates on writes, DMA, loading, reset, configuration,
cache bypass and each native CPU retirement, including mapping soft switches.
Vector pulls bypass L0 so forced-ROM vector bytes cannot alias ordinary RAM.
CPU-only MMU and ZipGS read ports keep the HDD DMA address out of the CPU data
selection; native and DMA accesses retain their shared physical path.
Misses and unacknowledged writes hold the complete bus cycle and all side
effects. The dedicated DDR transport respects independent AXI AW/W acceptance,
returns write completion only after B, propagates errors and drains its CDC
mailboxes before PL reload. Video RAM (128 KiB), DOC RAM (64 KiB) and floppy
track buffers remain in BRAM. Variable-lane cache writes use a complete-word
merge to preserve efficient BRAM inference.

DDR drain status uses a retained source-clock register before the control-clock
synchronizer. It covers request acceptance and outstanding completion, then
requires 15 quiet control cycles. A regression injects a decode pulse between
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

HP3 holds the canonical 640x400 source ring at `0x23000000`. The adapter
preserves the 640x200 active image, including the centered legacy 560-pixel
display, and duplicates each source row. PAL's extra border is cropped; the
active image is preserved. SHR prefetch starts a pixel earlier so its first
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
the machine is reset. The slot-7 HDD bootstrap is assembled from its licensed
AppleWin source and included with provenance. No Apple or TransWarp ROM bytes
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

Two HDDs accept raw HDV/PO and ProDOS-ordered 2MG payloads. The 3.5-inch drive
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

Physical board acceptance remains separate: boot both ROM revisions, legacy
and SHR graphics, audio, keyboard/mouse/pads, disk read/write/retry, PRAM,
core switching, every display mode/effect and sustained accelerated operation.
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
