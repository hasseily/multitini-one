# Hardware Results

## 2026-09-18: VID5 blur accepted; startup investigation closed

Following delivery of the VID5 C64 PAL and Game Boy / Game Boy Color update,
the owner confirmed: "Blur is fixed." This records owner-reported on-card
acceptance of the blur correction. No additional board measurements or
per-level test sequence were supplied.

The accepted delivery is `build/core-update-video-vid5.zip`, source commit
`81ad9271fb5fb16bdca35bd6bfd979413db44eac`, SHA-256
`389d5cb63f488667b9f54d43ff4bd44d8c4a53d998a8a7b9b82b2e76ff79774f`.
Its exact C64/GBC payloads and the retained VID4 firmware/Vectrex combination
remain unchanged. The original package and its pre-installation audit
records are preserved.

The owner also instructed us to forget the GBC startup issue. That
investigation is closed at the owner's request and is no longer an active
task or acceptance blocker. No startup cause or corrective fix is claimed.

## 2026-09-18: VID4 GBC startup failure and weak blur

After the VID4 delivery, the owner reported a Game Boy boot screen with
garbled text at the bottom, followed by a white screen instead of gameplay.
The owner then reported that GBC worked again. The failing state was not
captured over UART, and no cause or corrective action has been established.
At that point the intermittent startup report was unresolved. A later
passive UART read found the owner had switched
to the VID4 C64 core; its identifiers and CPU1 Ready state were correct.

The owner also reported that blur was barely visible on C64 and not visible
on Game Boy / Game Boy Color. The VID3/VID4 inverse-square scale correction
reduces each blur axis to 36/256 on C64 and 16/256 on GBC before RGB565
quantization. The existing numerical normalization tests did not establish
useful visible separation between the blur levels on these cores.

The accepted historical GBC baseline below remains distinct from VID4.
VID4 build, timing, and payload checks do not establish reliable cartridge
startup or sustained 60 Hz gameplay on the board.

A focused source comparison found that the VID3-to-VID4 GBC FPGA change
was the shared register-read adapter fix and build identity. The cartridge
loader, PSRAM logic, video logic, clocks and constraints were unchanged.
The staged read does not alter write-channel ownership, and stream-status
reads have no cartridge-write side effect. This inspection found no cause
for the reported startup failure.

`scripts/test_axi_gameboy_loader.py` now exercises the actual AXI adapter,
control bank, asynchronous mailbox, GBC cartridge loader and returned
status synchronizers together. Two random seeds and three uploads per seed
check 196,616 cartridge bytes, including 456 header bytes, with independent
write-channel arrival, response backpressure, scrambled read addresses,
partial final words, consecutive loads and a drained reset. The test also
withholds the final memory response and verifies that execution and the
firmware-visible Ready status remain blocked until completion. All checks
pass; no production upload logic was changed.

The external memory endpoint is modeled and the mailbox uses the portable
XPM model. This does not test physical PSRAM storage, pin timing, the mapper
or Game Boy CPU execution. The production write response echoes the held
write data rather than reading it back, so a successful upload transport
does not independently verify the stored cartridge. The investigation was
subsequently closed at the owner's request, as recorded above.

## 2026-09-14: shared interface firmware 20260920 board tested

The owner confirmed that firmware `20260920` was flashed and board tested,
then requested committing and pushing the changes. This is the firmware-only
menu-persistence correction over the compact shared interface and automatic
pause implementation. It uses the `20260919` supervisor and matching C64/GBC
FPGA packages at ABI 0.11.

The supplied `build/firmware-ui-20260920/FIRMWARE.BIN` is 4,612,980 bytes with
SHA-256 `c2d8c7fa981cd9991df5c2351339fe760414d3f7c20c73abf7ef2ba61378d7ee`.
The confirmation is owner-reported board evidence; no additional UART capture
or independent device readback was taken during this commit. The owner did
not enumerate per-core test sequences or a test duration.

The preceding build/payload audits retain their original pre-flash status.
[Shared front end](SHARED_FRONTEND.md) records the behavior, exact build inputs,
and software/FPGA validation. The retained `20260917` current-package aliases
are unchanged by this source commit.

## 2026-09-14: GBC functional baseline accepted

The owner confirmed that the current GBC implementation works correctly,
including saves, and stated that they were tested. This closes the functional
GBC baseline for the retained PSRAM-enabled `20260913` core (ABI 0.9) in the
current package with ARM firmware `20260917` and supervisor FPGA `20260915`.

