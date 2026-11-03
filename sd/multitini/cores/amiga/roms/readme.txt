Amiga Kickstart ROMs

Copy your own legally obtained ROM images into multitini/cores/amiga/roms/
on the SD card. Use one raw, uncompressed ROM image per variant.

Multitini variant   Tested Kickstart        File name        Size      CRC32
Amiga 600 ECS PAL   3.1 revision 40.063     kick.rom         512 KiB   FC24AE0D
Amiga 1200 AGA PAL  3.1 revision 40.068     kick-a1200.rom   512 KiB   1483A091

Both files can live in this folder. Multitini loads the file for the variant
you select. The ROM builds are model-specific. Check the revision or CRC32
if the original filename is ambiguous.

The loader accepts exact 256 KiB, 512 KiB, or 1 MiB raw images and mirrors
smaller images across its 1 MiB ROM aperture. The table lists the images
tested on the board.

If Multitini cannot open Kickstart, check the folder and filename above.
After correcting the file, open F12, select Media, then select Kickstart ROM
to retry.

Optional PFS3 filesystem handler

For a bare PFS3 partition HDF, copy the pfs3aio executable from the
PFS3 All-In-One archive into this folder as pfs3aio. Multitini exposes the
handler and partition table in memory. An AmigaOS installation that
already loads PFS3 can also mount the partition. After selecting an IDE
image, restart the Amiga so Kickstart scans it.

https://aminet.net/package/disk/misc/pfs3aio
