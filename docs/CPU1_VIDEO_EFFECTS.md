# CPU1 and FPGA video effects

The Effects page saves nineteen controls separately for each core. Effects
default off and require package and hardware ABI 0.15. Older images remain
usable with these controls disabled. Configurable menus and named presets
are outside this change.

The gameplay output contract is 1920 × 1080 at nominal 60 Hz with no processing
frame drops. Interruptions while changing cores are acceptable. The FPGA owns
capture, persistence, blur, bloom, presentation, and the
streaming color pipeline. CPU1 computes color tables when settings change;
CPU0 handles USB, storage, UI, and register access. Neither CPU schedules
individual video frames or touches the capture, output, or history pools.

The VID4 package supplies one C64 variant, PAL, alongside GBC and Vectrex.
C64 NTSC is excluded from the current build and package set.

## Fixed output cadence

All machine variants use a 148.5 MHz pixel clock and 2200 × 1125 total timing.
A 50 Hz PAL machine retains its authentic machine timing; the existing source
adapter presents it on the 60 Hz output raster. Repetition needed for that
rate conversion is separate from a missed effects-processing deadline.

After startup, the hardware scheduler processes the oldest complete capture
once per output period. A result must be ready for the following presentation
boundary. It never selects the latest capture, skips an older one, or depends
on CPU polling. Integration tests measure a fixed two-frame source-to-display
latency. The ordered identity
path remains running with effects off, including while settings are updated.

Machine reset, content loading, and diagnostic-screen selection can make the
source unavailable without stopping the display raster. The capture wrapper
supplies a black native frame for each such period and resumes source pixels
in order when they return. The schedule never waits for a CPU recovery call;
persistence can decay naturally during these black-input periods.

A missed deadline, full ring, AXI error, or display underrun is a fault, not an
accepted performance mode. Hardware stops launching jobs; firmware reports the
fault and disables effects transport. Accepted DDR transactions must drain
before reset or reuse. Fault fallback preserves output timing but does not
meet the no-drop acceptance gate. Startup, fault recovery, and full-PL core
replacement are separate from steady-state processing qualification.

## Processing order

1. Persistence takes the per-channel maximum of current pixels and decayed
   RGB888 history, once per 60 Hz processing tick. The brightness knee tests
   maximum channel >=32. History resets on startup, restart, or a change to
   persistence/blur/bloom settings.
2. Blur is horizontal three-tap for Light, separable three-tap for Medium,
   and separable five-tap for Strong. Edges repeat their nearest pixel.
   Each blur pass scales by the source-to-output pixel ratio and the selected
   level as described below; the Vectrex reference appearance is preserved.
3. Bloom adds a filtered halo at one eighth, one quarter, or one half strength,
   with saturation. It operates independently of blur. Neither blur nor bloom
   feeds back into persistence history. The spatial filter retains four RGB888
   rows in block RAM and processes the stream through a five-by-five window.
4. After scaling, the FPGA applies a signed Q10 color matrix and three
   256-entry LUTs for brightness, contrast, gamma, saturation, hue, temperature,
   RGB gains, color/white/green/amber/sepia tint, negative, and posterization.
   CPU1 builds these tables only when settings change; a complete bank commits
   at vblank.
5. Output-resolution FPGA effects add scanlines, aperture/shadow/LCD masks,
   and horizontal softening or sharpening. This streaming stage adds nine
   pixel clocks with matching sync and blanking delay, followed by the existing
   DVI output register.

Menus, diagnostic backgrounds, and decorative borders retain their colors.
Streaming filters repeat image and overlay boundaries. Frame processing
includes any artwork already composed by the active machine adapter.

The output-resolution horizontal filter uses balanced, bounded-width sums
to shorten its 148.5 MHz arithmetic path. Its kernels, rounding, saturation,
border handling, and pipeline latency are unchanged.

## Blur across source resolutions

