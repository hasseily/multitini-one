# Amiga active video geometry

## Capture origin

The pinned native Agnus/Denise pipeline places a standard 640-pixel HiRes
picture at horizontal samples 206 through 845, measured from the falling
HSYNC edge by the adapter's 14 MHz sample counter. Standard DIW registers
are 2C81/2CC1; the native window is H129..448 and V44..299. The measured RGB
coordinates include the real bitplane DMA, shifter and palette pipeline.

Build 4D000802 cropped horizontal samples 112..831 into a 720-pixel canvas.
That placed standard Workbench at x94..733 and clipped its last 14 pixels.
The board screenshots reproduced those coordinates. HDMI centering and
ClassicWB preferences were not the cause.

The revised crop starts at sample 206 and retains all 640 active samples.
It removes the asymmetric border and aligns the standard picture's edges
with the shared viewport and Multitini borders.

## PAL height and scaling

Standard noninterlaced PAL Workbench is 640 by 256. The adapter duplicates
each field row, producing a 640 by 512 capture with a 640-pixel stride
(1280 bytes). A 640 by 480 crop would discard 32 doubled rows. The former
720 by 576 overscan canvas required 1152 rows at 2x, exceeding 1080p.

The standard 640 by 512 canvas permits Auto 2x at 1920 by 1080: a 1280 by
1024 image centered at x320, y28. Auto also selects 2x for 1280 by 1024 and
1680 by 1050, and 1x for the three shorter output modes. Existing manual
1x through 4x behavior remains available. ARM firmware 4D000803 already
derives these dimensions from the FPGA frame geometry; no firmware change
or flash installation is required.

Native V44..299 corresponds to adapter lines 41..296 in a long PAL field.
In a short interlace field, VSYNC begins halfway through a line and the
same picture occupies adapter lines 42..297. The adapter latches native
field1 at VSYNC and adjusts the first row by one for short fields. It
then captures exactly 256 rows and bob doubles them to 512 rows.

Full PAL overscan outside the standard active area is cropped. The retained
NTSC test path centers 480 rows within the 512-row buffer; the product
profile still does not expose or qualify NTSC.

## Qualification

Build 4D000802 remains the rollback image. Corrective build 4D000804 has a
fresh source capture and passes routed setup (+0.259 ns), hold (+0.041 ns)
and native clock-enable timing (+13.331 ns), plus the existing clock, CDC,
DRC and coverage gates. No timing exception was introduced for this video
correction.

The passing normalizer regression checks every pixel, actual PAL line
lengths, both interlace phases, field latching, adjacent fields, final-row
drain, stalls and occupied-bank recovery. A calibrated old-origin mutation
fails. A separate native-content test verifies the complete standard
picture using the imported chipset, including all four corners and both
interlace field phases. Removing the field correction also fails.

The unchanged ARM firmware passes geometry and PNG tests for both the old
720 by 576 and new 640 by 512 frames, all output modes and manual scales.
On 2026-10-31 the card loaded 4D000804 with unchanged ARM 4D000803. The
source geometry reported 640 by 512 with a 1280-byte stride, and captured,
consumed and processed frame counts advanced without reported video errors.
New source/output screenshots show the pointer at the top-left limit, the
full maximized desktop and its right scrollbar, and the expected 1280 by
1024 viewport at (320,28). The user confirmed the corrected alignment.
The clear output screenshot is retained as
`build/amiga-ecs/alignment/after-output-clear.png`, SHA-256
`6f234c0fea484d8f7cf9114bf709cc449293fa742b48c0cb9d9fb2e231e4bc17`.
This accepts the observed standard PAL desktop alignment; hardware
interlace, overscan applications and all output modes remain separate tests.
