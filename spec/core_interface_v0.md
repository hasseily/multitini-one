# Core Interface 0.1

Status: pre-release signal contract. The first C64 adapter may extend it before
ABI 1.0. Board pins never cross this boundary.

## Clock and reset

The shell drives `clk` and synchronous active-high `reset`. An adapter asserts
`ready` only after its clocks, writable ROM stores, and internal reset sequence
are usable. `core_id` is stable while `ready` is high.

## Video and audio

Video is unscaled RGB888 plus active-high data enable and explicit syncs. The
adapter documents its pixel clock, polarities, active region, and PAL/NTSC
cadence. The shell owns conversion to the physical RGB565 DVI path.

Audio is signed 24-bit stereo. `audio_valid` marks a new sample pair. The
adapter documents its source rate; the shell owns rate conversion, gain,
clipping, mute, I2S, and SPDIF.

## Input events

`input_code`, `input_value`, and `input_valid` form a ready/valid event stream.
The shell holds code and value stable until `input_ready` is high. Releases are
explicit events; reset cannot be used as a substitute for missing releases.
The initial code namespace and C64 matrix table are not frozen until the USB
host path supplies captured keyboard and pad reports.

## Direct-load stream

`load_kind`, `load_offset`, `load_total`, `load_data`, `load_first`,
`load_last`, and `load_valid` form a ready/valid byte stream.

- All fields remain stable until `load_ready` is high.
- `load_first` marks byte offset zero and supplies the total byte count.
- `load_offset` is the absolute byte offset within this object.
- `load_last` marks offset `load_total - 1`.
- A receiver rejects an unexpected offset, early last, or late byte.
- Reset aborts a partial object. A new object must restart at offset zero.

The first kinds will cover C64 BASIC, KERNAL, character, and 1541 ROMs, then
PRG and CRT data. Numeric assignments wait for the rights-cleared core adapter
so unused formats are not baked into the common ABI.

## Deliberately absent

The disk and save-data service is not frozen yet. D64 sector service and G64
track service have different atomicity and latency needs. That contract will
be based on the rights-cleared 1541 integration and must include request IDs,
length, write protection, timeout, flush, and eject semantics.