The same native filter spans more display pixels when a core has a lower
source resolution. For example, the old Light filter's one-pixel neighbor is
six display pixels away for Game Boy / Color, but only 1.5 pixels away for
Vectrex. Blur uses the actual scaling ratio already supplied by the machine
adapter, rather than choosing strength by core name or inferring it from
frame dimensions alone.

VID3/VID4 used inverse-square compensation against Vectrex's 3:2 scale.
The owner reported that this left C64 blur barely visible and GBC blur
invisible. The small RGB888 changes could also disappear when stored as
RGB565. Matching numerical display-space variance was not sufficient to
give useful blur levels on a pixel-art image.

The VID5 C64/GBC update uses inverse-linear scaling with separate strengths.
For output scale `s` and level `L` (Light=1, Medium=2, Strong=3), each pass
uses Q8 alpha `min(256, round(384 * L / s))`. It retains the existing floored
kernel calculation, then rounds the blend with the center to RGB888.

| Source adapter | Output scale | Light per axis | Medium per axis | Strong per axis |
| --- | ---: | ---: | ---: | ---: |
| Vectrex / Vectrex32 | 3:2 | 1 | 1 | 1 |
| C64 PAL | 4:1 | 3/8 | 3/4 | 1 |
| Game Boy / Color | 6:1 | 1/4 | 1/2 | 3/4 |

Light remains horizontal only. Medium and Strong blend the horizontal and
vertical passes separately. Light preserves handheld detail while the
larger levels progressively soften it. The frozen Vectrex result remains
unchanged. Bloom retains its existing independent halo calculation, and
neither spatial effect enters persistence history. Off remains an identity
operation. The register ABI and saved setting values are unchanged.

The weights are build-time constants. The implementation reuses the native
line buffers and frame transport; no extra DDR pass or CPU frame work is
introduced. Throughput and the 60 Hz gameplay acceptance gate are unchanged.
VID3 introduced two blend stages; VID5 changes their coefficients without
adding stages. The filter retains nine pipeline clocks, one pixel per clock
throughput, and the existing frame schedule.

## Memory and ownership

| Region | Purpose |
| --- | --- |
| Below `0x20000000` | CPU0 code, heap, and stack |
| `0x20000000`–`0x20ffffff` | CPU1 code and private memory |
| `0x21000000`–`0x210fffff` | Uncached settings-job mailbox |
| `0x22000000`–`0x223fffff` | Four 1 MiB capture slots |
| `0x22400000`–`0x227fffff` | Four 1 MiB output slots |
| `0x22800000`–`0x229fffff` | Dense 32-bit `0x00BBGGRR` persistence history |

| Native frame | Dimensions | RGB565 stride |
| --- | --- | --- |
| C64 PAL | 382 × 270 | 768 bytes |
| Game Boy / Color | 160 × 144 | 384 bytes |
| Vectrex | 540 × 720 | 1152 bytes |

Capture and scanout share 64-bit PS HP0. The effects engine uses dedicated
64-bit HP1 for source reads, history reads/writes, and output writes. Both
interfaces use bounded bursts and drain accepted requests. History uses
`width * 4` bytes per row, without capture/output stride padding. The engine
reads old history before overwriting it. A completion includes every history
and output write response; it is not merely the last computed pixel.

Capture publishes only after all write responses succeed. The scheduler
retains its input slot until the engine completes and excludes both displayed
and pending output slots from allocation. Scanout switches at vblank and
acknowledges the old slot only after outstanding reads drain. It caches native
rows in block RAM. On a missing or failed row, its fault fallback uses direct
machine video for the rest of that display frame and counts an underrun.

Before PCAP, CPU0 disables transport and waits for capture, scheduler, HP1
engine, and scanout to become quiescent. There is no CPU1 frame job to cancel.
Error clearing is allowed only after disable and drain; it does not release
accepted AXI ownership. Restart resets history before processing resumes.

