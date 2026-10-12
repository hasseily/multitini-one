# Video settings

Open the menu with F12 and select **Video**. Alongside the output controls,
the active core has these settings:

| Setting | Choices |
| --- | --- |
| Border | Off / On |
| Type | Single line, Double line, Corner brackets, Dashed, Soft glow |
| Color | The classic 16-color palette, with reduced brightness and saturation |
| Padding | 1–20 output pixels between the game image and its border |
| Video size | Auto / 1x / 2x / 3x / 4x, saved for this core |

Left/Right adjusts the selected setting. Enter toggles Border or cycles the
other choices. Padding stops at 1 and 20 with the arrow keys; Enter cycles from
20 back to 1. Each core saves its own choices. Defaults are border Off,
Single line, Light gray, and 6 pixels.
A failed settings write leaves the previous choice
active and reports the storage error.

The border sits outside the complete machine image and below the menu. It is
hidden on the standby screen. Borders never resize the game window. Parts
outside the display are clipped: Vectrex has visible side
borders while the horizontal borders lie off-screen at 1080p. Padding uses
physical output pixels, not emulated machine pixels. The C64's own VIC border is
part of its machine image and remains intact.

At the default 1920×1080 resolution, the original centered game windows are
preserved: C64 PAL is 1528×1080, Game Boy / Color is 960×864, and Vectrex /
Vectrex32 is 810×1080 when Video size is Auto. Genesis Auto is 1280×960.
Video size can select a centered native-pixel integer multiplier; oversized
images clip at the display edges. The current package
supplies one C64 variant, PAL; display output remains nominal 60 Hz.

Firmware and core images with shell ABI 0.12 provide these controls. Older ABI
0.11 images still load; the new Video controls explain that an updated core
image is needed. Update all packaged variants to use the feature on every core.

## Display resolution

ABI 0.16 provides **Display resolution** in Video. Left/Right or Enter
cycles these modes. Mode 0 is the power-on default. Each mode centers the
complete machine image without cropping when Video size is Auto:

| Mode | Output | Pixel clock | C64 Auto | GBC Auto | Vectrex Auto | Genesis Auto |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 1920×1080 | 148.5 MHz | 4× | 6× | 3:2 | 4× |
| 1 | 1024×768 | 65 MHz | 2× | 5× | 1× | 3× |
| 2 | 1200×800 | 67.5 MHz | 2× | 5× | 1× | 3× |
| 3 | 1280×1024 | 108 MHz | 3× | 6× | 1× | 3× |
| 4 | 1360×768 | 85.5 MHz | 2× | 5× | 1× | 3× |
| 5 | 1680×1050 | 119 MHz | 3× | 6× | 1× | 4× |

All modes are about 60 Hz. Scanline spacing and Dot bleed follow the actual
scale. PHO3 retains the weaker Blur and fixed native-pixel Glow kernels; Blur
mixes 12.5%, 25% or 50% of its normalized kernel with the original pixel.
The native DDR images retain their original dimensions and addresses.
All 128×60 menu cells remain visible; smaller outputs use the existing 7×8 font
at 1×, centered in each cell. Border padding remains measured in output pixels.

Resolution is one global setting, restored after boot and core changes. It is
saved only after the clock and first frame acknowledge the change. A failed
settings write restores the previous setting and attempts to restore its mode.
Older core packages keep 1080p and retain the saved preference for newer cores.

ABI 0.21 adds **Video size** after Display resolution. Auto retains the table
above. Manual 1x–4x scales the native image independently of the output mode.
Each axis is centered with `floor((output - scaled image) / 2)`; pixels outside
the screen are clipped. Each core saves its own choice, with Auto as the default
for new and migrated profiles. Resolution changes preserve the multiplier, and
core changes restore that core's choice. A failed settings save restores the
previous choice. Older cores keep Auto until their package is updated.

The menu keeps the complete physical display. Borders follow the full image
edges, so clipped edges remain off-screen. Video-only screenshots include only
the visible image intersection; full-output screenshots include the complete
display with UI and borders.

The clock transaction cannot determine whether a monitor accepts a mode; there
is no EDID negotiation. Serial command `:display 0` restores and saves 1080p.
New modes and live switching still require board and monitor qualification.

The shared register ABI is:

| Register | Meaning |
| --- | --- |
| `DISPLAY_REQUEST`, `0x1D0` | Full-word write: mode 0–5 in bits 3:0; ABI 0.21 multiplier 0–4 in bits 6:4, with 0 Auto; reserved bits zero; invalid or busy writes set an error |
| `DISPLAY_STATUS`, `0x1D4` | Active mode in bits 3:0; ABI 0.21 multiplier in bits 6:4; busy in bit 8; error in bit 9; video held in bit 10 |
| `DISPLAY_MODES`, `0x1D8` | Supported-mode mask `0x3F` |

