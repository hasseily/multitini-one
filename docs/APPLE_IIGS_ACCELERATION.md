# Apple IIgs acceleration rework

## FPGA 4D000609 timing closure

The first complete 4D000609 build failed setup by 0.984 ns on 2,314
endpoints. It included four later timing-only RTL changes: grouped L0 tag
equality, a merged WOZ track-RAM address mux, precomputed CPU address
increments and a rewritten `adder4`. Nearly every failing path starts at the
CPU microcode ROM and crosses address generation and MMU/IO decode. About 75%
of each path is routing. In Fast mode the floppy engine enables also depend on
the CPU's same-cycle address (`iwm_native_pending`, `fast_floppy_stall`),
which puts WOZ, IWM and flux logic on the CPU's critical paths. Across today's
builds the routed setup slack ranged from -0.19 to -1.57 ns.

Eight placement variants of that netlist reached -0.56 to -1.29 ns. Repeated
unroute/reroute repair reached -0.23 ns at best. The earlier `gs609-hdl1`
netlist, without the four changes, had -0.186 ns on 60 endpoints. One repair
pass closed it: 0.100 ns temporary machine-clock margin, unroute,
AggressiveExplore physical optimization, Explore routing, then the normal
0.200 ns video-margin AggressiveExplore pass. All temporary margins are
cleared before measurement. No RTL, clock rate or timing exception changed.

The working tree was restored byte-exactly to the `gs609-hdl1` capture. The
four changes are kept in `build/gs609-post-hdl1-tweaks/`. The storage overlap
logic is unchanged. The normal export gates pass in a fresh process: setup
slack `+0.027 ns`, hold slack `+0.053 ns`, pulse-width slack `+1.250 ns`,
complete routing, phase proofs, CDC, bus skew and timing coverage. The Fast
floppy overlap (15 cases, 3 negative controls) and protocol suites were rerun
on these exact sources and pass; all 16 protocol track outputs are
byte-identical to the baseline 609 run. The SD
update is `build/gs609r-delivery/core609-sd.zip`; evidence is in
`core609-evidence.zip` beside it. This image has not been tested on the board.

Open item: the floppy-enable coupling to the CPU address remains. Further
growth in this logic will likely need those enables registered first.

## Fast floppy storage overlap

Independent CPU work can continue during a Fast floppy track load or flush
outside the existing two-millisecond IWM guard, guest-requested native delay
and active write protocol. Its configured CPU pacing continues while the
floppy's virtual timeline waits for storage. Cache, pause, reset and DMA
holds remain effective. Normal mode keeps its existing behavior.

An IWM access waits for storage and completes through the existing captured
request/response path. Protected driver delays stay coupled to the paused
disk; letting those loops run could feed the next serial write byte too early.
The backend and pending final write bits continue to drain. This change adds
no clock, reset or timing exception. Compatibility tests, implementation
timing and physical acceptance are separate qualification steps.

## M0.6.5 optional fast floppy timing

The candidate pairs ARM M0.6.5 (`4D000605`) with IIgs FPGA `4D000607`.
Options > Floppy speed selects Normal or Fast. Normal is the default. Changing
the option flushes mounted media with the old timing, resets the machine,
applies the new configuration and remounts the images. The setting is saved;
older FPGA images mask the unsupported option and show it as unavailable.

Fast advances the floppy controller, serial bit cells, mechanics and the
CPU's associated timing together at up to four times their native rate.
It uses the existing primary clock and clock enables, with one disk engine
and no new clock domain. Video, audio, RTC, paddles and SmartPort retain
their existing timebases. Normal retains the selective slowdown described
for FPGA `4D000606` below.

Fast requires enabled acceleration at a configured speed of at least
14 MHz. Stock and lower speeds retain normal floppy timing. SmartPort,
speaker/paddle/counter holds and IRQ compatibility holds also retain their
normal schedules. The menu's supported accelerated configuration is ZipGS.

