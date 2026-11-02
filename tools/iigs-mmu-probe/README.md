# Original IIgs MMU hardware probe

`MMUPROBE` is a native GS/OS S16 application for an original ROM 01 or ROM 03
Apple IIgs. It records observations instead of checking the machine against an
emulator. The final, stationary 640-mode SHR screen carries the measurements,
not just their hashes. One photograph or screenshot is the requested result.

**This is a takeover diagnostic. Save work first. It overwrites RAM, including
RAM disks, and requires a reboot after the photograph.** It performs no disk
writes and does not attempt to return to the interrupted operating system.
Use stock CPU speed; disable ZipGS/TransWarp and other accelerators first.

The delivery disk is a ProDOS **data** volume, without any Apple operating
system, ROM, or boot code. Mount it after booting the tester's own GS/OS and
double-click `MMUPROBE` in Finder. See [tester instructions](TESTERS.txt).

## Coverage

The fixed schedule has 466 profiles and 45,824 scheduled cases. Unwritable
high-bank pairs are skipped and identified in the result. Counts below exclude
the writable-bank survey and the supplemental raw observations.

| Section | Cases | Recorded observations |
| --- | ---: | --- |
| Shadow | 35,840, each repeated with complementary markers | All 128 combinations of SHADOW bits 0–6; 14 video boundaries; sources 00, 01, 02, 03, 0E, 0F, 7E, 7F; bank-00 and bank-02 RAMWRT variants; SPEED.4 off/on. Four destination bits per case. |
| Auxiliary selection | 4,096 | All 128 combinations of RAMRD, RAMWRT, ALTZP, PAGE2, 80STORE, HIRES and NEWVIDEO.0; eight addresses; logical banks 00, 02, E0 and E1. Read source and four write destinations. |
| Language-card access pairs | 4,096 | Every ordered pair of the 16 LC addresses, each read or written, through all four I/O aliases: 32 × 32 × 4. Observed RDROM, bank selection and effective write enable. |
| Language-card windows | 512 | All 32 LC address/access choices, two accesses each, with both ALTZP values, IOLC inhibit off/on, and logical banks 00, 01, E0, E1. D/E read source and effective D/E write enable. |
| Register readback | 1,280 | Every written byte for C029, C035, C036, C068 and C02D. The entire returned byte is retained. |

The 544-byte supplemental section also contains:

- Four ROM-bank samples for each of FC, FD, FE and FF, without hard-coded
  expected ROM contents.
- Reads at eight consecutive addresses in each of banks 80, 81, 82 and DF. These are samples
  of a potentially changing bus, not a claim that floating bus has one value.
- C800/D800 observations after independently seeding both LC banks, with
  IOLC inhibited, logical banks 00/01, and ALTZP off/on. Each pair is repeated
  after changing the LC bank through live E0 I/O.
- Higher-bank I/O tests through 00, 01, 02, 03, 0E, 0F, 7E and 7F, with
  SPEED.4 off/on: whether a C010 write reaches RAM, and whether C005 changes
  the live C068 register. This directly addresses upstream `TEST12`.
- Text-page-2 probes with SPEED.6 set, through banks 00–03, varying SPEED.4
  and SHADOW.5. These complement the main sweep's SPEED.6-clear observations.
- 64 C800 ownership sequences: INTCXROM, C00A/B and C02D.3 combinations;
  no trigger, C3 read, C3 write, C1 read, C7 read, C3 followed by C1, and C3
  followed by a CFFF read/write. Each captures C800/C801, the CFFF byte before
  release, then C800/C801 again. Other slots are selected internal.
- The first 16 unexpected-value/repeat-disagreement records and a total count.
  Overflow does not turn an incomplete measurement into a clean result.

SHADOW.7 is included in the full-byte register sweep; the address-effect matrix
varies the seven meaningful inhibit bits. High-bank sources are representative
bank pairs, not every physical bank. The writable-bank survey uses two marker
polarities at B400 across 00–7F. Bank 05 holds the program and is reserved.
An alias into its probe guard is flagged and excluded before testing code
addresses. This survey establishes writable echoes, not a RAM-card capacity
or aliasing certification.

## What the screen proves

The native code transposes observed destination/read-class bits into 128-bit
tables and deduplicates identical tables. No candidate MMU model selects those
tables. The decoder reconstructs every table entry and writes JSON and CSV.

The baseline uses NEWVIDEO=01, SHADOW=7F, SPEED=80, STATEREG=08, 80STORE off
and HIRES off to seed separate fast-even, fast-odd, E0 and E1 markers. Destination
bits mean a location contains the newly written marker. Other-than-marker or
original-sentinel values are exceptions; they are not silently classified as
ordinary mapping results. Complementary shadow passes must agree.