At Vectrex geometry, capture padding plus native processing and scanout require
approximately 376.36 MB/s of aggregate DDR payload at 60 Hz. Separate HP ports
still share the DDR controller. This arithmetic is a design budget, not a
measurement of worst-case board service time.

## Builds and evidence

The Vitis builder creates separate CPU0 and CPU1 BSPs (`USE_AMP=1` on CPU1),
bounds ELF load/BSS ranges, embeds the exact CPU1 binary in CPU0, and writes
`cpu1_manifest.json`. Boot order remains FSBL, PL, CPU0. Use the new HP0+HP1
hardware platform and matching FSBL, not an older platform export. VID4 uses
build ID `0x56494434` and retains ABI 0.15. Earlier VID3 and VID2 test bundles
use `0x56494433` and `0x56494432` respectively.

Settings schema 5 has 290-byte profiles and a 4696-byte file. Nineteen signed
little-endian 16-bit fields occupy bytes 252–289. Schemas 1–4 migrate with
neutral effects while preserving their existing settings.

The scalar/NEON frame kernel remains a test reference for arithmetic, including
all RGB565 inputs, persistence thresholds, border filters, and independent
history. It is excluded from the production CPU1 application, which rejects
legacy frame jobs. Cortex-A9 execution under Unicorn verifies the reference
NEON arithmetic; it is not a measurement of ARM performance.

HDL tests compare the frame engine and filter against the C reference across
all 64 effect combinations, native geometries, and injected AXI stalls/errors.
Scheduler tests cover order, ownership, deadline faults, wrap, and drain.
Integration tests cover capture through presentation without CPU servicing.
Separate tests cover color-table equivalence, CDC/reset, composition timing,
and 4×, 6×, and 3:2 scale factors.

Qualification requires positive routed setup slack, nonnegative hold and
pulse-width slack, clean routing/constraints and reviewed CDCs for every
packaged machine variant. The board gate requires steady 60 Hz output with
all effects enabled and zero deadline misses, capture drops, AXI errors, or
display underruns while USB, SD, and machine activity compete for resources.
Simulation and isolated FPGA timing do not establish that board guarantee.
Use the delivered package manifest for exact completed build evidence.

For board qualification, sample the read-only register block at shell base
`0x40000000` while the selected machine is running. `VIDEO_FRAME_PROCESSED`
must advance by 60 per second over a sufficiently long measurement interval,
allowing sample-boundary uncertainty. Check the presentation sequence as well
as processing count. Capture drops (`+0x1a0`), HP0 errors (`+0x1a4`), display
underruns (`+0x1a8`), deadline misses (`+0x1b8`), and HP1 errors (`+0x1c4`)
must stay zero. Record engine clocks (`+0x1bc`) and maximum AXI stall clocks
(`+0x1c0`). A stopped pipeline must not pass merely because its counters stop
changing. Verify the actual DVI cadence with the display/capture instrument;
software counters alone cannot establish output-pin timing.

Test all packaged geometries, maximum persistence/blur/bloom together, live
control changes, menu use, USB input, and SD activity. Do not clear counters
during a measured run. Begin a new measurement after an intentional core
change or transport restart has drained and reset diagnostics.

Appletini reference: `1fd4f371e8f4ef08bc63875550564590609fcfeb`. AMP startup/build
patterns and phosphor/filter behavior informed this implementation. The shared
transport, scheduler, DDR engine, worker protocol, and streaming pipeline are
Multitini implementations.

## VID2 test bundle, 2026-09-18

`build/firmware-video-vid2.zip` contains the matching supervisor, C64 PAL/NTSC,
PSRAM-enabled GBC, and Vectrex images, CPU0/CPU1 firmware, exact source archives,
and validation reports. Its SHA-256 is
`82c78a09d3865ad4b462bb0f7457c6f5b73ec86f7b6d6f48bc903eedf103960c`.
The included `sd/FIRMWARE.BIN` SHA-256 is
`f57843af81e3fd1c07e8a08154a1df511a640626da7afcf375f42ac3bf87a501`.
Generated images and reports remain outside source control.

