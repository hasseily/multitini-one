# Amiga M0.8.0dev10 development update

This source update adds Workbench display selection, storage extensions and
keyboard shortcuts to the previously committed ARM 805 / FPGA 806 baseline.
The core is named **Amiga**, with variant **Amiga 600 ECS PAL**.

## Behaviour

- Display choices are 640 by 256, 640 by 512 and 724 by 566 with 16 colours,
  plus 1280 by 512 with four colours. Interlaced capture weaves fields.
  The original helper07 changes Workbench through AmigaOS preferences and
  reports the applied geometry; it is required for menu-driven resolution changes.
- CPU choices are stock 68000, 68020 14 MHz and 68020 28 MHz. The 14 MHz
  profile changes execution enables on the existing clock, not the chipset clock.
- IDE sector transfers replace 256 individual host word transactions with
  one 512-byte transfer. Durable media journals retain their existing owner.
- IDE mode defaults to Auto: Fast IDE for capable 68020 profiles, Classic for
  stock 68000. Classic remains selectable for compatibility. Changing CPU or
  IDE mode saves pending disk work and restarts the guest.
- The Menu/Application key maps to Right Amiga. Control plus Left Amiga plus
  Right Amiga restarts the guest; Control + Left Windows + Menu also works.
- The optional IDE profiler records aggregate transfer timings. It is off by
  default. Reading profiler status freezes an active capture before printing.

The Fast IDE port retains the IDE portion of Minimig's pinned `fastchip.v`.
RTG and Akiko are excluded. Native tests exposed and corrected odd-word FIFO
positioning for longword accesses. The Classic Gayle path is retained.
See [host extension contracts](AMIGA_HOST_EXTENSIONS.md),
[display implementation](AMIGA_DISPLAY_OPTIONS.md) and the
[guest helper](../software/amiga/README.md).

## Exact artifacts

| Component | Identity | SHA-256 |
| --- | --- | --- |
| ARM firmware | M0.8.0dev10 / 4D000818, 5626540 bytes | `8ad006e81d039fc05a1313e3eb0de91c7f5c7b3429c32a5a2468d893b8ec7f5b` |
| Amiga FPGA | 4D000817, 4045568 bytes, CRC32 01D619BD | `4350860c4e451afd5d6a652960af5154e9248bdc140bd1c7dd5f3da072c4350f` |
| Matching package | amiga-fastide-818-817.zip, 40845241 bytes | `ddfdda163c17f4940dd3b463aff1c2ee9457282af21b50135fee7998901ba3ba` |
| Original helper07 ADF | 901120 bytes | `722d330510fc6688f81e05759eb82774ed02d536970462f21ece0d4950ff56f8` |

Runtime ABI is 0.27. The local package is
`build/amiga-ecs/amiga-fastide-818-817.zip`. It retains exact ARM and FPGA
sources, license notices, build settings and qualification evidence.
The installed SD update changed only firmware, `amiga-pal.bin` and `core.cfg`.
User ROMs, disks, settings and the existing helper were preserved, with a
local rollback backup made before publication.

No system ROM, Workbench image, HDF or user settings are distributed. Supply
Kickstart separately as described in
`sd/multitini/cores/amiga/roms/readme.txt`.

## Build and simulation results

FPGA 817 passed final nominal routed setup **+0.017 ns**, hold **+0.060 ns**,
pulse width **+1.250 ns** and native clock-enable setup **+14.345 ns**.
Strict export gates passed, including zero critical CDC crossings and matching
PS7 initialization. The setup result exceeds the required +0.010 ns gate.

The official host suite passed **131/131**. Native Fast IDE tests cover stock,
14 MHz and 28 MHz, Fast/Classic selection, complete sector reads and writes,
byte/word/longword ordering, odd FIFO positions, stalls, interrupt selection,
abort/reset recovery and management backend isolation. An upstream-packing
negative control fails as expected. Import and generated-port checks pass.

All 304 FPGA and 786 ARM captured input hashes matched the checkout before
the documentation update. The post-build C64 lifecycle fixture adds one
asserting display-restart mock outside both captured input sets; it changes
no runtime binary. This release record, its README link and the host-extension
status/recovery clarification are post-build documentation. Git's existing LF
normalization may also change raw line endings; the sealed source archives
remain the exact-build authority. Sealed binaries and source archives remain
unchanged.

## Board observations and limits

On 2026-11-01, Golden verified the 5626540-byte firmware update. PCAP loaded
the exact FPGA payload with CRC32 **01D619BD**, and the live core reported
build **4D000817**, ready=1, fault=0 and error=0.

The installed native status reports `saved_policy=Auto`, `expected=Fast`,
`fast_available=1`, `native_actual=Fast`, CPU configuration 03 and memory
configuration 33. This proves Fast IDE selection at 68020 28 MHz with 2 MiB
chip RAM and 8 MiB expansion RAM. The mounted installed Workbench HDF has
performed reads and writes. The display helper reports Ready with all four
choices available; the final observed screen is Standard, 640 by 256,
16 colours. Sampled video drops, underruns, AXI errors, deadlines and engine
errors are zero. No reboot or settings change was made for these final checks.

Earlier helper07 board checks exercised all four resolutions. The reset chord
and the 14 MHz/block-transfer baseline were tested before this Fast IDE image.
Those observations are distinct from new hardware acceptance: Fast IDE at
14 MHz and Classic/Fast switching on 817 have simulation coverage but were
not exercised during this final read-only inspection.

The earlier Classic IDE SysInfo DH0 result was 718202 bytes/s on ARM 816 /
FPGA 813. No matching Fast IDE SysInfo result is recorded here. Native mode
readback and successful disk traffic do not establish a throughput gain.
Full filesystem, application and power-loss compatibility remain broader
qualification work.

For rollback, preserve the old firmware, FPGA, core configuration and Amiga
settings together. Saving Classic uses a settings value rejected by ARM
815/816. An uncertain partial FIFO transfer requires full FPGA
reinitialization through Golden or a power cycle; selecting the already active
core is not sufficient. Keep recovery journals until storage recovery succeeds.