An observation with a nonzero status still needs analysis. In particular, if
unexpected values exceed the 16 raw exception slots, the broad sweep identifies
the affected region but a follow-up capture is required for its full byte data.
The program never reports such a run as clean.

This is a bounded architectural probe, not exhaustive proof of an entire MMU.
It does not test reset/power-on state, vector pulls, DMA, cycle-exact floating
bus, every expansion card, or every possible access history. Entry register
values are **GS/OS launch state**, not cold-reset values. ROM 01 and ROM 03 must
each be tested on their own physical machine. Emulator observations validate
the probe and transport; they do not settle hardware disagreements.

## Runtime and machine ownership

The S16 file is one 64 KiB, bank-aligned, private OMF 2.0 code segment marked
"no special memory". The entry stub copies the whole segment to bank 05 before
testing. It then masks CPU/video interrupts, uses a private native stack at
00:BFFF, and executes without OS or ROM calls. No MMU experiment calls a
subroutine while RAMWRT/ALTZP could change the stack. Experimental state lives
above A000 in the odd execution bank, outside the shadow-video ranges.

The only static writes to I/O are documented MMU/video switches, video interrupt
masks, and the controlled slot-ROM probes. No disk-controller accesses,
disk-service calls, PRAM writes, firmware updates, or card-ROM execution occur.
The video bank is selected before drawing so its addressing stays consistent.

A watchdog checks C019 edges between cases and during compression. At 2,700
edges it marks the result partial and stops scheduling profiles; this is
54 seconds at 50 Hz or 45 seconds at 60 Hz, before final transport rendering.
It is a guard, not a claim of measured original-board runtime. The current
software-validation receipt gives the observed sweep time and rendering cost.
The aim is a complete run in less than one minute at stock speed.

## Build

Software verification on 2026-11-02 completed all 45,824 cases under both ROM
profiles in GSSquared, with zero unexpected bytes or complementary-pass
disagreements. ROM 01 used 2,573 VBL edges and ROM 03 used 2,581, approximately
43 seconds at 60 Hz, before final packet generation and rendering. The measured
ROM 01 generation/rendering tail was 3,968,285 emulated CPU cycles. This supports
the under-one-minute stock-speed target; original-board runtime is still open.

The final ROM 01 window screenshot decoded exactly to the captured packet.
Eight host checks cover the transport, damage rejection/repair, an oblique
synthetic photograph, OMF records, and the ProDOS volume. `validation.json` in
the tester ZIP records the exact tested payload and separate evidence gates.
CPU entry was injected into an aligned emulated load segment: **actual GS/OS
Finder loading has not been verified**, and original-hardware acceptance is
pending. A file-format check is not a substitute for either gate.

Python 3.10 or newer and the cc65 assembler/linker are required. No external source tree,
system ROM, GS/OS image, assembler macros, or disk template is required.

```text
python tools/iigs-mmu-probe/build.py --cc65-bin D:/Repos/cc65/bin
python tools/iigs-mmu-probe/test_probe.py
```

The default output is `build/iigs-mmu-probe/`:

- `IIGS-MMU-PROBE-v1.zip`: disk, type-preserving loose executable, source,
  build scripts, decoder, instructions and GPL license.
- `MMUPROBE.po`: 800 KiB ProDOS data disk. `MMUPROBE` has type B3, aux 0000.
- `MMUPROBE#B30000`: the same native executable with a preservation filename
  for compatible host-to-ProDOS importers.
- `manifest.json`: source and output SHA-256 hashes.

The supplied disk requires the tester's own GS/OS boot volume. Simply renaming
a host file does not set its ProDOS file type; the disk image already has the
correct directory metadata. Copy the application from that disk in Finder.

## Decode a tester's image

The decoder needs Python 3.10 or newer and Pillow (`python -m pip install Pillow`). A precise
640×200 or integer-scaled active-picture capture can be decoded directly:

```text
python tools/iigs-mmu-probe/decode.py result.png --output decoded-rom1
```

For an emulator window or a photograph cropped to the encoded rectangle,
excluding the heading, specify its approximate pixel bounds. The decoder fits
small alignment errors using only the fixed sync bars and row numbers.

```text
python tools/iigs-mmu-probe/decode.py result.png --code-crop X Y WIDTH HEIGHT --output decoded-rom1
```

For an oblique photograph, `--corners` takes eight coordinates for the **whole
active picture**, including its eight-pixel heading, ordered TL, TR, BR, BL.
The image must resolve individual cells; a blurry image must be retaken.