Within an eligible floppy timing window, an ordinary CPU cycle takes at
least five virtual ticks; a guest slow-speed request takes fourteen.
IWM register transactions take at least fourteen ticks and commit once.
Their address, direction and write byte are captured, and a response
register separates the controller read from CPU completion. The request,
response and acknowledgment fit within that virtual cycle.
Outside that window the CPU can use its full configured speed. Cache
waits, a stopped guest and track service pause virtual disk time while the
file-service backend continues. An already emitted final write bit drains
before a save or eject can complete. Other native I/O charges its virtual
cycle once and pauses the disk during the remaining physical bridge wait.
These rules bound acceleration; they do not promise a fourfold application
speed increase.

All 38 protocol cases pass across both drive families: full decoded
sector reads/writes, pending-bit pause and resume, eligibility changes,
failed-block retry, durable flush and eject. The Normal results, complete
read/written/saved tracks and 5.25-inch store trace match the frozen606
reference exactly. Two pre-correction controls reproduce lost pending bits
with the final test oracle. ROM1 and ROM3 pass bounded boot in both modes.
Another 113 cases cover captured IWM requests and responses across pause,
cache waits, backend work, DMA, eligibility changes and SmartPort transitions.
Two actual HDD DMA runs cross the IWM address window, preserving all 512
data bytes and the IWM state in Normal and Fast modes. All 157 cases pass
against the current source after the timing cleanup.

The synthetic formatted read/write transfer is 3.922 times faster for the
3.5-inch track and 3.952 times faster for the 5.25-inch track. The selected,
spinning idle CPU kernel differs by at most two dispatches in 100,000 primary
clocks between the modes. Native,
video and audio cadence remain unchanged. These are simulation measurements,
not application or board benchmarks.

FPGA `4D000607` passes the normal timing, coverage, routing, CDC and phase
gates. A fresh process reopens the saved checkpoint without optimization or
constraint replay and independently passes the same gates: setup slack is
`+0.004 ns`, hold slack is `+0.012 ns` and pulse-width slack is `+1.250 ns`.
The audit checks all 222 frozen source inputs, the resolved constraint scopes
and zero added clock uncertainty. These are tool results; physical-board
acceptance remains pending.

## FPGA 4D000606 selective floppy timing

This change replaces the external CPU's whole-motor slowdown with timing
requests from the selected mounted drive and the active write engine. The
CPU can resume its configured accelerated rate while the spindle continues
rotating, including when the drive remains selected.

Native timing remains active in these intervals:

- The guest clears `$C036` bit 7 while selecting a mounted floppy. ROM1 and
  ROM3 use this request for their long Sony spin-up and seek delays.
- A selected, mounted 5.25-inch drive has its IWM motor command on and the
  guest requests motor-based slowdown with `$C036` bit 2. Physical motor
  coast after the off command does not extend this request.
- The IWM has an active disk-write protocol with media in its routed drive.
  This also covers Sony write cleanup after `$C031` deselects the 3.5-inch bus.

The existing retriggerable two-millisecond guard after IWM register accesses
still covers nearby command and byte-transfer work for a selected mounted
drive. Outside these intervals, ordinary computation can accelerate. Other
accelerator gates, the original CPU path, mechanical delays, flux clocks,
track buffers and media flushing retain their existing behavior. No ROM
addresses or program-counter ranges select this policy.

The IWM exports its existing motor command and a write-active observation.
Write-active is registered on the existing 14 MHz clock and cleared by the
existing reset. The access guard covers its one-edge entry latency. This
breaks feedback from immediate IWM bus decode into CPU speed selection. It
adds no clock domain, asynchronous crossing or timing exception. The new
register uses the existing timed relationship between native and primary
clocks. The port script reproduces both HDL changes from the pinned upstream.

The small production-adapter regression passes 16 groups and 2,048
family/drive/media/motor/guest-request/command combinations, plus directed
active-write and reset checks. A real 65816 program
performs the same 26,879 CPU advances with identical register and memory
results. Both deselected idle and selected, spinning, guest-fast operation
take 26,879 primary ticks. A guest-native request remains effective after
the two-millisecond guard expires and takes 1,508,528 ticks for that work.
These warm-loop measurements do not predict application or desktop speed.
Five negative controls fail as intended: restoring the previous motor hold
loses acceleration, and removing each native-request, motor-command,
write-active or routed-write guard breaks its corresponding timing check.

