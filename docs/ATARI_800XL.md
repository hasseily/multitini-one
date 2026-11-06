# Atari 800XL on Multitini

This build uses and adapts Atari-XT by ThrudTheBarbarian. The [original source](https://github.com/ThrudTheBarbarian/fpga-xt) is pinned in `provenance/atari-xt.import.json`. The upstream license is in `third_party/atari-xt/upstream/LICENSE`. About → Credits repeats the attribution.

The core models a 64 KiB NTSC Atari 800XL. It uses Atari-XT's fidelity 6502, ANTIC2, GTIA, PIA and POKEY. The shell displays a 384 × 240 raster and sends the four-channel mono POKEY mix to both audio outputs. The guest clock follows Atari-XT's 56-cycle division of the 100 MHz fabric clock, giving about 1.786 MHz.

## SD setup

Copy the OS ROM to `multitini/cores/atari-800xl/roms/atarixl.rom` (16 KiB) and BASIC to `multitini/cores/atari-800xl/roms/ataribas.rom` (8 KiB). Supply both files before starting the machine. The Media tab can retry a ROM load.

Put cartridges under `multitini/cores/atari-800xl/media/`. Choose **Cartridge** in the Media tab to insert a standard 8 or 16 KiB cartridge. Supported files are CART type 1 and type 2 `.car` images and exact-size raw `.rom` and `.bin` images. Insert and eject restart the guest. SIO disk and cassette, banked cartridges, and PAL are outside this implementation.

F1 is Start, F2 Select, F3 Option, F4 Help, and F5 Break. F12 opens the Multitini menu. Each of the two gamepad ports provides four directions and one fire button. Custom gamepad mappings apply for the current session so the shared SD settings remain readable by earlier firmware.
