# Appletini phosphor effects: PHO1 / -phos1

PHO1 replaces independent horizontal/vertical smoothing with **Dot bleed**,
**Blur**, and **Glow**, pinned to Appletini ONE F1.2.5 commit
`886ea50e7c5a98c28c7e37b5eff7548a8ea416ce`. Each control has Off, Light,
Medium, and Strong. Effects default Off. Persistence, color adjustments,
scanlines, masks, menu composition, and decorative borders retain their roles.

## Pixel contract

Persistence produces RGB888 history before spatial filtering. Blur and Glow
never feed back into that history. They operate in native source pixels.

- Blur Light uses horizontal `[1,2,1]/4` only. Medium adds the same vertical
  tent. Strong uses horizontal `[1,2,2,2,1]/8` and the three-row vertical tent.
  Appletini shifts each weighted tap separately before adding; this small
  truncation is preserved, including its slight darkening of some flat fields.
  Horizontal blur bypasses rows narrower than four pixels, matching Appletini.
- Glow uses a separate `[1,2,1]/4` horizontal and vertical halo from the
  persisted image. Each pass floors the weighted sum once. Light, Medium and
  Strong add 1/8, 1/4 and 1/2 of this same halo with per-channel saturation.
- Dot bleed shapes the processed pixels into two horizontal half-dot samples.
  Light uses `(3*center + near)/4`, Medium `(20*center + 12*near)/32`, and
  Strong `(17*center + 14*near + far)/32`, rounding to nearest RGB888 before
  RGB565 packing. Near is the neighboring dot toward that half; far is the
  dot on the opposite side. Row ends repeat their endpoint pixel.

At 2:1 Dot bleed uses Appletini's two samples exactly. Larger and fractional
enlargements select the half-dot at each output sample's center. At 1:1 it
averages the two already-quantized RGB565 samples, as Appletini does for its
smaller output. Multitini applies these spot weights per RGB channel to its
machine image; it has no Apple monochrome-frame metadata. Its existing
RGB565 frame storage remains the boundary before Dot bleed. Color/LUT,
scanlines, and masks follow this stage. Opaque menus and borders bypass it.
Screenshots reconstruct the same integer operations and edge clamps.

## Interface and build

ABI 0.19 identifies these semantics. The saved field sizes and register
addresses remain unchanged: `softness` now means Dot bleed, `blur` means
Appletini Blur, and `bloom` means Glow. New firmware enables effects and
filtered screenshots only with ABI 0.19 packages and hardware. Older compatible
cores can still load. Their saved levels are preserved rather than silently
changing the settings format.

The native filter retains four line RAMs and nine pipeline clocks. Dot bleed
uses both adjacent pixels from the existing scanout row cache, adding no DDR
transaction. It replaces the previous scaler blend in its existing two stages.
There are no new clocks, resets, clock crossings, ownership rings, or frame
transfers. The autonomous scheduler remains responsible for ordered captures
and the nominal 60 Hz output cadence; CPU1 prepares color-control tables.

Build the supervisor and all five machine images with
`python scripts/build_phos1.py`. This uses fresh project directories and the
normal Vivado timing, CDC, routing, bus-skew, and timing-coverage gates.
The matrix is supervisor, C64 PAL, PSRAM GBC, Vectrex, Genesis NTSC, and Genesis
PAL. C64 NTSC remains outside the release scope.

If a saved routed image misses timing, `scripts/repair_phos1_timing.py` takes
one explicit build directory, checks its captured source hashes, preserves
the prior reports/checkpoint, and reruns every normal export gate. Its default
is another routed AggressiveExplore pass. `--signoff-only` audits a repaired
checkpoint without another optimization. `--replicate-cell` names exact
registers for pre-route fanout replication and rerouting; this adds a temporary
0.100 ns machine setup margin and restores nominal uncertainty before signoff.
Clock rates, RTL, and final timing constraints remain unchanged. Exact repair
commands, generated Tcl, checkpoint hashes, and reports are retained under
each image's `reports/repair` directory.

Build the ARM application using `scripts/create_vitis_workspace.py` and the
explicit new supervisor XSA. Audit and package with
`scripts/package_phos1.py`; every image and workspace is explicitly selected.
`FIRMWARE-phos1.BIN` is copied to `sd/FIRMWARE.BIN`, the updater's required name.
The delivery ZIP contains all SD core packages, exact sources, build settings,
reports, and independently checked firmware/core payloads. It includes no ROMs
or games and does not flash the board.
Each package uses `0:/multitini/cores/<id>/media` for user content, including
Genesis; the older Genesis plan's content-directory example is superseded
for this bundle.

## Validation and performance

The independent host oracle imports the pinned Appletini spatial functions.
It checks Blur/Glow combinations, arbitrary RGB888 history, small geometries,
edge clamps, all levels, and scale independence. Dot bleed tests check visible
gap edges/centers, every solid RGB565 color, and the 1:1 packed average.
C/HDL comparisons cover enlarged pixels, nonlinear color transforms, masks,
scanlines, opaque UI, resets, cached word boundaries, and all seven scales.

Full-frame engine simulations take exactly the same clocks as GL01 under the
same bounded DDR stalls (up to 11 clocks). GBC 160x144 takes 43,449 clocks,
C64 382x270 takes 189,706, and Vectrex 540x720 takes 710,169. At 133 MHz these
are 0.327, 1.426, and 5.340 ms. The largest tested 640x720 frame takes 843,733
clocks (6.344 ms), against a 2,216,666-clock frame budget. These are simulation
results; they do not measure contention or throughput on the board.

Controlled Vivado 2025.2 synthesis uses the same tops, generics and FPGA part
for the captured GL01 sources and PHO1:

| Block | LUT before/after | FF before/after | BRAM before/after | DSP before/after |
|---|---:|---:|---:|---:|
| C64 native engine | 2726/2211 | 2256/1907 | 2/2 | 3/3 |
| GBC native engine | 2734/2226 | 2254/1905 | 2/2 | 3/3 |
| Vectrex native engine | 2717/2247 | 2255/1909 | 4/4 | 3/3 |
| Pixel effects | 1149/1584 | 1662/1706 | 1.5/1.5 | 15/9 |

Dot bleed costs 435 LUTs and 44 registers in the pixel-effects block while
removing six DSP multipliers. The simpler native kernels save 470-515 LUTs and
346-349 registers. These block deltas exclude the framebuffer reader and
whole-core placement; final image utilization is reported separately.

Retain logs, source hashes, resource reports and full-image timing gates under
`build/phos1`. `scripts/report_phos1_performance.py` verifies the completed
matrix and writes `build/phos1/performance/PERFORMANCE.md` and `performance.json`.
Build/simulation evidence does not establish measured board throughput or
gameplay acceptance.
