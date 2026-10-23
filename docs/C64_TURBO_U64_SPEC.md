# C64 turbo evidence: U64 and C64 Ultimate turbo as software sees it

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: U64 / C64 Ultimate turbo: what C64 software sees, and what Multitini should copy (design stage)

Nothing in the repository was changed, and I ran no build. Downloads are under (session scratch, not kept):
- `1541ultimate/` is a shallow clone at d43546f, 2026-10-22.
- `1541u-documentation/` is the docs repo.
- `gh/issue*.txt` holds the GitHub issues with their comments.
- `gh/gist874/` and `gh/gist839/` hold JC-000's measurement programs and data.
- `gh/code/` holds third-party code that uses `$D031`.

Tags: **[V]** checked in source, docs or code; **[M]** measured by a third party on real hardware; **[I]** my inference; **[O]** open.

## A. Corrections to the brief

1. **"All I/O is locked to 1 MHz" is wrong.** On U64E and C64U, reads of VIC and internal-SID registers run at turbo rate. CIA accesses, cartridge-port I/O, I/O1/I/O2 (REU, GeoRAM, UCI) and cartridge ROM are paced to one access per φ2.
   - [M] U64E fw 3.15: `lda $D012` scales with RAM up to 32 MHz; at 48 MHz it reaches 37.4x. `lda $DE80` (I/O1) is capped at one access per φ2 (gist839 `meas_c_external.json`; #839).
   - [M] C64U fw 1.1.0, φ2 ticks per read at index 15: RAM 0.064, `$D419` 0.085, `$D41A` 0.080, `$D012` 0.080, `$DC01` 1.040, `$DE00` 1.045 (`gh/code/amiga64_HARDWARE.md:1301-1313`).
   - [V] At 48 MHz, alternating `$D020` writes change colour at pixel level (#203, 2021). So VIC writes also land at turbo rate.
2. **"Writes cost about 1.9 µs" is a measurement artifact.** In the Doom table, `sta $de00,x` costs 1920 ns but the absolute `sta $dffe` costs 920 ns, the same as a read (`ext-research/doom/docs_georam-vs-reu.md:442-453`).
   - [I] The NMOS indexed store does a dummy read of `$DExx` before the write, so it makes two φ2 I/O accesses.
3. **"Write $00 and then the target" is not a hardware rule.** [M] JC-000 and Amiga64 each set every index with one plain write (gist874 `cpu_rate.tas:158-189`; `amiga64_HARDWARE.md:1292,1313`).
   - [I] Doom's finding came from the forced 1 MHz window after reset (#874; `xahmol_TURBOCONTROL_MANUAL.md:372-378`).
4. **Bit 7 polarity, as software sees it, is 1 = badline timing disabled.** The firmware's internal preference register uses the opposite polarity (section E2).
5. **One slot per φ2 always goes to the VIC, even with the screen off.** This is why index 15 gives 63x and index 14 (48) gives 47x (GideonZ, #874 comment of 2026-09-08 and #203).

## B. Turbo Control modes and which registers respond

[V] `u64_config.cc:325-326,387-394,1625-1636`. The internal enable byte is bit 0 = `$D031` and `$D07A/B`, bit 1 = `$D0BC`, bit 2 = `$D030`. That layout is confirmed by commit 2b20ef37, whose value table was {0,1,3,5,7}.

| Turbo Control (default **Off**) | Effective CPU speed | `$D031` | `$D030` | `$D07A/$D07B` | `$D0BC` |
|---|---|---|---|---|---|
| Off | Stock 1 MHz, exact (preference = $80) | reads $FF, writes ignored | $FF | dead | dead |
| Manual | Menu CPU Speed and Badline Timing | reads $FF, writes ignored ([M] Amiga64:1313) | $FF | dead | live if SCPU Detect is on |
| U64/C64U Turbo Registers | Live register | live | $FF ([M] xahmol:127) | live | live if SCPU Detect is on |
| TurboEnable Bit | Live register | live | live | live | live if SCPU Detect is on |

Other menu defaults [V `u64_config.cc:389-394`]: CPU Speed index 0, Badline Timing Enabled, SuperCPU Detect Disabled. On the C64U the label is "C64U Turbo Registers"; the Doom test matches on the substring "Turbo Registers".

The hardware model [V GideonZ #153; `u64_config.cc:1603-1637`; commit 6bdccc3a]:
- There is a preference register (from the menu) and a live speed register.
- Hardware clears the live register to 0 on reset.
- `$D030` = 1 copies the preference into live.
- Every change to a menu turbo setting strobes `SPEED_UPDATE`, which applies the setting at run time.

## C. Register specification

### $D031 (53297), U64 Turbo Control. Live in Turbo Registers and TurboEnable modes.
| Bits | Write | Read | Source |
|---|---|---|---|
| 3-0 | Speed index to live (table in D1) | Live index | [V] doc `turbo_mode.rst:77-115`; [M] #665; [M] a .cfg left `$D031` non-zero (xahmol lib .c:96-103) |
| 6-4 | Ignored [I] | **Multitini: 0** (U64 unknown) | [O] |
| 7 | 1 = badline timing **disabled**, 0 = enabled | Live flag (readback of `$8F` confirmed) | [V] doc:84; [M] #665 |
| Not live | Ignored | $FF (unused VIC register) | [V] doc:79-81 |

### $D030 (53296), TurboEnable bit. Live only in TurboEnable mode.
| Bit | Write | Read |
|---|---|---|
| 0 | 1: live := preference (menu speed and menu badline) [V #153]. 0: live := $00, meaning 1 MHz with badlines on [V doc:124] | 1 = turbo on [V doc:128-132]. **Multitini: on = live index ≠ 0** [I] |
| 7-1 | Ignored [I] | **Multitini: 1, so the byte reads $FE or $FF** [O]. MiSTer's VIC uses the same layout (`video_vicII_656x.vhd:1821`) |

### $D07A (53370) normal, $D07B (53371) turbo, $D079. SuperCPU-style and write-only. Live in register modes.
- [V] The doc lists them as "Software Speed Select Normal / Turbo (20 MHz)($079)" (`turbo_mode.rst:134-153`).
- [M] POKE 53370,0 and POKE 53371,0 work on the U64/C64U (#666, comment by bucko219).
- **Multitini:**
  - Any write to `$D07A`: live := $00.
  - Any write to `$D07B` or `$D079`: live := preference, the same as `$D030` = 1.
  - Reads return $FF.
  - [I] The target speed is not documented. `$D079` is decoded because the doc names "$079" and VICE treats `$D079` as `$D07B` (`ext-research/scpu/scpu64mem.c:723-725`).
- Other SuperCPU registers (`$D072/3`, `$D07E/F`, `$D0B0-$D0BB`) are not emulated; they read the internal VIC (GideonZ, #666).

### $D0BC (53436), SuperCPU detect. Read-only, separate option, default off.
- [V] Live when SCPU Detect is Enabled and Turbo Control is not Off (`u64_config.cc:1626-1631`).
- [O] The value the U64 returns is not published.
- The SuperCPU convention is bit 7 = 0 means present: VICE returns DOS-ext<<7 | RAMLink<<6 | optimisation bits (`scpu64mem.c:675-679`), and cc65 tests bit 7 only after it has confirmed a 65816 CPU.
- [M] #654 reports that detection worked only in Manual mode with a speed above 1 MHz.
- **Multitini: $00 when enabled. Do not implement it in the first release.**

### Address decode
- [I] The U64 has to decode at least A7-A0 to tell `$D07A/B` and `$D0BC` apart from VIC mirrors. Whether `$D031` also appears at `$D071`, `$D0B1` or `$D131` is [O].
- **Multitini:**
  - Decode A9-A0 exactly inside the VIC window, with no mirrors.
  - Respond to CPU accesses only, not REU or firmware DMA.
  - Writes still reach the VIC, where they have no effect. This requires keeping FPGA64's C128 path off: `turbo_en` = 0 at `video_vicII_656x.vhd:1756,1821`.
  - The VIC decodes only 6 address bits, so the turbo decode must sit outside it.

## D. Behaviours

### D1. Speed table and delivered rate
[V] Index 0-15 means 1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64 on U64E2/C64U (`u64_config.cc:322`). The original U64 table is 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48.

[M] #874 (C64U fw 1.1.0, core 1.49, NTSC, screen off): every index delivers its label exactly except 14 (47.00) and 15 (63.03). The U64E is short at its own top two indices (39 and 47).
- Slots are locked to the dot clock, 64 per φ2 (#203), which is exactly 2 × c64_clk on Multitini.
- Software cannot tell U64 from C64U through `$D031`. It uses timing, for example the raster probe in `xahmol_ultimate_turbo_lib.h`: 16 lines means 64 MHz, about 21 lines means 48 MHz.

**Multitini:** CPU cycles per φ2 = 1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, **47, 63**, spread evenly.
- [M] The I/O1 data supports even spreading: a 4-cycle `lda` with one I/O access costs 2 φ2 at 3-5 MHz and 1 φ2 at 6 MHz and above (meas_c).

### D2. Badline timing (CPU only; BA and the VIC are unchanged)
Source for this section: [V] `turbo_mode.rst:164-176` and #665.
- **Enabled:** the CPU halts whenever BA = 0, including badline and sprite fetch cycles. [M] At index 0 with the VIC on, the CPU runs at 0.94x (#874).
- **Disabled:** the CPU keeps running while BA = 0 for internal accesses. Accesses on the external bus, including real SID sockets, still wait for BA = 1. The VIC keeps priority, so graphics stay correct.
- [V] With badline timing disabled, the C64U also loses slot 32 in cycles where the VIC takes the φ2 half (#665 diagram).
- **Multitini:**
  - At index ≥ 1 with badline timing enabled, gate all island cycles, reads and writes, while BA = 0.
  - Index 0 with badline timing enabled must be the stock, exact path. [M] On the C64U, Off and Registers index 0 time identically to 0.01 tick (Amiga64:1305-1307).
  - With badline timing disabled, internal accesses run through BA low and φ2 I/O waits for BA = 1.

### D3. I/O timing contract at turbo (U64E2/C64U measured)
| Class | Timing | Multitini |
|---|---|---|
| RAM, BASIC/KERNAL/char ROM, `$00/$01` | 1 slot | Fast |
| VIC registers | Turbo rate; about 1 extra slot at indices ≥ 13 [M] | **Target fast.** φ2 is a stopgap and a documented deviation |
| Internal SID (UltiSID) | Reads at turbo rate [M]; writes untimed | Same as VIC |
| Colour RAM | [O] | Fast (dual-port); high nibble = last VIC byte [I] |
| CIA1/CIA2 | One access per φ2 [M] | φ2 |
| I/O1/I/O2 (REU registers, cartridge I/O, GeoRAM) | One access per φ2, reads and writes alike [M] | φ2 |
| Cartridge ROM, including emulated cartridges | φ2 ([V] #204, #609) | φ2 |

The φ2 access rule [M]:
- The access happens in the φ2 half and completes at the end of that native cycle.
- A request made more than about 0.5-0.6 µs into the cycle waits for the next one.
- Each I/O-space bus cycle counts as its own access: indexed-store dummy reads, RMW double writes, and page-cross dummy reads.
- [I] No posting is visible. Multitini must not post writes, because of REU triggers and IRQ acknowledges.
- CIA timers, TOD and the raster counter run in real time at any speed ([M] Amiga64:652-706; #874 timebase check).

### D4. REU
- [V] The CPU must stop on the triggering write (`$DF01` execute, or `$FF00` when armed), and no later instruction may run until the transfer is done. Sources: `reu_turbo_test.py:4-11`; `reu.vhd:286-299` (`g_no_dma_delay`, 2026-08-27); release 3.15:189.
- [M] DMA rate: C64U is flat at 1 byte/µs (`doom/reu-format.md:24`). U64E fw 3.15 tracks the CPU up to 3 MHz, then gives about 5 B/µs stash and 3.5 B/µs fetch, not monotonically (#839).
- **Multitini: 1 byte per φ2 at every speed.**

### D5. Reset and other events
| Event | U64 behaviour | Multitini |
|---|---|---|
| Any C64 reset | Live is cleared to 0 [V #153]. The FPGA forces 1 MHz for about 2.0 s and ignores `$D031` writes during that time [M #874:74; xahmol:66-72,405; FPGA-only commit 433c53c8; fw 3.14d note]. The firmware reset task re-applies the settings [V `u64_config.cc:1034-1052`]. [I] After the window, live = preference in every non-Off mode, Manual included ([M] `reu_turbo_test.py:118-121`) | Stock 1 MHz for 2.0 s. **Latch writes made in the window and apply them when it ends.** If nothing was written, apply the preference (Manual, Registers). **In TurboEnable mode, live stays 0** |
| Change to a menu turbo setting, or a 3.15 per-title .cfg | live := preference immediately [V hooks `u64_config.cc:923-926`] | Same |
| `$D031` write | Takes effect after about 14-20 φ2 cycles [M Amiga64:1313] | Next native φ2 boundary |
| NMI/RESTORE, IRQ, freeze or overlay without a change | Nothing documented [I] | No change |
| Host DMA or REST memory reads | CPU paused briefly [M] | Pause only |
| IEC activity | No documented slowdown. [M] #737 shows JiffyDOS still affected at 8 MHz. xahmol's line "window also follows IEC activity briefly" is unverified | Keep the existing IEC guard as an option, default on. This is a deviation |

### D6. Cartridges
U64 [V]:
- Turbo runs with cartridges, and cartridge accesses go at the φ2 rate.
- The "Dynamic" bus mode probes and caches which external-port regions need 1 MHz (fw 1.37). It does not apply to Multitini, which has no physical port.
- "Compatibility" mode forces 1 MHz and exposes every cycle (release 3.15:127-133).

**Multitini:** allow turbo with cartridges. Send every access in a cartridge window, and the accesses cartridges snoop, through the native slot. Offer an optional "Cartridge forces 1 MHz" setting.

## E. Resolved

1. **Decode and liveness by mode.** See B and C (high confidence for the documented addresses).
2. **Bit 7 polarity.**
   - Documented: 1 = disabled (`turbo_mode.rst:84`).
   - Software writes `$8F` to turn badlines off: #665; xahmol `TURBO_BADLINES_OFF $80`; idun ACE `acecall.asm:1106-1110`; backlot-64 `b64.inc:40`. Doom's `$0F` keeps badlines on.
   - The internal preference register is the inverse: `SPEED_PREFER = idx | (enabled<<7)` with en_dis "Enabled" = 1 (`config.cc:963`). Off writes $80 and must be stock-exact, so preference bit 7 = 1 means enabled.
   - Confidence: high for writes. Readback right after a copy from the menu is not verified.
3. **The 1.9 µs write cost** is the dummy read plus the write (see A2). High confidence.
4. **63x and 47x** come from the VIC's reserved slot (A5). High confidence.
5. **The $00→$0F rule** came from the post-reset window (A3). Medium-high confidence.
6. **The ~2 s window** is FPGA logic since 3.14d and covers PAL/NTSC detection for JiffyDOS. High confidence.
7. **Writes and the menu setting.** Software writes change only the live register. The menu value is untouched. Live stays until reset, a menu change or another write.
8. **TurboEnable and the C128 conflict.**
   - `$D030` = 1 gives the full menu speed, not 2 MHz.
   - The VIC keeps priority, so graphics stay correct, unlike on a C128.
   - Software that writes `$D030` for 2 MHz or by accident speeds up and breaks: #153, Krestology (#287), Dawnfall (#665).
   - A 2 MHz menu speed gives timing close to x128 and slightly faster than a real C128, which stretches I/O cycles (#301). Insomnia then reports an 8502.
9. **SuperCPU detect defaults off** [V `u64_config.cc:394`]. [I] The CPU is a 6510, so software that trusts `$D0BC` may run 65816 code. #654 also says it does not behave like a real SuperCPU.

## F. Unresolved: proposed Multitini choice and why

| # | Open | Proposed choice | Why |
|---|---|---|---|
| 1 | Mirrors of `$D031` and the others | Exact decode; add `$D079` | Fewer accidental speed changes from VIC-area fills |
| 2 | `$D031` bits 6-4 and bit 7 readback | Store bits 7 and 3-0; bit 7 reads as "disabled" | One consistent polarity; a stray `$FF` write reads $8F, not "absent" |
| 3 | `$D030` read bits 7-1, meaning of "on" | $FE \| (index ≠ 0) | Matches MiSTer and the doc's bit 0 |
| 4 | Target of `$D07A`/`$D07B` | 0 / preference | Same as `$D030`; "turbo" means the user's speed |
| 5 | Value of `$D0BC` | $00, feature deferred | SuperCPU convention; off by default anyway |
| 6 | Switch latency | Next φ2 boundary | No software can depend on about 20 µs |
| 7 | Writes in the reset window; TurboEnable at boot | Latch; TurboEnable boots off | Fixes #153/#287-type crashes; known software works either way |
| 8 | VIC/SID fast path | Required to match; φ2 allowed as a documented phase-1 deviation | Function is unchanged; only timing differs |
| 9 | Colour RAM, and CIA during BA low | Colour RAM fast; CIA allowed during BA low | Neither is on the VIC's fetch path in FPGA64 |
| 10 | Exact cutoff point in the cycle for φ2 I/O | Model a cutoff near fast clock 36 of 64 | Copies the U64E 2-vs-1-φ2 boundary |
| 11 | Model ID / "C77" 77 MHz table (#911) | Use the C64U table only | No register exists for software to identify the model |
| 12 | IEC slowdown | Keep the guard (option) | The 1541 core is cycle-exact; fast loaders need 1x |
| 13 | FLI floating-bus colour and the `$0000/$0001` quirk while accelerated | Keep native semantics for forwarded accesses | U64 behaviour is unknown and rarely visible |

## G. Software that uses U64 turbo and how it detects it

| Software | Use | Detection |
|---|---|---|
| Doom C64U (slesinger/doom) | `$D031` = $00 then $0F; needs the Turbo Registers mode | None (harmless in VICE) |
| xahmol ultimate-uci-oscar64, mandelbrot-upic | `$D030` \|= 1 then `$D031`; slow = `$D031`=0, then `$D030` &= ~1 | `$D031` ≠ $FF, then a CIA TOD or raster probe |
| Stevenic backlot-64 | `$8F` fast, `$00` slow | `$D031` ≠ $FF |
| idun-cartridge ACE | Writes an index, bit 7 optional | `$D031` ≠ $FF |
| Sabbi GNW64 | `$D031`=$0B; `$D030`=0 turns turbo off | `$D030` bit 1 for a C128; `$D0BC`=$01 or `$D031` reads back $0B |
| JC-000 c64-https, nist-curves, x25519, Amiga64, test probes | Manual or `$D031` sweeps | Timing-based liveness probes |
| VanessaE's MOD player | `$8F`/`$0F` | — |
| Image BBS 3.0 | `$D07A`/`$D07B` | SuperCPU checks |
| Ultiverzum (Singular, CSDb 235349) | Manual 48 MHz, badline timing on | None |
| C128 2 MHz idiom (Insomnia, Krestology, EasyFlash titles) | `$D030` bit 0 | `$D030` ≠ $FF |

## H. Tests to copy as acceptance gates
- gist874 `cpu_rate.tas`: the delivered-rate table.
- gist839 `meas_c.tas`: timing per I/O class.
- Amiga64 M5: timing of `$D419`/`$D012`/`$DC01`/`$DE00`.
- Doom `geobench`/`reubench`/`sidtest`/`irqtest`.
- `reu_turbo_test.py`: CPU halts on the `$DF01` write.
- xahmol `speed_probe.c`: index 15 gives 16 lines, index 14 gives 21 lines.
- GNW64 detection path.
- `$8F` digi with the screen on: no growl.

## I. Sources
- Docs: `turbo_mode.rst`; `howto/release_3.15.md`; https://ultimate64.com/Firmware (3.14d); https://ultimate64.com/OlderFirmware (1.34, 1.37)
- Firmware: GideonZ/1541ultimate `software/u64/u64_config.cc`, `software/system/u64.h:106,134-135`, `software/io/c64/c64.cc:67-68`, `fpga/cart_slot/vhdl_source/reu.vhd`; commits 2b20ef37, 97df0810, 6bdccc3a, d3209f54, 433c53c8
- Issues: #153, #203, #204, #287, #301, #609, #654, #665, #666, #737, #839, #874, #895, #911
- Gists: gist.github.com/JC-000/f13c5648da2f098db5ffb973cfd3d32e and /da2d191b285a3d8f5c5ef084d886d734
- Third-party code: JC-000/Amiga64 `docs/HARDWARE.md`; xahmol/ultimate-uci-oscar64; Sabbi/GNW64; idun-project/idun-cartridge; Stevenic/backlot-64; cc65 `acc_detect_scpu.s`, `acc_detect_c128.s`
- Doom: slesinger/doom (`src/defs.asm:1034-1052`, `src/main.asm:241-264`, `docs/georam-vs-reu.md:442-470`, `pipeline.md:1518-1530`)
- SuperCPU model: VICE `scpu64mem.c`
- Demo: https://csdb.dk/release/?id=235349