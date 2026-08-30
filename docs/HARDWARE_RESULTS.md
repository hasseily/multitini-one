# Hardware Results

## 2026-08-29 PAL diagnostic image

Artifact:

- Package: `Multitini-One-PAL-diagnostic-20260829.zip`
- `FIRMWARE.BIN` SHA-256:
  `8595b9e8a60c63adb52d87197e74dd5170f2488d831bc459faabe0447db8e512`
- Shell build ID recorded by Vivado: `fd554dbf`
- Source state: dirty engineering snapshot included in the package

Observed on the stand-alone board:

- The golden UART updater loaded and booted the secondary firmware image.
- DVI locked and displayed the diagnostic vertical color bars.
- UART command `v` disabled and re-enabled the DVI output.
- UART command `a` produced the fixed diagnostic tone cleanly.
- Extended command `:reboot` returned successfully to the golden image.

The final grey bar was half width because the diagnostic source groups pixels
by `x[10:8]`: seven 256-pixel groups consume 1792 pixels and the eighth receives
the remaining 128 of the 1920-pixel active line. This is known source behavior,
not a DVI timing failure, and it is not being changed for this image.

The status values printed by this image are not accepted as register evidence.
The firmware passed 32-bit arguments to `xil_printf` formats containing `l`,
which makes the Cortex-A9 BSP formatter consume 64-bit arguments and corrupt
the subsequent varargs. The following snapshot firmware corrected those
formats; numbers from this first diagnostic log remain unusable.

UART command `r` caused no visible change. That is expected for this image:
the C64 core is already held in reset until BASIC, KERNAL, and character ROMs
have all loaded, while the displayed bars and fixed tone are independent shell
diagnostics.

Not tested by this image:

- C64 machine video or SID audio on physical outputs;
- keyboard, joystick, SD, DDR, USB, Ethernet, or PSRAM;
- PAL long-run stability, NTSC, or repeated cold/warm boots.

## 2026-08-29 PAL C64 frozen-snapshot image

Artifact:

- Package: `Multitini-One-PAL-C64-snapshot-bringup-20260829.zip`
- Package SHA-256:
  `be7aa9bf9f30039f36a6d81d9afe623144f3ff2a478b4ff0316f720624bc3b5c`
- `FIRMWARE.BIN` size: 4,241,740 bytes
- `FIRMWARE.BIN` SHA-256:
  `db5128217b7510d265a0745089dde4642d00c936fd8325caa7bd3b61de842723`
- `FIRMWARE.BIN` CRC32: `0DDF77E5`
- Shell build ID: `fd554dbf`
- Source state: dirty engineering snapshot at base commit
  `fd554dbf46e22514bb132f3dc063ee0a892bdcc6`

Observed on the stand-alone board:

- The golden UART updater loaded and booted the snapshot image.
- The host uploader sent the exact 20 KiB BASIC, KERNAL, and character bundle;
  target validation completed and the C64 left reset.
- The C64 reached its PAL ready screen and DVI showed a coherent centered
  frozen frame with the intended black side borders.
- Switching between diagnostic bars and the machine frame worked.
- `:reboot` returned to the golden image.

This result establishes the ROM transfer, FPGA ROM stores, reset release, C64
boot, one-frame capture, DVI scaling, and vblank-only source-selection path on
this board. The user reported the test working perfectly. It does not establish
live frame delivery, keyboard, joystick, storage, or SID audio.

## 2026-08-29 PAL C64 live-video result

The source now uses four 382x270 banks of four-bit fixed-palette indices.
Approximately 50.123 Hz PAL frames are published through a Gray-pointer ring;
the 1080p60 side swaps only at vertical blank and cleanly repeats the current
bank when no new frame is ready. Focused XSim passes the swap, repeat,
full-ring drop, malformed-frame rejection, diagnostic-switch, and independent-
reset cases. The final test also passes Vivado 2025.2 XSim using the real XPM
and UNISIM libraries. Its only two `XPM_CDC_GRAY S-2` diagnostics occur during
the intentional reset-epoch pointer return to zero; neither steady-state
three-bit pointer rollover produces a CDC diagnostic.