Outputs are `measurement.bin`, `measurement.json` and `observations.csv`.
The data rect is 320×96 binary cells, each 2×2 logical pixels in 640-mode SHR.
Each row contains A5 5A sync, its row number, 34 payload bytes, a little-endian
CRC16-CCITT (poly 1021, initial FFFF), then A5 sync. A global IEEE CRC32 covers
the length-delimited packet. One erroneous data/index/CRC bit per row can be
repaired when its syndrome is unique; the whole packet must still pass CRC32.
The decoder rejects corrupted captures instead of returning plausible values.

## Result schema

Header offsets and integers are little-endian. Index tables are followed by
the exact observed 16-byte bitmaps, the 544-byte supplemental section, and CRC32.

| Header offset | Meaning |
| --- | --- |
| 00–04 | `MMU1`, format version 1 |
| 05 | Status bits: 01 time limit; 02 dictionary limit; 04 unexpected bytes; 08 complementary-pass disagreement; 10 alias into execution-bank guard |
| 06–07 | ROM revision, launch bank |
| 08–0F | Packet length, index count (1872), dictionary count, supplemental length |
| 10–17 | Scheduled case count, VBL edge count, unexpected/repeat count |
| 18–1C | C029/C035/C036/C068/C02D at application entry |
| 20–2F | Writable-bank bitset, low bit first |
| 30–31 | Skipped high-bank shadow cases |

Profile order is defined in [protocol.py](protocol.py), with cases ascending.
Each profile contributes one dictionary index for each result bit, least
significant bit first. Bitmap bit `case & 7` in byte `case >> 3` is that result
bit. Unused positions in 32-case profiles are zero.

- Shadow bits 0–3: marker in fast even, fast odd, E0, E1.
- Aux bits 0–3: the same marker destinations; bits 4–5: read source, numbered
  0 fast even, 1 fast odd, 2 E0, 3 E1. Case bits: RAMRD, RAMWRT, ALTZP, PAGE2,
  80STORE, HIRES, NEWVIDEO.0, in that order.
- LC-pair bit 0: either LC bank accepted the write; bit 1: C068.2; bit 2:
  C068.3. Access index 0–15 reads C080–C08F; 16–31 writes them. Profile's
  `first` access precedes the case's second access.
- LC-window bits 0–1: D read class 0 marker19, 1 marker2B, 2 matching FF:D800
  ROM byte, 3 unexpected. Bits 2–3: E read class 0 marker3D, 1 matching FF:E800
  ROM byte, 3 unexpected. Bits 4/5: D/E accepted the write. A case's switch is
  accessed twice, with access indexes as above.
- Register bits 0–7: exact returned register byte; written value is
  `case | (half << 7)`.

Supplement offsets: 0 ROM samples (16 bytes); 16 absent-bank reads (32);
48 LC-flat data (16); 64 extended-I/O data (32); 96 text-page-2/SPEED.6 data (32);
128 C800 data (320); 448 exception records (96). An exception record is profile word, case byte,
location byte, observed-value byte, phase byte. Locations 0–3 are the four
physical stores; FD is aux-read classification; FE is complementary-result
disagreement; E0/E1 are LC-pair bank readbacks; E2/E3 are LC-window reads.

## Reference basis

The probe is original GPL-3.0-only source, without imported MiSTer test code.
The disputed cases were identified by inspecting MiSTer
[`MMU_TEST.S` at 9adbba86](https://github.com/MiSTer-devel/Apple-IIgs_MiSTer/blob/9adbba8622f378253765b7d438b6cbcc4d03fc57/customtests/MMU_TEST.S).
The measurement protocol makes no assumption that its assertions are correct.

- [Apple IIgs Hardware Reference](https://downloads.reactivemicro.com/Apple%20II%20Items/Documentation/Manuals/IIgs/IIgs%20Hardware%20Reference.pdf): switch and register addresses.
- [Apple Miscellaneous Technical Note 7](https://mirrors.apple2.org.za/apple.cabi.net/FAQs.and.INFO/A2.TECH.NOTES.ETC/A2.CLASSIC.TNTS/a2misc007%281%29.htm): the FE1F identification routine returns the ROM revision in Y. The probe reads FF:FB59, the byte that the ROM routine loads into Y, without calling firmware after takeover.
- [Apple IIgs Technical Note 52](https://apple2.gs/technotes/tn-iigs-052/): no-special-memory segment policy.
- [OMF documentation](https://ciderpress2.com/formatdoc/OMF-notes.html) and [SourceGen's OMF parser](https://github.com/fadden/6502bench/blob/master/SourceGen/Tools/Omf/OmfSegment.cs): load-segment headers and attribute bits.

The outstanding acceptance gate is the tester's original ROM 01 and ROM 03
captures. Native GS/OS loader verification, CPU-execution validation, screenshot
transport validation and physical hardware acceptance are separate results.
