# C64 Snapshot Hardware Test

Status: engineering firmware built, packaged, and passed on the board on
2026-08-29. This is the preserved frozen-frame predecessor to the live bridge.

## What this image tests

This is the first ROM-upload and machine-video test after the independent DVI,
audio, UART, and golden-reboot diagnostics passed on hardware. It can:

- receive the selected C64 BASIC, KERNAL, and character ROMs over UART0;
- validate the exact 8192, 8192, and 4096-byte sizes, reject uniform data,
  verify the complete 20 KiB transfer with CRC32, and stream it to the FPGA;
- keep the C64 in reset if any receive, validation, stream, or ready check
  fails; and
- capture one complete 382x270 PAL C64 frame and display a frozen 4x snapshot
  at 1528x1080, centered with 196-pixel black side borders.

This is not yet a live C64. The displayed frame is intentionally frozen.
Keyboard, joystick, USB input, live video, storage, and SID audio are not
connected. Command `a` still controls the independent fixed diagnostic tone.

The preserved snapshot package itself did not contain C64 ROMs. The current
repository tracks the exact three-file test set selected by the repository
owner, and the uploader uses those paths by default.

## Exact firmware

- File: `build/firmware-c64-snapshot/FIRMWARE.BIN`
- Size: 4,241,740 bytes
- SHA-256:
  `db5128217b7510d265a0745089dde4642d00c936fd8325caa7bd3b61de842723`
- CRC32: `0DDF77E5`
- Firmware-slot capacity: `0x00DF0000` bytes
- Free space in slot: 10,372,788 bytes
- Shell build ID: `fd554dbf`
- Source: dirty engineering snapshot at base commit
  `fd554dbf46e22514bb132f3dc063ee0a892bdcc6`

## Program through golden UART

At the golden `[SER] cmd>` prompt on UART0 at 921600 baud, run:

```powershell
python ..\appletini-one\scripts\serial_firmware_update.py `
  .\build\firmware-c64-snapshot\FIRMWARE.BIN --port COM3 --no-wait-after
```

Replace `COM3` if needed. Do not write this image at QSPI offset zero. After
the transfer, close the updater and open UART0 at 115200 baud, 8N1.

The corrected status output should identify:

```text
magic=4D543031 abi=00000003 shell=4D544F4E build=FD554DBF
core=43363420 ... control=0000000F
```

The exact `status` and `error` values can change as commands run. In this image,
the `VIDEO_ACTIVE` status bit reports the diagnostic-pattern request, not which
source is physically visible.

## Upload the three ROMs

Install pyserial if it is not already available:

```powershell
python -m pip install pyserial
```

Close the terminal so the uploader can own the COM port, then run from the
repository root:

```powershell
python .\scripts\serial_c64_rom_upload.py --port COM3 --baud 115200
```

Use `--basic`, `--kernal`, and `--character` only to override those defaults.

The uploader prints the path, size, and SHA-256 of each supplied file, plus the
combined CRC32 and SHA-256. Success ends with:

```text
[host] target reported C64 boot ok
```

Failure leaves the C64 in reset. Re-run the uploader after correcting the
reported problem; no partial ROM set is accepted.

## Capture the first C64 frame

After the upload succeeds:

1. Wait about two seconds for the PAL C64 to reach a stable screen.
2. Reopen UART0 at 115200 baud.
3. Press `v` once.

Pressing `v` clears the diagnostic-pattern request. The bridge waits for a new,
complete 382x270 C64 frame, freezes it, and changes source only at 1080p
vertical blank. Expected output is a centered, static C64 screen with black
side borders. The diagnostic bars remain visible if no valid complete frame is
available.

Press `v` again to return to the diagnostic bars. A later request for machine
video requires a newly captured frame; stale or partly written frame memory is
never intentionally displayed.

## Recorded result

The uploader completed, the target reported C64 boot success, and the core
reached its PAL ready screen. The centered frozen frame was coherent, switching
between bars and machine view worked, and `:reboot` returned to golden. The
user reported the complete test working perfectly.

For a repeat test, record:

- the three ROM SHA-256 values printed by the uploader;
- the complete uploader and UART result;
- whether `CORE_READY` appears after upload;
- display make/mode and whether the centered static frame is coherent;
- whether `v` returns cleanly to bars and captures a new frame again; and
- whether `:reboot` still returns to golden.

Do not call live C64 video, keyboard, joystick, storage, or SID audio working
from this test. Those are later gates.
