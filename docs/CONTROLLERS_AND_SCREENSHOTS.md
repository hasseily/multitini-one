# Controllers and screenshots

[M0.1.0](FIRMWARE_M0.1.0.md) adds controller access without a dedicated Menu
button: hold Select and Start together for 500 ms. Release both before another
toggle. The gesture works across all cores and also returns from assignment
and test screens. D-pad, A/B and L/R navigate the normal configuration menu;
stick settings can now be adjusted with the controller.

Firmware CT01 (`43543031`) adds per-core controller profiles and screenshots.
CT02 (`43543032`) adds shoulder-button menu navigation. Its focused 15-check
host suite and 26 production runtime transaction tests pass. Video filtering
is unchanged; see the [resolution study](VIDEO_FILTER_RESOLUTION_STUDY.md).
It uses the existing supervisor and machine FPGA images. No HDL, clock,
reset, crossing, register layout, or timing constraint changes are required.
Host tests cover the input and capture paths; physical controller and SD-card
acceptance remain board tests.

## Controls

Open **Controls → Controllers** while a core is running. Choose a controller,
then choose a machine action and press Enter to bind it. Release controls when
prompted, then press the desired button, D-pad direction, stick, or trigger.
The editor shows only the running core's actions.

- **Set all controls** walks through the machine actions and menu shortcut.
- Enter on an action changes one assignment. Delete clears it. Escape cancels
  capture. A conflicting assignment must be explicitly moved or cancelled.
- **Test controller** shows physical inputs and the actions in the draft map.
- **Stick and trigger settings** changes deadzone, direction, and sensitivity per source axis.
- **Save changes** writes and applies the draft. **Discard changes** restores
  the saved configuration. Defaults affect the selected controller and core.
- F12 always provides keyboard access to the Multitini menu. Identified
  Home/Guide/PS buttons open it by default. Select/Back and Start/Options remain
  available to the machine when pressed separately. Holding Select and Start
  together for 500 ms is the shared menu shortcut, including generic HID pads.
- **LB/L1** selects the previous menu section; **RB/R1** selects the next,
  just like Shift+Tab and Tab. Generic HID pads use Button 5 and Button 6.
  Hold both to cancel tab movement. These shortcuts follow the keyboard's
  behavior inside editors, and are disabled during input capture and testing.
  They do not change the controller's in-game shoulder assignments.

The shared input layer retains up to 32 separate reported buttons, a D-pad,
and six axes. Xbox 360, Xbox One, PS4, and generic HID controllers share the
same profile system. Nine USB interface entries are available, shared with
keyboards. Multiple controllers of the same supported transport may connect.
C64, Genesis, and Vectrex expose two machine ports; Game Boy exposes one.
Additional connected pads remain selectable for configuration. Player/port
assignments can be automatic, explicit, or disabled.

Profiles use core ID, USB device/interface identity, and serial when available.
Without a serial they use the USB topology, so reconnect a pad to the same
port to restore its individual map. Identical pads on different ports retain
separate profiles. A controller replaced during editing cannot inherit the
previous device's unsaved changes.

Settings schema 8 stores up to 64 controller/core profiles. Older slot maps
are migrated when the controller is identified, preserving button and axis
settings for all four cores. Menu access moves to identified Home. Migration
is written with the next explicit settings save. Keep a copy of
`multitini/settings.bin` before testing if you need to return to older firmware.

## Screenshots

**PrtScr** saves the post-filtered game picture, cropped to its on-screen area
at its displayed size. **Shift+PrtScr** saves the full output
resolution, including scaling, borders, video effects, and any visible menu.
Both shortcuts work while the menu is closed or open. PrintScreen is consumed
by the supervisor; other held keys continue to reach the running machine.

The two screenshot actions can also be bound to controller inputs in Controls.
They start unassigned and are optional in setup. A held shortcut makes one
capture; release it before taking another. A capture already in progress is
allowed to finish before another starts.

PNG files are created under `screenshots` on the SD card. Names include the
core, uptime, capture number, and `video` or `screen`. Existing files are never
overwritten. An incomplete `.part` file is closed before a core switch and
removed on cancellation or failure. The complete PNG appears only after the
write and close succeed. The menu status and UART report completion or errors.

Both modes copy the displayed processed RGB565 frame and reconstruct the
existing display filters. Video-only captures exclude the menu and borders;
full-screen captures include them.
The implementation checks frame publication sequences before and after the
copy, and retries changing frames. It does not stop or claim an FPGA buffer.
PNG output is uncompressed and may approach 6 MiB at 1920×1080.
Capture requires a running core with the existing ABI 0.15 or newer frame
transport. Standby and diagnostic test-pattern capture are not provided.

## Board test

1. Update only `FIRMWARE.BIN` using the normal recovery updater. Keep existing
   core packages and media on the SD card.
2. Connect two pads, including two of the same type if available. Verify
   independent ports, Home versus Select/Start, and F12 access.
3. Bind an extra button, stick click, and trigger. Test clear, cancel, conflict,
   save, discard, and power-cycle restoration.
4. Give the same pad different C64 and Game Boy maps. Switch cores and confirm
   each map returns. Disconnect and reconnect pads without moving the survivor.
5. Take both keyboard screenshots and both controller screenshots. Open the
   PNGs from SD and compare dimensions, effects, borders, and menu text.
6. Repeat a capture with no SD card or a full card. Check that failure is
   reported and controls, core switching, and later captures still work.

## Build validation

The CT01 test image was compiled with Vitis 2025.2 using the retained
20260930 supervisor XSA/platform and supervisor bitstream. CPU0 and its embedded
CPU1 image were rebuilt and their source hashes verified. Machine core packages
were not rebuilt. The test bundle records the exact input hashes and includes
the retained FPGA source/settings archive and the firmware source archive.

The strict host build passed. Sixty of 61 CTest checks passed, including all
controller, settings, menu, screenshot, PNG, and SD fault-injection checks.
The existing Vectrex BASIC runtime test fails at Windows `tmpfile()` creation
before its runtime scenarios; it also fails outside the sandbox. The Python
suite ran 158 tests successfully with seven optional tool checks skipped.
The two existing C64 ROM/simulator suites were excluded because generated ROM
images and the `py65` dependency are absent. These results are not board tests.

Rebuild with `scripts/create_vitis_workspace.py`, setting `MULTITINI_XSA` to
the retained supervisor XSA, `MULTITINI_PLATFORM_XPFM` to its exported platform,
`MULTITINI_VITIS_WORKSPACE` to a new output directory, and
`MULTITINI_SKIP_GOLDEN=1`. Run it using `vitis -s`, then package the explicit
FSBL, supervisor `.bit`, and new `diagnostic.elf` with
`scripts/make_firmware_bin.bat`. The CPU1 manifest identifies every compiled
source and validates the embedded CPU1 payload.
