# Apple IIgs M0.5.2 update

M0.5.2 replaces the IIgs FPGA image and ARM firmware. The supervisor, C64,
GBC, Vectrex and both Genesis images retain their qualified M0.3.0 identities;
NES retains its M0.5.1 image. All eight images use the same verified PS
initialization. Their original source archives and timing reports are retained.

## Startup corrections

The stock-speed reproduction uses the owner's ROM3 set and System 6.0.1
hard-disk image. All 41 baseline machine/adapter dependencies match the IIgs
M0.4.0 image that the M0.5.1 package retained.

The external CPU selector previously decoded `$C032/$C033` instead of the RTC
pair `$C033/$C034`. RTC commands never executed. The real ROM stayed in its
`$FF:BAB7` clock-control polling loop for the remainder of a 120-million-tick
run, with no disk requests. The corrected selector reaches disk boot.

The first System 6 interrupt exposed a second defect. The MMU maps
`$C071–$C07F` to system ROM, but the external CPU memory predicate excluded
every I/O address. The IRQ dispatcher therefore received stale DDR data and
jumped to `$00:0010`. Those mapped ROM reads now complete through memory
before the native commit. The reproduced interrupt reaches `$E1:0010`, runs
the handler, returns to the interrupted block move and continues reading
GS/OS files. Ordinary I/O still uses its original native timing.

Neither fix changes the 65816, microcode, clocks, speed controls or timing
exceptions. The original ISA qualification remains supported by the identical
nine CPU source hashes; it was not rerun as part of this update.

Two IWM corrections accompany the native floppy work. Fractional bit-cell and
sampling-window sums now retain their carry before comparison with 1,000.
The ten-bit remainder still fits; the addition needs eleven bits. The byte-latch
hold counter also expires in production builds. Its functional decrement and
mode-disable reset were accidentally enclosed by `DEBUG_VERBOSE`. A directed
test reproduces the stale byte after a twelve-bit gap and verifies expiry
without debug logging. These changes are reproducible derived ports; the
pinned upstream files remain exact.

## Media support

| Drive | Accepted images |
| --- | --- |
| Hard disk 1 / 2 | HDV, ProDOS PO and ProDOS 2MG, up to 32 MiB |
| 3.5-inch | Exactly 800 KiB PO or ProDOS 2MG payloads; matching WOZ2 |
| 5.25-inch | Exactly 140 KiB DOS-order DSK; matching WOZ2 |

Sector floppies are presented to the existing IWM as generated WOZ2 GCR
tracks. Writes decode the changed tracks back to their original sectors.
The firmware preserves 2MG headers, trailing bytes and write protection. It
accepts the documented WOOF/Sweet16 zero-data-length variant only when the
block count gives a bounded valid payload. NIB remains unsupported.

Each acknowledged track block has a synced `.gsraw` snapshot. The snapshot
remains until decoded sector writes have passed the existing write-ahead
journal and target sync. Recovery rejects an undecodable track and preserves
its snapshot. Keep sidecars with their images and eject before removing power.

## Functional evidence

The complete Windows host suite passed 83 checks. The common library uses the
repository's ASan/UBSan configuration; other linked libraries retain their
existing instrumentation settings. Three focused media checks were rerun
after the final parser and write-retry assertions, with matching source hashes.

Independent GSSquared encoding/decoding checked 195 synthetic tracks and
1,600 tracks from all ten 800 KiB images in the supplied folder. The original
files remained unchanged. The two larger images were rejected by the
3.5-inch inspector. The 5.25-inch comparison permits only four unused padding
bits to differ; independent decoding verifies all 256 sector data bytes.

The production adapter passes all 12 cases covering seven speed settings,
HDD DMA, native WOZ reads/writes, retry and eject. The external CPU tests cover
RTC command execution, peripheral commits and interrupt delivery. The new
IRQ regression verifies all 15 dispatch ROM bytes, the four BRK/JML fetches,
and exclusion of ordinary `$C070` I/O at stock and maximum configured speed.

The public IWM regression checks all 1,000 fractional remainder states and
natural bit-cell timing, compares 2,272 synthetic bytes through decoding, CPU
reads and acknowledgements at two polling rates, and checks byte-latch expiry
and mode disable with debug logging both off and on. All nine cases pass.

The current production acceleration regression passes nine configurations.
Its warm register loop advances once per primary tick at maximum speed:
57.272728 MHz nominal. This measures CPU advances in the selected workload;
DDR misses and native I/O reduce throughput. With cache bypass forced, the
same variable-latency memory model measures about 10.52 MHz. These are
simulation results, not physical oscillator or board measurements.

Private real-ROM tests use `scripts/test_apple_iigs_boot.py`. ROM and disk
bytes stay in ignored build fixtures. Published reports contain identities,
source hashes and outcomes; private instruction traces are not delivered.
The boot harness supplies read-only host sectors and models DDR latency;
the ARM file service is covered separately by host checks and its compiled build.
Physical board acceptance remains separate from these tests and implementation.

