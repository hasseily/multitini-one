# Weaker Blur: PHO2 / -phos2

The owner reported PHO1 Blur too strong, including Light. PHO2 reduces every
level while retaining the Appletini spatial footprints. Effects default Off.

| Level | Kernel footprint | Blur contribution | Original pixel |
|---|---|---:|---:|
| Light | Horizontal `[1,2,1]/4` | 12.5% | 87.5% |
| Medium | Horizontal and vertical `[1,2,1]/4` | 25% | 75% |
| Strong | Horizontal `[1,2,2,2,1]/8`, vertical `[1,2,1]/4` | 50% | 50% |

Each pass sums weighted RGB888 taps and rounds once. The final mix rounds once
per channel. This preserves all constant RGB888/RGB565 colors exactly rather
than retaining PHO1's per-tap darkening. Edges clamp; horizontal kernels still
bypass rows narrower than four pixels. The blend uses the complete kernel
result, so Medium and Strong are mixed once after both axes.

Glow retains its independent fixed 3x3 halo, existing floor arithmetic and
1/8, 1/4, 1/2 saturating addition. Dot bleed and persistence retain PHO1's
integer behavior. Blur never enters persistence history. All effects remain
defined in native pixels, independent of display scale. See
[PHO1](PHOSPHOR_PHOS1.md) for the unchanged pipeline and Dot bleed contract.

The C/NEON reference fuses the mix into its existing horizontal/final vertical
pass. The FPGA uses an existing pipeline stage and fixed shifts/adds. There is
no new frame traversal, DDR transaction, line RAM, clock, reset, crossing or
pipeline stage. Full-frame deadline and routed timing checks qualify builds;
they do not measure board throughput or live display behavior.

ABI 0.20 and build ID `0x50484F32` identify the calibrated behavior. Settings
fields, addresses, level names and saved values are unchanged. New firmware
requires matching ABI 0.20 cores for effects and filtered screenshots, avoiding
an old PHO1 core being interpreted by the new software reference.

Build fresh images with:

```text
python scripts/build_phos1.py --build-id 50484F32 --output build/phos2/hardware
```

The matrix is supervisor, C64 PAL, PSRAM GBC, Vectrex, and Genesis NTSC/PAL.
Build the ARM apps with `scripts/create_vitis_workspace.py` using the new
supervisor XSA and `MULTITINI_VITIS_WORKSPACE=build/phos2/vitis`. Package with:

```text
python scripts/package_phos1.py --build-id 50484F32 --abi-minor 20 --suffix phos2 --hardware build/phos2/hardware --workspace build/phos2/vitis
```

The output is `build/firmware-phos2.zip`. `FIRMWARE-phos2.BIN` is identical to
`sd/FIRMWARE.BIN`, the updater name. The ZIP supplies all matching core variants,
source, provenance, build settings, timing reports and payload audits. PHO1
artifacts remain available. No board is flashed by these tools.

Regression gates cover all 65,536 RGB565 solid fills, exact white-impulse and
step-edge values, distinct strengths, independent Glow, unchanged history,
clamped edges, small geometries and all display scales. Hardware acceptance
remains separate from software, simulation and build evidence.