This is owner-reported board evidence. No new UART capture, flash operation,
or board test was performed during closeout. Earlier records establish
C64-to-GBC switching, Tomb Raider and V-Rally gameplay, and physical save
persistence confirmed on 2026-09-04. The new confirmation does not enumerate
additional games, reset sequences, or interrupted-write cases.

[GBC baseline](GBC_BASELINE.md) records the accepted scope, verified package
hash, source checks, and separate feature and qualification limits.

## 20260917: Ultima II Mount & Run passes

The user confirms that Enter in Mount & Run now launches Ultima II and
reaches the second-disk prompt. This validates the reported launch fix on
the board. Mounting the Player disk and continuing also works. A later
time-gate map transition reports `LOAD ERROR!` with fast ROMs off and on;
the UART media service reports no fault. A subsequent Right/native-LOAD
test also fails just after Player-disk insertion. The user then confirms a
clean Player disk works with fast ROMs on and identifies the previous copy
as corrupted. The cause of that corruption remains unconfirmed. See
[firmware 20260917](C64_LAUNCH_20260917.md) for the exact artifact and checks.

## 20260916 candidate: launcher LOAD failure

The follow-up board report shows `?FILE NOT FOUND ERROR IN 10` after the
injected RUN, returning to BASIC. The launcher fails before the game starts.
LIST shows the correct `10 LOAD"ULTIMA II",8,1`. Another RUN and manual
LOAD commands for ULTIMA II, `*`, and `$` also fail without remounting.
The source audit found a shared error-clear operation canceling a healthy
disk request after remount. See [firmware 20260917](C64_LAUNCH_20260917.md)
for the correction and the successful Ultima II board retest recorded above.
Right followed by manual RUN remains the previously confirmed workaround.

## 20260915 candidate: Ultima II comparison

The user reports that manual LOAD/RUN and the menu's Right LOAD followed by
RUN both reach Ultima II's second-disk prompt. Enter's direct RAM launch later
reports a read error. ULTIMA is a later module, not the startup PRG.
See [firmware 20260916](C64_LAUNCH_20260916.md) for the generic launcher change
and the subsequent board failure recorded above.

## 2026-09-14 disk launch report

The user reports that Decathlon Mount & Run, the fast loader in other tested
software, and battery saving work. Colors disk 1 stalls with Fast serial ROMs
and CPU turbo Off; with Fast serial ROMs On it reports that the drive must be
turned on. The Ultima I program browser shows HELLO but omits ULTIMA I.

The findings, candidate changes, and remaining board tests are recorded in
[C64 disk launch candidate 20260915](C64_LAUNCH_20260915.md). Its later board
follow-up is recorded above.

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
pass. At packaging time this qualified the source and vendor build only and
required the focused board procedure in
[`C64_USB_INPUT_HARDWARE_TEST.md`](C64_USB_INPUT_HARDWARE_TEST.md).

The subsequent board test qualified both the US symbolic and retained MiSTer
positional keyboard maps. The Xbox controller also enumerated, bound interface
0, delivered valid reports, and drove the D-pad and left-stick directions.
However, A did not assert C64 Fire and the player ring kept blinking. Those two
remaining observations led to the focused `2026082D` diagnostic build below.

### Build 2026082D: Xbox input map and player LED pass

Exact firmware artifact:

- File: repository-root `FIRMWARE.BIN`
- Vivado build manifest: UTC `2026-08-30T04:33:20Z`, base commit
  `765aee1fbf54c330c7fdab1543da38c0b577b277`, intentional dirty engineering
  tree, PAL C64, shell and firmware build ID `2026082D`
- `FIRMWARE.BIN` size: 4,365,516 bytes
- `FIRMWARE.BIN` SHA-256:
  `536b68ff99f53925d7825d70d7ee30bb586edb8cb301c1e5feff1607248a6600`
- `FIRMWARE.BIN` CRC32: `0680EEAB`
- Free firmware-slot space: 10,249,012 bytes
- Bootgen readback verified the expected FSBL, routed bitstream, and two
  application segments.

