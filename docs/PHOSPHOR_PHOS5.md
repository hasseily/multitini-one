# PHO5 / -phos5: display corrections and runtime diagnostics

PHO5 uses build ID `0x50484F35` and ABI 0.21. It retains PHO4's phosphor
effects, weaker Blur, integer Video size, complete menus and screenshots.

Video size changes within the current resolution drain the DDR owners, hold
pixel reset, commit geometry and settle without reprogramming the pixel clock.
The previous controller stalled if an identical clock reload remained locked.
Resolution changes and failure recovery retain complete clock programming.

Compact splash text is centered using its visible ink bounds. MULTITINI, ONE
and the F12 prompt are individually centered horizontally; their complete
three-line block is centered vertically. The panel and motif remain centered.
The compact canvas also advances one pixel to compensate for the standby
color register's latency relative to DE. Tests measure the transmitted raster
with a counter derived from outgoing DE, including the enabled effects path.
The 1080p pixels remain unchanged. This corrects padded-cell, vertical-block
and pipeline offsets that the previous panel-centering tests did not detect.

The original 5x7 font is encoded with eight padded columns per row. Glyph
lookup uses concatenated row/column bits, removing runtime multiply/subtract
arithmetic from the standby timing path without changing the font or pipeline.
Canvas and glyph origins are precomputed in pixel-clock registers. They update
while pixel reset is held, and settle before the existing frame-start release.
Pixel rendering uses direct deltas from these origins, avoiding successive
geometry and glyph-origin arithmetic on the critical output path. The output
color pipeline, clock and reset crossings remain unchanged.

The firmware retains DIA4's phase-specific Genesis error, safe status snapshot,
read-only video diagnostics and explicit effects-Off status. Saved settings,
media and effects defaults are unchanged. Genesis loading behavior is unchanged
unless a separately documented cause and fix is established. Passing modeled
transfers does not establish physical PSRAM operation or owner acceptance.

Build all six images from fresh captured sources at the nominal constraints:

```text
python scripts/build_phos1.py --build-id 50484F35 --output build/phos5/hardware
python scripts/package_phos1.py --build-id 50484F35 --abi-minor 21 --suffix phos5 --hardware build/phos5/hardware --workspace build/phos5/vitis
```

Use the new supervisor XSA for the ARM workspace. Retain normal source,
timing/hold/pulse-width/coverage/routing/CDC/bus-skew gates and independent
payload audits for every selected image. The PHO4 and DIA4 bundles remain
separate, hash-pinned artifacts. No flash or physical acceptance is implied.
