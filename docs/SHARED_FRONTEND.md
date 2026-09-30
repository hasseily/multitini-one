# Shared front end

Firmware `20260920` keeps the menu open across disk, cartridge,
core, and other configuration changes. It uses the compact text and automatic
menu pause FPGA images from `20260919`; all components use ABI 0.11. On
2026-09-14 the owner confirmed it was flashed and board tested. See
[Hardware results](HARDWARE_RESULTS.md) for the artifact and reported scope.
The `build/current` and root firmware aliases retain the earlier `20260917`
package; this source commit does not change those generated files.

## Look and navigation

The layout follows Appletini ONE's shared configuration UI: a dark panel,
narrow navigation column, persistent active-system header, aligned values,
marked selection, contextual help, and a fixed footer. The reference was
the clean sibling checkout at `391f73f271cb17746775239e68bbd15610b67101`,
particularly `ps_sources/frontend/config_menu_ui.h`, `config_menu_ui.c`,
and `config_menu_help.c`. No sibling files or new font assets were copied.

- Cores shows package names, selected variants, and the running variant.
- Media, Input, and Options contain rows supplied by the active core.
  GBC has cartridge and save rows without unused C64 drive entries.
- Browsers show 40 files at once, with 81-character filename fields, up from
  16 files and 41 characters. Long labels, names, and values are clipped
  independently. Browsers show
  the selected filename in their help area, sizes in KiB/MiB, the item
  position, and the core's media path.
- Tab and Shift+Tab move between sections. Each section remembers its
  selection. Escape returns from a browser to its originating item.
- Up/Down selects; Home/End jumps to the first/last item; Page Up/Down moves
  through lists. Left/Right selects core variants; Enter applies.
- C64 disk-program browsing retains Enter for native launch, Right for
  native LOAD only, and Left for the explicit direct-load shortcut.
- Informational and unavailable rows cannot dispatch actions. Their help
  explains the current control or requirement.
- Save/settings warnings remain visible across pages. Explicit command
  errors retain priority so recovery instructions are not hidden.

The overlay has **128x60 cells**, up from 80x36. The existing 7x8 font uses
2x scaling: glyphs are 14x16 pixels inside 15x18 cells. This reduces the
nominal horizontal gap from three pixels to one and the vertical gap from
six pixels to two at the default 1920x1080 resolution. ABI 0.16 also provides
five smaller [display modes](VIDEO_SETTINGS.md). They retain all 128x60 cells
with the same font at 1x scale, centered within smaller cells. There are 2.67
times as many text cells as the original overlay.

## Pause while configuring

The active machine pauses whenever the new menu is visibly open. The sidebar
shows **Core paused**. Closing the menu resumes the same machine state.
This is a pause, not a reset. Audio is muted for the same interval.

The common shell synchronizes committed menu visibility into the machine
clock domain. C64 uses its existing boundary-safe core pause; its drive CPU,
VIAs, GCR rotation, video capture, and pending RUN-key injection also pause.
GBC holds machine clock-enable phase and its cartridge RTC divider. Memory,
loader, and save controllers remain available so pending transfers and saves
can complete. The ARM continues disk/save polling while the machine is paused.

Only an explicit exit closes the menu: Escape from a main page, F12, or the
UART `o` toggle. Disk, cartridge, core, reset, and program-load actions preserve
its open state on success as well as failure. Successful file selections return
to their parent page; failed selections retain the browser and selected row.

Browsing and simple settings stay in the paused menu. Actions that require
machine progress temporarily hide the overlay and wait for its video-side
acknowledgment, without changing the user's requested menu state. The outer
menu service restores the menu and pause after the complete operation, including
nested reset/content work and UART commands. A one-second hide timeout cancels
the action. Keyboard capture releases held keys and blocks interleaved actions.

## ABI and compatibility

ABI 0.11 adds the dense-layout capability and mode bits to the existing
UI status/commit interface. Dense banks occupy `0x4000..0x7BFF` and
`0x8000..0xBBFF`, with 15,360 bytes each. Mode, bank, and visibility transfer
atomically through the existing vertical-blank mailbox.