A fresh PAL build with shell build ID `20260829` completed synthesis,
implementation, bitstream generation, and XSA export in Vivado 2025.2. The
manifest timestamp is `2026-08-29T15:33:38Z`; it records base commit
`fd554dbf46e22514bb132f3dc063ee0a892bdcc6` and a dirty engineering tree.

Packaged test artifact:

- Package: `Multitini-One-PAL-C64-live-video-20260829.zip`
- Package SHA-256:
  `5d0974963181d48013962e1e31311dff4c3bda4706ddf59d7cbb102774cff615`
- Source archive: `Multitini-One-source-C64-live-video-20260829.zip`
- `FIRMWARE.BIN` size: 4,241,740 bytes
- `FIRMWARE.BIN` SHA-256:
  `a44943738448e3114e58c03a30a2210270684ecf849911621e045ae5771f8703`
- `FIRMWARE.BIN` CRC32: `01F764E0`
- Free firmware-slot space: 10,372,788 bytes
- Bootgen readback verified the expected FSBL, routed bitstream, and two
  application segments.

Vendor-tool result:

- Timing passes with WNS +0.675 ns, WHS +0.053 ns, WPWS +1.749 ns, and zero
  failing endpoints. The DVI output path group passes with setup slack
  +1.672 ns and hold slack +1.718 ns.
- The CDC report contains 14 `CDC-3` and one `CDC-9` informational crossing,
  with no Warning or Critical entry. The fatal critical-CDC gate passes.
- Both live Gray-pointer bus-skew checks pass: actual skew is 0.507 ns and
  0.610 ns against a 6.733 ns requirement.
- All 11,033 routable nets are fully routed with zero routing errors, and DRC
  reports zero errors.
- The routed design uses 6,168 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one
  RAMB18 block, and seven DSP blocks. The live bridge accounts for 543 LUTs,
  335 flip-flops, and 64 RAMB36 blocks.

Observed on the stand-alone board:

- The golden UART updater loaded and booted this exact live firmware.
- The bundled 20 KiB ROM upload completed, and `v` displayed the Commodore 64
  BASIC screen with a continuously blinking cursor.
- A five-minute machine-view soak remained DVI-locked and showed no visible
  tearing or partial-frame boundary.
- Repeated `v` changes switched cleanly between diagnostic and machine views
  as whole frames.
- `r` fell back safely during reset and returned to a new live C64 frame after
  rearm.
- `:reboot` returned successfully to the golden image.
- The user reported every step in the live-video hardware procedure passing
  perfectly.

This result qualifies live PAL frame capture, complete-frame rate conversion,
vblank-only source switching, reset-epoch recovery, and golden fallback on the
tested board/display combination. It does not qualify physical keyboard or
joystick input, storage, SID audio, NTSC, or extended environmental operation.

## 2026-08-29 PAL C64 USB-input bring-up

### Build 2026082A: controller startup, no USB1 connector power

The ARM USB1 host translates a USB boot keyboard and common HID gamepad reports
into the already-tested C64 input mailbox. Host tests cover key and joystick
state, disconnect, rollover, retry, aggregation, port swap, and reset
reassertion.

Packaged test artifact:

- Package: `Multitini-One-PAL-C64-usb-input-2026082A.zip`
- `FIRMWARE.BIN` size: 4,346,380 bytes
- `FIRMWARE.BIN` SHA-256:
  `3e211fd424675753afa9a208b8c817bd12e196cb097951a0e38f2b61b8bab0db`
- `FIRMWARE.BIN` CRC32: `B25E5862`
- Free firmware-slot space: 10,268,148 bytes
- Bootgen readback verified the expected FSBL, routed bitstream, and two
  application segments.

The isolated PAL Vivado build passes with WNS +0.394 ns, WHS +0.057 ns, WPWS
+1.749 ns, zero timing failures, no Critical CDC entry, no routing error, and
no DRC error. Vitis rebuilt the FSBL and application against that exact XSA.

