# C64 fast disk: patched KERNAL and 1541 ROMs

This document describes the fast ROM pair built in `c64fast/`: a patched C64
KERNAL 901227-03 and a patched 1541-II DOS 251968-03 that move disk traffic
onto the parallel drive cable the fast ROM profile enables. It records the
protocol, the exact ROM changes, the drive engine, the measured results, the
limits, and what remains untested.

The current release uses this paired profile with the ABI 0.10 C64 FPGA
20260915 and ARM firmware 20260917. The owner confirms fast loading works
on the board, including Ultima II with a clean Player disk and fast ROMs On.
See [CURRENT_RELEASE.md](CURRENT_RELEASE.md) for the selected images and
[C64_LAUNCH_20260917.md](C64_LAUNCH_20260917.md) for the board record.

The ROM protocol is independent of CPU turbo. Default `Mount & Run` now
uses a native drive LOAD, so compatible programs benefit from the fast pair;
the explicit Left/direct-RAM shortcut bypasses the initial LOAD. The pair
targets unprotected, normal D64 images. Software that runs its own drive code
(custom loaders, most copy protection) keeps using the stock serial lines with
authentic timing and is not accelerated.

## Results

Measured on the py65 simulator in `scripts/c64sim` (PAL C64, badlines on,
authentic 1541 timing), using the same disk images for both pairs. These are
simulation timings, not physical-board measurements:

| Operation | Stock pair | Fast pair | Ratio |
|---|---|---|---|
| `LOAD"BIG",8,1`, 202 blocks (51 KiB) | 127.4 s | 12.8 s | 9.9x |
| `LOAD"HELLO",8`, 8 blocks, cold drive | 6.55 s | 1.87 s | 3.5x |
| `LOAD"$",8` (3 entries) | 1.73 s | 1.70 s | 1.0x |
| `SAVE"NEW",8`, 8 blocks | 7.15 s | 6.04 s | 1.2x |

Every loaded byte was compared with the file on the image. The 202 block
figure matches the well known stock time of about two minutes.

Where the 12.8 s of the big LOAD go, from breakpoints on the drive:

| Phase | Total | Per unit |
|---|---|---|
| GCR decode in the drive (stock ROM decoder) | 5.4 s | 27 ms per sector |
| Transfer of the decoded sector over the cable | 2.9 s | 14.5 ms per sector (57 cycles per byte) |
| Reading a whole track raw, one revolution each | 2.1 s | 207 ms per track |
| Head stepping between tracks | 0.28 s | 28 ms per track |
| Waiting for the controller IRQ to dispatch a job | 0.12 s | 12 ms per track |
| Header validation, chain walk, framing | 0.2 s | |

The GCR decode is the wall. The 1541 needs about 350 cycles per 5-byte GCR
group with either the stock decoder or a fresh one, because the 6502 has to
shift nibbles across byte boundaries; the fast loaders that beat this keep
1.5-2 KiB of lookup tables in drive RAM. See "Limits and next steps".

## Hardware the fast pair relies on

The fast ROM profile (ABI 0.10, control bit `C64_DRIVE_PARALLEL_ENABLE`)
enables the MiSTer 1541 model's DolphinDOS option:

- C64 user port PB0-PB7 (CIA2 port B, `$DD01`) to drive VIA1 port A (`$1801`),
  8 data lines, wired-AND.
- C64 PC2 (a one-cycle low pulse after any read or write of `$DD01`) to drive
  VIA1 CB1. The drive sees it as the CB1 flag in `$180D`.
- Drive VIA1 CA2 to C64 FLAG2 and, as a level, to C64 PA2 (`$DD00` bit 2).
  The fast pair uses only the level: CA2 runs in the VIA's manual output mode
  and toggles as the request/acknowledge line.
- 8 KiB of drive RAM at `$8000-$9FFF`.

The stock 1541-II ROM sets VIA1 port A to all outputs at reset and would
drive the user port low, so firmware asserts the control bit only while the
fast pair is installed. CIA2 port A input is data-direction aware in the
shell so a read-modify-write of `$DD00` (VIC bank switching) cannot latch a
low strobe into the output register.

## Protocol

### Lines and states

The stock serial protocol is kept for every command byte under ATN and for
any device that does not answer the handshake. Fast mode is negotiated per
transaction and ends at the next ATN.

