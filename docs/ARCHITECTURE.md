# Architecture Baseline

Current status: the C64 and GBC functional baselines are accepted. The owner
confirmed GBC operation and tested saves on 2026-09-14. See
[Current release](CURRENT_RELEASE.md) and [GBC baseline](GBC_BASELINE.md) for
the accepted artifacts and scope. The following records the earlier bring-up
milestones; its pending tests describe those stages.

The independent PAL DVI, fixed-tone audio, UART, and golden-reboot
diagnostics have passed on hardware. Live PAL video, USB input, wired Xbox
input, SD ROM loading, 1541 startup, and read-only D64 game loading also have
recorded board results. Build `2026082F` connects live machine PCM to I2S and
SPDIF. Its first board test found mostly clean output plus repeatable stuck-tone
faults. Candidate `20260830` changed the C64 default SID model from 8580 to
6581, but the 2026-09-02 report says the scratching remains. Candidate
`20260902` corrects the reproduced dual-SID output schedule. The exact build
booted on the connected board, and the 2026-09-03 user retest reports that the
scratching/audio bug is fixed.

Current source also extends the C64 storage path with exFAT, RTC-backed FAT
timestamps, writable data-only D64 images, journal recovery, safe media
barriers, and read-only G64 support. Standalone PAL and NTSC `20260903` images
containing that batch completed the vendor flow and were reported by the user
to look okay on the board. This is a visual smoke result, not complete
writable-media, exFAT, RTC, or content qualification.

Build `2026090E` adds the ABI 0.6 text overlay, SD core catalog, on-screen menu,
and full-PL PCAP supervisor. Fresh PAL, NTSC, and PSRAM-enabled GBC images pass
the strict vendor gates. The connected board reports the expected build/ABI,
switches from C64 to GBC through PCAP, and runs Tomb Raider and V-Rally into
gameplay. Rollback and the broader bidirectional-switch/media matrix remain
pending.

ABI 0.7 build `2026090F` extends the common media ABI to GBC battery-backed
external RAM. The FPGA pauses the machine for a coherent PSRAM snapshot after a quiet
interval or an eject request. The ARM writes the complete `<cartridge>.sav`
through the same synced, checksummed journal mechanism used for writable D64
media. Reset, detach, switching, and golden reboot are fail-closed on commit
failure. Portable tests pass. Fresh PAL, NTSC, and PSRAM-enabled GBC Vivado
2025.2 builds pass timing, CDC, DRC, route, bitstream, and XSA gates; a matching
exFAT-enabled Vitis supervisor image also builds and packages successfully.
The staged `2026090F` image was installed, and the user confirmed GBC game-save
persistence on the board on 2026-09-04.

Current source adds a versioned, checksummed, journaled supervisor settings
file and a supervisor-only boot FPGA target. Core and timing choices are
remembered independently of an explicit Autoboot target, which defaults off
and changes only when the user selects `Autoboot current core`. With Autoboot
off, no machine is loaded and the common shell shows the Multitini standby
screen. Per-core media and C64 input choices are restored only when that core
is active. Fresh supervisor, PAL C64, NTSC C64, and PSRAM-enabled GBC vendor
builds and matching packages pass; the board test remains.

ABI 0.8 build `20260912` adds a four-byte supervisor-to-shell direct-load
register, two-entry 8 KiB track caches for both D64 and G64, and a generic
per-core configuration page. Its attempted drive-clock warp option failed the
first IEC board test and has been removed. Current source instead gives C64 an
optional paired fast-serial-ROM profile plus an independent FPGA64 Smart 2x CPU
option. CPU turbo defaults off; it does not change 1541 timing. The replacement
source passes fresh supervisor, PAL, NTSC, PSRAM-enabled GBC, Vitis, and package
build gates; it has not yet been tested on the board.

## Ownership boundaries

The ARM control program owns files, USB protocol work, settings, menus, update
policy, and bounded replies to FPGA service requests.

The common FPGA shell owns board pins, safe idle states, clocks and resets,
control registers, video and audio output stages, input-event delivery, memory
services, and counters.

Each machine adapter translates the common shell interface to an imported core.
The imported core stays close to its pinned upstream source. Board-specific
changes belong in the shell or adapter.

## Machine images and runtime selection

`MULTITINI_MACHINE=SUPERVISOR` is the project default. It imports only the
common HDL and contains no machine adapter or core. Explicit
`MULTITINI_MACHINE=C64` and `MULTITINI_MACHINE=GBC` projects select one complete
machine adapter for an SD-loadable package. Only the common and selected-machine
sections of the HDL manifest enter a project, so inactive cores are not build
dependencies.

