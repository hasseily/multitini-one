# Multitini M0.8.1

- Fixes a CPU stall after ejecting an Amiga floppy or IDE image that could
  leave mouse and keyboard input unresponsive.
- Mounts bare AmigaDOS and PFS3 partition HDFs through virtual disk metadata.
  PFS3 can use an optional `roms/pfs3aio` filesystem handler.
- Shows a green **Screenshot saved** badge for two seconds after a successful
  screenshot save, including when drive activity indicators are disabled.

Install `FIRMWARE-MULTITINI.BIN` through Golden Boot and merge the package's
`sd/multitini` folder into the SD root. The disk-swap fix needs the included
Amiga 600 ECS PAL and Amiga 1200 AGA PAL images, both FPGA `4D000829`.
ARM firmware is `M0.8.1` / `4D000801`, with runtime ABI 0.27.

All 133 host tests passed. The Amiga images passed routed timing and five CPU
simulation profiles, including modeled interface delays.
The disk-swap and screenshot changes await a board retest.
