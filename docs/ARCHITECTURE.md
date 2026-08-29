# Architecture Baseline

Status: source design, not Vivado or hardware validated.

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
reuse-validation image. This is full-image selection, not partial
reconfiguration.

Each image is one full Zynq configuration containing:

- the PS and its DDR, QSPI, SD, UART, I2C, and two ULPI controllers;
- a small AXI control register bank;
- an independently visible DVI test pattern;
- independently audible I2S and SPDIF test tones;
- safe idle values on Apple, Ethernet, and PSRAM pins; and
- one retained machine adapter/core integration whose outputs are not yet
  selected onto the physical DVI and audio paths.

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
Vivado design. These facts cannot be certified on this Mac.

Sparse ROM bytes and input event pairs move from the 133.333 MHz control domain
to the C64 domain through separate one-entry `xpm_cdc_handshake` mailboxes. A
source holds every payload bit until the destination accepts it. Reset request,
ready, and error levels
use named bit synchronizers; the sparse error-clear event uses a tested
three-stage toggle synchronizer. The behavioral CDC tests deliberately use
unrelated clocks and backpressure. Vivado CDC reports and XPM handshake
constraint binding remain mandatory.

The C64 adapter holds machine reset until BASIC, KERNAL, and character ROMs have
each completed a strictly framed transfer. The ARM command ABI supports begin,
data, end, and abort. Firmware retries bounded mailbox backpressure and leaves
the core in reset if a ROM set cannot be loaded.

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
