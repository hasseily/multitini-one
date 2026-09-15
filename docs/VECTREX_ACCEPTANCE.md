# Vectrex acceptance record

This branch adds a new machine and a native GS BASIC runtime. An FPGA build,
simulation replay, and physical play test are separate acceptance gates.
The requested final result requires the physical play test as well.

## Reproducible evidence

| Check | Evidence | Scope |
| --- | --- | --- |
| 6809 chip | `scripts/test_vectrex_cpu.py` | Synchronous bus, instruction cycles, interrupts, stack and pause at both clock ratios |
| VIA and AY chips | `scripts/run_vectrex_chip_tests.py` | Timer, serial, control-pin, tone, envelope and controller-port tests |
| Analogue and raster model | `scripts/run_vectrex_video_tests.py` | DAC delay, sample/hold, beam, clipping, queue pressure and phosphor decay |
| DVI scanout | `scripts/test_vectrex_rendering.py` | Every portrait pixel over two frames, including scale phases and borders |
| Machine transport | `scripts/test_vectrex_transport.py` | 164 checks; actual CPU/chips, framing, bank switch, abort epochs, input, pause and reset-held loading |
| Board services | CMake `vectrex_firmware`, `sd_storage`, `vectrex32_runtime` tests | Real BASIC and board adapter with mocked MMIO, clock and storage |
| Vecalabeth | `scripts/verify_vecalabeth.py` with the external game directory | Title, character setup, food purchases, overworld, dungeon generation, combat and exit |
| Native games | `scripts/test_vectrex_games.py` with an external asset manifest | Deterministic controls, exact source/asset hashes, frames, bus counters and optional PCM |

## V5 FPGA build history

Vivado 2025.2 completed `build/vivado-vectrex-v5` for `xc7z020clg484-2` and
exported the bitstream and `multitini_one.xsa`. The recorded gates pass:

| Gate | V5 result | Report under `build/vivado-vectrex-v5/reports/` |
| --- | --- | --- |
| Setup, hold, pulse width | WNS +0.103 ns, WHS +0.047 ns, WPWS +1.833 ns; zero failing endpoints | `timing_summary.rpt` |
| Routing | All 10,436 routable nets routed; zero routing errors | `route_status.rpt` |
| CDC | 27 CDC-3 and one CDC-9 informational entries; no warning/critical entries | `cdc.rpt` |
| DRC | No errors or critical warnings; warning entries remain | `drc.rpt` |
| Bus skew | All five constraints met; minimum slack +26.448 ns | `bus_skew.rpt` |
| Timing coverage | No unclocked registers, unconstrained internal endpoints, or unaccounted input/output delays | `timing_coverage_gate.rpt` |

The coverage report accounts for the clock outputs `dvi_clk` and
`audio_i2s_bick`, and the explicitly false-pathed `audio_spdif_out`.
DRC warnings include DSP pipeline and BRAM asynchronous-control checks;
methodology warnings also remain. Passing the gates does not mean no warnings.

Routed utilization is 6,233 LUTs, 3,781 flip-flops, 123 RAMB36 plus two RAMB18
(124 of 140 BRAM tiles), and seven DSP blocks. The portrait framebuffer remains
540 by 720 with a 96 MHz machine fabric and 1.5 MHz CPU cadence.

`build/vivado-vectrex-v5/source-inputs.json` captures 41 SHA-256 source and
settings hashes. All 41 match the reviewed working files. `build_manifest.txt`
records baseline `2ede66e487dfa403799929f708acaf1563b33def`, `dirty=1`, and
`ACTIVE_MACHINE=3`; the captured input hashes identify the actual build inputs.
The Vitis application was rebuilt subsequently. The VEC1/VEC2 entries below
record those packages; VEC3 adds the physically tested border update.

## Native replay comparison

The native replay commands and long BIOS startup behavior are described in
[`VECTREX_GAME_TESTS.md`](VECTREX_GAME_TESTS.md). The
`renderer-native96-final/report.json` run executes 13,500,001 CPU cycles in
nine emulated seconds at 96 MHz / 540 by 720 with zero renderer overflows.
The seven-game `renderer-pipeline-final/report.json` replay uses a 24 MHz
fabric and 135 by 180 framebuffer, retaining the 1.5 MHz CPU cadence. Each
14-second run records 21,000,002 CPU cycles and zero renderer overflows.
All report paths in this section are under `build/vectrex-games/`.

The final `reset-ram-final/report.json` repeats Mine Storm and Berzerk after
the reset queue flush, reset input gate, and RAM write-address mux changes.
Its machine-source hash matches the V5 captured input. Compared with
`renderer-pipeline-final/report.json`, all reported CPU, bus, VIA/RAM, beam,
button, audio and game-fetch counters match. Both statistics CSV files are
byte-identical; all 15 PNG hashes per game and both 14-second WAV hashes also
match. Only the machine source hash, output locations, host runtime and report
metadata differ. The other five games were not repeated after that last patch.

| Game | First game-fetch cycle / PC | Game instruction fetches | Recorded 32-bit fetch hash |
| --- | --- | --- | --- |
| Mine Storm | 7,925,117 / `$E016` | 292,908 | 1,696,201,050 |
| Berzerk | 7,022,478 / `$001A` | 784,361 | 3,328,957,538 |