CPU-executed write tests cover Sony asynchronous, Disk II synchronous and
Disk II asynchronous modes. Each executes 1,025 real guest data writes and a
7.2 ms CPU-only gap while write mode remains active. All critical I/O timing,
serial bits, track positions, CPU results and 512 final synthetic track bytes
match the previous policy exactly, including after the access guard expires.
After write mode ends and the guard expires, the drive remains selected and
spinning: 300,000 primary ticks permit 120,000 instruction dispatches versus
5,443 under the previous motor hold. This is a bounded synthetic write and
warm-RAM test with simulated mechanics, not a filesystem or hardware test.

ROM1 and ROM3 boot the supplied read-only Arkanoid 800K image through the
native IWM/WOZ path at speed code 6. ROM3 executes the exact bootblock opcode
at tick 143,151,383 and completes 1,219 reads over 180 million primary ticks.
ROM1 reaches that opcode at 152,164,718 and completes 755 reads in the same
window. Neither enters a repeated BRK loop. Their long native-speed waits
remain intact. This establishes bounded boot progress; it does not establish
full game or copy-protected disk compatibility.

The extended ROM3 run passes 650 million primary ticks, reaches the native
IRQ handler at tick 345,032,636, completes all 4,109 reads and makes 113 track
changes without a repeated BRK loop. The same 153,228 native-delay dispatches
and 28,286,096 native-speed ticks after access-guard expiry remain protected.
The matched ROM1 control changes only the CPU timing policy: both boot,
preserve native-duration waits, and execute the same 20,448 eligible dispatches
take 1,890,568 ticks with the previous policy versus 105,004 with this change.
These are bounded simulation results, not hardware performance measurements.

The new IIgs FPGA identity is `4D000606`, ABI 0.22. ARM M0.6.4 (`4D000604`)
and its media-safety capability are retained. No ARM source or firmware
binary changes are required.

The selected FPGA passes the original strict export gates with nominal setup
slack `+0.002 ns`, hold slack `+0.052 ns` and pulse-width slack `+1.250 ns`.
The primary CPU clock group has `+0.084 ns` setup slack. Clock identity,
native phase, timing coverage, CDC, bus-skew, routing and design-rule gates
pass. The native clock census covers 9,059 pins; native CE setup and hold
slacks are `+13.795 ns` and `+1.218 ns`.

Timing closure uses physical replication of the shared PH2 enable and two
CPU decode branches, rerouting and recorded incremental optimization. The
original failed build and every later attempt remain preserved. One attempted
post-route forced replication was rejected without changing the checkpoint;
the selected result uses the supported unroute/replicate/reroute sequence.
Temporary optimization uncertainty is restored to zero before nominal
measurements and export. No RTL, clock rate, timing exception or constraint
policy changes were made during closure. All 217 captured inputs still match
the freeze.
Evidence is under `build/iigs606-selective/hardware/apple-iigs/` and
`build/iigs-floppy-critical/`. These are offline results; board acceptance of
FPGA `4D000606` remains open.

An independent process directly reopens the selected routed checkpoint and
repeats the original timing, phase, coverage, CDC, skew, routing and DRC gates.
The accepted `fresh-dcp-audit-final-2` passes with the same `+0.002 ns` setup,
`+0.052 ns` hold and `+1.250 ns` pulse-width slack. All 131 serialized clock
and exception directives have equivalent numeric values and object scopes.
The comparison resolves 418 selectors, including 312 scoped selectors and
336 hierarchy commands, with zero empty selectors. All 42 timed clock pairs
have zero user uncertainty. The first supplemental audit is explicitly
rejected because it did not honor hierarchy context; its raw PASS is retained
as historical evidence and is not accepted for delivery. The corrected
comparator passes ten scoped-query and failure controls.

The test delivery is `build/iigs-floppy-critical/core606-update.zip`, with
exact source, physical repair history, build reports and validation tools.
`core606-sd.zip` is the small two-file update for a board already running ARM
M0.6.4. Its IIgs binary and `core.cfg` are byte-identical to the full delivery.
Keep the existing ROMs, media, settings and recovery journals. After copying
the two files, verify FPGA `4D000606` and ARM `4D000604`, then test Thexder
idle responsiveness, eject/reinsert and Restart active core at 57 MHz with
the menu closed. No board installation or physical test of `4D000606` was
performed as part of this offline build.