| Image | Setup slack | Hold slack | Pulse-width slack |
| --- | ---: | ---: | ---: |
| Supervisor | +0.283 ns | +0.055 ns | +1.833 ns |
| C64 PAL | +0.002 ns | +0.049 ns | +0.265 ns |
| C64 NTSC | +0.031 ns | +0.035 ns | +0.265 ns |
| GBC with PSRAM | +0.376 ns | +0.052 ns | +0.265 ns |
| Vectrex | +0.089 ns | +0.035 ns | +1.833 ns |

All five builds passed routing, coverage, bus-skew, and CDC gates. C64 and
GBC retain only the eight reviewed external PSRAM CDC waivers. The selected
CPU build is `build/v42`, using the HP0+HP1 platform exported in `build/v40`.
Payload readback and an independent archive audit passed.

Validation passed 45 GCC host tests, 44 Clang ASan/UBSan host tests, 179 Python
tests with two skips, and the FPGA arithmetic, ordered-pipeline, and real-XPM
regressions. Ten focused package tests passed after adding the GBC PSRAM gate.
Sanitizer instrumentation covers `multitini_common` and
`xilinx_supervisor_settings_host`; other libraries keep their existing flags.

The narrow setup margins qualify this as a test bundle. No image was flashed,
and uninterrupted 60 Hz gameplay under board load remains unverified.

## VID3 blur validation, 2026-09-18

Light blur preserves 59/63 (94%) of the alternating one-pixel handheld test
pattern's contrast, compared with zero under the previous native kernel.
The same tests cover a generic 240x160 source as well as the current 160x144
Game Boy / Color source; this does not add a GBA core. All 64 Vectrex option
combinations match the frozen pre-change output and history checksum.

At a common 48-display-pixel period, measured contrast response differs from
Vectrex by less than half a percentage point across the packaged source
geometries. This compares blur with Off and averages RGB565 quantizer phases;
it does not claim identical kernels at every spatial frequency.

The updated reference passed 580 Cortex-A9 NEON/scalar comparisons, including
all 64 option combinations at six scale ratios. Host validation passed 45 GCC
tests, 44 Clang ASan/UBSan tests, and 180 Python tests with two skips. The
sanitizer instrumentation scope remains the same as VID2.

The FPGA filter matched 664,460 reference pixels across 884 jobs. The DMA
engine matched 1,117,988 output/history pixels across 753 jobs. Under the
documented injected AXI stalls, the largest 640x720 test took 843,733 clocks
against a 2,216,666-clock frame budget. Portable and real-XPM integration
checks passed for all three scaling ratios, including source pauses, ordered
presentation, live options, drain, and restart. These simulations retain the
two-frame presentation latency; they do not measure physical DDR contention.

The balanced output-resolution horizontal filter passed 523,264 exhaustive
center/neighbor-sum/mode cases against the integer kernels. Streaming checks
passed at all three scales, including composition alignment and 24 actual
CPU1-generated color tables. All six real-XPM scale/reset configurations
passed with the same frozen source. The refactor adds no pipeline stages.

## VID3 qualified build matrix, 2026-09-18

Packaging selects the five `vid3-r2` FPGA builds and the matching `build/v43`
CPU firmware, using the HP0+HP1 platform exported in `build/v40`.

| Image | Setup slack | Hold slack | Pulse-width slack |
| --- | ---: | ---: | ---: |
| Supervisor | +0.393 ns | +0.071 ns | +1.833 ns |
| C64 PAL | +0.154 ns | +0.081 ns | +0.265 ns |
| C64 NTSC | +0.171 ns | +0.030 ns | +0.265 ns |
| GBC with PSRAM | +0.337 ns | +0.025 ns | +0.265 ns |
| Vectrex | +0.053 ns | +0.003 ns | +1.833 ns |

