# GBC Functional Baseline

Status: complete and accepted on 2026-09-14. The owner confirmed the current
GBC implementation works correctly, including saves, and stated that they
were tested. This closes the second-core functional reuse milestone.

## Accepted package

| Component | Build | ABI |
| --- | --- | --- |
| ARM firmware | 20260917 | 0.10 |
| Supervisor FPGA | 20260915 | 0.10 |
| GBC FPGA, PSRAM enabled | 20260913 | 0.9 |

The differing build IDs are intentional and identify artifacts rather than
calendar dates. The current supervisor accepts the retained GBC package.
Closeout preserves the tested firmware and FPGA bytes.

The GBC payload is `build/current/multitini/cores/gbc/gbc-default.bin`,
4,045,568 bytes, SHA-256:

```text
91937d6059bde81795092146711b3a429939cc753f080d5d8436d996e73e9043
```

`provenance/current_release.json` pins both the payload and `core.cfg`, plus
the other current runtime inputs. Run
`python scripts/promote_current_release.py --check` to verify the selection,
assembled SD tree, and compatibility copies.

## Baseline scope

- Select GBC through the shared core menu and full-PL loading service.
- Load user-supplied GB/GBC cartridges from 32 KiB through 8 MiB using PSRAM
  and the built-in open SameBoy-derived bootstrap.
- Display 160x144 game frames scaled 6x within the common 1080p output, and
  route stereo audio through the common sound service.
- Use the shared USB gamepad service or the keyboard fallback: arrows,
  Z, X, Enter, and right Shift.
- Restore and save supported external battery-backed cartridge RAM through
  the common media service and journaled SD storage.
- Use the shared reset, detach, and core-switch barriers to commit dirty
  cartridge RAM before ending the active session.

These are the implemented baseline facilities. The owner's acceptance covers
the tested setup; it does not establish every cartridge, peripheral, or
failure sequence individually.

## Saves

Place cartridges in `0:/multitini/cores/gbc/media`. Saves sit beside them as
`<cartridge-name>.sav`: `Example.gbc` uses `Example.gbc.sav`. Keep that name
when moving or backing up a cartridge and its save.

After roughly one second without a cartridge-RAM write, the machine pauses
for a coherent snapshot and SD commit. The menu reports **Pending write**,
**Saved**, or **WRITE FAILED**. Wait for **Saved** before removing power.
Reset, cartridge detach, core switch, and golden reboot force a commit first.
A failed commit retains the dirty state and blocks the operation so it can
be retried after the storage problem is resolved. Sudden power loss before
a successful commit can still lose recent progress.

The external-RAM save path recognizes battery-bearing ROM/RAM, MBC1, MBC2,
MMM01, MBC3, MBC5, MBC6, Camera, HuC3, and HuC1 header layouts. Recognition
is not a claim that every mapper or cartridge has been board-tested. MBC3
wall-clock state and MBC7/TAMA internal EEPROM or backup RAM are not persisted.

## Evidence

Board evidence is the owner's 2026-09-14 confirmation of the current GBC
implementation and tested saves. Earlier board records establish C64-to-GBC
switching and Tomb Raider/V-Rally gameplay, followed by confirmed physical
save persistence on 2026-09-04. No new flash, UART capture, or board test was
performed during closeout. See [Hardware results](HARDWARE_RESULTS.md).

Fresh closeout checks pass all 28 host tests, including Game Boy boot,
content/save service, input, persistent journals, and supervisor settings.
The 47-file Game Boy import audit, generated interface/bootstrap/ports checks,
repository policy check, and exact current-package verification pass.

All ten focused HDL regressions pass: GBC video, input, clock enable,
cartridge loader, memory, and saves; common PSRAM line service, PSRAM BIST,
memory CDC, and audio. The existing GBC lint script also passes using
Icarus 13 and Verilator 5.050. Logs and exact invocations are retained locally
in `build/validation-gbc-baseline/`. These checks retain the script's VHDL
and generated-Zynq black boxes and upstream-video lint exclusion; they do
not replace mixed-language Vivado elaboration or timing review.

The retained image's saved Vivado 2025.2 reports show WNS `+0.181 ns`,
WHS `+0.054 ns`, and WPWS `+0.265 ns`, with no failing setup, hold, or
pulse-width endpoints. These are historical build results. No new FPGA or
ARM build is needed to accept the existing tested image.

## Separate follow-up work

Persistent RTC, MBC7/TAMA backup storage, SGB, MegaDuck, camera UI, link cable,
cheats, rewind, savestates, and borders are outside this baseline. Exhaustive
mapper coverage, interrupted-write/power-loss tests, repeated-switch rollback,
long-run storage/memory stress, and broader display/input/audio qualification
remain product qualification work.

The retained GBC binary is hash-verified, but its historical build manifest
records a dirty tree at `2e7356a` without a complete saved source delta or
input-hash set. Exact historical source-to-binary mapping is not established.
Functional acceptance does not close that provenance gap. A future public
GBC binary needs matching captured source and build inputs; a replacement
image also needs board validation. This closeout updates the local package
record and does not newly publish or rebuild the binary.
