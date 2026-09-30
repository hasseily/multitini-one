# Appletini shared-platform imports

The source for this port is Appletini ONE
`be2ea4fee3c5ebc239b8b20aad9738bee89f8ff0` (29 September 2026).
Work starts from Multitini `a4e509bd9972c3fc883c524b29c85b66bb6c7e94`, including
the accepted Vectrex and DDR video work. The sibling repository is read-only.

## Scope

- USB completion recovery, wired Xbox One and DualShock 4 input, shared
  controller slots, mapping, live preview, and menu input capture.
- Hide dot entries in SD media and document browsers.
- Preserve effective CDC route bounds, gate bus skew, and import reproducible
  Vivado run settings and complete build-input capture.
- Six selectable display resolutions with safe clock changes and fallback.
- Recoverable golden self-update and validated image manifests.

All remaining applicable imports, including the optional display and golden
follow-ups, are implemented on branch `appletini-parity`. This is source and
local-test parity with the pinned Appletini shared platform. Board acceptance
is a separate gate. No Genesis core is added in this change.

Existing USB3300 startup, exFAT, long filenames, RTC timestamps, text reader,
and CPU1 support are already imported. Apple bus, disk, SmartPort, TransWarp,
and slot-2 card features have no corresponding standalone Multitini service.
They are outside this port. No Apple software or game assets are imported.

## USB controls

CherryUSB advances from `825dc4166ef4b92ced2b15446c668e74f2bc064c` to
`06f424d1d23dd271dcc9cc1e19ec47ca07032dd6`. The only upstream change is EHCI
completion handling: descriptors still being retried stay owned by hardware,
and exhausted retries are distinguished from endpoint stalls. Failed HID and
Xbox 360 completions wait 10 ms before resubmission.

The imported Xbox One driver matches wired `045e:02ea`, interface 0. It retains
Appletini's Hello-gated startup, bounded clone fallback, asynchronous Guide ACK,
clear-halt recovery, and per-endpoint failure limits. The PS4 decoder matches
wired Sony `054c:05c4` and `054c:09cc`, 64-byte report 1. Other controller models,
Bluetooth, rumble, motion, and headset audio are not covered by these drivers.
The existing Xbox 360 startup and player LED command are retained.

HID interfaces and both vendor transports allocate from nine shared input
slots. Each pad retains X/Y/Z/Rx/Ry/Rz, eight buttons, a hat, and auxiliary
button activity. Partial reports preserve omitted controls. Existing core
adapters receive their existing formats; Vectrex retains two independent pads.

Open **Input > Controller setup** with a keyboard. Select a USB slot and a
logical axis or button, then its physical source. Axes also have inversion,
0–50% dead zone, and 25–200% sensitivity. The preview shows physical input
while gameplay input is released. Save applies all slots; Escape cancels.
Mappings follow slots, so reconnect devices in the same order to reuse them.
Menu/Options opens or closes the menu; the left stick or D-pad navigates,
A/Cross selects, and B/Circle returns. Use a keyboard on the setup page so
moving the controller cannot change its own settings.

Settings schema 6 adds controller maps and a display-mode field. Schemas 1–5
remain readable and migrate only when settings are saved. All layouts retain
CRC checks and journal recovery. Closing the menu requires held pad controls
to be released before gameplay resumes. Disconnect releases only that slot.

## Validation status

Host tests cover the actual EHCI completion scanner, Xbox One URBs and recovery,
PS4 packets, HID and Xbox 360 retry scheduling, transport slot collisions,
menu capture and release, mapping ranges, partial reports, settings migration,
and interrupted settings journals. The native USB fixtures work on macOS,
Linux and Windows; only native Mach-O tests bypass the ELF discovery section.
The production linker retains that section.

The host suite passes 52 tests, both normally and with AddressSanitizer and
UndefinedBehaviorSanitizer. The applicable Python suite passes 140 tests
(discovery excluding `test_c64sim` and `test_c64fast`). Full discovery also
finds ROM-dependent tests which need absent local `1541-II.251968-03.bin` and
generated `c64fast/bin` images. Those baseline artifacts are not added by this
import; the full `make check` gate therefore cannot pass in this checkout.

Display simulation checks all six complete output frames, sync polarities,
128×60 compact menus, direct core scanout and all four common shell builds.
Clock tests include independent AXI channel stalls, failed writes, relock,
geometry acknowledgement, fallback, and a DDR owner that never drains.
Native DDR reader, filtering and complete pipeline checks pass at scales
1, 2, 3, 4, 5, 6 and 3:2, including outstanding write responses during drain,
pixel reset, restart and history clearing. Blur matches the C reference under
backpressure. The existing common HDL suite, C64/GBC lint, generated-file
checks, import audits and repository policy checks also pass locally.

These imports have not yet been qualified on Multitini hardware. Accepted
release packages remain unchanged. See [display settings](VIDEO_SETTINGS.md)
and [golden recovery images](golden-update.md) for operation, register/image
contracts, regression commands, and the remaining physical tests.

Before publishing an image, run the vendor build and its timing/CDC/PSRAM
gates for the supervisor and each machine. On the board, check every output
mode and core transition with effects enabled, controller reconnect/recovery,
and staged golden boot/repair including interrupted updates. Publish the exact
source archive and build-input manifest with any new public image.

## Timing and build checks

The board XDC no longer declares whole control/video/machine/audio clock groups
as asynchronous. XPM keeps ownership of its generated handshake and Gray/FIFO
constraints. Shell exceptions target first-stage level/toggle D pins and
asynchronous reset assertion pins only. Later synchronizer stages remain timed.
`check_video_bus_constraints.tcl` checks the implemented frame-bank and capture
FIFO pointer census and each effective maximum delay. A missing, unbounded,
wrong, or failing path aborts export. Bus-skew violations also abort export.

The shared Vivado profile sets eight worker threads in the parent and child
runs. It uses OPT Explore, default placement, PHYS Explore, ROUTE Explore, and
one routed AggressiveExplore pass. Temporary 0.2 ns control/pixel setup margins
are removed before final signoff. Run properties are only changed when needed.
Apple connector timing margins were not imported: those outputs are constant
in Multitini. The existing PSRAM, IOB, CDC and timing-coverage gates remain.

Every ordinary hardware build captures selected HDL, constraints, build helpers,
licenses, provenance and interface specifications before synthesis. The source
archive includes dirty source bytes. Verification after implementation rejects
changes made during the build. `reports/build_inputs.json`, its source archive,
and the run-property manifest accompany the XSA. Existing package-specific
incremental build audits remain available.

Native Tcl fault tests verify the effective-bound gate and run-profile
idempotence. Source capture and archive verification pass locally. Vivado/Vitis
are not installed on this macOS workspace; actual synthesis, routing and board
acceptance remain required for the new hardware image.
