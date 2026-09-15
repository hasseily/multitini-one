# Vectrex simulation inputs and captures

The chip tests, beam tests and game harness use the same HDL as the machine.
Generated executables, ROM inputs, vector traces and captures stay in ignored
`build/` directories. A passing activity check is not a claim of complete game
compatibility or a physical board test.

On Windows, invoke the runners with an absolute Python path. Every compiler
and simulator child goes through `scripts/run_native_tool.py`, which supplies
the UCRT runtime and prevents modal loader-error dialogs.

```powershell
& 'C:/Program Files/Python312/python.exe' -u scripts/run_vectrex_chip_tests.py
& 'C:/Program Files/Python312/python.exe' -u scripts/run_vectrex_video_tests.py
```

The VIA tests check timer/PB7 modes, first/final shift-register bits, external
and internal shift clocks, interrupts and CA2 reset/pulse behavior. The PSG
tests check the AY8912 controller port, package variants, tone division and
envelope shapes. Video tests cover analogue settling, sample and hold,
reference subtraction, integrator gain, immediate blanking, zero, pause,
line geometry, clipped pixels, retracing, queue back pressure and phosphor
decay.

## Local ROM inputs

The preparer can extract the 8,192 BIOS bytes from the `rom_data` initializer
in an existing local copy of `rtl/bios_rom.vhd` from Vectrex_MiSTer commit
`945088b8862c09aa556f3d0c3efa07eeaa1a5b5b`. It does not import machine HDL.
Supply the path explicitly:

```powershell
& 'C:/Program Files/Python312/python.exe' -u scripts/test_vectrex_games.py --bios-vhdl PATH_TO_BIOS_ROM_VHDL --download --prepare-only
```

The optional game download uses the ordinary public archive linked by
[Spike's Big Page](https://www.atarihq.com/vectrex/emu.htm). The source URL,
archive checksum and individual extracted checksum are written to
`build/vectrex-games/assets/manifest.json`. Only Armor Attack, Berzerk, Clean
Sweep, Scramble, Spike and Star Castle are extracted. Mine Storm uses the
BIOS. These local inputs remain `NOASSERTION`; availability is not an open
content license and the harness does not add ROM files to source control.
The pinned archive checksum must be reviewed if the source changes.

## Machine acceptance

The default Verilator backend builds a C++ model with the installed UCRT GCC.
It avoids the shell-dependent archive recipes in the Windows makefiles.
`--backend iverilog` selects the slower four-state simulator, useful for
detecting unknown signals. Both backends agreed on the first 375,001 CPU
cycles and startup framebuffer in the initial BIOS smoke test.

```powershell
# Cold boot: 20 seconds includes the BIOS splash and game title/music.
& 'C:/Program Files/Python312/python.exe' -u scripts/test_vectrex_games.py --games minestorm --run-name cold-minestorm

# Faster game regression using the console's public warm-reset input.
& 'C:/Program Files/Python312/python.exe' -u scripts/test_vectrex_games.py --warm-reset-cycle 375000 --cycles 15000000 --jobs 3 --run-name warm-gameplay

# Native fabric rate and full framebuffer dimensions.
& 'C:/Program Files/Python312/python.exe' -u scripts/test_vectrex_games.py --games minestorm --clock-hz 96000000 --width 540 --height 720 --warm-reset-cycle 375000 --cycles 13500000 --run-name native-gameplay
```

Cold startup intentionally keeps all cartridges at the same Vectrex screen
for about 10.24 seconds: the BIOS runs 512 refresh loops before testing the
cartridge. Its subsequent game title waits for both the title counter and
music. Five-second cold runs therefore cannot establish cartridge execution.
This sequence is visible in the
[BIOS disassembly](https://github.com/technobly/Vectrex-VecForth/blob/master/VecForth/Vectrex/Reference/BIOS.ASM).

The optional warm reset asserts `core_reset_request` after the BIOS has set
its normal RAM flag. It exercises the console reset behavior; it does not
patch ROM, RAM, CPU state or timer values. Cartridge images are loaded through
the public framed load interface. Replayed buttons and signed axes use the
public input event interface. The script repeats start and fire pulses so
menus reached at different times can start their games.

Each run records bus/IO activity, executed addresses, first game instruction,
renderer overflow count, image hashes, grayscale snapshots and a final beam
and VIA-pin trace window. Audio is captured directly from `audio_valid` as
48 kHz stereo signed 24-bit PCM and packaged as WAV. The report includes each
channel's peak, DC level, AC RMS, changed-sample count and full-scale sample
count. Paused framebuffer capture time is excluded from the audio. These
measurements establish signal activity and clipping; listening is still
needed to assess speech intelligibility and fidelity.

`report.json` contains the exact HDL checksums from
compilation, not the potentially changed working tree at report time.
Review captured gameplay and compare a control-free run when an interaction
needs confirmation. Startup activity alone is not a gameplay acceptance gate.

Full physical validation still needs the board's DVI output, audio, analog
controller events and game progression. Light pen, imager, cartridge-specific
hardware and exhaustive 6809/6522 timing are separate compatibility work.
