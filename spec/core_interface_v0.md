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
| 1541 ROM | `0x04` | 16384 | Implemented and board-tested in the earlier read-only-D64 build |
| PRG | `0x10` | 3-65538 | Implemented in source; pending vendor build and board test |
| CRT | `0x11` | 80-2101312 | Implemented for the documented conservative mapper profiles; pending vendor build and board test |
| D64 | `0x12` | validated D64 sizes | Implemented in source for reads and transactional writes; pending vendor build and board test |
| G64 | `0x13` | validated G64 v0 | Implemented read-only in source; pending vendor build and board test |
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
These are logical addresses. On Appletini ONE, the two x4 PSRAM devices operate
in parallel: device A stores bytes 0-3 of each logical line and device B stores
bytes 4-7. The board service therefore sends physical address
`logical_address >> 1`, preserving alignment. Logical ROM occupies physical
`0x000000-0x3FFFFF`; cartridge RAM begins at physical `0x400000`. This packing
is required because each LY68L6400 implements only address bits A[22:0].
The C64 cartridge loader places the selected CRT payload at `0x200000` and
uses the same one-outstanding service. Only mapper layouts whose logical bank
addressing matches that packed representation are accepted.

## Media request service

ABI 0.5 extends the first D64 transport to bidirectional requests and G64
reads. A core presents one request at
a time using `media_request_valid`, `media_request_write`, a request ID, LBA,
block count, and mount generation. LBA and block count use 256-byte units. The
core holds every request field stable until the shell returns a matching
`media_complete` pulse. Successful completions require the exact requested
block count. `INVALID_ARGUMENT`, `IO_ERROR`, and `TIMEOUT` failure completions
carry a zero completed count so a core can preserve dirty state and enter a
deterministic retry/error state.

`media_mount`, `media_eject`, and `media_complete` are one-cycle events. Their
payload registers remain available alongside the pulse. Mounts carry slot,
content kind, image size, read-only state, and a nonzero generation selected by
firmware. Eject carries the slot and a new nonzero generation for the empty
slot, invalidating any request from the previous mount; ejecting an already
empty slot is idempotent. Completion carries request ID, status, completed
block count, and generation. The path is deliberately filename-free; ARM
firmware owns paths and files.

The data buffer and its ownership crossing are shell implementation details and
are not exposed directly. Read requests receive exactly `block_count * 256`
bytes through the normal direct-load stream. Write requests publish that same
number of bytes through destructive reads of `MEDIA_DATA`; `VALID` marks an
available byte, and `LAST` is asserted only for the final byte. `MEDIA_STATE`
reports one coherent mounted/dirty/eject-pending snapshot.

D64 eject is deferred while the drive has a dirty track. Firmware drains and
applies the exact write request, then persists the complete in-memory image
through a checksummed write-ahead journal before considering eject or reset
safe. G64 remains read-only. Its service normalizes each requested half-track
to an 8 KiB direct-GCR extent and rejects variable speed maps that the current
drive path cannot reproduce.
