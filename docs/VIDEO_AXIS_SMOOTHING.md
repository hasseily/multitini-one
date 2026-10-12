# Independent video smoothing

Historical AX01/GL01 record. PHO1 supersedes these controls and kernels with
[Appletini Blur, Dot bleed and Glow](PHOSPHOR_PHOS1.md).

AX01 changes the Effects page to two independent controls:

- **Horizontal smoothing** blends native source columns before centered
  interpolation during enlargement. Light, Medium, and Strong use increasing
  native blend strengths. Off repeats pixels exactly.
- **Vertical smoothing** blends five native source rows at three evenly
  spaced strengths. It does not blend columns.

Both controls default Off. The owner confirmed there are no existing presets
to migrate. The former horizontal Sharp mode is replaced by Strong smoothing.
Color, persistence, bloom, scanlines, and masks keep separate controls.

## Pixel contract

For output coordinate `x`, let `n/d` be the active enlargement ratio,
`source = floor(x*d/n)`, `phase = (x*d) mod n`, and
`distance = 2*phase + d - n`. The interpolation neighbor is the previous
source pixel for negative distance, otherwise the next source pixel. Clamp
at the first and last source pixels. The base Q8 weight is
`round(abs(distance)*128/n)` for every enabled level.
At 1:1 there is no subpixel interpolation, but native horizontal blending
still applies. At 3:2, pixel phases alternate
according to the same centered mapping rather than an integer-scale shortcut.

Expand both RGB565 samples to RGB888, mix each channel using
`(center*(256-weight) + neighbor*weight + 128) >> 8`, and truncate back to
RGB565 before the existing color matrix and tone LUT. This is a deliberate
new processing order: the old output horizontal filter ran after the LUT.
Scanlines and masks remain after color processing. Menus and decorative
borders bypass interpolation and picture effects. An opaque UI pixel does
not become an interpolation neighbor; all neighbors come from the game image.
Screenshots use the same integer operations and display geometry.

Before this scaling step, horizontal smoothing uses the native three-column
kernel `[1,2,1]/4`, mixed with the original center at Q8 weights **64, 128,
192** for Light, Medium, Strong. Floor the kernel, then round the center/kernel
blend. This gives Strong more smoothing than interpolation alone, without
extrapolating past neighboring pixels or distorting the interpolation phases.

Vertical smoothing uses fixed per-core 1080p reference weights at every
output resolution. The table gives Q8 mixing coefficients out of 256:

| Core | Light | Medium | Strong |
| --- | ---: | ---: | ---: |
| C64 / Genesis | 128 | 176 | 224 |
| Game Boy / Color | 96 | 132 | 168 |
| Vectrex | 128 | 176 | 224 |

All vertical levels use `[1,2,2,2,1]/8`. Mix the floored kernel result with
the center row using the listed weight. Sharing the footprint avoids a large
Medium-to-Strong jump in blur width. Vertical smoothing consumes the horizontal
result, so both axes compose independently. Persistence precedes both axes;
Bloom adds a fixed-radius halo to their combined result. Its separable
`[1,2,1]/4` filter covers three native columns and three native rows at every
strength. Each pass floors once in RGB888. Light, Medium, and Strong add
1/8, 1/4, and 1/2 of the same halo, saturating each channel before RGB565
packing. The halo comes from the persisted image before either smoothing
axis, so smoothing changes the base without changing the halo kernel.
The radius is one native pixel; enlargement magnifies it with the picture.
These are deliberate new levels, not a match for the previous two-dimensional
blur.

## Interface and implementation

ABI 0.17 identifies the new meaning of the existing effect fields. The
register addresses, saved settings wire size, and effect count remain unchanged.
GL01 / ABI 0.18 makes Bloom's radius fixed. ABI 0.17 cores retain the previous
five-tap Strong halo; load newly built ABI 0.18 cores to use the improvement.
Light and Medium keep their existing pixels. Existing settings need no
migration. Firmware GL01 also updates the portable/NEON frame reference.
`VIDEO_FRAME_OPTIONS` uses bits 7:6 for horizontal smoothing, alongside
persistence at 1:0, vertical smoothing at 3:2, and Bloom at 5:4; bits 31:8
remain reserved.
New firmware requires a matching core package before enabling these effects
or reconstructing screenshots. Older cores can still be loaded.

