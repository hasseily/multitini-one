Atari 800XL

Copy your Atari XL OS ROM (16384 bytes) as atarixl.rom and your Atari
BASIC ROM (8192 bytes) as ataribas.rom into this directory on the SD card.
The core needs both files before it starts.

Put standard 8 KiB and 16 KiB cartridges in ../media/. Use .car files
with a type 1 or 2 header, or raw .rom or .bin images for unbanked carts.
In F12 Media, open Cartridge and choose a file or Eject cartridge.
Inserting or ejecting restarts the guest.
Put .atr disks and .xex executables in ../media/ as well. Disk D1: mounts
an ATR read-only and offers Eject Disk. Restart active core on the System
tab restarts with the current media. Run XEX launches a program.

This core uses modified Atari-XT HDL by ThrudTheBarbarian.
Original source at https://github.com/ThrudTheBarbarian/fpga-xt
