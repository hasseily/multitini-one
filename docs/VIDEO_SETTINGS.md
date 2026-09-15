# Video settings

Open the menu with F12 and select **Video**. Alongside the output controls,
the active core has these settings:

| Setting | Choices |
| --- | --- |
| Border | Off / On |
| Type | Single line, Double line, Corner brackets, Dashed, Soft glow |
| Color | The classic 16-color palette, with reduced brightness and saturation |
| Padding | 1–20 output pixels between the game image and its border |

Left/Right adjusts the selected setting. Enter toggles Border or cycles the
other choices. Padding stops at 1 and 20 with the arrow keys; Enter cycles from
20 back to 1. Each core saves its own choices. Defaults are border Off,
Single line, Light gray, and 6 pixels.
A failed settings write leaves the previous choice
active and reports the storage error.

The border sits outside the complete machine image and below the menu. It is
hidden on the standby screen. Borders never resize the game window. Parts
outside the display are clipped: Vectrex has visible side
borders while the horizontal borders lie off-screen. Padding uses physical pixels in
the 1920×1080 output, not emulated machine pixels. The C64's own VIC border is
part of its machine image and remains intact.

The original centered game windows are preserved: C64 PAL is 1528×1080,
C64 NTSC is 1616×1000, Game Boy / Color is 960×864, and Vectrex / Vectrex32
is 810×1080. There is no game-window size control.

Firmware and core images with shell ABI 0.12 provide these controls. Older ABI
0.11 images still load; the new Video controls explain that an updated core
image is needed. Update all packaged variants to use the feature on every core.

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

## Acceptance

Directed checks cover menu navigation, settings migration and failure recovery,
pixel placement, all styles and colors, padding endpoints, and the disabled
path. Pixel captures are simulation or software previews, not board captures.
Vendor timing and physical display checks are recorded with each test package.

Focused checks can be rerun with `scripts/test_video_settings.py`,
`scripts/test_video_border.py`, and `scripts/test_video_settings_runtime.py`.
The last script executes the actual board handlers with mocked MMIO and storage;
it checks version guards, save ordering, live rollback and core-switch restore.
