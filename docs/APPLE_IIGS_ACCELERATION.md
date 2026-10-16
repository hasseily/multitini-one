# Apple IIgs acceleration rework

The owner reported that 57 MHz felt barely twice as fast as stock in real use:
booting, Finder window drawing and applications. This record covers the
measurements that explained it, the changes on branch `iigs-accel-fixes`, and
the simulation evidence. The owner now confirms System 6.0.1 boots on the
M0.5.4 core, with measured boot times of 59 seconds at Stock, 37 seconds at
14 MHz and 35 seconds at 57 MHz. Live ARM measurements now identify about
32 seconds of USB polling inside the HDD transfer path. M0.5.6 removes that
penalty without rebuilding the FPGA. Synthetic CPU throughput still does not
establish boot or UI throughput.

## M0.5.6 HDD transfer pacing

The stream FIFO briefly deasserts READY after each four-byte publication.
`mt_core_link_stream` calls its progress callback before retrying. The IIgs
callback previously ran the full USB host poll every time. A real System 6
boot performed 236,422 callbacks: 1,793 reads with 128 burst waits and three
command waits each, plus three writes with 512 byte waits and one completion
wait each. This placed about 32 seconds of ARM work in the disk path regardless
of the selected 65816 speed.

The IIgs callback now services USB at most once per millisecond during a
transfer. The normal runtime loop still polls USB between disk requests.
Command ordering, stream data, timeouts and cancellation are unchanged.
The USB OSAL millisecond clock also now evaluates the BSP frequency macro
before division; its unparenthesized `CPU_FREQ/2` expansion previously made
that clock run at one quarter speed.

The owner-authorized Golden UART update installed M0.5.6 on the board.
Golden verified file and flash CRC32 `F5A33519`; the running ARM banner reports
`M0.5.6 build=4D000506`. All eight FPGA images and the CPU1 video worker remain
byte-identical to the retained qualified inputs.

These live measurements use ROM3, the owner's System 6.0.1 HDD, ZipGS, all four
compatibility delays off, other drives ejected and the menu closed. Time starts
at IIgs reset release. **The endpoint is the final completed HDD request,
not an automated assertion that Finder is usable.** Profiling is enabled.

On 2026-10-16 the owner separately confirmed that M0.5.6 at 57 MHz reaches a
usable Finder desktop in roughly five seconds, with working mouse/keyboard
and normal window drawing: "Yes, fast and working normally". This is physical
board acceptance for that boot and desktop workflow. The approximate visual
timing is separate from the precise disk-completion timestamps below.

| CPU setting | Transfer callback | Final HDD completion | HDD service total | Progress callback total | Reads / writes |
| --- | --- | ---: | ---: | ---: | ---: |
| 57 MHz | Original, same M0.5.6 image | 36.573 s | 33.932 s | 32.102 s | 1,793 / 3 |
| 57 MHz | Paced | 4.735 s | 1.907 s | 0.806 s | 1,793 / 3 |
| 14 MHz | Paced | 5.670 s | 1.725 s | 0.782 s | 1,793 / 0 |
| Stock | Paced | 24.552 s | 1.941 s | 0.845 s | 1,793 / 3 |
| 57 MHz repeat | Paced | 4.717 s | 1.905 s | 0.808 s | 1,793 / 3 |

Every request completed without an error. Every run read 918,016 bytes from
the same image. At 57 MHz the fix saves 31.838 seconds at the measured endpoint,
a 7.72x improvement. SD reads take about 0.64 seconds in the paced runs.
Progress is a subset of HDD service; do not add overlapping stage totals.

After the fix, time outside HDD service is 22.611 seconds at Stock, 3.945 at
14 MHz and 2.828 at 57 MHz. This remainder includes CPU execution, DDR waits,
firmware request discovery and emulated device timing; it is not a CPU-only
counter. The remaining 14-to-57 MHz difference cannot be assigned entirely to
DDR from these ARM measurements. The selected production-model trace also
contains about 0.95 seconds in the SmartPort/IWM ROM wait loop and 147,905
sound-GLU writes; their device cadence does not scale with the CPU selector.
Those are model observations, not measured board stall counters.
The model uses compatibility flags `0x10` versus `0x17` on the board, so its
individual wait times cannot be subtracted from these hardware captures.

