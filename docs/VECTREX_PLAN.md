# Vectrex and Vectrex32 implementation plan

Status: FPGA vendor gates, host/simulation checks and final packaging pass on
branch `vectrex`. The owner accepted the VEC3 update after physical testing on
2026-09-15; see `VIDEO_TEST_BUILD.md` for the exact package and reported scope.

## Acceptance target

The owner requested a fresh Vectrex machine implementation using existing chip
implementations, plus Vectrex32 support. Completion means stable, playable
Vectrex games and the owner's Vecalabeth on Multitini One, with correct controls,
geometry, speed, audio, loading, reset, and return to the common menu. A build,
title screen, or interpreter accepting source is not the gameplay gate.

The initial compatibility set is Mine Storm, Berzerk, Scramble, Armor Attack,
Clean Sweep, Spike, and Star Castle. Record exact ROM hashes and test routes;
add differences discovered through tests rather than game-name workarounds.
Vecalabeth is supplied in an external game directory containing
`vec.bas` and twelve vector-data files. Keep this user content outside tracked
source. Host tools must accept its path and record input hashes.

## Architecture and ownership

- Reuse individually pinned, licensed 6809, 6522, and AY-compatible chip code.
  Port the latter two from Appletini ONE; do not change that sibling repository.
- Write the Vectrex memory map, cartridge bus, analogue model, beam capture,
  renderer, and Multitini integration in this repository. Do not import the
  educational-only Dar/MiSTer machine or renderer implementation.
- The FPGA runs the 6809/VIA/PSG at their native cadence. Its fixed-point beam
  model retains DAC changes, sample-and-hold, reference offset, ramp, zeroing,
  blanking, and time. Monitor refresh must not change machine execution rate.
- Use deterministic BRAM for machine memories and initial monochrome intensity
  storage. Prove BRAM/port fit before fixing the output resolution. Stream
  optional overlays from external memory with bounded buffering.
- Run MIT GS BASIC natively on ARM, retaining its language/graphics semantics.
  Replace PiTrex-specific rendering, input, clock, console, and file hooks.
  Vector commands and PSG writes feed FPGA services; interpreter execution must
  yield sufficiently often to preserve USB/menu/storage service deadlines.
- Full software-facing Vectrex32 support adds writable 2 KiB cartridge RAM,
  instruction generation/refill/notification, readback and low-level functions.
  Original PIC32 firmware execution is a distinct MCU-emulation mechanism and
  is not required to run the native BASIC API. Do not call unsupported APIs
  successful. Track every API and compatibility difference explicitly.
- Existing full-PL loading, package validation, text UI, input, audio, recovery,
  and settings remain common services. Add a Vectrex core profile and media
  types; never put Vectrex logic into the Apple bus path.

## Source and content rules

Every imported file needs its source URL/path, commit, original SHA-256, license,
and description of changes. Keep license texts. Import only the needed GS BASIC
subtree and audited dependencies, not a whole PiTrex firmware image. Games and
BIOS use the repository content policy and exact provenance; supplied local
games are test inputs, not automatically redistributable assets.

## Work order and gates

1. **Chip and runtime foundations.** Verify 6809 execution/interrupt bus phases,
   VIA PB7 one-shot and continuous output, shift-register bit ordering and CB2
   timing, CA2 zero control, PSG sound/register/port-A behavior. Build GS BASIC
   with portable callbacks and execute Vecalabeth beyond parsing. Preserve
   targeted tests and per-file provenance.
2. **Machine and rendering.** Implement mirrored RAM/ROM and cartridge decoding,
   controller comparator, DAC audio, beam evolution and intensity rendering.
   Test tiny original programs and directed beam stimuli. Synthesize the actual
   selected architecture on XC7Z020 early; record BRAM/LUT/DSP use and clocks.
3. **Ordinary game execution.** Add a deterministic simulation harness that
   loads external BIOS/cartridges, replays input and captures frames/audio/bus
   diagnostics. Check gameplay, vector joins, dots, text, diagonal motion,
   sampled audio, repeated reset, and sustained frame delivery.
4. **Vectrex32 execution.** Integrate GS BASIC file loading, 60 Hz frame pacing,
   vector/text primitives, controls and errors. For Vecalabeth verify title,
   character creation, overworld movement, entering/exiting a dungeon, combat,
   food/health changes and progression. Record reproducible input scripts and
   visual captures. Implement and test the remaining Vectrex32 API, including
   generated 6809 code and shared RAM; audit the 1.27-to-1.29 version gap.
5. **Shared runtime and vendor build.** Add VECTREX project/package selection,
   content validation, file browser, controls, pause/resume, reset and safe
   core switching. Run existing affected regressions and new tests. Complete
   Vivado timing/CDC/DRC/routing and Vitis builds; package exact selected inputs.
6. **Board acceptance.** Install the reviewable test package with recovery,
   verify exact build IDs, and play the named games and Vecalabeth. Check sound,
   analog/digital controls, disconnect/reconnect, pause, core changes, warm/cold
   starts and a sustained run. Distinguish model/build results from observed
   board behavior. Missing board access remains an explicit unmet gate.

