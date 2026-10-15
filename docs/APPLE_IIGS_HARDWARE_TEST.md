# Apple IIgs hardware test

Install the complete `build/firmware-core-gs-m052/sd/` contents, preserving existing
media and settings. Update Multitini with its included `FIRMWARE.BIN` using
Golden B2. The matching supervisor and FSBL enable the new HP3 source video
port. Load the new IIgs core only after installing the matching firmware.
This package does not itself flash or test the board.

M0.5.2 corrects RTC control writes at `$C034` and fetches the native interrupt
dispatch ROM bytes at `$C071–$C07F` before committing those I/O cycles. Both
faults occur at stock speed. It also adds the raw floppy formats below.
The IWM corrections preserve fractional-timer carry and byte-latch expiry.
The package includes exact source captures, implementation receipts and final
reports. Use its `SHA256SUMS.txt` to identify the installed files. Complete
stock-speed and accelerated board checks before recording physical acceptance.

## User ROMs

The private hardware-test package stages these files from the owner's supplied
ROM collection in `multitini/cores/apple-iigs/roms/`:

| File | Bytes | Required |
| --- | ---: | --- |
| `rom3.rom` | 262144 | ROM3 selected |
| `rom1.rom` | 131072 | ROM1 selected |
| `char.rom` | 4096 | Both revisions |
| `adb3.rom` | 4096 | ROM3 selected |
| `adb1.rom` | 4096 | ROM1 selected |
| `hdd.rom` | 256 | Included GPL source-built bootstrap |

Put disks and PRAM in `multitini/cores/apple-iigs/media/`. ROMs and media are
separate sibling directories. If you used the first test bundle, move its ROM
files from `apple-iigs/media/roms/` to `apple-iigs/roms/`; the new firmware reads
only the corrected location. Keep disk images and PRAM files in place.

`scripts/prepare_apple_iigs_roms.py` accepts user-supplied binaries or auxiliary
hexadecimal byte files. With `--mame-zip`, it orders ROM3 banks exactly as the
pinned upstream Makefile: 341-0728, second half of 341-0748, first half of
341-0748. It also prepares ROM1 from 342-0077-b, ADB3 from 341s0632-2.bin,
and ADB1 from 341s0345.bin with the required leading 1 KiB zero block. The
344s0047.bin character ROM uses the lower seven bits of its first 2 KiB US bank,
duplicated to 4 KiB to match the pinned core. Explicit 2 KiB character inputs
are also duplicated. It performs no downloads;
private preparation outputs stay under `build/`; packaging copies the required
ROMs into the SD payload, with input/output hashes in its private ROM manifest.
The repository and delivered current source archive contain no Apple ROM bytes. Its
default output has the SD directory layout under `build/apple-iigs-user-roms/sd/`.

Select **Apple IIgs** in Cores. Missing ROMs leave the shared menu available;
use its System ROM action after supplying the required files. Choose ROM1 or
ROM3, stock CPU speed and ZipGS/host-only settings in core options. F12 and the
shared Select+Start gesture open the menu. Ctrl+F11 resets; Ctrl+OpenApple+F11 is a
cold reset. Command/OpenApple uses Alt; Option/SolidApple uses the Windows key.

## Disk and board checks

1. Start at stock speed with a user-owned HDD `.hdv`, `.po` or ProDOS `.2mg`.
   Mount HDD0/HDD1 independently. Confirm boot and a write/read-back cycle;
   verify the same file after eject and re-mount. Preserve 2MG headers. Enter
   several nested media folders, return to their parent with Back, and mount
   two images with the same basename in different folders. Confirm remembered
   full relative paths and recovery journals survive restart.
2. Mount an 800 KiB PO and an 800 KiB ProDOS 2MG in the 3.5-inch drive; mount
   a 140 KiB DOS-order DSK in the 5.25-inch drive. Also test native matching
   WOZ2 media in both drives. For a standalone floppy boot, eject the HDDs and
   the other floppy, mount the boot image and cold-reset. Let drive
   initialization finish. Check boot, seeks, side selection, write
   protection and write/read-back. Compare sector payloads after eject and
   re-mount; check 2MG headers and trailing bytes remain intact. Eject before
   removing power. Preserve `.gsraw` sidecars and other journal files if an
   interrupted write requires recovery. NIB is not supported.
3. Check the ROM control panel, save PRAM, power cycle and verify restoration
   separately with ROM1 and ROM3. The first unsaved PRAM starts invalid so
   the user's ROM installs defaults.
4. Exercise keyboard, relative mouse motion/buttons and a mapped gamepad.
   Verify release after menu entry, USB removal and core switching.
5. Check text, HGR/DHR and 320/640 SHR palettes, DOC audio and speaker audio.
   Check the entire left and right edge of SHR graphics, with a blue border
   and a different image background: no stray border column should enter the
   640-pixel image. Check 320 fill mode as well. Exercise all six DVI modes,
   picture effects and manual/Auto sizing.
6. Raise CPU speed through 3.58, 4.77, 7.16, 14.32, 28.64 and 57.27 MHz,
   using only the rates advertised by the installed image. Check software
   ZipGS changes, native I/O/disk slowdown, warm/cold reset and sustained
   gameplay. The menu's Accelerator card option selects ZipGS software controls
   or host-only control. ZipGS 100% selects the installed maximum. Effective
   throughput depends on memory misses, writes, interrupts and I/O.
7. Switch between IIgs and each existing core, then Appletini/Multitini via
   Golden. Confirm pending disk writes finish before the switch.
8. Use the System USB SD mount action. Confirm dirty media finishes before
   the PC sees the card, copy a test file, safely eject it on the PC and stop
   the modal. Confirm the core restarts and folders/media can be opened again.
   Also check disconnect, failed remount and recovery. See `USB_SD_MOUNT.md`.

Record the package SHA-256, board identity, ROM hashes, media and selected
settings with each result. Build/simulation success does not establish board
behavior or compatibility with every IIgs title.
