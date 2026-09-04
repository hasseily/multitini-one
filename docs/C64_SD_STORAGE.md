# C64 SD ROM and disk workflow

Historical hardware baseline: build `2026082E` implemented SD ROM loading and
read-only drive-8 D64 service. Its Vivado/Vitis/Bootgen build produced a
4,475,348-byte `FIRMWARE.BIN` with SHA-256
`079257d25af73abfc6c3f9372771660934942d9009655e5684bc78952d4cd118`.
The user-reported board test passed C64 and 1541 ROM loading, default D64
mounting, Decathlon loading and gameplay, and UART `j` joystick-port
selection. Build `2026082D` did not contain that storage feature.

Current source extends the baseline with exFAT, RTC-backed FAT timestamps,
writable normal D64 images, journal recovery, safe media barriers, and
read-only G64 images. ABI 0.8 build `20260912` additionally provides packed
four-byte load transfers and two cached tracks per disk format. Its attempted
1541 drive-warp option failed IEC loading on the board and was removed. ABI 0.9
build `20260913` replaces it with an opt-in, user-supplied fast serial ROM pair
and adds D64 `Mount & Run`. Fresh Vivado, exFAT-enabled Vitis, and package builds
pass and are installed on the SD card, but have not been tested on the board.
None of these changes
should be attributed to `2026082E` or another packaged historical image.

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

The repository contains its selected BASIC, KERNAL, and character ROMs as
tracked inputs, but the SD copies use the short canonical names above. It does
not provide a 1541 ROM, either member of the fast serial ROM pair, or a media
image. Supplying those files does not change their copyright or redistribution
status; see
[CONTENT_POLICY.md](CONTENT_POLICY.md).

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

On a C64 image, the application mounts the SD volume once and reports the
FatFs result on UART. It holds the C64 in reset while it reads the three
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
without stopping the running C64. Other D64 and G64 files are mounted only
when requested through UART.

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

## Ultimate-style Mount & Run

The F12 Media page keeps direct disk launch separate from the normal Drive 8
mount operation. Mount a D64 on `Drive 8`, then choose `Mount & Run` and select
a PRG from that disk's directory. Firmware validates the 35-, 40-, or 42-track
D64 directory chain, shows complete loadable BASIC-start (`$0801`) PRG entries,
revalidates the exact directory slot when selected, and follows the file's
sector chain into a bounded PRG buffer. Malformed, cyclic, stale, unclosed,
oversized, non-PRG, out-of-range, and non-BASIC-start entries are rejected
before the disk is ejected or any partial program is sent to the core.

After extraction, the normal dirty-media barrier flushes and temporarily
ejects the disk. The staged direct-RAM PRG loader resets the C64, writes the
program, and updates BASIC pointers without starting it. Firmware remounts the
same D64 and only then sends a separate `RUN` request, so immediate program
drive access cannot race the remount. A failed flush refuses the launch; a
failed remount leaves the program staged, does not type `RUN`, and leaves
`:diskretry` available. A successful retry remounts the D64 and then sends the
deferred RUN request.

This is the local equivalent of an Ultimate direct PRG launch; it is not a
faster IEC protocol. It does not accelerate typed `LOAD` commands or later
custom-loader traffic. G64 has no filesystem directory to extract and is
explicitly ineligible. Copy-protected disks and software that depends on its
own loader must continue through the ordinary D64/G64 Drive 8 path. Loose PRG
loading remains a separate `Program` action, and the optional `Fast serial
ROMs` profile remains independent of both paths.

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
drive-clock timing and is therefore the compatibility-preserving speedup.

The F12 `Core config` page provides `Fast serial ROMs`, default Off. Enabling it
requires both `roms/fast/kernal.bin` (exactly 8 KiB) and
`roms/fast/1541.bin` (exactly 16 KiB). These are user-supplied matching fast
serial ROMs, such as a properly licensed C64/1541 pair; Multitini does not ship
or download them. JiffyDOS is one compatible commercial pairing, but a purchase
or device/emulation license does not automatically grant redistribution rights;
obtain it from its authorized source and do not add the images to a Multitini
package. Firmware validates the complete pair before resetting the core. It
streams both ROMs while reset is asserted and persists the On setting only after
the C64 starts successfully. If the switch fails, the previous active ROM pair
is restored.

At later activation, a saved On setting is consulted before ROM loading. If
either fast file is missing or invalid, firmware reports the condition and
loads the stock `roms/kernal.bin` plus `roms/1541.bin` profile instead. The
stock BASIC and character paths do not change. Turning the option Off safely
reloads the complete stock pair. This design accelerates compatible serial disk
software through its ROM protocol without overclocking the 1541 in PL; programs
that bypass the ROM protocol retain their original behavior.

`CPU turbo` is a separate, default-Off C64 option. `Smart 2x` uses FPGA64's
extra 6510 RAM/ROM cycles while VIC, CIA, I/O, and 1541 timing stay at 1x. It is
independent of the selected ROM profile and automatically pauses during IEC
traffic and for 0.5 seconds afterward, matching upstream's compatibility
behavior. Software that depends on exact CPU timing may need CPU turbo Off.

## Serial ROM fallback

The existing uploader still sends only BASIC, KERNAL, and character ROMs as an
exact 20,480-byte XMODEM-1K/CRC bundle. The currently packaged `20260902` image
retains its historical 115200-baud UART setting:

```powershell
python .\scripts\serial_c64_rom_upload.py --port COM10 --baud 115200
```

Change `COM10` as needed. Current source changes UART0 to the
golden-compatible 921600 baud, but that takes effect only after a fresh
Vivado/XSA and Vitis platform/FSBL build. The script issues
`:c64rx 20480 0xCRC32`, defaults to the three tracked repository ROM files,
and retains its explicit file-path options. It does not upload `1541.bin` or a
disk image. After a serial fallback boot, use `v` if the Multitini standby screen
are still selected.

## Validation boundary

The first board result qualifies ROM autoload, 1541 startup, the default
read-only drive-8 D64 mount, read service during Decathlon loading and
gameplay, and runtime joystick-port selection on the tested card and SD setup.
That result is build `2026082E`; it does not qualify D64 writes,
journal/recovery behavior, exFAT, RTC-backed
timestamps, G64, safe reset/reboot barriers, arbitrary game compatibility, or
long-run SD reliability. Those are current source features awaiting a full
physical-card validation. The ABI 0.8 burst path and track caches are covered
by portable and HDL tests. The drive-warp implementation failed ordinary D64
loads on the board and was removed rather than being presented as compatible.
The replacement fast-pair selection, settings migration, and `Mount & Run`
path are covered by portable tests and the ABI 0.9 `20260913` Vivado,
Vitis, and package build. Board tests are still needed with stock, missing,
invalid, and properly licensed matching fast ROM pairs, plus mounted-D64
programs that access the disk after startup. CPU turbo remains orthogonal.
