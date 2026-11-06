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

On 2026-11-06, the ANTIC2 port limited player/missile DMA to scanlines 8
through 247. The test checks both memory reads and CPU cycle steals across a
full frame. [Altirra Hardware Reference Manual, section 4.13](https://www.atari800xl.eu/docs/reference/altirra-hardware-reference-manual.pdf)

Run `python scripts/port_atari_xt_core.py --check` to verify the source hashes
and regenerated machine port. With Vivado on `PATH`, run
`python scripts/test_atari_xt.py` to simulate the production adapter with a
source-built 6502 diagnostic ROM. It exercises OS, BASIC and self-test banking,
ANTIC character fetch and raster output, POKEY audio and keys, PIA/GTIA input,
standard cart mapping, eject and interrupted streams.

Build the 800XL with `python scripts/build_core_atari_xt.py` or pass
`--image atari-320xe` to build the 320 KiB variant. The board acceptance
checks are XL ROM boot, BASIC keyboard entry, audio, both controllers, cart
boot/eject and switching through the shared menu.