The old bank aliases and 80x36 rendering remain available in the same two
expanded memories. Reset selects legacy mode. Older firmware retains its
previous menu/execution behavior, because it cannot close its overlay before
blocking native loads. New firmware always selects dense mode, which also
activates shared menu pause. It rejects packages older than ABI 0.11 before
quiescing the active core, checks the loaded shell capability, and never
writes dense frames to a legacy-only overlay. Install the matching firmware
and all core packages together.

## Adding a core's controls

The renderer consumes `mt_menu_item_t` descriptors: label, value, help,
enabled state, value color, action, and action argument. Media, Input,
and Options all use this contract. Actions identify a stable argument;
rearranging rows must not change their meaning.

`firmware/common/core_frontend.c` owns the C64 and GBC profiles, browser
content masks, and descriptor builders. `mt_core_frontend_apply()` turns
a runtime snapshot into a view. It performs no file or register I/O and
never mutates the running core. Unknown cores get neutral empty pages.

For another core:

1. Add its profile, content types, and descriptor builder in `core_frontend`.
2. Populate its service snapshot in the supervisor and connect its action
   handlers. UI metadata does not create a machine loader or media service.
3. Keep save/reset/switch barriers in the service layer. Rendering and
   navigation must never flush, load, reset, or write settings.
4. Add profile/action tests, including disabled rows and unknown IDs.
   No core-specific branch belongs in `menu.c`.

The catalog limit remains 16 cores. Descriptor lists are bounded by
`MT_MENU_MAX_ITEMS`; profile storage has explicit capacities. Strings must
outlive the rendered view. The package format is unchanged; its ABI requirement is now 0.11.

## Efficient presentation

`mt_ui_present_cached()` maintains one CPU copy per FPGA text bank. It
uploads only changed packed words to the inactive bank. An identical frame
needs no writes; changing visibility alone needs one commit. Rendering
still occurs only when the supervisor marks the menu dirty.

The presenter waits for both mailbox readiness and synchronized active
bank/visibility/density state. It never writes the active bank. The runtime
invalidates both caches after PL reconfiguration, including rollback loads.
File/CRC preflight failures preserve the intact overlay and pending commit.
Core reset alone does not reset the overlay.

Use a persistent `mt_ui_presenter_t`, initialize it once, and invalidate it
after any operation that resets PL or its text memory. The legacy stateless
presenter remains available.

## Reproducible previews

```text
python scripts/preview_menu.py --cc C:/msys64/ucrt64/bin/gcc.exe --output build/ui-preview-dense
python scripts/preview_menu.py --helper build/validation-host-current/menu_preview.exe --output build/ui-preview-dense
```

The helper calls production menu/profile code and exports its cells. The
renderer reads the exact HDL font, palette, and glyph placement.
`build/ui-preview-dense/index.html` contains nine scenarios: no-core startup,
C64 media/options, GBC controls/saves, a save failure, a 144-file browser,
a 16-core catalog, and a full 16-item settings page. All content names are synthetic fixtures. Source
and image hashes are recorded beside the renders. These are software
renders, not board captures.

## Candidate build

Use Vivado/Vitis 2025.2. Generate each project with
`scripts/create_project.tcl`, then run `scripts/build_and_export_xsa.tcl`.
Set `MULTITINI_BUILD_ID=20260919` and a separate `MULTITINI_VIVADO_DIR`
for every image:

| Image | Directory under build/ | Machine | Additional settings |
| --- | --- | --- | --- |
| Supervisor | vivado-ui-20260919-supervisor | SUPERVISOR | GBC PSRAM=0 |
| C64 PAL | vivado-ui-20260919-c64-pal-final | C64 | video standard=PAL, GBC PSRAM=0 |
| C64 NTSC | vivado-ui-20260919-c64-ntsc | C64 | video standard=NTSC, GBC PSRAM=0 |
| GBC | vivado-ui-20260919-gbc | GBC | GBC PSRAM=1 |

The variable names are `MULTITINI_MACHINE`, `MULTITINI_C64_VIDEO_STANDARD`,
and `MULTITINI_ENABLE_GBC_PSRAM`. Preserve the generated timing, CDC, route,
clock, utilization, and input-hash records for every image.