## Mounted 3.5-inch idle slowdown

The owner reports that inserting a 3.5-inch floppy slows the whole machine
until the image is ejected, including after disk activity finishes. At
diagnosis the board reported IIgs FPGA `4D000603`, ROM3 and speed code 6
(57 MHz).

The Sony drive's physical spindle state intentionally survives deselection
through `$C031` bit 6. The accelerator previously treated that entire spindle
lifetime as a native-speed compatibility hold whenever a 3.5-inch image was
mounted. Ejecting removed the media qualification and released the hold.

The FPGA `4D000604` correction qualified that 3.5-inch motor hold with the active
3.5-inch selection. A selected, spinning drive retains native timing for the
driver's spin-up and seek loops; deselection permits the configured CPU speed
again. Physical rotation, flux timing, track buffers, dirty-track flushing,
the separate IWM access timer and the stock CPU path are unchanged. The port
script reproduces the correction without modifying the pinned upstream.

The production-adapter regression executes the same 26,879 real 65816
advances and checks identical register and DDR results with that disk still
mounted and spinning. Selected activity takes 592,636 primary ticks;
deselected idle takes 26,879, restoring the maximum warm-loop cadence. All
16 acceleration groups and 128 media/motor/selection combinations pass.
IWM byte timing and live speed/card-setting regressions also pass.

The exact supplied Arkanoid 800K image also passes the ROM3 native-floppy boot
gate at maximum speed through 650 million primary ticks. It executes its boot
byte at tick 143,227,031, reaches the native IRQ handler at 353,941,668 and
completes 4,109 WOZ reads without a repeated BRK loop. This is bounded boot
and ongoing media progress, not full game compatibility. Its exact inputs and
results are in `build/iigs-floppy-idle-repro/arkanoid-corrected/validation.json`.

An equal-time HDD-booted insertion comparison uses the same ROM3, System 6.0.1
HDD and Arkanoid floppy. Before insertion its traces match. During the next
150 million ticks, the correction completes 3,299 media requests and
22,921,202 CPU advances, versus 1,667 and 16,513,297 with the old spindle-wide
hold. Both perform active disk work. This is a mixed-workload comparison,
not a settled Finder-idle or hardware benchmark. See
`build/iigs-floppy-idle-repro/comparison-validation.json`.

The selected FPGA `4D000604` passes strict signoff with nominal setup slack
`+0.100 ns`, hold slack `+0.028 ns` and pulse-width slack `+1.250 ns`.
Physical replication of one high-fanout CPU decode net and rerouting closed
setup timing. The temporary optimization uncertainty was restored to zero
before checkpoint and bitstream export. RTL and release timing constraints
were unchanged by that physical repair. All 216 captured inputs and their
source archives match; the PS7 initialization hash matches M0.6.3 exactly.
Evidence is under `build/iigs-floppy-idle/hardware-native/apple-iigs/`.

The standalone package is `build/iigs-floppy-idle/core-update-m064.zip`.
It contains the two SD update files, exact source, repair provenance and
validation evidence; it reuses ARM M0.6.3 without rebuilding it. The previous
SD files were backed up, both replacements passed SHA256 readback, and host
flush, exclusive lock, dismount and eject succeeded. UART then reported
PCAP CRC32 `3451E6DB`, FPGA `4D000604`, ABI 0.22, `ready=1` and `fault=0`,
with the owner's Thexder 2MG still mounted in drive 2. This establishes
delivery and initial live core health. Subsequent eject and restart attempts
failed with "Apple IIgs media did not finish flushing". A media retry restored
one eject, and a warm CPU reset restored one restart, but the failure returned.
The owner powered the system off before the combined correction below was
installed. The idle-performance check on FPGA `4D000604` was not completed.
Receipts are in `build/iigs-floppy-idle/live-board-604.json` and
`build/iigs-floppy-idle/flush-restart-live.json`.

## M0.6.4 floppy flush and restart correction

