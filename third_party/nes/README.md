# MiSTer NES machine import

Source: <https://github.com/MiSTer-devel/NES_MiSTer>

Pinned commit: `49a0a662e244469ca77b2155746a066df704ffae`.

The exact 20 imported originals and SHA-256 hashes are in `import.json`.
`upstream/` preserves their original bytes. The inherited project license is
GPL-3.0-only (`upstream/COPYING`). The four T65 files carry their own BSD
three-clause notices, including the non-endorsement condition; preserve
`T65-NOTICE.txt` with binary distributions. The repository's per-file license
ledger records both classes.

The machine originates in Ludvig Strigeus's FPGANES. The MiSTer contributors
added and maintained the PPU, APU, controller, mapper and other machine work.
The retained T65 names its original authors in each source header.

Run `python scripts/audit_nes_import.py --source build/nes-upstream` to
compare a clean checkout at the pin. Run `python scripts/port_nes_core.py
--check` to verify every generated source port. The port generator preserves
the CPU, PPU, APU and selected mapper algorithms. Its changes are:

- Add a common master clock enable to every machine sequential block and
  qualify the native T65 enable. All machine state stays in one service clock
  domain; memory pressure never creates a fabric clock.
- Reset the PAL phase counter explicitly and qualify PAL tick suppression by
  system type, so the fixed NTSC wrapper retains exactly twelve master ticks
  per CPU cycle from a deterministic cold start.
- Replace the internal mapper tri-state bus with separately driven outputs
  and an explicit mux for mappers 0, 1, 2, 3, 4, 7, 9, 10, 11, 66 and 71.
- Make declarations, parameter widths, optional inputs and procedural output
  types explicit for Vivado. Change missing combinational sensitivity lists
  to `always @*`.
- Clear pending PPU `$2007` transport on reset and suppress memory requests
  during internal reset, so the stalled memory boundary starts deterministically.
- Honor mapper 2 AND bus conflicts when the canonical header selects that
  board variant. The existing ROM-read and CPU-data AND path handles the write
  after any cache miss has completed.
- Remove MiSTer save-state/rewind transport and the unrouted analog RCA output
  stage. The original state-register defaults still implement unit reset.

`multitini_nes_adapter` implements complete canonical iNES NTSC cartridge loads,
shared controller input, RGB video, mono APU sound on both channels, local
native RAM, PSRAM cartridge ROM caching, and generation-checked battery saves.
The 42.954544 MHz service clock supplies a 21.477272 MHz master tick. A bounded
tick scheduler repays ROM stalls with coherent machine enables; pause and save
barriers stop time without accruing catch-up work. Eight byte lanes provide
the local 8 KiB save RAM's native-byte and snapshot-word views.
Firmware translates supported NES 2.0 NTSC/multiregion headers into that
canonical format without altering cartridge payloads. Mapper 2 submapper 2
retains its AND bus conflicts through iNES byte 10 bit 5. The HDL loader accepts
that flag only for mapper 2; its other header, mapper and size checks remain.
Raw NES 2.0 transport, FDS, NSF, Vs., PAL/Dendy, trainers, four-screen boards,
save states and special peripherals are outside this port.

No games, system ROMs, FDS BIOS, fonts, logos, palette files, MiSTer platform
code or prebuilt FPGA images are imported. The fixed 64-entry RGB palette is
the GPL-covered Kitrinx palette from the pinned `rtl/video.sv`, retained in
the adapter. The simulation cartridge is original 6502 test code generated
inside its testbench.

Mixed-language simulation compiles the real T65 VHDL and the selected PPU,
APU and mapper sources. Implementation, timing and physical board results
must be reported separately; importing this source does not establish board
support or broad game compatibility.
