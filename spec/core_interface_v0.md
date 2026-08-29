# Core Interface 0.1

Status: pre-release signal contract, exercised by C64 and GBC adapters. It may
still extend before ABI 1.0. Board pins never cross this boundary.

## Clock and reset

The shell drives `clk` and synchronous active-high `reset`. An adapter asserts
`ready` only after its clocks, writable ROM stores, and internal reset sequence
are usable. `core_id` is stable while `ready` is high.

## Video and audio

Video is unscaled RGB888 plus active-high data enable and explicit syncs. The
adapter documents its pixel clock, polarities, active region, and PAL/NTSC
cadence. `video_valid` marks a new pixel sample. The shell owns conversion to
the physical RGB565 DVI path.

Audio is signed 24-bit stereo. `audio_valid` marks a new sample pair. The
adapter documents its source rate; the shell owns rate conversion, gain,
clipping, mute, I2S, and SPDIF.

## Input events

`input_code`, `input_value`, and `input_valid` form a ready/valid event stream.
The shell holds code and value stable until `input_ready` is high. Releases are
explicit events; reset cannot be used as a substitute for missing releases.
The C64 adapter currently defines the high byte of `input_code` as a namespace:

| Namespace | Value | Low byte / value |
| --- | ---: | --- |
| PS/2 set 2 | `0x00` | Low byte is the scan code; `input_value[0]` is pressed and bit 1 is extended |
| Joystick 1 | `0x01` | Low byte selects bit 0-6; `input_value[0]` is its new level |
| Joystick 2 | `0x02` | Low byte selects bit 0-6; `input_value[0]` is its new level |

The first USB host translation and user-visible C64 key map are documented in
`docs/C64_USB_INPUT.md`. They remain an initial compatibility contract until
the physical keyboard/gamepad gate passes.
The ARM register ABI's `INPUT_EVENT` command retries mailbox backpressure and
crosses the code/value pair coherently into the selected C64 clock domain.

The GBC adapter uses namespace `0x10`. The low byte selects Right, Left, Down,
Up, A, B, Select, or Start as indices 0 through 7; `input_value[0]` is the new
pressed level.

## Direct-load stream

`load_kind`, `load_offset`, `load_total`, `load_data`, `load_first`,
`load_last`, and `load_valid` form a ready/valid byte stream.

- All fields remain stable until `load_ready` is high.
- `load_first` marks byte offset zero and supplies the total byte count.
- `load_offset` is the absolute byte offset within this object.
- `load_last` marks offset `load_total - 1`.
- A receiver rejects an unexpected offset, early last, or late byte.
- Reset aborts a partial object. A new object must restart at offset zero.

The C64 ABI 0.1 assignments are:

| Kind | Value | Exact size | Current adapter |
| --- | ---: | ---: | --- |
| BASIC ROM | `0x01` | 8192 | Implemented |
| KERNAL ROM | `0x02` | 8192 | Implemented |
| Character ROM | `0x03` | 4096 | Implemented |
| 1541 ROM | `0x04` | 16384 | Reserved; drive excluded from first build |
| PRG | `0x10` | variable | Reserved |
| CRT | `0x11` | variable | Reserved |
| D64 | `0x12` | variable | Reserved for media service design |
| G64 | `0x13` | variable | Reserved for media service design |
| GBC bootstrap | `0x20` | 2304 | Implemented; optional override of the built-in open bootstrap |
| GB/GBC cartridge | `0x21` | 32768-8388608 | Implemented |

The loader begins with empty ROM stores, accepts only the three exact system
ROM sizes above, and asserts a sticky error for unsupported or malformed
objects. The GBC loader accepts one optional exact bootstrap and one cartridge.
The bitstream contains the pinned open GBC bootstrap but no commercial game.

## External line memory

The interface supports one outstanding aligned 64-bit line transaction:

- `memory_valid`, address, write flag, write data, and byte strobes remain
  stable until `memory_ready` accepts the request;
- reads set all byte strobes to zero;
- writes may update any subset of the eight bytes;
- each accepted request receives exactly one `memory_rvalid` response;
- read responses return the line data; write responses return the resulting
  merged line; and
- `memory_fault` is sticky until reset and prevents an adapter from claiming
  readiness.

The GBC map places cartridge ROM at `0x000000` and cartridge RAM at `0x800000`.
The current service supports at most 8 MiB of ROM and 128 KiB of cartridge RAM.

## Deliberately absent

The disk and save-data service is not frozen yet. D64 sector service and G64
track service have different atomicity and latency needs. That contract will
be based on the rights-cleared 1541 integration and must include request IDs,
length, write protection, timeout, flush, and eject semantics.