The first physical USB1 check failed. The user observed that the keyboard
received no USB1 power. UART command `u` reported:

```text
usb1 started=1 portsc=8C001000 keyboards=0 gamepads=0 joyport=2 reports=0 submit_err=0 transfer_err=0 input_err=0 last=0
```

This result establishes controller startup and a set `PORTSC.PP` request. It
does not establish connector VBUS or HID operation: no device attached, no HID
interface enumerated, and no input report arrived.

### Build 2026082B: USB1 power and generic HID pass; keymap and Xbox fail

Exact firmware artifact:

- File at test time: repository-root `FIRMWARE.BIN`
- Source/build-input commit:
  `c2b87fad2913580de296eb9b06a9186b57deeefa`
- `FIRMWARE.BIN` size: 4,351,948 bytes
- `FIRMWARE.BIN` SHA-256:
  `e13e1b3df355e38590ebff3c5f496fa10a8b5c1d89bee0066f649aed1caf9425`
- `FIRMWARE.BIN` CRC32: `F31417DA`
- Free firmware-slot space: 10,262,580 bytes
- Routed bitstream SHA-256:
  `1db57776fab49b7fea3056032c4a20205652fabe6805887979e5541d5feddba2`
- XSA SHA-256:
  `f3b0608b77e7b0c8f4612d3a61665234453c3f4f98dc067888f1635f157c953e`
- Bootgen readback verified the expected FSBL, routed bitstream, and two
  application segments.

The matched PAL Vivado 2025.2 build passes at WNS +0.119 ns, WHS +0.053 ns,
and WPWS +1.749 ns with zero total negative slack or failing endpoints. All
11,046 routable nets are fully routed. CDC has 15 Info-only crossings and no
Warning or Critical entry; all four bus-skew constraints pass. DRC has zero
errors or critical warnings. The routed design uses 6,169 LUTs, 5,703
flip-flops, 85 RAMB36 blocks, one RAMB18 block, and seven DSP blocks. A fresh
Vitis 2025.2 workspace rebuilt the FSBL and final application from this XSA.

Firmware build `2026082B` adds explicit USB3300 host-power setup through the
Zynq USB1 ULPI viewport in
[`cherryusb_zynq_hc.c`](../firmware/platform/xilinx/cherryusb_zynq_hc.c), using
the register policy in
[`cherryusb_ulpi.h`](../firmware/platform/xilinx/cherryusb_ulpi.h). It keeps
`SuspendM` set, enables host `AutoResume`, uses the USB3300 internal VBUS
comparator, and sets `DpPulldown`, `DmPulldown`, `DrvVbus`, and
`DrvVbusExternal`. The firmware requires readback values of Function Control
`0x41`, Interface Control `0x10`, and OTG Control `0x66`.