Vivado 2025.2 completed synthesis, implementation, bitstream generation, and
XSA export. Timing passes with WNS +0.525 ns, WHS +0.040 ns, WPWS +1.749 ns,
and no failing endpoints. The four bus-skew slacks are +6.213 ns, +6.194 ns,
+125.927 ns, and +125.693 ns. The routed design uses 6,170 LUTs, 5,703
flip-flops, 85 RAMB36 blocks, one RAMB18 block, and seven DSP blocks; the live
video bridge accounts for 546 LUTs. Vitis 2025.2 rebuilt the matching FSBL and
diagnostic application, and Bootgen packaging and readback passed.

The wired Microsoft Xbox 360 controller `045e:028e` completed its player-1 LED
output on the first attempt and continued receiving valid 20-byte input
reports. The final per-controller UART line was:

```text
usb1 xinput0 reports=839 state=10 seen=7F changes=86 in_pending=1 led=1 out_try=1 out_pending=0 submit_err=0 transfer_err=0 last=0 out_last=0 raw=0014001000002EFB6B00
```

`seen=7F` proves that every intended direction, Fire, Space, and Control state
bit was decoded during the test. On the live C64, D-pad and left-stick
directions worked, A/Fire produced the expected masked joystick value `15`, B
produced Space, and holding X while pressing a number changed the C64 cursor
color, proving Control end to end. X alone remained nonprinting, as expected
for a modifier. `led=1`, `out_try=1`, and `out_last=0` record successful player
LED delivery without retry or transfer error.

This qualifies the `2026082D` wired Xbox 360 functional input mapping and
player LED command on the tested controller, in addition to the already
qualified keyboard maps. A follow-up test also passed hotplug after boot,
disconnect while held with state release, reconnect, and runtime
joystick-port swap on the same controller and USB topology.

After `j` selected C64 joystick port 1, A/Fire and B/Space both appeared as
Space, X remained Control, and D-pad motion produced the left-arrow glyph, `2`,
and other matrix-dependent characters at BASIC. This is expected C64 behavior:
joystick port 1 shares CIA keyboard-matrix lines, so the ROM keyboard scan can
interpret joystick direction and Fire closures as keys. Those port-1
fingerprints confirm the swap end to end; they are not an Xbox remap. This
closes the focused Xbox checklist for the tested controller and topology.

## Build 2026082E: SD ROM, 1541, and read-only D64 board pass

Exact firmware artifact:

- File: repository-root `FIRMWARE.BIN`
- Shell and firmware build ID: `2026082E`
- `FIRMWARE.BIN` size: 4,475,348 bytes
- `FIRMWARE.BIN` SHA-256:
  `079257d25af73abfc6c3f9372771660934942d9009655e5684bc78952d4cd118`
- Bootgen readback verified the expected FSBL, routed bitstream, and application
  partitions.

The user-reported hardware result passed the complete first SD-to-game path.
Golden installed the root firmware, the C64 system ROMs and 1541 ROM loaded
from `/cores/c64/roms`, and `/cores/c64/media/drive8.d64` mounted as read-only
drive 8. Decathlon loaded from the disk and ran. The game uses C64
joystick port 1, and UART `j` switched the Xbox controller to that port
successfully.

This qualifies automatic SD ROM loading, 1541 startup, default D64 mounting,
read service during a real game load, gameplay from the mounted image, and
joystick-port swapping on the tested hardware and media. Build `2026082E` did
not route SID audio to the physical output, so this result makes no live-audio
claim.

## Build 2026082F: initial live machine audio board result

Build `2026082F` connects the selected machine's coherent signed 24-bit stereo
PCM to both physical audio transports. The I2S path uses the board's 12.288 MHz
MCLK and 3.072 MHz BCLK. The SPDIF path uses a phase increment matched to the
133.333 MHz control clock and captures each stereo pair together. Core reset or
loss of readiness invalidates the held sample while audio clocks continue.

The firmware starts the AK4493 muted, selects live PCM, waits for stable frames,
and then releases soft mute when the core is ready. UART commands are:

| Command | Result |
| --- | --- |
| `a` | Select and unmute live machine audio. |
| `t` | Select and unmute the fixed diagnostic tone. |
| `m` | Mute the currently selected source. |

The matching PAL C64 vendor and package flow passes:

- Vivado 2025.2 routed timing: WNS +0.252 ns, WHS +0.059 ns, WPWS +1.749 ns;
- zero failed or unrouted nets and zero Critical CDC findings;
- successful bitstream and XSA export, Vitis FSBL/application build, and
  Bootgen three-partition readback;
