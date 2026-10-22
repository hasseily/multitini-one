# Apple IIgs SmartPort

M0.6.0 adds eight virtual SmartPort disk drives in slot 7. The slot ROM
provides the ProDOS entry point and standard and extended IIgs SmartPort
calls. Existing 3.5-inch and 5.25-inch floppy emulation remains separate.

## Storage and compatibility

The eight disk units have stable numbers 1 through 8. An empty drive remains
a device with no media; it does not renumber the later drives. Controller
status reports eight devices. Each drive reports its own capacity, online
state and write protection through status and the device information block.
With all eight drives empty, the slot card is hidden so native floppy boot
retains its existing priority. Mounting any hard disk exposes all eight units.

Disk images use 512-byte blocks. Raw HDV and ProDOS-order PO images and
ProDOS-order 2MG containers are supported. SmartPort addressing does not
change the limits of the filesystem inside an image. In particular, a larger
virtual device does not enlarge a ProDOS volume.

Standard calls use a bank-zero parameter list and buffer and a 24-bit block
number. Extended calls use four-byte parameter/buffer pointers and a 32-bit
block number; the memory address must fit the IIgs 24-bit address space.
Both call forms follow Apple's emulation-mode convention: E=1, direct page
zero and data bank zero. Native GS software switches to this convention
before calling the slot firmware. The inline parameter-pointer size is two
bytes for standard calls and four bytes for extended calls.
Standard block buffers must fit wholly within bank zero. Extended buffers
may cross banks, but must fit wholly within the 24-bit address space.
Calls use at most 35 stack bytes, including the caller's JSR and helper frames.

ReadBlock and WriteBlock transfer one block through the existing fast DMA
engine. Status returns controller/device status or the device information
block. Init and Control reset are supported. Format validates mounted,
writable media and returns success for the already formatted block store;
it does not erase the image or create a filesystem. Unsupported character
device commands and device-specific control/status codes return an error.

The Multitini media menu mounts the eight hard disks. Old saved settings
retain their two hard disks, both floppy drives and CPU/mouse options when
upgraded. The new firmware writes the expanded settings format after saving.
Keep ROMs in `apple-iigs/roms` and media in `apple-iigs/media` under the core
directory. The generated `hdd.rom` is part of the matching package.

## Implementation

The card occupies C700-C7FF and claims its C800-CFFF expansion ROM through
the IIgs slot-ROM selection rules. Accessing CFFF releases that ownership.
The source-built ROM contains a 256-byte slot page followed by a 2 KiB
expansion ROM. It is loaded with IIgs stream kind 0x49; an older FPGA image
cannot accept this stream and must be updated with the firmware.

The [KEGS-priority MMU audit](APPLE_IIGS_MMU_AUDIT.md) adds internal C800
ownership. Its matching `hdd.rom` releases that latch through CFFF in C7
before entering expansion ROM, preserving the published C70A/C70D/C757
entries and caller registers. Install the rebuilt slot ROM with the corrected
IIgs FPGA: the previous slot ROM can fail after the system boot scan selects
internal C800. This change does not inherently require an ARM rebuild.

Media kind 0x43 carries a 32-bit block count in the existing media-size
argument. This preserves the common shell ABI and avoids changing the other
cores. Wire slots 0 and 1 retain the original HDDs; slots 2 and 3 remain
floppies and slot 4 remains PRAM. The six added HDDs use slots 5 through 10.

The DMA engine retains the accelerated memory path and cache invalidation.
Disk data does not pass through a byte-at-a-time SmartPort software FIFO.
Banked transfers, completion errors and resets must preserve the CPU and
posted-write ordering. ARM retains write journals and the existing paced USB
service callback. No transfer timing probes are added.

## Validation and hardware test

The M0.6.0 source passes 93 host tests, 171 production-CPU ROM cases,
six controller groups, 19 full-adapter groups, and cache/loader/DDR tests.
ROM cases include 51 DMA initiations and a measured 35-byte maximum stack.
Coverage includes all eight units, unmounted gaps, invalid calls, bank and
address-limit crossings, high block numbers, write protection, failed I/O,
and reset/eject during transfers. The Clang run passes 87 tests with the common
and supervisor-settings host libraries instrumented by ASan/UBSan.

The owner's System 6.0.1 HDD image also passes a 180-million-tick simulation
at Stock and 57 MHz using the final ROM and adapter: 222 and 1,756 disk
requests complete respectively, with disk boot and native IRQ dispatch in
both runs. These bounded simulations establish storage/bootstrap progress;
they do not establish a usable Finder desktop or board performance.
Exact inputs and results are in the package's validation evidence.

The same release restores the native IIgs border around SHR. NTSC/PAL VGC
simulations preserve every active pixel in 320, 640 and fill modes. The full
724x496 frame passes the DDR effects oracle in 658,398 clocks against a
2,216,666-clock processing budget. These simulation checks do not establish
board timing or physical display acceptance.

For the board test, install the complete matching M0.6.0 package, including
`FIRMWARE-MULTITINI.BIN`, the IIgs FPGA file, `core.cfg` and `roms/hdd.rom`.
Preserve existing media. Confirm the new firmware/core identities in About.

1. Boot the existing System 6.0.1 image from hard disk 1 at Stock and 57 MHz.
   Check Finder, mouse/keyboard and the expected acceleration.
2. Mount distinct test images in all eight hard disks. Confirm that GS/OS
   finds each one and that writes return to the intended image.
3. Leave a gap in the mounted drives and mount an image in hard disk 8.
   Confirm the higher drive remains visible.
4. Exercise a protected image and a larger non-ProDOS image supported by the
   guest filesystem. Verify reads, writes and capacity through GS/OS.
5. Eject and remount images, restart the core and reboot the board. Verify
   saved mounts and that both floppy drives still work.
6. Check all four native SHR borders, guest border-color changes, display
   sizing and screenshots using the [IIgs hardware guide](APPLE_IIGS_HARDWARE_TEST.md).

The package is for hardware testing until these checks pass on the board.

## Reference

The call layouts and return conventions follow Apple's
[Apple IIgs Firmware Reference, SmartPort chapter](https://www.brutaldeluxe.fr/documentation/cortland/v2_01_AppleIIGSFirmwareReferenceManual.pdf).
Appletini One at commit `7b972bfda432657444d55a3eaaccf4b1d8e06699` provides
the existing SmartPort behavior used as a reference. Source provenance and
license records identify any imported or adapted material separately.
