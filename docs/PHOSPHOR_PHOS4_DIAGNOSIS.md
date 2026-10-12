# PHO4 runtime bug investigation

The owner reports that Genesis loading returns `transfer failed`, effects have
no visible result, and Video size does not change the picture. The firmware
and cores are reported to be the matching PHO4 bundle. The effects page says
`60 Hz FPGA video processing`. These are physical observations; the earlier
package, timing and arithmetic checks did not establish runtime acceptance.

## Established Video size defect

The display controller rewrites all 24 clock registers even when only the
multiplier changes. It then requires LOCKED to fall before committing the new
geometry. A regression with an unchanged, continuously locked pixel clock
reproduces the stall: `stable-LOCKED multiplier change stalled: writes=24 locked=1`.

Same-resolution requests now drain the DDR owners, hold pixel consumers in
reset for 32 control cycles, commit geometry, wait for mailbox acknowledgement
and settling, then release at the display frame boundary. They issue no clock
AXI writes. Resolution changes and recovery still program the complete clock
configuration. The focused controller and four-machine geometry/reset tests
pass. This shared HDL change requires new supervisor and machine bitstreams;
it has not been delivered or tested on the board.

## Compact splash lettering

The PHO4 splash centers its panel canvas but uses padded glyph-cell widths
for its lettering. At compact scale, padding contributes leftward offsets
of 12 pixels for MULTITINI, 6 for ONE and 3 for the prompt. The standby color
register adds a separate one-pixel shift to the right relative to outgoing DE.
The three-line block including the prompt is 9 pixels below the screen center.

The source correction centers each line using its visible ink width and moves
all three lines up 9 physical pixels in compact modes. The compact canvas
also advances one pixel to compensate for color latency. The panel and motif
are centered on the transmitted raster. The 1080p raster is unchanged. Golden
raster tests measure glyph bounds using a counter derived from outgoing DE.
This is another shared HDL correction requiring rebuilt FPGA images; DIA4
retains the original PHO4 supervisor and core pixels.

## Effects status

Effects default Off. The identity frame pipeline still runs at 60 Hz when the
master switch is Off, and its old status text obscures this distinction. The
updated status explicitly says `Effects off; enable effects to apply saved
controls.` It preserves the saved switch and effect values. Faults still take
precedence. A regression covers an active identity pipeline, enabling effects
again, and unchanged frame ownership.

The owner's master-switch state is still needed before attributing the missing
scanlines and Blur to this status issue. The status correction alone does not
prove that the reported effects problem is fixed.

## Genesis

Existing service tests used the pre-burst ABI. They now exercise ABI 0.21 word
bursts, backpressure, the final physical-write delay before SAVE, and recovery
from injected failures. A separate RTL test connects the real control
registers, reset/clear crossings, byte and abort mailboxes, and Genesis loader.
Cold load, replacement and abort recovery pass with exact cartridge/save
bytes. Its initial memory response and core-ready signals are modeled.

The extended integration also passes three loads through the actual adapter,
chipset, cartridge SRAM, ROM cache, memory CDC and PSRAM line service. It checks
the full save RAM before release, real CORE_READY and an original 68000 RAM
signature. PSRAM driver/device replies remain modeled; pin timing and physical
read data are outside that test. No transfer failure is reproduced there.

No Genesis cause or functional fix is established. Firmware now records the
failed reset/configuration/cartridge/save/mount/release/ready phase and one
snapshot of ten nondestructive status registers. It adds no command, retry or
payload read. Command/response values are live after failure; the existing
abort or cleanup may have replaced the failing command. The menu shows the
phase and link result, and UART prints the snapshot.

The UART `:diag` command also reports ABI-gated video controls, counters and
display status. It never reads MEDIA_DATA, which consumes save bytes. No serial
port is connected to the development host. The failing `[MD]` result and
`[display] size=` output are requested from the owner.

## Retained evidence

`build/genesis-transfer-diagnosis/` holds the current host and Genesis
integration logs and source hashes. All 61 host tests pass. The existing PHO4
and PHO3 bundles remain hash-identical. The real-register effect upload tests
also pass with both portable models and vendor XPM, including reset recovery
and quantized scanline output. This is not physical acceptance of the owner's
reported effects behavior.

## DIA4 diagnostic firmware

DIA4 uses ARM build ID `0x44494134` and ABI 0.21. Its supervisor bitstream and
machine cores remain PHO4 (`0x50484F34`); the separately fixed Video size RTL is
not part of this diagnostic image. It retains the exact installed machine
inputs while isolating the Genesis failure.

The diagnostic firmware adds the phase-specific on-screen error, cached UART
snapshot, truthful effects-Off status and safe `:diag` counters above. It
preserves loading behavior, saved settings and media. It does not establish a
Genesis functional fix. The package retains the exact PHO4 supervisor sources
and normal signoff separately from the newly compiled ARM sources and platform
records. No flash or board acceptance is performed by the development host.

Keep the installed PHO4 core files. Copy only the diagnostic `sd/FIRMWARE.BIN`
to the updater's required SD path and use the existing golden updater. Then
retry the Genesis cartridge and report the phase-specific error. Also check
that Enable effects is On before checking the core picture with the menu
closed. Retain the original PHO4 firmware package for recovery.
