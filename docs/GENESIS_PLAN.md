# Genesis / Mega Drive integration

The owner requested the complete core integration on branch `genesis`, based on
`appletini-parity` at `84397f5`. This is the modern Nuked-MD machine from MiSTer
MegaDrive, not the older fpgagen/Genesis implementation. The source pin and
per-file grants are recorded in provenance and the import manifest.
The pinned commit is `083e1b1f4db8ffb32d6e8f83cf822fedc6184a61` from
`MiSTer-devel/MegaDrive_MiSTer`; 34 upstream files are retained verbatim.

## Architecture

- Keep the Nuked 68000, Z80, VDP, FM and PSG logic close to upstream.
- Use separate NTSC and PAL full-PL images. Region selects Japanese/export
  behavior within the selected clock variant. DVI retains nominal 60 Hz.
- Load cartridges from SD through the common byte stream. Accept raw and SMD
  formats, validate bounds before reset/load, and keep game data outside Git.
- Store cartridge ROM in board PSRAM, behind a cache and sequential prefetch.
  A dedicated clock buffer holds the chip model on an unresolved ROM demand;
  the memory service and output clocks continue. Never substitute stale data.
  Physical throughput and wait counts must be measured before acceptance.
- Keep timing-bound chip RAM in BRAM. Use DDR for complete source video fields,
  avoiding four full RGB565 frames in the Zynq-7020's limited BRAM. Two small
  line stores normalize H32/H40 fields into 320x240, including an eight-pixel
  blank left column; interlace uses field bob.
  The shared display/effects pipeline then operates at its existing cadence.
- Use two three/six-button pads, shared controller mapping and keyboard fallback.
- Keep cartridge SRAM/EEPROM saves behind the existing generation-tagged media
  protocol and atomic file replacement. Flush before eject, reset or core load.
- Reuse the menu, runtime, audio output, output modes, effects and recovery.

Master System FM has a noncommercial helper and is excluded. SMS compatibility,
Sega CD, 32X, MD+ streaming, lightguns and multitaps are separate products or
peripherals, outside this Genesis integration. The source audit records any
further cartridge-chip limitations explicitly; unsupported images must fail
clearly instead of silently using incorrect hardware.

## Validation

First test content parsing, saves/input, loader transactions, cache coherence,
source-field capture, DDR ownership, and reset/drain behavior. Compile and run
the real chip RTL with an original test cartridge, without commercial ROMs.
Run the existing host and shared HDL regressions after integration.

Local simulation does not establish board support. The final Vivado/Vitis
build must close timing and CDC/PSRAM gates, fit the target part, and report the
achieved clocks. Board acceptance requires cartridge boot, audio, both pads,
SRAM/EEPROM reload, all display modes and effects, region/clock variants,
interlace, reset/core switching and sustained output without processing drops.
Publish matching source and build inputs with any public image.

### Local results, 2026-09-30

- All 56 host tests pass with AddressSanitizer and UndefinedBehaviorSanitizer.
- All 149 applicable Python tests pass. The two existing C64 simulation
  modules need `py65` and ROM/generated fixtures unavailable in this checkout;
  they are excluded from that count. This is not a full `make check` result.
- Complete-chip lint and both NTSC/PAL shell elaborations pass, with no
  combinational-loop waivers. Generated interfaces and ports match their
  scripts; exact import hashes and per-file licenses pass the source audit.
- All 13 Genesis HDL tests pass against unchanged input hashes: loader,
  ROM cache, save state machine, tagged media stream, controllers, EEPROM,
  cartridge mapping, RAM, video normalization, DDR video bridge, actual chip,
  full adapter and VDP bus-latch portability.
- The original test cartridge executes the real 68000 and Z80, writes SRAM,
  produces PSG audio and two complete H40/V224 VDP fields, and survives
  injected ROM stalls. Directed normalizer tests cover the other raster sizes.
  The adapter fixture loads configuration, cartridge and save bytes through
  production interfaces, then checks every byte of two 64 KiB save snapshots
  across a failed write and retry.
- Shared display regressions pass for all six output modes and all five shell
  machine selections, with the existing C64/GBC/Vectrex bridges and effects.
- A PAL build-input bundle captures and verifies 129 exact source inputs,
  including the imported originals, license texts and port/proof scripts.

Vivado, Vitis and the ARM cross compiler are unavailable on this development
host. No bitstream or target firmware binary is claimed. Resource use, timing,
vendor RAM inference and physical behavior remain gates. In particular, the
upstream wide VRAM arrays can consume substantially more BRAM than their byte
capacity suggests; inspect the inferred primitive count before accepting fit.

