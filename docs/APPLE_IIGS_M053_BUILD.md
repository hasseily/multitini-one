# Apple IIgs M0.5.3 update

M0.5.3 corrects acceleration holdoffs and adds saved IIgs mouse sensitivity.
It replaces the ARM firmware and IIgs FPGA image. The supervisor, C64, GBC,
Vectrex and both Genesis images retain their qualified M0.3.0 identities;
NES retains M0.5.1. Their exact source archives and timing reports are retained.

## Acceleration corrections

The owner's System 6.0.1 HDD reproduction showed an empty 5.25-inch drive's
motor latch holding the CPU at native speed. This persisted with speaker,
joystick, counter and IRQ delays all disabled. The compatibility gate now
requires mounted media of the matching drive family. It preserves raw IWM
status, mounted-drive timing and the separate two-millisecond IWM access hold.

IRQ dispatch ROM reads at `$C071–$C07F` also triggered the paddle delay. The
paddle hardware only decodes `$C070`; the acceleration gate now uses that
same full address. Reads of fast RAM with a slow shadow mirror now use the
fast CPU path. Shadow writes, direct Mega II memory, I/O and DMA retain their
native timing. The original imported sources remain byte-exact, and all 20
derived ports are reproducible from the pinned import.

Fresh firmware settings default IRQ delay to Off, matching the original
ZipGS AppleTalk-delay switch default. Existing valid saved settings preserve
their choices. Set IRQ delay to Off in Core options for performance checks.
Multitini's speed controls require no GS/OS driver; optional ZipGS control
software supplies an additional interface inside GS/OS. See the
[original Zip Technology manual](https://www.whatisthe2gs.apple2.org.za/files/ZipGSX/Manual/ZIPGSX_Manual.pdf).

The menu's requested speed reaches the running CPU through the existing
configuration loader. A paused-core regression applies all seven rates to the
same CPU instance without reset, verifies unchanged architecture while paused,
and measures resumed CPU progress. It also changes only the IRQ-delay flag
around an interrupt-masked loop and verifies that the hold is applied and
removed. All ten checks pass with the final production HDL.

## Throughput and limits

The focused gate regression passes eight groups, including 49,664 shadow
mapping/configuration comparisons and 64 motor/media combinations. Three
negative controls run the previous source and fail the intended paddle,
shadow-read and empty-motor assertions. Adapter, RTC, native event, IRQ
dispatch, ZipGS software mappings and peak-rate regressions also pass.

| Synthetic workload | Stock primary ticks | Maximum primary ticks | Speedup |
| --- | ---: | ---: | ---: |
| Warm shadow-RAM reads | 592,636 | 26,879 | 22.0483x |
| Native shadow stores | 167,508 | 83,521 | 2.00558x |

The first timed loop covers 3,840 iterations and 26,879 CPU advances at either
rate after 256 warmup iterations. The store test checks all 1,024 byte writes
and their slow-memory mirrors across 512 sixteen-bit stores. Its timed
interval excludes 16 warmup iterations and covers the remaining 496 stores
(992 target byte writes) and 6,447 CPU advances.
The 57.272728 MHz setting describes eligible CPU advances. Native screen
writes and DDR misses limit application throughput; Finder redraw cannot be
assumed to scale with the selected MHz. These are simulations of the production
CPU and adapter, not measured hardware or a complete Finder benchmark.

The real System 6.0.1 HDD comparison uses the same private ROM/media bytes,
maximum selected speed and all four compatibility delays Off (`0x17`). At
180 million primary ticks, the corrected core completes 28,781,972 CPU
advances versus 10,776,502 from the immutable M0.5.2 source snapshot: 2.6708x
equal-time progress. Its first native IRQ target executes at tick 28,926,125,
versus 46,130,373 before correction. This is boot progress through different
amounts of guest work, not a window-redraw timing measurement.

The old empty-drive motor hold occupies 83.09% of that run, even with no
floppy mounted and all four delay flags disabled. The corrected qualified
motor hold stays zero. CPU cache waits still occupy 23.17% of the corrected
run and native paths retain their compatibility timing. These counters
overlap; they must not be added into a purported total slowdown.

The corrected run completes 400 million primary ticks, 1,794 HDD reads and
the native boot/IRQ assertions without a repeated BRK loop. Inspection of
native VGC pixels shows the Finder desktop, menu bar, HARDDISK and Trash.
During the final 100 million ticks, Finder's repeated drive probes trigger
the retained IWM-access delay for 93.91% of ticks. That compatibility hold
is separate from the corrected empty-motor latch. It remains a limit on
acceleration and is not disabled by the four speaker/paddle/counter/IRQ flags.
No interactive redraw benchmark or physical HDMI test was performed.

The final stock-speed ROM3/System 6 HDD test passes 180 million ticks with
219 completed reads. It executes the disk boot byte at tick 74,262,973 and
the native IRQ target at tick 105,388,373. The exact supplied Arkanoid 800K
image passes 650 million ticks through the native IWM at maximum speed:
4,033 requests complete, the boot byte executes at tick 248,755,821 and the
native IRQ target at tick 470,494,917. Both use fresh-default flags `0x10`,
finish without a repeated BRK loop and retain exact current source hashes.
Original private ROM and disk bytes remain unchanged.

The 65816 leaf implementation, microcode, clocks and timing requirements are
unchanged. The earlier ISA qualification is retained by matching all nine CPU
source hashes, rather than rerunning unchanged instruction tests.

## Mouse sensitivity

IIgs Core options offers Mouse sensitivity at 25%, 50%, 75%, 100%, 150%, 200%,
300% and 400%. Changes apply immediately and persist across restarts. The
default is 100%, including older settings files. The firmware scales IIgs
relative movement before the existing input transport, retaining fractional
movement at low settings. Buttons and the shared menu cursor are unchanged.

All 87 host checks pass. Two manifest tests initially encountered a temporary
directory permission failure and passed when rerun with workspace-local
temporary storage, without source changes. The four focused IIgs/USB checks
cover every sensitivity, signed fractional cancellation, saturation, button
delivery, packet splitting and exact retries, menu capture, disconnect,
legacy settings, saved paths and failed-save rollback. Strict main, USB and
IIgs service compilation and independent source review also pass.

## Build and delivery

The firmware identity is M0.5.3, build ID `4D000503`, ABI 0.22. The fresh Vitis
ARM build passes compiled identity, exact source, CPU1 embedding, FSBL and PS
initialization checks. It includes the final mouse changes; the earlier
pre-mouse build is preserved separately and is not selected for delivery.
A fresh IIgs Vivado build captures 191 source/settings inputs and the standard
generated build configuration. The selected routed image passes at the
57.272728 MHz target:

| Final routed check | Result |
| --- | ---: |
| Worst setup slack | +0.002 ns |
| Worst hold slack | +0.036 ns |
| Worst pulse-width slack | +1.250 ns |
| Setup/hold/pulse-width failing endpoints | 0 / 0 / 0 |
| Routed nets | 55,658 |
| Critical CDC rows | 0 |
| LUTs / flip-flops | 35,833 / 24,483 |
| RAMB36 / RAMB18 / DSP blocks | 119 / 10 / 24 |

The setup margin is narrow. These are worst-corner static timing results,
not physical-board acceptance. Native clock-enable setup and hold slack are
+9.597 ns and +3.393 ns. The phase/publication checks, all eight effective
multicycle coverage rows, source archive checks and matching PS initialization
pass. No new timing exception or clock change was needed.

The original route missed setup by 0.194 ns. Physical replication and rerouting
reduced the miss to 0.135 ns; an AlternateReplication pass reached 0.132 ns.
A proposed post-route force-replication attempt was rejected by Vivado before
changing the checkpoint. The final supported pre-route replication pass
targeted the remaining floppy-control and slow-RAM data branches, then rerouted
and ran AggressiveExplore. Its temporary 0.100 ns primary-clock optimization
margin was restored to the original value before strict signoff. RTL and
numeric timing policy remained unchanged throughout these physical repairs.

The package retains the exact generated Tcl, arguments, logs, before-repair
reports and hashes for all attempts under the selected IIgs image's
`reports/repair` archive. Pass 4 is the successful selected export. The final
hardware audit binds that log, both final checkpoints, the bitstream, XSA and
the unchanged 191-input capture. The selected bitstream SHA-256 is
`70d41c60fd3ac0ffe31c5beb4c5a93d3635b3dd1072e4f3957f227e6abae38a9`.

Use Vivado/Vitis 2025.2 and fresh output directories:

```powershell
python scripts/port_apple_iigs_core.py --check
python scripts/audit_apple_iigs_import.py
python scripts/test_apple_iigs_accel_gates.py --build-dir build/reproduce-gs-m053/gates
python scripts/test_apple_iigs_paused_settings.py --build-dir build/reproduce-gs-m053/settings
python scripts/build_core_gs.py --image apple-iigs --build-id 4D000503 --output build/reproduce-gs-m053/hardware
```

The private boot runner accepts `--compatibility-flags 0x10` for the fresh
IRQ-delay-Off default, `0x17` for all four delays Off, or `0` for the previous
defaults. ROM and disk bytes remain private inputs. Package the matching fresh
ARM build and selected IIgs image with `scripts/package_core_gs.py`, using
`--build-id 4D000503 --abi-minor 22 --include-nes` and explicit reuse paths for
the seven other images. Validate with `--check-only` before packaging.

The delivery is `build/firmware-core-gs-m053.zip`. Required private ROMs are
under each core's `roms` directory; media loaders and empty media directories
are included. No games or OS disks are included. Current sources and FPGA
source archives contain no private ROM or media bytes. Install matching
firmware and the IIgs image together, then follow
[the board checks](APPLE_IIGS_HARDWARE_TEST.md). Physical acceptance remains
separate from simulation, compilation and routed timing.
