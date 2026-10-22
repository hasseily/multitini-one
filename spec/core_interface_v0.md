# Core Interface 0.1

Status: pre-release signal contract, exercised by C64 and GBC adapters. It may
still extend before ABI 1.0. Board pins never cross this boundary.

## Clock and reset

The shell drives `clk` and synchronous active-high `reset`. An adapter asserts
`ready` only after its clocks, writable ROM stores, and internal reset sequence
are usable. `core_id` is stable while `ready` is high.

## Shared menu pause

ABI 0.11 adds a shared `pause_request` for C64, GBC, and later adapters. The
shell derives it from committed overlay visibility **and dense mode**, then
synchronizes it into the machine clock domain. Firmware cannot override pause
while the dense menu is visible. Closing the menu resumes the preserved machine
state without asserting reset.

Pause holds machine execution and emulated time, including C64 CIA time-of-day,
drive CPU/VIAs and disk rotation, and GBC cartridge RTC phase. Machine audio
is muted for the same interval. Physical clocks, memory transactions, loader
logic, and disk/save request controllers continue operating. ARM polling must
continue so pending transfers and battery saves can finish while the machine
is paused. Input releases also remain accepted; pause does not reset input.

Actions requiring CPU or drive progress first commit the menu hidden and wait
for the acknowledged hidden state before starting. This includes native LOAD,
RUN, media changes, ROM-profile changes, and restart. This temporary overlay
hide preserves the user's requested menu state; completion restores the menu
and pause after success or failure. A failed hide cancels the action. Only an
explicit exit closes the menu. Browsing and simple settings remain paused.
See [Shared front end](../docs/SHARED_FRONTEND.md) for action policy.

The legacy 80x36 overlay retains its previous execution behavior. Older firmware
does not request dense mode and cannot hide its menu before blocking native
loads. New firmware always requests dense mode and requires ABI 0.11 packages.

## C64 expansion capabilities

ABI 0.23 adds `C64_EXPANSION_CONFIG` at `0x0D0` and read-only
`C64_EXPANSION_CAPS` at `0x0D4`. Capability bit 31 marks a valid mask; bit 0
advertises dual SID and bit 1 advertises the 16 MB REU. Other machines return
zero. Firmware must check ABI and capability before reading or writing these
registers.

Configuration bits 2:0 select SID addressing: 0 sends the same accesses to
both engines, 2 selects $D420 for chip 2, and 3 selects $D500. Other encodings
become 0. Bit 3 enables the REU. Reserved bits read zero; the supported C64
image defaults to `0xB`. Partial writes respect byte strobes. A coherent
mailbox carries the latest complete word to the machine clock domain.

REU and CRT cannot simultaneously own PSRAM. Firmware rejects their conflicting
actions before loading or saving settings, and the adapter suppresses REU while
a cartridge is present or loading. Disabling REU resets its command state and
drains any accepted memory response. See [C64 expansions](../docs/C64_EXPANSIONS.md).

## IIgs installed capabilities

ABI 0.22 adds read-only `IIGS_ACCEL_CAPS` at `0x0CC`. Bit 31 marks a valid
IIgs capability; bits 6:0 advertise supported CPU speed codes. Bit 8 advertises
retained media requests across mount/eject and fresh-ID retry publication.
Bit 9 (`MT_IIGS_FAST_FLOPPY_SUPPORTED`, `0x200`) permits configuration flags
bit 7 (wire byte 3 bit 7): Fast floppy. It defaults Off. Changing it saves and
ejects media under the old timing, then applies the new value while reset is
asserted and remounts media. Firmware retains a saved On preference with an
older image, disables its menu toggle and transmits the bit as zero.
Other bits are zero. Other machine images return zero. Never read this register on older
ABIs; those IIgs images support the legacy mask `0x1F`.

| Code | Fast-cycle target |
| --- | --- |
| 0 | Stock motherboard timing |
| 1 | 3.579546 MHz |
| 2 | 4.772727 MHz |
| 3 | 7.159091 MHz |
| 4 | 14.318182 MHz |
| 5 | 28.636364 MHz |
| 6 | 57.272728 MHz |

These are eligible cached CPU cycle rates; native peripheral access, cache
misses, acknowledged writes and compatibility delays reduce application
throughput. Firmware offers only advertised codes and clamps persisted faster
settings when an older image is selected. ZipGS software percentages use the
installed maximum, with its wall-clock timer retained on the native clock.

Target-scoped media draining requires bit 8. Such firmware rejects older
IIgs images before issuing media commands and asks for a core update. A
capable IIgs shell retains outstanding read and write snapshots across mount
and eject, and publishes a fresh request ID/generation even when the native
controller keeps request-valid asserted during a retry. The file service
keeps the target file open until its requests and hardware eject state finish;
requests from other drives can continue. PRAM operations also require an
acknowledged matching request before reset can proceed. Older firmware can
ignore bit 8 and retain its original global drain policy.

