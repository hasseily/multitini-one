# Atari 800XL on Multitini

This build uses and adapts Atari-XT by ThrudTheBarbarian. The [original source](https://github.com/ThrudTheBarbarian/fpga-xt) is pinned in `provenance/atari-xt.import.json`. The upstream license is in `third_party/atari-xt/upstream/LICENSE`. About → Credits repeats the attribution.

Multitini modified Atari-XT's `a8_core.sv` on 2026-11-06. The port aligns CPU and chipset subcycles, maps SD-loaded ROMs for CPU and ANTIC reads, and exposes the Atari joystick and POKEY keyboard and audio interfaces.

The SIO port on 2026-11-06 also modified `a8_core.sv`, `pia_regs.sv`, `pokey.sv` and `pokey_serial.sv` to carry PIA COMMAND and POKEY serial data between the Atari and the Multitini disk service.

The core models a 64 KiB NTSC Atari 800XL. It uses Atari-XT's fidelity 6502, ANTIC2, GTIA, PIA and POKEY. The shell displays a 384 × 240 raster and sends the four-channel mono POKEY mix to both audio outputs. The guest clock follows Atari-XT's 56-cycle division of the 100 MHz fabric clock, giving about 1.786 MHz.

## SD setup

Copy the OS ROM to `multitini/cores/atari-800xl/roms/atarixl.rom` (16 KiB) and BASIC to `multitini/cores/atari-800xl/roms/ataribas.rom` (8 KiB). Supply both files before starting the machine. The Media tab can retry a ROM load.

Put files under `multitini/cores/atari-800xl/media/`. The Media tab has separate controls for each format.

- **Cartridge** inserts a standard 8 or 16 KiB cartridge. Use CART type 1 or 2 `.car` files or exact-size raw `.rom` or `.bin` files. Insert and eject restart the guest.
- **Disk D1:** mounts a read-only `.atr` image with 128- or 256-byte sectors at standard SIO speed. Select **Boot D1:** to restart with the mounted disk and eject any cartridge. Mount another image to swap disks without resetting. Some Atari XL disk software needs BASIC disabled at boot. Hold F3 (Option) while restarting that software.
- **Run XEX** validates a segmented `.xex` executable, replaces D1 with a temporary read-only boot disk, and restarts. The loader needs two free RAM pages and reserves memory used by the Atari OS. See [XEX loader notes](ATARI_XEX_BOOT.md).

Disk writes, additional SIO drives, cassette, banked cartridges, and PAL remain outside this implementation.

F1 is Start, F2 Select, F3 Option, F4 Help, and F5 Break. F12 opens the Multitini menu. Each of the two gamepad ports provides four directions and one fire button. Custom gamepad mappings apply for the current session so the shared SD settings remain readable by earlier firmware.
