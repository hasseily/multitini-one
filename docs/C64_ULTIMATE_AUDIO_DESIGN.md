# Multitini One C64: Ultimate Audio at $DF20-$DFFF (design)

Design stage only. No HDL, firmware, script or build file was changed for this design. Nothing was built or simulated.

- Repository: `multitini-one`, branch `c64-turbo`, HEAD `5341812` (M0.7.0dev5, C64 image `4D00C705`).
- Revision: final design after three reviews (fidelity, integration, licence and tests). All 28 review issues are dispositioned in the text. Reconciled with the turbo design and the user decisions of 2026-10-23 (section 0.7).
- **Release.** Ultimate Audio ships in the same release as the CPU turbo of `docs/C64_TURBO_DESIGN.md` (called TD below), together with REU image preload and the debug registers, with one ABI step, 0.23 to 0.24 (TD §6.0). Neither feature ships alone.
- **Citation base.** Every [V] points at a primary source that was read:
  - `upstream <path>:<lines>` = GideonZ/1541ultimate at `fea2798c217674cf0e6eddb121792f3a214ddd9f` (GPL-3.0), for example `https://github.com/GideonZ/1541ultimate/blob/fea2798c217674cf0e6eddb121792f3a214ddd9f/fpga/io/sampler/vhdl_source/sampler2.vhd#L128-L202`. Bare `sampler2.vhd:...` and similar VHDL/C++ names mean the same tree;
  - `Doom <path>` = slesinger/doom at `deed1f7256d3`; `xahmol <path>` = xahmol/ultimate-uci-oscar64 at `1a0a0b9ba79f`; `c64cast <section>` = kfox/c64cast (MIT) at `6199c5ffac0d`, architecture document on audio, cited by section title;
  - `Freshness audio.asm` = the KickAssembler source in CSDb release 131522 (no licence: facts only, no code copied);
  - `1541u-documentation <path>` = GideonZ/1541u-documentation at `2da134b31b54`, for example `https://github.com/GideonZ/1541u-documentation/blob/2da134b31b548c56647ebcccf7ef9100cd155ee1/howto/release_3.15.md`;
  - `c705r2 <report>:<line>` = the local Vivado reports of the M0.7.0dev5 C64 image `4D00C705` (`build/c705r2/c64-pal/reports/`). `build/` is not under source control, so these figures cannot be checked from the repository; `docs/C64_VICE310_COMPARISON.md:51` records the image's +0.133 ns setup slack. The combined build (UA-M4) reports every figure again;
  - `PDF p.N` = `https://github.com/GideonZ/1541ultimate/blob/fea2798c217674cf0e6eddb121792f3a214ddd9f/doc/ultimate_audio_v0.2.pdf` ("All rights reserved": facts restated in our own words).
  - Design-stage local copies of these files were a convenience, not an evidence location, and this document does not cite them.
- Design-stage background research notes are not cited as evidence. A claim that rests only on such a note is tagged [I].
- Tags: **[V]** verified by reading a primary file:line or URL; **[M]** measured or reported by a third party (user issue reports included); **[I]** inference, including the output of our own models and scripts; **[O]** open, with the experiment that settles it.
- Repository paths are relative to the repository root. `docs/C64_TURBO_DESIGN.md` is cited by section number only (`TD §n`), because its line numbers change when it lands. A rule cited from TD is a design rule of that document, not primary evidence. Turbo milestones, experiments, tests and gates keep their TD names with a `TD` prefix (TD M5, TD E4, TD T3); this design's milestones are UA-M0 to UA-M5 and its tests and gates keep T1-T15 and G2-G9. The Expand gate has one name in both documents, G-REU0.

Nothing in this document claims hardware support.

---

## 0. Summary

### 0.1 What software sees

- A new Options-tab checkbox, **"Ultimate Audio $DF20-$DFFF"**, default **Off**, at action **argument 8**, directly after "RAM expansion" (the full row order is in TD §6.2).
- Off: the machine is bit-for-bit today's C64, including the REU register mirrors through `$DFFF`.
- On (this turns the REU on; not available while a cartridge is attached): seven sample voices of 32 bytes each appear at `$DF20`, `$DF40`, ..., `$DFE0`. The register layout is the 1541 Ultimate / U64 / C64U one:
  - write-only registers, multi-byte fields big-endian;
  - signed 8-bit or 16-bit little-endian samples read from REU memory;
  - one-shot or A-B loop playback, a 6-bit volume and a 4-bit pan per voice;
  - a per-voice end-of-sample IRQ on the shared /IRQ line.
- Reads anywhere in the window return the IRQ status byte (even addresses) or the version byte `$10` (odd addresses). The REU keeps `$DF00-$DF1F`.
- Pitch, end timing and IRQ timing do not depend on PAL/NTSC, CPU turbo or DDR latency.

### 0.2 Architecture in a few lines

1. All UA logic runs on `c64_clk` inside the adapter. It adds no new clock. It has three crossings, all built from modules already reviewed in this repository and all listed for review in section 3: one more bit in the expansion-config mailbox (C3), a second `ddr_path` busy synchronizer (C4), and a status snapshot mailbox (C5). It also uses two levels that cross in turbo-owned mailboxes: the preload hold and its "UA held" echo (TD X11), and the shared "C64 diagnostics armed" level (TD X3). These add no crossing (C11, C12).
2. The voice timing and state are parallel per voice. A 12.5 MHz half-tick comes from a phase accumulator on `c64_clk`. Each sample period is exactly `(2*rate + 3)` half-ticks in both 8-bit and 16-bit mode.
3. Samples come from the REU DDR aperture through a second, read-only instance of the existing `multitini_c64_ddr_path` on **HP3**. In the C64 image only, HP3's ACLK becomes `c64_clk`, as HP2's already is. The REU's HP2 path is not touched.
4. Each voice has a 2-word (16-byte) buffer with one-sample-ahead prediction, including loop jumps. One UA read is outstanding at a time, with a minimum issue gap.
5. The data is coherent with REU writes. Reads are deferred while a matching REU write is queued, in flight, or completed less than `UA_POST_B_HOLD` = 16 clocks ago. A REU write accepted later invalidates matching buffered words and blocks a same-word buffer hit on the next clock.
6. The mixer follows the upstream law: volume `v/32`, the upstream 4-bit pan table, saturating sum to 18 bits. A boxcar average over each 48 kHz output period follows. The result is added to the SID sample with saturation at the adapter's existing 48 kHz point.

### 0.3 What is reused

- The REU, the REU cache (one additive read-only output group, no behaviour change), `multitini_c64_ddr_path` (unchanged, second instance) and the expansion-config mailbox (4 to 5 bits).
- The adapter's 48 kHz audio point, `irq_ext_n`, the `cpu_bus_*` strobe used by the REU registers, the drain/quiescence path and the capability/ABI pattern from dev3.
- `multitini_async_mailbox` for the status snapshot, as the C64 media-state snapshot already uses it [V hdl/board/multitini_one_top.sv:1038-1065].
- Firmware: `build_c64()`, `MT_MENU_ACTION_TOGGLE_CORE_OPTION`, `change_c64_expansion()` and `configure_c64_expansion()`.
- From the turbo design, shipped in the same release: the REU image preload with its UA hold and "UA held" bits in `C64_REU_HOST` (TD §6.1, §6.4 (a)), the UART debug session, and the shared arming bit `C64_CPU_CONFIG` bit 19 (TD §6.5).

### 0.4 Licence decision

New original SystemVerilog under GPL-3.0-only. It is written from the documented interface and from the verified behaviour of the GPL-3.0 upstream files `sampler2.vhd`, `sampler_regs.vhd` and `sampler_accu.vhd` at `fea2798`. No upstream file is imported into the product build (section 5). A test-only exact import of the sampler VHDL (option C, excluded from product builds) is mandatory in UA-M0 as the independent reference for the model. Do not import `lp_filter.vhd` (its notice forbids reuse), the v0.2 PDF ("All rights reserved") or the MOD player binary (no source).

### 0.5 What changes for the user

- A new checkbox in Core options, after "RAM expansion". UA plays from REU memory, so turning it on also turns the REU on, in the same apply. Turning the REU off also turns UA off. UA is therefore never "on but inactive" because of the REU. A cartridge blocks the REU and therefore UA: while one is attached the row is greyed with "Eject cartridge first" (sections 6.2, 6.3).
- With UA on, the REU registers no longer mirror above `$DF1F`, as on a U64 (section 1.9).
- The setting applies live while the menu pauses the machine. It is one global flag. The REU stays a per-profile setting, and every apply keeps the active profile's REU on while UA is on. A firmware downgrade loses the UA flag and the turbo settings in global bytes 26-27; the per-profile REU, SID and turbo on/off settings survive, and the settings file is not wiped (section 6.4).
- No board support is claimed. REU image preload ships in the same release (TD §6.4 (a)). Software that brings its own samples into the REU, such as the Pastebin example player and `ua_972_check`, and software that needs a preloaded REU image, such as Doom C64U, the Freshness MOD player and the synth, can all be tested once the image exists.

### 0.6 Regression gates (user rule)

