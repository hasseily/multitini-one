# Apple IIgs hardware test

Install the complete `build/firmware-smartport-m060/sd/` contents, preserving existing
media and settings. Update Multitini with its included `FIRMWARE-MULTITINI.BIN` using
Golden B2. Use the package's matching supervisor, FSBL and core configuration.
M0.6.0 adds eight-drive SmartPort with extended IIgs calls and banked fast
DMA. Install its ARM firmware, IIgs FPGA image, `core.cfg` and 2304-byte
`roms/hdd.rom` together. The other seven FPGA images are retained.
Follow the [SmartPort hardware checks](APPLE_IIGS_SMARTPORT.md) as well as
the existing checks below. The USB SD recovery, screenshot geometry and
shared USB polling fixes from M0.5.7-M0.5.9 remain included.

After installing M0.6.0, check the native GS border in SHR: 42 pixels on each
side and 24 source lines above and below the complete 640x200 image. Changing
the guest border color must affect all four sides. Check both NTSC and PAL,
and return to text mode to verify its existing placement inside the frame.
Capture both filtered video and full screen. At Auto size, the video PNG is
1448x992 in 1920x1080 and 1680x1050 output modes; it is 724x496 in the other
four modes. Full-screen PNGs match the selected display resolution. Check the
first/last active image columns, all four borders, menu overlays and manual
1x through 4x clipping against the display.
These screenshot checks still need physical acceptance; the M0.5.7 Finder
boot confirmation below remains valid for that installed firmware.

Verify the selected IIgs image on the actual card. Use the size and CRC32
from the new package's `apple-iigs/core.cfg` and confirm the same values in
the load log. UART `s` must report core `49494753`, ABI `00000016` and build
`4D000600`. Package hashes alone do not identify the file loaded by the board.

## Earlier hardware evidence

The following results apply to their named releases, not to M0.6.0. The old
M0.5.7 package's `core.cfg` named
`apple-iigs-native.bin`, size `4045568`, CRC32 `D3BAF34E`. The load log must
have matched that size and CRC, core `49494753`, ABI `00000016` and build
`4D000505`. On 2026-10-16 a copyright-screen freeze came
from an old card image: build `4D000400`, CRC32 `5E618146`. It also froze under
M0.5.6. Replacing both the FPGA file and its matching `core.cfg` prevents that
old image from being selected. Package hashes alone do not identify the file
loaded by the board.

After repairing the card and installing M0.5.7 with the named firmware file,
the load log confirmed CRC32 `D3BAF34E` and the owner confirmed that Finder
boots. The shared USB polling change remains enabled. This confirms boot
recovery; it is not a new boot-time measurement.

The M0.5.6 ARM image was installed through Golden and measured on the owner's
board. Copying the package does not itself apply the firmware update.

M0.5.6 paces USB polling during IIgs disk transfers and corrects the USB clock
conversion. It reuses all eight FPGA images and the CPU1 video worker. With the
owner's System 6 HDD, final disk completion falls from 36.57 to 4.74 seconds
at 57 MHz; Stock and 14 MHz measure 24.55 and 5.67 seconds with the fix.
These are reset-to-final-HDD timestamps. On 2026-10-16 the owner separately
confirmed a usable Finder in roughly five seconds at 57 MHz, with working
mouse/keyboard and normal window drawing: "Yes, fast and working normally".
See the [measurement details](APPLE_IIGS_ACCELERATION.md).

M0.5.5 changes ARM firmware only and retains all eight qualified FPGA images,
including the M0.5.4 IIgs image. Accelerator card **None** forces stock speed
and disables the CPU speed control. Changing between None and ZipGS saves
media and restarts the IIgs, clearing prior software speed, cache, unlock and
delay overrides. Saved menu delay choices are reapplied. Returning to ZipGS
starts at Stock; select a faster rate afterward without restarting.

The acceleration rework on branch `iigs-accel-fixes` changes only the IIgs FPGA
image and its block design; firmware, ABI and PS initialization are unchanged.
Its bundle is `build/firmware-core-gs-m054b.zip`: install its `sd/` contents
the same way as the M0.5.3 package below. It carries the M0.5.3 firmware.
While accelerated, E0/E1 memory, shadowed screen writes and I/O no longer wait
for the 1 MHz sync, DDR writes are posted, and the IWM accepts mode writes as a
real IWM does. See the [acceleration record](APPLE_IIGS_ACCELERATION.md). Stock
speed keeps its native timing. Step 6 covers the accelerated checks.

M0.5.3 corrects three accelerator slowdowns: an empty floppy drive's motor
latch, IRQ dispatch ROM reads counted as paddle accesses, and fast shadow-RAM
reads classified as slow. Mounted floppy timing and shadow writes stay native.
Fresh settings default IRQ delay to Off. Existing saved choices are preserved;
set IRQ delay to Off in Core options for the accelerated performance checks.