`UseExternalVbusIndicator` remains clear. The board's TPS25221 `FAULT#` output
is open-drain and the `USB_MOUSE_EXTVBUS` net has no pull-up, so it cannot be
used as a valid healthy-state input. USB3300 `CPEN` still enables the TPS25221
VBUS switch, as recorded in
[`BOARD_FACTS.md`](BOARD_FACTS.md#known-from-the-reviewed-sources).

UART command `u` now reports PHY identity, setup and cleanup results,
VBUS-drive and VBUS-valid readback, ULPI errors and timeouts, and relevant
register snapshots through
[`usb_hid_service.c`](../firmware/platform/xilinx/usb_hid_service.c). The host
test in
[`test_cherryusb_ulpi.c`](../tests/host/test_cherryusb_ulpi.c) covers viewport
command encoding and the `0x66` OTG / `0x10` interface policy. These automated
checks cannot prove connector power or device enumeration.

Shutdown clears both USB3300 VBUS-drive bits, reads them back, and waits for
the internal `VbusValid` indication to fall. That verification remains pending
across retries if it fails; `:reboot` cancels instead of resetting with USB1
power in an unverified state.

Board results prove the corrected USB1 power path, keyboard enumeration and
input, and generic HID gamepad input. A USB keyboard received power, produced
reports, and controlled the live C64 BASIC screen. The generic low-speed
gamepad `06a3:0107` enumerated as HID with `gamepads=1` and an increasing
report count. Its final status included `func=46`, which is the active
low-speed Function Control value rather than the idle `func=41` setup value.

Two failures define the next build. The positional/hybrid symbol map made the
PC `=` key act as C64 plus while `Shift+=` produced a shifted graphics plus;
`PRINT 2+2` therefore echoed a plus-like glyph but evaluated as `22`. The
wired Microsoft Xbox 360 controller `045e:028e` could not enumerate because
its four-endpoint interface met CherryUSB's erroneous `>=` capacity check:

```text
[E/usbh_core] Endpoint num 4 overflow
[E/usbh_core] Parse config descriptor fail
[E/usbh_hub] Port 1 enumerate fail
```

Build `2026082B` qualifies connector VBUS, USB keyboard transport, and the
generic HID controller on this board. It does not qualify a US symbolic keymap
or Xbox/XInput support. Held-disconnect, explicit VBUS shutdown, and full
hotplug coverage were not separately recorded and remain regression checks.

### Build 2026082C: US symbolic map and Xbox candidate

Exact firmware artifact:

- File: repository-root `FIRMWARE.BIN`
- Initial build manifest: UTC `2026-08-30T03:27:47Z`, base commit
  `ee71f5dd5a77e8a15c9881e2a07d3ee48c9c9c77`, intentional dirty engineering
  tree, PAL C64, shell and firmware build ID `2026082C`
- `FIRMWARE.BIN` size: 4,362,060 bytes
- `FIRMWARE.BIN` SHA-256:
  `ae75318d458c6f6203c5ed6a87be1283ace6b39dacf0307c7e82c52e281ea004`
- `FIRMWARE.BIN` CRC32: `EF6EB912`
- Free firmware-slot space: 10,252,468 bytes
- Routed bitstream SHA-256:
  `e0b6df0a38dcbd1c4a6f31e2274d4c74e62cf77489f9332bd36a276a9c650e4b`
- XSA SHA-256:
  `530322983caa359baedcd1cbc50b990b0657862c3c6cf545ba91f9f86289158c`
- Diagnostic ELF SHA-256:
  `50373cdb3f936e082a3004a10e3f195a9e19923dbb423506d762d0309877e5f4`
- FSBL ELF SHA-256:
  `474bd41ca01df2d9bf69b30f92321c8a0a674591040152ea3e10883a50f0f63c`
- Bootgen readback verified the expected FSBL, routed bitstream, and two
  application segments.

The default PAL Vivado 2025.2 build uses shell build ID `2026082C` and passes
at WNS +0.599 ns, WHS +0.036 ns, and WPWS +1.749 ns with zero total negative
slack or failing endpoints. All 11,035 routable nets are fully routed. CDC has
14 `CDC-3` and one `CDC-9` Info-only crossing. All four bus-skew constraints
pass; the two live-pointer skews are 0.476 ns and 0.575 ns against 6.733 ns.
DRC has zero errors or critical warnings and retains 51 advisory warnings. The
routed design uses 6,167 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one RAMB18
block, and seven DSP blocks.

Vitis 2025.2 rebuilt the matching FSBL and application. The application uses
179,856 bytes of text, 1,932 bytes of data, and 141,760 bytes of BSS. Its USB
class registry contains hub, HID, and Xbox entries; the three records span 48
bytes, `xbox_custom_class_info` is retained, and `usbh_xbox_run` and
`usbh_xbox_stop` are strong linked definitions.

Eleven compiled host tests, 44 Python tests, and the repository policy check
pass. This result qualifies the source and vendor build only. The US symbolic
map and Xbox 360 path require the focused board procedure in
[`C64_USB_INPUT_HARDWARE_TEST.md`](C64_USB_INPUT_HARDWARE_TEST.md). Until that
passes, the physical input boundary remains the proven `2026082B` keyboard
transport and generic HID controller.