- repository-root `FIRMWARE.BIN`: 4,475,348 bytes, SHA-256
  `24c8ca616e939ddd13ae46807bcd315affcd6a56f6836c14af4c7cf70510903c`.

The first audible board test found the live path otherwise clean, but it did
not pass the game-audio gate:

- Decathlon produced scratch/random noise for about ten seconds near the end
  of its opening theme, then later developed a high-pitched tone that remained
  stuck.
- Ultima II also developed a stuck tone.
- UART `t` replaced the fault with the clean fixed diagnostic tone. UART `a`
  restored live audio and the stuck tone returned. This points to the live C64
  audio source rather than the shared serializer, DAC, or analog output.

This qualifies basic live-audio transport and source switching on the tested
board, not SID compatibility, long-run stability, channel behavior, level, or
SPDIF.

## Build 20260830 candidate: 6581 default, scratching still reported

Build `2026082F` hardwired both imported SID instances as 8580. The upstream
SID model uses an 8580 noise timeout of `24'h950000`, about 9.77 seconds at the
nominal SID rate, which closely matches Decathlon's timed scratch/random
passage. Both tested games predate the 8580. This is a root-cause candidate,
not yet a confirmed diagnosis for either stuck tone.

Candidate build `20260830` makes the adapter's default SID version 6581 while
retaining an explicit parameter for later settings support. Its fresh routed
PAL Vivado result passes with +0.176 ns setup slack, +0.051 ns hold slack, zero
failed nets, and zero Critical warnings. Vitis and Bootgen pass, and Bootgen
readback shows the expected FSBL, FPGA bitstream, and application partitions.
The repository-root `FIRMWARE.BIN` is 4,475,348 bytes with SHA-256
`99a21c6f929ffe056faabd7d80da9f64bded7d83b8ccf989213ba1fa157aee47`.

On 2026-09-02 the user reported that the scratching heard before this attempted
fix remains. This rejects the SID-model change as a sufficient repair. The
exact live UART banner and artifact hash were not available in this session,
so this is recorded as user retest feedback rather than a newly captured board
log.

## Build 20260902: coherent dual-SID result scheduling fixes reported scratching

Source analysis found a deterministic one-clock corrupt value in the imported
SID port's shared filter schedule. Filter state 7 launches the final audio MAC.
The first SID result is therefore valid at state 8. For the second SID, the
first full state-15 edge launches that same MAC, but the old logic also copied
`faudio` into the right output on that edge, before the nonblocking MAC result
was visible. The value copied was the preceding filter intermediate. A later
state-15 edge corrected it, leaving a roughly 31.7 ns bad right-channel value
once per roughly 1.0 us SID period.

The adapter samples at an unrelated nominal 48 kHz. It could therefore capture
that approximately 3.1%-duty transient around 1,500 times per second and hold
each captured error for a complete PCM frame, which is a direct mechanism for
the reported scratch. The fixed port waits for one repeated state-15 edge and
then publishes both SID results together. It also resets the shared schedule
and output latches on machine reset and moves early-referenced declarations to
strict-tool-safe positions.

A focused Vivado XSim test models the filter's intermediate and final MAC
timing while driving both SID slots as mono. Before the repair it observed four
left/right mismatches at cycles 20, 48, 80, and 112 of a 128-clock run. With
the repair, the same test reports:

```text
PASS: mono dual-SID output updates as a coherent pair
```

The fresh PAL route passes with +0.411 ns setup slack, +0.029 ns hold slack,
zero routing errors, and no Critical warnings or critical CDC findings. Vitis
and Bootgen packaging pass; Bootgen readback contains the expected FSBL, FPGA
bitstream, and application partitions. The repository-root `FIRMWARE.BIN` is
4,475,348 bytes with SHA-256
`9af37acf16f35c0f784ad98a505d4864c38d187bb84675f81a884b6002ef1b52`.

The 4,475,348-byte artifact was installed through the golden UART updater on
the connected Appletini. After reboot, the live shell returned:

```text
magic=4D543031 abi=00000004 shell=4D544F4E build=20260902
core=43363420 status=0000014D error=00000000 control=00000000
[C64] drive8 empty
```

On 2026-09-03 the user reported that the audio bug was fixed. This board result
qualifies the scheduling repair against the previously reported scratching on
the connected hardware. The exact game sequence and duration, stuck-tone
recurrence, channels, level, reset behavior, SPDIF, and long-run stability were
not separately reported and remain open for broader live-audio qualification.