- Strobe C64 to drive: the PC2 pulse of a `$DD01` access (write of a data
  byte, or read of a received byte as its acknowledge).
- Strobe drive to C64: the CA2 level toggle (REQ). The C64 keeps the last seen
  level in the CIA2 shift register `$DD0C` (bit 2), which nothing else on a
  C64 uses, and waits for a change. A byte is valid or acknowledged on every
  toggle, so a missed transient cannot deadlock either side.
- Busy: the drive holds CLK low from the moment it accepts fast mode until it
  leaves it. The C64 treats "no toggle and CLK high" as "the drive stopped
  talking" and reports the stock status (`$02` read timeout, `$03` write
  timeout).
- EOI: the sender holds DATA low while a byte is valid; the receiver samples
  DATA before acknowledging.
- Mode flag on the C64: both CLK out and DATA out released (`$DD00` bits 4
  and 5 clear). A stock listener holds DATA, a stock talker holds CLK, so
  the flag is hardware state and no RAM byte can drift out of sync.
- On the drive: `$35` = mode (bit 7 expansion RAM present, bits 0-1 none,
  plain, stream), `$37` = shadow of PCR (`$0D` REQ low, `$0F` REQ high).

### Detection exchange (under ATN, after a secondary address)

1. The KERNAL sends the secondary address byte as stock. The drive
   acknowledges it (DATA low) and processes it.
2. The KERNAL sets PA2 to input, records the REQ level, and strobes PC2 once
   (`LDA $DD01`). It waits up to about 30 ms for a REQ toggle.
3. The drive checks the CB1 flag within about 800 us after the secondary
   address. No strobe: stock protocol. Strobe: it pulls CLK low, sets port A
   to output, writes a status byte (bit 7 expansion RAM present, bits 0-6
   resident engine version) and toggles REQ; the C64 reads it (acknowledge).
4. The C64 sends one command byte (port B output, PC2): `$00` plain, `$01`
   framed stream, `$02` engine upload (length low, length high, bytes, each
   acknowledged with a toggle). The drive answers every command with a
   response byte: for `$01`, `$01` if a framed stream will be served, else
   `$00`; for `$02`, `$01` when the image is in place. After an upload the
   C64 sends another command.
5. Both sides return to the stock flow: the C64 releases ATN, CLK and DATA
   (port B output for a LISTEN) and skips the stock turnaround; the drive
   waits for ATN release as stock.

Close secondary addresses (`$Ex`) skip the exchange. The exchange costs one
strobe and two bytes, about 0.2 ms per TALK or LISTEN.

### Plain mode (any channel, byte at a time)

- Drive to C64 (TALK): for each byte the drive sets DATA for EOI, writes the
  byte to port A, toggles REQ, waits for the C64's read strobe (CB1), clears
  the flag and fetches the next byte with the stock `$D3AA`. It leaves through
  the stock idle path when the channel is exhausted, or through the ATN
  service when ATN arrives. Directory listings, `INPUT#`, `GET#` and the error
  channel go this way.
- C64 to drive (LISTEN): the C64 writes the byte to port B (PC2), the drive
  reads DATA (EOI) and the byte, toggles REQ and stores it with the stock
  `$CFB7` (file data, filenames, command channel). The C64 repeats the same
  strobe every 2 ms until acknowledged: a strobe that arrives while the drive
  is still leaving its ATN service is cleared by the DOS's own port writes.
  SAVE, `PRINT#`, and OPEN filenames go this way.

### Framed stream (KERNAL LOAD)

The KERNAL's LOAD asks for a stream at its TKSA. If the drive answers `$01`,
the KERNAL runs its own receiver instead of the stock byte loop. Frames:

```
header byte: bit 7 raw (reserved, never sent), bit 6 last frame
count byte:  1..254 data bytes follow
```

The first frame carries the current byte of the channel plus the rest of the
DOS buffer (the two load address bytes come first as in stock). Then the
drive engine sends one frame per sector. The C64 stores bytes with an
unrolled loop that alternates on the expected REQ level (about 32 cycles per
byte on the C64 side). VERIFY uses a compare loop instead. STOP is polled at
every frame boundary and leaves through the stock break exit (`$F633`).

## KERNAL patch (`c64fast/kernal_fast.a`)