The opt-in UART commands are `:gsprofile boot 0`, `:gsprofile boot 4` and
`:gsprofile boot 6` for Stock, 14 and 57 MHz. They safely reset/remount the
selected IIgs and start timing at reset release. Append ` baseline` to restore
the original progress behavior for that capture. `:gsprofile stop` freezes
and prints the report, then restores paced behavior. Select ZipGS first.
The diagnostic emits no UART output during the measurement interval.

All 88 host tests pass. Permanent production-linked checks cover the BSP clock
macro, rollover, callback pacing and serial routing. The real stream-code
probe checks byte integrity, timeout and cancellation. The earlier v1 capture
had a diagnostic clock conversion error: its absolute durations are rejected
and retained only as raw evidence. All results above use the corrected v2
clock and pass a comparison against the host observation window.

Evidence: `build/iigs-accel-diagnosis/hardware-timing-20261016/m056-hardware-validation.json`,
the five referenced raw captures, `build/iigs-accel-diagnosis/host-final-validation.json`,
and `build/iigs-accel-diagnosis/permanent-serial-clock-progress-validation.json`.
The owner confirmation is recorded in
`build/iigs-accel-diagnosis/hardware-timing-20261016/m056-owner-acceptance.json`.
The residual timing review is
`build/reviews/iigs-m056-residual-acceleration-review.json`. The complete private
delivery is `build/firmware-core-gs-m056-tested.zip`; it includes the tested
firmware, retained cores, private boot ROMs, media loaders, source and evidence.

## M0.5.5 accelerator-card selection

Previously, None hid the ZipGS registers but retained acceleration. A focused
executed-CPU review measured the same 2.86/14.32/57.27 MHz warm-loop rates with
ZipGS and None. It also reproduced guest speed and disable overrides surviving
a card-only change because the unchanged host speed was not reapplied.
The reported 37-second None boot was therefore not a stock-speed baseline.

M0.5.5 makes None force Stock and disables its speed control. Older saved None
configurations with a nonzero speed normalize to Stock. Changing card mode
saves media and uses the existing full IIgs reset/reload/remount path, which
clears guest ZipGS speed, enable, unlock, cache, slot and delay state. Menu
delay choices apply again after reset. Returning to ZipGS starts at Stock;
subsequent speed-only changes remain live. This uses the current FPGA reset
interface and requires only an ARM firmware rebuild. No FPGA build is run.

The package is `build/firmware-core-gs-m055.zip`, with M0.5.5 firmware and all
eight explicitly reused qualified images. The IIgs bitstream is the unchanged
M0.5.4 image identified below. Required private ROMs remain in each core's
`roms` directory; media loaders and empty media directories are included.
The new card-selection behavior still needs a hardware check.

All 87 host checks pass, including mode-change media ordering, legacy saved
settings, rejected changes and rollback failures. The production CPU/adapter
regression retains its ten live-speed/IRQ cases and adds twelve measurements
across three guest overrides and nine machine resets. These use only the
existing core-reset command boundary, with no global FPGA reset. Guest writes
dirty ZipGS speed/disable, unlock, cache, slot and delay state before each
transition. None executes the same writes without enabling acceleration;
returning to ZipGS starts clean, then reaches 14.32 and 57.27 MHz in the warm
loop. Both negative controls fail: omitting the reset leaves stale state, and
omitting None-speed normalization leaves acceleration enabled. Evidence is in
`build/host-gs-m055/ctest.log` and
`build/reviews/iigs-card-mode-reset-production/validation.json`.

Reproduce the ARM-only build and private package with the explicit retained
platform and all eight images:

```powershell
$env:MULTITINI_VITIS_WORKSPACE = "$PWD/build/gs055"
$env:MULTITINI_XSA = "$PWD/build/core-gs/hardware/supervisor/multitini_one.xsa"
$env:MULTITINI_PLATFORM_XPFM = "$PWD/build/core-gs/vitis/multitini_platform/export/multitini_platform/multitini_platform.xpfm"
$env:MULTITINI_CPU1_BASELINE = "$PWD/build/firmware-core-gs-m054b"
$env:MULTITINI_SKIP_GOLDEN = '1'
$env:XILINX_VITIS = 'C:/Xilinx/2025.2/Vitis'
python scripts/run_native_tool.py -- C:/Xilinx/2025.2/Vitis/bin/vitis.bat -s scripts/create_vitis_workspace.py
python scripts/package_core_gs.py --hardware build/core-gs-m054h/hardware --workspace build/gs055 --build-id 4D000505 --abi-minor 22 --include-nes --suffix core-gs-m055 --output build/firmware-core-gs-m055 --reuse-image supervisor=build/core-gs/hardware/supervisor --reuse-image c64-pal=build/core-gs/hardware/c64-pal --reuse-image gbc=build/core-gs/hardware/gbc --reuse-image vectrex=build/core-gs/hardware/vectrex --reuse-image genesis-ntsc=build/core-gs/hardware/genesis-ntsc --reuse-image genesis-pal=build/core-gs/hardware/genesis-pal --reuse-image nes=build/core-nes-m051/hardware/nes --reuse-image apple-iigs=build/core-gs-m054h/hardware/apple-iigs --evidence build/host-gs-m055/ctest.log --evidence build/reviews/iigs-card-mode-reset-production/validation.json
```

