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
advertises dual SID, bit 1 advertises the 16 MB REU, and bit 2 (`REU_DDR`)
advertises DDR-backed REU with the shared DDR quiescence contract. Other machines return
zero. Firmware must check ABI and capability before reading or writing these
registers.

Configuration bits 2:0 select SID addressing: 0 sends the same accesses to
both engines, 2 selects $D420 for chip 2, and 3 selects $D500. Other encodings
become 0. Bit 3 enables the REU. Reserved bits read zero; the supported C64
image defaults to `0xB`. Partial writes respect byte strobes. A coherent
mailbox carries the latest complete word to the machine clock domain.

The REU owns DDR `0x25000000-0x25FFFFFF`; cartridge storage remains on PSRAM.
Capability bit 19 (`CART_COEXIST`) advertises per-cartridge REU, UCI and
Ultimate Audio compatibility. Requested configuration remains unchanged while
an incompatible cartridge suppresses effective peripheral state. Images without
this capability retain the blanket REU/cartridge exclusion. With this capability,
read-only `C64_REU_HOST` bits 16, 17 and 18 report effective REU, Ultimate Audio
and UCI activation through the existing coherent status snapshot. See
[cartridge compatibility](../docs/C64_CARTRIDGE_COMPATIBILITY.md).
Capability bit 20 (`CART_ROM_EXT`) advertises the ROM-only profiles for CRT
hardware types 11, 54, 64, 65, 66, 71, 86 and 87, all with subtype 0. The
largest admitted ROM is 2 MiB. Firmware checks this bit before reset or
streaming; older images retain their unsupported-type rejection. Exact CHIP
layouts and mapping limits are listed in the cartridge compatibility guide.
Capability bit 21 (`CART_EASYFLASH`) advertises CRT hardware type 32
(EasyFlash), subtype 0. Firmware streams its 1 MiB flash as a dense CRT of 128
8 KiB packets, bank b's ROML then its ROMH, CHIP type 0; the loader stores
them in that canonical order. With this capability, `C64_EF_JOURNAL` (0x17C)
presents one flash operation at a time: valid bit 31, kind bits 30:29
(1 program, 2 sector erase, 3 chip erase), ROMH bit 28, chip address bits
27:9 (bank in 27:22, offset in 21:9) and data bits 7:0. Writing back the exact
word read pops it; any other write is a bad write. The FPGA queues each
operation before it changes cartridge memory and holds the next one while the
queue is full. With bit 21, `C64_PERIPHERAL_CONTROL` bit 16 sets the
EasyFlash boot jumper to Disable: with `$DE02` M=0, GAME is released and a
reset starts BASIC. While EasyFlash is attached, the UCI registers move from
`$DF1B-$DF1F` to `$DE1B-$DE1F`. See [EasyFlash](../docs/C64_EASYFLASH.md).
Capability bit 22 (`DRIVE_ACTIVITY`) adds live lights to `C64_DRIVE_STATUS`:
drive A's LED in bit 8 and spindle in bit 9, both only while its CPU runs,
and the EasyFlash LED (`$DE02` bit 7) in bit 10.
Disabling REU resets its command state and
drains accepted writes and memory responses without erasing REU storage. Firmware
checks the live ABI and `VALID | REU_DDR` capabilities before requiring the DDR
quiescent bit in `VIDEO_FRAME_STATUS`. This check does not depend on whether REU
is currently enabled: dirty writes can outlive disable. Older C64 images do not
advertise this contract. See [C64 expansions](../docs/C64_EXPANSIONS.md).

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

## C64 Ultimate-compatible extensions (ABI 0.26)

Firmware checks the package ABI and live capabilities before using this range.
The extension registers are present only on the C64 image; absent capabilities
read zero and reject unsupported data operations. These additions retain the
stock VICE-compatible CPU/VIC/REU timing path.

| Register | Offset | Meaning |
| --- | --- | --- |
| NET_CAPS | 0x02c | Valid bit31; revision1 in bits3:0; W5100S byte bridge bit4 |
| NET_CONTROL | 0x030 | Run bit0; clear asserts Ethernet reset |
| NET_ACCESS | 0x034 | Address14:0, data23:16, write24; full word only |
| NET_STATUS | 0x038 | Ready0, busy1, done2, error3, reset done4, interrupt5; read byte15:8, completion sequence23:16, valid31 |
| C64_UCI_STATUS | 0x128 | Native endpoint snapshot; also snapshots STATUS2 |
| C64_UCI_HANDSHAKE | 0x12c | Tagged host operation |
| C64_UCI_CONTROL | 0x130 | Bus ID4:0; freeze8, IRQ9, HOLD10 |
| C64_UCI_STATUS2 | 0x134 | Consumption counters and completion tokens |
| C64_UCI_RAM_READ | 0x138 | Physical RAM address15:0 and length-minus-one26:16 |
| C64_SID_INDEX | 0x154 | Explicit shadow table index0..56 |
| C64_SID_DATA | 0x158 | Full-word shadow table data |
| C64_SID_COMMIT | 0x15c | Low-byte sequence; atomically commit a complete table |
| C64_SID_STATUS | 0x160 | Active sequence7:0; busy8; rejected9; reason14:10; index21:16; valid31 |

