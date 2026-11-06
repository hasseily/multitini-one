# Atari disk and executable loading

The ATR reader validates the 16-byte header and checks the declared payload
against the file size. It supports 128-byte and 256-byte sectors. SIO reads
sectors 1 through 3 as 128 bytes in either density. Double-density images may
store those sectors consecutively or include padding. For padded images, a
nonzero second 384-byte block indicates interleaved boot sectors. An all-zero
block is treated as padding after three consecutive boot sectors. The reader
calculates each file offset directly and preserves the header's write-protect
flag for the caller.

For XEX, firmware presents a read-only D1 disk. Its first two sectors contain
an original Multitini 6502 bootstrap. Each subsequent sector reads 128 bytes
directly from the XEX file, with zero padding at the end of the final sector.
The guest's OS boots from the disk and the bootstrap uses the stock `SIOV`
call to fetch file data. No complete disk image is stored in ARM memory.

The parser accepts Atari DOS binary load segments with an initial `$FFFF`
marker and optional further markers between segments. The bootstrap loads
segments in file order, calls each `INITAD` when its segment finishes and
uses the final `RUNAD`. When a file supplies no `RUNAD`, it starts at its
first segment's load address. The bootstrap banks out built-in BASIC so the
file can use RAM at `$A000`–`$BFFF`.

The bootstrap and its sector buffer need two consecutive free RAM pages
between `$0700` and `$97FF`. Firmware selects the first available location
after scanning the segment headers. XEX files can load into `$0100`–`$017F`.
The loader reserves `$0180`–`$01FF` for its stack and the stock OS stack.
An executable that leaves no space or overlaps that reserved range, the disk
control block or mapped ROM is rejected.
Initialization code must return with `RTS` and keep the loader and OS disk
services usable. XEX boot supplies a disk transport and loader without a
guest DOS file system.

The assembly source is `firmware/atari_xex_boot.a65`. With ACME on `PATH`, run
`python scripts/build_atari_xex_boot.py --check` to verify the generated C
header. Run the script without `--check` to regenerate the header.

`ctest --test-dir <host-build> -R '^atari_disk$'` checks image parsing and
sector generation. With py65 installed, run
`python scripts/test_atari_xex_boot.py <test_atari_disk-executable>` to
execute the generated disk in a 6502 simulation. Optionally pass
`--xl-rom <atarixl.rom>` to exercise the original 800XL Rev 2 disk bootstrap
with a supplied ROM. The simulation hooks `SIOV` and does not exercise FPGA
or physical serial timing.
