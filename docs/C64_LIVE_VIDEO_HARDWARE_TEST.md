# C64 Live-Video Hardware Test

Status: passed on 2026-08-29. The four-bank bridge passes focused XSim, a fresh
routed PAL build in Vivado 2025.2, and the complete board procedure below with
the matching Vitis/Bootgen firmware.

## What this test establishes

This is the next gate after the PAL UART-ROM/frozen-snapshot image passed. The
new bridge stores each accepted 382x270 PAL frame as four-bit fixed-palette
indices in one of four banks. A source-domain publish pointer and pixel-domain
release pointer form a Gray-coded ring. The 1080p60 side acquires at most one
completed bank at each vertical blank.

The C64 produces approximately 50.123 frames per second. DVI remains fixed at
60 frames per second, so some complete PAL frames must be shown for a second
output refresh. This is an intentional clean repeat. The bridge never
intentionally changes banks in the middle of a DVI scan. If all banks are
occupied, it drops a whole incoming PAL frame before capture starts.

This test can establish live PAL machine video and reset-safe frame ownership.
It does not establish keyboard, joystick, storage, SID audio, NTSC, or long-run
product qualification. Command `a` still controls the independent diagnostic
tone.

## Require a new exact firmware

Do not use the frozen-snapshot `FIRMWARE.BIN` whose SHA-256 is
`db5128217b7510d265a0745089dde4642d00c936fd8325caa7bd3b61de842723`.
That image can display only one captured frame.

The accepted live Vivado baseline is shell build ID `20260829`, manifest
timestamp `2026-08-29T15:33:38Z`, base commit
`fd554dbf46e22514bb132f3dc063ee0a892bdcc6`, and a dirty engineering tree. The
fresh PAL reports record:

- WNS +0.675 ns, WHS +0.053 ns, WPWS +1.749 ns, and zero failing endpoints;
- DVI output-path setup slack +1.672 ns and hold slack +1.718 ns;
- 14 `CDC-3` and one `CDC-9` informational crossing, with no Warning or
  Critical CDC entry;
- live-pointer actual bus skews 0.507 ns and 0.610 ns against 6.733 ns;
- all 11,033 routable nets fully routed, zero routing errors, and zero DRC
  errors; and
- 6,168 LUTs, 5,703 flip-flops, 85 RAMB36 blocks, one RAMB18 block, and seven
  DSP blocks. The live bridge uses 543 LUTs, 335 flip-flops, and 64 RAMB36
  blocks.

Use only the matching packaged test artifact:

- package: `Multitini-One-PAL-C64-live-video-20260829.zip`;
- `FIRMWARE.BIN` size: 4,241,740 bytes;
- `FIRMWARE.BIN` SHA-256:
  `a44943738448e3114e58c03a30a2210270684ecf849911621e045ae5771f8703`;
- `FIRMWARE.BIN` CRC32: `01F764E0`; and
- free firmware-slot space: 10,372,788 bytes.

Bootgen readback verified the expected FSBL, routed bitstream, and two
application segments.

Before programming a live image, record here or in its package manifest:

- source commit and dirty/clean state;
- Vivado manifest timestamp;
- routed timing, CDC, DRC, and utilization summaries;
- bitstream, XSA, FSBL, diagnostic ELF, and `FIRMWARE.BIN` paths;
- `FIRMWARE.BIN` byte size, SHA-256, and CRC32; and
- package filename and SHA-256.

The fresh Vivado reports and matching Vitis/Bootgen artifact set passed their
pre-board gates. The board procedure below subsequently passed; the recorded
observations are in `docs/HARDWARE_RESULTS.md`.

## Program and load ROMs

Use the golden UART updater exactly as for the proven snapshot image, but point
it at the newly packaged live `FIRMWARE.BIN`. Preserve QSPI offset zero and
program only the firmware slot at `0x00200000`.

After the image boots, close the golden updater, open UART0 at 115200 baud 8N1,
and save the startup/status log. Close that terminal so the uploader can own the
port, then run from the repository root:

```powershell
python .\scripts\serial_c64_rom_upload.py --port COM3
```

Replace `COM3` if needed. Success must end with:

```text
[host] target reported C64 boot ok
```

The diagnostic bars remain the safe fallback until a complete PAL frame is
available and machine view is requested.

## Prove live delivery

1. Reopen UART0 at 115200 baud and save the status output.
2. Press `v` once to request machine video.
3. Confirm the centered 1528x1080 C64 image has 196-pixel black borders on both
   sides and no mixed lines, palette corruption, or horizontal tear.
4. Watch the ready-screen cursor for at least 30 seconds. It must continue to
   blink. A coherent but permanently frozen cursor fails this test.
5. Leave the machine view up for at least five minutes. DVI must remain locked,
   and animation must not show a horizontal seam or alternate between partial
   frames.
6. Press `v` repeatedly, waiting for the visible result after each press.
   Every bars/machine transition must be a whole-frame transition.
7. With machine view requested, press `r` once. The bridge must fall back to
   diagnostic bars after C64 readiness drops. Press `r` again; the C64 must
   reboot and machine view must return only after a new complete frame arrives.
8. Send `:reboot` and confirm return to the golden image.

A 60 fps lossless capture can provide stronger rate-conversion evidence. On a
changing C64 screen, adjacent identical output frames are expected because
60 Hz is faster than approximately 50.123 Hz. Every repeat must be a complete
frame; any horizontal boundary between old and new C64 content is a failure.
A phone-camera recording is useful for gross tearing but cannot by itself
measure the exact repeat cadence.

## Stop conditions

Stop and retain the UART log and display capture if any of these occurs:

- machine view appears before ROM validation and `CORE_READY`;
- a partial, shifted, or non-palette frame becomes visible;
- a source change occurs partway down the screen;
- reset leaves an old machine frame visible instead of failing back to bars;
- the cursor freezes while UART and DVI remain otherwise active;
- DVI loses lock; or
- golden reboot fails.

## Record the result

Record the exact firmware/package hashes, board revision, jumper state, display
make and mode, supply and ambient conditions, complete uploader/UART log,
duration, cursor behavior, source-toggle count, reset behavior, any capture
method, and golden-reboot result. Keep board observations separate from XSim
and vendor-tool results.