## Current evidence

- Branch created from a clean `main` checkout.
- Vecalabeth uses high-level sprites/text/geometry, 60 Hz frame waits, digital
  input and game-relative text-file reads. Its inspected source contains no
  CodeSprite/Peek/Poke/JSR calls, sound writes, or save-file writes.
- The imported 6809 passes 24/96 MHz wrapper tests, instruction timing,
  synchronous reads, IRQ/FIRQ/NMI, and pause. Adapted VIA/AY tests pass timer,
  shift-register, control-pin, tone, envelope, and controller-port checks.
- Beam/raster tests pass. The DVI scaler compares 1,749,600 output pixels across
  two frames, including repeated rows/columns, margins, and frame restart.
- The machine transport passes 164 directed checks, including malformed frames,
  abort epochs, 64 KiB banking, pending line/PSG cancellation under reset,
  reset-held BIOS loading, and discarding queued input before fresh reassertion.
- Game simulations have reached observed Berzerk, Armor Attack, Scramble,
  Spike, Star Castle, Clean Sweep, and Mine Storm gameplay without renderer
  overflow. Mine Storm's 14-second run reaches MINE FIELD 1 after its launch
  animation. Berzerk's replay reaches score 50. The seven 14-second replays are
  recorded in `build/vectrex-games/renderer-pipeline-final/report.json`.
  Final reset/input/RAM changes were replayed for Mine Storm and Berzerk in
  `reset-ram-final/report.json`: all CPU/game counters, both statistics files,
  all 30 captured image hashes, and both WAV hashes match the earlier run.
  These short replays do not establish complete playthroughs or board behavior.
- External Vecalabeth runs through setup, food purchases, overworld movement,
  dungeon generation/navigation, attacking a Skeleton, and dungeon exit. The
  Windows host replay with the external BIOS font reports 1,891 frames,
  126,463 statements, 689,792 lines, five asset opens, and no runtime error.
  All thirteen game files and the BIOS remain unchanged. The same gameplay
  gates pass under Linux AddressSanitizer, UndefinedBehaviorSanitizer and leak
  checks; libc random sequences make its map/counters differ. This is interpreter/backend
  evidence, not proof of ARM frame deadlines or physical controls.
- The final V5 FPGA build in `build/vivado-vectrex-v5` passes the Vivado 2025.2
  timing, CDC, DRC, routing, bus-skew, and timing-coverage gates on
  `xc7z020clg484-2`. Setup WNS is +0.103 ns, hold WHS +0.047 ns, and pulse-width
  slack +1.833 ns; all failing-endpoint counts are zero. It uses 6,233 LUTs,
  3,781 registers, 124 of 140 BRAM tiles, and seven DSPs. CDC reports only
  informational synchronized crossings. DRC/methodology warnings remain;
  this is not a warning-free build. See [the acceptance record](VECTREX_ACCEPTANCE.md)
  for exact reports and scope.
- V5 exports a bitstream and XSA. Its `source-inputs.json` records 41 SHA-256
  source/settings hashes, all matching the reviewed files. The build manifest
  records baseline `2ede66e487dfa403799929f708acaf1563b33def` with `dirty=1`;
  the baseline commit alone does not identify this implementation.
- All 75 function names in the Vectrex32 1.29 manual have native implementations.
  [The API coverage record](VECTREX32_API.md) states the rendering, timing,
  software-6809/shared-RAM, terminal, filesystem, and peripheral limitations.
  `Version()` remains 127 for the imported language lineage. API coverage is
  not original PIC32 firmware execution or full SmartCart compatibility.
- The final Vitis application and fresh FSBL build from the accepted V5 XSA in
  `build/v32`. The application uses C++17, exceptions/RTTI, constructor startup,
  a 64 MiB heap and a 2 MiB stack. These are engineering budgets, not measured
  peak use. The ELF reports 827,303 bytes text, 4,380 data and 90,714,480 BSS.
  All 32 host tests pass, including existing C64/GBC services and both new
  Vectrex32 test targets. The final runtime also passes ASan/UBSan unit tests.
- The [test bundle](VECTREX_TEST_BUILD.md) contains the selected supervisor,
  native core and application with source and build evidence. Its byte audit
  verifies every initialized FSBL/application ELF load segment, addresses,
  processed FPGA payloads and core manifest. No Vectrex BIOS/game is bundled.
- Sustained gameplay, sound review, measured ARM frame timing and physical-board
  acceptance remain open gates. No serial board was visible at packaging time.

## References

- Appletini chip source baseline: `31a5d7073e61588afe834ff1bb80c06750e1a613`.
- GS BASIC source baseline: `malbanGit/pitrex-baremetal`, commit
  `457c5a98f914a812c9341c2949e9d57473ccac4a`, `gsBasic/` (MIT).
- Vectrex32 1.29 manual:
  <https://vectrex32.com/wp-content/uploads/2023/07/Vectrex32.pdf>.
- Vectrex machine behavior references include original schematics/manuals and
  BSD-licensed MAME device behavior; record any adapted code and license.