The final ROM3/System 6 HDD tests pass 180 million primary ticks at Stock and
maximum speed, with 219 and 324 disk reads respectively. Both execute the exact
boot-sector byte at tick 74,262,973 and the native IRQ target at tick
105,388,373, then continue guest code and disk reads with no repeated BRK loop.
Their 45 source and test hashes match the selected 187-input FPGA capture.
The exact Arkanoid 800K image also passes a 650-million-tick native floppy run:
3,995 reads complete, the disk's boot-sector opcode executes at tick
299,487,549, and the native IRQ handler executes at tick 521,150,989. Guest
code continues without a repeated BRK loop. The conversion receipt binds that
WOZ to the original private PO and the shipped firmware codec. All original
private ROM and disk hashes remain unchanged.

A separate 800-million-tick observation with the same final production sources
reaches the complete System 6 Finder desktop. It completes 1,794 HDD reads
without a repeated BRK loop. Its 43 production source hashes match the FPGA
capture; the augmented test bench differs only by verified pixel-monitor
additions. Native VGC pixels were inspected directly. HDMI and physical board
behavior remain hardware checks.

## Implementation and firmware

The final IIgs implementation passes strict setup, hold and pulse-width gates:
`+0.038 ns`, `+0.039 ns` and `+1.250 ns` respectively. The primary clock remains
57.272728 MHz. The 187 captured source/settings inputs remain unchanged through
implementation; the source ZIP contains those inputs and the three standard
build-configuration files.
The routed design uses 35,737 LUTs, 24,504 flip-flops, 119 RAMB36 plus 10 RAMB18
blocks (124 of 140 equivalent tiles), and 24 DSPs. All 55,599 nets route;
critical CDC findings and waivers are zero.

The first routed attempt missed one video path by 0.041 ns. A general physical
optimization reduced that to 0.029 ns. Eight physical replicas of the verified
115-load framebuffer selector, followed by a full reroute, close the path.
The RTL and final clock/timing requirements are unchanged. Exact target census,
commands, checkpoints and before/after reports are retained.

Vivado 2025.2 also reproduces the known loss of three serialized multicycle
selectors during reporting. The existing IIgs repair reserializes the exact
four canonical setup/hold pairs. Scope/equivalence checks run after reporting,
the final timing summary and export; the numeric timing policy is unchanged.
The original failed attempts remain separate from the selected final evidence.

The matching CPU0 and CPU1 firmware build is M0.5.2, ID `4D000502`, ABI 0.22.
The build uses the qualified supervisor platform; its PS initialization matches
the new IIgs XSA and all seven retained FPGA images. Payload validation checks
the CPU1 image embedded in CPU0, ELF load segments, FSBL and selected bitstreams.

## Reproduce a fresh build

Use Vivado/Vitis 2025.2. Run the directed checks and private boot regression
before implementation. Point `--rom-source` to prepared runtime ROMs and
`--disk` to the owner's System 6 image. Use `--max-primary-ticks 180000000`,
`--min-reads 80` and `--require-irq-dispatch` at `--speed 0` and `--speed 6`.
The native floppy case uses `--floppy-woz`, `--raw-floppy` and the production
conversion receipt, with a 650,000,000-tick budget. The earlier 300,000,000-tick
attempt expired just before the observed boot-sector handoff. Use a fresh build
directory for each run. A fresh IIgs build uses:

```powershell
python scripts/port_apple_iigs_core.py --check
python scripts/audit_apple_iigs_import.py
python scripts/test_apple_iigs_iwm.py --build-dir build/reproduce-gs-m052/iwm
python scripts/test_apple_iigs_irq_dispatch.py --build-dir build/reproduce-gs-m052/irq
python scripts/test_apple_iigs_adapter.py --build-dir build/reproduce-gs-m052/adapter
python scripts/build_core_gs.py --image apple-iigs --build-id 4D000502 --output build/reproduce-gs-m052/hardware --plan
python scripts/build_core_gs.py --image apple-iigs --build-id 4D000502 --output build/reproduce-gs-m052/hardware
```

The delivered implementation receipts also preserve the exact generated
wrapper used to reuse eight unchanged IP synthesis checkpoints after the
superseded startup-only attempts. It replaces only the two original IWM/drive
project entries with their reproducible derived ports and records active
synthesis order. Full top synthesis and implementation still run; the saved
IP input, setting and output hashes must match before reuse. A fresh build
above creates its own IP outputs and does not need that recovery overlay.

Build the matching ARM firmware in a fresh short Vitis directory, using the
qualified supervisor XSA and exported platform as described in
`APPLE_IIGS_PLAN.md`. Pass `--build-id 4D000502 --abi-minor 22 --include-nes`
to `scripts/package_core_gs.py`, explicitly select the new IIgs directory and
the seven retained image directories, and supply the private boot-ROM source.
Use `--check-only` before creating a fresh package directory.

The private package is `build/firmware-core-gs-m052.zip`. It includes the ROMs
in each core's `roms` directory and media loaders, with no games or OS disks.
IIgs disks belong in the sibling `multitini/cores/apple-iigs/media/` directory.
See `APPLE_IIGS_HARDWARE_TEST.md` for installation and physical acceptance.