The firmware previously required every drive to stop requesting data before
sending an eject. A production-service reproduction completes 100,000 healthy
HDD requests while the unrelated floppy never receives its eject command.
The new drain waits for the requested drive and continues serving the others.
The file remains open until that drive's requests, the FPGA eject-pending
state and its recovery journal have all drained.

The shared FPGA media bridge also cleared a retained request snapshot on
mount or eject, even for a different drive. The IIgs adapter still awaited
its completion, so firmware could lose the operation needed to finish a
flush. The IIgs bridge now retains the snapshot until completion. A fresh
request ID or media generation republishes a retained-buffer retry even
when VALID remains high. Other machine cores keep their existing policy.
The change adds no clock or crossing; the identity latches use the existing
core clock and mailbox path.

PRAM saves now require an acknowledged operation for slot 4 with the expected
generation and direction. Firmware fences an earlier save before issuing
another configuration pulse, including when the final payload byte committed
but the transport's end operation failed. An uncertain pulse remains an
error until service initialization; it cannot manufacture a successful flush.
Dirty media and recovery journals remain retained on failure.

The paired identities are ARM firmware M0.6.4 (`4D000604`) and IIgs FPGA
`4D000605`, ABI 0.22. The new firmware checks optional capability bit 8 before
starting IIgs media service. An older IIgs FPGA reports an update error before
media changes. Both files and the matching `core.cfg` must be installed.
The 2MG parser is unchanged; the owner withdrew the earlier size-error report.

The production bridge regression passes five IIgs groups, 36 snapshots and
70 commands, including mount/eject at 12 mailbox phases and retry identity
changes. It also checks all six other machine policies. Five negative
controls restore the old failures. Production firmware tests cover unrelated
HDD traffic, retained dirty data, failed saves, acknowledged PRAM completion,
transport failures and the capability guard; six negative controls fail as
expected. A separate production-adapter test finishes real protected WOZ
track reads with the spindle running and the 3.5-inch bus deselected. The
synthetic CPU program has reached STP; host eject passes with the pause
input off or on. Native eject passes with the pause input on by pulsing the
native core's eject output. This tests output-boundary capture, not guest
IWM command decoding. Deselection is the reset value of `$C031`, not a guest
write. Each case retains its request until completion, drains both
sides without a write or reset during the drain, and clears mounted, busy,
dirty, pending, ACK and disk-ready state. All 87 host checks pass. These
results do not establish physical acceptance of the combined update.

The selected FPGA `4D000605` passes strict nominal signoff with setup slack
`+0.008 ns`, hold slack `+0.046 ns` and pulse-width slack `+1.250 ns`.
All 56,661 nets are routed with zero routing errors. Phase proofs, timing
coverage, bus skew, CDC and design-rule gates pass; critical CDC rows and
waivers are both zero. The normal implementation flow closes timing without
an additional repair pass or constraint replay. All 217 captured inputs match
the current source. The PS7 initialization hash still matches M0.6.3 exactly.
Evidence is in `build/iigs605-isolated/hardware/apple-iigs/`.

The paired package is `build/iigs-media-fix/combined-update-m064.zip`.
Its SD payload contains the new ARM firmware, IIgs native FPGA binary and
matching `core.cfg`. CPU0 and CPU1 are freshly built; the FSBL, supervisor
FPGA and exported platform are explicitly retained from the pinned M0.6.3
package. The source archives include the exact FPGA capture, complete current
ARM and FPGA source, retained boot-partition provenance and local validation
tools. `INSTALL.txt` covers the three-file copy, Golden firmware update,
identity checks and the original Thexder idle/eject/change/restart tests.
On 2026-10-19 the owner confirmed the combined update working on the board.
This records owner-reported acceptance; the installation and live device
identity were not independently reread by the agent after that confirmation.
The package remains the exact sealed build delivered before the board test.

## Earlier acceleration measurements

