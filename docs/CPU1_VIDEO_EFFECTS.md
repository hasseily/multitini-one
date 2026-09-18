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
| C64 NTSC | 404 × 250 | 896 bytes |
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
hardware platform and matching FSBL, not an older platform export. The VID2
package build ID is `0x56494432` and its ABI is 0.15.

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
