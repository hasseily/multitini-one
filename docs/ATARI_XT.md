# Atari-XT FPGA integration

This private Multitini prototype incorporates and adapts [Atari-XT by
ThrudTheBarbarian](https://github.com/ThrudTheBarbarian/fpga-xt). It uses
the SALLY fidelity CPU, ANTIC2/GTIA, PIA, POKEY, and NTSC palette from
commit `a4340eb034503c6189197480bccc55564e7708a5`. The exact source list and
local changes are in `provenance/atari-xt.import.json`. The upstream modified
CERN Open Hardware Licence is retained in `third_party/atari-xt/upstream/LICENSE`.

See [Atari 800XL setup](ATARI_800XL.md) for SD layout, supported media and
controls.

## Machine interface

SALLY and ANTIC use separate one-cycle read ports on the same RAM and ROM
stores. The 800XL variant has 64 KiB of RAM. PIA PORTB enables the OS, BASIC and self-test windows for
both masters. A standard 8 KiB cartridge overlays `$A000-$BFFF` and a
16 KiB cartridge overlays `$8000-$BFFF`. GTIA TRIG3 follows RD5. It reads high
when a cartridge supplies `$A000-$BFFF` and low when the window is available
to built-in BASIC or RAM. Mapped ROM and cartridge writes leave the
underlying RAM intact. [Altirra Hardware Reference Manual, section 2.8](https://www.atari800xl.eu/docs/reference/altirra-hardware-reference-manual.pdf)

The 320 KiB variant uses the Compy Shop layout. It adds sixteen 16 KiB banks
mapped at `$4000–$7FFF`. PORTB bits 2, 3, 6 and 7 select a bank. Active-low
bits 4 and 5 enable the selected bank for SALLY and ANTIC independently.
The self-test ROM is available when both accessors use base RAM. On
2026-11-06 the Atari-XT port added the effective PIA pin levels to the machine
interface so input bits pull high even while software accesses the data
direction register. [Altirra Hardware Reference Manual, section 2.7](https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf)

The adapter holds reset for firmware ROM transfers. Cart writes invalidate
the old cart when the new stream starts. A failed cart transfer leaves the
guest held in reset until a complete replacement or an eject command arrives.

## Timing and verification

The 100 MHz root runs 56 clocks per SALLY cycle. ANTIC2 provides 384 by 240
captured samples from its NTSC raster. The adapter feeds the mono POKEY mix
to both shared audio channels.

ANTIC2 starts ordinary playfield DMA and performs player/missile DMA on
scanlines 8 through 247. Refresh continues during vertical blank. The focused
DMA test checks reads and CPU cycle steals across a full frame. A display
list with 30 eight-line mode-4 rows verifies 32 glyph reads on the last
visible line and refresh-only steals through vertical blank.
[Altirra Hardware Reference Manual, sections 4.11 through 4.14](https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf)

RTL replays of title-screen snapshots from both Bomb Jack XEX revisions
reproduced the crash and alternating corrupt frames with the previous DMA
schedule. Both continued through the same windows with the playfield DMA
gate. A gameplay snapshot of the original revision wrote to the display-list
registers 11 times after visible scanout began with the previous DMA schedule
and zero times with the gate. Captured gameplay frames show a corrupted frame
between two clean frames on the previous schedule. The corrected replay
renders that frame cleanly.

On the board, build `4D000869` stalled at the earlier splash with both
original XEX revisions. A replay from before the splash reproduced the stall.
The game sampled the one-cycle NTSC VCOUNT value `$83` at frame rollover,
then polled at a phase that missed it on each subsequent frame. Changing the
branch operand at XEX file offset `$2A3E` from `$FB` to `$F8` makes the game
retry both VCOUNT reads after a mismatch. Both patched XEXs reached gameplay
through the Multitini loader in Atari800. The patched v1.5 also reached
gameplay on the board. Pressing M during gameplay disables music and stopped
the reported flicker after picking up a bomb on the NTSC board.

The VCOUNT compatibility variant keeps the internal scan counter unchanged
and returns the previous stable count during the rollover cycle of a CPU
read. The normal variants preserve the hardware value. The focused VCOUNT
test checks both modes at every cycle of an NTSC frame.

Run `python scripts/port_atari_xt_core.py --check` to verify the source hashes
and regenerated machine port. With Vivado on `PATH`, run
`python scripts/test_atari_xt.py` to simulate the production adapter with a
source-built 6502 diagnostic ROM. It exercises OS, BASIC and self-test banking,
ANTIC character fetch and raster output, POKEY audio and keys, PIA/GTIA input,
standard cart mapping, eject and interrupted streams.

Build the 800XL with `python scripts/build_core_atari_xt.py` or pass
`--image atari-320xe` to build the 320 KiB variant. Use
`--image atari-320xe-compat` for the 320 KiB VCOUNT compatibility variant.
The board acceptance
checks are XL ROM boot, BASIC keyboard entry, audio, both controllers, cart
boot/eject and switching through the shared menu.
