# ClassicWB hard-disk boot on Multitini

## Result

ClassicWB FULL v28 with Workbench 3.1 reached its Scalos desktop on the
physical card on 2026-10-31. This is an installed operating-system boot from
an IDE HDF. The earlier unactivated image also booted its AmigaDOS installer
from the hard disk.

The installed image was selected as IDE0 master with both DD drives
ejected. The test used ECS PAL, 68020 acceleration, 2 MiB chip RAM, 8 MiB
fast RAM and the user's A600 Kickstart 3.1 revision 40.063. The captured
desktop identifies Amiga 3.1 / ECS / 020 and reports 1,963,176 bytes free
chip RAM and 7,137,736 bytes free fast RAM.

Final console status was core `414D4947`, FPGA `4D000802`, ABI 0.27,
status `0000014D`, error zero, service ready and fault zero. The installed
HDF recorded 2,995 reads. Firmware remains `M0.8.0dev2` / `4D000803`;
no FPGA or firmware rebuild was needed for this test.

## Preparation

The user requested `http://download.abime.net/classicwb/ClassicWB_FULL_v28.zip`.
Its SHA-256 is
`7d7db4989112ebd52f6e0b2d227b043172cd309b2609122505c1ababc02367cd`.
The original System.hdf is a 500 MiB partition image, not a whole IDE disk.
A separate copy gained an RDB with 2,001 cylinders, 16 heads and 32 sectors
per track. DH0 begins at cylinder 1, byte offset 262144, and contains all
1,024,000 original DOS1 sectors unchanged before installation.

The user supplied the Install ADF from the Archive.org item
`amiga-wb31_kryoflux`. All six companion Workbench 3.1 ADFs were downloaded
from that same item and checked against its published sizes and SHA-1s.
The Workbench3.1 label, required commands and version.library passed the
ClassicWB installer's checks.

On another working copy, the offline installation applied ClassicWB's
Activate and six Workbench 3.1 copy scripts, preserving file metadata and
the Scalos LoadWB handoff. ECS PAL HiRes 640 by 256, 16 colours, was retained;
the AGA Copper option was disabled. RDB checksums and full filesystem checks
passed with zero errors and warnings. All 449 installed CLONE targets had
their protection, timestamp and comment metadata verified. The original
download and original partition remain unchanged.

The installed HDF is `ClassicWB_FULL_v28-installed.hdf`, 524,550,144 bytes.
Its pre-boot SHA-256 is
`b079a1fa2cc5733b15157f6304108ea8a97837c858d0b2cae93669143a014120`.
Normal guest writes may change the live card copy. Private manifests,
console logs and actual board screenshots are retained with the local test
assets. No ROM, Workbench ADF or installed HDF bytes were added to Git.

## Limits

Desktop boot is verified; application/game compatibility and maximum speed
are separate checks. The offline installer did not execute its 68k FixFonts
refresh, but copied font caches were retained and the desktop rendered.
The initial source frame clipped a small portion of the desktop's right
edge. FPGA 4D000804 subsequently corrected the standard crop; new source
and output screenshots show the complete desktop, and the user accepted
the alignment. See [video geometry](AMIGA_VIDEO_GEOMETRY.md).

The user then reported that SysInfo 3.24 opens its window and stops
responding. The [RTC investigation](AMIGA_RTC.md) documents a reproduced
clock-loop defect and the installed ARM 805 / FPGA 806 fix. The user confirmed
that SysInfo works on that pair. Physical audio and input acceptance remain
as recorded in the general board report.
