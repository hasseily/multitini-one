# ZX Spectrum 48K and 128K

The ZX Spectrum image uses the MiSTer ULA, T80 Z80 and YM2149 sources pinned in
`provenance/zx-spectrum.import.json`. The Multitini adapter supplies RAM, ROM,
keyboard, Kempston input and a standard-speed TAP player. The package uses
core ID `ZXSP` and the directory `multitini/cores/zx-spectrum/`.

## Start the core

Copy your own Spectrum ROM to `multitini/cores/zx-spectrum/roms/spectrum.rom`.
The length selects the machine. Use a 16 KiB Spectrum 48K ROM, or concatenate
the two 16 KiB Spectrum 128K banks in editor/menu and 48K BASIC order for the
32 KiB image. These images are external inputs and stay outside Git and source
packages.

Put `.tap` files under the core's `media/` directory. Choose a file from the
Spectrum media menu and return to the machine. In 48K BASIC press J to enter
`LOAD`, press the PC apostrophe key twice to enter the double quotes, then
press Enter. In the 128K menu choose Tape Loader. Press F1 to play or pause the
tape. F2 stops and rewinds it. A machine reset also stops and rewinds the tape.

USB keyboard events drive the original eight by five matrix. Shift is CAPS
SHIFT and left Ctrl or left Alt is SYMBOL SHIFT. Cursor keys and Backspace
generate the corresponding CAPS SHIFT chords. Both USB controller slots feed
one Kempston joystick interface.

## Scope and timing

- PAL 48K and 128K ULA timing use the upstream 7 MHz pixel rate with Z80 phases
  derived by the ULA. The common 56 MHz root gives the 48K raster about 50.08 Hz
  and the 128K raster about 49.36 Hz.
- The shell captures 344 by 280 samples, including a 44-sample border on
  each side of the 256 by 192 picture. The common video path handles output
  refresh and scaling.
- ROM and RAM reside in FPGA block RAM. The 128K paging latch selects the
  top RAM bank, screen bank and ROM bank. Its lock bit lasts until reset.
- Audio combines the beeper and the 128K AY-3-8912-compatible PSG into mono
  PCM on both output channels.
- TAP media supports standard-speed blocks up to 128 KiB per file. Playback
  starts stopped and advances through blocks in order. The adapter verifies
  framing and each block's little-endian length before exposing the tape.
- Additional MiSTer machine variants, expansion ports, snapshots, turbo tape
  and alternate tape or disk formats are outside this port's scope.

## Build and test

`python scripts/port_zx_spectrum_core.py --check` verifies imported source
hashes and reproducible ULA and PSG changes. With Vivado on `PATH`, run
`python scripts/test_zx_spectrum.py` for the mixed-language simulation. A
source-built diagnostic program exercises Z80 execution, RAM banking, audio,
keyboard, controller input, both raster geometries, and tape pulse timing and
load rejection. Build the image with `python scripts/build_core_zx_spectrum.py`.

Physical acceptance requires booting a supplied ROM for each model, checking
the border and audio, loading a TAP through the ROM tape loader, and switching
away from and back to the core through the shared menu.
