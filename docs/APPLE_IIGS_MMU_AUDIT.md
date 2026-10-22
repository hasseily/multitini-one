# IIgs MMU audit against KEGS and GSSquared

## Scope and reference policy

Audited the combinational MMU, soft-switch state, CPU peek, native retirement,
and adapter ROM/RAM selection. **KEGS rules take precedence over GSSquared**,
as requested. This policy is not a claim that all KEGS behavior has been
measured on an Apple IIgs.

References:

- [KEGS 1.38 archive](https://kegs.sourceforge.net/kegs.1.38.zip), SHA-256
  `d54c04cef80cc2d0dbd605210130047c03c5c2a89edc13745b74d16aa97d97ff`.
  Audited `src/moremem.c` mapping/state, `sim65816.c` reset/absent memory,
  `video.c` reset, and `config.c` ROM loading.
- GSSquared commit `695e627acb8154adfe7ea5ac972b32fbd50e2c3a`:
  `src/mmus/mmu_iigs.cpp`, `mmu_iigs.hpp`, `mmu_iie.cpp`, `mmu_ii.cpp`, and
  `apps/iigsmmutest/tests.hpp`. Reference sources were not changed.
- Imported MiSTer IIgs remains pinned to
  `9adbba8622f378253765b7d438b6cbcc4d03fc57`. Changes are reproduced through
  `scripts/port_apple_iigs_core.py`; imported upstream bytes are unchanged.

The HDL and regression additions are original code. Tests need neither
reference repository nor private media. Reference downloads, private ROMs,
game bytes and execution receipts remain under ignored `build/`.

## Findings and corrections

| Area | Previous behavior | Selected KEGS behavior |
| --- | --- | --- |
| Extended shadow | SHR predicate accepted only bank 01; bank 03 restore writes disappeared. Higher-bank text/HGR and I/O aliases were also enabled. | `fixup_shadow_all_banks`: installed odd banks 03+ mirror `$2000–$9FFF` into E1 when SPEED.4 is set and SHADOW.3 clear. Higher even banks have no aux redirect or I/O aliases. |
| Aux HGR | Bank-00 writes redirected to aux could ignore AUXHGR inhibit. | Use resolved bank parity. Aux HGR1 requires SHADOW bits 4 and 1 clear; HGR2 requires 4 and 2 clear. SHR bit 3 clear overrides both within `$2000–$9FFF`. Main HGR uses bits 1/2. |
| Text/aux | Low-bank text mostly correct; higher even banks inherited partial RAMRD/RAMWRT. | Preserve TXT1 bit 0 and ROM03 TXT2 bit 5, including aux. Low main banks use ALTZP, RAMRD/RAMWRT, and 80STORE/PAGE2 overrides. Higher banks stay directly addressed. Shadow mirrors occur only on writes. |
| IOLC inhibit | Flat C/D backing layout followed GSSquared. | `fixup_shadow_iolc`: fast Cxxx exposes physical Dxxx; Dxxx exposes physical Cxxx. Cxxx bypasses ALTZP; Dxxx/Exxx retain it. These windows become writable RAM. |
| Language card | E0/E1 overlaid ROM and obeyed LC write protection. | `fixup_lc`: only fast banks overlay ROM/protect writes. E0/E1 D–F use slow RAM. LC bank 1 D writes fold to C; D reads fold only with RDROM clear. |
| Vectors | Prior Doom GS fix suppressed forced ROM under IOLC inhibit. | Preserved: vector pulls use ROM with mapped fast-bank LC, RAM when inhibited. |
| I/O state | C000–C00F writes ignored banks 01/E1. | Identical side effects in 00/01/E0/E1, with SHADOW.6 gating 00/01. Write-only switches remain unchanged on reads. |
| Slot selection | INTCXROM did not reach MMU decode; SLOTC3ROM was unused. | INTCXROM overrides external slots. Slot 3 uses SLOTC3ROM. Per KEGS, C00A/B and C02D bit 3 share state/readback. |
| C800 ownership | Unconditional internal ROM only in bank 00; no internal latch. | C3 reads/writes with SLOTC3ROM clear latch INTC8ROM. Other Cn accesses do not clear it. CFFF returns its old selected byte then releases ownership. Internal C800 is selected by INTCXROM or INTC8ROM in all four aliases. |
| Mapped writes | C1–CF writes could corrupt hidden LC bank-1 RAM. | Mapped logical Cxxx enables no RAM. Genuine Dxxx-to-Cxxx LC writes remain legal. |
| Acceleration | Internal ROM reads could bypass ownership updates. | Mapped C1–CF accesses retire natively, updating ownership once per valid commit. CPU peek remains CPU-addressed during DMA. |
| IRQ dispatch | C071–C07F ROM selected only in bank 00. | Dispatch bytes available in all four mapped I/O aliases. C070 stays paddle I/O. |
| ROM01 FC/FD | Mirrored the loaded FE/FF ROM. | No ROM lookup; return KEGS's zero-filled reserved-half value. ROM03 retains FC–FF. ROM writes are discarded. |
| Absent RAM | Returned the bank number in some banks. | Zero reads, discarded writes, no DDR lookup or shadow mirror. |
| Reset | SHADOW=08, INTCXROM=0; warm reset replaced SPEED bits; HIRES survived. | SHADOW=00, C068=0D, LC writes enabled/prewrite clear. Cold SPEED=80 for ROM01/C0 for ROM03; warm preserves SPEED. Clear HIRES/MIX, set TEXT/AN3, text color F0. |
| C029/C036 | Stored reserved bits; reset NEWVIDEO unconditionally. | C029 stores written_value & $E1; warm reset preserves bits 6/0 and clears 7/5. C036 clears bit 5 and permits bit 6 only on ROM03 when input bit 5 is clear. |
| LC state/C068 | Prewrite transitions and atomic C068 already correct. | Preserved and exhaustively tested with interleaved accesses, aliases, stalls and resets. ROMBANK remains a stored mapping no-op. |

Multitini's slot-7 SmartPort expansion ROM remains available below internal
ROM priority. Synthetic test programs now explicitly clear INTCXROM before
requesting external slot ROM, rather than depending on the old reset value.

Correct C800 ownership also exposed a defect in our SmartPort firmware. The
actual system ROM boot scan reads C3xx and leaves INTC8ROM set. The old
slot-7 entry jumped straight into C800, which now correctly selects internal
ROM. All three published firmware entries now release the latch through
CFFF from a C7 trampoline before entering expansion ROM. The trampolines
preserve A/P/X/Y/SP and the public entry addresses; C800 payload bytes are
unchanged. The corrected FPGA must be paired with a rebuilt `hdd.rom`.

## Reference disagreements and limits

GSSquared's wider shadowing, including uninstalled banks, extended aux/IOLC
aliases, AUXHGR interpretation, linearized aux-memory addressing and E0/E1
LC ROM/protection were not adopted where KEGS differs. GSSquared includes
hardware-test claims for some of these; KEGS precedence is the selected
compatibility policy, not independent hardware proof.

KEGS reports NEWVIDEO bit 0 cleared as unsupported. The core retains its
existing E1-as-E0 behavior there. KEGS does not model GSSquared's C029
linearization. The following remain explicit limits to whole-emulator parity:

- Unowned C800 returns fixed `$80`; unimplemented external slot ROM returns
  `$FF`. KEGS uses a video-derived floating-bus byte. Ownership and RAM
  exclusion are corrected; floating-bus timing is unchanged.
- KEGS exposes a guest-visible bank-EF DOC-RAM alias, with sizing-address
  exceptions. This core does not expose that alias; DOC RAM access remains
  through the sound interface.
- C037 remains the core's DMA-bank extension; KEGS ignores it. Device
  registers such as C02B warm-reset preservation and expansion-card
  implementations are outside this MMU patch.
- KEGS host diagnostic halts on unsupported/reserved writes are not emulated.
  Their register storage/readback results are tested.

## Validation

```text
python scripts/port_apple_iigs_core.py --check
python scripts/test_apple_iigs_mmu_decode.py
python scripts/test_apple_iigs_mmu_state.py
python scripts/test_apple_iigs_mmu_slots.py
```

| Test | Result |
| --- | --- |
| Independent decode oracle | 12,484,556 checks passed: every SHADOW byte, both ROMs, 32/128-bank configurations, boundaries, 50,000 combined random states, 32 background scenes. |
| Production state/retirement | 472,977 checks over 19,356 transactions passed: C029/C035/C036/C068 values, four aliases, LC access pairs, dirty warm/cold resets. |
| 65816 RTL model plus adapter/cache | 66 checks at stock and 66 at maximum passed; 80 native slot retirements, 30 protected writes. Includes Arkanoid restore at `$032000`/`$0393A0`, and absent-bank zero/drop checks. |
| SmartPort firmware | 171 cases and 51 DMA operations passed with INTC8ROM initially set; C7 entry preserves registers and the peak stack remains 35 bytes. Old firmware fails the latch regression. |
| Full adapter with rebuilt SmartPort | All 19 groups passed, including C70D calls with INTC8ROM initially set at stock and maximum speed. |

Source-hashed receipts: `build/mmu-audit/decode-final/`, `state-final/`, and
`slots-final/`. The pre-patch KEGS state suite had 5,947 mismatches. A separate
dirty-HIRES reset baseline also failed before its correction; logs are retained.
SmartPort receipts are `build/mmu-audit/smartport-fixed/validation.json` and
`adapter-smartport-fixed/validation.json`. The rebuilt 2,304-byte `hdd.rom`
has SHA-256 `932f71ef55edd7bd74da7a797a54a610c1af8d2d0a4ee3d20d5756891d6b6d34`.

Existing peek, vector, acceleration and SmartPort fixtures were corrected
where their expected mapping encoded the removed all-bank I/O aliases,
GSSquared C/D backing, slow-bank ROM overlay, old INTCXROM reset or unconditional
C800 fallback. Their cache, timing, exact-byte and retirement checks remain.
The upstream vector negative control still reproduces forced ROM under IOLC,
with only unused interface pins added to the pinned decoder for compilation.

The existing full-machine port/lint, IRQ-dispatch, IOLC-vector, logical-cache,
CPU-peek and acceleration suites also pass. Final receipts are under
`build/mmu-audit/qualified-*`; the adapter receipt above supersedes the earlier
adapter run by including the new firmware and prelatched C800 case.

Actual system ROMs also pass bounded HDD boot with the private Arkanoid disk
and rebuilt SmartPort ROM at speed 6, fresh PRAM and 120 million primary ticks:

| System ROM | Completed block reads | RAM instructions | Boot block / native IRQ |
| --- | ---: | ---: | --- |
| ROM 01 | 1,540 | 8,623,735 | Both reached |
| ROM 03 | 1,464 | 8,695,598 | Both reached |

Receipts are `build/mmu-audit/boot-fixed-rom1/validation.json` and
`boot-fixed-rom3/validation.json`; both verify unchanged captured/current
sources and report no repeated BRK. System ROMs and media match the earlier
runs; `fixed-boot-roms/provenance.json` records the sole ROM asset change to
`hdd.rom`. A paired pre-audit ROM03 run completed 1,464 block reads, whereas
the corrected MMU with the old slot ROM completed none. This negative control
and the C3/C7/C800 bus trace establish why the matching slot ROM is required.
These checks establish bootstrap progress, not playable Arkanoid behavior.

Native floppy boot is not validated. Both patched and paired pre-audit ROM03
runs stop at the same IWM `$E1C0EE` poll after 41 completed media requests
within 120 million primary ticks. Only the MMU/iigs source pair differs;
other captured sources, private inputs and simulator arguments are identical.
`build/mmu-audit/native-prepatch-rom3/comparison.json` records the comparison.
This observed fixture failure predates the MMU patch. Patched ROM01 also
stalls after 41 requests; no ROM01 pre-audit comparison was run.

`build/mmu-audit/final-validation.json` indexes the 13 successful suite receipts
and verifies 508 recorded source hashes against the final working tree.
The generator check, repository policy check and `git diff --check` pass.

## FPGA build attempt

On 2026-10-22, IIgs image `4D000611` was built from commit `5e3a20c` on
`m07`, using Vivado 2025.2 and the build/repair recipe from the previously
qualified `4D000610` delivery. The input capture contains 235 files.

```text
python scripts/build_core_gs.py --image apple-iigs --build-id 4D000611 --output build/gs611 --jobs 1
```

Synthesis and routing completed. The initial build generated a bitstream,
but the setup-timing export gate rejected it. Both subsequent physical
optimization passes also failed nominal setup timing:

| Implementation | Setup WNS (ns) | Hold WHS (ns) | Failing setup endpoints | Setup TNS (ns) |
| --- | ---: | ---: | ---: | ---: |
| Standard build | -0.813 | +0.042 | 2,013 | -540.729 |
| `reroute_high`, 4 threads | -0.324 | +0.017 | 115 | -10.948 |
| `phys2_fanout`, 6 threads | -0.209 | +0.017 | 58 | -4.100 |

These are final timing-report values after clearing temporary optimization
margins. Pulse-width slack is +1.250 ns in all three reports. The last
checkpoint has 56 failing machine-clock endpoints and two video endpoints;
the machine-to-CLK14 crossing passes at +0.201 ns. All 57,469 routable nets
are routed without errors. The initial native-clock and phase gates passed,
but no repaired checkpoint qualified for full signoff or bitstream export.

The worst machine path runs from CPU microcode BRAM through address
generation, the L0 cache lookup/data mux and ALU flags to `P_reg[1]`.
It has 28 logic levels and 65% routing delay. Floppy track-RAM address paths
also fail. The separate video limit is text-bank BRAM to a glyph-ROM address
pin. These are the remaining timing targets; the current reports do not
attribute all of their delay to the MMU changes alone.

The attempt receipt is `build/gs611/build-attempt.json`; diagnosis is in
`build/gs611/initial-diagnosis.json` and `final-diagnosis.json`. Repair
checkpoints, exact Tcl recipes, logs and result receipts are retained under
`build/gs611-repair/`. Its `recorder-recovery/transition.json` records a
log-header parser correction; the Vivado run and its evidence were preserved.

No qualified `4D000611` package was produced or installed. The previously
qualified `4D000610` delivery is unchanged. Further timing closure is required
before the MMU changes can be tested on the board. The matching SmartPort
`hdd.rom` was rebuilt with the hash recorded above; ARM firmware was not
rebuilt. Full-game behavior and board acceptance remain unproven.
See [Arkanoid diagnosis](APPLE_IIGS_ARKANOID.md) for game
evidence and the separate ROM03 image-compatibility question.