All five builds passed routing, timing coverage, bus-skew, and CDC gates.
C64 and GBC retain only the eight reviewed external PSRAM CDC waivers.
The NTSC clock audit confirmed unchanged clock definitions and no unclocked
or unconstrained endpoints after the physical clock-buffer optimizations.

Each build ran fresh synthesis with an explicitly selected, immutable
same-core implementation checkpoint. `AUTO_INCREMENTAL_CHECKPOINT=0` and
`INCREMENTAL_CHECKPOINT.DIRECTIVE=TimingClosure` prevented automatic reference
selection and targeted timing closure. Measured reuse was 98.97–100% of cells
and 97.51–100% of nets. The bundle includes all five reference checkpoints,
their source/settings records, exact build scripts, and rebuild instructions.
Earlier failed NTSC attempts are archived as historical failures.

The package manifest records exact file hashes. Narrow margins make this a
test bundle. The subsequent board test exposed the boot regression below;
appearance and uninterrupted 60 Hz gameplay under physical board load remain
unverified.

## GP0 boot regression and recovery, 2026-09-18

After installing VID3 from SD and power cycling, the monitor reported no
signal. UART0 on USB0 showed a failed shell probe: MAGIC read `4D5C383D`
instead of `4D543031`. Other constant registers were corrupted as well.
VIDE diagnostics reproduced the same values before CPU1 release and after
the worker reached Ready. PS reset, level-shifter, and clock-divider reads
matched the selected hardware platform; these are register observations,
not measurements of the physical clock or DVI pins.

The GP0 adapter previously followed live ARADDR after the read handshake.
Its delayed read-enable could therefore apply a side effect to a different
address. The adapter now retains the accepted address, ID, and transfer
validity for a full decode cycle before capturing the response. Register
side effects consume the same retained address on that capture edge, once.
Response backpressure and reset preserve this ownership. This also removes
the PS address-launch delay from the large register-decode timing path.
It adds one register-read cycle and no clock, crossing, or video stage.

The original adapter fails the new address-ownership regression; the fixed
adapter passes the existing tests and 24 additional read transactions.
The control-bank regression also passes. The VIDF supervisor test built from
this fix has setup +0.394 ns, hold +0.071 ns, pulse-width +1.833 ns, and minimum
bus-skew +5.933 ns, with no critical CDC findings or unrouted nets.

USB0 upload and flash verification passed for the VIDF supervisor with the
unchanged VIDE CPU firmware. Its first board boot returned correct MAGIC,
ABI, shell ID, build ID, and core ID both before and after CPU1 release.
The shell probe passed, SD catalog loading succeeded, and supervisor standby
completed. A later `:diag` returned the same correct identifiers. This
establishes recovery of register access and startup on this board; it does
not identify the physical cause of the former read-data corruption or prove
display cadence. The owner then loaded the existing VID3 Game Boy Color core
and confirmed that the game was visible. This confirms display recovery on
the VIDE CPU / VIDF supervisor boot followed by that existing GBC image.
Sustained 60 Hz output and zero processing drops under load remain unmeasured.
Existing SD core images must also receive the shared adapter fix; the
supervisor test alone does not update them.

Recovery `:reboot` now bypasses uninitialized runtime/media/USB services when
the shell probe fails, retaining UART drain and verified multiboot reset.
The normal runtime path still flushes media and shuts USB power down.
The recovery command and the subsequent USB0 update were exercised on the
board. Fixed read-only `:diag` output remains available in either console;
its DevCfg `pcfg_done_irq` is a sticky interrupt flag, not a live DONE pin.

The first attempt to install the matching VID4 firmware was cancelled before
flash access: the active GBC core could not flush its battery save. The
normal reboot guard correctly refused the reset. The old GBC image still
contains the read-side-effect defect, but
the generic flush error does not establish whether that defect or another
media failure caused the refusal. No save-protection check was bypassed.