Every image is one full Zynq configuration containing:

- the PS and its DDR, QSPI, SD, UART, I2C, and two ULPI controllers;
- a small AXI control register bank;
- the common double-buffered text overlay (candidate ABI 0.11 adds 128x60
  dense text and menu pause while retaining the legacy 80x36 mode);
- an independent DVI test pattern and, for PAL C64, optional live machine
  frames converted to the fixed C64 palette;
- a shared I2S/SPDIF stage that selects live machine PCM or the fixed test tone;
- safe idle values on Apple, Ethernet, and PSRAM pins; and
- for an SD core package only, one retained machine adapter/core integration.
  The supervisor boot image omits this layer entirely. The selected machine's
  video and PCM feed the common output stages after dynamic load.

The ARM supervisor can replace the supervisor shell or active machine image
from a validated raw bitstream package while the ARM application remains alive
in DDR. It first
flushes media and quiesces the active core, isolates the PL, loads through
DevCfg/PCAP, probes the new shell/core/ABI, then activates the new service. A
failure after quiescing attempts to reload the last known active package.
Every selectable image must therefore retain the common shell and ABI. This is
runtime full-PL reconfiguration; partial reconfiguration remains deliberately
outside the baseline. See [MULTICORE_RUNTIME.md](MULTICORE_RUNTIME.md).

## Clock and reset domains

The source board uses a fixed 33.333333 MHz oscillator on `PS_CLK`. The PS
generates the fabric clocks. No clock or reset comes from the Apple connector.

Planned initial domains:

| Domain | Planned rate | Source | Use |
| --- | ---: | --- | --- |
| Control | approximately 133.333 MHz | direct PS FCLK0 and PS fabric reset | AXI registers and shell control; its clock and reset remain independent of PL MMCM lock across full-PL PCAP |
| PSRAM reference | 199.995 MHz | dedicated Clocking Wizard and lock-qualified reset | IDELAYCTRL reference |
| Video | 148.5 MHz | PS FCLK2 through Clocking Wizard | 1080p diagnostic output |
| C64 machine | requested 31.527954 MHz PAL or 32.727266 MHz NTSC | PS FCLK2 through a separate Clocking Wizard | FPGA64 machine, ROM/input loaders, raw video/audio |
| GBC machine | requested 33.554432 MHz | PS FCLK2 through the build-selected machine Clocking Wizard | GB/GBC machine, cart/bootstrap loaders, raw video/audio |
| IIgs machine | 57.272728 MHz | PS FCLK2 through the build-selected machine Clocking Wizard | 65816 and its 14.318 MHz enable grid, slow RAM, cache and, in the IIgs image only, the HP2 AXI port for CPU RAM |
| Audio MCLK | 12.288 MHz | board oscillator | DAC clock and I2S derivation |
| Audio BCLK | 3.072 MHz | MCLK divided by four | 48 kHz, 64-bit I2S frames |

Every asynchronous crossing must use a named synchronizer or handshake. Every
generated clock and exception must bind to an existing object in the final
Vivado design. In the verified default PAL build, the fatal CDC gate passed and
the `2026082F` live-audio report contains only 24 `CDC-3` Info crossings and one
`CDC-9` Info reset crossing. The machine-audio payloads use coherent XPM
handshakes, and both readiness crossings launch from a machine-domain register.
The standalone `20260903` PAL, NTSC, and default-off-PSRAM GBC builds each
passed their own strict timing, CDC, DRC, and route gates. Any ABI 0.6 rebuild
must repeat those reviews because the overlay changes the implemented design.

Sparse ROM bytes and input event pairs move from the 133.333 MHz control domain
to the C64 domain through separate one-entry `xpm_cdc_handshake` mailboxes. A
source holds every payload bit until the destination accepts it. Reset request,
ready, and error levels
use named bit synchronizers; the sparse error-clear event uses a tested
three-stage toggle synchronizer. The behavioral CDC tests deliberately use
unrelated clocks and backpressure. The default PAL XPM and synchronizer paths
passed the fatal CDC report gate; every other machine build must repeat that
review.

The C64 adapter holds machine reset until BASIC, KERNAL, and character ROMs have
each completed a strictly framed transfer. The ARM command ABI supports begin,
data, end, and abort. Firmware retries bounded mailbox backpressure and leaves
the core in reset if a ROM set cannot be loaded.

The next source-generated image configures UART0 at the golden-compatible
921600 baud while retaining UART1 at 115200. The PS UART configuration is
initialized by the generated FSBL, so applying this source change requires a
fresh Vivado/XSA and Vitis platform/FSBL build. The already packaged `20260902`
image remains at its historical 115200 baud.

