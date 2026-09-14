# C64 SD ROM and disk workflow

This describes the accepted ARM firmware 20260917 with C64 FPGA 20260915,
ABI 0.10. The current package includes the selected stock C64/1541 ROMs and
the matching fast pair. See [CURRENT_RELEASE.md](CURRENT_RELEASE.md) for
installation and [C64_LAUNCH_20260917.md](C64_LAUNCH_20260917.md) for the
launch correction and board evidence.

Storage supports exFAT, RTC-backed FAT timestamps, journaled writes to normal
D64 images, safe media barriers, and read-only G64 images. The board-confirmed
scope and remaining limits are recorded at the end of this document.

## Card layout

Keep golden-updater files at the SD-card root and put each machine's package,
ROMs, and media together under `/multitini/cores`:

```text
/
|-- FIRMWARE.BIN                 optional golden-update input
|-- FIRMWARE.OK                  renamed/verified golden-update image
`-- multitini/
    `-- cores/
        `-- c64/
            |-- core.cfg
            |-- c64-pal.bin
            |-- c64-ntsc.bin
            |-- roms/
            |   |-- basic.bin        exactly 8,192 bytes
            |   |-- kernal.bin       exactly 8,192 bytes
            |   |-- characters.bin   exactly 4,096 bytes
            |   |-- 1541.bin         exactly 16,384 bytes
            |   `-- fast/            optional atomic fast serial pair
            |       |-- kernal.bin   exactly 8,192 bytes
            |       `-- 1541.bin     exactly 16,384 bytes
            `-- media/
                |-- drive8.d64       optional default disk
                |-- other.d64        normal writable D64
                |-- protected.d64    D64 with error-info tail; read-only
                |-- other.g64        read-only G64
                |-- program.prg      directly loadable C64 program
                `-- cartridge.crt    directly loadable cartridge