The owner then authorized terminating the game and physically power-cycled
the card. USB0 upload, CRC verification, secondary-slot programming, and
flash verification completed for the matching VID4 firmware. CPU and FPGA
both report `56494434`; all eight repeated MAGIC reads and the fixed
identifiers before and after CPU1 release are correct. The shell probe,
CPU1 startup, SD catalog, and supervisor standby all pass. The installed
5,030,420-byte `FIRMWARE.BIN` has SHA-256
`8dc98614ba2638c59950b5c182e92855ecc8c2b32b107446b3adc65857bb6036`.
The matching machine images still require installation on SD. The earlier
GBC display confirmation does not establish sustained 60 Hz performance
for these new images.

## VID4 qualified build matrix, 2026-09-18

The selected set is `build/v46` CPU firmware and the four `vid4-r1` FPGA
builds below, using the unchanged HP0+HP1 platform from `build/v40`.
C64 has one packaged variant, PAL. Its source timing remains PAL while
the shared display raster remains nominal 60 Hz. The NTSC attempt is
excluded from the package and is no longer a release requirement.

| Image | Setup slack | Hold slack | Pulse-width slack |
| --- | ---: | ---: | ---: |
| Supervisor | +0.386 ns | +0.071 ns | +1.833 ns |
| C64 PAL | +0.166 ns | +0.031 ns | +0.265 ns |
| GBC with PSRAM | +0.333 ns | +0.025 ns | +0.265 ns |
| Vectrex | +0.067 ns | +0.003 ns | +1.833 ns |

All four passed the full routing, timing coverage, bus-skew, and CDC gates.
C64 and GBC retain only the eight reviewed external PSRAM CDC waivers.
All captured CPU and FPGA source hashes match the selected build inputs.
The focused recovery suite passed 14 tests, the package gate suite passed
10 tests, and the strict host compile of the Xilinx entry point passed.

Each FPGA build used fresh synthesis and an explicitly selected immutable
checkpoint: the qualified VIDF supervisor or the corresponding qualified
VID3 machine image. Automatic checkpoint selection remained disabled with
the `TimingClosure` directive. Actual reuse was 97.11–98.31% of cells and
85.68–93.37% of nets; no fallback or timing-constraint relaxation was used.
The package retains the exact checkpoints, source captures, settings,
platform, reports, compile commands, and payload audits needed to rebuild.

The final firmware must match the installed VID4 supervisor image byte for
byte. Machine images still need copying to the SD card and board validation.
The narrow timing margins make this a test bundle; the physical gameplay
checks for sustained 60 Hz and zero processing drops remain outstanding.

## VID5 blur validation, 2026-09-18

VID5 responds to the owner's report that VID4 blur was barely visible on
C64 and invisible on Game Boy / Game Boy Color. The reference and FPGA now
use the level-dependent coefficients described above. This changes only
the spatial blend. Frame sizes, line storage, pipeline depth, clocks,
registers, memory transfers, bloom and persistence remain unchanged.

Focused RGB565 tests check stored red, green and blue values: visible,
progressive changes between levels, monotonic edges, preserved flat fields,
and readable-detail bounds for Light. Both the faint VID4 curve and the
original unscaled handheld blur fail these new checks. The frozen Vectrex
output hash remains `d180bb5d`. GCC, Clang ASan/UBSan, and 580 ARM
NEON/scalar reference cases pass.

Full spatial-filter and DMA-engine comparisons against the C reference
pass, including all machine frame sizes and the maximum 640-by-720 engine
case. That case finishes in 843,733 cycles against a 2,216,666-cycle frame
budget with the test's bounded memory stalls. Portable and actual-XPM
display-pipeline simulations pass at C64, GBC and Vectrex scales, including
source pauses, live option changes, reset and drain. These are simulation
results, not physical DDR-contention or sustained gameplay measurements.

The immutable evidence archive is
`build/video-vid5-validation-evidence.zip`, SHA-256
`7e1f4810ac496c52d02bebaa47d3cef1f70be0c7aecb3cca151848c3cc61b0ae`.
It includes source hashes, full test logs, the negative controls and a
synthetic preview produced by the actual RGB565 C reference. On-card blur
acceptance was pending at delivery; the owner's subsequent confirmation
is recorded below.

