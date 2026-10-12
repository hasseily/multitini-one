# Video size and weaker Blur: PHO3 / -phos3

PHO3 adds per-core **Video size**: Auto, 1x, 2x, 3x and 4x. Auto preserves
the current Multitini geometry for each core and output resolution. Manual
values multiply native source pixels in both axes, center the complete image,
and clip pixels outside the physical display. They never change the HDMI mode
or select different output-clock divider values.

| Core at 1920x1080 | Native image | Auto image |
|---|---|---|
| C64 PAL | 382x270 | 1528x1080, 4x |
| Game Boy / Color | 160x144 | 960x864, 6x |
| Vectrex / Vectrex32 | 540x720 | 810x1080, 3:2 |
| Genesis / Mega Drive | 320x240 | 1280x960, 4x |

Auto follows the existing fit table rather than a new generic maximum-fit rule.
Manual origins are `floor((output - scaled image) / 2)` on each axis, including
negative results. For example, Vectrex at 4x occupies 2160x2880; at 1080p its
origin is (-120,-900), so only the centered portion is visible. Returning to
Auto restores the complete image.

Video size defaults to Auto, including migrated settings, and is saved
independently for each core. Output resolution remains global. Resolution
changes preserve the active multiplier; core changes restore that core's
preference. Settings are saved only after geometry and the first output frame
acknowledge the change. A settings write failure restores the previous choice.
Older cores retain Auto and require an updated package for manual size.

The menu remains at the full output resolution. Decorative borders follow the
complete image's edges: off-screen edges disappear instead of creating a border
at the clipped screen edge. Padding remains measured in physical pixels.
Scanlines, masks and Dot bleed retain their full-image phase across clipping.
Keyboard and controller mappings are unchanged. Full-output screenshots include
the display, menu and borders; video-only screenshots contain the visible image
intersection, without UI or off-screen pixels.

The direct machine bridges and native DDR capture stay at today's Auto geometry.
This preserves every source pixel even when the selected output is clipped.
The native DDR reader applies the independent manual scale and source offset.
Manual output is black during reader startup or a cache miss; it does not show
an incorrectly sized Auto fallback. Auto retains its existing fallback behavior.

Mode and multiplier cross one acknowledged seven-bit mailbox while scanout is
held in reset. Complete output geometry and crop source coordinates/phases are
registered before release. The existing drain/reset/relock transaction is used
for size changes with the selected mode's unchanged divider values. Source
dimensions, frame-slot addresses, native filtering and the nominal 60 Hz cadence
remain unchanged; the reader adds crop and leading-edge cache handling.

PHO3 retains the weaker Blur calibration described in
[PHO2](PHOSPHOR_PHOS2.md): Light, Medium and Strong mix 12.5%, 25% and 50% of
the normalized rounded kernels with the original pixel. Constant colors remain
exact. Glow retains its independent fixed 3x3 halo and floor arithmetic; Dot
bleed and persistence retain their previous integer behavior.

ABI 0.21 and build ID `0x50484F33` identify this release. `DISPLAY_REQUEST`
and `DISPLAY_STATUS` carry mode in bits 3:0 and multiplier in bits 6:4, with
0 meaning Auto. Request bits 31:7 must be zero. The multiplier feature is
version-gated separately from the weaker Blur semantics introduced in ABI 0.20.

Build fresh supervisor, C64 PAL, PSRAM GBC, Vectrex and Genesis NTSC/PAL images:

```text
python scripts/build_phos1.py --build-id 50484F33 --output build/phos3/hardware
```

Build the ARM applications using the new supervisor XSA and
`MULTITINI_VITIS_WORKSPACE=build/phos3/vitis`, then package:

```text
python scripts/package_phos1.py --build-id 50484F33 --abi-minor 21 --suffix phos3 --hardware build/phos3/hardware --workspace build/phos3/vitis
```

The output is `build/firmware-phos3.zip`. `FIRMWARE-phos3.BIN` is identical to
`sd/FIRMWARE.BIN`, the updater name. One rebuilt image per existing variant
supports all five size choices. Matching source, captures, build settings,
timing reports and firmware/core payload audits accompany the bundle. Prior
PHO1 artifacts and superseded PHO2 build evidence remain separate.

Qualification includes all mode/multiplier geometries, signed border clipping,
native-reader source offsets and cache warm-up, bounded stalls, scanline/mask
phase, Dot bleed at 1x through 4x, full-screen UI isolation, settings migration
and rollback, atomic mailbox/reset settling and unchanged mode-divider values.
Every full image must pass the existing setup, hold, pulse-width, routing, CDC,
bus-skew and timing-coverage gates. These checks do not establish board
throughput or physical display acceptance. No board is flashed by the build or
package tools.