The owner reported that 57 MHz felt barely twice as fast as stock in real use:
booting, Finder window drawing and applications. This record covers the
measurements that explained it, the changes on branch `iigs-accel-fixes`, and
the simulation evidence. The owner now confirms System 6.0.1 boots on the
M0.5.4 core, with measured boot times of 59 seconds at Stock, 37 seconds at
14 MHz and 35 seconds at 57 MHz. Live ARM measurements now identify about
32 seconds of USB polling inside the HDD transfer path. M0.5.6 removes that
penalty without rebuilding the FPGA. Synthetic CPU throughput still does not
establish boot or UI throughput.

## M0.5.7 shared USB poll timing

M0.5.6's transfer callback kept its own poll timestamp. The normal runtime loop
already serviced USB before each HDD request, but did not update that timestamp.
A callback could therefore repeat a full USB poll immediately after the main
loop had done one.

M0.5.7 records the completion time of every ordinary or deferred full USB poll
inside the USB service. The IIgs callback requests a deferred poll only when
one millisecond has passed since that shared timestamp. The check uses raw
timer ticks, avoiding microsecond conversion for every FIFO pause. Ordinary
runtime polling remains unthrottled; long transfers still collect input, and
the diagnostic baseline continues to poll on every callback. The interval is
measured from poll completion so a long poll does not trigger another poll
immediately when it returns.

All 88 host tests pass, including strict main/USB compilation. The shared-timer
regression runs 20 short requests across five milliseconds without an extra
transfer poll, then permits the fallback at the one-millisecond deadline.
It also covers poll completion timing, timer wrap, host restart and deferred
mouse delivery. The native ARM build passes. Build and test receipts are in
`build/iigs-usb-shared-poll-m057/validation.json` and the packaged evidence.

This is an ARM-only change. The package is
`build/firmware-core-gs-m057-named.zip`, with all eight FPGA images and the
CPU1 video worker retained. It delivers `FIRMWARE-MULTITINI.BIN` at both the
package and SD roots; the existing Golden updater accepts that name.

The first reported M0.5.7 copyright-screen freeze used an older FPGA file on
the actual SD card. UART reported build `4D000400` and PCAP CRC32 `5E618146`;
the card readback had SHA256
`0490cc1ed27d5930e6266c1688554c9dea074c6a000c8f8461a02bc7fc959f62`.
Both paced and original-polling resets made zero HDD requests. Restoring the
exact previously accepted M0.5.6 ARM image also left that old core stuck.

The selected package instead contains build `4D000505`, CRC32 `D3BAF34E`,
SHA256 `96c165622a06415cf945b8bca6dae929f569c4c77bb6bf6247d0290fb44f6736`.
The old card image and configuration were backed up; both were replaced and
read back against the package. M0.5.7 was then installed using the named
update file, and its running firmware banner was observed. The next core
load reported PCAP size `4045568` and CRC32 `D3BAF34E`. The owner confirmed
"Restarted; Finder boots". The redundant-poll removal remains enabled.
No new speed measurement was taken; the M0.5.6 timings below remain
specific to their earlier captures. Compare live PCAP size/CRC and runtime
identity with the delivery; a package hash alone does not identify a running
board image.

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
`M0.5.6 build=4D000506`. The package retains all eight FPGA images and the
CPU1 video worker byte for byte. The hardware captures report IIgs build
`4D000505`; their aggregate's FPGA SHA256 was declared from the package,
not measured by reading back the then-installed SD file.

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

In M0.5.6 through M0.5.8, the opt-in UART commands were `:gsprofile boot 0`, `:gsprofile boot 4` and
`:gsprofile boot 6` for Stock, 14 and 57 MHz. They safely reset/remount the
selected IIgs and start timing at reset release. Append ` baseline` to restore
the original progress behavior for that capture. `:gsprofile stop` freezes
and prints the report, then restores paced behavior. Select ZipGS first.
The diagnostic emits no UART output during the measurement interval.

M0.5.9 removes this completed diagnostic: the stage timers, statistics,
per-transfer callback wrapper, main-loop profiling checks and slow-baseline
switch are deleted. The shared USB poll timestamp and one-millisecond
fallback remain functional input-service code. Enabled profiling added timer
reads and statistics updates; disabled profiling still left checks and the
callback wrapper. Their overhead was not measured independently. The timing
table above remains the original instrumented hardware result, not a new
measurement of M0.5.9.

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