The current diagnostic firmware exposes `:c64rx` on UART0. The host uploader
combines the repository's selected BASIC, KERNAL, and character ROMs into an
exact 20 KiB XMODEM-1K/CRC transfer by default; explicit path overrides remain
available. Firmware verifies the bundle CRC32 and each ROM's size and
nonuniform content before using the portable stream helper. Failure leaves the
C64 in reset.

Build `2026082E` introduced one ARM-owned FatFs instance and keeps `ff.h`
private to the Xilinx storage layer. C64 startup reads the three required
system ROMs from `0:/multitini/cores/c64/roms`, then independently tries the optional
16 KiB 1541 ROM. A missing drive ROM disables drive service without undoing a
successful C64 boot. The UART three-ROM path remains independent and
unchanged. That build passes its first board test: the C64 and 1541 ROMs loaded
from SD, the default drive-8 D64 mounted read-only, and Decathlon loaded and
ran.

Current source configures Xilinx `xilffs` 5.5 for long file names and exFAT.
The Vitis generator synchronizes the generated and exported config copies,
backports the four FatFs R0.16 patch-2 media-validation guards into Vitis
2025.2's R0.15a source, and stamps the resulting platform. A reused XPFM must
carry that exact semantic stamp. The generated `get_fattime()` contains a weak
hook; the application overrides it with the last valid PCF8563 reading cached
by the main loop. An unavailable, invalid, or voltage-low RTC leaves the last
good value in place, with `2010-01-01` used until the first good sample.

The media ABI now has a PS-to-core load stream, a core-to-PS reverse data
stream, explicit mounted/dirty/eject-pending state, and request completions.
Mount generations still reject stale requests. Normal data-only D64 images are
writable. Firmware range-checks the reverse block stream, requires its final
marker at the exact expected byte, updates the PS DDR copy, and commits the
complete image through a synced checksummed journal before acknowledging the
write. Mount-time recovery replays only a structurally valid complete journal.
D64 images carrying an error-info tail are deliberately read-only.

G64 version-zero images use the same drive-8 boundary but remain read-only.
The ARM validates track extents and accepts fixed speed zones 0 through 3,
then supplies normalized half-tracks to the direct-GCR reader. Variable
per-byte speed maps are rejected instead of being approximated.

ABI 0.8 reduces ARM MMIO traffic by accepting four direct-load bytes per AXI
write and draining them in order through the existing byte-stream CDC
mailbox. Each C64 disk adapter also retains the two most recently completed
normalized 8 KiB tracks in block RAM. Revisiting either track is serviced
locally; a D64 write invalidates the modified cached track. This cache is
format-neutral and does not alter 1541 timing.

The C64 `Fast serial ROMs` setting selects an alternate, user-supplied ROM pair:
an exact 8 KiB KERNAL at `roms/fast/kernal.bin` and an exact 16 KiB drive ROM at
`roms/fast/1541.bin`. BASIC and character ROMs continue to use their unchanged
stock paths. Firmware reads and validates the complete pair before asserting
reset, streams both while the core is held in reset, and saves the setting only
after the new pair starts successfully. A failed runtime switch restores the
previous active pair. If a persisted fast selection is unavailable during core
activation, firmware reports the fallback and loads the stock profile. The
repository builds a fast pair from the stock ROMs in `c64fast/bin`
([C64_FAST_DISK.md](C64_FAST_DISK.md)); it is not part of the firmware
package and must be copied to the card like any other ROM.

With ABI 0.10 shells, enabling the fast pair also connects a DolphinDOS-style
parallel cable between the C64 user port and the emulated drive and maps 8 KiB
of drive RAM at `$8000-$9FFF`, exactly like the upstream option. Firmware sets
control bit 6 beside the ROM streams and clears it for the stock pair, because
the stock drive ROM drives the same VIA port low. The drive strobe is also
readable as a level on user-port PA2, and CIA2 port A input is data-direction
aware. Cable strobes pause CPU turbo the way IEC traffic does. This path
passes focused XSim only; it has not been built by the vendor flow or tested
on the board.

FPGA64 Smart 2x independently adds 6510 RAM/ROM cycles without changing VIC,
CIA, I/O, or drive timing. The track caches and packed ARM transfer reduce host
latency without changing the drive clock. No whole-drive warp control remains.