```

The firmware uses FatFs volume `0:`. The canonical ROM and default-media paths
are:

- `0:/multitini/cores/c64/roms/basic.bin`
- `0:/multitini/cores/c64/roms/kernal.bin`
- `0:/multitini/cores/c64/roms/characters.bin`
- `0:/multitini/cores/c64/roms/1541.bin`
- `0:/multitini/cores/c64/roms/fast/kernal.bin` (optional fast pair)
- `0:/multitini/cores/c64/roms/fast/1541.bin` (optional fast pair)
- `0:/multitini/cores/c64/media/drive8.d64`

Create the directories and copy the content files yourself. The firmware does
not format the card. It does not rename or alter unrelated content. When a
normal D64 is written, firmware may create a temporary sidecar named after the
image with `.mtj` appended, such as `other.d64.mtj`. That file is an internal
write-ahead journal, not mountable media. It is removed after a complete
commit and retained for recovery if an operation is interrupted.

The current package provides the selected stock ROMs and matching `c64fast`
pair using the canonical names above. Their exact inputs and hashes are in
[current_release.json](../provenance/current_release.json). No game or disk
image is included. The ROMs retain their recorded `NOASSERTION` rights status;
see [CONTENT_POLICY.md](CONTENT_POLICY.md).

## FatFs and exFAT

The current Vitis generator selects Xilinx `xilffs` 5.5 and enables both long
file names and exFAT before building the platform. Vitis 2025.2 leaves some
generated and exported configuration copies stale, so the generator
synchronizes both `xilffs_config.h` copies, `CMakeCache.txt`,
`cmake_lib_configs.txt`, and `bsp.yaml`. It also applies the four FatFs R0.16
patch-2 media-validation bounds checks to the generated R0.15a source and
retains the bounded no-card handling inherited from Appletini.

A reused Vitis platform is accepted only when its semantic stamp identifies
the LFN, exFAT, media-validation, RTC timestamp-hook, and bounded no-card
features. A platform built before these changes must be regenerated; merely
rebuilding the application is insufficient.

The generated FatFs `get_fattime()` calls a weak application hook. Multitini's
strong hook uses a cached PCF8563 RTC reading, refreshed once per second by the
main loop. Invalid or voltage-low readings do not replace the last good value.
Before the first valid reading, or when I2C/RTC initialization fails, file
writes use FatFs' fixed `2010-01-01 00:00:00` fallback.

## Automatic startup

The supervisor boots to standby unless `Autoboot current core` is enabled.
Select C64 through F12 to activate it. The application reports SD status on
UART and holds the C64 in reset while it reads the three
required system ROMs at their exact sizes, rejects uniform or invalid content,
and streams them into the FPGA. A successful load releases reset and turns off
the Multitini standby screen, so the C64 display appears directly.

If the SD card, a required system ROM, validation, or FPGA streaming fails,
the C64 remains in reset and the Multitini standby screen remains visible. UART and USB still
start, and the serial three-ROM upload remains available as a recovery path.

The 1541 ROM is separate from the required 20,480-byte C64 system-ROM bundle.
When `1541.bin` is present and valid, firmware enables drive 8. A missing or
invalid `1541.bin` does not prevent the C64 itself from booting, but drive
mounting remains unavailable; `:dir` and `:disk` can still inspect the media
directory and empty status.

After the drive service is ready, firmware automatically tries to mount
`0:/multitini/cores/c64/media/drive8.d64`. Before reading that image, it validates and
replays any complete checksummed journal left by an interrupted write. The
default file is optional: a missing or invalid image leaves drive 8 empty
without stopping the running C64. Use the F12 Media page's `Drive 8` action
or UART to mount another D64 or G64. At a game's disk-change prompt, mount
the replacement through `Drive 8` without restarting the game.

## UART disk commands

Enter these extended commands in the running firmware's serial console:

| Command | Result |
| --- | --- |
| `:dir` | List recognized `.d64`, `.g64`, `.prg`, and `.crt` files immediately inside `0:/multitini/cores/c64/media`. |
| `:mount FILE.d64` | Safely eject the current disk, recover a complete journal if present, validate the named D64, and mount a normal data-only image writable or an error-tail image read-only as drive 8. |
| `:mount FILE.g64` | Safely eject the current disk, validate the named G64, and mount it read-only as drive 8. |
| `:disk` | Show the current drive-8 file, type, byte size, generation, writable/read-only policy, FPGA mounted/dirty/eject state, RAM-dirty state, and service-fault state. |
| `:eject` | Drain pending media work and eject only after the FPGA reports a stable unmounted and clean state. A failed persistence step retains the dirty image for retry. |
| `:diskretry` | Clear a retryable media-transfer fault, let the FPGA reissue its held dirty write, and retry a pending post-reset remount. |

`:mount` rejects names containing `/`, `\`, or `:` and accepts only D64 or G64
extensions. It never searches the SD-card root or another core's directory.

Core reset, PRG/CRT content changes, and `:reboot` use the same media
preparation path. Firmware first flushes and ejects the current disk. Reset or
reboot is refused or cancelled when that barrier cannot complete, so the
operation does not silently discard a dirty D64. After a normal core reset or
content load, firmware remounts the prior image; `:diskretry` remains available
if that remount fails.

## Mount & Run

The F12 Media page keeps direct disk launch separate from the normal Drive 8
mount operation. Mount a D64 on `Drive 8`, then choose `Mount & Run` and select
a PRG from that disk's directory. Firmware validates the 35-, 40-, or 42-track
D64 directory and complete file chains, rechecks the exact selected entry,
and rejects malformed, cyclic, stale, unclosed, oversized, or out-of-range
files before changing the mounted disk. Valid non-BASIC PRGs are included
and marked `[load only]`.

| Key | Action |
| --- | --- |
| Enter | Load through the emulated drive and start an eligible BASIC program. Non-BASIC entries use native LOAD only. |
| Right | Type native `LOAD"<exact name>",8,1` only. Type `RUN` afterward for a BASIC program when needed. |
| Left | Use the previous direct-RAM shortcut for an eligible BASIC program. This bypasses the initial native LOAD and its side effects. |

For Enter on a BASIC-start (`$0801`) file, firmware stages an original tiny
BASIC launcher containing `10 LOAD"<exact name>",8,1`. It reserves the
selected program's memory extent, resets the C64 and drive, remounts the D64,
and starts that launcher. BASIC's in-program LOAD starts the newly loaded
program. There is no guessed SYS address or delayed RUN queued after LOAD.
Names that cannot be typed exactly, or that ambiguously select another
directory entry, are refused by the native path.

All paths use the dirty-media flush/eject barrier before reset and remount.
A failed flush refuses the launch; a failed remount leaves the staged program
stopped and retains `:diskretry`. Firmware 20260917 preserves pending disk
requests when starting the staged program. The correction and prior behavior
are documented in [20260915](C64_LAUNCH_20260915.md),
[20260916](C64_LAUNCH_20260916.md), and
[20260917](C64_LAUNCH_20260917.md).

G64 has no D64 PRG directory browser; use the ordinary `Drive 8` mount and
the software's normal loading instructions. Loose PRG loading remains a
separate `Program` action. `Fast serial ROMs` can accelerate compatible
native LOAD operations, including the default BASIC launcher.

## D64 write and recovery rules

The firmware reads the complete selected D64 into PS DDR, validates its exact
size, and serves 256-byte data sectors to the FPGA drive. The accepted image
sizes are:

| Data sectors | Normal data-only D64 | D64 with one error byte per sector |
| ---: | ---: | ---: |
| 683 | 174,848 bytes | 175,531 bytes |
| 768 | 196,608 bytes | 197,376 bytes |
| 802 | 205,312 bytes | 206,114 bytes |

The three normal data-only forms are writable. The three error-tail forms are
mounted read-only because updating sector data without defined error-tail
semantics would make the combined image ambiguous. Read-only media rejects
write requests rather than modifying the in-memory copy.

For a writable image, the FPGA sends an exact reverse byte stream for the
changed block range. Firmware validates the range before consuming bytes,
requires the final marker on exactly the last expected byte, updates the RAM
image, and persists the entire D64 before acknowledging success. Persistence
uses this order:

1. Write and sync a sidecar containing a versioned header, complete D64
   payload, CRC-32, and matching trailer.
2. Rewrite and sync the ordinary D64 while the complete journal remains.
3. Remove the journal only after the target sync succeeds.

On the next mount, a complete journal with matching sizes, complements,
trailer, and CRC is replayed and then removed. An incomplete or corrupt journal
is rejected and preserved for diagnosis; it is never treated as a valid disk
image. The design does not depend on `f_rename()` being power-loss atomic.

The FPGA holds dirty/eject-pending state until the matching write completion
succeeds. Firmware reads the media state twice for a stable clock-domain
snapshot before treating eject as complete. A failed journal write, target
write, sync, completion, or eject therefore leaves an explicit retry path
instead of forgetting the mounted image.

## G64 read scope

Current source accepts strict version-zero `GCR-1541` images whose track table,
track lengths, extents, and fixed speed entries validate. Fixed speed zones 0,
1, 2, and 3 are carried with each normalized half-track. A speed-table value
that points to a variable per-byte speed map is rejected with an explicit
unsupported-speed-map error.

G64 tracks are normalized into the fixed 8 KiB transfer buffer used by the
direct-GCR drive reader. Missing track-table entries produce an empty track.
G64 is read-only: no reverse write stream or file save-back is accepted.

## Disk-access acceleration and compatibility

ABI 0.8 uses one 32-bit MMIO write for each four direct-load bytes. The shell
holds that word and drains its bytes in order through the existing
clock-domain mailbox, so the 1541-facing byte protocol and media request sizes
do not change. An ABI 0.7 package still uses the original one-byte MMIO path.

Both D64 and G64 adapters retain the two most recently completed normalized
8 KiB tracks in FPGA block RAM. A revisit copies the track locally rather than
requesting it from the ARM and SD card again. Replacement is round-robin. A
D64 write invalidates the cached copy of that track, and mount, eject, failed
loads, and partial loads never publish a cache entry. The cache does not alter
drive-clock timing.

The F12 `Core config` page provides `Fast serial ROMs`, default Off. Enabling it
requires both `roms/fast/kernal.bin` (exactly 8 KiB) and
`roms/fast/1541.bin` (exactly 16 KiB). The repository builds a matching pair
in `c64fast/bin` from the stock ROMs; it uses the parallel drive cable and the
drive RAM expansion that the profile enables ([C64_FAST_DISK.md](C64_FAST_DISK.md)).
Alternative ROM pairs are not covered by the current board acceptance.
Firmware validates the complete pair before resetting the core. It
streams both ROMs while reset is asserted and persists the On setting only after
the C64 starts successfully. If the switch fails, the previous active ROM pair
is restored.

At later activation, a saved On setting is consulted before ROM loading. If
either fast file is missing or invalid, firmware reports the condition and
loads the stock `roms/kernal.bin` plus `roms/1541.bin` profile instead. The
stock BASIC and character paths do not change. Turning the option Off safely
reloads the complete stock pair. On ABI 0.10, the fast profile also enables
the parallel drive cable and 8 KiB of drive RAM; Off disables those features.
This accelerates compatible ROM-mediated loads without overclocking the 1541.
Custom loaders that bypass the ROM protocol are not accelerated.

`CPU turbo` is a separate, default-Off C64 option. `Smart 2x` uses FPGA64's
extra 6510 RAM/ROM cycles while VIC, CIA, I/O, and 1541 timing stay at 1x. It is
independent of the selected ROM profile and automatically pauses during IEC
traffic and for 0.5 seconds afterward, matching upstream's compatibility
behavior. Software that depends on exact CPU timing may need CPU turbo Off.

## Serial ROM fallback

The existing uploader still sends only BASIC, KERNAL, and character ROMs as an
exact 20,480-byte XMODEM-1K/CRC bundle. Current UART0 runs at the
golden-compatible 921600 baud; UART1 runs at 115200 baud. For UART0:

```powershell
python .\scripts\serial_c64_rom_upload.py --port COM10 --baud 921600
```

Change `COM10` to the connected port. The script issues
`:c64rx 20480 0xCRC32`, defaults to the three tracked repository ROM files,
and retains its explicit file-path options. It does not upload `1541.bin` or a
disk image. After a serial fallback boot, use `v` if the Multitini standby screen
is still selected.

## Validation boundary

Firmware 20260917 Mount & Run reaches Ultima II's second-disk prompt and
supports the Player-disk swap on the board. The owner confirms a clean Player
image works with fast ROMs enabled and confirms fast loading. The earlier
`LOAD ERROR!` was attributed by the owner to the old corrupted Player image;
its corruption cause is unconfirmed. Earlier ROM-autoload, D64 gameplay, and input results
remain in [HARDWARE_RESULTS.md](HARDWARE_RESULTS.md).

Portable and HDL tests cover parsing, transfer framing, caches, media barriers,
and journal logic. These passes and the reported gameplay do not establish
exhaustive power-loss recovery, every writable game workload, long-run SD
reliability, all G64 images, alternative ROM pairs, or CPU turbo compatibility.
The old drive-clock warp experiment was removed and is not a current option.
