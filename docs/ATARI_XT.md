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

SALLY and ANTIC use separate one-cycle read ports on the same 64 KiB RAM
and ROM stores. PIA PORTB enables the OS, BASIC and self-test windows for
both masters. A standard 8 KiB cartridge overlays `$A000-$BFFF` and a
16 KiB cartridge overlays `$8000-$BFFF`. GTIA TRIG3 follows RD5. It reads high
when a cartridge supplies `$A000-$BFFF` and low when the window is available
to built-in BASIC or RAM. Mapped ROM and cartridge writes leave the
underlying RAM intact. [Altirra Hardware Reference Manual, section 2.8](https://www.atari800xl.eu/docs/reference/altirra-hardware-reference-manual.pdf)

The adapter holds reset for firmware ROM transfers. Cart writes invalidate
the old cart when the new stream starts. A failed cart transfer leaves the
guest held in reset until a complete replacement or an eject command arrives.

## Timing and verification

The 100 MHz root runs 56 clocks per SALLY cycle. ANTIC2 provides 384 by 240
captured samples from its NTSC raster. The adapter feeds the mono POKEY mix
to both shared audio channels.

Run `python scripts/port_atari_xt_core.py --check` to verify the source hashes
and regenerated machine port. With Vivado on `PATH`, run
`python scripts/test_atari_xt.py` to simulate the production adapter with a
source-built 6502 diagnostic ROM. It exercises OS, BASIC and self-test banking,
ANTIC character fetch and raster output, POKEY audio and keys, PIA/GTIA input,
standard cart mapping, eject and interrupted streams.

Build with `python scripts/build_core_atari_xt.py`. The board acceptance
checks are XL ROM boot, BASIC keyboard entry, audio, both controllers, cart
boot/eject and switching through the shared menu.