## Video and audio

The Vectrex shell extension in ABI 0.13 provides `VECTREX_VIDEO_CONFIG`
(0x0C4) and `VECTREX_OVERLAY_STATUS` (0x0C8). Configuration holds overlay enable
in bit 0, beam-gain index in bits 2:1, and artwork-opacity index in bits 4:3;
reserved bits read zero. The default is 0xB (enabled, 2x beam, 50% artwork).
Changes take effect coherently at vertical blank. Read-only status reports
loaded in bit 0 and error in bit 1; other machine images report zero.

Vectrex stream type 0x45 accepts 777600 bytes of row-major 540x720 ABGR4444
artwork, or one zero byte to clear it. Firmware pads a short even file with
transparent black. A new transfer invalidates the prior image immediately;
only a complete, valid, non-aborted transfer publishes the new image. Final
stream acceptance can precede the last physical memory write, so firmware
polls loaded/error status after STREAM_END. Overlay errors remain independent
of native machine readiness. Soft machine reset retains the overlay. Firmware
must check the actual core ID and ABI before using the extension.

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

ABI 0.8 changes only the supervisor-side register transport for this stream.
When both ends report ABI 0.8 or newer, firmware writes four little-endian
bytes at a time to `STREAM_DATA_BURST` and observes `STREAM_STATUS.READY` and
`STREAM_STATUS.DRAINED`. The shell serializes those bytes into the unchanged
core-facing ready/valid byte stream, preserving order and the existing
`load_first`, offset, and `load_last` semantics. Firmware waits for the burst
holding register to drain before ending the object. ABI 0.7 images continue to
use the original byte register.

The C64 ABI 0.2 assignments are:

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
| PRG stage | `0x15` | 3-65538 | ABI 0.9; reset/inject/repair pointers without typing RUN |
| PRG run | `0x16` | 1-byte zero token | ABI 0.9; type RUN only for a successfully staged PRG |
| GBC bootstrap | `0x20` | 2304 | Implemented; optional override of the built-in open bootstrap |
| GB/GBC cartridge | `0x21` | 32768-8388608 | Implemented |
| GB/GBC save RAM | `0x22` | 256-131072, multiple of 256 | ABI 0.7; implemented for supported external battery RAM |

The loader begins with empty ROM stores, accepts only the three exact system
ROM sizes above, and asserts a sticky error for unsupported or malformed
objects. The GBC loader accepts one optional exact bootstrap, one cartridge,
and an optional whole save-RAM image while reset is asserted. The bitstream
contains the pinned open GBC bootstrap but no commercial game.

ABI 0.9 adds the C64 staged-PRG pair used by D64 `Mount & Run`. Firmware stages
a validated BASIC-start PRG while the disk is safely ejected, remounts the
parent D64, and only then sends the run token. Ordinary `C64_PRG` retains its
existing automatic-RUN behavior for loose files.

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

The C64 adapter keeps the two most recently completed 8 KiB track payloads in
FPGA block RAM for both D64 and G64. A hit is copied locally into the active
1541 buffer without issuing another ARM/SD request. A D64 write invalidates the
cached copy of the modified track. Mount, eject, and failed or partial fills
cannot leave a cache entry valid.

ABI 0.8 assigns control bit 5 to the optional FPGA64 Smart 2x CPU turbo. It
adds 6510 RAM/ROM cycles without changing VIC, CIA, I/O, or drive timing, and
automatically pauses during IEC activity and for 0.5 seconds afterward without
altering the saved setting. The setting defaults off and must be explicitly
enabled by the user. Control bit 4 is reserved and ignored; the 1541 always
runs at authentic timing.

ABI 0.10 assigns control bit 6 to a DolphinDOS-style parallel cable between
the C64 user port (CIA2 port B, PC2, FLAG2) and the emulated 1541 (VIA1 port
A, CB1, CA2). Setting it also maps 8 KiB of drive RAM at `$8000-$9FFF`,
matching the upstream option. Firmware sets the bit only while a fast ROM pair
is installed, because the stock drive ROM drives VIA1 port A low; while the
bit is clear, or while the drive is in reset, the C64 reads released lines.
Cable strobes pause CPU turbo like IEC traffic.

ABI 0.7 also uses this request service for GBC battery RAM. After a quiet
interval, or immediately on eject, the adapter pauses the machine and snapshots
the complete external cartridge-RAM extent as one write request at LBA zero.
Firmware journals `<cartridge>.sav` before returning success. A failed
completion leaves the image dirty; detach, reset, core switching, and golden
reboot must not proceed until a retry succeeds. RTC state and mapper-internal
MBC7/TAMA backup storage are not part of this transfer.

## Shared video processing (ABI 0.15)

CPU0 owns these registers. CPU1 receives color-settings jobs through an uncached
mailbox and never accesses PL. The FPGA schedules frames autonomously in order
at the fixed 60 Hz output cadence. Firmware must check both package and hardware
versions before using the new register ranges.

