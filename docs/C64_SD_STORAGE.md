# C64 SD ROM and D64 workflow

Status: implemented in the current `2026082E` firmware and covered by host
tests. Its matching Vivado/Vitis/Bootgen build passes, producing a 4,475,348
byte `FIRMWARE.BIN` with SHA-256
`079257d25af73abfc6c3f9372771660934942d9009655e5684bc78952d4cd118`.
It has not yet completed an SD-card hardware test. The `2026082D` packaged
firmware does not contain this feature.

## Card layout

Keep golden-updater files at the SD-card root and put machine content under
the lowercase `/cores` directory:

```text
/
|-- FIRMWARE.BIN                 optional golden-update input
|-- FIRMWARE.OK                  renamed/verified golden-update image
`-- cores/
    `-- c64/
        |-- roms/
        |   |-- basic.bin        exactly 8,192 bytes
        |   |-- kernal.bin       exactly 8,192 bytes
        |   |-- characters.bin   exactly 4,096 bytes
        |   `-- 1541.bin         exactly 16,384 bytes
        `-- media/
            |-- drive8.d64       optional default disk
            `-- other.d64        optional manually mounted disks
```

The firmware uses FatFs volume `0:`. The canonical paths are therefore:

- `0:/cores/c64/roms/basic.bin`
- `0:/cores/c64/roms/kernal.bin`
- `0:/cores/c64/roms/characters.bin`
- `0:/cores/c64/roms/1541.bin`
- `0:/cores/c64/media/drive8.d64`

Create these directories and copy the files yourself. Firmware does not
format the card or create, delete, or rename content under `/cores`. Long-file
name support is enabled so `characters.bin` and ordinary D64 names work.
Unrelated root files and directories outside `/cores` are ignored. Golden
continues to inspect only its root update names, so the machine-content tree
coexists with `FIRMWARE.BIN` and `FIRMWARE.OK`.

The repository contains its selected BASIC, KERNAL, and character ROMs as
tracked inputs, but the SD copies use the short canonical names above. It does
not provide a 1541 ROM or D64 image. Supplying those files does not change their
copyright or redistribution status; see [CONTENT_POLICY.md](CONTENT_POLICY.md).

## Automatic startup

On a C64 image, the application mounts the SD volume once and reports the
FatFs result on UART. It holds the C64 in reset while it reads the three
required system ROMs at their exact sizes, rejects uniform/invalid content,
and streams them into the FPGA. A successful load releases reset and turns off
the diagnostic color bars, so the C64 display appears directly.

If the SD card, a required system ROM, validation, or FPGA streaming fails,
the C64 remains in reset and the color bars remain visible. UART and USB still
start, and the serial three-ROM upload remains available as a recovery path.

The 1541 ROM is separate from the required 20,480-byte C64 system-ROM bundle.
When `1541.bin` is present and valid, firmware enables drive 8. A missing or
invalid `1541.bin` does not prevent the C64 itself from booting, but drive
mounting remains unavailable; `:dir` and `:disk` can still inspect the media
directory and empty status.

After the drive service is ready, firmware automatically tries to mount
`0:/cores/c64/media/drive8.d64`. The file is optional: a missing or invalid
default disk leaves drive 8 empty without stopping the running C64. Other D64
files are mounted only when requested through UART.

## UART disk commands

Enter these extended commands in the running firmware's serial console:

| Command | Result |
| --- | --- |
| `:dir` | List `.d64` files immediately inside `0:/cores/c64/media`. |
| `:mount FILE.d64` | Eject the current disk, read and validate the named file from the media directory, and mount it read-only as drive 8. Pass a file name, not a path. |
| `:disk` | Show the current drive-8 file, byte size, generation, and read-only state, or report that the drive is empty. |
| `:eject` | Eject the current drive-8 disk. |

`:mount` rejects names containing `/`, `\`, or `:` and accepts only a D64
extension. It never searches the SD-card root or another core's directory.

## Read-only D64 scope

The firmware reads the complete selected D64 into PS DDR, validates its exact
size, and serves 256-byte data sectors to the FPGA drive. The accepted image
sizes are:

| Data sectors | Without error-info tail | With one error byte per sector |
| ---: | ---: | ---: |
| 683 | 174,848 bytes | 175,531 bytes |
| 768 | 196,608 bytes | 197,376 bytes |
| 802 | 205,312 bytes | 206,114 bytes |

This first service is deliberately read-only. It advertises a read-only mount,
rejects write requests, and never writes a modified image back to SD. Only
drive 8 and D64 media are in scope; G64, D81, multiple drives, save-back, and
filesystem mutation are not qualified by this implementation.

## Serial ROM fallback

The existing uploader still sends only BASIC, KERNAL, and character ROMs as an
exact 20,480-byte XMODEM-1K/CRC bundle:

```powershell
python .\scripts\serial_c64_rom_upload.py --port COM10
```

Change `COM10` as needed. The script issues `:c64rx 20480 0xCRC32`, defaults to
the three tracked repository ROM files, and retains its explicit file-path
options. It does not upload `1541.bin` or a D64. After a serial fallback boot,
use `v` if the diagnostic color bars are still selected.

These source, host-test, and vendor-build results are not a board claim. The
matching packaged firmware must still be installed and tested with the SD card
before ROM autoload, 1541 startup, default D64 mounting, disk commands, and
read service can be marked hardware-qualified.