- The REU is correct at stock today: Expand by Bonzai runs flawlessly on M0.7.0dev5 [V docs/C64_EXPANSIONS.md:103-108]. Nothing in this design may change stock REU behaviour.
- "Stock REU behaviour" means the option **Off**. With it Off the machine is bit-identical to today (section 1.1). **G-REU0** (Expand disks 1 and 2 with UA off and turbo Off or forced exact; this design's former G1; defined in TD §10.3; section 9) is a hard regression gate and blocks the release.
- With UA **on**, the REU is deliberately U64-like: no register mirrors above `$DF1F`. G2 (Expand, UA on and idle) is informational. An Expand failure caused only by the missing mirrors is accepted and documented in `docs/C64_EXPANSIONS.md`, not counted as a regression: Expand then needs UA off, as it would on a U64. Any other failure is a defect.
- G3 (Expand with seven voices playing) has two parts. Its REU-readiness part blocks the release. Its Expand-behaviour part is informational in the same way as G2.

### 0.7 Decisions that apply here

| ID | Kind | Decision | Applied in |
|---|---|---|---|
| Q7 | user rule | "Fix real U64 user problems instead of copying them" | 7 |
| Q12 | user, 2026-10-23 | The sampler becomes a C64 Options-tab option; turning it on enables the REU; its registers are I/O2 under turbo; argument 8 | 0.1, 1.10, 6.2 |
| Q15 | user, 2026-10-23 | REU image preload, C64 RAM readback and audio capture are in the first release; the last two are debug features | 0.5, 2.5, 6.7, 9 (G8, X-PRELOAD) |
| Q16, Q19 | user, 2026-10-23 | Debug features are off by default and work only inside a UART debug session: `:uastat`, the UA status counters and test-only status bits, the coherence probe, audio capture, RAM readback and diagnostic builds. They are not in the menu and are not persisted. Board gates enable debug first. REU image preload is a user feature | 6.1, 6.3, 6.4, 9 |
| Q17 | user, 2026-10-23 | Turbo and UA ship in one release with one ABI step, 0.24 | header, 6.1, 9 |
| Q18 | user, 2026-10-23 | Turning UA on also turns the REU on in the same apply. Turning the REU off while UA is on turns UA off. A loaded cartridge blocks the REU and therefore UA | 0.5, 1.1, 1.8, 6.2-6.4, 7 (I14) |
| Q20 | user, 2026-10-23 | Stock REU behaviour must not change. Expand disks 1 and 2 are a hard gate with UA off and turbo Off or forced exact. With UA on, an Expand failure caused only by the missing mirrors above `$DF1F` is accepted and documented | 0.6, 9 (G-REU0, G2) |
| DL1 | design decision, user may override | The UA preference is one global flag, settings byte 26 bit 2, consistent with the approved global turbo bytes 26-27 and downgrade-safe | 6.4 |

Q-numbers are the turbo design's (TD §0.6), which records the same decisions. TD's open questions are written "TD question N" and keep their own numbers: TD question 18, for example, is not Q18.

---

## 1. Software-visible specification

### 1.1 Enable option and decode

- **Effective enable** `ua_active = ua_enable && reu_active && !ua_hold`, where `reu_active = reu_enable && !cart_loaded && !cart_loading` [V hdl/core/c64/multitini_c64_adapter.sv:427] and `ua_hold` is `C64_REU_HOST` bit 13, the REU image preload hold (TD §6.4 (a); section 2.5). The UA therefore follows the REU's cartridge exclusion with no new rule.
  - `ua_enable` is `C64_EXPANSION_CONFIG` bit 4. Firmware sets it only together with bit 3 (REU enable): turning UA on turns the REU on, and turning the REU off turns UA off (section 6.3). The register clamp also clears bit 4 unless bit 3 is written 1 (section 6.1), so "UA on, REU off" cannot be written. The AND with `reu_active` stays as a second safeguard and covers the cartridge states.
  - This is a deviation from the U64 (section 7, I14). There the mapping comes only from "Map Ultimate Audio" and is independent of the REU enable, and REU and sampler cartridge prohibits are applied separately [V upstream software/io/c64/c64.cc:312-327, 1363-1371, 1414-1436]. On Multitini the REU is the only sample memory, so UA without the REU would have nothing to play.
- **UA active:**
  - CPU reads and writes of `$DF20-$DFFF` (I/O2 with `addr[7:5] != 0`) go to UA.
  - `$DF00-$DF1F` stays REU, with today's VICE semantics inside that block (`$0B-$1F` read `$FF`) [V multitini_c64_reu.sv:103-121].
  - Voice `n` (0-6) is at `$DF20 + $20*n`; the register is `addr[4:0]`.
- **UA inactive:** decode, read data, IRQ and audio are bit-identical to today [I, by construction in section 2.2; checked by test T2].
  - With the REU on, the REU registers mirror every 32 bytes through `$DFFF` [V adapter:437; multitini_c64_reu.sv:153-154].
  - With the REU off, I/O2 reads return the open-bus value `lastVicData` [V hdl/core/c64/multitini_c64_buslogic.vhd:163, 190].
- **REU DMA cycles** to `$DF20-$DFFF` do not reach UA, in either direction. This matches today's REU, which releases its aperture during its own DMA [V adapter:428-430]. `cpu_bus_valid` already excludes DMA [V hdl/core/c64/ports/fpga64_sid_iec.vhd:1039]. What a U64 does here is [O4].
- **Voice 7** (offset `$E0` on the Ultimate, reachable only by its internal CPU) does not exist. IRQ status bit 7 always reads 0 [V upstream slot_server_v4.vhd:956-957, 971-985; sampler_regs.vhd:58].

### 1.2 Register map (per voice, write-only)

Each byte takes effect on the clock it is written. Nothing is latched.

| Off | Name | Bits kept | Reset value | Effect |
|---|---|---|---|---|
| `$00` | Control | b0 gate, b1 repeat, b2 IRQ enable, b5:4 mode, b6 interleave; b3 and b7 ignored | `$00` | Section 1.4 |
| `$01` | Volume | b5:0 (masked, not clamped: 64 becomes 0) | `$00` | gain = v/32; 32 = 0 dB, 63 = +5.9 dB |
| `$02` | Pan | b3:0 | `$08` | Section 1.6 |
| `$03` | - | ignored | | |
| `$04` | Start bits 25:24 | ignored (U64 keeps b1:0; `$01` selects its REU) | | Multitini always plays from the REU (section 7, I5) |
| `$05-$07` | Start 23:0 | 24 | 0 | Live: used at the next logical fetch |
| `$08` | - | ignored | | |
| `$09-$0B` | Length | 24 | 0 | End when position == length; 0 means 2^24 |
| `$0C-$0D` | - | ignored | | |
| `$0E-$0F` | Rate | 16 | 0 | Loaded at each sample boundary (section 1.5.3) |
| `$10` | - | ignored | | |
| `$11-$13` | Repeat A | 24 | 0 | Loop target, relative to start |
| `$14` | - | ignored | | |
| `$15-$17` | Repeat B | 24 | 0 | Loop point, relative to start |
| `$18-$1E` | - | ignored | | |
| `$1F` | IRQ clear | strobe | | bit 0 = 1 clears this voice's flag; the value `$FF` clears all flags |

Offsets and bits kept match upstream [V upstream sampler_regs.vhd:89-163; sampler_pkg.vhd:59-77]. The reset values differ: upstream has none, and its power-up values are vol 32 / pan 8 [V sampler_regs.vhd:33-54]. Section 7 (I4) covers this. The PDF's "defaults" (vol `$3F`, rate 283, ...) are an unused constant [V sampler_pkg.vhd:40-52] and are not implemented.

### 1.3 Reads

| Address | Value | Side effect |
|---|---|---|
| any even address `$DF20-$DFFE` | IRQ status: bit n = flag of voice n (n = 0-6); bit 7 = 0 | none |
| any odd address `$DF21-$DFFF` | `$10` (version 1.0) | none |

- This is the upstream map [V sampler_regs.vhd:81-87]. The PDF documents only `$DF20` and `$DF21`.
- The value is combinational from the flags and is valid within the access cycle. There is never a placeholder value; the U64 bridge can return `$AA` on a late reply [V upstream slot_to_io_bridge.vhd:69-80].
- Registers cannot be read back, as on the U64. An RMW instruction on a UA address writes values derived from the status or version byte, exactly as the C64 bus does on a U64 [I].

### 1.4 Control register (`$00`)

| Bit | Meaning |
|---|---|
| 0 | Gate. 0 to 1 while idle starts the voice from position 0 |
| 1 | Repeat. Loops A to B while gate = 1 |
| 2 | IRQ enable. Sets the voice flag when the end is reached |
| 5:4 | `00` = 8-bit; **any other value = 16-bit little-endian** (upstream; the PDF calls `10`/`11` reserved) |
| 6 | Interleave. Stride 2 (8-bit) or 4 (16-bit), so a partner voice at start+1 (8-bit) or start+2 (16-bit) plays the other channel |
| 3, 7 | ignored |

[V upstream sampler_regs.vhd:91-102; sampler2.vhd:172-198.]

### 1.5 Playback

#### 1.5.1 Voice state machine

The machine is evaluated on every `c64_clk` while the machine is not paused. It is the upstream machine [V sampler2.vhd:128-202] with three changes: the B/length fix (section 7, I1), one period law (I2) and no extra fetch time.

- **IDLE**, gate = 1:
  - position := 0, output := 0, period counter := `2*rate + 3`;
  - logical fetch at `start + 0`; position += stride.
- **PLAY**, at each sample boundary (section 1.5.3):
  - output := the sample fetched at the previous boundary. If it has not arrived, the previous output is kept (section 2.4.5);
  - period counter := `2*rate + 3` (live rate);
  - then:
    - if `position == B` **and gate and repeat**: position := A;
    - else if `position == length`: go to FINISHED, and set the flag if IRQ enable = 1. No fetch;
    - otherwise position is unchanged;
  - if still playing: logical fetch at `start + position`; position += stride.
  - On any clock, gate = 0 **and** repeat = 0 means go to IDLE immediately.
- **FINISHED**: the output holds; gate = 0 goes to IDLE.
- **IDLE** holds the last output until the next start.

Strides: 8-bit 1 (2 with interleave); 16-bit 2 (4 with interleave). The 16-bit sample is bytes `[a, a+1]` = low, high.

Consequences (same as upstream unless noted):
- Bytes 0..L-1 play after one initial silent period. The flag is set when the last sample starts; that sample is then held [I, from our model of upstream sampler2.vhd:128-202].
- The loop plays `[A, B)` while gate = repeat = 1. No IRQ fires while looping.
- Gate = 0 with repeat = 1 ("release") plays on to the length.
  - **Differs from upstream only when B == length:** Multitini ends, while upstream runs on through memory for ever (I1).
- **A new start needs the voice in IDLE.** From PLAY, write gate = 0 **and** repeat = 0 (for example control `$00`). From FINISHED, write gate = 0. Then write gate = 1. Rewriting gate while repeat stays 1 is not a restart: `$03`, `$02`, `$03` keeps the voice in PLAY with no jump back to position 0. There is no gate-edge restart [V upstream sampler2.vhd:129-135, 163-170].
  - Each control write is applied on its own clock. So the MOD player's `$00`-then-value pair, about 1 µs apart under turbo, always restarts [I].
- Length, A and B are compared with equality only. A length or B that is not a multiple of the stride never matches, as upstream. An odd 16-bit length plays for ever, wrapping inside the REU (section 1.5.4). c64cast's moving-length deadline depends on equality, and on the length test running while the loop is taken [M c64cast, sections "The streaming REU ring" and "The length register is a dead-man deadline (#645)"].
- A rate written as two bytes can give one period from a half-written value, as upstream [I].

#### 1.5.2 Sample formats

- 8-bit: a signed byte is the high byte of the 16-bit sample; **the low byte is 0**. Upstream reuses a stale low byte from earlier 16-bit use; the largest difference is 255/65536 of full scale [V sampler2.vhd:124, 218-227].
- 16-bit: signed little-endian.
- Signedness is not in the PDF. Signed is what the hardware does [V upstream sampler_accu.vhd:16, `sample_in : signed(15 downto 0)`] and what working software uses [V Doom tools/mp3topcm.py:52-79]. Users found it the hard way [M 1541ultimate issue #745; Doom tools/mp3topcm.py:55-64].

#### 1.5.3 Period law

```
period   = (2*rate + 3) x 80 ns          (= (rate + 1.5) x 160 ns)
           for 8-bit and 16-bit, with or without interleave
divider  = round((12.5 MHz / f - 3) / 2)  for a wanted rate f
```

- This equals the U64-II 8-bit period, if the C64U sampler clock is 100 MHz [I: two independent visit-level models of upstream sampler2.vhd:128-202 agree; confirmed only when the UA-M0 xsim bench of the exact upstream VHDL (option C) or a C64U measurement (O1) agrees]. Existing 8-bit content would then play at the pitch users hear on a C64U. 16-bit content plays 0.06-0.38 % faster than on a U64-II, and at the same speed as 8-bit (#972 fixed, section 7, I2).
- Software computes dividers in one of two ways, and they differ at 22.05 kHz:
  - **Formula** `rate = round(6.25 MHz / f)`: Doom [V Doom tools/mp3topcm.py:103], the Freshness MOD player [V Freshness periodfix.asm:16-25] and xahmol's MOD library (`period*12500/7094`). On Multitini these play 0.16-1.24 % slow over 8-48 kHz. The U64 (66.67 MHz) and the 16-bit modes are slower still, up to about 1.9 % [I, model; M c64cast measured an effective 6.157-6.164 MHz reference on a U64-II, section "The reality"].
  - **PDF table** [V PDF p.6]: identical to the formula except at 22050 Hz, where the table gives **281** (the formula gives 283). Table users play 22.05 kHz content **0.34 % fast**.

| Wanted | PDF table divider | Plays at (Multitini, = U64-II 8-bit) | Formula divider | Plays at | Best divider | Plays at |
|---|---|---|---|---|---|---|
| 8000 Hz | 781 | 7987 Hz (-0.16 %) | 781 | same | 780 | 7997 Hz |
| 22050 Hz | **281** | 22124 Hz (+0.34 %) | 283 | 21968 Hz (-0.37 %) | 282 | 22046 Hz |
| 44100 Hz | 142 | 43554 Hz (-1.24 %) | 142 | same | 140 | 44170 Hz |
| 48000 Hz | 130 | 47529 Hz (-0.98 %) | 130 | same | 129 | 47893 Hz |

[I, computed from the period law; the UA-M0 model `scripts/ua_model.py` regenerates this table and the committed test checks it.] Rate 0 gives a 240 ns period, rate `$FFFF` gives 10.49 ms. The law is one build parameter, `UA_PERIOD_OFFSET_HALF_TICKS = 3`, so a C64U measurement (O1) can retune it without a redesign.

#### 1.5.4 Addressing

- Logical byte address = `(start[23:0] + position) mod 2^24`, an offset in the 16 MiB REU aperture `0x25000000-0x25FFFFFF` [V multitini_c64_ddr_path.sv:69].
- REU size, the REU bank latch and the REU 512 KiB auto-increment window play no part [V docs/C64_EXPANSIONS.md:52-56]. The UA never uses the REU cache's windowed read-ahead [V multitini_c64_reu_cache.sv:57-59].
- A C64 program can never make the UA read outside the aperture. Start bits 25:24 are ignored (section 7, I5).

### 1.6 Mixing as software sees it

The per-voice law is upstream's [V upstream sampler_accu.vhd:54-84]:
- `scaled = floor(s16 * vol / 32)` (17-bit).
- Pan factors `(pl, pr)`:
  - pan 0-7: `(7, pan)`;
  - pan 8-15: `(15 - pan, 7)`.
  - So pan 7 and pan 8 are both `(7, 7)`: centre is full level on both sides.
- `L = sat21(sum scaled*pl) >> 3` and `R` likewise. One full-scale voice at vol 63 and full pan peaks at 43 % of 18-bit full scale [I, computed].
- Keep this law exactly. Programs set relative voice levels with it. xahmol's `0x80` "centre" stays hard left, as on the U64 [V xahmol include/ultimate_audio_lib.h:74; upstream sampler_regs.vhd:107-108].

Board output (section 2.7):
- UA L/R are added to SID L/R at unity, where 18-bit UA full scale equals 18-bit SID full scale. This is the U64 "0 dB" convention: an 18-bit input times gain `0x80`, then bits 24:1, maps 18-bit full scale to 24-bit full scale [V upstream fpga/io/audio/generic_mixer.vhd:52-67; software/u64/u64_config.cc:303-305, 445-446 (default index 24 = `0x80`)].
- No crossfeed. Saturation, never wrap.
- There is no user mixer setting in the first release (section 6.6).

### 1.7 IRQ

- The flag of voice n is set when voice n enters FINISHED with IRQ enable = 1.
- /IRQ = OR of the flags, ORed with the REU IRQ into the existing `irq_ext_n` [V adapter:534]. It is a level. A program that sets bit 2 without a handler hangs, as on the U64 [M xahmol CHANGELOG 1.4.1].
- Clearing:
  - a write to `$1F` of voice n with bit 0 = 1 clears flag n; the value `$FF` clears all;
  - reads never clear;
  - if a set and a clear land on the same clock, the set wins (upstream [V sampler2.vhd:116-153]);
  - rewriting control does not clear.
- C64 reset and UA inactive clear all flags. Unlike upstream, where disabling the mapping only masks the line and keeps the flags [V upstream slot_to_io_bridge.vhd:51], Multitini holds UA in reset while inactive (section 1.8).

### 1.8 Reset, enable and disable

| Event | Multitini behaviour |
|---|---|
| FPGA configuration, hard PL reset | UA in reset: registers at their reset values (section 1.2), voices IDLE, outputs 0, flags 0, buffers empty |
| C64 reset (`core_reset_n` low) | Same as configuration, except `ua_fault` and the debug fields of `C64_UA_STATUS`, `C64_UA_FETCHES` and `C64_REU_MISSES`. `ua_fault` clears only on hard PL reset; the debug fields clear on hard PL reset and when diagnostics are armed, and read 0 while disarmed (section 6.1). An outstanding DDR read completes and is discarded |
| UA becomes active (option on, which implies the REU on; no CRT; no preload hold) | Leaves reset with all registers at their reset values |
| UA becomes inactive (option off, also when the REU is turned off, because that turns the option off; preload hold; CRT loading or loaded, which cannot happen while UA is on because a CRT load is refused while the REU is on [V firmware/platform/xilinx/c64_content_service.c:334-337]) | Held in reset: voices stop, flags clear, output 0, `$DF20-$DFFF` returns to the REU mirrors (or open bus) |
| REU image preload sets the hold (`C64_REU_HOST` bit 13 = 1, TD §6.4 (a)) | UA becomes inactive as above, while the machine is paused: buffers empty, no new read; a read already in flight completes and is discarded. `ua_held` (`C64_REU_HOST` bit 14) reads 1 once UA is in reset. The machine stays paused for the whole hold, so no CPU access sees the REU mirrors in the window |
| Preload releases the hold (bit 13 = 0) | UA leaves reset with all registers at their reset values (voices idle, flags clear), as after a C64 reset; the C64 then resumes |
| Menu or PRG-install pause (`core_paused`) | The UA timebase freezes: no boundary, no IRQ and no position change during the pause. Fetches already issued complete. Audio is muted while the menu is visible anyway [V hdl/board/multitini_one_top.sv:1162-1166] |
| REU DMA holding the CPU | UA keeps playing; its timing is independent of the CPU |
| UA DDR path fault (RRESP error or wrong RLAST on HP3) | **Local to UA.** UA output is forced to 0, no new read issues, outstanding responses are still absorbed so the path drains, and a sticky `ua_fault` bit is set in `C64_UA_STATUS`. The C64 keeps running; the REU and `reu_fault_latched` are not touched. The bit clears only on hard PL reset (the next core load), because the path's own fault bit is sticky until `c64_aresetn` [V hdl/core/c64/multitini_c64_ddr_path.sv:13-16, 107]. A C64 reset does not re-arm UA after a fault |

Upstream keeps the registers across a C64 reset; its firmware then writes control = 0 and volume = 0 for all voices [V upstream sampler2.vhd:229-233; command_intf.cc:98-108]. Section 7 (I4) discusses the difference. Whether a U64 sampler keeps running while its menu freezes the C64 is [O5]. Freezing is chosen so that playback stays in step with the frozen program.

### 1.9 REU-register mirror changes

- UA off: none.
- UA on:
  - REU decodes only `$DF00-$DF1F`;
  - a read of `$DF20`, `$DF40`, ... no longer reads (or clears) REU status; only `$DF00` does;
  - writes to `$DF21`, `$DF24`, ... no longer reach REU registers;
  - software that uses REU mirrors above `$DF1F` breaks, as on a U64, whose REU never mirrors there [V upstream fpga/cart_slot/vhdl_source/reu.vhd:483-488].
  - This is why G2 is informational (section 0.6). Whether Expand uses these mirrors is O-Expand.
- Today's Multitini REU reads `$10` at `$DF21` after reset (command register) [V multitini_c64_reu.sv:108, 131], so a UA-off machine looks like "version `$10`" at `$DF21`. Detection code also requires `$DF20 == $00` 256 times plus a working probe [V Freshness audio.asm:25-79], so it cannot be fooled. The REU status mirror reads `status | $10`, never `$00` [V multitini_c64_reu.sv:107].
- With UA off, Doom C64U's blind UA writes land on REU mirrors. They are harmless because no write sets command bit 7 and `chainToGame` rewrites every REU register before its own fetch [I, from Doom src/intro/intro.asm:265-266, 329-345 and the REU register map].

### 1.10 Behaviour under the CPU turbo design

Turbo ships in the same release. Its rules for this window are TD §1.7, §2.13.3, §2.13.8 and §5.8. This section restates them, and its text must match TD.

**Access classes** (TD §1.7, §2.13.3, §2.13.8):

| Access | Exact (Turbo Control Off, or any forced-exact cause) | Turbo, badline timing enabled | Turbo, badline timing disabled |
|---|---|---|---|
| `$DF00-$DF1F` REU registers (UA on or off) | native slot, stock | phi2 forward (FWD_IO), never posted, only in BA-high cycles | phi2 forward; completes in BA-low cycles through I/O-only ownership (`cpuIoOwn`) |
| `$DF20-$DFFF`, UA on (UA registers) | native slot | same as above | same as above |
| `$DF20-$DFFF`, UA off, REU on (REU mirrors) | native slot | same | same |
| `$DF20-$DFFF`, UA off, REU off (open bus, `lastVicData`) | native slot | forwarded, never answered locally | forwarded, I/O-only |
| `$FF00` write (REU trigger) | native slot | local RAM write + phi2 snoop forward, BA-high cycles only | local write + snoop, I/O-only |
| REU DMA, including DMA to `$DF00-$DFFF` | native, stock BA rules | native, CPU halted, stock BA rules (TD D16) | same |

- The turbo island map holds no UA state: `$DF20-$DFFF` is a phi2 forward in every UA state. UA needs no logic in the turbo path.
- I/O-only decode: under `cpuIoOwn`, buslogic decodes IOF from `cpuAddr`, so the UA select (`cart_iof` and `addr[7:5] != 0`, section 2.2) works unchanged. `cpu_bus_valid` includes `cpuIoOwn`, so a UA write strobes once. Read data returns through `io_data` and the existing priority chain (TD §2.13.8).
- REU DMA cycles never reach UA in any mode: `ua_io_select` excludes `reu_dma_req`, and `cpu_bus_valid` excludes DMA (sections 1.1, 2.2). Native-slot REU DMA keeps the VICE BA rules in every mode (TD §1.8, D16).
- A cartridge forces exact mode until TD M7, and the REU and UA are off while one is loaded. After TD M7, cartridge I/O is a cartridge forward that never gets I/O-only ownership. UA stays off, because the cartridge still excludes the REU.
- This matches the U64, where the sampler sits behind the cartridge-slot bridge [V upstream slot_server_v4.vhd:946-1008], and where REU, UltiSID and CIA accesses were reported immune to BA with badline timing disabled [M U64 issue #665, as recorded in TD §2.13.8].

**Binding pacing rule:**
- The fast I/O port never serves a CPU access to `$DF00-$DFFF`. UA and REU registers must never move to a fast class. No such access completes less than one native cycle after the previous I/O2 access. Back-to-back accesses complete exactly one phi2 apart while BA stays high, and also in BA-low cycles when badline timing is disabled. With badline timing enabled, BA-low cycles add waits.
- Reason: the common detection routine waits at most 128 status reads for the probe IRQ [V Freshness audio.asm:25-79]:
  - the probe takes 102.4 µs (256 samples at rate 1, section 1.5.3), the same as on a U64-II [I, model];
  - at one access per phi2 the wait budget is about 130 µs (128 x 32 `c64_clk`) [I, computed];
  - at turbo rate it would be about 10 µs, and every detecting program would report "no sampler" [I, computed];
  - I/O-only completion removes badline stalls but never brings the spacing below one phi2, so the budget stays about 130 µs with the screen on or off [I].
- An access in the acknowledge class (below) completes at about fast clock 13 of the next native cycle instead of fast clock 4. The next forward still loads at the following e_15, so the pacing stays one phi2 per access [I, TD §5.9].
- **Enforcement.** The rule will be recorded in the new `docs/C64_ULTIMATE_AUDIO.md`, in `docs/C64_EXPANSIONS.md` and in the new `docs/C64_TURBO.md` (section 11; TD §12). The checks are listed below.

**IRQ-acknowledge class** (TD §5.8). The class is decoded by address only (I/O visible, A15..A8 = `$DF`), whatever the UA state:
- Reads with A4..A0 = 0 (`$DF00` and its mirrors `$DF20`, `$DF40`, ..., `$DFE0`) are REU status reads. With UA on, the mirror addresses are UA status reads. They have no side effect and only complete a few fast clocks later, which lengthens the detection wait slightly [I].
- Writes with A4..A0 = `$1F` and A7..A5 != 0 (`$DF3F`, `$DF5F`, ..., `$DFFF`) are UA IRQ-clear writes. With UA off they are harmless REU mirror writes to an unused register.
- An acknowledge-class forward completes only after the island has registered the c64 marker toggled at e_`IRQ_SAFE_PHASE` (default 5).
- The decode stays address-only, so no UA state enters the island's half-period path. With UA off, a missed REU status acknowledge would be a real fault; with UA on, the cost is a few fast clocks.

**UA IRQ under turbo:**
- The UA end-of-sample IRQ is a level, not an access: `irq_ext_n = !(reu_irq | ua_irq)`, where `ua_irq` is the OR of the voice flags (sections 1.7, 2.2). It enters fpga64's IRQ term the same way as the REU IRQ.
- Only the `$1F` clear writes are acknowledge-class accesses. Reads never clear UA flags.
- A clear write updates the flags at the R of its native cycle, and `irq_ext_n` rises before e_5. The island's next enable comes after the marker, so it sees the released line, and `STA $DF3F / RTI` cannot re-enter the handler [I].
- In turbo the island samples IRQ live at every enable, 2 fast clocks after a change. In exact mode it uses B0, as today. When a flag is set depends only on the UA timebase, never on the CPU speed.

**Checks:**
- Turbo side: TD T3 (TD §10.2) checks every CPU access to `$DF00-$DFFF` at all 16 speed indices, with badline timing on and off and UA on and off. It checks phi2 forwards only (never the fast I/O port), I/O-only completion in BA-low cycles, and the acknowledge rule on `$DF3F` to `$DFFF`. TD E4/T7 (TD §9 R4, §10.1 E4) include the UA clear writes, normal and I/O-only, with UA on and off.
- UA side: T9 (detection replay, section 9) passes at one access per phi2 and fails at turbo-rate spacing by design, which documents the rule.
- Board: X-DET (section 9, UA-M5).

**Timing independence:** the half-tick comes from `c64_clk`, which stays native under turbo (TD §2.2). The turbo CPU clock `c64_cpu_clk` is a second output of the same clock wizard, and nothing UA uses runs on it (section 2.8). Pitch is identical in PAL, NTSC, stock and every turbo speed. Today's PAL MMCM output is -32 ppm from nominal and the turbo plan's is -5.6 ppm (TD §3.1, CLK2). Both are negligible for pitch (0.06 cents at most).

**Real software:** Doom C64U sets `$D031 = $0F` before its UA writes [V Doom src/intro/intro.asm:182, 511-516]. The #744 repro ran at 64 MHz [M U64 issue #744].

**Cartridge turbo** (TD argument 7, milestone TD M7, not in the first release) does not apply; UA is not a cartridge.

### 1.11 Deviations from the U64, at a glance

Section 7 holds the full list with compatibility checks: I1 B == length ends; I2 one period law; I4 full register reset; I5 REU-only addressing; I6 no `$AA`; I7/I8 isolated memory routing and coherence; I9 defined underrun; I11 preload holds UA; I12 timebase frozen in the menu; I14 UA turns the REU on, no CRT; 8-bit low byte = 0; voice 7 absent. The DC hold of a stopped voice is kept while running (section 7, K1). A UA memory fault mutes UA only (section 1.8); the U64 has no equivalent fault path.

---

## 2. Architecture

### 2.1 Block diagram

```
 c64_clk domain (adapter)                                                     PS7
 ----------------------------------------------------------------------------  ------
  T65 / turbo proxy -> cpu_bus_valid, cpu_bus_addr, cpu_bus_wdata, cart_iof
                              |
                +-------------v--------------------------+
                | I/O2 decode (adapter)                  |
                |  ua_window = addr[7:5] != 0            |
                |  UA off: everything to REU (as today)  |
                |  UA on : $DF00-$DF1F REU, rest UA      |
                +-----+----------------------+-----------+
                      |                      |
         +------------v----------+   +-------v-------------------------------------+
         | multitini_c64_reu     |   | multitini_c64_ua                            |
         | (unchanged)           |   |  reg file 7 x 16 B (FF)                     |
         |   |                   |   |  half-tick gen (12.5 MHz, phase acc.)       |
         | multitini_c64_reu_    |   |  voice engines x7 (parallel)                |
         | cache  (unchanged     |   |  fetch unit:                                |
         |  logic; new read-only |-->|   2-word buffer per voice (LUTRAM data)     |
         |  pending-write and    |   |   prediction 1 sample ahead (incl. loop)    |
         |  snoop outputs)       |   |   defer on pending REU write                |
         +-----------+-----------+   |   snoop-invalidate on accepted REU write    |
                     |               |   1 outstanding, min issue gap              |
         ddr_path #1 (as today)      |  mixer (upstream law, 1 voice/clock)        |
                     |               |  boxcar average per 48 kHz period           |
                     |               +----+------------------+--------------+------+
                     |                    |                  |              |
                     |            ddr_path #2 (read-only) ua_irq      ua_l, ua_r (18 b)
                     |                    |                  |              |
                     |                    |      irq_ext_n = !(reu_irq|ua_irq)
                     |                    |                  |   48 kHz point:
                     |                    |                  |   sat24({sid,6'b0}+{ua,6'b0})
                  S_AXI_HP2            S_AXI_HP3 (ACLK = c64_clk, C64 image only)
                     \                  /
                      +---- DDRC ------+     REU aperture 0x25000000-0x25FFFFFF
```

### 2.2 Register file and decode (adapter)

```
ua_window     = cpu_bus_addr[7:5] != 3'b000;
ua_io_select  = ua_active && !reu_dma_req && cart_iof && ua_window;
reu_io_select = reu_active && !reu_dma_req && cart_iof && !(ua_active && ua_window);
io_ext        = cart_io_read || reu_io_select || ua_io_select;
io_data       = ua_io_select  ? ua_rdata :
                reu_io_select ? reu_io_rdata : cart_io_data;
ua_rdata      = cpu_bus_addr[0] ? 8'h10 : {1'b0, ua_flags[6:0]};
ua_write      = cpu_bus_valid && cpu_bus_write && ua_io_select;
                // voice = cpu_bus_addr[7:5] - 1, register = cpu_bus_addr[4:0]
irq_ext_n     = !(reu_irq || ua_irq);   // ua_irq = |ua_flags
```

- With `ua_active = 0` every term reduces to today's expression [V adapter:430, 520-521, 534]. Test T2 confirms this with a trace comparison.
- `ua_io_select` also excludes `reu_dma_req`, so REU DMA never sees UA (section 1.1).
- The SID-mode clamp already rejects MiSTer mode 4 (second SID at `$DF00`) [V hdl/common/multitini_control_regs.sv:220-225]. Keep it. A future `$DF00`/`$DE00` SID must be mutually exclusive with UA (the same conflict with an ARM2SID at `$DF00` was reported upstream [M 1541ultimate issue #124]).
- Registers are flip-flops: 7 voices x about 130 bits. The engines read them in parallel.

### 2.3 Voice engine

- **Half-tick generator** (one for all voices):
  - `acc += 12_500_000`; when `acc >= CORE_CLOCK_HZ`, subtract and emit `half_tick`. At most one per clock, because 12.5 MHz < 31.5 MHz;
  - gated by `!core_paused`;
  - `CORE_CLOCK_HZ` is the adapter's existing constant [V adapter:118]. PAL increments 0.3965 half-ticks per clock, NTSC 0.3819.
  - The average is exact relative to `c64_clk`. Jitter is one `c64_clk` (31.7 ns PAL).
- **Per voice:**
  - state (2 b), position (24 b), period counter (18 b, max 131073), output (16 b), next sample (16 b + valid), logical fetch address (24 b + mode/straddle);
  - on `half_tick` in PLAY: if `counter > 1` decrement, else boundary (section 1.5.1) and reload `2*rate + 3`.
- **Why parallel and not upstream's 8-slot round robin:**
  - `c64_clk/8` = 3.94 MHz is below the 6.25 MHz tick, so the upstream structure cannot be copied (its prescaler only supports 50-125 MHz [V upstream sampler2.vhd:48-60]);
  - parallel evaluation makes every control write an immediate event and costs about 1.1k LUT (section 4).
- **Upstream property kept:** sample timing never waits for memory. A late sample replays the previous value [V upstream sampler2.vhd:141-143, 218-227].

### 2.4 Sample fetch

#### 2.4.1 Port

- A second instance of `multitini_c64_ddr_path`, unmodified, on **S_AXI_HP3**, used read-only:
  - `core_memory_write = 0`, `burst = 0`: single 8-byte beats;
  - `core_memory_busy = owed || request_valid`.
- It reuses the reviewed orphan-drain and quiescence logic [V multitini_c64_ddr_path.sv:17-38].
- HP3 is enabled in every image and tied off unless the machine is the IIgs [V scripts/create_project.tcl:205-206, 383-393; multitini_one_top.sv:2001-2006]. The C64 change must split that `generate` block (section 11).
- In the C64 image only, HP3's ACLK moves from FCLK0 to `c64_wiz/clk_out1` and `S_AXI_HP3_0` joins the `c64_clk` ASSOCIATED_BUSIF. This is the HP2 pattern [V create_project.tcl:409-419, 548-559]. PS7 settings do not change, so `ps7_init.c` stays identical, which packaging verifies [V create_project.tcl:551-553].
- Precedent: the IIgs already runs HP2 (machine RAM) and HP3 (frame ring) at the same time, "so frame DMA cannot block a CPU transaction in a fabric arbiter" [V create_project.tcl:383-386].

#### 2.4.2 Buffers

- Per voice: 2 entries, each a 64-bit word plus a 21-bit word tag (`addr[23:3]`) and a valid bit.
- Data in LUTRAM (14 x 64 b), tags in flip-flops so that snoop and lookup compares run in parallel. No BRAM.

#### 2.4.3 Logical fetch and lookup

- At each boundary the engine produces the logical address `a`.
  - 8-bit needs `word(a)`.
  - 16-bit needs `word(a)` and `word(a+1)`. These differ only when `a mod 8 = 7`, possible with an odd start; then both entries are used.
- If resident, the sample is latched into `next` on that clock, **unless the word's tag matches the registered snoop (section 2.5, rule 2)**. That match is treated as a miss: the entry is about to be invalidated for a write accepted on the previous clock. Otherwise, or on that match, a **demand** request is raised, which rule 1 then defers until the write is visible. The sample is latched when the data arrives, if that is before the next boundary.
- This matches upstream's one-period budget per sample, where the fetch is issued at the boundary and must land before the next one [V sampler2.vhd:141-198].

#### 2.4.4 Prediction

- After a logical fetch, the next fetch address is predicted from live registers: `start + ((position == B && gate && repeat) ? A : position)`. If its word is not resident, a **predict** request is raised.
- If it is resident, the following sequential word is requested as **lookahead** into the other entry, unless that entry is pinned by a pending demand or straddle.
- Sequential streams therefore get up to 8 samples of slack, loop jumps at least one period.
- A register write that changes the address only costs a demand miss. Correctness never depends on the prediction [I].

#### 2.4.5 Issue

- One UA read outstanding at a time, a single 8-byte beat.
- Candidate priority: demand > predict > lookahead; round robin within a class.
- At least `UA_MIN_ISSUE_GAP` clocks between issues (default 32, one per phi2; this bounds HP3 load to 0.985 M reads/s).
- Each issue records `{voice, entry, tag}` in one owner register. A response always belongs to that owner, so no tag space is shared with any other master (I7).
- **Underrun:**
  - if a sample is not latched by the boundary, the previous output repeats, the position still advances, and the sticky `underrun` flag and count advance while diagnostics are armed (debug fields, section 6.1);
  - end timing and IRQ timing never depend on memory;
  - at normal rates (period at least 10 µs) underrun needs DDR latency above one period;
  - at degenerate rates (rate below about 12, over 500 kHz) underrun is expected, as on the U64, but defined.
- **Reset and disable:**
  - buffers, owner-valid and pending demands clear;
  - the `owed` bit survives soft reset and disable, like the REU's `memory_pending` [V multitini_c64_reu.sv:124-126];
  - a response that arrives for a cleared owner is discarded;
  - no new issue while `owed`.
- **Fault (local policy):**
  - a path fault (RRESP error, wrong RLAST) latches `ua_fault` in the UA. `ua_fault` is a user-facing bit: it stays live with or without a debug session;
  - UA output is forced to 0 and no further reads issue. The fetch unit keeps absorbing any response still owed, so the path drains and DDR quiescence still completes;
  - `ua_fault` is reported only through `C64_UA_STATUS` (section 6.1). It is **not** ORed into `reu_fault_latched`. That latch feeds `fatal_load_error`, which holds `core_reset_n` low and drops `ready` [V adapter:297-309, 431-434], so an audio fault would stop the whole machine and be reported as a REU fault;
  - `ua_fault` clears only on hard PL reset, matching the sticky path fault [V multitini_c64_ddr_path.sv:107]. Firmware shows it on the Options row (section 6.2).

### 2.5 Coherence with the REU cache

**Facts [V multitini_c64_reu_cache.sv]:**
- Write-through with a 4-entry merging 8-byte posted queue (:45-50).
- A queued write leaves the queue when it is issued, and is "in flight" (`state != IDLE && memory_write`) until its B response returns (:147-152, 158).
- No line is ever dirty. DDR plus the queue plus the in-flight write is the whole truth.
- A partial youngest word stays queued until it is full, another word queues, `flush` or `!enable` (:147). `flush = !reu_busy` [V adapter:454].

The queue, the in-flight write and the registered snoop cover every accepted write: a queued word moves to in-flight on the same edge that removes it from the queue, so there is no clock in which it is in neither set [V multitini_c64_reu_cache.sv:137-158]. HP2 writes use AWCACHE = 0 (non-bufferable), so the B response comes from the endpoint [V hdl/board/multitini_one_top.sv:509-510].

**Rule (coherence) [I]:**
1. **Defer.** A UA read of word W may issue only if W matches none of:
   - the valid queue entries (`wb_addr[head..head+count-1]`);
   - the in-flight write's `memory_addr[23:3]`;
   - the registered snoop (rule 2) of the last clock;
   - the **post-B hold** set: the words of REU writes whose B response arrived less than `UA_POST_B_HOLD` clocks ago (default **16**). Writes complete one at a time, so the set needs one entry per write that can finish inside 16 clocks: 2 entries if a DDR write takes at least 8 clocks [I]. T5 checks the boundary.

   A match skips the candidate this clock. The read therefore reaches DDR only after every earlier accepted REU write to W has received its B response, plus the hold.
2. **Snoop.** Register `accept_write` and `request_addr[23:3]` from the cache request port (one clock late). A match:
   - invalidates matching resident entries in all voices on the next edge;
   - blocks a resident hit on the same clock (section 2.4.3), which closes the one-clock window between the registered snoop and the invalidation;
   - marks a matching in-flight UA read stale, so that its data is discarded and the word is requested again (and then deferred until the write drains).
3. A sample already latched into `next` before the write was accepted, or on the same clock, stays. That is a legal ordering: the read happened first.

- Together these guarantee that every byte the engine consumes equals REU memory at some moment after the last REU write accepted **before the clock of** the logical fetch. This is the U64's single-SDRAM ordering [V upstream fpga/cart_slot/vhdl_source/slot_server_v4.vhd:1144-1160].
- **Cache change:** additive read-only outputs only: `pending_addr[0..3]`, `pending_valid[3:0]` (from head/count), `inflight_write_addr`, `inflight_write_valid`, a `write_done` pulse (B received for a write), and the request-port snoop signals already present at the module boundary. The cache's logic and timing do not change; `tb_c64_reu_cache` must pass unchanged. The turbo design adds one input to the same cache, `invalidate`, for REU image preload (TD §6.4 (a)); it stays idle unless firmware preloads. Both additions are benched together (X-T4, section 9).
- **Cost of a defer:**
  - incrementing REU address (the normal case): the matching word drains within at most 5 DDR transactions plus the partial-word linger, about 8 phi2 (about 8 µs) during a streaming stash, because a word fills one byte per phi2 [I]; then the 16-clock hold (0.5 µs);
  - **fixed REU address** (`$DF0A` bit 6): every byte merges into the same young queue entry, its strobe never fills, and the cache issues no read-ahead during a stash, so the entry drains only when the command ends (`flush = !reu_busy`) [V multitini_c64_reu.sv:92, 271-272; multitini_c64_reu_cache.sv:52, 147; adapter:454]. A UA read of that one word is deferred for the **whole command**, up to about 66 ms PAL for 64 KiB. The voice repeats its last sample (defined underrun) and recovers within one period after the drain. A fixed-address swap behaves the same once its line is resident [I];
  - the per-voice slack is at least one period (20.8 µs at 48 kHz), and 8 periods for sequential streams;
  - only a program that stashes or swaps into the 8-byte word being played can hit a defer. No known UA program does that with a fixed REU address. A cache input that drains the young entry on a UA defer would remove the long case, but it changes cache logic and would put the cache back under the Expand gate, so it is not planned.
- **Cross-port ordering [I]:** a write acknowledged on HP2 must be visible to a later HP3 read.
  - The Zynq DDRC is expected to order a read after an acknowledged write to the same address, even from a different HP port. The REU already relies on the same-port equivalent [V cache:3-6].
  - Correctness does not rest on that alone: the default `UA_POST_B_HOLD` = 16 clocks (about 0.5 µs) also covers a write that is acknowledged before it becomes visible to the other port, as long as it becomes visible within the hold. It costs time only on a defer.
  - **[O2]** remains open as characterisation (G9, optional). The parameter can be raised without a redesign.
- **PS writes** into the aperture (REU image preload) are not snooped. The preload therefore holds UA with `C64_REU_HOST` bit 13 and waits for bit 14 (UA held) before it copies, invalidates the REU cache after the copy, and releases the hold last (TD §6.4 (a); section 6.7; section 7, I11).

### 2.6 Arbitration with REU DMA and HP2

- The REU path is unchanged: same HP2 master, same cache, same single-transaction rule [V cache:3-6]. UA adds no PL-side arbiter in front of it.
- Contention exists only inside the PS:
  - HP2 and HP3 are expected to share one DDRC port through the memory switch [I, Zynq-7000 TRM; O13];
  - with at most one single-beat UA read outstanding, a REU transaction can wait behind **at most one UA beat** at the DDRC, which is tens of nanoseconds plus a possible page miss [I];
  - the REU qualification budget is 64 `c64_clk` (2.03 µs) per DDR transaction [V docs/C64_EXPANSIONS.md:93-99].
- Knobs, all build parameters, for the case that O7 shows any REU impact:
  - `UA_MIN_ISSUE_GAP` (default 32);
  - `UA_YIELD_TO_REU` (default 0): when 1, the UA does not issue while the REU cache is between SEND and response or has a request pending;
  - the Zynq DDRC per-port priority, which would be a PS7 change (avoid, because `ps7_init.c` must stay identical).
- **Rejected alternative:** sharing HP2 below the cache through a PL arbiter. It can delay a REU fill by a whole UA transaction (about 15-25 clocks) inside a 64-clock budget that already fails at 90 clocks on a cold bank change [V docs/C64_EXPANSIONS.md:93-99]. It stays the fallback if HP3 proves unusable.

### 2.7 Mixer, output rate, stereo, volume/pan, clipping

- **Voice mix:**
  - every 8 clocks, the mixer visits voices 0-6 (one per clock) and computes the section 1.6 law;
  - the first voice initialises the accumulators, the others add with 21-bit saturation (upstream `sum_limit` [V upstream my_math_pkg.vhd:20-33]);
  - `mix_L/R = acc >> 3` (18-bit), updated at 3.94 MHz PAL / 4.09 MHz NTSC.
  - It uses one 16x6 multiplier (DSP48 or LUTs); the pan multiplies by 0-7 are shift-adds.
- **Anti-alias and rate change:**
  - every mix strobe adds `mix_L/R` into a 25-bit accumulator;
  - at the adapter's 48 kHz tick [V adapter:663] the sum is latched and cleared;
  - over the next clocks the latched sum is divided by its exact strobe count (82/83 PAL, 85/86 NTSC) through a small reciprocal table and one DSP multiply;
  - the result is used at the next 48 kHz tick, a fixed 20.8 µs UA latency.
  - This replaces plain point sampling of a stepped signal with a boxcar average, which removes most of the nearest-neighbour resampling distortion when 22.05/44.1 kHz content plays into a 48 kHz output.
  - It is weaker than the U64 chain (state-variable low-pass, then resampler [V upstream lp_filter.vhd:22, 74-75; u2p_riscv.vhd:299, 812-813]). That filter cannot be imported (section 5). T13 checks that the implementation matches the analytic boxcar response; quality against the U64 stays open (R6).
- **Board mix:**
  ```
  audio_l <= sat24($signed({raw_audio_l, 6'b000000}) + $signed({ua_l, 6'b000000}));   // R likewise
  ```
  - The SID term keeps its exact current text, `$signed({raw_audio_l, 6'b000000})` [V adapter:667-668], because `tests/test_audio_path.py` asserts that literal [V tests/test_audio_path.py:40-41]. The existing asserts then pass unchanged; new asserts cover the saturating sum and UA = 0 when inactive (T10f).
  - Unity gain. Hard left/right (Doom pans voice 0 hard left and voice 1 hard right [V Doom src/intro/intro.asm:265-266]). In single-SID mode SID is on both sides and UA stays stereo.
  - Saturate, never wrap (#744 also reports wrap above full scale on the U64 [M U64 issue #744]).
  - A sticky `clip` flag is kept for diagnostics (a debug field, section 6.1).
  - UA inactive forces `ua_l = ua_r = 0`, so the SID samples are bit-identical to today (test T7).
  - The summed sample feeds S/PDIF and HDMI audio through the existing `machine_audio_to_control` handshake, and the debug audio capture tap, which records it before the UI mute (TD §6.4 (c)). Turbo adds no logic between the 48 kHz point and the sum, or after it.
- **Relative loudness:** the MiSTer SID and UltiSID scales are not known to match. The UA/SID ratio on the U64 at default mixer settings is [O3]. Change the unity constant only after that measurement.

### 2.8 Clock domains

All UA logic, including the HP3 AXI master, runs on `c64_clk` (PAL 31.527954 MHz, NTSC 32.727266 MHz [V create_project.tcl:52-60]). The domain has +7.052 ns WNS in c705r2 [V c705r2 timing_summary.rpt:206].

Turbo adds `c64_cpu_clk` on `clk_out2` of the same clock wizard (TD §3.1). UA and HP3 stay on `clk_out1` (`c64_clk`). Nothing UA uses runs on `c64_cpu_clk`.

Slack elsewhere in the same build [V c705r2 timing_summary.rpt:202, 207; the video figure also docs/C64_VICE310_COMPARISON.md:51]:
- the 148.5 MHz video clock (`clk_out1_zynq_ps_bd_clk_wiz_video_0`) has the least, **+0.133 ns**;
- `control_clk` (`clk_fpga_0`) has +0.446 ns.

UA puts no logic in the video domain. It adds to `control_clk` only the 1-bit config widening, the destination side of the status mailbox, three read-mux entries and one AND gate (section 3). About 2.2-2.6k LUT and 2.6k FF of new `c64_clk` logic plus the HP3 AFI connection can still move placement around the video and display logic. The turbo island lands in the same build (section 4.1). Both tight domains are therefore watched (R5).

---

## 3. Clocks, resets and crossings review

| # | Item | Kind | Treatment | Evidence |
|---|---|---|---|---|
| C1 | No new clock | - | UA uses `c64_clk` only | [I] |
| C2 | HP3 ACLK: FCLK0 to `c64_clk` in the C64 image only | changed clock on a PS port | Same as HP2 today. The PS AFI does the ACLK-to-DDR crossing. ASSOCIATED_BUSIF and ASSOCIATED_RESET (`c64_aresetn`) for `S_AXI_HP3_0` in the C64 branch only. IIgs keeps HP3 on FCLK0. `ps7_init.c` hash unchanged. HP3 must stay on `clk_out1`; turbo's `c64_cpu_clk` is `clk_out2` of the same wizard (TD §3.1) | [V create_project.tcl:409-419, 514-519, 548-559] |
| C3 | Expansion config bit 4 (control to c64) | existing crossing widened | `multitini_async_mailbox` WIDTH 4 to 5. The `changed` compare and the default word cover bit 4 (default 0). Same latest-value semantics and `DRAIN_ON_RESET` | [V hdl/core/c64/multitini_c64_expansion_config.sv:10-30] |
| C4 | HP3 path quiescence (c64 to control) | existing reviewed crossing, **second instance: a second synchronizer that must be reviewed** | The level crosses inside `multitini_c64_ddr_path` (`cdc_bit_sync` plus quiet counter). The two control-domain results are ANDed into `VIDEO_FRAME_STATUS.QUIESCENT` | [V multitini_c64_ddr_path.sv:30-38; top:822-827] |
| C5 | `C64_UA_STATUS`, `C64_UA_FETCHES`, `C64_REU_MISSES` snapshot (c64 to control) | **new crossing, mandatory** (needed for fault reporting and gate G3) | One `multitini_async_mailbox` carries the whole snapshot coherently, sent whenever it differs from the last one sent, as the media-state mailbox does. `ua_fault` is always live. The debug fields (section 6.1) count only while the C64 diagnostics armed level (C12) is 1; they hold 0 while it is 0, and its rising edge clears them. The gating is on the c64 side, so the snapshot carries zeros while disarmed. Counters wrap or saturate, software never writes them and no read clears them, so there is no reverse crossing beyond the existing TD X3 level; firmware takes differences within one armed period. Destination registers are read in the top-level C64 read override, C64 image only | [V top:1038-1065 (pattern); multitini_async_mailbox.sv] |
| C6 | Reset of UA registers and engines | synchronous, c64 domain | `reset \|\| !core_reset_n \|\| !ua_active`, the same structure as the REU (`reset \|\| !core_reset_n`, `enable = reu_active`). `ua_active` includes the preload hold term `!ua_hold` (section 1.1). `ua_fault` resets on `reset` (hard PL reset) only. The debug fields reset on `reset` and are held at 0 while diagnostics are disarmed | [V adapter:436] |
| C7 | Fetch `owed` and HP3 path | survives soft reset | Only hard PL reset (`reset`) clears `owed`. `c64_aresetn` handling of orphans is inside `ddr_path` | [V multitini_c64_ddr_path.sv:17-29] |
| C8 | Timing rules | none new expected | No new XDC. In the combined image the CDC gate must report exactly C3-C5 here plus the turbo crossings of TD §3.2 (X1-X12), and no other unexpected critical crossing. The existing first-stage false-path pattern names `sync_meta_reg` and `dst_sync_reg[0]`; confirm in the CDC report that the new mailbox and synchronizer cells match it | [V scripts/build_and_export_xsa.tcl:484-520; hdl/board/multitini_timing.xdc:70-73] |
| C9 | Combinational path from cache to UA | intra-domain | The snoop is registered in the UA (one clock late). This keeps the cache's `request_ready` cone out of UA logic. The pending-address compare uses cache registers only | [I] |
| C10 | Pause | intra-domain | `core_paused` gates the half-tick | [V adapter:505 `.pause_out(core_paused)`] |
| C11 | UA hold in, UA held out (`C64_REU_HOST` bits 13 and 14) | turbo-owned mailbox, **no new crossing** | The control-to-c64 level word of TD X11 carries {invalidate request counter, UA hold}; the c64-to-control word carries {done counter, REU idle, UA held}. Both are level words with counters, never strobes. UA consumes the c64-side hold level in `ua_active` and drives `ua_held = ua_hold & ua_block_in_reset`, registered. Reviewed together with TD §3.2. Resets to 0 (TD RST4) | TD §3.2, §6.1 |
| C12 | C64 diagnostics armed (`C64_CPU_CONFIG` bit 19) | turbo-owned mailbox, **no new crossing** | The bit crosses on TD X3 with the rest of the CPU config word. The adapter feeds the same c64-side level to `turbo_regs` and to the UA block. It resets to 0 and is not an apply event. Reviewed together with TD §3.2 | TD §3.2, §6.1 |

Every item above must be reviewed at the RTL review gate (AGENTS.md "Review every new clock, reset, crossing, and timing rule").

---

## 4. Resource and bandwidth budget

### 4.1 Resources (xc7z020; c705r2 PAL utilisation [V c705r2 utilization.rpt:25])

Used 24,763 LUT, 22,998 FF, 114 RAMB36 + 17 RAMB18 (122.5 RAMB36 equivalents), 26 DSP. Free before turbo: about 28.4k LUT, 83.4k FF, **17.5 RAMB36**, 194 DSP. BRAM is the scarce resource. **UA uses no BRAM.**

Turbo lands in the same build. Combined figures [I until the TD M1 and UA-M4 reports]: turbo adds about 1,510 LUT, 885 FF and 5.5 RAMB36 (TD §4.3), and UA adds the figures below. Together: about 28,500-28,900 LUT (54 %), 26,500 FF (25 %), 128.0 of 140 RAMB36 (91 %) and 27-29 DSP. The two tight timing domains are the same as before (section 2.8, R5).

| Block | LUT [I] | FF [I] | DSP |
|---|---|---|---|
| Register file 7 x 130 b | 150 (write decode) | 910 | 0 |
| Voice engines x7 (24-b position, 18-b counter, 2 x 24-b equality, 24-b adder, control) | 1,050 | 560 | 0 |
| Half-tick generator | 40 | 30 | 0 |
| Buffers: 14 x 64 b LUTRAM, 14 tags | 60 + 60 | 340 | 0 |
| Defer and snoop compares (6 + 14 x 21 b) | 120 | 30 | 0 |
| Issue, owner, demand/predict/lookahead select | 200 | 80 | 0 |
| Mixer and boxcar | 200 | 150 | 1-3 |
| Second `ddr_path` (figures measured for the HP2 instance [V c705r2 utilization.rpt:128]) | 44 | 181 | 0 |
| Decode, IRQ, audio sum | 60 | 20 | 0 |
| Post-B hold, fault latch | 30 | 50 | 0 |
| Status snapshot: flags, playing bits, counters (c64 side) with the armed gate, mailbox, REU-miss counter, control-side registers | 150 | 250 | 0 |
| **Total** | **about 2.2-2.6k** | **about 2.6k** | **1-3** |

For comparison, the REU cache is 1,883 LUT / 1,575 FF [V c705r2 utilization.rpt:112].

### 4.2 Bandwidth (PAL; [I, computed]; `scripts/ua_model.py` regenerates these figures in UA-M0)

| Client | DDR transactions/s | Clocks between | HP3 occupancy at 0.5 µs/beat [I] |
|---|---|---|---|
| REU stash (1 byte/phi2, 8-byte merge) | 123,156 writes (HP2) | 256 | - |
| REU fetch/verify (32-byte bursts) | 30,789 reads (HP2) | 1,024 | - |
| UA 7 voices 8-bit 48 kHz | 42,000 | 751 | 2.1 % |
| UA 7 voices 16-bit 48 kHz | 84,000 | 375 | 4.2 % |
| UA 7 voices 16-bit interleaved 48 kHz (worst legal 48 kHz case) | 168,000 | 188 | 8.4 % |
| UA cap (`UA_MIN_ISSUE_GAP` = 32) | at most 985,249 | 32 | at most 49 % |
| Detection probe (1 voice, rate 1, 8-bit) | 312,500 | 101 | about 16 % for 102 µs |
| Doom intro (2 voices, 8-bit 22.05 kHz) | 5,513 | 5,719 | 0.3 % |

- Upstream issues one byte per request [V upstream sampler2.vhd:264]. The word fetch cuts the request count by 8x (8-bit) or 4x (16-bit).
- The DDR latency used for occupancy (0.3-0.5 µs per beat) is an estimate [I]. The REU's 64-clock budget suggests the same order.

---

## 5. Licence and source decision

| Option | Licence | Fit | Decision |
|---|---|---|---|
| A. Import `sampler2/sampler_regs/sampler_pkg/sampler_accu.vhd` (+ helpers) into the product | GPL-3.0: the upstream `LICENSE.txt` is the unmodified GPLv3 text and the four sampler files have no per-file header [V upstream LICENSE.txt; sampler2.vhd:1-5] | Licence-compatible with GPL-3.0-only Multitini [V MULTITINI_ONE_PLAN.md:129-134]. Technically unfit: the 8-slot round robin needs `clk/8 >= 6.25 MHz` and a 50-125 MHz prescaler [V sampler2.vhd:48-63]; it has a byte-wide tagged memory bus, a slot-bridge I/O bus and the #972 and B == length behaviour | Rejected for the product |
| B. Original SystemVerilog from the documented interface and the verified upstream behaviour | GPL-3.0-only (SPDX header), with an attribution comment naming the upstream files and commit | Fits `c64_clk`, the DDR path and the adapter | **Chosen for the product** |
| C. Test-only exact import under `third_party/ultimate_reference/upstream/`, excluded from product builds | GPL-3.0; upstream `LICENSE.txt` copied | The only independent check of the "upstream quirks" model on which the period law and the I1/I2/I4 compatibility claims rest. A mixed-language xsim bench runs the exact upstream VHDL. Follows the `vice_reference` precedent [V licenses/files.csv:619-624; third_party/vice_reference/NOTICE.md, import_manifest.json] | **Mandatory, milestone UA-M0** |
| Never import | `lp_filter.vhd`/`lp_filter2.vhd`: "not supposed to be used in other projects without written permission" [V upstream fpga/io/audio/lp_filter.vhd:3-10]. The v0.2 PDF/docx: "All rights reserved" (cite its URL; restate facts in our own words). `ModPlayer_16k/module.bin`: binary without source. `slot_to_io_bridge.vhd`: not needed | - | - |

**Option C file set.** The four sampler files do not compile alone [V upstream sampler2.vhd:5-7, sampler_regs.vhd:5-6, sampler_accu.vhd:5-8, srl_fifo-generic.vhd:40]. The minimum set is:
- `fpga/io/sampler/vhdl_source/{sampler2, sampler_regs, sampler_pkg, sampler_accu}.vhd` (no file header);
- `fpga/io/sigma_delta_dac/vhdl_source/my_math_pkg.vhd`;
- `fpga/ip/srl_fifo/vhdl_source/srl_fifo-generic.vhd` (header "(C) COPYRIGHT 2022, Gideon's Logic B.V." with no licence text, so its row states that it relies on the repository GPL-3.0 [V upstream srl_fifo-generic.vhd:1-12]) and the `sync_fifo` it instantiates, with that file's own dependencies;
- `fpga/ip/busses/vhdl_source/{io_bus_pkg, mem_bus_pkg}.vhd`;
- `LICENSE.txt`.

The import step must confirm the set with an xsim compile and list any further transitive file. No file with the `lp_filter` notice may enter the set.

Per-file handling:
- `licenses/files.csv`: one row per new file.
  - Original files: path; origin "Multitini One; behaviour modelled on 1541ultimate fea2798c sampler2/sampler_regs/sampler_accu (GPL-3.0)"; GPL-3.0-only; note "no upstream text copied".
  - Option C files: "exact-import", "Unmodified test-only reference; excluded from product builds", and a note per file: "no file header; repository GPL-3.0" or "copyright-only header; repository GPL-3.0".
- `third_party/ultimate_reference/NOTICE.md` and `import_manifest.json`, as in `vice_reference`.
- `provenance/imports.lock.json`: a digest entry for every option C file. `scripts/check_repo.py`: add the prefix `third_party/ultimate_reference/upstream/` to `tracked_imports` [V scripts/check_repo.py:198-207], or the new files escape the import-lock check.
- `provenance/sources.lock.json`: pin 1541ultimate at `fea2798c...` (behavioural reference and option C import).
- Third-party test programs and content stay out of Git. Doom C64U is CC BY-NC-SA 4.0 and contains id Software-derived data and Bobby Prince's music [V Doom ATTRIBUTION.md:5, 13, 38-42]. The Freshness player, the synth, BreadAmp, the Pastebin example and `ua_972_check` carry no licence. All are local inputs under `build/`.
- Test fixtures are synthetic tones generated by our own scripts. The Juhani Junkala WAV set in u64-bbcbasic is reported as CC0 [I, from a background note; check `demos/audio/CREDITS.md` before any use].
- Our board PRGs are GPL-3.0-only sources in `c64ua/` (section 11), built to `build/`, never committed as `.prg`.

---

## 6. Firmware, menu, ABI and settings

### 6.1 Registers and ABI

The ABI step, the gating rule and the whole register map 0x0D0-0x13C are owned by TD §6.0-6.1. This section states the UA registers and bits in full and the shared items in one line each.

| Item | Change |
|---|---|
| `C64_EXPANSION_CONFIG` @0x0D0 | Bit 4 = Ultimate Audio enable. Firmware sets it only together with bit 3 (REU enable), in one register write (section 6.3). The HDL also ANDs it with the effective REU state, the cartridge state and the preload hold (section 1.1). `MT_C64_EXPANSION_CONFIG_MASK` 0x0F to 0x1F. Default and reset value stay 0x0B (dual SID at `$D500`, REU on, UA off) [V firmware/include/multitini_regs.h:194-197]. Bits 31:5 reserved, read 0. The value crosses to `c64_clk` in the existing mailbox, widened to 5 bits (C3) |
| Clamp | `clamp_c64_expansion` keeps today's SID rule [V multitini_control_regs.sv:220-225] and clears bit 4 unless caps bit 3 is set **and** the written bit 3 is 1. The register therefore can never read back "UA on, REU off", even with a firmware bug or old firmware |
| `C64_EXPANSION_CAPS` @0x0D4 | Bit 3 = `ULTIMATE_AUDIO` (`MT_C64_EXPANSION_CAPS_ULTIMATE_AUDIO` = 0x08). Bit 4 = `REU_HOST_LOAD` (REU image preload, TD §6.1). Top parameter `32'h80000007` to `32'h8000001F` in the C64 image [V top:736], 0 elsewhere. Older images report 0x80000007 [V], so the bit is a safe gate |
| ABI | One minor step, 0.23 to 0.24, shared with turbo, REU image preload and the debug registers (TD §6.0). `MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR` = 24 gates config bit 4, 0x0F0-0x0F8 and the Options row. Firmware checks the package ABI, then the live `ABI_VERSION`, and only then reads the caps (TD §6.0; pattern of [V firmware/platform/xilinx/c64_content_service.c:33-41]). It never touches a UA register before the first two checks pass. `MT_C64_EXPANSION_MIN_ABI_MINOR` stays 23 for the SID and REU rows [V firmware/include/multitini_regs.h:189] |
| `C64_UA_STATUS` @0x0F0 (RO, **mandatory**; reads 0 without caps bit 3) | Bit 0 `ua_fault` (**user**): sticky; set by an HP3 RRESP error or a wrong RLAST; only hard PL reset (the next core load) clears it; live whether or not diagnostics are armed; drives the menu "Fault" state (section 1.8). Bits 1-3 (**debug**): underrun seen, clip seen, defer seen, sticky while armed. Bits 7:4 reserved, 0. Bits 14:8 `playing[6:0]` (**debug**): voice is in PLAY; read 0 while disarmed. Bit 15 reserved, 0. Bits 31:16 underrun count (**debug**): saturating; counts while armed |
| `C64_UA_FETCHES` @0x0F4 (RO, **debug**) | 32-bit wrapping count of UA HP3 reads issued, counted while armed. A count that keeps advancing proves the voices are still being served |
| `C64_REU_MISSES` @0x0F8 (RO, **debug**) | Bits 15:0: saturating count of **REU readiness misses**: phase-15 DMA slot samples at which the REU owns DMA (`dma_req`) but `dma_valid` is low because the REU is in a DDR request or wait state (`RAM_*_REQUEST/WAIT`, `TAIL_READ_*`) [V hdl/core/c64/ports/fpga64_sid_iec.vhd:468-470; multitini_c64_reu.sv:56-59, 76-77]. Slots lost to BA are not counted. Counts while armed, with UA on or off. Bits 31:16 read 0. Observation only: one strobe output from the fpga64 port and one state output from the REU, no logic change. TD §1.8 and §12 list both; the existing REU suites, T2 (X-T3) and G-REU0 check them. Present whenever caps bit 3 is set, so G-REU0 and G3 run on the same image |
| Debug gating | `C64_CPU_CONFIG` bit 19, "C64 diagnostics armed" (owned by TD §6.1), gates every debug field above together with the turbo diagnostic status bits. While it is 0 the fields hold 0 and read 0. Its rising edge clears them. It changes no behaviour and is not an apply event. It resets to 0, only `:c64diag on` inside a UART debug session sets it, and it is never persisted (TD §6.5). No status register clears on read, and software never writes them |
| `C64_REU_HOST` @0x110 (owned by TD §6.1) | UA uses bit 13 (UA hold, written by the preload) in `ua_active` and drives bit 14 (UA held, read): 1 once the c64 side has applied the hold and UA is in reset; reads 0 without caps bit 3 (C11) |
| Offsets | 0x0F0-0x0F8 are confirmed by TD §6.0. Turbo uses 0x0D8-0x0E4 and keeps 0x0E8-0x0EC for growth. 0x0FC is reserved for the optional G9 probe register: decoded only in a diagnostic build with `UA_COHERENCE_PROBE` = 1, never in a release image. The debug and host registers use 0x100-0x110. Firmware never accesses a reserved or free offset |
| Spec and generator | One spec edit for the whole release (TD §6.0) adds the UA items with the rest: `spec/interface_v0.json` CONFIG/CAPS descriptions, the three status registers and the constants; then regenerate `multitini_regs_pkg.sv` and `multitini_regs.h` with `scripts/generate_registers.py` |

### 6.2 Options row (binding user requirement)

| Field | Value |
|---|---|
| Location | `build_c64()` in `firmware/common/core_frontend.c`, a new `frontend->core_options[]` entry directly after "RAM expansion" [V core_frontend.c:62, 135-145]. It is screen order 11 of the full C64 order, which TD §6.2 owns |
| Label | `Ultimate Audio $DF20-$DFFF` (26 characters; `LABEL_W` = 35 [V firmware/common/menu.c:14]) |
| Control | `MT_MENU_CONTROL_CHECKBOX`; `control_value` = readback of `C64_EXPANSION_CONFIG` bit 4 |
| Action | `MT_MENU_ACTION_TOGGLE_CORE_OPTION`, **argument 8** |
| Why 8 | 0-3 are in use (Fast serial ROMs 0, CPU turbo 1, SID audio 2, RAM expansion 3 [V core_frontend.c:117-145]). Turbo uses 4-7, 9 and 10 and routes them ahead of the `file_index >= 2` expansion branch [V main.c:4505-4508]. Argument 11 is reserved for a possible UA level row (section 6.6). Arguments 2, 3, 8 and 11 reach `change_c64_expansion()` (TD §6.2). Arguments are semantic, not row indexes [V tests/host/test_core_frontend.c:157-158]. The first release shows 9 C64 rows. With TD M7, TD M9 and the level row it would show 12, which equals `MT_MENU_MAX_CORE_OPTIONS` [V firmware/include/multitini_menu.h:18] |
| Shown when | `expansion_valid && reu_supported && (caps & ULTIMATE_AUDIO)`, and the UA gating of section 6.1 passes, like the dual-SID and REU rows [V core_frontend.c:65-68, 128-145] |
| Enabled when | `!cartridge_attached \|\| c64_ultimate_audio_enabled`. The second term keeps an unreachable "on with a cartridge" state clearable: the menu sends actions only for enabled rows, and a disabled checkbox still draws as checked [V firmware/common/menu.c:375, 1098-1105] |
| Status | The messages of section 6.3 |
| Apply | Live, while the menu pauses the machine, as one settings save followed by one register write (section 6.3). Not in the restart list; only C64 option 0 restarts [V main.c:3551-3558] |

Row states. Help is word-wrapped into at most 2 lines of `CONTENT_W` = 94 characters [V firmware/common/menu.c:14-16, 905-925]. Value text fits `VALUE_W` = 57. Status texts fit the 64-byte status buffer [V main.c:178]. The first matching state wins:

| State | Checkbox | Value (colour) | Row enabled | Help (ASCII) |
|---|---|---|---|---|
| UA on and `ua_fault` | checked | `Fault` (warning) | yes | `Ultimate Audio stopped after a memory error and stays muted until the core is loaded again. Enter turns it off.` (111) |
| UA on | checked | `On` (blue) | yes | `7-voice Ultimate Audio sampler at $DF20-$DFFF. It plays from REU memory, so turning it on also turns the REU on.` (112) |
| UA off, cartridge attached | unchecked | `Eject cartridge first` (muted) | no | `Cartridges exclude the REU, and Ultimate Audio plays from REU memory. Eject the cartridge first.` (96) |
| UA off | unchecked | `Off` (blue) | yes | same as "UA on" (112) |

- There is no "Needs REU" or "Inactive" state. Turning UA on turns the REU on, and turning the REU off turns UA off (section 6.3), so UA is never on while the REU is off.
- Frontend state, next to `c64_reu_enabled` [V main.c:2474-2478; firmware/include/multitini_core_frontend.h:30-32]: `bool c64_ultimate_audio_enabled = (config & 0x10) != 0`, and `bool c64_ultimate_audio_fault` = `C64_UA_STATUS` bit 0, read without a debug session.

Other rows on an image with UA:
- **RAM expansion** (argument 3): the values do not change. Its help becomes `Enable the 16 MB DDR REU; eject any cartridge first. Turning it off also turns Ultimate Audio off.` (98). Without UA it keeps today's text [V firmware/common/core_frontend.c:135-145].
- **Media tab cartridge row** with the REU on: the value `Turn REU off first` stays [V core_frontend.c:93]. While UA is on, its help becomes `Cartridges conflict with REU I/O. Turning REU off in Core options also turns Ultimate Audio off.` (96).

The longest help text is 112 characters, so every text fits in two wrapped lines [I, checked by script against the wrap rule].

All strings are ASCII. `mt_ui_cell` maps bytes outside 32-127 to `?` [V firmware/common/ui.c:9-11]. Lengths and ASCII were checked by script.

### 6.3 Runtime changes

- `handle_menu_action`: arguments 2, 3, 8 and 11 go to `change_c64_expansion()`. The turbo arguments 4-7, 9 and 10 are routed before today's `file_index >= 2` branch (TD §6.2) [V main.c:4505-4508]. Argument 11 is unused in the first release.

**UA/REU apply rules** (`change_c64_expansion` [V main.c:3867-3906], arguments 2, 3 and 8). One user action makes **one settings save, then one register write** that carries bits 4:0, so the REU and UA bits change on the same `c64_clk` edge. If the save fails, firmware restores the settings, does not write the register, and warns "Expansion setting not saved". This is today's order [V main.c:3888-3905].

`ua_cap` means UA passes the gating of section 6.1 and caps bit 1 (16 MB REU) is set. `crt` means `c64_content_service_crt_attached()` is true.

| Before (REU, UA) | Action | Cartridge | After (REU, UA) | Stored | Message |
|---|---|---|---|---|---|
| (0, 0) | UA on (arg 8) | no | (1, 1) | profile REU = 1; global UA = 1 | status "Ultimate Audio and 16 MB REU enabled" |
| (1, 0) | UA on | no | (1, 1) | profile REU = 1; global UA = 1 | status "Ultimate Audio enabled" |
| (0, 0) | UA on | yes | refused | nothing | warning "Eject the cartridge before enabling Ultimate Audio" |
| (1, 1) | UA off (arg 8) | - | (1, 0) | profile REU = 1; global UA = 0 | status "Ultimate Audio disabled; REU stays on" |
| (1, 1) | REU off (arg 3) | - | (0, 0) | profile REU = 0; global UA = 0 | status "REU and Ultimate Audio disabled; cartridges available" |
| (1, 0) | REU off | - | (0, 0) | profile REU = 0 | status "REU disabled; cartridges available" (today) |
| (0, 0) | REU on (arg 3) | no | (1, 0) | profile REU = 1 | status "16 MB REU enabled" (today) |
| (0, 0) | REU on | yes | refused | nothing | warning "Eject the cartridge before enabling REU" (today) |
| any | SID (arg 2) | - | SID changes; REU and UA unchanged | profile SID; profile REU = current REU | status "SID audio setting saved" (today) |
| (0, 1) | - | - | unreachable: firmware never writes it, and the clamp removes it | - | - |

```
old = shell_read(C64_EXPANSION_CONFIG); sid = old[2:0]; reu = old[3]; ua = old[4]
arg 2: sid = next(sid)
arg 3: if !reu { if crt { warn; return }  reu = 1 }  else { reu = 0; ua = 0 }
arg 8: if !ua_cap { warn "Core option unavailable"; return }
       if !ua  { if crt { warn; return }  ua = 1; reu = 1 }  else { ua = 0 }
backup settings
profile.sid = sid; profile.reu = reu            // stored REU follows the effective REU
if ua_cap && ua != old[4]:                      // only a change of the UA bit
    settings.c64_ultimate_audio = ua             // writes the global flag (UA image only)
if !save(): restore backup; warn "Expansion setting not saved"; return
write C64_EXPANSION_CONFIG = (old & ~0x1F) | sid | reu<<3 | ua<<4
```

- Only an apply that flips the UA bit writes the global UA flag: UA on, UA off, or REU off while UA is on (the table's Stored column). A SID change, REU on, and REU off while UA is already off leave it unchanged, so the stored-cartridge case of the restore rule keeps the global flag. On an image without UA, the REU row never changes the global UA flag.
- The UA row is disabled while UA is off and a cartridge is attached (section 6.2), so the refused UA-on case can happen only if the state changes while the menu is open. The check stays anyway.
- A CRT load is already refused while the REU is on [V firmware/platform/xilinx/c64_content_service.c:334-337], so a cartridge can never be loaded while UA runs. The content service needs no new rule.
- Turning UA off leaves the REU on. Turning the REU back on after "REU off" leaves UA off; the user turns UA on again if wanted.

**Restore rule** (`configure_c64_expansion` [V main.c:2271-2283]; it runs on every restore [V main.c:2311-2312]):
```
ua_avail     = ua_cap                                   // section 6.1 gating plus caps bit 1
cart_pending = profile.cartridge_name != "" && !profile.reu
ua_eff       = ua_avail && settings.c64_ultimate_audio && !cart_pending
reu_eff      = caps.REU_16MB && (profile.reu || ua_eff)
write C64_EXPANSION_CONFIG = sid | reu_eff<<3 | ua_eff<<4
```
- Restore writes no settings and causes no turbo apply.
- The existing cartridge restore runs after this step and is skipped while the REU is on [V main.c:2323-2339].
- A stored cartridge wins over the global UA flag when the profile's REU is stored off. That profile restores its cartridge with REU and UA off, and the UA row shows the cartridge state. The global flag is unchanged.
- With one C64 package, `ua_eff` always equals the global flag. Today there is one package, `c64`, and its `pal` and `ntsc` variants share one profile. Turning UA on stores REU = 1 in that profile, and loading a cartridge needs REU off, which turns UA off. The two special cases above can happen only in two ways: with more than one C64 package, or after REU or cartridge changes made on an image without UA (where the REU row never touches the global flag).

**`:uastat`** (debug-only; it lives in the firmware debug service, TD §6.5):
- Outside a UART debug session it refuses with "debug is off; send :debug on" before it touches any register. Without caps bit 3 it refuses with "Ultimate Audio not available on this image".
- It prints `C64_UA_STATUS` decoded, `C64_UA_FETCHES`, `C64_REU_MISSES`, `C64_EXPANSION_CONFIG` bits 3-4, `C64_REU_HOST` bits 12-14, and, in a probe build, the 0x0FC register.
- The counters, the sticky debug flags and `playing` read 0 until `:c64diag on` arms diagnostics (section 6.1). `:c64diag on|off` is a debug-only command shared with turbo (TD §6.5). Board gates G-REU0, G3 and G9 arm first (section 9).
- `:debug on` prints a banner that names `:uastat` and the UA debug fields among the facilities it unlocks. Neither command appears in the menu, and nothing about them is persisted.

- DDR drain: no firmware change. `core_requires_ddr_drain` is already true for `REU_DDR` images [V main.c:408-421], and the HP3 path joins the same quiescent bit.

### 6.4 Persistence

- **Bit 4 of the per-profile expansion byte is unusable**, although old decoders appear to mask only `&7`/`&8`. The schema-10 decoder rejects the whole settings file when any of bits 7:4 is set: `if ((config & 0xf0U) != 0U || ...) return false;` [V firmware/platform/xilinx/supervisor_settings.c:699-703]. A downgrade to current firmware would therefore drop every saved setting.
- **Decision:** store the preference in **global settings byte 26, bit 2** (`0x04`).
  - Current firmware neither writes nor validates bytes 26-27: the encoder writes 24, 25 and 28+ after a `memset` [V supervisor_settings.c:573-580]; the decoder checks only byte 24 [V :686].
  - An upgrade reads 0 = Off.
  - Schema 10 is kept.
- **Byte 26 is defined in this release** (TD §6.3): bit 0 = disk auto-slow Off, bit 1 = cartridge forces 1 MHz (both turbo), bit 2 = Ultimate Audio on (this design), bits 3-7 reserved: written 0, **ignored on decode, never rejected**, so later flags stay downgrade-safe. Byte 27 holds the turbo detail (TD §6.3).
- **Scope (DL1, design decision, user may override):** the flag is global to all C64 packages, consistent with the approved global turbo bytes 26-27. A per-package flag would need schema 11, which wipes settings on downgrade.
- **How the global UA flag and the per-profile REU flag work together:**
  - every apply of section 6.3 writes the active profile's REU flag (expansion byte bit 3); an apply that flips the UA bit also writes the global UA flag, in the same save;
  - after every apply, an effective UA bit of 1 implies that the active profile's stored REU = 1;
  - at restore, the rule of section 6.3 computes the effective state, and nothing is saved;
  - UA is never stored in the per-profile expansion byte: its bits 7:4 must stay 0 [V supervisor_settings.c:699-703]. The per-profile REU default stays On [V firmware/platform/xilinx/supervisor_settings.h:22].
- **Never persisted:** the debug session, the armed flag, the UA hold and every UA debug field (TD §6.5).
- **Downgrade:** schema-10 firmware at HEAD keeps the file, but its next save zeroes bytes 26-27 [V supervisor_settings.c:577-580]. A downgrade therefore loses the UA flag and the turbo detail and flags. The per-profile REU and SID settings survive, and no other setting is wiped.
- API:
  - add `bool c64_ultimate_audio` (default false) to `mt_supervisor_settings_t`, next to the turbo byte 26 flags;
  - add `mt_supervisor_settings_remember_c64_ultimate_audio(settings, bool)`. `change_c64_expansion` calls it and the profile SID/REU remember call before one `save_persistent_settings()` call (or one combined remember call);
  - `configure_c64_expansion` reads the global flag.

### 6.5 Default value: Off

- The U64 default is Disabled [V upstream c64.cc:95].
- Off keeps the stock REU, including the `$DF20+` mirrors that are compared with VICE 3.10, exactly as dev5 shipped it. Expand runs "perfectly" there [V docs/C64_EXPANSIONS.md:103-108].
- Programs that need UA document the setting (Doom's `doom.cfg` sets "Map Ultimate Audio $DF20-DFFF=Enabled" [V Doom release/doom.cfg:11]).
- An idle UA costs nothing audible, but enabling it by default would silently change REU mirror semantics for every program.
- Turning UA on also turns the REU on (section 6.3). The per-profile REU default is already On [V firmware/platform/xilinx/supervisor_settings.h:22], so with default settings only the UA bit changes.

### 6.6 Audio mixer settings

- No volume/pan mixer rows in the first release. UA is mixed at unity, hard left/right (section 2.7).
- The U64's "Vol/Pan Sampler L/R" defaults to 0 dB with Left 3/Right 3 pan (gains 243/256 and 79/256), about -9.8 dB crosstalk [V upstream software/u64/u64_config.cc:300-305, 320, 445-456, 1347-1367].
- Multitini's dual SID is hard-panned [V docs/C64_EXPANSIONS.md:16-19], so hard UA panning is consistent.
- Add a level row only if O3 or listening tests show an imbalance. It would be argument 11, screen order 12, which TD §6.2 reserves; it is not in the first release. A later revision would take its storage from the reserved bits of settings byte 26; until then those bits stay reserved and ignored.

### 6.7 MOD player out of scope; REU image preload in this release

- The U64 built-in "Play MOD" is the Freshness `module.bin` without source in that tree [V upstream software/ModPlayer_16k/module.bin; software/filetypes/filetype_reu.cc:96-98, 149-160]. It cannot ship in a GPL-3.0 image, and it stays out.
- Every third-party UA program we found, except the self-contained tests, needs its samples preloaded into the REU or loaded through UCI, which Multitini lacks [I, from the program survey; for Doom: V Doom docs/intro.md, "REU delivery"].
- **REU image preload ships in this release** (TD §6.4 (a), a user feature that needs no debug session): "Load into REU" in the C64 media file browser loads a `.reu` file at REU offset 0, and `:reuload FILE.reu [OFFSET]` does the same with an optional offset. The C64 is not reset.
- For UA, the preload pauses the machine, holds UA (`C64_REU_HOST` bit 13) and waits for UA held (bit 14), waits for REU idle, copies, invalidates the REU cache, then releases the hold and resumes (TD §6.4 (a); sections 1.8, 2.5).
- With that feature, users can run the Freshness `module.prg` from CSDb with their own MOD files, and Doom C64U can load its `assets.reu`.

---

## 7. Improvements over the U64

User rule (Q7): "If users are opening issues for U64 that really matter then we should implement the solution and not blindly follow U64." Each item has a compatibility check.

| # | U64 behaviour (evidence) | Multitini | Compatibility check |
|---|---|---|---|
| I1 | B == length while the loop is not taken never ends. The B test pre-empts the length test even with repeat = 0 [V sampler2.vhd:145-154]. Users hit it: a one-shot after a loop with leftover B plays for ever [M xahmol 1.4.2, u64-bbcbasic]. "Release" with B = length (Heartbeat loop-with-release) also runs past the end [I, from upstream sampler2.vhd:145-154 and xahmol docs/HEARTBEATPLAYER_MANUAL.md:289-293; O: not yet observed on hardware] | Take the loop only if `pos == B && gate && repeat`; otherwise test length | Doom loops with gate + repeat and B = length: unchanged. c64cast (loop over a ring, moving length): unchanged, because the loop branch still wins when taken. Detection (repeat = 0, B = 0, length 256): unchanged. Only the bug case differs |
| I2 | 16-bit plays slower than 8-bit at the same rate (#972, open; 80 ns per sample on a U64-II [M 1541ultimate issue #972; I, consistent with our model of sampler2.vhd:172-198]). `ua_972_check` is red on a C64U [M issue #972] | One period, `(2*rate + 3)` half-ticks, for every mode | 8-bit pitch equals the U64-II/C64U (O1). 16-bit is 0.06-0.38 % faster than on a U64-II, inaudible as pitch. Layered 8/16-bit voices stay in sync. `ua_972_check` expected green |
| I3 | Period differs from the documented law by 0.2-1.9 % [M c64cast: 6.157-6.164 MHz effective] | Document the real law and the divider formula (section 1.5.3) in Multitini's docs | Documentation only |
| I4 | Registers survive a C64 reset. Stale state leaks: a stale length low byte makes the Freshness probe 511 samples (204.4 µs) and fails detection under turbo [I, computed from Freshness audio.asm:25-79, which never writes the low byte]. The firmware zeroes control and volume on C64 reset [V upstream software/io/command_interface/command_intf.cc:98-108; software/io/audio/sampler.h:35-42] | C64 reset and UA inactive load ctrl 0, vol 0, pan 8, all else 0; flags clear; output 0 | After any C64 reset a U64 already shows ctrl 0 / vol 0. Only a program relying on other stale pre-reset values could tell [I] |
| I5 | `start + position` can leave the REU and read firmware memory (top byte `$00`); the MOD player's probe reads address 0 [V Freshness audio.asm:25-79] | `(start[23:0] + pos) mod 2^24` inside the REU aperture; start bits 25:24 ignored | The probe runs at volume 0, so it is inaudible, and its timing is identical. Programs that use `$01` as the top byte (all known) are unaffected. A C64 program cannot read PS DDR (isolation) |
| I6 | The bridge can return `$AA` on a late reply [V slot_to_io_bridge.vhd:69-80]; 3.15 lists "Ultimate Audio timing fixes" [V 1541u-documentation howto/release_3.15.md:21] | Combinational read data, valid within the cycle | None |
| I7 | A USB tag collided with voice 5's fetch tag; USB drives disconnect while UA plays. Fixed in U64 3.15 [V upstream commit a749334df8; 1541u-documentation howto/release_3.15.md:192], not yet on the C64U [M Heartbeat, heartbeat-demo] | UA is the only master on its HP3 path. One outstanding read with one owner register; no shared tag space | None. G5 also plays 7 voices during SD disk writes and USB activity |
| I8 | Single SDRAM, natural ordering. Multitini's posted REU cache would otherwise let UA read stale DDR | Defer on pending REU writes, plus snoop-invalidate (section 2.5) | Pastebin example (and u64-bbcbasic `*SAMPLE`, if REU and CRT ever coexist): data stashed by DMA plays immediately. Streaming double-buffers see the newest bytes. A fixed-address stash into the word being played is deferred to the end of the command (section 2.5) |
| I9 | FIFO without overflow guard; degenerate rates play stale or garbage bytes [V sampler2.vhd:257]; #744 suspected cross-voice corruption (closed as a capture-tool error [M U64 issue #744]) | Bounded issue rate. Late sample = previous sample. Position and IRQ timing exact. Sticky underrun flag | At audio rates nothing changes. At degenerate rates the output is defined instead of garbage |
| I10 | Cartridge prohibits are fixed for the session; UBoot64 (FC3 type) locks UA out after it switches itself off [M UBoot64-v2 issue #25; V upstream c64.cc:1414-1436 (prohibits fixed at cartridge start)] | First release: UA follows the REU, and the REU excludes all CRTs. Later, if REU + CRT coexist: decide per cartridge type from its live I/O2 decode, re-evaluated on every change, never once per session | First release: none |
| I11 | Rewriting REU memory while a voice plays makes noise (BreadAmp V07 [M]); the firmware clears the sampler before Play MOD [V filetype_reu.cc:96-98] | REU image preload holds UA (in reset, buffers empty, no new read) from before the copy until after the REU cache invalidate, and waits for the "UA held" acknowledgement before it writes (section 2.5; TD §6.4 (a)). UA leaves the hold with its voices idle | None. X-PRELOAD checks it on the board |
| I12 | The sampler keeps running while the menu freezes the C64 [O5] | The UA timebase freezes with `core_paused` | Music resumes in step with the program. An IRQ cannot fire during the pause |
| I13 | Pitch depends on the product's system clock (prescaler fractions per clock [V upstream sampler2.vhd:48-63]; resulting periods [I, model]) | Same pitch on PAL and NTSC images and at every turbo speed | None |
| I14 | The mapping is independent of the REU enable; REU-only and sampler-only cartridge prohibits are applied separately [V upstream c64.cc:312-327, 1363-1371, 1414-1436] | UA turns the REU on; no CRT. Turning UA on turns the REU on in the same apply, and turning the REU off turns UA off (section 6.3). The HDL keeps `ua_active = ua_enable && reu_active && !ua_hold` as a second safeguard (section 1.1). With UA off, `$DF20` belongs to the REU mirrors or open bus, and detection reports no sampler | No known UA program runs with the REU off: all of them play from REU memory, so tying the two together costs nothing. u64-bbcbasic (a CRT that needs the sampler) is excluded in the first release, as I10 already says. Revisit together with I10 if REU and CRT ever coexist |
| m1 | The 8-bit low byte is stale from earlier 16-bit use [V sampler2.vhd:124] | Low byte 0 | At most 255/65536 of full scale; inaudible |
| m2 | Voice 7 is internal only | Absent; status bit 7 = 0 | Same as seen from the C64 |

Kept on purpose (working software depends on it, or the change is not worth the risk):
- big-endian registers, signed PCM, LE 16-bit data, loop points relative to start;
- equality compares; odd 16-bit length plays for ever (wrapping inside the REU);
- 6-bit masked volume and 4-bit pan, so xahmol's `0x80` "centre" stays left;
- no read-back; even/odd read map over the whole window; modes `10`/`11` = 16-bit; the IRQ is a level;
- a stop needs gate = 0 **and** repeat = 0.
- **K1 (DC hold):** a stopped voice holds its last sample while running.
  - Changing it would only move the click from restart to stop.
  - It would break a "1-sample DAC" trick if any program uses one [O10].
  - Reset and disable zero the output (I4), which removes the reported pain.

---

## 8. Risks, ranked, with retiring experiments

| Rank | Risk | Retiring experiment | Mitigation |
|---|---|---|---|
| R1 | UA reads on HP3 delay REU DDR transactions past the 64-clock budget. REU timing faults (Expand-class demos) appear only while UA plays | Board gate G3 with its mandatory instruments: `C64_REU_MISSES` (readiness misses at the phase-15 deadline) and `C64_UA_FETCHES`/`playing` (proof that the voices really play), read with `:uastat` inside a debug session with `:c64diag on`. Sim (supporting only; bounded simulation is not a physical latency proof [V docs/C64_EXPANSIONS.md:93-94]): T6 | `UA_MIN_ISSUE_GAP`, `UA_YIELD_TO_REU`; fallback arbiter (section 2.6) |
| R2 | An HP2 write acknowledged by B is not visible to a later HP3 read within `UA_POST_B_HOLD` clocks (O2) | Optional G9: needs a test-only PL coherence engine in a diagnostic build (section 9); not planned for the first release | `UA_POST_B_HOLD = 16` **by default**; raise it if G9 or a field report shows a mismatch. Only a program that stashes into the word being played could ever hear the case |
| R3 | Turbo detection margin: probe 102.4 µs against a wait budget of about 130 µs. It fails if turbo ever serves I/O2 faster than one phi2, or if a stale length low byte survives without a reset (511 samples, 204.4 µs) | Turbo ships in the same release: T9 (bus replay); TD T3, the one-phi2 `$DF00-$DFFF` check at all 16 indices with badline timing on and off (section 1.10); board gate X-DET at every index, badline timing on and off, screen on and off | Binding pacing rule in section 1.10; I4 reset clears the stale byte after any C64 reset |
| R4 | Period law unverified against the exact upstream VHDL and a real C64U (O1) | UA-M0 option C bench (exact upstream VHDL); C64U: `ua_972_check` plus CIA-timed IRQs at rates 1, 281, 283, 781 in 8-bit and 16-bit | One parameter |
| R5 | Placement change perturbs the tightest domains: the 148.5 MHz video clock (+0.133 ns) and `control_clk` (+0.446 ns) [V c705r2 timing_summary.rpt:202, 207; docs/C64_VICE310_COMPARISON.md:51]. The combined image adds the turbo island to the UA logic and HP3 (section 4.1) | TD M1 builds include the UA RTL and HP3 once UA-M2 passes, so placement is judged with both features (TD §10.3); the one combined full build (TD M5 = UA-M4) with timing signoff at the sealed path; record per-domain WNS/WHS before and after | Floorplan or netlist-reuse plus reroute repair as used for the IIgs images; temporary margins cleared before signoff |
| R6 | Boxcar resampling quality is poorer than the U64's filtered chain | T13 records the alias levels; listening tests; a U64 capture of the same tones would give the comparison | Add a polyphase FIR later (DSP, no BRAM) |
| R7 | UA/SID loudness balance (O3) | U64 recording at default mixer: full-scale SID square against a UA square at vol 63, pan 0 | Constant gain, or a later level row |
| R8 | An improvement (I1, I2, I4) breaks an unknown program | Corpus run: Doom intro, Freshness MOD player and synth, Pastebin example, `ua_972_check`; later the xahmol programs if UCI ever exists | Each change is local and can be parameterised |
| R9 | **Retired.** No third-party content could load samples without a REU loader | - | REU image preload ships in this release (section 6.7; TD §6.4 (a)); G8 and X-PRELOAD test it |
| R10 | **Resolved.** Settings coordination with turbo on byte 26 | Host test of all byte 26 flags, reserved bits ignored, and downgrade decode (T10b, X-T7) | Byte 26 is defined in this release with the reserved-ignored rule (section 6.4) |
| R11 | The interaction of turbo's IRQ-acknowledge class with UA `$1F` writes is wrong | TD E4/T7 (TD §9 R4, §10.1 E4), which include the UA `$1F` writes, normal and I/O-only, with UA on and off (X-T2) | Address-only decode (section 1.10) |
| R12 | Turning UA on also turns the REU on, which surprises a user: a program that probes for a REU behaves differently, or a later cartridge load is refused | Host tests of every apply row and text (T10, T10c) | The status text names both ("Ultimate Audio and 16 MB REU enabled"); the RAM expansion and cartridge help say that turning the REU off also turns UA off; the per-profile REU default is already On (section 6.5) |
| R13 | A gate reads zero counters because diagnostics were not armed, or debug state leaks into normal use | T15 (gating), T10c (`:uastat` refused outside a session); the release gate "Debug" (section 9, UA-M5) | Board procedure arms first and logs `:uastat`; results count only from runs that logged it (section 9, UA-M5) |

---

## 9. Verification plan and milestones

Small tests come before any full Vivado build (AGENTS.md). No board claim is made until a board test passes. Every runner step must finish inside the existing 300 s step timeout [V scripts/test_c64_expansion_infrastructure.py:44], so each bench below states its size.

**Place in the combined release plan** (TD §10.3 owns it; one line per milestone here):
- Release step 0: TD N0 (stock NTSC build and board test of today's tree), before any UA work.
- UA-M0: release step 1, with TD M0. The first implementation commit, before any RTL edit by either feature, is the T2 golden trace (UA off) on the unmodified RTL.
- UA-M1 to UA-M3: release step 2, after the single spec edit, in any order with TD M1-M4. Once UA-M2 passes, every TD M1 build includes the UA RTL and HP3.
- Cross-feature small tests X-T1 to X-T9 on the merged tree: release step 3, before the full build. The UA tests they include are marked below.
- UA-M4: release step 4, the one combined full build, which is also TD M5's signoff build.
- UA-M5: release step 5, the board release gates.
- TD numbers the release steps 0-5, not S0-S5, because TD §13.6 uses S1-S10 for its NTSC tests.
- If either feature fails a blocking gate, the whole release waits. Shipping one feature alone needs a new user decision and a second ABI step. NTSC follows Q14: if it does not close at 64x, PAL ships alone with both features.

### UA-M0: reference model and exact upstream reference (both mandatory)
- `scripts/ua_model.py` (GPL-3.0-only, with a `licenses/files.csv` row): a cycle model of section 1, with an "upstream quirks" switch that reproduces sampler2 (prescaler law, B == length, mode-dependent period, stale low byte). It also regenerates the section 1.5.3 divider table and the section 4.2 figures; `scripts/test_c64_ultimate_audio.py` compares them with committed expected values.
- **Option C bench** `hdl/sim/tb_c64_ua_upstream_ref.sv`: a mixed-language xsim bench around the exact upstream VHDL (section 5), with an io_bus driver and a memory model, at 50, 66.67 and 100 MHz. It records boundary, fetch and IRQ times for rates 1, 281, 283 and 781 in 8- and 16-bit, interleave, an A-B loop, release with B == length, a one-shot with stale B == length, and the detection probe. The quirks mode must match these traces visit for visit. Size: under 10^7 clocks per frequency.
- The design-stage scratch scripts are not references and are not copied into the repository; `ua_model.py` replaces them. The period law stays [I] until the option C bench (or a measurement, O1) agrees.
- Generate expected traces for UA-M1.

### UA-M1: unit benches (xsim)
| ID | Bench | Pass criteria |
|---|---|---|
| T1 | `tb_c64_ua_regs` | Reset values; big-endian assembly; byte-immediate effect; even/odd read map over the whole window; status bit 7 = 0; IRQ clear by bit 0 and by `$FF`; set beats clear on the same clock; ignored offsets and start bits 25:24 |
| T3 | `tb_c64_ua_voice` against UA-M0 traces | 8/16-bit, interleave strides, odd-start 16-bit straddle, A-B loop, release, I1 cases (one-shot with stale B = length; release with B = length) both end, odd 16-bit length never ends, live rate change. **Restart cases:** `$03`, `$02`, `$03` continues with no jump to position 0; `$03`, `$00`, `$03` restarts from 0; FINISHED then `$00`, `$01` restarts. **Length 0 = 2^24 and the 2^24 wrap:** a bench-only hierarchical preload of the position register to 2^24 - 16 (xsim `force`/`release`), so the case takes under 100 samples. Period within 10 ppm over 10^7 half-ticks at PAL and NTSC `CORE_CLOCK_HZ`; same period for 8- and 16-bit (#972). Size: about 3 x 10^7 clocks in total |
| T4 | `tb_c64_ua_fetch` with a latency-stress AXI model (10-200 clocks random) | 2 and 7 voices streaming 4 KiB buffers at rates 4-20, placed at the start, middle and last 4 KiB of the aperture: no cross-voice data (#744 class); underruns only above the cap; hold on underrun; sticky flag; orphan response after soft reset discarded; **fault:** an RRESP error sets `ua_fault`, mutes UA, stops issue, and the path still drains. Size: at most 2 x 10^7 clocks per scenario |
| T5 | `tb_c64_ua_coherence` with the real `multitini_c64_reu_cache`, including TD's `invalidate` input held idle (X-T4) | Reference: a byte-accurate memory model updated at each accepted REU write. Every consumed byte equals the reference memory at some moment after the last write accepted **before the clock of** the logical fetch. Cases: stash into the region being played (incrementing address); fixed-address stash and swap (underrun defined, recovery within one period after the drain); partial youngest word; **a REU write accepted on the same clock as a UA issue, and on the clock before a resident hit, for every snoop and defer phase**; swap commands; UA enabled after a guest reset while posted REU writes are still queued; post-B hold boundary (read at hold - 1 deferred, at hold allowed). **Negative controls:** defer off, snoop off, resident-hit snoop check off, hold set to 0 with a model that delays visibility; each must fail |
| T7 | `tb_c64_ua_mix` | Pan law at 0, 7, 8, 15; vol 32 = unity; 21-bit saturation with 7 voices at vol 63; boxcar normalisation (DC in = DC out at 82/83 and 85/86 counts); final sat24, no wrap; UA inactive gives a bit-identical SID sample stream; mixer and boxcar output bit-exact to `ua_model.py` over 2 x 10^6 clocks of random voice data |
| T13 | Spectral check on `ua_model.py` output (bit-exact to the RTL by T7) | Single tones at 1, 5, 10 and 15 kHz from 22.05, 44.1 and 48 kHz content, 8192 output samples each at 48 kHz. Pass: every spectral line (the tone and the zero-order-hold images folded by 48 kHz sampling) is within 0.5 dB of the analytic prediction (sample-and-hold at the source rate, boxcar over each 48 kHz period, then sampling); no other line above -50 dBc, which bounds the 31.7 ns half-tick quantisation [I]. The measured alias levels are recorded in `docs/C64_ULTIMATE_AUDIO.md`. T13 proves the implementation; quality against the U64 stays R6 |

### UA-M2: adapter integration
| ID | Test | Pass criteria |
|---|---|---|
| T2 | I/O2 decode equivalence | **UA off, reference 1:** `scripts/test_c64_vice_reu.py` passes unchanged; it already compares IO2 mirrors and REC behaviour with VICE 3.10 [V docs/C64_EXPANSIONS.md:45]. **UA off, reference 2:** a golden CPU/REU bus trace over `$DF00-$DFFF` (mirrors, status clear, `$FF00` trigger, DMA release), generated by the new T2 stimulus on the **unmodified** RTL and committed in the first implementation commit, before any RTL edit by either feature (TD §10.3, release step 1), because turbo edits `fpga64_sid_iec.vhd` and the adapter; every later commit must reproduce it, and it is re-run with Turbo Control Off on the merged tree (X-T3). **UA on:** `$DF00-$DF1F` REU, `$DF20+` UA; a `$DF20` read does not clear REU status |
| T6 | REU non-interference (supporting evidence only) | `tb_c64_reu_native` and `tb_c64_reu_machine` scenarios (swap, verify, fixed address, cold bank change) with UA streaming at the cap on a shared-DDRC model: REU DMA cycle traces identical to UA off within the 64-clock budget |
| T8 | Reset, pause, fault and quiescence | Guest reset mid-fetch: no fault, orphan absorbed. Pause freezes positions and IRQs. **A UA RRESP error leaves `core_reset_n` and `ready` high and `reu_fault_latched` low**, sets `ua_fault`, and the UA path still reaches quiescence. **Preload hold (X-T5):** `ua_hold` = 1 empties the buffers and blocks issue; a read in flight is discarded; `ua_held` rises only after UA is in reset; release returns UA in its reset state; `reu_fault_latched` is untouched. The top-level parts (the split `generate`, `gs_memory_quiescent` still driven in the C64 image, the HP3 quiescent ANDed into `VIDEO_FRAME_STATUS.QUIESCENT`) are checked by string markers in `tests/test_build_inputs.py`, as the existing top wiring is [V tests/test_build_inputs.py:413-426] |
| T9 | Detection replay | Bus scripts written from a **behavioural description** of the Freshness `detectaudio` routine and xahmol's `uii_audio_detect()`: the order of register writes and the polling rules, stated as facts. No code or disassembly is transcribed; a comment credits both sources and the `files.csv` note says "behavioural sequence; no third-party code or bytes copied". Expected results: after a C64 reset, pass at 1 MHz and at one access per phi2; fail at 64 MHz spacing (documents the binding pacing rule of section 1.10; part of X-T3). **Stale length low byte (`$FF`) with no reset in between:** pass at 1 MHz (the 1429 µs wait covers the 204.4 µs probe); fail at one access per phi2 (about 130 µs). This is the same on a U64 and is recorded as a known limit of the routine under turbo |
| - | Existing suites unchanged | `scripts/test_c64_reu.py`, `test_c64_reu_cache.py`, `test_c64_reu_native.py`, `test_c64_reu_machine.py`, `test_c64_ddr.py`, `test_c64_vice_reu.py`, `test_c64_vice_comparisons.py`, `test_c64_machine_schedule.py`, `test_c64_machine_interrupts.py`, `test_c64_mame_comparisons.py`, `test_c64_mame_sid.py`, `test_c64_expansion_infrastructure.py`, **`test_c64_acceleration.py`**, **`tests/test_c64_ddr_drain_runtime.py`** (the DDR handoff that section 6.3 relies on), **`tests/host/test_c64_content_service.c`** (the CRT/REU conflict and caps gating that section 6.3 relies on) [V scripts/ and tests/ listings; capture_vectrex_build_inputs.py:56] |
| T15 | Diagnostics arming (X-T6) | With `C64_CPU_CONFIG` bit 19 = 0, every UA debug field (status bits 1-3, `playing`, underrun count, `C64_UA_FETCHES`, `C64_REU_MISSES`) reads 0 while 7 voices play and REU transfers run; arming clears them and they then count; disarming returns them to 0; `ua_fault` is live in both states; the turbo status bits 24-26 follow the same level (TD side) |

### UA-M3: firmware host and build inputs
The firmware tests below are UA's part of X-T7; T10e is UA's part of X-T9; T7 and T10f are UA's part of X-T8.

| ID | Test | Pass criteria |
|---|---|---|
| T10 | `tests/host/test_core_frontend.c` | Row only with caps bits 1 and 3 and the UA ABI; argument 8 at screen order 11; checkbox from config bit 4; the four row states of section 6.2 (checkbox, value, colour, enabled flag, help text), including `Fault` and the cartridge state; the unreachable "on with a cartridge" state is enabled and can be cleared; the RAM expansion and Media cartridge help on a UA image, and today's texts without UA; row counts stay caps-dependent |
| T10b | `tests/host/test_supervisor_settings.c` | Byte 26 bit 2 round trip next to turbo bits 0-1; a file without it reads Off; bits 3-7 written 0 and ignored on decode (a file with them set decodes, not rejected); per-profile expansion byte unchanged and never carries UA. **Downgrade:** the test encodes a file with bit 2 set using the new encoder at run time (no binary fixture: `check_repo.py` rejects `.bin`) and runs it through a frozen copy of today's schema-10 accept predicate, committed as a test fixture source (`tests/host/fixtures/settings_schema10_accept.c`, GPL-3.0-only, copied from `supervisor_settings.c` at `5341812`). Pass: accepted, and every field it decodes is unchanged. **Restore rule** (section 6.3): `ua_eff` and `reu_eff` for every combination of caps, global flag, profile REU and stored cartridge, including the stored-cartridge case |
| T10c | `tests/test_menu_runtime.py`, `tests/test_c64_expansion_runtime.py` | Paused apply; every row of the section 6.3 table with its stored values and message; one save, then one register write that carries both bits; save failure restores the settings and makes no MMIO write; the global flag untouched by the REU row on an image without UA, and by a SID or REU change that does not flip the UA bit (including the stored-cartridge case); `:uastat` refused outside a debug session with no register access, and inside one prints the registers of section 6.3 |
| T10d | `tests/test_interface_spec.py`, `tb_control_regs`, `tb_c64_expansion_config` | Constants (ABI 0.24, `MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR` = 24, caps 0x08, mask 0x1F); bit 4 cleared without caps bit 3 **or** with bit 3 written 0; 5-bit mailbox; the three status offsets; 0x0FC not decoded in a release build |
| T10e | `tests/test_build_inputs.py` | HP3 ACLK = `c64_clk` (`clk_out1`) and HP3 in the c64 ASSOCIATED_BUSIF in the C64 image only, and turbo's `c64_cpu_clk` on `clk_out2`; IIgs and every other image's Tcl and top-level text unchanged; `ps7_init.c` hash unchanged; diagnostic settings (including `UA_COHERENCE_PROBE` = 1) refused without `MULTITINI_C64_DIAGNOSTIC_BUILD=1`; the build-input capture includes both features' runners, models and docs, `c64ua/` and `third_party/ultimate_reference/` |
| T10f | `tests/test_audio_path.py` | Existing asserts pass unchanged (the SID term keeps its literal text); new asserts: the sum saturates (no wrap) and `ua_l`/`ua_r` are 0 when UA is inactive |
| T14 | `tests/test_c64ua.py` | `c64ua/build.py --check` assembles every board PRG with the pinned ACME and matches the recorded digests, as `tests/test_c64fast.py` does for `c64fast/` |

### UA-M4: the combined full build (TD M5; only after UA-M0 to UA-M3 and X-T1 to X-T9 pass)
- **One build per standard for both features.** TD M5's signoff build and UA-M4 are the same build (TD §10.3, release step 4). PAL first; NTSC follows Q14.
- **Images.** Only the C64 image is rebuilt (`c64-pal`, and NTSC if that variant is built). Other images are reused with their existing build IDs and ABI 0.23. This is the review of every affected image that MULTITINI_ONE_PLAN.md build step 2 asks for [V MULTITINI_ONE_PLAN.md:155-158]:
  - their machine logic is unchanged;
  - the common-shell edits (mask and clamp in `multitini_control_regs.sv`, the regenerated register package with the ABI minor bump, the HP3 `generate` split, the C64-only HP3 branch in `create_project.tcl`) leave non-C64 elaboration unchanged, which T10e checks by text and T10d by simulating the clamp;
  - firmware already gates each feature on `abi_major ==` and `abi_minor >=` per package [V main.c:416, 1946-1968, 2266-2267], so images that report 0.23 and newer images coexist.
  - An image rebuilt later for another reason picks up the new shell and must pass its own gates.
- **Release build parameters.** `UA_MIN_ISSUE_GAP` = 32, `UA_YIELD_TO_REU` = 0, `UA_POST_B_HOLD` = 16, `UA_PERIOD_OFFSET_HALF_TICKS` = 3. A different value ships only after the experiment named for that parameter (O7/R1, O2/R2, O1/R4). The build-input capture records the values.
- **Diagnostic builds.** `UA_COHERENCE_PROBE` = 1 is a diagnostic setting. It is accepted only with `MULTITINI_C64_DIAGNOSTIC_BUILD=1`, it sets `C64_CPU_CAPS2` bit 14, and release packaging refuses such an image (TD §6.5).
- Timing signoff at the sealed path. Record per-domain WNS/WHS before and after; the c705r2 baseline is video +0.133 ns, `control_clk` +0.446 ns, `c64_clk` +7.052 ns [V c705r2 timing_summary.rpt:202-207].
- The CDC gate reports C3, C4 and C5 and the turbo crossings of TD §3.2 as expected, and no other unexpected critical crossing (C8).
- Utilisation: UA adds no BRAM.
- The build-input capture list is updated before the build (section 11) and re-verified at the end. Captured tests and runners are not edited mid-build.

### UA-M5: board (results recorded, claims only for what passes)

**Procedure for every board run** (TD §6.5):
1. Send `:debug on` and check the reply.
2. Send `:c64diag on` if the gate reads diagnostic fields (UA counters, sticky debug flags, `playing`, turbo status bits 24-26).
3. Log `:turbostat` and `:uastat`.
4. Run the gate, then log both again at the end.
5. Send `:c64diag off`, then `:debug off`.

One stock regression run per image also runs without a session, as a control. It is not a gate result and is exempt from the logging rule. It is valid when its results equal those of the session run and a session opened right after it logs the same CONFIG, CAPS and CAPS2 words. Gate results count only from session runs that logged the caps, CAPS2 and status words, plus `:uastat` for gates that involve UA or the REU. UA gates run with Turbo Control Off unless they name a turbo setting.

| ID | Gate | Release | Expectation |
|---|---|---|---|
| G-REU0 | **Expand by Bonzai disks 1 and 2, UA off** (this design's former G1; defined in TD §10.3) | **blocking** | As TD §10.3: UA option Off; Turbo Control Off, then the three configured-but-exact turbo states; the stock run matches M0.7.0dev5 [V docs/C64_EXPANSIONS.md:103-108] and each configured run is identical to it (PAL strict; the NTSC rule is in TD §10.3). The same gate records the `C64_REU_MISSES` baseline for G3 (three stock runs with `:c64diag on`), runs reudiag `reutiming` (TD §10.2, M2) and runs again after a 16 MiB REU image preload |
| G2 | Expand disks 1 and 2, UA on and idle | informational | Expected identical. A failure caused only by the missing REU mirrors above `$DF1F` (O-Expand) is accepted and documented in `docs/C64_EXPANSIONS.md`: Expand needs UA off, as on a U64. Any other failure is a defect |
| G3 | **Expand disks 1 and 2, UA on, 7 voices playing** | readiness part **blocking** (PAL); Expand behaviour informational | **Procedure:** (0) `:debug on`, `:c64diag on`. (1) Core loaded; UA on (it turns the REU on); `:uastat` baseline. (2) Load the `c64ua` background loader with Multitini PRG load (the reset before the loader is harmless). It stashes synthetic tones, starts 7 looping voices (gate + repeat, B = length, IRQ off; first 16-bit interleaved 48 kHz, in a second run rate 0 to load HP3 at the issue cap) and returns to BASIC. (3) Insert Expand disk 1 through Media > Drive 8, **not Mount & Run** and not Multitini PRG load, which hold the C64 in reset and would stop the voices [V multitini_c64_content_loader.sv:571-578; adapter:306-308]. (4) Start Expand with its normal LOAD/RUN typed at the BASIC prompt; swap to disk 2 through Drive 8. (5) `:uastat` at the end. **Pass:** `playing` = `$7F` and the fetch count advanced (the voices played throughout and no reset intervened); `C64_REU_MISSES` advanced by zero, or by no more than its G-REU0 range over three runs each; `ua_fault` = 0. Expand behaviour is recorded under the G2 rule. **If Expand stops the voices itself** (`playing` drops), the run is inconclusive for R1: run G3b, the `c64ua` REU stress PRG (stash, fetch, swap and verify streams, fixed and incrementing addresses) with the same counters, while 7 voices play |
| G4 | REU board regression | **blocking** with UA off; UA on informational | The REU board tests used for dev5 acceptance with UA off; UA-on results recorded under the G2 rule |
| G5 | Self-made UA PRGs (`c64ua/`) | **blocking** (PAL; NTSC if it ships) | Decode and read map; IRQ via the KERNAL chain; period measured with CIA timers at rates 1, 130, **281**, 283, 781, `$FFFF` (8- and 16-bit equal); the CIA-timed period at Turbo Control Off and at index 15 with badline timing disabled (the periods must match); stash-then-play immediately; double-buffer streaming; restart storm; S/PDIF or HDMI capture, or `:audiocap` inside a debug session, for clicks and spectrum (compare with T13) |
| G6 | `ua_972_check` (local input) | **blocking** (PAL; NTSC if it ships) | Green border |
| G7 | Pastebin simple player (local input) | **blocking** (PAL; NTSC if it ships) | One-shot plays and ends |
| G8 | Doom C64U intro (private local input) from a preloaded `assets.reu` (REU image preload, TD §6.4 (a)); Freshness MOD player with MOD files we have rights to use; Freshness synth | Doom **blocking** (PAL); Freshness informational | Music plays. Detection at every turbo index is X-DET |
| G9 | O2 cross-port characterisation | **optional**, never released | Needs a test-only PL coherence engine in a diagnostic build of the C64 image (`MULTITINI_C64_DIAGNOSTIC_BUILD=1` with `UA_COHERENCE_PROBE` = 1; CAPS2 bit 14 set; release packaging refuses it). Probe register at 0x0FC. Random writes through HP2, reads of the same word on HP3 0-32 clocks after B; mismatches counted and printed by `:uastat` inside a debug session. Zero mismatches in about 10^8 iterations at hold 0 would show margin under the default hold of 16 |

**Cross-feature release gates that involve UA** (defined in TD §10.3):
- **X-DET:** after a C64 reset, the `c64ua` detection PRG (our own code, following the common 128-read rule) finds the sampler with UA on, at Turbo Control Off and in Manual at every index 0-15, badline timing enabled and disabled, screen on and off; it reports no sampler with UA off; Doom C64U detects UA at index 15. PAL; NTSC if it ships.
- **X-PRELOAD:** with UA on and 7 voices playing, "Load into REU" of a 1 MiB synthetic file; after resume the voices are idle, `ua_fault` = 0, `:reudump` equals the file, and a REU fetch of the range returns the file bytes; repeated with UA off. PAL.
- **Debug:** every debug command, `:uastat` included, refuses without a session; `:debug off` restores the release values; a power cycle leaves no debug state. PAL.
- Turbo gates (TD M5) run with UA off unless they name UA.

---

## 10. Open questions

| ID | Question | Experiment or owner |
|---|---|---|
| O1 | C64U sampler clock and exact 8-bit period (assumed 100 MHz, `rate + 1.5` ticks) | First the UA-M0 option C bench of the exact upstream VHDL (settles the model); then `ua_972_check` with CIA timing, or an audio capture at rates 130, 281 and 283, on a C64U and a U64-II (settles the clock) |
| O2 | Is an HP2 write acknowledged by B always visible to a later HP3 read, and within how many clocks? | Optional G9 (needs a test-only PL engine). Not blocking: the default `UA_POST_B_HOLD` = 16 covers a short visibility delay |
| O3 | UA level relative to the MiSTer SID | U64 recording at default mixer, compared with Multitini |
| O4 | Do REU DMA cycles read or write UA registers on a U64? | U64: REU fetch into `$DF40-$DF5F` with a gated repeating voice; stash from `$DF20` and compare with status/open bus |
| O5 | Does a U64 sampler run while its menu freezes the C64? | Play a loop, open and close the menu, check the phase |
| O6 | #745 gap 2: silence with a looping length of 2,160,000 on a C64U | Repeat with B = length = 2,160,000 and 2,160,128; capture audio |
| O7 | REU impact of HP3 traffic on the board | G3 with `C64_REU_MISSES`, `C64_UA_FETCHES` and `playing` read by `:uastat` inside a debug session with `:c64diag on`; G3b if Expand stops the voices |
| O8 | **Closed (Q17).** Who ships first, UA or turbo? | Both ship in one release with one ABI step, 0.24 (TD §6.0) |
| O9 | **Closed (DL1, design decision, user may override).** Global or per-package UA preference? | Global: settings byte 26 bit 2, downgrade-safe (section 6.4). A per-package flag would need schema 11, which wipes settings on downgrade |
| O10 | Does any program use the DC hold as a DAC? | Corpus search for 1-sample one-shots; ask on CSDb/Lemon64 |
| O11 | BreadAmp internals (UCI use, modes, rates) | Disassemble the V07 PRG |
| O12 | The realised MMCM `c64_clk` frequency against `CORE_CLOCK_HZ` | Mostly answered: PAL is -32 ppm today and -5.6 ppm in the turbo plan (TD §3.1, CLK2). Pitch error equals that ppm difference. Confirm in the clock report of the combined build |
| O13 | Do HP2 and HP3 share one DDRC port (affects R1)? | Zynq-7000 TRM memory-interconnect chapter; confirmed in practice by G3 |
| O14 | Does any software rely on `$DF21 == $10` alone to detect UA? | Corpus search for `$DF21` reads (with UA off a Multitini REU also reads `$10` there) |
| O-Expand | Does Expand use REU mirrors at `$DF20` and above? | G2 (informational; a failure is accepted and documented, section 0.6) |

---

## 11. File-level change list (not done now)

### HDL
- **New** `hdl/core/c64/multitini_c64_ua.sv`: top; register file, read mux, IRQ flags, `ua_fault`, status flags and counters with the armed gate (hold 0 while disarmed, clear on arming); `ua_hold` input and registered `ua_held` output; instantiates the parts below.
- **New** `hdl/core/c64/multitini_c64_ua_voice.sv`: half-tick generator and seven parallel engines.
- **New** `hdl/core/c64/multitini_c64_ua_fetch.sv`: buffers, prediction, defer (queue, in-flight, registered snoop, post-B hold), snoop invalidate and resident-hit block, single-outstanding issue, owner and orphan handling, local fault.
- **New** `hdl/core/c64/multitini_c64_ua_mix.sv`: upstream-law mixer and boxcar averager.
- `hdl/core/c64/multitini_c64_reu_cache.sv`: additive read-only outputs (pending write addresses and valid bits, in-flight write address and valid, write-done pulse). No logic change. TD adds the `invalidate` input to the same file (TD §12); both additions are benched together (X-T4).
- `hdl/core/c64/multitini_c64_reu.sv`: one additive observation output, "in a DDR request or wait state", for the readiness-miss counter. No logic change; TD §1.8 and §12 list it. The existing REU suites, T2 (X-T3) and G-REU0 check it.
- `hdl/core/c64/ports/fpga64_sid_iec.vhd`: one additive observation output, the phase-15 DMA slot strobe (the clock that latches `dma_slot_valid`). No logic change; TD §1.8 and §12 list it, with the same checks.
- `hdl/core/c64/multitini_c64_adapter.sv`:
  - `ua_enable` input, a UA read-only DDR port and the status snapshot outputs;
  - `ua_hold` from `C64_REU_HOST` bit 13 into `ua_active`, the `ua_held` echo back to `C64_REU_HOST` bit 14 (both through TD X11), and the C64 diagnostics armed level (`C64_CPU_CONFIG` bit 19, through TD X3) into the UA block;
  - decode (section 2.2), `io_ext`/`io_data`, `irq_ext_n`;
  - audio sum at the 48 kHz point, keeping the SID term's literal text;
  - the REU readiness-miss counter;
  - **no** connection from the UA fault to `reu_fault_latched` or `fatal_load_error`.
- `hdl/core/c64/multitini_c64_expansion_config.sv`: 5-bit mailbox.
- `hdl/common/multitini_control_regs.sv`: mask 0x1F; clear bit 4 unless caps bit 3 is set and bit 3 is written 1; 0x0F0-0x0F8 read decode; 0x0FC decoded only in a probe build.
- `hdl/board/multitini_one_top.sv`:
  - caps `32'h8000001F` (bit 3 UA, bit 4 REU image preload, TD);
  - `ua_enable = expansion_config[4]`;
  - second `multitini_c64_ddr_path` on HP3 in the C64 branch;
  - **split** the `g_no_apple_iigs_ddr` block [V top:2001-2006]: keep `gs_memory_quiescent = 1` for every machine except 5, and tie off `hp3_*` only when `ACTIVE_MACHINE` is neither 0 nor 5. Changing only the condition would leave `gs_memory_quiescent` undriven in the C64 image; it would read 0 and block the PCAP drain [V top:824-827];
  - AND the HP3 quiescent into the drain status;
  - the status snapshot mailbox (C5) and the C64-only read override for 0x0F0-0x0F8.
- `hdl/hdl_sources.txt` `[c64]`: add the four new files after `multitini_c64_reu_cache.sv`.
- `scripts/create_project.tcl`: separate C64 and IIgs branches for HP3; C64 connects `S_AXI_HP3_ACLK` to `c64_wiz/clk_out1` and adds `S_AXI_HP3_0` to the `c64_clk` ASSOCIATED_BUSIF; FCLK0 keeps HP3 in all other images.
- `spec/interface_v0.json`, then regenerate `hdl/generated/multitini_regs_pkg.sv` and `firmware/include/multitini_regs.h`: the UA bit, mask, caps bit, `MT_C64_ULTIMATE_AUDIO_MIN_ABI_MINOR`, `C64_UA_STATUS`, `C64_UA_FETCHES`, `C64_REU_MISSES`, all in the single spec edit for ABI 0.24 that also carries turbo, preload and debug (TD §6.0, §12).

### Firmware
- `firmware/common/core_frontend.c`: the argument 8 row and its four states; the RAM expansion and Media cartridge help on UA images (section 6.2).
- `firmware/include/multitini_core_frontend.h`: `c64_ultimate_audio_enabled`, `c64_ultimate_audio_fault`.
- `firmware/platform/xilinx/main.c`: `change_c64_expansion` arguments 3 and 8 per the section 6.3 table (UA on turns the REU on; REU off turns UA off; one save, then one write); `configure_c64_expansion` restore rule (section 6.3); routing of arguments 2, 3, 8 and 11; frontend state; status strings.
- `firmware/platform/xilinx/debug_service.{c,h}` (new in TD §12): debug-only `:uastat` (section 6.3); `:c64diag` is shared with turbo.
- `firmware/platform/xilinx/supervisor_settings.{c,h}`: global byte 26 bit 2; remember function; default false; reserved-ignored rule for byte 26 bits 3-7 (defined once, with the turbo bits, TD §6.3).
- REU image preload, including the UA hold and the wait for UA held, is turbo-owned firmware in `c64_content_service.c` (TD §12).

### Tests
- **New** `hdl/sim/tb_c64_ua_upstream_ref.sv` (UA-M0, option C), `tb_c64_ua_regs.sv`, `tb_c64_ua_voice.sv`, `tb_c64_ua_fetch.sv`, `tb_c64_ua_coherence.sv`, `tb_c64_ua_mix.sv`; adapter-level additions for T2/T6/T8 (with the preload hold)/T9/T15; the T2 golden trace (committed before any RTL edit by either feature).
- **New** `scripts/test_c64_ultimate_audio.py` (runs the benches, the model checks and T13) and `scripts/ua_model.py` (UA-M0), with expected-value files.
- **New** `tests/host/fixtures/settings_schema10_accept.c` (T10b) and `tests/test_c64ua.py` (T14).
- Extend `tests/host/test_core_frontend.c`, `tests/host/test_supervisor_settings.c`, `tests/test_menu_runtime.py`, `tests/test_c64_expansion_runtime.py`, `tests/test_interface_spec.py`, `tests/test_build_inputs.py`, `tests/test_audio_path.py`, `hdl/sim/tb_control_regs.sv`, `hdl/sim/tb_c64_expansion_config.sv`, and `CMakeLists.txt` for the new host fixture. Add the `:uastat` cases (refusal outside a session with no register access; output inside one) to TD's new `tests/host/test_debug_service.c`.
- **New** `third_party/ultimate_reference/` (option C, mandatory, test-only): `upstream/` files of section 5, `NOTICE.md`, `import_manifest.json`.
- **New** `c64ua/` board PRGs, following `c64fast/` [V licenses/files.csv:241-250; c64fast/README.md:23]: ACME sources (`.a`, GPL-3.0-only) for the decode/IRQ/period/coherence tests, the G3 background-voice loader, the G3b REU stress PRG and the X-DET detection PRG (our own code, following the common 128-read rule); `build.py` that assembles with ACME 0.97 into `build/`, records digests and generates the synthetic 1 MiB X-PRELOAD file; `README.md` with the X-PRELOAD procedure. No `.prg` is committed (`check_repo.py` rejects it [V scripts/check_repo.py:18-21]).
- `scripts/check_repo.py`: add `third_party/ultimate_reference/upstream/` to `tracked_imports`; add `.a` and `.asm` to `SOURCE_SUFFIXES` so a missing licence row on an assembly source fails the check [V scripts/check_repo.py:23, 151]. The 6 tracked `.a`/`.asm` files (3 in `c64fast/`, 3 Game Boy boot sources) already have rows [V licenses/files.csv:197-198, 241-250].
- `scripts/capture_vectrex_build_inputs.py` (C64 branch [V :40-62]): add `scripts/test_c64_ultimate_audio.py`, `scripts/ua_model.py` and its expected files, `docs/C64_ULTIMATE_AUDIO.md`, `docs/C64_ULTIMATE_AUDIO_DESIGN.md`, `third_party/ultimate_reference/**`, `c64ua/**` and the new host fixture. New `tb_c64_*.sv` benches are already covered by the glob. The dev3 template edited the same script [V git show --stat 3fdea82].

### Documentation and provenance
- `docs/C64_EXPANSIONS.md`: a UA section (option; UA on turns the REU on and REU off turns UA off; cartridge rule; lost mirrors above `$DF1F` with UA on; Expand note from G2; deviations; the binding pacing rule under turbo).
- **New** `docs/C64_ULTIMATE_AUDIO.md`: programmer's notes in our own words (register map, real period law, both divider conventions and the PDF's 281, signedness, loop and restart rules, turbo access classes and the binding pacing rule, T13 alias levels). It cites only primary sources and GitHub permalinks pinned to the commits in the header of this design.
- `licenses/files.csv`: rows for every new file, including the option C notes and the T9 "behavioural sequence" note. `provenance/sources.lock.json`: the 1541ultimate `fea2798c` pin. `provenance/imports.lock.json`: option C digests.
- The new `docs/C64_TURBO.md` (TD §12): the binding pacing rule for `$DF00-$DFFF` (section 1.10).
- This design lands as `docs/C64_ULTIMATE_AUDIO_DESIGN.md`; `docs/C64_TURBO_DESIGN.md` carries the turbo-side items (T3 and E4 cases, the UA hold, the shared offset map).