The control clock stays fixed. A private AXI master drains capture, scanout,
and filtering before reprogramming the pixel MMCM. Mode and multiplier cross as
one acknowledged seven-bit mailbox while scanout is held in reset; the complete
geometry and crop source coordinates/phases are registered before release.
Size-only changes use the selected mode's unchanged divider values.
Mailbox clocks and reset ownership remain live while the pixel datapath resets. Blur/Glow kernels
remain fixed in source pixels; Dot bleed follows the new output geometry.

Direct machine bridges and native DDR capture retain the complete Auto raster.
The reader independently scales and clips that native image. Manual output is
black during reader startup or cache misses, preserving UI without showing a
wrong-sized Auto fallback. Auto retains its existing fallback. Native frame
dimensions, slot addresses and ordered filtering remain unchanged.

Clock failure attempts the default dividers once, preserving an error flag.
Repeated failure waits for a new explicit request. A drain timeout keeps the
old pixel clock running until outstanding owners finish; it never resets a
live owner to make the transaction appear complete.

AX01 core packages use ABI 0.17; GL01 uses 0.18; PHO1 uses 0.19; PHO2 uses 0.20.
PHO3 uses ABI 0.21 and includes the weaker Blur calibration and Video size.
Historical release scripts retain their
original ABI and build IDs. Package newly built cores with
`scripts/make_core_package.py`, whose default now matches ABI 0.21.

## Implementation contract

The shared shell register `BORDER_CONFIG` at `0x0C0` carries one atomic setting:

| Bits | Field |
| --- | --- |
| 0 | Enabled |
| 3:1 | Style, 0–4 in the order above |
| 7:4 | Color, 0–15 |
| 12:8 | Padding, 1–20 |
| 31:13 | Reserved, zero |

The reset value is `0x00000670`. Control-to-video transfer preserves the whole
setting and applies changes at vertical blank.
No game clocks, source timing, or framebuffer contents depend on these settings.

Vectrex ABI 0.13 also adds **Overlay**, **Overlay brightness** and **Beam
brightness** to this page. Artwork brightness ranges from 25 to 100 percent;
beam gain ranges from 1x to 4x with saturation. Defaults are On, 50 percent,
and 2x. These controls preserve the same game window and border placement.
See [Vectrex overlays](VECTREX_OVERLAYS.md) for the file format and settings.

The **Effects** page provides per-core color adjustments, scanlines, masks,
Dot bleed, persistence, Blur, and Glow. Left/Right adjusts the selected control;
effects default off. PHO3 retains weaker calibrated Blur, supported by ABI 0.20
and newer cores. Light, Medium and Strong mix 12.5%, 25% and 50% of the
normalized blur with the original pixel. Persistence, Blur, and Glow run in the FPGA at native
resolution. Dot bleed shapes enlarged pixels using adjacent cached source dots.
Its ordered frame
pipeline follows the 60 Hz output cadence; CPU1 prepares color settings.
See [video size and weaker Blur](PHOSPHOR_PHOS3.md) for the current processing order
and [CPU1 and FPGA video effects](CPU1_VIDEO_EFFECTS.md) for historical
implementation and acceptance records.

## Acceptance

Directed checks cover menu navigation, settings migration and failure recovery,
pixel placement, all styles and colors, padding endpoints, and the disabled
path. Pixel captures are simulation or software previews, not board captures.
Vendor timing and physical display checks are recorded with each test package.

Focused checks can be rerun with `scripts/test_video_settings.py`,
`scripts/test_video_border.py`, and `scripts/test_video_settings_runtime.py`.
The last script executes the actual board handlers with mocked MMIO and storage;
it checks version guards, save ordering, live rollback and core-switch restore.

`make hdl-display-test` checks all six timing/polarity presets, compact text,
direct C64/GBC/Vectrex scanout, clock faults, and native DDR filter/reader/drain
behavior at all supported scale ratios. The filter compares against the C
reference under backpressure. `tests/test_display_runtime.py` runs the actual
board display transaction with mocked clocks and storage, including rollback,
timeouts, old-core guards and effects restart.

`scripts/test_signed_video_geometry.py` checks clipped origins, full-image effect
phase, borders and UI/DVI alignment. `scripts/test_display_mode_settling.py`
checks all six modes and five multiplier choices for each machine geometry,
including same-mode size changes, independent clock-bus stalls, stopped pixel
clocks, registered crop sidebands, Auto capture and unchanged divider values.