The supervisor also exposes an Ultimate-style D64 `Mount & Run` operation.
The ARM parses the directory and selected PRG sector chain from the mounted
in-memory D64, using stable directory-slot identity and two-pass validation so
a corrupt or stale entry cannot cause a partial direct load. It then uses the
same flush/eject reset barrier and PRG stream loader as loose content, followed
by remounting the parent D64 and issuing a separate RUN token. ABI 0.9 makes
that stage/remount/run order explicit so immediate disk access cannot race the
remount. Only validated BASIC-start (`$0801`) PRGs are offered. This path
accelerates only initial PRG placement;
normal IEC and copy-protection behavior remain in the unchanged drive path.
The user-supplied `Fast serial ROMs` profile is an independent option.

Eject is a state barrier rather than a blind unmount. The FPGA retains dirty
or eject-pending state until the matching write completion succeeds, and the
ARM waits for a stable clean/unmounted state. Core reset, PRG/CRT content
changes, and golden reboot first pass through that barrier; failure refuses or
cancels the destructive transition and leaves `:diskretry` available. The
exact layout and `:mount`, `:eject`, `:dir`, `:disk`, and `:diskretry` behavior
are documented in [C64_SD_STORAGE.md](C64_SD_STORAGE.md). This extended path is
source-only and is not part of the board-qualified `2026082E` artifact.

The PAL video adapter registers coherent RGB, data-enable, and sync values in
the machine domain. The live bridge accepts only exact 382x270 frames whose
pixels match the core's fixed 16-color palette. It stores four-bit palette
indices in four independent dual-clock frame banks, then converts each index
to RGB565 while scaling 4x into 1080p with 196-pixel black side borders.

The source domain publishes completed banks through a three-bit Gray-coded
ring pointer. The 1080p pixel domain acquires at most one bank at each output
vertical blank and returns the previously displayed bank through a second Gray
pointer. The read and write ports therefore never intentionally own the same
bank. Invalid geometry or palette data rejects the entire source frame. If the
consumer stops long enough to fill the ring, capture drops whole source frames
before their first pixel rather than overwriting queued or displayed data.

PAL produces approximately 50.123 frames per second while DVI remains fixed at
60 frames per second. When no newly completed PAL frame is available, the DVI
side repeats the current bank. All bank and diagnostic-source changes occur at
output vertical blank, so rate conversion produces clean repeats without a
mid-scan swap. A grant/acknowledge rearm barrier flushes pointer ownership after
an independent source reset, pixel reset, or loss of C64 readiness and keeps
machine video fail-closed until a new complete frame arrives. Focused XSim
covers live swaps, repeats, queue-full frame drops, palette/geometry rejection,
diagnostic switching, and independent resets. The preserved PAL live-video
image also passes its recorded board procedure.

The selected adapter emits signed 24-bit stereo samples at a nominal 48 kHz.
Build `2026082F` packs validity, left, and right into one 49-bit source-domain
value. Two coherent XPM handshakes carry that value to the independent audio
BCLK and control domains. Each destination holds the newest complete pair, so
small source/oscillator drift produces bounded zero-order hold instead of FIFO
overrun or underrun. Loss of machine readiness invalidates the held sample and
forces silence through core reset or ROM loading while the physical clocks keep
running.

The board's 12.288 MHz oscillator supplies audio MCLK. Dividing it by four
produces the 3.072 MHz BCLK and exact 48 kHz, 64-bit I2S frame rate. Live PCM
and the diagnostic serializer run continuously from the same BCLK and reset;
source selection changes only at a complete stereo-frame boundary. The local
SPDIF transmitter uses the 133.333 MHz control-clock phase increment and
captures the right sample with the left so an asynchronous update cannot mix
two stereo pairs. `AUDIO_MUTE` dominates both sources.

Firmware initializes the AK4493 muted. It selects live PCM in the PL, waits at
least two complete PCM frames, and then releases DAC soft mute. UART `a`
selects live audio, `t` selects the fixed diagnostic tone, and `m` mutes the
current source. The exact AK4493 datasheet was reviewed locally, and the routed
PAL baseline applies a conservative +/-5 ns receiver window to I2S LRCK and
SDATA. The `2026082F` source, focused XSim, routed timing, CDC, DRC, and Bootgen
checks pass. Its board test showed that `t` cleanly masks live SID faults and
`a` restores them, separating the common output stage from the machine source.

The C64 adapter exposes the imported core's two-bit SID-version choice and
defaults both instances to 6581 in candidate `20260830`. Build `2026082F` had
hardwired 8580; that model's roughly 9.77-second noise timeout closely matches
the timed Decathlon artifact, but the reported retest still scratches and
rejects the model change as a sufficient fix.

