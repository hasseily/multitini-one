# Amiga ECS board qualification

## Installed development build

Initial board tests on 2026-10-31 used FPGA `4D000802`, runtime ABI 0.27, and firmware
`M0.8.0dev2` / `4D000803`. The upstream Minimig source is pinned at
`e47c3457b486d09daaf47c8934bda90cf7723045`.

The initial test configuration used ECS PAL, 2 MiB chip RAM, 8 MiB fast RAM,
unthrottled TG68K 68020 acceleration, two DD drives and two 64 MiB test HDFs.
Both floppy drives are ejected. The HDFs are blank and unpartitioned; they
contain no Workbench installation. User-provided ADF copies are available
in the Media browser.

A subsequent test successfully booted ClassicWB FULL v28 with Workbench 3.1
from a separate IDE HDF on the card. That installed image is now selected as
IDE master, with both floppy drives ejected. The Scalos desktop was captured
and the final status reported ready with no faults. See
[ClassicWB boot](AMIGA_CLASSICWB_BOOT.md) for the installation and evidence.

The later alignment correction loaded FPGA `4D000804` with the same
firmware. Its routed margins are setup +0.259 ns, hold +0.041 ns and native
clock enable +13.331 ns; all export gates pass. Board screenshots show the
complete standard 640 by 256 desktop in the 640 by 512 capture, displayed
at 2x in 1080p. The pointer reaches the top-left edge and the right
scrollbar is visible. The user confirmed that alignment looks good. See
[video geometry](AMIGA_VIDEO_GEOMETRY.md) for exact evidence.

The user subsequently reported that SysInfo 3.24 opens a window and stops
responding. Video and shell counters continued without reported transport
errors. An offline reproduction identifies a native RTC seconds-at-59
hang path; the live guest PC was not observed. Work continued offline
while the user used the card, followed by the authorized installation below.

After the user authorized installation, Golden Boot installed ARM
`M0.8.0dev3` / `4D000805` with matching file/flash CRC32 A61CB503. FPGA
`4D000806` loaded with CRC32 44A98C6D and reports ABI 0.27, ready, fault
zero and error zero. The existing ClassicWB HDF remains IDE master with
68020 acceleration, 2 MiB chip and 8 MiB fast RAM. Both DD drives are
ejected. New RTL timing passes with setup +0.244 ns, hold +0.008 ns and
native clock-enable setup +13.748 ns; the hold margin is narrow.

The 805 boot reports valid board RTC time; RTC capability is present and
no clock-feed error was logged. Video capture/processing advanced with
640 by 512 geometry and no reported errors. The user then confirmed that
SysInfo works on this pair. Native RTC progression has not been measured
directly; SysInfo benchmark accuracy and long runs remain separate checks.

The pre-install USB export and verified copies succeeded. After the firmware
restart, Windows did not enumerate the screenshot-retrieval export; one
reconnect attempt also saw no USB reset/port events. Export stopped cleanly
and the Amiga restarted without faults. This USB observation is unqualified,
not a demonstrated source regression. The card was left running Amiga with
the shared menu closed. Exact install logs and backups are retained under
`build/amiga-ecs/board-tests/`; the audited build packages remain immutable.

The active user-owned A600 Kickstart 3.1 image is revision 40.063, 512 KiB,
SHA-256 `8c8a0cf04f91b88eaf0c4f1126041987067e2286a8ee590bdbae447a8000c5ee`.
Firmware mirrors it into the 1 MiB aperture. ROM and disk bytes remain
outside Git and the ROM-free core package.

F12 opens the menu. The Core CPU option switches between `68000 stock` and
`68020 accelerated`; changing it saves disks and restarts the guest. Fast
RAM can be 4 or 8 MiB. Normal DD timing is the compatibility default; a
separate floppy speed option is available. Media contains DF0, DF1 and the
two IDE image slots.

## FPGA and software evidence

Final routed setup margin is +0.264 ns, hold +0.046 ns and pulse width
+1.250 ns. The unexcepted native clock-enable path has +13.073 ns setup
margin. Strict clock, CDC, DRC, coverage and export checks pass, with zero
unconstrained internal endpoints and zero critical CDC crossings.

All 126 ARM host tests pass for ARM 805. Actual stock and accelerated CPU simulations
cover chip/fast RAM, repeated fast-RAM writes, blitter DMA cache misses and
hits, and palette writes, including delayed data and native clock edges.
Loader, host FIFO, input, native phase and full PAL video tests pass.
Enhanced CPU test sources are retained in the checkout and separately bound
to the sealed FPGA source archive as post-capture verification evidence.

## Board observations

| Check | Observed result |
| --- | --- |
| FPGA loading | PCAP accepted 4,045,568 bytes, CRC32 DD31166D; core AMIG, build 4D000802 |
| Stock boot | Captured Kickstart 3.1 insert-disk screen |
| Accelerated boot | Captured the same insert-disk screen with CPU=2 |
| DD reads | DF0 and DF1 recorded 60 and 9 reads in the accelerated two-ADF test |
| IDE reads | Both blank test HDFs recorded 16 boot-probe reads |
| IDE writes | Original guest diagnostic passed both master and slave |
| Video transport | Captured/consumed/processed frames advanced; no reported drops, underruns, AXI errors or deadlines in the sampled run |
| USB SD | Repeated exclusive export, verified copies, PC eject and core restart succeeded |
| Screenshots | Board-generated 720 by 576 PNGs confirmed stock boot, accelerated boot and diagnostic success |

The IDE diagnostic requires IDENTIFY capacity exactly 131072 sectors before
writing. It touches only LBA 131070, byte offset 67107840, in each disposable
64 MiB HDF. It preserves the original sector, writes and verifies a pattern,
flushes, restores the original, then verifies again. Each disk recorded
three reads and two writes. The board displayed green for complete success.
Both whole HDFs matched their before-test SHA-256 afterward:
`3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351`.

Golden recovery remains intact. On this card Golden's monitor is COM12 at
921600 baud; ARM firmware's console is COM13 at 921600. A reboot must be
sent to the ARM console before catching Golden on its separate port.

## Amiga 1200 AGA PAL

The board loaded FPGA `4D000828` over PCAP with CRC32 `DEDC263F`. Final
routed setup slack was +0.202 ns and hold slack was +0.036 ns.

With A1200 Kickstart 3.1 revision 40.068, Street Racer booted from DF0 in stock
68EC020 mode. The serial console recorded 72 reads and zero faults. The owner
also confirmed the game booted in the accelerated 28 MHz mode.

## Remaining ECS qualification

The current AROS 20261031 1 MiB ROM reaches its graphical splash with the
accelerated profile. No HDF reads were observed during that attempt, so its
hard-disk boot is not accepted. The ROM and boot disk are retained as local
test assets. Kickstart 3.1 is restored as the active ROM.

The accelerated game-image test recorded floppy reads but did not establish
that the game completed boot. Hardware floppy writes, Paula audio,
keyboard/mouse/gamepad behavior, interlace and all output modes remain
unverified. These claims must stay separate from the passing simulations.

Stock mode retains the upstream 68000 phase schedule. Chip-memory misses can
hold the native timeline, so exact wall-clock stock behavior under every
workload is not established. Application benchmarks and maximum sustainable
acceleration are also unmeasured. This image exposes 68000 and 68020 modes;
68030/MMU modes are not integrated or qualified.