| Register | Offset | Meaning |
| --- | --- | --- |
| VIDEO_EFFECT_ADDRESS | 0x140 | Inactive-bank word address, bits 9:0 |
| VIDEO_EFFECT_DATA | 0x144 | Write the selected word |
| VIDEO_EFFECT_COMMIT | 0x148 | Commit a complete bank with a nonzero sequence |
| VIDEO_EFFECT_STATUS | 0x14c | Busy bit 0, valid bit 1, rejected bit 2, supported bit 3 |
| VIDEO_EFFECT_SEQUENCE | 0x150 | Sequence active in the pixel domain |
| VIDEO_FRAME_CONTROL | 0x180 | Capture bit 0, display bit 1, autonomous bit 2; normal operation writes 7 |
| VIDEO_FRAME_STATUS | 0x184 | Capture busy, display pending, display active, quiescent, error, autonomous supported, engine busy, scheduler fault in bits 0–7 |
| VIDEO_FRAME_CAPTURE_PRODUCER | 0x188 | Published capture count |
| VIDEO_FRAME_CAPTURE_CONSUMER | 0x18c | Released capture count |
| VIDEO_FRAME_DISPLAY_SUBMIT | 0x190 | Atomic sequence[31:2] and output slot[1:0] |
| VIDEO_FRAME_DISPLAY_ACTIVE | 0x194 | Acknowledged active sequence and slot |
| VIDEO_FRAME_GEOMETRY | 0x198 | Width[15:0], height[31:16] |
| VIDEO_FRAME_STRIDE | 0x19c | Native RGB565 row stride in bytes |
| VIDEO_FRAME_CAPTURE_DROPS | 0x1a0 | Dropped complete capture attempts |
| VIDEO_FRAME_AXI_ERRORS | 0x1a4 | Read/write response error count |
| VIDEO_FRAME_DISPLAY_UNDERRUNS | 0x1a8 | Display fallback count |
| VIDEO_FRAME_ERROR_CLEAR | 0x1ac | Write 1 while disabled and quiescent to clear reported errors/counters |
| VIDEO_FRAME_OPTIONS | 0x1b0 | Persistence[1:0], blur[3:2], bloom[5:4]; latched per job |
| VIDEO_FRAME_PROCESSED | 0x1b4 | Successful processing completions since reset |
| VIDEO_FRAME_DEADLINES | 0x1b8 | Missed presentation deadlines; any nonzero value fails qualification |
| VIDEO_FRAME_ENGINE_CYCLES | 0x1bc | Most recent job clock count, including DDR stalls |
| VIDEO_FRAME_ENGINE_MAX_STALL | 0x1c0 | Maximum consecutive AXI stall clocks since reset/error clear |
| VIDEO_FRAME_ENGINE_ERRORS | 0x1c4 | HP1 processing error count |

Effect addresses 0–8 hold signed Q10 matrix coefficients. Addresses 9–11 are
reserved zero offsets. Address 12 holds scanlines[1:0], mask[3:2], horizontal
filter[5:4], and master enable[6]. Addresses 0x100–0x1ff hold packed
`0x00BBGGRR` LUT entries. All 269 words must be supplied before commit.
Partial/invalid writes and writes to a pending bank are rejected. The pixel
domain swaps the complete bank at vblank and acknowledges before it is reused.

Capture/output pools each contain four fixed 1 MiB slots, based at 0x22000000
and 0x22400000 respectively. Producer minus consumer is an unsigned count
bounded by four, including wrap. Consumer may release only published frames;
capture never overwrites a retained slot. Rows have 128-byte-aligned stride.
The hardware scheduler consumes the oldest capture, releases it only after
the engine's complete write-response fence, and excludes active and pending
output slots from allocation. Persistence history is dense `0x00BBGGRR` at
0x22800000, with a reserved size of 2 MiB. HP0 owns capture/scanout; HP1 owns
native frame processing. Neither CPU accesses these three memory pools.

Autonomous mode requires both capture and display enabled. While it owns the
ring, manual consumer and display-submit writes are rejected, and control
may only remain 7 or become 0. Disable and drain before switching ownership.
The manual ring registers remain available for isolated transport diagnostics.
Writing options while running takes effect at the next job without stopping
the ordered identity/effects path. A change to these options resets history.

Display publication is acknowledged after vblank adoption and old-read drain.
Disabling transport preserves ownership until outstanding HP0 and HP1 transactions
finish. `QUIESCENT` requires control zero and all engines drained; firmware
must observe it before PCAP. Error clearing only changes diagnostics, never
ring ownership or outstanding AXI handshakes. Underruns, capture drops, DDR
errors, and missed deadlines set the error status and stop new autonomous jobs.
There is no latest-frame selection or tolerated performance drop count.
See `docs/CPU1_VIDEO_EFFECTS.md` for
processing order, DDR ownership, and the fault fallback contract.