Candidate `20260902` instead corrects a deterministic schedule error at the
shared dual-SID filter boundary. Filter state 7 launches the final audio MAC.
The left slot has state 8 available to capture that result, while the right
slot reaches full state 15 on its launch edge and then remains there until the
next 1 MHz enable. The old logic published on the first state-15 edge, before
the right result became visible, so one filter intermediate escaped for one
machine clock per SID period. Since the adapter's 48 kHz sampler is not phase
locked to that pipeline, it could hold those narrow corrupt values for entire
PCM frames. The corrected port waits for a repeated state-15 edge and publishes
the left and right results together. Focused XSim reproduced four mono-channel
mismatches in 128 clocks before the fix and none afterward.

The golden-UART-installed `20260902` image subsequently booted with C64 core
status `0000014D` and no shell error, and the user reported that the prior
scratching/audio bug was fixed on the connected board. That result validates
the scheduling repair against the reported fault. It does not by itself cover
long-run stability, exact game sequences, channels, level, reset behavior, or
SPDIF.

The GBC adapter holds reset until a cartridge is strictly framed and accepted
by the external-memory service. Its open CGB bootstrap is generated from a
pinned SameBoy-derived source; an optional 2304-byte override uses the same
direct-load stream. One gamepad namespace maps directly to the eight upstream
button bits.

Exact 160x144 GBC frames enter four RGB565 dual-clock banks. Gray-coded
producer and consumer pointers keep the machine and 1080p domains from owning
the same bank, and the display changes banks only at output vertical blank.
Pixels are enlarged 6x into a centered 960x864 image; a missing source frame
repeats the current bank cleanly. The same F12 overlay and diagnostic-video
switch used by C64 sit after this bridge. USB gamepads use the common eight-bit
pad convention; arrows, Z, X, Enter, and right Shift provide keyboard control.

## Common bulk-memory service

The core interface exposes one outstanding, aligned 64-bit line request. Reads
and byte-masked writes use explicit request acceptance and response-valid
handshakes. The GBC adapter uses it for cartridge ROM and external cartridge
RAM, with a one-line cache that suppresses machine clock enables during misses
or writes.

`multitini_memory_cdc` carries this contract between the machine and control
domains. `multitini_psram_line_service` translates it to the imported
Appletini PSRAM driver. The physical GBC path is opt-in and defaults off; the
PSRAM pins remain inactive until Vivado and hardware gates pass.

The two x4 PSRAM devices share command/address signals and transfer four bytes
each per command. The line service packs one logical 64-bit line across them:
logical bytes 0-3 use device A and bytes 4-7 use device B at physical address
`logical_address >> 1`. This exposes the pair as a contiguous 16 MiB logical
store and keeps GBC cartridge RAM at logical `0x800000` from aliasing ROM on
the devices' 23-bit physical address buses.

## Interface versioning

`spec/interface_v0.json` is the source of truth for the first ARM-to-shell
register ABI. Generated C and SystemVerilog definitions must be regenerated and
checked together. Major versions are incompatible. Minor versions only add
backward-compatible behavior. ABI 0.6 adds two text VRAM banks, an atomic
vblank commit/status handshake, and a build-time core-variant index so the ARM
can identify and safely restore the boot image. ABI 0.7 adds the GBC save-RAM
stream and journal-backed media service. An older 0.6 GBC package remains
loadable, but the supervisor does not advertise or send persistence data to it.
ABI 0.8 adds the packed four-byte direct-load transport and the C64 CPU-turbo
control bit. The core-facing loader remains a byte ready/valid stream, and ABI
0.7 packages retain their byte-at-a-time register path. Fast serial ROM
selection is an ARM-owned profile switch and does not overclock the PL or add a
drive-warp control bit. ABI 0.10 adds the C64 drive parallel-cable control bit,
which firmware sets only while the fast ROM pair is installed.

`spec/core_interface_v0.md` records the separate core-facing ready/valid,
memory, and bidirectional media contracts. It is deliberately pre-release.
D64 transfers use bounded 256-byte blocks, while G64 reads use normalized
half-track payloads; the shared ABI does not pretend that the two file formats
have identical storage semantics.

## Prepared memory test

`multitini_psram_bist` is a destructive, line-oriented sequencer for the
Appletini PSRAM command interface. It enters QPI mode, disables wrapping,
writes and reads a deterministic pattern, captures the first mismatch, counts
all mismatches, and records read, write, and command-turnaround maxima. Its
protocol and error behavior pass simulation against a variable-latency model.

The BIST is not connected to board pins in the first diagnostic top. The
Vivado owner must first bind and review the imported PHY primitives, add
IDELAYCTRL, resolve external PSRAM timing, and then expose an explicit opt-in
test command. Until then `PSRAM.CE#` remains high.