The retained M0.5.2 fixes correct RTC control writes at `$C034` and fetch the native interrupt
dispatch ROM bytes at `$C071–$C07F` before committing those I/O cycles. Both
faults occur at stock speed. It also adds the raw floppy formats below.
The IWM corrections preserve fractional-timer carry and byte-latch expiry.
The package includes exact source captures, implementation receipts and final
reports. Use its `SHA256SUMS.txt` to identify the installed files. Complete
stock-speed and accelerated board checks before recording physical acceptance.

## User ROMs

The private hardware-test package stages the owner's supplied system ROMs
and the source-built SmartPort ROM in `multitini/cores/apple-iigs/roms/`:

| File | Bytes | Required |
| --- | ---: | --- |
| `rom3.rom` | 262144 | ROM3 selected |
| `rom1.rom` | 131072 | ROM1 selected |
| `char.rom` | 4096 | Both revisions |
| `adb3.rom` | 4096 | ROM3 selected |
| `adb1.rom` | 4096 | ROM1 selected |
| `hdd.rom` | 2304 | Included source-built SmartPort slot and expansion ROM |

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
ROM3, stock CPU speed and ZipGS or None in core options. F12 and the
shared Select+Start gesture open the menu. Ctrl+F11 resets; Ctrl+OpenApple+F11 is a
cold reset. Command/OpenApple uses Alt; Option/SolidApple uses the Windows key.

## Disk and board checks

1. Start at stock speed with a user-owned HDD `.hdv`, `.po` or ProDOS `.2mg`.
   Mount the eight hard disks independently. Confirm boot and a write/read-back cycle;
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
   In IIgs Core options, change Mouse sensitivity through 25%, 50%, 75%, 100%,
   150%, 200%, 300% and 400%. Check immediate movement changes, fine motion at
   25%, button dragging and persistence after restarting. The default is 100%.
   This setting affects the IIgs pointer; the shared menu cursor is unchanged.
   Verify release after menu entry, USB removal and core switching.
5. Check text, HGR/DHR and 320/640 SHR palettes, DOC audio and speaker audio.
   Check the entire left and right edge of SHR graphics, with a blue border
   and a different image background: no stray border column should enter the
   640-pixel image. Check 320 fill mode as well. Exercise all six DVI modes,
   picture effects and manual/Auto sizing.
6. Select None and confirm a restart, a Stock speed label and a disabled speed
   control. Reboot with an older saved None/nonzero-speed configuration and
   confirm it is normalized to Stock. Return to ZipGS and confirm another
   restart at Stock. Previously software-selected ZipGS speed, cache disable,
   register unlock and delay settings must not survive these transitions;
   the saved menu delay choices apply instead. Remembered media must remount.
   With ZipGS selected, raise CPU speed through 3.58, 4.77, 7.16, 14.32, 28.64 and 57.27 MHz,
   using only the rates advertised by the installed image. Check software
   ZipGS changes, native I/O/disk slowdown, warm/cold reset and sustained
   gameplay. Speed-only changes remain live. ZipGS 100% selects the installed maximum. Effective
   throughput depends on memory misses, writes, interrupts and I/O.
   The Multitini CPU acceleration menu needs no GS/OS driver. Optional ZipGS
   control software provides an additional interface inside GS/OS, as described
   in the [original manual](https://www.whatisthe2gs.apple2.org.za/files/ZipGSX/Manual/ZIPGSX_Manual.pdf).
   Compare Stock and maximum during an HDD-only session with both floppies
   ejected, including Finder startup, window opening/redraw and a CPU-heavy
   workload. Record all delay settings and the observed speedup. The owner
   reports 59/37/35-second System 6.0.1 boots at Stock/14/57 MHz on M0.5.4;
   M0.5.6 identifies and removes about 32 seconds of ARM USB polling from
   that workload, as recorded above. At maximum
   speed also check DOC sound effects and music, the boot beep, paddles,
   keyboard and mouse, the Control Panel, 3.5-inch and 5.25-inch boot, read and
   write, and switching speed while a program runs. Report any program that
   misbehaves only at maximum speed, with its delay settings.
7. Switch between IIgs and each existing core, then Appletini/Multitini via
   Golden. Confirm pending disk writes finish before the switch.
8. Use the System USB SD mount action. Confirm dirty media finishes before
   the PC sees the card, copy a test file, safely eject it on the PC and stop
   the modal. Confirm the core restarts and folders/media can be opened again.
   Also check disconnect, failed remount and recovery. See `USB_SD_MOUNT.md`.

Record the package SHA-256, board identity, ROM hashes, media and selected
settings with each result. Build/simulation success does not establish board
behavior or compatibility with every IIgs title.