## What limited the old design

A per-tick profile of the production adapter, booting the owner's ROM3 and
System 6.0.1 HDD, plus synthetic 65816 loops, found three main limits at the
maximum speed:

1. Every bank E0/E1 access, shadowed write and I/O access waited for the
   emulated 1 MHz Mega II sync, 60–75 primary ticks each. Any loop with one
   screen store ran exactly 2.0x faster than stock. The core has no physical
   bus, so this sync is not needed while accelerated.
2. Every fast-RAM write blocked for a complete DDR round trip. The cache was
   write-through, had one transaction in flight, and crossed two clock-domain
   handshakes each way.
3. A Finder startup wait of about four seconds came from the IWM model, not
   the empty floppy drive. The GS/OS 5.25-inch driver sets IWM mode `$04`
   through `$C0ED` to make motor-off immediate. The model only accepted mode
   writes at `$C0EF`, so the driver waited out the one-second motor timer three
   times.

HDD DMA at native pace, slot ROM at 1 MHz, 8-byte cache lines and the 2 ms IWM
hold without a media check cost smaller amounts.

## Changes

Stock speed keeps its native cadence: with the accelerator off, or while any
compatibility hold forces native timing, the clock divider receives its
original inputs.

- **No 1 MHz sync while accelerated.** `iigs.sv` feeds the upstream
  `clock_divider` gated `valid`, `slot_access`, `cyareg[3:0]` and `dma_active`
  inputs when external and accelerated. I/O stays on the native bridge, one
  native commit per access, so peripheral side effects and holdoff arming are
  unchanged. Slot reads get a second preparation edge for the two-register
  HDD ROM output.
- **Primary-clock Mega II RAM.** Slow-RAM port A runs on the CPU clock. E0/E1
  RAM and shadowed writes complete in the adapter's fast path: reads in three
  primary ticks from registered data, writes staged and applied on the next
  edge that is not the video port's read edge. Native and DMA writes are
  staged the same way.
- **Posted writes and burst lines.** The cache has a four-entry merging write
  buffer behind a one-cycle retire slot. Writes retire on acceptance. Reads
  that miss, bypass reads and loader requests wait for the buffer to drain.
  A read in the cycle that retires a written byte, or a staged HDD DMA byte,
  stalls instead of hitting the old word. Lines are 32 bytes, filled by one
  four-beat burst. Bypass mode keeps one single-beat DDR read per CPU read.
- **Synchronous DDR path.** In the IIgs image only, `S_AXI_HP2_ACLK` comes from
  the machine clock and the DDR path is a single-clock AXI3 master. Genesis
  and NES keep HP2 on FCLK0. Quiescence counts only after the core reset
  request reaches the machine domain. A machine reset no longer abandons an
  AXI handshake. After a sticky fault, posted writes are dropped so the
  pre-reload drain still completes.
- **DMA.** Accelerated HDD DMA runs at three CLK14 ticks per byte, without
  the 1 MHz sync for E0/E1 or shadowed targets.
- **Sound GLU hold.** Without the sync, `$C03C–$C03F` accesses could outrun
  the GLU, which runs one host access per 16 CLK14 ticks. Lost DOC accesses
  made the ROM interrupt handler read stale values and never clear VBL, so
  GS/OS stalled at startup. The native commit for these addresses now waits
  while the GLU is busy.
- **IWM.** A new derived port, `ports/iwm_woz.sv`, latches the mode register
  on any odd address with Q6=Q7=1 while idle, except the `$C0E9` motor-on
  access. The immediate mode view, which the drives and the status register
  see on the write tick, uses the same decode. The 2 ms IWM hold applies only
  when the selected drive has media.