### Portability and clock review

The chip model uses a dedicated BUFGCE at nominal 107.386 MHz (NTSC) or
106.406 MHz (PAL). ROM misses and coherent save snapshots stop the whole chip;
the parent-domain cache, loader and save transport continue. The audio
conditioner uses a globally buffered, related divide-by-two clock. Constraints
and build checks require both clock paths and the exact division ratio.
Source capture crosses to the 133 MHz control domain and the independent DVI
pixel domain through the shared mailbox/FIFO mechanisms. HP0, HP1 and HP2 must
drain before an output clock change or PL reload.

The upstream VDP bus latch cells 12, 13 and 15 contain feedback with a
self-clearing pulse race. Their portable replacement retains all 672 stable
transitions in the exhaustive 11-state model and explicitly resolves 32
input/state cases with no Boolean fixed point as a sampled clear pulse.
`scripts/test_genesis_vdp_pair.py` enumerates those cases and checks 2,752 RTL
steps. This is a deliberate resolution of undefined combinational behavior,
not an equivalence claim for the pulse race. Original source remains intact;
the port generator checks the exact transformation. Board compatibility of
that pulse behavior is part of acceptance. The full-chip fixture records zero
such pulse cases; the exhaustive helper test supplies their directed coverage.

## Build and SD layout

Create each project in a separate output directory (Vivado/Vitis 2025.2):

```sh
MULTITINI_VIVADO_DIR=build/genesis-ntsc MULTITINI_MACHINE=GENESIS MULTITINI_GENESIS_PAL=0 \
  vivado -mode batch -source scripts/create_project.tcl
MULTITINI_VIVADO_DIR=build/genesis-ntsc \
  vivado -mode batch -source scripts/build_and_export_xsa.tcl
```

Repeat with `MULTITINI_GENESIS_PAL=1` and
`MULTITINI_VIVADO_DIR=build/genesis-pal` for PAL. Package NTSC first and PAL second:
variant indices 0/1 are part of the firmware contract. Use
`scripts/make_core_package.py --id genesis --name "Genesis / Mega Drive"
--core-id 0x4D442020 --content-root /multitini/content/genesis`
with `--variant "ntsc|NTSC|PATH_TO_NTSC.bit"`,
`--variant "pal|PAL|PATH_TO_PAL.bit"`, and an output directory under
`multitini/cores/genesis`. The shared packaging tool records sizes and hashes.
Build the matching supervisor firmware with the new Genesis sources; do not
promote this branch into the accepted release until the board gates pass.

Put user-owned `.md`, `.gen`, `.bin`, or `.smd` cartridges in
`/multitini/content/genesis`. Raw word-swapped dumps are decoded automatically;
SMD input has a 512-byte header and complete 16 KiB interleaved blocks.
ROM size is 512 bytes through 8 MiB. Checksum mismatches are advisory because
patched/homebrew cartridges often retain an old header checksum.

Each cartridge has a fixed 64 KiB `.sav` image. This includes EEPROM-only
boards whose headers do not advertise SRAM. Missing saves use erased bytes or
the cartridge-specific zero-fill quirk;
loading a cartridge stages configuration, ROM and save data while reset is
held, then mounts the save generation before starting the machine. Failed
save writes preserve dirty state and block destructive transitions until retry.

Two controller ports retain their assignments through menu capture. The menu
selects three- or six-button protocol independently for each port. Keyboard
fallback uses arrows, Z/X/C for A/B/C, A/S/D for X/Y/Z, Enter for Start, and
right Shift for Mode. Normal controller mapping remains shared with other cores.
Settings schema 7 preserves earlier profiles and stores each Genesis profile's
two controller protocol choices.

The source DDR ring occupies `0x23000000..0x233fffff` on HP2. It is separate
from CPU0 (below `0x20000000`), CPU1 (`0x20000000..0x20ffffff`), shared control
(`0x21000000`), and the existing video/effects pools (`0x22000000..0x229fffff`).
All three video AXI ports drain before an output clock change. A stopped or
malformed source field is discarded, and a displayed source slot remains
owned until its replacement is acknowledged.

SVP cartridges (Virtua Racing) and Sega Channel images are rejected by their
known cartridge identifiers. The upstream SVP and EEPROM helper grants were
not established by the import audit. The base mapper is ported from its
explicitly licensed source; EEPROM uses original portable helpers instead.
The noncommercial Master System FM helper is neither imported nor built.
