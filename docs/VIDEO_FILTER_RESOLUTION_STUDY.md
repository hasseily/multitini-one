# Video filter resolution study

Date: 2026-10-01. This study changes no filter behavior or FPGA source.

Historical baseline through CT02. AX01 replaces these controls with
[independent axis smoothing](VIDEO_AXIS_SMOOTHING.md). Reproduce the original
measurements at commit `dd4e978`; current reference code implements AX01.

The same saved Blur or Horizontal Blend setting changes the treatment of game
pixels when the output resolution changes. The two controls use different
spatial units. The source and numerical evidence below explain the difference;
they are not measurements from a monitor or the board.

## Processing order

The FPGA applies persistence, Blur, and Bloom to a native frame. Scanout then
enlarges it with nearest-neighbor pixel repetition. Color adjustment and
Horizontal Blend run on the enlarged picture, followed by scanlines and masks.
Menu and border pixels bypass those picture effects, and horizontal neighbors
do not cross their boundaries. CPU1 prepares color tables; it does not perform
the runtime frame blur.

The relevant paths are [frame filtering](../hdl/common/multitini_video_frame_filter.sv),
[scanout scaling](../hdl/common/multitini_video_frame_reader.sv), and
[output effects](../hdl/common/multitini_video_effects.sv). Composition and the
effect-enable boundary are in [video output](../hdl/board/multitini_video_test.sv).

## Mode dependence

[Display geometry](../hdl/common/multitini_display_geometry.sv) selects these
native-to-output scales. They are uniform on both axes.

| Core | 1920x1080 | 1024x768 | 1200x800 | 1280x1024 | 1360x768 | 1680x1050 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| C64 PAL | 4 | 2 | 2 | 3 | 2 | 3 |
| Game Boy / Color | 6 | 5 | 5 | 6 | 5 | 6 |
| Vectrex | 1.5 | 1 | 1 | 1 | 1 | 1 |
| Genesis | 4 | 3 | 3 | 3 | 3 | 4 |

Blur uses fixed native kernels. Light applies `[1,2,1]/4` horizontally.
Medium applies it horizontally and vertically. Strong uses `[1,2,2,2,1]/8`
on both axes. Each pass blends the filtered value with its center using
`alpha = min(256, round(384 * level / scale)) / 256`, where the levels are
1, 2, and 3. The actual RTL and C reference agree on these coefficients:

| Scale | Light alpha, /256 | Medium alpha, /256 | Strong alpha, /256 |
| ---: | ---: | ---: | ---: |
| 1 or 1.5 | 256 | 256 | 256 |
| 2 | 192 | 256 | 256 |
| 3 | 128 | 256 | 256 |
| 4 | 96 | 192 | 256 |
| 5 | 77 | 154 | 230 |
| 6 | 64 | 128 | 192 |

Reducing the scale therefore increases the mixing of neighboring native
pixels. For C64 Light, each neighbor contributes 9.375% at 4x but 18.75% at
2x, before rounding. This loses more source detail at the lower resolution.
It does not hold the output-pixel spread constant either: ignoring rounding,
the added horizontal variance of Light is `alpha * scale^2 / 2`. Before alpha
saturates, that grows with scale. The filter's comment promising the same
output-pixel radius is inaccurate.

Horizontal Blend instead always reads the preceding and following **output**
pixels. Soft uses `[1,6,1]/8`, Strong uses `[1,2,1]/4`, and Sharp uses
`[-1,6,-1]/4` with clipping. Its coefficients and one-output-pixel reach do
not change with the scale; the scale input in this stage controls scanlines.
One blended output pixel occupies half a 2x source pixel but only a quarter
of a 4x source pixel. Consequently this control also affects source detail
more at lower scales. Enabling both controls compounds their effects.

Bloom keeps fixed native kernels without Blur's scale compensation, so its
halo spans different numbers of output pixels. Genesis H32 is first expanded
from 256 to the common 320-pixel surface; these filter units refer to that
normalized surface, not directly to original H32 pixels.

No stale mode-coefficient path was found. The frame pipeline drains before a
mode change. The filter takes its scale from the active mode in the control
clock domain and latches the coefficient at the start of each frame job.

## Numerical check

The [C measurement probe](../tools/video_filter_probe.c) called the production
[frame reference](../firmware/common/video_frame.c) and
[spatial reference](../firmware/common/video_effects.c) for 112 combinations:
seven scales, four Blur values, and four Horizontal Blend values. Input was a
64x16 RGB565 image with alternating one-native-pixel-wide full-green and black
vertical stripes. Other effects were disabled. Results below show remaining
contrast relative to Off; lower values mean more lost detail. Horizontal
Blend averages the displayed pixels belonging to each source stripe. The
middle half of the row excludes image boundaries. Each column isolates its
named effect.

| Scale | Blur Light | Blur Medium | Horizontal Blend Strong |
| ---: | ---: | ---: | ---: |
| 1 | 0% | 0% | 0% |
| 1.5 | 0% | 0% | 25% |
| 2 | 25% | 0% | 49% |
| 3 | 49% | 0% | 66% |
| 4 | 62% | 25% | 75% |
| 5 | 71% | 40% | 80% |
| 6 | 75% | 49% | 83% |

These percentages are rounded from actual RGB565 reference results, not just
ideal kernel arithmetic. The contrast test is deliberately demanding; it
does not imply that all game detail disappears when a stripe result is zero.

Reproduce the measurements from a fresh checkout with a C compiler. The probe
is source-controlled; its executable and CSV stay in the ignored build folder:

```sh
mkdir -p build/blur-software-study
gcc -std=c11 -O2 -Wall -Wextra -Werror -Ifirmware/include \
  tools/video_filter_probe.c firmware/common/video_frame.c \
  firmware/common/video_effects.c -lm -o build/blur-software-study/measure
build/blur-software-study/measure > build/blur-software-study/results.csv
```

The frame, video-effects, and screenshot host tests passed during this study.
Existing dynamic filter validation covers seven scales and three geometries,
80 jobs each. All source hashes in that validation record and the dynamic
pipeline validation record still match the current sources. Those simulations
establish arithmetic and ownership agreement, not consistent visual softness
across modes. The current host visibility acceptance thresholds cover 4x and
6x; cross-mode visual acceptance remains untested on the board.

## Recommendation and FPGA impact

Use the game image as the reference for consistent softness across output
modes. Preserve the accepted 1080p per-core native Blur response at every
mode, instead of recalculating its strength from the selected output scale.
For Horizontal Blend, derive its radius and weights from the source-image
scale, anchored to its current 1080p appearance. Fractional weights will be
needed where the corresponding radius is less than one output pixel. Keep
Blur and Horizontal Blend distinct and explain their roles in the UI.

Do not simply restore inverse-square Blur compensation. The owner found
VID4 too faint, and the VID5 inverse-linear correction was accepted at 1080p.
[The recorded history](CPU1_VIDEO_EFFECTS.md) explicitly separates matching
numerical variance from useful visible blur in RGB565.

Implementing this recommendation requires FPGA core updates: the coefficients
and horizontal neighbor spacing are currently defined in HDL. Firmware alone
cannot change that behavior. The C references and screenshot reconstruction
must follow any revised output filter. Preserve 1080p golden results, add
source-detail and edge-response checks across all six modes, then run timing,
uninterrupted-frame checks, and board comparisons. This study does not apply
that change or rebuild any FPGA image.