- **Timing structure.** No new logic drives a RAM write pin or a wide
  register enable from the CPU enable in the same cycle. The retire slot
  loads its address and byte every cycle; only its valid bit depends on the
  CPU enable. The phase ticks carry `DIRECT_ENABLE` so control-set reduction
  keeps them on CE pins, as the build's phase proof requires.

## Simulation results

The committed acceleration gates measure real 65816 loops on a synthetic ROM
with a DDR model that answers in a few cycles. They assert these ratios of
stock to maximum speed:

| Gate loop | Stock ticks | Maximum ticks | Ratio | Assertion |
| --- | ---: | ---: | ---: | --- |
| Shadow-RAM read loop | 592,636 | 26,879 | 22.0x | one tick per CPU advance |
| Store to shadowed bank 00 | 167,508 | 6,944 | 24.1x | at least 20x |
| Store to bank E1 | 167,004 | 6,944 | 24.0x | at least 20x |
| Load from bank E1 | 167,004 | 7,439 | 22.4x | three ticks per fast read |
| HDD DMA of 512 bytes into E1 | 28,680 | 6,132 | 4.7x | three CLK14 ticks per byte |

Stock values are pinned exactly. The shadow loops match the published M0.5.3
records. M0.5.3 has no record for the bank-E1 loops, so their stock values
are pinned to the first measurement on the M0.5.4 sources.

The next two tables come from profiling benches kept outside the
repository: a synthetic loop bench on the production adapter, and a boot
profile that needs the owner's private ROM and disk. They are estimates, not
regression gates. "Before" was measured on the M0.5.3 sources.

Synthetic loops, primary ticks per iteration, stock against 57 MHz. The DDR
model answers in about 25 primary cycles, an estimate of the board's HP2 path.

| Loop | Before | Now | Now, DDR latency doubled |
| --- | ---: | ---: | ---: |
| Register-only loop | 20.0x | 20.0x | 20.0x |
| JSR, PHA, PLA, RTS | 3.8x | 15.8x | 15.7x |
| 16-bit store to fast RAM | 4.2x | 17.4x | 17.4x |
| Store to E1 screen | 2.0x | 21.0x | 21.0x |
| Store to shadowed bank 01 | 2.0x | 24.0x | 24.0x |
| Copy E1 to E1 | 2.0x | 22.2x | 22.2x |
| Read-modify-write on E1 | 2.0x | 21.0x | 21.0x |
| MVN of 4 KiB | 4.0x | 12.5x | 11.1x |
| Cache-missing reads | 8.4x | 14.0x | 12.1x |
| JSL to RTL in bank E1 | 2.3x | 17.9x | 17.9x |
| Keyboard register poll | 2.0x | 12.0x | 12.0x |

Boot of the owner's System 6.0.1 HDD with the same DDR model, primary ticks:

| Configuration | 1,700 disk reads | Finder ready |
| --- | ---: | ---: |
| Stock, before | 1,233M | about 1,650M |
| 57 MHz, before | 356M | over 600M |
| Stock, now | 1,090M | about 1,450M |
| 57 MHz, now, default delay flags | 96M | about 180M |
| 57 MHz, now, all delays off | 87M | about 160M |

"Finder ready" is the end of the Finder's startup drive polling, measured the
same way for every row. The desktop is fully drawn slightly earlier.

The repository boot tests pass on the final tree. Stock HDD boot reaches the
boot-block opcode at tick 74,255,773 and the native IRQ handler at 105,391,669,
against 74,262,973 and 105,388,373 before. Stock native cycles and stock DMA
bytes no longer stretch while waiting for a DDR write acknowledgement; the
per-cycle stock cadence of the speed regressions is unchanged. The maximum-speed
HDD boot completes all 1,794 reads within 180M ticks; M0.5.3 completed 324.
The maximum-speed Arkanoid 800K floppy boot passes, reaching its boot block at
tick 241,244,069 and the native IRQ handler at 462,851,725, against
248,755,821 and 470,494,917 for M0.5.3. Floppy loading is paced by the disk
itself: with media mounted, IWM activity correctly holds native timing.

The fast regression set passes, including:

- acceleration gates with ten negative controls: slot preparation edge, IWM
  media gate, IWM mode write, IWM motor-on exclusion in the mode register and
  in the immediate mode view, native slow-RAM edge, paddle alias,
  shadow-write class, sound GLU hold, and that hold for reads;
- a sound GLU program at speeds 3 and 6 that writes DOC registers and wave
  RAM, then reads them back with the GLU read protocol;