| Game | Observed simulation scene |
| --- | --- |
| Mine Storm | MINE FIELD 1, player craft and mines after the launch animation |
| Berzerk | Maze, robots, player and score 50 |
| Armor Attack | Tanks and maze |
| Scramble | Ship, terrain and HUD |
| Spike | Character and speech bubble |
| Star Castle | Rings and player craft |
| Clean Sweep | Maze, player and score |
| Vecalabeth | Dungeon, ladder, skeleton combat with health loss, and return outside |

The seven-game renderer replay has 14-second 48 kHz, stereo, 24-bit WAV captures
with no full-scale samples. The final Mine Storm/Berzerk reset/RAM replays retain
identical audio hashes. Digital signal activity and unclipped samples do not
establish sound quality or speech intelligibility. Listen through the final
board audio path during acceptance.

## Vectrex32 API scope

All 75 function names in the Vectrex32 1.29 manual have implementations, including
CodeSprite/Peek, binary file and terminal operations, and sprite diagnostics.
The [API coverage and limitations](VECTREX32_API.md) document is part of this
acceptance record. This native ARM language/backend reports `Version()` 127;
it does not execute the original PIC32 firmware. High-level lines, text timing,
shared-RAM/code layout and peripheral behavior have documented differences.
Vecalabeth's deterministic progression and directed API tests do not establish
all program paths, original hardware fidelity, or ARM frame deadlines.

## Board session

### VEC3 video update accepted on hardware

On 2026-09-15 the owner reported "Tested on physical. Good." for the delivered
VEC3 update and requested commit and push. The exact bundle, firmware hash,
installation procedure and validation scope are recorded in
[Video test build](VIDEO_TEST_BUILD.md). Borders share the Video tab, retain
the original game sizes, and clip at the display edges. Size controls were removed.

All five FPGA images passed setup, hold, pulse-width, routing, CDC and bus-skew
gates. Vectrex setup/hold/pulse slack is +0.104/+0.045/+1.833 ns. The VEC3
software evidence includes 34 host tests, 148 Python tests (two skips), three
sanitizer tests, 32 portable HDL benches and seven focused video tests.
The owner report establishes successful physical testing of this update;
the detailed per-game and sustained-load procedures below remain separate.

### VEC2 media browser correction

The owner reported the missing `media` directory convention and invisible
BASIC subdirectories in VEC1. VEC2 (`56454332`) corrects the package root to
`0:/multitini/cores/vectrex/media` and adds folder/parent navigation, including
USB Backspace dispatch. Nested BASIC loads use the containing directory for
asset access and retain the full relative path for Reset. Nested cartridge
settings retain the existing wire format and length limit.

All 33 host tests pass, including the menu-to-folder-to-BASIC-to-asset-to-Reset
regression, path bounds, rejected traversal, and saved cartridge round trips.
The folder state and common menu also pass directed ASan/UBSan tests. The
updated ARM application builds in `build/v32b` using the verified V5 platform;
it reports 834,559 bytes text, 4,380 data and 90,718,576 BSS. The corrected
package and its byte audit are in `build/vectrex-media-fix`. The V5 FPGA image
is the same selected image as in the original test bundle.

### Original VEC1 evidence

The final local [test bundle](VECTREX_TEST_BUILD.md) uses the accepted V5 core,
the separately accepted supervisor build and a fresh Vitis 2025.2 platform/FSBL
from the V5 XSA. The ARM ELF links with 827,303 bytes text, 4,380 data and
90,714,480 BSS, including its reserved heap and stack. All 32 host tests pass.
ASan, UBSan and leak checks pass both runtime unit targets and the two external
Vecalabeth progression routes, including real BIOS raster glyphs.

The Windows BIOS-font replay records 2,000 frames, 100,181 statements and
697,427 lines on its first route. Its dungeon/combat route reaches the return
outside at frame 1,891 with 126,463 statements and 689,792 lines. Both preserve
all thirteen game hashes and the BIOS hash. Linux has a different libc random
sequence; the same gameplay-event checks pass there. Reports and vector captures
are in `build/vectrex32-final-bios-replay` and
`build/vectrex32-sanitize-bios-replay`.

`build/vectrex-test/package-audit.json` verifies the final firmware's FSBL
partition and two application partitions against every initialized ELF segment,
including load/entry addresses and padding. It also regenerates the selected
supervisor/native bitstreams to compare their exact payloads and checks the
core manifest. This establishes package identity, not hardware operation.

Record the exact supervisor firmware, core package, BIOS and game hashes.
Use the controls documented in `VECTREX_FIRMWARE.md`. For each native game,
start a game, move, fire or use its actions, score, lose a life, and restart.
Check vectors, text, motion, sound, and controller response during sustained
play. For Vecalabeth, repeat the host progression with physical controls and
verify that frame pacing stays usable in the overworld and dungeon.

Also exercise F12/menu return, pause/resume, repeated program launch, cartridge
ejection, cold and warm starts, two controllers, controller disconnect and
reconnect, and switching between machine cores. File API tests must use a
disposable game directory when testing writes or removal.

The VEC3 owner report above records the branch's physical acceptance.
Completion of every detailed game and stress-test procedure has not been recorded.
