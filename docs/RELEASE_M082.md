# Multitini M0.8.2

## Changes since M0.8.1

- **ZX Spectrum** adds PAL 48K and 128K modes, a USB keyboard, Kempston
  joystick input, and read-only TAP and TZX playback. In Media, choose
  **Normal** or **Fast (8x)** for either format. **Instant** loads TAP blocks
  through recognized English 48K and 128K BASIC ROM routines. See the
  [Spectrum setup and supported TZX blocks](ZX_SPECTRUM.md).
- **Atari 8-bit** adds the NTSC Atari 800XL (64 KiB) and Compy Shop (320 KiB)
  variants. The Media tab loads standard 8 KiB and 16 KiB cartridges, mounts
  read-only ATR disks in D1:, and runs XEX executables. Use **Eject Disk** in
  the D1: picker and **Eject cartridge** in the cartridge picker. To boot a
  mounted ATR, eject any cartridge and restart the active core. Controller
  Select and Start operate the matching Atari buttons.
  See the [Atari setup and media guide](ATARI_800XL.md).
- **Atari Compy Shop (VCOUNT compatible)** is a separate 320 KiB image for
  software that depends on reading a stable scan counter at NTSC frame
  rollover. In RTL replay, both unmodified Bomb Jack revisions passed the
  splash-screen wait with this image's VCOUNT behavior. The standard Atari
  variants retain the original rollover value. The Atari images also correct
  ANTIC playfield DMA during vertical blank. See the
  [Atari verification record](ATARI_XT.md#timing-and-verification).
- **Apple IIgs** adds WOZ1 mounting. Floppy
  updates improve mounting, Sony drive timing and IWM behavior. Other fixes
  address Apple modifier keys, legacy video edges, unmapped WOZ track writes,
  held-key status, sound interrupt order and SCC register/reset behavior. See
  the [IIgs integration notes](APPLE_IIGS_PLAN.md) and
  [hardware test record](APPLE_IIGS_HARDWARE_TEST.md).
- **Amiga** adds the A1200 AGA + 68040 variant, with 28 MHz and 56.75 MHz
  settings. Select it in Cores and use a Workbench disk with 68040 support.
  See the [Amiga integration notes](AMIGA_PLAN.md).
- **Shared menus** show lock icons for read-only media. Controller setup adds
  per-axis dead zone, sensitivity and direction with
  live raw and tuned values. Controller assignments persist after an explicit
  save. The About page has an Appletini-style layout with hardware diagnostics
  and credits. See [USB input and bindings](USB_INPUT.md).

## Install

1. Back up the SD card, including media, saves, settings and the previous
   firmware.
2. Extract `Multitini-082.zip`. Merge the contents of `sd/` into the SD card
   root. Keep your existing media, saves and settings. Add your system ROMs
   according to the `roms/` instructions for each core you use.
3. Install `FIRMWARE-MULTITINI.BIN` through Golden Boot. The copies at the ZIP
   root and under `sd/` are identical. Then select the core from the updated
   SD tree. See the [Golden update guide](golden-update.md).

Use the firmware and core packages from the same ZIP. The public ZIP supplies
ROM setup instructions and omits private system ROMs, games and OS disks.

## Build identities

ARM firmware is `M0.8.2` / `0x4D000802`, with runtime ABI 0.28. The FPGA
selection is the same as M0.8.2dev13. Reused images keep their build IDs and
ABI versions.

| FPGA image | Build ID | ABI |
| --- | --- | --- |
| Supervisor, Game Boy Color, Vectrex, Genesis NTSC and PAL | `0x4D000300` | 0.21 |
| C64 PAL and NTSC | `0x4D00C714` | 0.27 |
| NES | `0x4D000501` | 0.22 |
| Apple IIgs | `0x4D000860` | 0.28 |
| ZX Spectrum | `0x4D000867` | 0.28 |
| Atari 800XL NTSC | `0x4D00086A` | 0.28 |
| Atari Compy Shop NTSC | `0x4D000869` | 0.28 |
| Atari Compy Shop (VCOUNT compatible) | `0x4D00086B` | 0.28 |
| Amiga 600 ECS PAL | `0x4D000829` | 0.27 |
| Amiga 1200 AGA PAL | `0x4D000839` | 0.27 |
| Amiga 1200 AGA + 68040 | `0x4D000843` | 0.27 |

The ZIP's `manifest.json` and `SHA256SUMS.txt` record the packaged files.
`source.zip` and `evidence/` provide source snapshots, original build inputs
for reused images, and the build and payload checks for this selection.

## Verification status

All 139 host tests and 18 menu runtime tests passed. The package uses
timing-qualified FPGA images. Focused RTL tests cover the Spectrum tape
player and the Atari DMA and VCOUNT changes. The earlier IIgs
IWMTEST board result applies to FPGA `0x4D000615`, as recorded in the
[IIgs hardware test record](APPLE_IIGS_HARDWARE_TEST.md#iwmtest-on-fpga-4d000615).
Board acceptance of the assembled M0.8.2 release is pending.