- the cache unit test with retire-slot, read-after-write, DMA-commit, fault
  drain and loader read-back cases, including a CPU read in the cycle that
  retires the last DMA byte, with negative controls for both hit blocks;
- 40 memory-phase scenarios in each cache mode. Read pauses stop a target
  read-back during its line fill or single-beat bypass read, and stop a read
  that waits for posted writes to drain. With the cache on, that read is the
  read-back of the word just written. In bypass mode every fetch is a DDR
  read, so it is the fetch after a store;
- the DDR path test with bursts, faults, resets mid-transaction and the
  quiescence window;
- adapter, external CPU, logical cache, CPU peek, IRQ dispatch, paused
  settings, slow RAM, IWM, video, ports, native clock and build-input tests.

## Implementation and package

The first routed build of the merged change failed setup by 1.782 ns. Retired
writes reached cache RAM write pins and the write-buffer enables in the same
cycle as the CPU enable, and the slow-RAM decode reached the CPU data input.
The retire slot, staged slow-RAM writes, registered slow-RAM reads and a
reordered CPU read-data mux remove those paths. Vivado's control-set reduction
also folded the phase-0 publication enables into data logic, which broke the
build's phase proof; `DIRECT_ENABLE` on the phase ticks restores real CE pins.

The final tree builds with Vivado 2025.2 and meets timing at signoff with no
repair pass. Every export gate passes:

| Routed check | Result |
| --- | ---: |
| Worst setup slack | +0.007 ns |
| Worst hold slack | +0.034 ns |
| Worst pulse-width slack | +1.250 ns |
| Failing endpoints | 0 |
| Routed nets, routing errors | 54,203, 0 |
| Critical CDC rows | 0 |
| LUTs / flip-flops | 34,635 / 24,310 |
| RAMB36 / RAMB18 / DSP blocks | 120 / 9 / 24 |

The setup margin is as narrow as M0.5.3's. The worst path runs from the
65816 microcode ROM to a ZipGS register input, within one primary-clock
period. An earlier build of this branch missed setup by 0.079 ns and needed
one `repair_phos1_timing.py --directive AggressiveExplore` pass; use the same
pass if a rebuild misses by a few picoseconds.

The image's build id is `4D000505`. Its `ps7_init.c` hash is unchanged
(`7f046da6…`), so the existing FSBL and platform apply. The bitstream SHA-256
is `169e513532ac727b82b2bf4bee1eaf7351fa231392bebbe930699060108e1746`.

The board-test bundle is `build/firmware-core-gs-m054b.zip`. It contains the
unchanged M0.5.3 firmware, the six retained FPGA images and the new IIgs
image. Its `SHA256SUMS.txt` identifies every installed file. Required private
ROMs are under each core's `roms` directory; no games or OS disks are
included. Reproduce with:

```powershell
python scripts/port_apple_iigs_core.py --check
python scripts/build_core_gs.py --image apple-iigs --build-id 4D000505 --output build/core-gs-m054h/hardware
python scripts/package_core_gs.py --hardware build/core-gs-m054h/hardware --workspace build/core-gs-m053/vitis --build-id 4D000503 --abi-minor 22 --include-nes --suffix core-gs-m054b --output build/firmware-core-gs-m054b --selection-manifest build/core-gs-m054h/hardware/core-selection.json --reuse-image supervisor=build/core-gs/hardware/supervisor --reuse-image c64-pal=build/core-gs/hardware/c64-pal --reuse-image gbc=build/core-gs/hardware/gbc --reuse-image vectrex=build/core-gs/hardware/vectrex --reuse-image genesis-ntsc=build/core-gs/hardware/genesis-ntsc --reuse-image genesis-pal=build/core-gs/hardware/genesis-pal --reuse-image nes=build/core-nes-m051/hardware/nes
```

## Open items

- M0.5.6 HDD boot, usable Finder, mouse/keyboard and window drawing at 57 MHz
  are owner-confirmed on the board. Remaining checks cover sound, floppies,
  ZipGS controls and core switching at stock and maximum speed.
- The DDR latency used in these figures is an estimate. The cache keeps
  hit, miss and wait counters that could be exposed to measure it on the board.
- Accelerated I/O no longer waits for the 1 MHz sync. Software that times
  itself by I/O access spacing may run differently; the speaker, paddle,
  counter, IRQ and IWM holds still restore native timing where they apply.
- The last second of the Finder startup wait is genuine IWM behavior with a
  5.25-inch drive reported as attached.