Horizontal interpolation reads adjacent pixels from the existing native-row
cache. It adds no DDR frame pass. Vertical smoothing reuses the native filter
line buffers; Bloom still needs horizontal taps. The intended output is one
pixel per clock with the existing nine-clock effects latency. No new clocks,
clock crossings, or frame storage are required. Reset, word-boundary prefetch,
viewport edges, and output metadata alignment require directed validation.
The cached reader requires at least four leading pixel clocks before the
viewport; every supported centered core/mode geometry supplies that margin.

## Validation and delivery

Source and arithmetic checks do not establish board performance. Before
delivery, run host references, screenshot checks, reader/effects/native-filter
simulations, and the autonomous frame pipeline with all display scales.
Each rebuilt core must pass the normal Vivado timing, CDC, and routing gates.
The test bundle must contain matching firmware, all machine core variants,
and exact build sources and settings. Hardware testing must check the visual
levels plus zero new capture drops, display underruns, and deadline misses.

The revised levels passed 61 host tests and 158 Python checks. The native
filter matches the C reference for all horizontal/vertical/Bloom combinations,
edge geometries, and all seven active output scales. Reader, effects, and
complete pipeline checks pass with portable models and Vivado's actual XPM
models, including both reset arrangements and source-pause recovery.

With all effects at Strong and the test's bounded DDR stalls, the full frame
engine takes 710,169 clocks for 540x720 and 843,733 clocks for 640x720. At
133 MHz these are about 5.3 and 6.3 ms, below the 16.7 ms frame budget. This
is simulation evidence, not a measured board frame rate.

The rebuilt GBC image uses 16,461 LUTs and 21 DSPs, compared with 16,087 LUTs
and 33 DSPs in the retained 2026-09-30 image. Its block RAM remains 88 RAMB36
and 12 RAMB18 tiles. Exact per-image timing reports, source captures, and
firmware audits accompany the test bundle. No board acceptance has been
recorded for AX01.

## GL01 fixed-radius bloom

The 2026-10-11 change removes the wider Strong halo branch from the native
FPGA filter. The software reference uses the same three-tap kernel at all
strengths. Four line RAMs, the nine-clock pipeline, clocks, resets, crossings,
frame ownership, and DDR transfers keep their existing contracts.

Directed acceptance checks use RGB565-visible white lines on both axes:
Light/Medium/Strong brighten the same adjacent row or column, leave the second
neighbor black, and produce distinct stored pixel values. An independent
3x3 stencil checks all smoothing pairs, per-core reference scales, clamped
edges, saturation, and unchanged persistence history. The C/HDL oracle and
autonomous pipeline checks pass locally. Full core bitstreams and board
acceptance remain separate gates.

GL01 passed 66 native-filter cases (29,568 C/HDL jobs), 16 complete frame-engine
cases (2,865 jobs, including full-size frames and AXI fault injection), and
portable/XPM autonomous pipelines at all seven output scales. The Cortex-A9
NEON/scalar comparison passed 724 cases. ABI and package checks passed 13
tests. The source-generated RGB565 preview confirms identical Light/Medium
pixels and the revised Strong footprint.

The host suite passed all 61 tests after restoring CherryUSB to the repository's
pinned `06f424d1d23dd271dcc9cc1e19ec47ca07032dd6`. The initial checkout at
`825dc4166ef4b92ced2b15446c668e74f2bc064c` lacked the EHCI active-retry fix and
failed `usb_ehci_completion`. The unchanged regression now passes all 23
completion cases, including active retries and error classification.

Vivado 2025.2 routed the isolated frame engine at a 7.500 ns clock period:

| Native frame / reference scale | Setup slack | Hold slack | LUTs | Registers | RAMB36 / RAMB18 | DSPs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 540x720 / 3:2 | +0.526 ns | +0.082 ns | 2,626 | 2,255 | 4 / 0 | 3 |
| 382x270 / 4:1 | +0.718 ns | +0.078 ns | 2,645 | 2,274 | 0 / 4 | 3 |
| 160x144 / 6:1 | +0.106 ns | +0.076 ns | 2,648 | 2,254 | 0 / 4 | 3 |
| 640x720 / 4:1 | +0.359 ns | +0.087 ns | 2,629 | 2,258 | 4 / 0 | 3 |

All four runs have zero failing setup, hold, and pulse-width endpoints;
pulse-width slack is +2.620 ns. These numbers cover the engine/filter block.
The isolated runs omit PS7, board I/O and complete machine integration, and
retain the corresponding DRC warnings plus DSP pipelining advisories. They
do not establish full-core timing, CDC or board performance. Local reports,
source hashes and the preview are under `build/gl01-validation`,
`build/gl01-ooc` and `build/gl01-preview`.
