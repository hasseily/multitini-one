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

## First image

The first image is one full Zynq configuration containing:

- the PS and its DDR, QSPI, SD, UART, I2C, and two ULPI controllers;
- a small AXI control register bank;
- an independently visible DVI test pattern;
- independently audible I2S and SPDIF test tones;
- safe idle values on Apple, Ethernet, and PSRAM pins; and
- later, the C64 adapter and core.

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
| Audio MCLK | 12.288 MHz | board oscillator | DAC clock and I2S derivation |
| Audio BCLK | 3.072 MHz | MCLK divided by four | 48 kHz, 64-bit I2S frames |

Every asynchronous crossing must use a named synchronizer or handshake. Every
generated clock and exception must bind to an existing object in the final
Vivado design. These facts cannot be certified on this Mac.

## Interface versioning

`spec/interface_v0.json` is the source of truth for the first ARM-to-shell
register ABI. Generated C and SystemVerilog definitions must be regenerated and
checked together. Major versions are incompatible. Minor versions only add
backward-compatible behavior.

`spec/core_interface_v0.md` records the separate core-facing ready/valid
contract. It is deliberately pre-release. Media service semantics wait for a
rights-cleared 1541 integration instead of assuming that D64 sector requests
also fit G64 track data.

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
