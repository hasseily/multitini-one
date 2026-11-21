# Multitini M0.8.3

## Changes since M0.8.2

- **Keyboard controller bindings** work across the shared core input service.
  In **Controls → Controllers**, select the keyboard interface, set
  **Assign to**, and bind each game action. Release Enter before pressing the
  key to assign, then choose **Save changes**. Bindings are saved per core and
  keyboard. New keyboard profiles start as **Not used** with empty bindings.
  Arrow keys, Enter and modifier keys can be assigned. F12 and Print Screen
  keep their system functions. Computer cores receive unbound keys while
  assigned keys act as controls. See [USB input and bindings](USB_INPUT.md).
- **Amiga 68040** adds a **16 MiB Fast RAM** choice as one Zorro III bank.
  The memory bridge supports four-word cache-line reads, registered burst
  replies and sequential DDR prefetch. These changes reduce memory waits;
  game speed and sustained performance still require board measurements.
  Changing Fast RAM size saves disks and restarts Amiga. Use a Workbench disk
  with 68040 support. See the [Amiga integration notes](AMIGA_PLAN.md).
- **Amiga controllers** provide Red, Blue, Green and Yellow through the CD32
  pad protocol on the selected images. Red and Blue also serve as the two
  direct joystick fire buttons. Bind these controls separately for each port.
- **Apple IIgs sound** updates DOC oscillator, interrupt, RAM-retention and
  host-access behavior, and corrects the ROM 03 Sound GLU clock phase. The
  selected image includes the directed DOC and Sound GLU fixes. DOCGRADE
  compatibility remains incomplete; rerun it on the board with this image.
  See the [IIgs integration and timing record](APPLE_IIGS_PLAN.md).

## Install

1. Back up the SD card, including media, saves, settings and the previous
   firmware.
2. Extract `Multitini-083.zip`. Merge the contents of `sd/` into the SD card
   root. Keep existing media, saves and settings. Supply your system ROMs
   according to the `roms/` instructions for each core you use.
3. Install `FIRMWARE-MULTITINI.BIN` through Golden Boot. The copies at the ZIP
   root and under `sd/` are identical. Select the core from the updated SD
   tree. See the [Golden update guide](golden-update.md).

Use the firmware and core files from the same ZIP. Update the IIgs FPGA,
`core.cfg` and supplied source-built SmartPort `hdd.rom` together. The public
ZIP includes ROM setup instructions and omits private system ROMs, games and
OS disks.

## Build identities

ARM firmware is `M0.8.3` / `0x4D000883`, with runtime ABI 0.28. Reused FPGA
images retain their own build IDs, ABI versions and original source evidence.

| FPGA image | Build ID | ABI |
| --- | --- | --- |
| Supervisor, Game Boy Color, Vectrex, Genesis NTSC and PAL | `0x4D000300` | 0.21 |
| C64 PAL and NTSC | `0x4D00C714` | 0.27 |
| NES | `0x4D000501` | 0.22 |
| Apple IIgs | `0x4D000878` | 0.28 |
| ZX Spectrum | `0x4D000867` | 0.28 |
| Atari 800XL NTSC | `0x4D00086A` | 0.28 |
| Atari Compy Shop NTSC | `0x4D000869` | 0.28 |
| Atari Compy Shop (VCOUNT compatible) | `0x4D00086B` | 0.28 |
| Amiga 600 ECS PAL, Amiga 1200 AGA PAL and Amiga 1200 AGA + 68040 | `0x4D000871` | 0.28 |

`Multitini-083.zip` contains only the firmware and SD files. The separate
`Multitini-083-sources-and-build-evidence.zip` contains `manifest.json`,
`SHA256SUMS.txt`, `source.zip` and `evidence/`, with the exact software sources,
licenses, original FPGA inputs, timing reports and package checks. Each
download has a separate SHA-256 checksum file. The IIgs image passed routed
signoff with +0.007 ns setup and +0.056 ns hold slack. Its narrow margin and
the selected timing-recovery recipe are recorded in the archived evidence.

## Verification status

All 139 host CTest cases and 19 menu runtime tests passed. The fresh Vitis
CPU0 and CPU1 build passed compiled-source and platform checks, ELF memory
checks and the embedded CPU1 payload audit. Repository policy checks passed
for 2,102 files. The selected FPGA images retain their recorded implementation
signoff; no new FPGA implementation was run for this release.

IIgs machine lint and the directed DOC OIR, DOCGRADE, Sound GLU and ROM 01/03
sound-phase simulations passed. The DOCGRADE check is an HDL regression,
separate from a complete guest DOCGRADE result. Amiga simulations passed for
the adapter input FIFO, CD32 serial buttons, direct fire and mouse input, and
AP68040 RAM with 16 MiB Fast RAM, the 2x CPU mode and burst reads. Source,
platform, firmware and core payload audit evidence is retained in the separate
sources and build evidence download.

Board acceptance of the assembled M0.8.3 package is pending. The keyboard
bindings, Amiga 16 MiB RAM and CD32 controls, sustained DDR performance, and
IIgs sound changes require tests on the selected hardware images. Earlier
board results remain limited to the builds named in the
[Amiga](AMIGA_HARDWARE_TEST.md) and
[IIgs](APPLE_IIGS_HARDWARE_TEST.md) test records.
