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

## 2026-08-29 PAL C64 USB-input image awaiting board test

The ARM USB1 host now translates a USB boot keyboard and common HID gamepad
reports into the already-tested C64 input mailbox. Host tests cover key and
joystick state, disconnect, rollover, retry, aggregation, port swap, and reset
reassertion. The matching build ID is `2026082A`.

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

No physical USB device has been tested with this image yet. Run
`docs/C64_USB_INPUT_HARDWARE_TEST.md`; do not promote these automated results
to hardware support until its keyboard, gamepad, disconnect, reset, video, and
golden-reboot checks pass.