`C64_EXPANSION_CONFIG` bit5 enables UCI and bit6 selects the uploaded SID map.
Bits0..4 retain their earlier REU/dual-SID meaning. Expansion capabilities add
UCI5, SID table6, chip count minus one10:8, 8580 support11, SID IO slots12,
physical RAM DMA13 and UCI IRQ14. The production image supplies eight chips.
`C64_CPU_CONFIG` bit20 enables the optional SuperCPU detection shim; matching
`C64_CPU_CAPS2` bit15 advertises it. This is a software detection interface,
not a 65816 CPU or SuperCPU memory implementation.

The SID table has 57 words. Words0..31 select chip write masks for each 32-byte
slot in D400..D7FF; bit8 inhibits writes. Reads use the lowest selected chip.
Words32..47 select masks for the eight IO1 and eight IO2 slots. Words48..55
hold each chip's resolved left coefficient7:0, right coefficient15:8,
model16 and digi correction17. The firmware resolves gain/pan into these
coefficients. Word56 holds the Ultimate Audio left coefficient7:0, right
coefficient15:8, averaging16, DC removal17 and output attenuation19:18.
Reserved bits must be zero. Each main SID slot requires a nonzero
mask; an inhibited slot requires a single owner. IO masks may be zero.

Every upload writes all 57 words with an explicit INDEX write before DATA.
There is no auto-increment. DATA and COMMIT are rejected while busy. A complete
valid table moves through an immutable committed bank and becomes active at
the native SID boundary, or immediately while the machine is paused. The
previous active table remains in use until adoption. Firmware waits for the
matching sequence and valid flag before releasing HOLD. An ambiguous timeout
retains HOLD and requires recovery. Reject reasons are missing word1, empty
main slot2, unavailable chip3, unsupported IO4, reserved/invalid field5,
busy commit6 and ambiguous inhibited owner7.

UCI occupies exactly DF18..DF1F when enabled and permitted by cartridge
mapping. Generic ROM-only cartridge type0 permits it; other cartridge types
and cartridge loading suspend it. Its native registers retain the Ultimate command handshake and byte
queues. The host buffer at shell offset C000..C7FF is a 2 KiB dual-clock RAM:
commands C000..C37F, responses C380..C6FF and status text C700..C7FF. Host
accesses are aligned, single-word AXI transactions. Sub-word writes use byte
strobes; unaligned and burst transactions are rejected. The native port owns
command bytes; the host owns reply bytes until its publish operation completes.
DF1D returns C9 with no IRQ and49 while IRQ is asserted. Reading either reply
queue, including an empty read, clears IRQ; DATA_ACC also clears it. A PUSH
with TRIGGER arms a deferred freeze on the next accepted CPU write to FF00.
Publishing a reply cancels both immediate and deferred freeze. If the same
FF00 write starts REU DMA, DMA drains before the C64 scheduler pauses.

STATUS bits are state1:0, abort2, DATA_ACC3, error4, freeze5, trigger6, IRQ7,
command length17:8, sequence21:18, epoch25:22, suspended26, enabled27,
host operation busy28, idle-abort29 and valid31. STATUS2 has response bytes
consumed9:0, status bytes consumed18:10, operation acknowledgment20:19,
dropped operation21, busy-write22, overflow23, RAM completion token27:24,
CMD_BUSY30 and valid31. Read STATUS before STATUS2 to obtain one snapshot.
Consumed counts survive LAST-to-next-command handoff until the next reply is
published, so a partial SoftIEC read advances only bytes actually consumed.

HANDSHAKE packs opcode3:0, response length13:4 (0..896), status length23:16
(0..255), sequence28:25 and low three epoch bits31:29. Bits15:14 and24 must
be zero. Operations are ACCEPT1, NEXT2, ABORT_DONE3, LAST4, MORE5,
IDLE_ABORT_DONE6 and RESET15. Every accepted or stale operation advances the
acknowledgment; stale tags cannot change the new command. The native endpoint
can count 256 status bytes, but the host ABI limits text to255 for client
terminator compatibility. Data transport supports896, including DOS ECHO.

HOLD pauses the whole C64 and accelerated CPU after active REU DMA drains.
Physical RAM writes use stream kind17: two little-endian address bytes followed
by at most256 data bytes. Physical RAM reads use RAM_READ with length1..896;
firmware waits for the modulo16 completion token before using the reply area.
Accepted reads must drain even during cancellation. REU transfers use the
16 MiB DDR allocation at25000000 and finish with the REU/Ultimate Audio cache
invalidation acknowledgment before releasing HOLD. Neither transfer resets the
CPU or Ultimate Audio playback registers.

UCI target IDs are DOS1, independent DOS2, network3, control4, SoftIEC5 and
HTTP/JSON6. CPU0 owns SD access and the W5100S byte bridge. Network targets use
the controller's hardware sockets, bounded foreground polling, DHCP/DNS and
plain HTTP. HTTPS returns an explicit unsupported result before opening a
socket. Missing Ultimate-specific physical devices return the protocol's
unsupported-command status. The filesystem is rooted at the selected C64
media directory; SD and Usb0..2 aliases refer to that root. Temp is a separate
volatile sibling directory cleared in bounded foreground passes at core start.
