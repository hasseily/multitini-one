# Vectrex overlay test update

This update adds MiSTer `.ovr` artwork and adjustable beam brightness to
Vectrex and Vectrex32. **Both the VEC4 firmware and the Vectrex core package
must be installed.** Physical testing of this overlay update is pending.

## Install

Use the normal firmware updater with `sd/FIRMWARE.BIN` (**VEC4**). Merge the
supplied `sd/multitini/cores/vectrex/` directory into the same directory on
your SD card, replacing only `core.cfg` and `vectrex-native.bin`.
Keep the installed C64 and GBC packages unchanged. Preserve all ROMs,
media, saves, and `multitini/settings.bin`; existing settings migrate
automatically. The update contains no Vectrex BIOS, games, or artwork.

The new Vectrex image exposes ABI 0.13. The firmware retains the exact
supervisor FPGA, boot loader, and ARM platform from the physically accepted
VEC3 video-settings build; the supervisor continues to expose ABI 0.12.
The new controls check the active hardware's identity and ABI, so older
machine images remain usable and show unsupported overlay controls.

## Artwork and controls

Download the [pinned MiSTer overlay archive](https://github.com/MiSTer-devel/Vectrex_MiSTer/raw/945088b8862c09aa556f3d0c3efa07eeaa1a5b5b/overlays/overlays.zip)
and extract the desired `.ovr` files locally. The archive SHA-256 is
`2c6f6e8a1e02dcaf1a522f91a2e380ea1c4597dfa0faac379d3984e07e1b6461`.
It is an external artwork collection; no artwork is included in this update.

Place an `.ovr` beside its game with the same filename stem:

| Game | Artwork |
| --- | --- |
| `multitini/cores/vectrex/media/Arcade/berzerk.vec` | `multitini/cores/vectrex/media/Arcade/berzerk.ovr` |
| `multitini/cores/vectrex/media/Vecalabeth/vec.bas` | `multitini/cores/vectrex/media/Vecalabeth/vec.ovr` |
| BIOS-only Mine Storm | `multitini/cores/vectrex/media/Mine_Storm.ovr` |

Keep the user-supplied 8192-byte BIOS at
`multitini/cores/vectrex/roms/bios.bin`. BASIC programs retain their own
containing directory for external assets.

Use **F12 → Media → Overlay → Choose file...** to select artwork manually.
Folders are supported. Loading artwork does not restart the current native
game. Changing games replaces the artwork with the matching sidecar; a
missing sidecar clears the previous image. Unreadable or malformed sidecars
produce a warning while the selected game remains available. An invalid
manual file leaves the current artwork in place.

**F12 → Video** adds three Vectrex controls:

- **Overlay enabled:** On or Off; default On.
- **Overlay brightness:** 25%, 50%, 75%, or 100%; default 50%.
- **Beam brightness:** 1x, 2x, 3x, or 4x; default 2x.

The settings apply to native games and Vectrex32. They are saved separately
for Vectrex, alongside the existing border preferences. A failed settings
save leaves the live display and in-memory preferences unchanged. As with
earlier settings, an already-durable journal can recover a requested change
on the next boot if a later storage operation failed.

MiSTer artwork is headerless 540 by 720 ABGR4444. The loader retains all four
channels without palette reduction. Short, positive even files receive a
transparent-black tail. Some upstream files were exported with shorter
dimensions; a headerless file cannot describe or repair a different row
stride. See [format, source hashes, and artwork provenance](VECTREX_OVERLAYS.md).

## Evidence and limits

The accepted base is `video-settings-test-20260915.zip`, SHA-256
`439ed09606703a5d171f5b0e506382d9edcc19ec6bd806b79a9b352a4e16bbb2`.
The owner reported successful physical testing of that VEC3 update on
2026-09-15. This identifies the reused components; it does not establish
physical acceptance of the new firmware, Vectrex overlay renderer, or PSRAM
read path.

The selected new image is `build/vivado-overlay-v5`, completed with
Vivado 2025.2 on 2026-09-15. Its final reports show WNS **+0.089 ns**, WHS
**+0.035 ns**, and WPWS **+1.833 ns**, with no failing timing endpoints.
All eight PSRAM input setup and hold paths pass. All ten bus-skew constraints
pass, with minimum slack +26.148 ns; all 14,164 nets are routed, and critical
CDC findings are zero. Resource use is 9,037 LUTs, 6,104 flip-flops, 125 of
140 BRAM tiles, and 7 DSPs. The setup and hold margins are narrow. This is
a test image with physical overlay testing still pending.

The final software evidence contains 35 passing host tests and 150 Python
tests completed with two explicit skips. The complete HDL regression and
video tests pass. Both Mine Storm and Spike run for nine simulated seconds
at a 96 MHz fabric clock, each executing 13,500,001 CPU cycles with game
instruction fetches, button reads, rendered vectors, audio activity, and
zero renderer overflows. Vecalabeth completes the 2,000-frame directed
replay. Fresh overlay captures check 1,166,400 actual compositor pixels
across three beam gains. These are bounded simulations, not full-game or
physical-device acceptance.

The package builder requires an explicit final Vectrex build directory.
It rejects negative setup, hold, pulse-width, or bus-skew slack; failing
timing endpoints; incomplete routing; missing timing coverage; and critical
CDC findings. All eight PSRAM data pins must have passing setup and hold
paths from falling SCK to input registers packed into the FPGA I/O blocks.
The capture clock is a second 96 MHz output of the same MMCM, shifted by
279 degrees; it uses the rising edge. This PHY uses neither IDELAY nor IDDR.
The output clock uses explicit DRIVE 12 and FAST slew settings.

A setup-only multicycle value of two, scoped to these eight input registers,
associates the preceding SCK falling edge with the capture edge. The setup
requirement is approximately 13.28 ns. There is no matching hold multicycle
exception: hold must still check the next SCK falling edge, approximately
2.86 ns from the capture phase. The package checks these relationships and
the approximately 10.416 ns clock period in the actual reports. The new
Vectrex path has no external-PSRAM CDC waiver. Constraint counts and timing
values come from the selected reports, not from the older VEC3 image.

`manifest.json` records actual selected inputs, hashes, timing results, and
test logs. `package-audit.json` compares every initialized ARM ELF segment
by load address and independently regenerates both processed FPGA payloads
with Bootgen. `SHA256SUMS.txt` covers the packaged files. Software tests and
simulated pixel checks are not board captures or proof of analog video
fidelity. No downloaded artwork or artwork screenshots are in the bundle.

The PHY test evidence distinguishes five passing timing/warm-state cases
from two deliberate capture-phase failures outside the tested operating
window. Those two named negative controls must exit with code 1 and their
exact expected first-word mismatch. All other cases must exit successfully.
The package checks the complete log's byte boundaries, output hashes, and
per-case manifest; these are not waived production timing failures.
Overlay, native transport, beam/renderer, Vecalabeth, and native game replay
records retain exact source and input hashes. External assets and rendered
images remain local; the package contains only their metadata and test logs.

## Exact source and reproduction

The bundle deliberately records two source snapshots:

- `source.zip` and `source-manifest.json` contain the current firmware and
  Vectrex source, licenses, constraints, and build scripts. Actual compiler
  inputs, including CherryUSB, must appear in this inventory.
- `old-supervisor-source.zip` and
  `old-supervisor-source-manifest.json` preserve the accepted base snapshot.
  `evidence/accepted-vec3/` retains its supervisor reports and Vitis platform
  records. The current source snapshot must not be used to claim reproduction
  of the older supervisor binary.

`inputs/` contains the selected new application ELF and Vectrex BIT/XSA,
plus the exact reused supervisor BIT/kind marker/XSA, FSBL, and
`used-vitis-platform.zip`. The latter archive is the actual platform export
used by the compiler, including its basis XSA, headers, and libraries.
The new Vectrex XSA records the updated PL build; it did not replace that
accepted ARM platform for the supplied firmware.

Use Vivado/Vitis 2025.2 and the captured platform when reproducing the supplied
build. The local package verifier also uses Pillow to verify decoded PNG
pixel hashes. In a Git checkout at the new manifest's commit, overlay `source.zip`
and verify `source-manifest.json`. Extract the accepted platform archive into
an isolated directory and pass its `multitini_platform.xpfm` through
`MULTITINI_PLATFORM_XPFM` when invoking `scripts/create_vitis_workspace.py`.
The captured `evidence/vitis/` files record the actual compiler commands,
linker script, application configuration, and build log. Rebuilding the
old supervisor requires a separate checkout overlaid with the old source
archive. Any rebuilt artifacts need their own timing, payload audit, and
physical acceptance.

The ignored local assembly helper is retained as
`evidence/helpers/assemble-vectrex-overlays.py`; the selected build and input
capture helpers are beside it. In the original workspace, after final
signoff, the invocation has this form:

```powershell
& 'C:/Program Files/Python312/python.exe' -u scripts/run_native_tool.py -- `
  'C:/Program Files/Python312/python.exe' build/assemble-vectrex-overlays.py `
  --vectrex-build build/vivado-overlay-v5 `
  --firmware-log build/v34-final.log
```

The helper records the explicit final module and replay logs at their named
workspace paths. It requires the accepted base at its recorded path, current final firmware/source
inventory, final test logs, and an absent or empty output directory. It
never overwrites an existing package or searches for a fallback bitstream.
The dated ZIP uses the local date when assembly runs. Exact values in the
generated manifest take precedence over preliminary development results.