Space comes from retiring the datasette and RS-232 code: the board has
neither port. Every entry into the removed ranges is replaced by a byte-exact
stub (device 1 and 2 return DEVICE NOT PRESENT; CHRIN returns CR with ST bit 7;
CHROUT returns error 5; CLOSE drops the entry; NMI skips the RS-232 dispatch).
The KERNAL jump table, RAM vectors, messages, screen editor and IRQ are
untouched. Bare `LOAD`/`SAVE` (device 1) report DEVICE NOT PRESENT.

Hooks (all 3-byte JSR replacements of a stock JSR, or 5-byte replacements
padded with NOPs):

| Address | Stock | Hook |
|---|---|---|
| `$ED0E` | RS-232 idle wait before TALK/LISTEN | NOPs |
| `$ED19` | send pending byte under `$ED11` | `send_hook` |
| `$ED3D` | 1 ms delay under ATN | `atn_delay_hook`: port B input, PA2 output, then the delay |
| `$EDBB` | SECOND sends the SA | `second_hook`: SA, detection, fast LISTEN setup |
| `$EDC9` | TKSA sends the SA | `tksa_hook`: SA, detection, skip the turnaround |
| `$EDE7` | CIOUT sends the buffered byte | `send_hook` |
| `$EE13` | ACPTR entry | `acptr_hook`: fast receive or stock |
| `$F4D0` | LOAD's TKSA | `load_tksa_hook`: request a stream, run `fast_load` |

