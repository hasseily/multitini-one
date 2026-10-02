# Independent video smoothing

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
Bloom retains its original kernel and adds its halo to their combined result.
These are deliberate new levels, not a match for the previous two-dimensional
blur.

## Interface and implementation

ABI 0.17 identifies the new meaning of the existing effect fields. The
register addresses, saved settings wire size, and effect count remain unchanged.
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

Build and test results will be recorded with the delivered package. No board
acceptance has been recorded for AX01.
