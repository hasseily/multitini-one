# Resolution-aware standby and complete menus: PHO4 / -phos4

PHO4 corrects two display bugs reported on the PHO3 package. It keeps the
weaker Blur and per-core Video size controls from PHO3, with ABI 0.21 and
build ID `0x50484F34`.

The existing standby artwork now follows the center of each selected output.
Compact modes retain their half-size glyphs and decoration. The 1080p artwork
retains its existing coordinates, colors, and scale. New splash designs are
separate proposals and are not part of this correction.

The menu retains its 128x60 text grid. Integer cell sizes previously left
uncovered strips at some resolutions: 1024x768 covered only 720 rows, for
example. Pixels in the remainder strips now inherit the background and
transparency of the nearest edge cell. Glyphs remain inside the complete grid,
without repeating or stretching into these strips. This covers the entire
display for the opaque config menu while preserving transparent text overlays.
Full-output screenshots use the same rule.

Text VRAM sizes, commits, clocks, resets, and data pipeline latency are
unchanged. No interface migration is needed. The shared display RTL requires
fresh supervisor, C64 PAL, PSRAM GBC, Vectrex and Genesis NTSC/PAL bitstreams.
Build and package with explicit PHO4 selections:

```text
python scripts/build_phos1.py --build-id 50484F34 --output build/phos4/hardware
python scripts/package_phos1.py --build-id 50484F34 --abi-minor 21 --suffix phos4 --hardware build/phos4/hardware --workspace build/phos4/vitis
```

Use the fresh supervisor XSA to build the ARM applications in the selected
Vitis workspace before packaging. Retain source captures, settings, reports,
and payload audits for every selected image. The PHO3 bundle remains intact.

Qualification checks the standby against screen-centered physical bounds in
all six modes, every active and blanking pixel of the text overlay in dense
and legacy layouts, opaque menu coverage to the final row and column, and
transparent edge-cell inheritance without repeated glyphs. Host screenshot
tests cover full rasters, mixed edge backgrounds and transparent corners.
Each FPGA image must pass the existing complete timing and CDC gates.
Simulation and build results remain separate from physical display acceptance.