Code lives in the retired RS-232 block `$EEBB-$F0BC`, the RS-232 OPEN block
`$F409-$F49D`, and the tape block `$F72C-$FCD0`, which also carries the drive
engine image (1012 bytes). `$FB8E-$FB96` (SAVE's pointer copy) moved to
`$F65C` so the tape block is contiguous. Zero page used: `$97` (stream
request), `$9E`, `$9F`, `$A3-$A5`, `$B0-$B1`, `$B4-$B5`, all bytes the stock
serial code already clobbers or that belonged to tape and RS-232.

Behaviour differences worth knowing:

- A drive that stops mid-stream (read error) ends the LOAD with `?LOAD ERROR`
  (ST bit 1) instead of the stock behaviour of retrying until STOP.
- ACPTR and CIOUT return with interrupts enabled, as stock, but the fast
  paths do not disable interrupts while they wait. A program whose interrupt
  handler reads `$DD01` (user-port joystick polling) during a KERNAL disk
  call would acknowledge a byte it never took; nothing else on the board
  drives those pins.
- The tape buffer `$033C-$03FB` and the RS-232 variables are no longer
  touched by the KERNAL.

## DOS patch (`c64fast/dos_fast.a`)

Reclaimed: the power-on self test `$EAA7-$EB1E` (replaced by a plain RAM
clear, which also removes the ROM checksum that would reject any patched
image), its LED blinker `$EA6E-$EA9F`, the `&` utility loader `$E7A3-$E852`
(the `&` command now reports 31,SYNTAX ERROR), the unreachable stepper fast
path `$FA7B-$FABD`, the filler at `$C09F` and `$FF79`, and the NOP sled at
`$E780` that the ROM calls at the end of its initialisation. Everything else,
including format, validate, copy, rename, REL files and the job controller,
is stock.

| Address | Stock | Patch |
|---|---|---|
| `$EAA2` | `LDX #$FF` (VIA1 DDRA = outputs) | `LDX #$00`: port A inputs at reset |
| `$E780` | init hook | mode reset, PCR `$0F`, expansion RAM probe (`$8000` read-back) |
| `$E86D` | ATN service entry | drop fast mode, port A input |
| `$E8C5` | SA decode (8 bytes) | `fast_detect`, then the stock close test |
| `$E916` | TALK loop, first ATN check | `talk_hook`: plain send loop, or the engine |
| `$EA44` | LISTEN loop, byte receive | `listen_hook`: plain receive loop |
| `$FE90` | `&` in the command table | `$FF` |

Zero page `$35`, `$37`, `$3B`, `$3C` are bytes the stock DOS never references.
The drive keeps its stock behaviour toward a stock KERNAL: without the
detection strobe every path falls through to the original code.

## Drive engine (`c64fast/engine.a`)

The engine is uploaded by the KERNAL into the expansion RAM the first time a
stream is requested after a drive reset (about 40 ms), and stays resident.
The status byte carries its version so a newer KERNAL replaces it.

Expansion RAM map:

```
$8000 + s*256   raw GCR bytes 0..255 of sector s (21 page slots)
$9500 + s*69    raw GCR bytes 256..324 of sector s (21 tail slots)
$9AB0/$9AC5/$9ADA  present flags, link track, link sector per sector
$9AF0           engine (JMP, "MT", version), 1012 bytes
$9F00           21 x 8 raw header bytes
```

Per LOAD: the engine sends the rest of the current DOS buffer as the first
frame, takes the link, then for each track of the chain:

1. Issues a stock `$E0` execute job on the channel's buffer with a `JMP` stub
   in the buffer page and the header table set to the current track, so the
   ROM brings the motor up and spun without stepping.
2. In the job (interrupt context): steps to the target track itself (4 ms per
   half step, 20 ms settle), selects the zone bits and sector count, then
   reads every sector as it passes in one revolution: header sync, 8 raw
   header bytes, the sector number from two nibbles, data sync, 325 raw bytes
   into the slot. Sectors already stored are skipped; the loop ends when all
   sectors are present or after about 80 headers.
3. Back in the main context, follows the chain from the entry sector:
   validates the raw header (block id `$08`, checksum against the disk id the
   DOS holds, sector and track), extracts the link with the stock 5-to-4
   decoder `$F7E6`, decodes the sector in place with the stock block decoder
   `$F8E0` (the 69 tail bytes are copied to `$01BA` where that routine expects
   them), verifies the block id and checksum with `$F5E9`, and sends 254 data
   bytes (or the final count) as one frame straight from the slot.
4. A sector missing or failing verification triggers one more track read,
   then the stock error path `$E60A` with job status 2 (20,READ ERROR), which
   closes the channel and blinks the LED as the stock DOS does.

At the end the channel is marked exhausted like after the stock EOI byte, so
the KERNAL's CLOSE works unchanged. ATN is polled through `$7C` in every wait
loop, so a C64 that resets or breaks is served within one byte.

## Limits

- The GCR decode dominates (27 ms per sector). Options, in order of gain:
  a table-driven decoder in drive RAM (needs 1-2 KiB, only available if the
  engine moves into the DOS ROM, which means retiring the formatter), a
  Krill-style decode during the read, or raw frames decoded on the C64 with
  KERNAL tables (blocked by the in-place overlap of 325-byte raw blocks over
  254-byte decoded blocks; only one raw block can be pending).
- The transfer costs 57 cycles per byte round trip. Prefetching the next
  byte before waiting for the acknowledge would save about 8 cycles.
- SAVE is bound by the DOS write-and-verify job, 330 ms per block; a faster
  SAVE needs a drive-side write engine.
- Stepping uses conservative real-drive timing (28 ms per track).
- The selected pair is board-confirmed for the reported fast loading and
  clean Ultima II Player-disk use. This does not qualify every custom loader,
  replacement ROM pair, writable workload, or long-run timing interaction.

## Build and test

```
python c64fast/build.py            # writes c64fast/bin/{kernal,1541,engine}.bin and PROVENANCE.json
python c64fast/build.py --check    # rebuild and compare with the recorded local images
python -m unittest tests.test_c64fast
```

For the optional 202-block LOAD regression in PowerShell:

```powershell
$env:MULTITINI_SLOW_TESTS = '1'
python -m unittest tests.test_c64fast
```

The build needs ACME 0.97 on the PATH. It overlays the patch sections on the
stock images and refuses any changed byte outside the reclaimed regions
listed in `build.py`. Install by copying `c64fast/bin/kernal.bin` and
`c64fast/bin/1541.bin` to `0:/multitini/cores/c64/roms/fast/` and turning on
`Fast serial ROMs` in `Core config`.

`tests/test_c64fast.py` boots the pair on the simulator and covers the
directory, byte-exact LOAD with both secondary addresses, VERIFY with and
without a mismatch, FILE NOT FOUND, SAVE round trip decoded from the GCR
tracks, `INPUT#` from a SEQ file, the error channel, STOP during a LOAD, the
resident engine on a second LOAD, and the build reproducibility.

## Provenance

The images are derived works of the owner-supplied stock ROMs and keep their
`NOASSERTION` rights status (`licenses/files.csv`); the patch sources and the
build tool are GPL-3.0-only. `c64fast/bin/PROVENANCE.json` records the input
and output digests and every changed byte range.