### Core-only delivery

`scripts/package_video_blur_update.py` packages two explicit VID5 builds:
C64 PAL and Game Boy / Game Boy Color, at ABI 0.15. Supply the original
`build/firmware-video-vid4.zip` and its expected SHA-256
`e31d5fa603ca2eb4986acba6b8309eb9280c4d3b8eda1e6de02f7e7966ef8d81`.
The packager verifies its retained components against that archive's
historical sources. It verifies the two new FPGA builds against their
captured source inputs and the current checkout. It does not rebuild or
emit the retained CPU, FSBL, supervisor or Vectrex.

The update's `sd/` directory contains exactly four files:

- `multitini/cores/c64/core.cfg`
- `multitini/cores/c64/c64-pal.bin`
- `multitini/cores/gbc/core.cfg`
- `multitini/cores/gbc/gbc-native.bin`

Merge these files into the same SD paths and reload the core. Keep existing
media, saves and settings. No firmware flash is needed. The installed
firmware and supervisor remain VID4 (`56494434`), while the two new cores
report VID5 (`56494435`). Both use the same ABI 0.15 contract. Vectrex stays
on its existing image, and no C64 NTSC variant is included.

The package carries the exact new source/license archive, BIT/XSA inputs,
static same-core reference checkpoints, scripts, source captures, timing
reports and validation evidence. `baseline-dependency.json` pins the
original VID4 archive and its retained component hashes. Keep that original
archive for the unchanged firmware/supervisor/Vectrex source provenance.
The focused packager suite passes 12 tests, and all 10 existing full-bundle
package tests still pass. The new gates reject changed source or payload
copies, a mismatched XSA bitstream, altered synthesis/signoff recipes,
incorrect incremental settings/reuse, and unexpected SD files.

### Qualified VID5 builds

The selected images are `vivado-cpu1-video-vid5-r1-c64-pal` and
`vivado-cpu1-video-vid5-r1-gbc`, both using fresh synthesis with their exact
qualified VID4 same-core checkpoints. Automatic checkpoint updates remain
disabled with `TimingClosure`. No timing constraint or gate was relaxed.

| Image | Setup slack | Hold slack | Pulse-width slack | Minimum bus-skew slack |
| --- | ---: | ---: | ---: | ---: |
| C64 PAL | +0.061 ns | +0.040 ns | +0.265 ns | +5.963 ns |
| GBC with PSRAM | +0.313 ns | +0.025 ns | +0.265 ns | +5.839 ns |

Both pass routing, timing coverage, bus-skew and CDC gates, retaining only
the eight reviewed external PSRAM CDC waivers. The 82 C64 and 90 GBC
captured inputs match the built sources. Each exported XSA contains the
exact selected bitstream and retains the VID4 PS initialization bytes.
The GBC build adds 233 LUTs and 10 flip-flops overall versus VID4, with no
added block RAM or DSPs. Processing latency and frame-memory traffic do not
change. The narrow timing margins make these test images. The owner has
since accepted the blur correction; sustained 60 Hz gameplay measurement
remains separate from that visual confirmation.

### Owner acceptance

After the VID5 delivery, the owner confirmed "Blur is fixed" and requested
closing the GBC startup investigation. Blur is accepted on the board, and
the startup investigation is no longer an active task or acceptance blocker.
No startup cause or corrective fix was established.

Preserve the tested `build/core-update-video-vid5.zip` unchanged: SHA-256
`389d5cb63f488667b9f54d43ff4bd44d8c4a53d998a8a7b9b82b2e76ff79774f`,
source commit `81ad9271fb5fb16bdca35bd6bfd979413db44eac`, with the retained
VID4 firmware and Vectrex. This acceptance record does not rebuild or
replace any delivered artifact. See [Hardware results](HARDWARE_RESULTS.md)
for the owner-reported scope.
