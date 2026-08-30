# Architecture Baseline

Status: the independent PAL DVI, fixed-tone audio, UART, and golden-reboot
diagnostics have passed on hardware. Live PAL video, USB input, wired Xbox
input, SD ROM loading, 1541 startup, and read-only D64 game loading also have
recorded board results. Build `2026082F` connects live machine PCM to I2S and
SPDIF. Its first board test found mostly clean output plus repeatable stuck-tone
faults. Candidate `20260830` changes the C64 default SID model from 8580 to
6581; hardware retest remains pending.

## Ownership boundaries

The ARM control program owns files, USB protocol work, settings, menus, update
policy, and bounded replies to FPGA service requests.

The common FPGA shell owns board pins, safe idle states, clocks and resets,
control registers, video and audio output stages, input-event delivery, memory
services, and counters.

Each machine adapter translates the common shell interface to an imported core.
The imported core stays close to its pinned upstream source. Board-specific
changes belong in the shell or adapter.

## Build-time machine selection

One complete machine adapter is selected when the Vivado project is created.
`MULTITINI_MACHINE=C64` is the default; `MULTITINI_MACHINE=GBC` selects the
Game Boy Color reuse-validation image. Only the common and selected-machine
sections of the HDL manifest enter that project, so an inactive core is not a
build dependency. This is full-image selection, not partial reconfiguration.

Each image is one full Zynq configuration containing:

- the PS and its DDR, QSPI, SD, UART, I2C, and two ULPI controllers;
- a small AXI control register bank;
- an independent DVI test pattern and, for PAL C64, optional live machine
  frames converted to the fixed C64 palette;
- a shared I2S/SPDIF stage that selects live machine PCM or the fixed test tone;
- safe idle values on Apple, Ethernet, and PSRAM pins; and
- one retained machine adapter/core integration. The PAL C64 RGB output feeds
  the live-frame bridge, and the selected machine's PCM output feeds the common
  audio stage.

Partial reconfiguration is deliberately outside this baseline.

## Clock and reset domains

The source board uses a fixed 33.333333 MHz oscillator on `PS_CLK`. The PS
generates the fabric clocks. No clock or reset comes from the Apple connector.

Planned initial domains:

| Domain | Planned rate | Source | Use |
| --- | ---: | --- | --- |
| Control | 133.333 MHz | PS FCLK0 through Clocking Wizard | AXI registers and shell control |
| PSRAM reference | 199.995 MHz | same Clocking Wizard | IDELAYCTRL reference |
| Video | 148.5 MHz | PS FCLK2 through Clocking Wizard | 1080p diagnostic output |
| C64 machine | requested 31.527954 MHz PAL or 32.727266 MHz NTSC | PS FCLK2 through a separate Clocking Wizard | FPGA64 machine, ROM/input loaders, raw video/audio |
| GBC machine | requested 33.554432 MHz | PS FCLK2 through the build-selected machine Clocking Wizard | GB/GBC machine, cart/bootstrap loaders, raw video/audio |
| Audio MCLK | 12.288 MHz | board oscillator | DAC clock and I2S derivation |
| Audio BCLK | 3.072 MHz | MCLK divided by four | 48 kHz, 64-bit I2S frames |

Every asynchronous crossing must use a named synchronizer or handshake. Every
generated clock and exception must bind to an existing object in the final
Vivado design. In the verified default PAL build, the fatal CDC gate passed and
the `2026082F` live-audio report contains only 24 `CDC-3` Info crossings and one
`CDC-9` Info reset crossing. The machine-audio payloads use coherent XPM
handshakes, and both readiness crossings launch from a machine-domain register.
The NTSC and GBC vendor builds still require their own report review.

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

The current diagnostic firmware exposes `:c64rx` on UART0. The host uploader
combines the repository's selected BASIC, KERNAL, and character ROMs into an
exact 20 KiB XMODEM-1K/CRC transfer by default; explicit path overrides remain
available. Firmware verifies the bundle CRC32 and each ROM's size and
nonuniform content before using the portable stream helper. Failure leaves the
C64 in reset.

The `2026082E` firmware adds one ARM-owned FatFs instance and keeps
`ff.h` private to the Xilinx storage layer. C64 startup reads the three required
system ROMs from `0:/cores/c64/roms`, then independently tries the optional
16 KiB 1541 ROM. A missing drive ROM disables drive service without undoing a
successful C64 boot. The UART three-ROM path remains independent and unchanged.

The first media service reads one selected D64 completely into PS DDR and
serves bounded 256-byte read requests through the common command/register and
CDC-mailbox path. Mount generations reject stale requests. The service is
drive-8-only and read-only: firmware does not modify the image or write it back
to SD. The exact layout and operator commands are documented in
[C64_SD_STORAGE.md](C64_SD_STORAGE.md). Build `2026082E` passes its first board
test: the C64 and 1541 ROMs loaded from SD, the default drive-8 D64 mounted,
and Decathlon loaded and ran. The service remains read-only.

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
the timed Decathlon artifact. The model mismatch remains a diagnosis candidate
until Decathlon and Ultima II pass the repeated board test.

The GBC adapter holds reset until a cartridge is strictly framed and accepted
by the external-memory service. Its open CGB bootstrap is generated from a
pinned SameBoy-derived source; an optional 2304-byte override uses the same
direct-load stream. One gamepad namespace maps directly to the eight upstream
button bits.

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

## Interface versioning

`spec/interface_v0.json` is the source of truth for the first ARM-to-shell
register ABI. Generated C and SystemVerilog definitions must be regenerated and
checked together. Major versions are incompatible. Minor versions only add
backward-compatible behavior.

`spec/core_interface_v0.md` records the separate core-facing ready/valid and
memory contracts. It is deliberately pre-release. Media service semantics wait
for a rights-cleared 1541 integration instead of assuming that D64 sector
requests also fit G64 track data.

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