For firmware, set `MULTITINI_VITIS_WORKSPACE` to `build/v20` and
`MULTITINI_XSA` to
`build/vivado-ui-20260919-supervisor/multitini_one.xsa`. Leave
`MULTITINI_PLATFORM_XPFM` unset, then run
`vitis -s scripts/create_vitis_workspace.py` to create a fresh platform.

Create `build/firmware-ui-20260920`, then package explicit inputs with
`scripts/make_firmware_bin.bat` in its OUT, FSBL, BIT, APP argument order:

| Argument | Path |
| --- | --- |
| OUT | `build/firmware-ui-20260920/FIRMWARE.BIN` |
| FSBL | `build/v15/multitini_platform/export/multitini_platform/sw/boot/fsbl.elf` |
| BIT | `build/vivado-ui-20260919-supervisor/multitini_one.runs/impl_1/multitini_one_top.bit` |
| APP | `build/v20/diagnostic/build/diagnostic.elf` |

The existing FSBL is an explicit retained input; PS/DDR initialization is
unchanged. Package the newly built C64 PAL/NTSC and GBC images with
`scripts/make_core_package.py` and ABI 0.11. Firmware `20260920` reuses these
exact `20260919` images; it requires no FPGA rebuild. The local delivery package
is `build/multitini-ui-20260920.zip`; it contains no games, ROMs, settings, or saves.
Systems already running the `20260919` package need only the new `FIRMWARE.BIN`.

Validation records live in `build/validation-ui-dense`, `build/ui-preview-dense`,
and `build/firmware-ui-20260919` for the original dense interface; the menu
persistence fix uses `build/validation-menu-20260920` and
`build/firmware-ui-20260920`. Host checks cover the full grid, navigation,
clipping, bank histories, density acknowledgments, and rejected legacy layouts.
Full-raster HDL tests exercise both layouts/banks and their address boundaries.
Pause tests cover GBC phases, C64 drive CPU/VIA state, interrupted RUN
injection, and PAL/NTSC time-of-day phase retention. The production menu-close
helper also passes delayed-acknowledgment and timeout tests.

The original `20260919` candidate passes 29 native host tests, the same 29 tests with address and
undefined-behavior sanitizers, and 141 Python tests (three skipped). The full
HDL suite, dense/legacy raster checks, generated-register check, and source
policy pass. Vitis builds all firmware sources without new compiler warnings.

Firmware `20260920` passes 29 native host tests, the same sanitizer checks,
and 147 Python tests (three skipped). Six new runtime scenarios compile the
production menu, presenter, keyboard filter, and dispatcher with hardware
service mocks. They cover successful changes, errors/rollback, browser context,
nested hides, UART operations, delayed commits, cancellation, and explicit exit
keys. The firmware build and payload/source audits pass with no new compiler
warnings; the existing FPGA and core package bytes are reused unchanged.

All four routed FPGA images pass timing, CDC, routing, bus-skew, and timing
coverage gates. These are build results, not board measurements:

| Image | Setup slack | Hold slack | Pulse-width slack |
| --- | --- | --- | --- |
| Supervisor | +0.200 ns | +0.065 ns | +1.833 ns |
| C64 PAL | +0.277 ns | +0.036 ns | +0.265 ns |
| C64 NTSC | +0.352 ns | +0.051 ns | +0.265 ns |
| GBC | +0.262 ns | +0.067 ns | +0.265 ns |

Source captures match every final build. Firmware partition audits verify the
selected FSBL, supervisor bitstream, and application; all three core payloads
match fresh Bootgen processing of their selected bitstreams. The accepted
runtime remains unchanged. The delivery includes these audit records and a
source delta with exact byte copies to preserve captured line endings.

The owner subsequently confirmed firmware `20260920` was flashed and board
tested. The build-time audits above predate that confirmation and retain
their original status. Further qualification can cover menu open/close during
gameplay, held-key release, continued save completion, native disk launch,
reset, switching, and rollback; individual sequences were not enumerated in
the owner's confirmation.
