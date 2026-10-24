# Multitini One C64: SuperCPU Detect at $D0BC (design)

Design only. Nothing here is implemented or tested.

- Date: 2026-10-24.
- Branch `c64-turbo`. Drafted against `c7c2fba` (M0.7.0dev6). Every repository anchor was re-read at `3df6d04` (M0.7.0dev7, C64 image `4D00C707`, ABI 0.25), the branch head on 2026-10-24, with a clean tree.
- Milestone XM3 of [C64_U64_EXTENSIONS_PLAN.md](C64_U64_EXTENSIONS_PLAN.md) ("the plan"). Decision record Q21. Deviations D20 and D21.
- Sibling designs:
  - [C64_U64_EXTENSIONS_PLAN.md](C64_U64_EXTENSIONS_PLAN.md): overview, shared frame (registers, settings, menu arguments, address map, stored/effective layers, budget, crossings), milestones and gates.
  - [C64_TITLE_CONFIG.md](C64_TITLE_CONFIG.md) ("the title-config design"): same-name `.cfg` files, the title overlay and its end events.
  - [C64_UCI.md](C64_UCI.md) ("the UCI design"): the Ultimate Command Interface, including Control `$50` LOAD_CONFIG.
  - [C64_MULTI_SID.md](C64_MULTI_SID.md) ("the multi-SID design"): up to 8 SIDs; it shares the fast-read address nets this design loads (section 4.2).
- **Allocations used, all from the plan:** `C64_CPU_CONFIG` bit 20, `C64_CPU_CAPS2` bit 15, ABI 0.26, `settings.bin` byte 26 bit 3, Options argument 14, milestone XM3, decision Q21.
- **Citation base.**
  - Repository paths are relative to the repository root. Line numbers are at `3df6d04`. `firmware/platform/xilinx/main.c` is cited by function name, with an approximate line where it helps.
  - `1541ultimate <path>:<lines>` = GideonZ/1541ultimate at `d43546fe` (GPL-3.0 firmware). The U64 machine FPGA is not in that tree (section 2.2).
  - `1541u-documentation <path>:<lines>` = GideonZ/1541u-documentation at `2da134b`.
  - `1541ultimate #N` = GitHub issue N of GideonZ/1541ultimate, as read on 2026-10-24.
  - `area6510 <image> @<byte>` = GEOS, GDOS64 and MegaPatch source disk images of Bitbucket `mkgit64/area6510` (`gd-*.d81`, `mp-*.d81` at `a7517970`; the commit of `gmakeboot64-src.d81` was not recorded [O]). Byte offsets point at the assembler source text inside the image.
  - Other third-party programs (VICE, Denise, MAME, Ozmoo, GNW64, CCGMS, cc65, Ultima IV Remastered, mist64/geos Wheels, GoDot) are cited by project and file. They were read on 2026-10-24; versions are not pinned [O]. Facts only: no third-party code or binary is copied into the repository.
  - `build/...` reports are local Vivado outputs. `build/` is not under source control, so those figures cannot be checked from the repository.
  - TD = [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), ISS = [C64_TURBO_U64_ISSUES.md](C64_TURBO_U64_ISSUES.md), SPEC = [C64_TURBO_U64_SPEC.md](C64_TURBO_U64_SPEC.md), FVIC = [C64_TURBO_FAST_VIC.md](C64_TURBO_FAST_VIC.md), PRIOR = [C64_TURBO_PRIOR_ART.md](C64_TURBO_PRIOR_ART.md). Their line numbers are edit targets for section 4.4.
  - Sibling documents are cited by section title, because their numbering may change.
- **Tags.** [V] verified in source (path:line). [D] documented (doc:line). [R] user report. [I] inference. [O] open: needs a measurement or a decision. **USER** marks a decision for the user, with a recommended default.

Nothing in this document claims hardware support.

---

## 0. Summary

### 0.1 What software sees

- A new Options row, **"SuperCPU Detect (D0BC)"**, default **Disabled**, in the turbo group after "Badline timing".
- Enabled, with Turbo Control not Off: a CPU read of `$D0BC` returns **`$01`**. Programs that test for a CMD SuperCPU find one.
- Disabled, or Turbo Control Off: nothing changes. The VIC answers `$D0BC` with `$FF`, as today.
- Visibility depends only on configuration. Speed, live `$D031`, the reset window and every forced-exact cause do not hide it. This fixes 1541ultimate #654.
- A toggle never moves the live speed: it is not an apply event.
- No other SuperCPU register is added, unless the user chooses Q-SC4 (a) (read-only `$D0B8`/`$D0B9` in the register modes).

### 0.2 The hazard and the advice

- GDOS64 and the GEOS SuperCPU64-Patch take 65816 or SuperCPU-RAM paths when `$D0BC` is not `$FF`. Both crash on our 6510 (section 2.4). No `$D0BC` value avoids this; only the default and the warnings can.
- A title `.cfg` can turn Detect on for one title. That overlay survives a disk swap, a C64 software reset and, for a D64 title, Options → Reset while drive 8 holds a disk (section 3.7).
- So every warning tells the user to use **"Revert title settings"** (or set the row to Disabled) before booting GEOS or GDOS64. Revert always ends the overlay.

### 0.3 Gates

- G-SCPU (section 7.5): benches on both CPU paths, host tests, PAL routed timing, and board tests including GNW64 and G-REU0.
- Stock behaviour: CONFIG bit 20 resets to 0, so at the default `$D0BC` takes today's VIC path. The stock bus trace is unchanged by construction (section 7.1); Expand by Bonzai (G-REU0) is re-run on the board (B8).
- **Timing baseline.** The dev7 PAL image closes the fast island at +0.030 ns under a one-build waiver, below the plan's +0.100 ns ceiling. XM3 cannot meet that ceiling without the plan's timing decision (section 4.2, Q-SC18).

---

## 1. Goal and scope

**Goal.** Add the U64 option "SuperCPU Detect (D0BC)" to the C64 Options tab, with the behaviour of section 0.1.

**In scope.**
- `$D0BC` read decode in the turbo register block, on the native (exact) and the fast (turbo) CPU read paths.
- The config bit, the caps bit, the clamp, the ABI constant.
- The global setting, the menu row, its help and status texts.
- The option-registry row that the title-config design and UCI `$50` read. The `.cfg` reader itself belongs to the title-config design.
- Benches, host tests, board tests, and a U64 measurement item (E7-C).

**Out of scope** (each is a decision in section 8):
- `$D0B8`/`$D0B9` speed status (unless Q-SC4 (a)), `$D0B0-$D0BB`, the `$D0B5` switch, `$D0BD-$D0BF`.
- The `$D07E`/`$D07F`/`$D072-$D078` SuperCPU hardware registers.
- SuperCPU RAM at `$D200-$D3FF`, a 65816, SuperRAM.
- A controller hotkey for the option.
- The U64 UCI unlock at `$D038`/`$D036` (the plan, "I/O address and priority map").

**Reversal.** This reverses Q11 [D TD:172] for `$D0BC` only. `$D0B8`/`$D0B9` stay out unless the user chooses Q-SC4 (a), which this design recommends because two GEOS components need them in the register modes (section 2.4). The new decision record is Q21.

---

## 2. U64 behaviour

### 2.1 Firmware facts

| # | Fact | Tag |
|---|---|---|
| U1 | Item `CFG_SUPERCPU_DET` = `0x54`, ENUM "SuperCPU Detect (D0BC)", values `Disabled`/`Enabled`, default 0 = Disabled | [V 1541ultimate software/u64/u64_config.cc:174, 394] |
| U2 | It sits in store "U64 Specific Settings", group "Turbo Settings", after Turbo Control, CPU Speed and Badline Timing | [V u64_config.cc:2944-2948] |
| U3 | Firmware sets bit 1 (`0x02`) of `C64_TURBOREGS_EN` (`C64_IO_BASE + 0x02`) when Detect is on and Turbo Control is not Off. Base values per mode: Off `0x00` (forced), Manual `0x00`, U64 Turbo Registers `0x01`, TurboEnable Bit `0x05` | [V u64_config.cc:326, 1625-1634; 1541ultimate software/system/u64.h:106] |
| U4 | So the firmware enables `$D0BC` in Manual as well: Manual + Detect writes `0x02`, with `$D031`/`$D07x` dead | [V u64_config.cc:326, 1631] [I] |
| U5 | A Detect change runs `setCpuSpeed`, which writes `C64_SPEED_PREFER` and strobes `C64_SPEED_UPDATE = 1` | [V u64_config.cc:926, 1634-1636; u64.h:134-135] |
| U6 | The same function runs once at boot (`effectuate_settings`) | [V u64_config.cc:1054, 1079] |
| U7 | Turbo Control = Off disables (greys) the Detect row; other modes enable it. The stored value is kept | [V u64_config.cc:1603-1622] |
| U8 | A `.cfg` line is `SuperCPU Detect (D0BC)=Enabled` under `[U64 Specific Settings]`. Item names match case-insensitively. The C64U menu label is "SuperCPU detect (D0BC)". The docs spell it "($D0BC)" | [V 1541ultimate software/userinterface/configio.cc:205-219, 349; R chrisgleissner/c64commander `docs/research/menu-config-mapping/prompt.md:562-565`; D 1541u-documentation config/turbo_mode.rst:63] |
| U9 | Docs: "SuperCPU Mode Detect Register … only available when it is separately enabled … **read only**" | [D 1541u-documentation config/turbo_mode.rst:63-68, 154-162] |

### 2.2 FPGA and observed behaviour

| # | Fact | Tag |
|---|---|---|
| U10 | The U64 machine core is closed. The public tree has bitstreams only (`external/u64.sof`, `external/u64e2_*.bit`). The `$D0BC` value, decode width, mirrors, timing and DMA behaviour cannot be read from source | [V 1541ultimate external/; O] |
| U11 | #654 (C64U 3.14, open): detection works only with Turbo Control = Manual and CPU speed > 1 MHz; it is not visible in "C64U Turbo Registers" mode | [R 1541ultimate #654] |
| U12 | #654 also asks for visibility with Turbo Control Off and for a `$D0B5` switch bit. GideonZ (2026-03-30): "When the setting is OFF, none of the registers should be visible, nor accessible.. OFF should mean off" | [R 1541ultimate #654] |
| U13 | Likeliest cause of U11: the FPGA gates the register on the effective speed being above 1x; the live `$D031` is 0 after reset in Registers mode | [I] |
| U14 | `$D0B0-$D0BB` (incl. `$D0B8`) are not emulated; the internal VIC answers | [R 1541ultimate #666, GideonZ] |
| U15 | Users: "a bad idea to use the word 'SuperCPU'"; "It's just the speed setting register". GDOS64 author: "one register missing which make the mouse 'wobbling'" | [R Lemon64 forum t=87516; 8-Bit Boyz forum, GDOS64 thread] |
| U16 | MegaPatch 3.3r7 never detected a SuperCPU on a U64; DarkVision advises keeping Detect Disabled | [R Lemon64 forum t=76762] |

### 2.3 What a real SuperCPU returns (emulator models)

| # | Fact | Tag |
|---|---|---|
| S1 | VICE: reads of `$D0B0-$D0BF` go to the SuperCPU; `$D0BC-$D0BF` = `dosext<<7 \| ramlink<<6`, ORed with `optim & 7`. `$D0B8`/`$D0B9` = `soft_1mhz<<7 \| any_1mhz<<6`, ORed with `optim & 7` | [V VICE scpu64mem.c:665-685, 866-880] |
| S2 | VICE reset sets `optim = $C7`, so `$D0BC` reads `$07` after reset. Any `$D074-$D077` write makes the low bits 0, so it then reads `$00` | [V scpu64mem.c:705-712, 1520] |
| S3 | Denise and MAME use the same formula and the same `$C7` reset | [V Denise supercpu.cpp:113, 775-792; MAME supercpu.cpp:368, 1041-1049] |
| S4 | A real SuperCPU read `PEEK(53424)` (`$D0B0`) = 65 at 1 MHz and 1 at 20 MHz: low bits `%001`. With no DOS extension and no RAMLink, `$D0BC` would then read `$01` | [R commodore-128.org forum msg 17799] [I] |
| S5 | `$D0Bx` has no mirrors at `$D1Bx`/`$D2Bx`/`$D3Bx`; `$D200-$D3FF` is SuperCPU RAM | [V VICE scpu64mem.c:913-965] |

### 2.4 Software that reads `$D0BC`

| Program | Test | Effect when it finds a SuperCPU on a 6510 | Tag |
|---|---|---|---|
| GDOS64 boot (`DetectSCPU` → `CheckSCPU` → `InitDeviceSCPU`) | `lda $D0BC / cmp #$ff / beq no_scpu`: **any value ≠ `$FF`** | Installs 65816 patches for MoveData, InitForIO, DoneWithIO, ClearRam, FillRam. Crash at the first patched call | [V area6510 gd-d1-symb.d81 @150485; gd-d3-prog.d81 @162105, @147106] [I crash] |
| GEOS SuperCPU64-Patch (AUTO_EXEC) | `bit $d0bc / bmi` (`IsSCPUaktiv`) | Writes `$D07E`/`$D074`/`$D07F`/`$D07B`, copies code to `$D2A6-$D2FF` and jumps there. Crash at every boot from that disk | [V area6510 gmakeboot64-src.d81 @331832, @343872] [I crash] |
| Ozmoo | `lda $d0bc / and #$80 / beq .supercpu` | Skips REU set-up ("SuperCPU and REU doesn't work well together"): slower, still runs | [V Ozmoo asm/ozmoo.asm:1042-1061] |
| GNW64 | after `$D031 = $0B`: `lda $d0bc / cmp #$1 / bne` | Takes the "U64 / SCPU" timed path. Needs exactly `$01` | [V GNW64 main.asm:111-145] |
| Ultima IV Remastered | `bit $d0bc / bmi` | Brackets serial I/O with `$D07A` … `$D07B`: helpful in register modes | [V u4remastered src/ifflinit/loaderinit.s:140-143] |
| CCGMS | `lda $d0bc / asl / bcs` | Prints "SuperCPU Enabled!"; uses `$D07A`/`$D07B` | [V ccgmsterm src/init.s:21-29] |
| Wheels `ToBasic` | `ldx $D0BC / bmi` | 65816 stores only if the RAM type is SuperRAM: safe with an REU | [V mist64/geos kernal/wheels/tobasicb.s:36-48] |
| cc65 `detect_scpu()` | `.byte $E2,$01` 65816 test first, then bit 7 | Returns "not found" on a 6510 whatever `$D0BC` reads | [V cc65 acc_detect_scpu.s:15-33; third_party/c64/upstream/rtl/t65/T65_MCode.vhd:571-583] |
| GoDot ScanRAM | 65816 decimal test first | Safe on T65 | [V GoDot modifiers/m_.ScanRAM.a:217-234; third_party/c64/upstream/rtl/t65/T65_ALU.vhd:92-113] |
| SuperMouse64 | **no `$D0BC` test**; every poll: `ldy #1 / bit $D0B8 / bpl / dey`, `sta $D07A`, pot wait, `sta $D07A,y` | Not a `$D0BC` consumer. With `$D0B8` = `$FF` it "restores" via `$D07A`, so in Registers/TurboEnable mode GEOS drops to 1 MHz at every poll, 1351 or not (Q-SC4) | [V area6510 gd-d2-kernal.d81 @138202; I for "1351 or not"] |
| MegaPatch 3 ScrSaver64 | **no `$D0BC` test**: saves `$D0B8`, writes `$D07A`; on exit `ldy #0 / bit SpeedFlag / bmi / iny / sta $D07A,y` | Not a `$D0BC` consumer. `$D0B8` = `$FF` makes it write `$D07A` again, so register-mode GEOS stays at 1 MHz after every screen-saver run (Q-SC4) | [V area6510 mp-d3-system.d81 @11251, @12929, @28843 (`SCPU_HW_NORMAL = $d07a`, `SCPU_HW_SPEED = $d0b8`); no `$D0BC` symbol in that image; hdl/core/c64/multitini_c64_turbo_regs.sv:58] |

GNW64's comments call the `$D0BC == $01` path "Is there a Turbo U64?" and the `$D031` path "Is there an SCPU?". Its README tells U64 users to select "U64 Turbo Registers" [V GNW64 README], where #654 says `$D0BC` is invisible. So the comments are probably swapped, and the `$01` test targets a real SuperCPU [I]. Either way the code expects `$01` [V].

---

## 3. Our design

### 3.1 Read and write behaviour

"Effective Turbo Control" is the `mode` field the turbo block decodes: `mode = (config_word[10] || legacy_turbo_request) ? config_word[5:4] : 0` [V hdl/core/c64/multitini_c64_turbo_regs.sv:42]. Firmware writes mode 0 when Turbo Control is Off [V firmware/common/c64_features.c:96]. "Detect" is `C64_CPU_CONFIG` bit 20 after the clamp.

| Effective Turbo Control | Detect | CPU read of `$D0BC` | CPU write of `$D0BC` |
|---|---|---|---|
| Off (mode 0) | any | VIC answers: `$FF` (unchanged path) | VIC, no effect (unchanged) |
| Manual, Registers, TurboEnable | Disabled | VIC answers: `$FF` (unchanged path) | VIC, no effect |
| Manual, Registers, TurboEnable | Enabled | **`$01`** | VIC, no effect; the read value stays `$01` |

**Value `$01`.** Reasons, with evidence in section 2:
- Bit 7 = 0 passes every bit-7 presence test (Ozmoo, U4R, CCGMS, Wheels, SuperPatch, GDOS64 configure).
- Bit 6 = 0 (no RAMLink).
- `$01` is the only value that passes GNW64's `cmp #$01` [V GNW64 main.asm:123-125].
- `$01` matches the real-hardware low bits `%001` [R S4].
- `$00` (SPEC:79, 180) and `$07` (the emulators' reset value) both fail GNW64.
- No value except `$FF` keeps GDOS64 boot off its 65816 path [V]. So the value cannot remove that hazard; only the default and the warnings can.
- The U64 value is unknown. It is item E7-C (section 7.4). If E7-C finds a different U64 value, keep `$01` unless a program is found that needs the U64 value (Q-SC1).

**The value is a constant.** It does not follow speed, `$D031`, `$D07A`/`$D07B` writes, DOS extension or RAMLink state. No program found reads more than bit 7 or the exact `$01` [V section 2.4].

**Visibility depends only on configuration** (fix for #654, section 5). With Detect Enabled and mode ≠ 0, `$D0BC` reads `$01` regardless of:
- speed index 0-15, including index 0 (1 MHz);
- badline timing;
- live `$D031`, including 0 after reset in Registers mode and `$00` after `$D030` = 0 in TurboEnable mode;
- the 2-second reset window, a cartridge, the content loader, the disk auto-slow guard, a menu pause (all forced-exact causes [D TD:220]);
- the CPU path: native/exact requests and fast FIO reads;
- PAL or NTSC, `c64u_top`, Follow CPU.

Reason for the reset-window and forced-exact rule: programs probe once at start. A PRG that the firmware loads and runs, or a title started with a `.cfg`, can probe inside the 2-second window [I]. A gate on effective speed would hide the register then, which is the #654 fault.

**Debug override.** The debug `:speed` override replaces SPEED and MODE with `0x10 | index`, so it forces mode 1 [V firmware/common/c64_debug.c:66-68]. Without a rule, `:speed N` with Turbo Control Off and Detect Enabled would make `$D0BC` read `$01` inside a UART session, a register the user's settings switch off. Rule: when the override is active and the incoming config word has mode 0, `mt_c64_debug_effective_overlay()` clears `MT_C64_CPU_CONFIG_SCPU_DETECT` before it forces mode 1 (section 4.3). With Turbo Control on, the override keeps bit 20 and Detect stays visible. So a debug speed command never exposes a `$D0BC` that the settings hide.

### 3.2 Decode

| Address or access | Behaviour with Detect visible | Source of truth |
|---|---|---|
| CPU read, I/O visible, `$D000-$D3FF` page, A9..A0 = `$0BC` | `$01` | native: `ext_cs_vic` and `native_rd_addr` [V hdl/core/c64/multitini_c64_adapter.sv:505, 566]; fast: `!fio_rd_sid` and `fio_rd_addr = cpu_address[9:0]` [V adapter.sv:506; hdl/core/c64/multitini_c64_cpu_island.sv:165-166] |
| `$D03C`, `$D07C`, `$D0B0-$D0BB`, `$D0BD-$D0BF` | VIC (`$FF`) | 10-bit exact compare. Exception only if Q-SC4 (a) is chosen: `$D0B8`/`$D0B9` in the register modes, independent of Detect |
| `$D1BC`, `$D2BC`, `$D3BC` | VIC (`$FF`) | A9..A8 ≠ 0 fails the compare; a real SuperCPU has no such mirrors either [S5] |
| `$D4BC` (SID page), `$D8BC` (colour RAM), `$DCBC` (CIA) | unchanged | `!fio_rd_sid` gate; colour and forward cycles never take `io2_q` from the FIO data [V cpu_island.sv:209; hdl/core/c64/multitini_c64_island_map.sv:63-76] |
| `$D0BC` with I/O banked out (RAM or CHAR ROM) | RAM / CHAR, unchanged | `ext_cs_vic` = 0 on the native path; the island classifies the page as local [V island_map.sv:53-54, 63] |
| REU DMA read of `$D0BC` | VIC (`$FF`) | DMA takes `dma_din <= cpuDi` straight from buslogic; the override acts only on `proxy_native_data`, the CPU's input [V hdl/core/c64/ports/fpga64_sid_iec.vhd:473, 808, 1287; adapter.sv:505, 537]. Same as `$D031` today |
| Debug RAM peek | not I/O; unchanged | the peek reads island memory [V hdl/core/c64/multitini_c64_host.sv:39-56] |
| Any write to `$D0BC` (native or fast) | reaches the VIC, which ignores register `$3C` (`when others => null`); the turbo block has no `$0BC` write case, so no hint bit and no live change | [V hdl/core/c64/ports/video_vicII_656x.vhd:1799-1800, 1913-1915; turbo_regs.sv:55-61, 116-119] |

Real-SuperCPU difference [I]: VICE serves `$D0B0-$D0BF` from the SuperCPU even during DMA [V scpu64mem.c:866-871]. What an REU DMA read of `$D0BC` returns on real hardware is [O]. No program found does it. Our rule keeps the REU path and its DMA data unchanged (standing REU rule).

### 3.3 Registers and bits (plan allocations)

| Item | Value |
|---|---|
| `C64_CPU_CONFIG` @0x0D8 bit 20 | `SCPU_DETECT`. Reset 0. Mask 0x000FFFFF → **0x001FFFFF**. Reset value 0x151 unchanged. Not in `C64_CPU_CONFIG_DEBUG_MASK` (0xB0880): it is a user setting [V spec/interface_v0.json:802-808] |
| Clamp | `clamp_c64_cpu()`: `if (!C64_CPU_CAPS2[15]) result[20] = 0;` [V hdl/common/multitini_control_regs.sv:245-265] |
| `C64_CPU_CAPS2` @0x0E4 bit 15 | `SCPU_DETECT`. `C64_CPU_CAPS2_RELEASE` 0x80002F01 → **0x8000AF01** [V spec/interface_v0.json:812]. The top already builds CAPS2 from `RELEASE[31:8]` [V hdl/board/multitini_one_top.sv:752-753] |
| `C64_CPU_STATUS` | no change; bit 27 stays reserved [V turbo_regs.sv:84] |
| Crossing | none new. Bit 20 rides in the 32-bit `c64_cpu_config` field of the `controls_i` word mailbox, `WIDTH(120)` at dev7, whose `RESET_VALUE` carries `MT_C64_CPU_CONFIG_DEFAULT` [V hdl/board/multitini_one_top.sv:1244-1251]. This design adds no bit to `controls_i` |
| ABI | 0.26 (the plan, "ABI and registers"). 0.25 shipped with dev7 [V build/C64-4D00C707-070dev7-SD/README.txt], so the bit cannot fold into it. New constant `C64_SCPU_DETECT_MIN_ABI_MINOR` = 26 |
| Apply event | **No.** The apply counter follows bits 6:0 only [V c64_features.c:108-109; c64_debug.c:69-70]. The firmware writes the word with `apply = false` (section 4.3) |

### 3.4 Setting

| Item | Value |
|---|---|
| Storage | `settings.bin` byte 26 bit 3 (the plan, "Settings storage"), held in memory as bit 3 of `mt_supervisor_settings_t.c64_turbo_flags` [V firmware/platform/xilinx/supervisor_settings.h:64] |
| Scope | global, like the rest of the turbo detail (Q5 [D TD:159, 1985]) and like the U64 store |
| Default | 0 = Disabled |
| Downgrade | dev6/dev7 firmware ignores bit 3 and writes 0 on its next save [V firmware/platform/xilinx/supervisor_settings.c:588-589, 710]. Result: Disabled, the safe direction |

Why bit 3 of `c64_turbo_flags` and not a separate bool: `mt_c64_turbo_config()` already receives `flags` [V c64_features.c:89-90], and both callers pass `persistent_settings.c64_turbo_flags` today [V main.c `configure_c64_turbo()` (~2384), `change_c64_turbo()` (~4253)]. After XM1 they pass the effective flags (the plan, "Stored, effective and the title overlay", rule 1). No signature change. Bits 0-1 are the only users today [V firmware/common/core_frontend.c:172, 177; c64_features.c:102-104; main.c `change_c64_turbo()` cases 6, 7].

### 3.5 Menu row

| Item | Value |
|---|---|
| Argument | 14 (the plan, "Menu") |
| Position | turbo group, after "Badline timing" (arg 5), before "Disk auto-slow" (arg 6): U64 order. The release rows before XM2's title rows become `{0,1,9,4,5,14,6,2,3,8}`, 10 rows; with argument 17 (and 18-19 while active, the plan, "Menu") 11-13 rows |
| Shown when | `state->abi_minor >= MT_C64_SCPU_DETECT_MIN_ABI_MINOR` **and** `state->c64_cpu_caps2` bit 15. The probe already clears bit 15 below ABI 26 (section 4.3); the frontend checks the ABI too, as the expansion rows do [V core_frontend.c:68], so a state built with ABI 24 and the RELEASE caps shows no row |
| Label | `SuperCPU Detect (D0BC)` (the U64 label; 22 of 35 label columns [V firmware/common/menu.c:14]) |
| Values | exactly four: `Disabled`; `Enabled`; `Enabled (inactive: Turbo Off)`; `Enabled (not applied)`. Each gains the suffix ` (title)` while the title overlay owns the key. Longest: `Enabled (inactive: Turbo Off) (title)`, 37 of 57 value columns [I from menu.c:10-16 with `MT_UI_COLUMNS` = 128]. The spelling `(inactive: Turbo Off)` is the plan's (rule B7) |
| Colour | blue; muted while inactive |
| Help (ASCII, 170 chars; two wrapped lines of 94 [V menu.c:908-928]) | `Programs see a SuperCPU at $D0BC, but there is no 65816 or SuperRAM: GEOS/GDOS64 SuperCPU patches crash and Ozmoo skips the REU. Leave Disabled unless a program needs it.` |
| Help while the overlay owns the key | `Set by GAME.cfg for this title. GEOS/GDOS64 SuperCPU patches crash: Revert title settings or set Disabled first.` 112 chars with an 8-char name, 167 with the 63-char source-name limit of the overlay (title-config design, "New portable code"). Both fit two wrapped lines and the 192-byte per-row help buffer [I, wrap rule of menu.c:908-928] |
| Control | Enter toggles. Never blocked (plan rule B7). Editable while Turbo Off, like the speed rows (Q9(e) [D TD:1925]). While the title overlay owns the key, Enter edits the overlay, not `stored` (plan, "Stored, effective and the title overlay", rule 7; section 4.3) |
| Pause rule | applies while paused; no restart [V main.c `menu_action_needs_core_progress()` (~3695-3725): only C64 argument 0 needs progress] |
| Hotkey | none (the user's hotkey list is Turbo on/off, Faster, Slower) |

The row value comes from three inputs:
- `eff`, the **effective** Detect flag: `overlay owns SCPU ? overlay value : stored c64_turbo_flags bit 3` (plan rules 1 and 4). Today `supervisor_menu_view()` copies the stored flags [V main.c:2602 `state.c64_turbo_flags = persistent_settings.c64_turbo_flags`]. XM1 fills `state.c64_turbo_flags` from the effective flags (title-config design, "What XM2 needs from XM1"). Ownership comes from `state->c64_title_owned` (title-config design, "Menu and front end").
- `turbo_on`, the effective Turbo Control: `state->cpu_turbo_enabled`, the source of the "Turbo on/off" row [V core_frontend.c:140, 157].
- The `C64_CPU_CONFIG` readback that `supervisor_menu_view()` already takes [V main.c:2604 `state.c64_cpu_config = shell_read(MT_REG_C64_CPU_CONFIG)`].

States, checked in this order:
1. `eff` = 0 → `Disabled`.
2. `eff` = 1 and not `turbo_on` → `Enabled (inactive: Turbo Off)`.
3. `eff` = 1, `turbo_on`, readback bit 20 = 1 and readback mode ≠ 0 → `Enabled`.
4. `eff` = 1, `turbo_on`, otherwise → `Enabled (not applied)` (the clamp refused it, or the word is not written yet; it should not happen on an image that shows the row).
Then append ` (title)` if the overlay owns the key.

State 2 uses `turbo_on`, not the readback mode. Reason: under the debug `:speed` override the readback mode is 1 while Turbo Control is Off, and bit 20 is then clear (section 3.1), so a readback-mode rule would show `Enabled (not applied)` for a state that is simply inactive [I]. Examples: stored Disabled + `GAME.cfg` Enabled → `Enabled (title)`, `$D0BC` = `$01`. Stored Enabled + `GAME.cfg` Disabled → `Disabled (title)`.

### 3.6 Status texts

The status buffer holds 63 characters; longer text is cut [V main.c:190 `supervisor_status_text[64]`, `set_supervisor_status()` ~1976-1987]. Every text below fits.

| Event | Status line | Length |
|---|---|---|
| Toggle to Enabled, turbo on | `SuperCPU Detect on: $D0BC reads $01; GEOS patches crash` (warning colour) | 55 |
| Toggle to Enabled, turbo off | `SuperCPU Detect saved; inactive while turbo is off` | 50 |
| Toggle to Disabled | `SuperCPU Detect off` | 19 |
| Toggle while the title overlay owns the key (no save) | `SuperCPU Detect on for this title (GAME.cfg)` / `SuperCPU Detect off for this title (GAME.cfg)`: the "for this title" form of the title-config design ("Editing while a title overrides a setting"). The name in brackets is left out when the whole text would exceed 63 characters | ≤ 63 |
| A title `.cfg` launch turns Detect on | `Title .cfg turned SuperCPU Detect on; Revert before GEOS` (warning colour) | 56 |
| UCI `$50` LOAD_CONFIG turns Detect on | `UCI config turned SuperCPU Detect on; Revert before GEOS` (warning colour) | 56 |
| Options → Reset keeps a D64-source overlay that has Detect on | `Core restarted; SuperCPU Detect stays on (title .cfg)` (warning colour). Replaces `Core restarted; Game.cfg stays active` in this case | 53 |
| Save failure | `SuperCPU Detect setting not saved` (warning; nothing written) | 33 |
| Image without the cap | `Core option unavailable` (existing text) | 23 |

**Precedence on the launch status line** (owned by the title-config design, "Menu and status"): error > SuperCPU warning > skipped count > Command Interface note > clean. An error text means the file did not apply (refused file, commit error, a failed all-or-nothing `$50`), so a file that did turn Detect on always shows the SuperCPU warning. The applied and skipped counts stay visible in the "Title .cfg files" row (`On: Driller.cfg, 2 skipped`) and in `:cfg`. The file name is in the "Revert title settings" row, the `:cfg` output and the UART report lines. `mt_c64_cfg_summary()` (title-config design, "New portable code") returns the warning text when the resolved candidate sets SCPU to Enabled.

UART `:turbostat` (debug session only, display only) gains `scpu=0|1`, decoded as `CONFIG[20] && CONFIG[10] && CONFIG[5:4] != 0`, the HDL's `mode` term [V turbo_regs.sv:42; c64_debug.c:130-143]. The HDL also accepts the legacy CONTROL bit 5 request when CONFIG[10] = 0; `:turbostat` cannot see that request. Firmware always sets CONFIG[10] (`MT_C64_CPU_CONFIG_AUTHORITATIVE`) [V c64_features.c:94], so the display matches the HDL for every word firmware writes [I].

No STATUS hint bit for "software read `$D0BC`" (rejected: GDOS64, Ozmoo and U4R all probe it, so the hint would push users toward the option that crashes GEOS [I]).

### 3.7 `.cfg` and UCI interaction

**Registry row** (the plan, ".cfg dialect and option registry"):

| Field | Value |
|---|---|
| id, argument | `SCPU_DETECT`, 14 |
| storage, scope | byte 26 bit 3 (`c64_turbo_flags` bit 3), global |
| caps gate | ABI ≥ 26 and `C64_CPU_CAPS2` bit 15 |
| section | `[U64 Specific Settings]`, alias `[C64U Specific Settings]` |
| item | `SuperCPU Detect (D0BC)`; alias `SuperCPU Detect ($D0BC)`; case-insensitive, so the C64U label `SuperCPU detect (D0BC)` matches (U8) |
| values | `Disabled`, `Enabled` |
| overlay group | `SCPU` (title-config design, key K4) |
| apply class | turbo word write **without** an apply event |
| overlay allowed | yes |

Rules:
- A title file with `Enabled` and `Turbo Control=Off` stores Enabled in the overlay; the row shows `Enabled (inactive: Turbo Off) (title)`, as in the menu.
- An image without the cap: the key is skipped and reported, for example `GAME.cfg: SuperCPU Detect ignored (not supported)` (plan rule B8). `Disabled` on such an image is CHECKED, not reported (title-config design, "From parse to candidate").
- A U64 full dump carries every item of the store [V configio.cc:209-219], so it holds `SuperCPU Detect (D0BC)=Disabled` unless the writer had Detect on; slesinger/doom's `release/doom.cfg` (v1.0.0) is such a file [R]. That forces Disabled for that title only. Harmless.
- **U64 files that pair `Enabled` with Registers/TurboEnable, or with 1 MHz, act differently here.** On a U64 such a file has no effect, because #654 hides the register in Turbo Registers mode and at 1 MHz [R #654]. Here, after the #654 fix (D20), it turns `$D0BC` on. The U64 keeps Detect in the global store [V u64_config.cc:394], and "Save to File" writes every item of the store [V configio.cc:209-219]. So a U64 user who left Detect globally Enabled writes `SuperCPU Detect (D0BC)=Enabled` into every title file, including Registers-mode files of the doom.cfg shape. Rule: whenever a `.cfg` or UCI `$50` turns Detect on, the status line says so (section 3.6), and the docs say the same. Alternative: Q-SC14 (**USER**).
- UCI Control `$50` LOAD_CONFIG uses the same parser and overlay. It **merges** into an active overlay, it does not replace it (title-config design, "UCI Control $50 LOAD_CONFIG"). A Detect change from `$50` is a turbo-word write with no apply event. It changes no expansion state, so it needs no UCI CPU hold (UCI design) and cannot move the live speed.

**When the overlay ends.** This table copies the end-event list of the title-config design ("When the overlay ends"). Detect follows the overlay: while the overlay owns SCPU, `$D0BC` follows the overlay value.

| Event | Overlay | Note |
|---|---|---|
| Next trigger launch | replaced by the new candidate, or cleared if none | a Mount & Run of the GEOS disk is a new launch and ends a Detect overlay |
| Launch refused or failed before the reset | unchanged | nothing changed on the machine |
| Launch failed after the commit | cleared, stored values written | the machine holds no title |
| Options → Reset | PRG source: cleared. D64 source: **kept** while drive 8 holds a disk (the source or a later swap), cleared when drive 8 is empty. CRT source: cleared, then re-applied by the CRT restore relaunch | title-config decision U6. Status per section 3.6 when a kept D64 overlay has Detect on |
| "Fast serial ROMs" toggle (argument 0, restarts the C64) | CRT source: kept. D64 source: as Options → Reset. PRG source: cleared | |
| Reset by C64 software (`JMP $FCE2` and the like), UCI Control REBOOT | unchanged | not a firmware overlay event; the UCI design defines REBOOT as a C64 reset that keeps overlay, title session and registers |
| Cartridge detach | cleared | |
| Drive 8 eject or mount (disk swap) | **unchanged** | keeps multi-disk titles working |
| Core switch, `supervisor_activate()`, variant change | cleared | |
| Standby, power-off | cleared | RAM only |
| USB SD export | cleared | |
| "Revert title settings" (argument 18), `:cfg revert` | cleared, stored values written | the row is shown whenever an overlay is active |
| "Title .cfg files" → Off | cleared, stored values written | |
| UCI `$50` LOAD_CONFIG | merged | |
| Load into REU, `:speed`, debug commands | unchanged | the debug layer sits above the overlay |

**Consequence and advice.** A GEOS or GDOS64 disk mounted on drive 8 and booted with LOAD after a title whose file set Detect still reads `$01` and crashes [I]. For a D64 title this also holds after Options → Reset, while drive 8 holds a disk. "Revert title settings" always ends the overlay. So the owned-row help, the launch status line and the docs all advise: **before booting GEOS or GDOS64 after such a title, use Revert title settings (or set the row to Disabled), then reset.** Q-SC17 records the alternative (drop SCPU from a kept D64 overlay at Options → Reset) and why it is not recommended. Per-title use still lowers the R1 risk compared with a global Enabled.

---

## 4. Implementation plan by file

### 4.1 Spec and generated files (part of the single 0.26 spec edit of the plan)

| File | Change |
|---|---|
| `spec/interface_v0.json` | Register `C64_CPU_CONFIG` description (~line 282): add "SuperCPU Detect[20]". `C64_CPU_CAPS2` description (~line 303): add "SuperCPU Detect[15]". Bitfields `C64_CPU_CONFIG` (~612-621): `"SCPU_DETECT": 20`. Bitfields `C64_CPU_CAPS2` (~636-645): `"SCPU_DETECT": 15`. Constants (~795-812): `C64_SCPU_DETECT_MIN_ABI_MINOR: 26`, `C64_CPU_CONFIG_MASK: "0x001FFFFF"`, `C64_CPU_CAPS2_RELEASE: "0x8000AF01"` [V current values at :803, :812] |
| `hdl/generated/multitini_regs_pkg.sv`, `firmware/include/multitini_regs.h` | regenerated by `scripts/generate_registers.py`; checked by `make check-generated` [V Makefile:18-26] |
| `spec/core_interface_v0.md` | 0.26 section names the bit, the cap and "not an apply event" |
| `scripts/make_core_package.py` | `DEFAULT_ABI_MINOR = 25` → **26** [V scripts/make_core_package.py:19]. `tests/test_core_package_tool.py:34` asserts it equals the spec minor, so a spec at 26 with the default at 25 fails. A package built at 0.25 would also hide the feature, because the probe uses `min(package, live)` minor [V c64_features.c:19] |

### 4.2 RTL

**`hdl/core/c64/multitini_c64_turbo_regs.sv`** (the only RTL with logic changes; about 6 lines).

```systemverilog
localparam [7:0] SCPU_DETECT_VALUE = 8'h01;          // Q21; E7-C may revisit
wire scpu_detect_live = config_word[20] && mode != 2'b00;
// One shared compare per path: A9..A8 = 0 is common to $030, $031 and $0BC,
// so the $0BC term adds as few loads as possible to T65 A0-A9 (C3).
function automatic override_hit(input [9:0] a, input scpu);
    override_hit = a[9:8] == 2'b00 &&
                   (a[7:1] == 7'b0011000 ||           // $030, $031
                    (scpu && a[7:0] == 8'hbc));       // $0BC
endfunction
function automatic [7:0] read_value(input [9:0] address);
    case(address)
        10'h031: read_value=registers_live ? live_d031 : 8'hff;
        10'h030: read_value=c128_live ? {7'h7f,(live_d031!=0)} : 8'hff;
        10'h0bc: read_value=SCPU_DETECT_VALUE;
        default: read_value=8'hff;
    endcase
endfunction
assign native_rd_override=override_hit(native_rd_addr, scpu_detect_live);
assign fast_rd_override=override_hit(fast_rd_addr, scpu_detect_live);
```

- Today's lines: `mode` :42, `read_value` :63-69, overrides :70-71 [V]. The shared form is logically equal to `addr==10'h030 || addr==10'h031 || (scpu_detect_live && addr==10'h0bc)`; T-SC1 checks that over all 1024 addresses. Vivado restructures LUT logic anyway, so the routed report, not the RTL shape, is the evidence for C3 [I].
- The `$0BC` override is **gated**, unlike `$D030`/`$D031`, which are overridden in every mode [V :70-71]. With Detect off, the VIC drives the byte through the unchanged path. "Off" is then off by construction, not because the VIC's `$3C` happens to read `$FF`.
- No new port. Bit 20 arrives in `config_word`. The write `case` (:55-61), hints (:116-119), status (:84-88) and apply logic (:108-115) are untouched.

**`hdl/core/c64/multitini_c64_adapter.sv`**: **no change.** The muxes at :505-506 already route any VIC-page override on both paths [V]. Keep those two lines textually identical. The bench lanes depend on their exact text: `scripts/test_c64_turbo_exact.py:16-17` strips the override by string replacement, and `scripts/test_c64_turbo_ordering.py:23-24` puts it back by string replacement [V]. The ordering lane copies the turbo block instance from the adapter text [V test_c64_turbo_ordering.py:16], so the new `$0BC` logic reaches the lane benches with no script change.

**`hdl/common/multitini_control_regs.sv`**: in `clamp_c64_cpu()` add `if (!C64_CPU_CAPS2[15]) result[20] = 0;` after the Follow CPU line (:263) [V :245-265]. The mask comes from the regenerated `MT_C64_CPU_CONFIG_MASK`.

**`hdl/board/multitini_one_top.sv`**: no change. CAPS2 bit 15 comes from the regenerated `MT_C64_CPU_CAPS2_RELEASE` [V :752-753].

**Timing and fit (review item C3).**

Baseline, post-route, fast-island clock group `clk_out2_zynq_ps_bd_clk_wiz_c64_0`:

| Image | Fast-island setup slack | Worst path | Slices | LUT | Source |
|---|---|---|---|---|---|
| dev6 PAL `4D00C706` | +0.143 ns | island BRAM → T65 | 12,340 (92.78 %) | 36,187 | [V build/Multitini-070dev6/evidence/c64-pal/reports/timing_summary.rpt:166, 66002-66004; evidence/c64-pal/reports/utilization.rpt:25; build/c706p3/build/c64-pal/multitini_one.runs/impl_1/multitini_one_top_utilization_placed.rpt:76] |
| dev7 PAL `4D00C707` | **+0.030 ns**, accepted "for this build only" | `main_ram_i/character_reg` (RAMB36, falling edge) → `cpu_i/cpu/BAL_reg[8]` (rising edge), requirement 7.929 ns of a 15.859 ns period, 8 logic levels | 12,519 (94.13 %) | 36,689 | [V build/Multitini-070dev7/evidence/c64-pal/reports/timing_summary.rpt:166, 65735-65752; build/c707p3/build/c64-pal/multitini_one.runs/impl_1/multitini_one_top_utilization_placed.rpt:76; evidence/c64-pal/reports/utilization.rpt:25; build/C64-4D00C707-070dev7-SD/README.txt] |
| dev6 NTSC `4D00C706` | +0.184 ns | — | — | — | [V build/Multitini-070dev6/evidence/c64-ntsc/reports/timing_summary.rpt:65959-65961] |
| dev7 NTSC `4D00C707` | +0.219 ns (period 15.276 ns) | — | 12,362 (92.95 %) | 36,769 | [V build/Multitini-070dev7/evidence/c64-ntsc/reports/timing_summary.rpt:166, 65587-65589; evidence/c64-ntsc/reports/utilization.rpt:25; build/c707n2/build/c64-ntsc/multitini_one.runs/impl_1/multitini_one_top_utilization_placed.rpt:76] |

- The plan's ceilings are fast-island WNS not below +0.100 ns and slices ≤ 95 %. The build script enforces 0.100 ns [V scripts/build_and_export_xsa.tcl:207]. dev7 PAL fails the slack ceiling before any XM3 RTL; its slice headroom to 95 % is 116 slices (12,635 − 12,519). dev7 NTSC meets both (headroom 273 slices).
- Fit: about 10-20 LUT, 0 FF, 0 BRAM [I]. The slice count can move by more than that from placement alone [I].
- Logic depth: the compare and AND sit on the fast read D path into `io2_q`: T65 address → `fast_rd_override` → `island_fio_data` → `io2_q`. `io2_q` is a rising-edge register [V cpu_island.sv:202, 209], so this is one full `c64_cpu_clk` period.
- **Fanout: the change adds loads to T65 A0-A9**, because `fast_rd_addr` is `fio_rd_addr = cpu_address[9:0]` [V cpu_island.sv:165]. The same nets drive the falling-edge island BRAM addresses [V hdl/core/c64/multitini_c64_island_memory.sv:60-77, `always @(negedge clk_cpu)` on `cpu_addr`], a half-period path. TD already records that the FIO read-mux loads on A0/A1 drive the critical half-period path [D TD:144, 1382]. The dev7 PAL limiter is the other half of the same loop (BRAM → T65). So the change touches the critical path through fanout, not logic depth [I].
- RTL mitigation: the shared compare above.
- Measurement: in the routed PAL and NTSC builds, record the load count and net delay of A0-A9 (A0/A1 first) and the worst half-period slack, before (the baseline XM3 starts from) and after. The multi-SID design adds a slot-table decode on A5-A9 of the same nets; it uses this record as its baseline.
- If fast-island WNS falls below the ceiling: use the TD R2 ladder, starting with F1 (Pblock plus address replication / `MAX_FANOUT`) [D TD:2168]. Registering `scpu_detect_live` is **not** a fix for this path: it does not reduce address fanout.
- **Baseline decision (Q-SC18).** XM3 is the first HDL milestone after dev7. The plan decides between a timing-recovery milestone (XM0) that restores PAL fast-island slack to at least +0.100 ns before XM3, and a per-milestone waiver policy (**USER**). This design recommends XM0: XM3 adds loads to the limiting nets, and a delta measured against +0.030 ns cannot be told apart from placement noise.
- `scpu_detect_live` comes from `c64_clk` registers into a `c64_cpu_clk` register: a timed related-clock path, as `registers_live` already is [V cpu_island.sv:2]. It is quasi-static and not the limiter [I].

### 4.3 Firmware

| File | Function / symbol | Change |
|---|---|---|
| `firmware/include/multitini_c64_features.h` | new defines | `MT_C64_TURBO_FLAG_DISK_AUTOSLOW_OFF 0x01U`, `MT_C64_TURBO_FLAG_CARTRIDGE_NATIVE 0x02U`, `MT_C64_TURBO_FLAG_SCPU_DETECT 0x08U` (bit 2 is the UA flag on the wire and is never in `c64_turbo_flags`) |
| same | `mt_c64_capabilities_t` [V :8-11] | add `bool scpu_detect` |
| `firmware/common/c64_features.c` | `mt_c64_probe_capabilities()` [V :7-41] | inside the turbo block: `if (minor < MT_C64_SCPU_DETECT_MIN_ABI_MINOR) c.cpu2 &= ~MT_C64_CPU_CAPS2_SCPU_DETECT; c.scpu_detect = c.turbo && (c.cpu2 & MT_C64_CPU_CAPS2_SCPU_DETECT);` |
| same | `mt_c64_turbo_config()` [V :89-111] | after the Follow CPU line (:105-106): `if ((flags & MT_C64_TURBO_FLAG_SCPU_DETECT) && (caps->cpu2 & MT_C64_CPU_CAPS2_SCPU_DETECT)) config \|= MT_C64_CPU_CONFIG_SCPU_DETECT;` The bit is set whatever the mode; the HDL applies the Turbo Control gate (one place of truth, so the title overlay follows automatically). The one firmware exception is the debug override rule of section 3.1, because the override changes the mode itself. The counter line (:108-109) is unchanged and ignores bit 20 |
| `firmware/common/c64_debug.c` | `turbo_status()` [V :130-143] | append `scpu=%u`, decoded per section 3.6 |
| same | `mt_c64_debug_effective_overlay()` [V :60-72] | inside `if (s->override_active && s->session)`: if `(config & MT_C64_CPU_CONFIG_MODE_MASK) == 0`, clear `MT_C64_CPU_CONFIG_SCPU_DETECT` **before** forcing `0x10 \| index` (section 3.1). Counter logic unchanged; bit 20 is outside `0x7F`, so this never raises an apply event |
| `firmware/platform/xilinx/supervisor_settings.h` | `c64_turbo_flags` comment [V :64] | "Byte 26 bits 0, 1 and 3" |
| `firmware/platform/xilinx/supervisor_settings.c` | encode [V :588-589], decode [V :710] | `(c64_turbo_flags & 0x0BU) \| (UA ? 4U : 0U)`; decode `& 0x0BU`. Name the mask (`MT_SETTINGS_C64_TURBO_FLAGS_MASK`), so the UCI and title-config designs can extend the same byte |
| `firmware/common/core_frontend.c` | `build_c64()` [V :64-213; turbo block :118-183] | new row after the Badline row (:167-170), inside `if (state->c64_turbo_available)`, shown when `state->abi_minor >= MT_C64_SCPU_DETECT_MIN_ABI_MINOR && (state->c64_cpu_caps2 & MT_C64_CPU_CAPS2_SCPU_DETECT)`; argument 14; value per the four states of section 3.5 from the effective `state->c64_turbo_flags`, `state->cpu_turbo_enabled`, `state->c64_cpu_config` and `state->c64_title_owned` (` (title)` suffix and the owned help, written into the per-row buffers of the title-config design). Uses the plan's bounds-checked append helper (XM1) |
| `firmware/platform/xilinx/main.c` | `supervisor_menu_view()` [V :2568; flags :2602] | `state.c64_turbo_flags` from the **effective** flags (XM1) |
| same | new `static void change_c64_scpu_detect(void)` | see the steps below |
| same | `handle_menu_action()`, `MT_MENU_ACTION_TOGGLE_CORE_OPTION` C64 branch [V ~4947-4958] | new branch `file_index == 14U → change_c64_scpu_detect()` before the `>= 2U` expansion catch-all; today 14 would fall into `change_c64_expansion()` and warn |
| same | `configure_c64_turbo()` [V :2376-2389] | no change here; it passes `persistent_settings.c64_turbo_flags` today, and XM1 changes it to pass the effective flags |
| same | `menu_action_needs_core_progress()` | no change; argument 14 applies while paused |
| same | Options → Reset status | when a kept D64 overlay owns SCPU with Detect on, the status of section 3.6 (a requirement on the title-config design's `MT_MENU_ACTION_RESET_CORE` handling) |
| `firmware/common/c64_option_registry.c` (XM1) | registry table | one row per section 3.7 |

`change_c64_scpu_detect()` (plan rules 1, 2, 3 and 7; title-config design, "Editing while a title overrides a setting"):
1. Caps check: `caps.scpu_detect`, else `Core option unavailable`.
2. If `mt_c64_overlay_owns(&c64_title_overlay, SCPU)` (group `SCPU`, registry id `SCPU_DETECT`): `mt_c64_overlay_edit(&c64_title_overlay, 14U)`, which toggles the overlay value only. No `settings_before_update`, no `save_persistent_settings()`. Status `SuperCPU Detect on|off for this title (GAME.cfg)`.
3. Otherwise: `settings_before_update = persistent_settings`; flip `MT_C64_TURBO_FLAG_SCPU_DETECT` in `persistent_settings.c64_turbo_flags`; `save_persistent_settings()`. On failure restore `persistent_settings`, warn, and write nothing. Status per section 3.6.
4. In both cases: `configure_c64_turbo(&supervisor.active, profile, false)`, which after XM1 builds the word from the **effective** flags; `supervisor_menu_dirty = true`.
5. It does **not** call `mt_c64_debug_clear_override()`: it is not a speed row, and the live speed must not move.

If the user chooses the plan's rule 7 alternative ("menu edits stored and drops the key from the overlay"), step 2 becomes: drop `SCPU` from the overlay, then step 3. Either way the global saved value never changes as a side effect of a title edit.

Why `apply = false` is enough: `mt_c64_debug_effective_overlay(…, false)` keeps the previous counter [V c64_debug.c:69-71], and the turbo block applies only on a counter change [V turbo_regs.sv:46, 108]. A Detect toggle can therefore never set live := preference, even in Registers mode.

### 4.4 Docs (when XM3 lands)

Stale statements this feature owns (TD lines at `3df6d04`):
- TD:13, 76, 172 (Q11 partly reversed → Q21), 211-221 (add a `$D0BC` column to the liveness table; line 221 says no SuperCPU register is decoded), 258, 264, 1769 (CONFIG [31:20] reserved), 1773-1774 (add SCPU Detect to the "never increments" list), 1835 (CAPS2 [30:15] reserved), 1920, 1969, 2411 (M5 gate "GNW64 / xahmol detection": add a Detect-on run), 2441, 2530, 2890, 2898.
- FVIC:446; SPEC:79, 180; ISS:256 (remove SCPU Detect from the apply set); PRIOR:349.
- ISS:36, 75, 292-304, 383-385 proposed `$D0B8`/`$D0B9` gated on Detect, reading `$C7`/`$07`. Q-SC4 replaces that: (a) ungated in the register modes, reading `$C1`/`$01`; or (b) a documented limitation.
- TD:277 ("Index 0 with badline timing enabled is the stock exact path … bit-identical in timing to Off") and TD:2284 (Manual index 0 and Registers live `$00` must be bit-identical to Off) [V]: add "(with SuperCPU Detect Disabled; with it Enabled, only CPU reads of `$D0BC` differ)", and state that the G-LOCK index-0 lockstep runs with CONFIG bit 20 = 0.

New TD records: Q21 (this design), deviations D20 and D21 (section 5), board rows and E7-C in TD §10.

User docs and the C64 help page say:
- Detect makes GEOS/GDOS64 SuperCPU patches crash and makes Ozmoo skip the REU.
- A title file that turns Detect on keeps it on across a drive-8 disk swap, a C64 software reset and a UCI REBOOT, and, for a D64 title, across Options → Reset while drive 8 holds a disk.
- Before booting GEOS or GDOS64 after such a title: use **Revert title settings** (or set the row to Disabled), then reset.
- U64 Registers-mode files with `SuperCPU Detect (D0BC)=Enabled` turn the register on here (section 3.7).

Q-SC4 outcome: (a) adds the `$D0B8`/`$D0B9` rule to TD §1.1 and the "U64 registers" help; (b) records the ScrSaver64/SuperMouse64 1 MHz limitation there.

---

## 5. Fix-instead-of-copy list

| # | U64 behaviour or issue | Matters? | Our behaviour | Tag |
|---|---|---|---|---|
| F1 | #654: Detect works only in Manual above 1 MHz; invisible in Registers mode | Yes: the option does not do what it says, and it fails in the mode U64-aware software recommends | Visible in Manual, Registers and TurboEnable at all 16 indices and through every forced-exact cause. Deviation **D20** | [R #654; I] |
| F2 | A Detect toggle strobes `SPEED_UPDATE`; in Registers mode that sets live := preference (the #787 class of side effect) | Yes: toggling a detect register must not change the running speed | Not an apply event; written with `apply = false`. Deviation **D21** | [V u64_config.cc:926, 1636; D TD:1773-1774] |
| F3 | #654 asks for visibility with Turbo Control Off | No: GideonZ requires Off to mean off, and our Off is exact stock | Not done. Off reads the VIC | [R #654] |
| F4 | The row is greyed while Turbo Control is Off | Minor usability | Row stays editable and shows "inactive" (Q9(e) precedent). A `.cfg` can set it while Off on the U64 too | [V u64_config.cc:1603-1622; configio.cc:399-401 sets items without an enabled check] |
| F5 | The U64 publishes no value | Yes for software authors | `$01`, documented and bench-tested | [V section 3.1] |
| F6 | The U64 does not warn that GEOS/GDOS64 crash with Detect on | Yes: the most serious hazard found | Help text and status lines name it. Docs point users to per-title `.cfg` use and to **Revert title settings** before booting GEOS after such a title, because the overlay survives a disk swap, a C64 software reset and, for a D64 title, Options → Reset (section 3.7) | [V section 2.4; title-config design, "When the overlay ends"] |
| F7 | "SuperCPU" naming confuses users (Lemon64 t=87516) | Yes, partly | Keep the U64 label (users and `.cfg` files know it); the help says what is missing | [R U15] |
| F8 | #666 / GEOS wobble: `$D0B8` missing | **Yes, now, in the register modes.** Two current consumers do not test `$D0BC` and need no mouse: MegaPatch ScrSaver64 and the SuperMouse64 poll. With `$D0B8` = `$FF` both "restore" through `$D07A`, which sets live `$D031` to 0 in Registers/TurboEnable mode, so GEOS stays at 1 MHz with Detect off [V section 2.4; turbo_regs.sv:58]. Image BBS needs only the writes | **USER** decision now (Q-SC4): (a) read-only `$D0B8`/`$D0B9` in the register modes, not gated on Detect, or (b) a documented limitation | [R #666; V area6510 mp-d3-system.d81 @11251, @12929, @28843] |
| F9 | #654: `$D0B5` speed switch | No program found reads it | Not added | [V section 2.4] |
| F10 | Manual + Detect advertises a SuperCPU whose `$D07A`/`$D07B` switch is dead | Low: Manual exists to force a speed; disk auto-slow (default On) covers serial I/O | U64 parity, documented in the help and docs | [V u64_config.cc:326, 1631; D TD:211-216] |

---

## 6. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | A user enables Detect and boots GDOS64 or a GEOS disk with the SuperCPU64-Patch: crash. This includes a GEOS disk mounted and LOADed after a title whose `.cfg` set Detect (the overlay survives a disk swap, a C64 software reset and, for a D64 title, Options → Reset) | Medium / high for that user | Default Disabled; help and status warnings; docs recommend per-title `.cfg` use and Revert title settings before GEOS after such a title; B7 shows Detect Disabled leaves these boots as they were; B10(d) records the swap case for D64 and PRG sources |
| R2 | Other programs take SuperCPU paths that need `$D200` RAM or 65816 code (MegaPatch `o.Patch_SCPU`, SMB64, Metal Dust) | Unknown / crash | Same as R1; the MegaPatch idiom stays [O] (Q-SC10) |
| R3 | Ozmoo stops using the REU with Detect on | Certain / slower play only | Help text names it |
| R4 | The extra compare adds loads to T65 A0-A9, which feed the half-period address → BRAM path. dev7 PAL is already at +0.030 ns against a +0.100 ns ceiling | Medium / timing | Timing decision before XM3 (Q-SC18, XM0 recommended); one shared compare with the `$030`/`$031` terms; record A0-A9 loads, net delays and worst half-period slack before and after; TD R2 ladder (F1 replication / `MAX_FANOUT` first) if below the ceiling (section 4.2) |
| R5 | E7-C shows the U64 returns another value, decodes mirrors, or answers DMA reads | Medium / low | Keep `$01` and the exact decode unless software needs otherwise (Q-SC1, Q-SC6) |
| R6 | U64 files that pair `SuperCPU Detect (D0BC)=Enabled` with Registers/TurboEnable or 1 MHz have no effect on a U64 (#654) but enable `$D0BC` here (D20). A U64 user who left Detect globally Enabled writes that line into every saved title file | Medium / high only if GEOS/GDOS64 is booted under that overlay | The status line always reports Detect on from a file (section 3.6); docs; Revert advice; Q-SC14 offers ignoring such files; B10(c) |
| R7 | Visible with a cartridge loaded: a cartridge program finds a SuperCPU and writes `$D07A`/`$D07B` | Low / none | A cartridge forces exact, so the CPU stays at 1 MHz [V turbo_regs.sv:82-83]; such writes only move `live_d031` in the register modes [V turbo_regs.sv:56-59, 120-121] |
| R8 | Downgrade to dev6/dev7 firmware clears the setting | Certain / safe | Falls back to the default |
| R9 | Stale Q11 text in TD/SPEC/FVIC/PRIOR/ISS confuses later work | Medium | Section 4.4 doc edits land with XM3 |
| R10 | Someone edits the adapter mux lines (adapter.sv:505-506). The exact lane's strip (`str.replace` [V scripts/test_c64_turbo_exact.py:16-17]) then misses and leaves `native_rd_override` undriven in a lane with no turbo block (the copied declarations include adapter.sv:330; the copied links stop before the turbo block instance [V test_c64_turbo_exact.py:12, 14]), so VIC reads go X and the lane fails loudly [I]. The ordering lane's restore (`str.replace` [V test_c64_turbo_ordering.py:23-24]) can miss **silently** if only the exact script is updated: the ordering lane then keeps the stripped mux and stops testing the production one | Low / high for test validity | This design leaves the lines alone. The T-SC2 runner asserts that both mux strings occur exactly once in the adapter and exactly once in the generated ordering lane, in the style of the ordering runner's existing anchor check [V test_c64_turbo_ordering.py:14] |
| R11 | Only with Q-SC4 (a): a program treats a non-`$FF` `$D0B8` as a SuperCPU without testing `$D0BC` | Low / unknown | None found among the programs of section 2.4 [I]; register modes only, so Off and Manual are unchanged; GDOS64 configure reads `$D0B8` only after its `$D0BC` test [V area6510 gd-d4-config.d81 @~333896] |

---

## 7. Tests and gates

### 7.1 Simulation benches (small tests first)

**T-SC1: turbo block unit** (`hdl/sim/tb_c64_island_regs.sv`, run by `scripts/test_c64_cpu_island_components.py --test regs` [V runner :15-25, 39-40]).
- Extend the exact read decode sweep (today `a == 48 || a == 49` [V tb :75-78]) to every combination of mode 0-3 × bit 20 ∈ {0,1}: `native_rd_override` and `fast_rd_override` are true exactly for `$030`, `$031`, and `$0BC` when `bit20 && mode != 0`; the data at `$0BC` is `$01` on both paths when overridden.
- Visibility during the reset window (before `end_window()`), with `cart_present`, `loader_active`, `pause_request`, guard blocking (`guard_active = 0`), at indices 0-15, with `live_d031 = 0` in mode 2 and after a `$D030` = 0 write in mode 3.
- Native and fast writes to `$0BC`: `live_d031`, `status[23:21]` and the `$0BC` value unchanged.
- Toggling bit 20 with an unchanged apply counter: no `applying`, `live_d031` unchanged, `status[31:28]` unchanged.
- Legacy path: `config_word[10] = 0`, `legacy_turbo_request = 1` → visible; request 0 → not visible.
- The shared compare (section 4.2) equals the plain three-term compare at all 1024 addresses on both paths.
- Only if Q-SC4 (a) is chosen: `$0B8`/`$0B9` overridden exactly in modes 2-3, reading `$C1` at live index 0 and `$01` otherwise, whatever bit 20 is; not overridden in modes 0-1.

**T-SC2: full machine, both CPU paths** (new `hdl/sim/tb_c64_scpu_detect.sv`, new `scripts/test_c64_scpu_detect.py`, reusing `test_c64_turbo_ordering.make_lane()`, which copies the production turbo block instance and the two override muxes into the lane and maps `cpu_config` to `test_config` [V scripts/test_c64_turbo_ordering.py:10-24]).
- An original 6502 program (source under `tests/c64_turbo/`, no binary committed) reads with I/O visible: `$D0BC`, `$D0BD`, `$D03C`, `$D07C`, `$D0B8`, `$D1BC`, `$D2BC`, `$D3BC`; then `$D0BC` with `$01 = $34` (RAM) and `$01 = $33` (CHAR); it stores each result.
- **Detection idioms.** The same program runs the known detection idioms, rewritten as original code (no third-party binary or source is copied), and stores each branch outcome:

  | Idiom (after) | Code | Outcome stored |
  |---|---|---|
  | GNW64 [V GNW64 main.asm:111-142] | `lda $d030 / and #$02 / bne` (C128 test), then `lda #$0b / sta $d031 / lda $d0bc / cmp #$01 / bne`, then `lda $d031 / cmp #$0b / bne` | GNW64's `TURBO` value 0/1/2 |
  | GDOS64 boot (section 2.4) | `lda $d0bc / cmp #$ff / beq` | SuperCPU path taken |
  | GEOS SuperCPU64-Patch, U4R, Wheels | `bit $d0bc / bmi` | SuperCPU path taken |
  | Ozmoo [V ozmoo.asm:1046-1048] | `lda $d0bc / and #$80 / beq` | SuperCPU path taken |
  | CCGMS [V ccgmsterm src/init.s:24-26] | `lda $d0bc / asl / bcs` | SuperCPU path taken |
  | cc65 `detect_scpu()` [V acc_detect_scpu.s:22-30] | `clc / .byte $e2,$01 / bcc`, then `lda $d0bc / asl / bcs` | result 0/1 |

  Expected: every `$D0BC` idiom takes its SuperCPU path exactly when bit 20 = 1 and mode ≠ 0. cc65 returns 0 in every configuration (`$E2` is a 2-byte NOP on T65 [V third_party/c64/upstream/rtl/t65/T65_MCode.vhd:571-583]). GNW64 `TURBO`: Off → 0; Manual → 0 with Detect off, 1 with Detect on; Registers and TurboEnable → 1 either way (through `$D031`, live in both modes [V turbo_regs.sv:44, 63-69]). The `$D031 = $0B` write moves the live index in the register modes; that is expected.
- Configurations: Off; Manual index 0 badline on (exact path); Manual index 15 (fast FIO path); Registers right after reset (live 0) and after `$D031 = $0F`; TurboEnable after `$D030 = 0`; each with bit 20 = 0 and 1; PAL and NTSC. Both standards run in simulation and both block; routed timing is a separate G-SCPU item (section 7.5).
- Expected: `$01` only at `$D0BC` with I/O visible, bit 20 = 1 and mode ≠ 0; every other result equals lane 0 (stock CPU), except `$D0B8` in the register modes if Q-SC4 (a) is chosen.
- With bit 20 = 0: lane 1 results and bus order equal lane 0 (stock equivalence).
- REU-style DMA: `dma_req` with `dma_addr = $D0BC`, bit 20 = 1 → `dma_result == $FF` [lane ports V scripts/test_c64_turbo_exact.py:37-58].
- The runner asserts that both adapter mux strings occur exactly once in the adapter and exactly once in the generated ordering lane (R10).
- The new files match the build-input capture globs `scripts/test_c64_*.py`, `hdl/sim/tb_c64_*.sv` and `tests/c64_turbo/**` [V scripts/capture_vectrex_build_inputs.py:41, 64, 94].
- **Licence list.** `scripts/check_repo.py` fails any tracked source file without a `licenses/files.csv` row [V scripts/check_repo.py:23, 151-152]. Add rows for `hdl/sim/tb_c64_scpu_detect.sv`, `scripts/test_c64_scpu_detect.py`, any `.py` probe generator under `tests/c64_turbo/`, and the probe program source: origin `Multitini One`, licence `GPL-3.0-only`, status as the sibling turbo rows (`source-reviewed`, then `sim-tested` once the bench passes) [V licenses/files.csv:689, 751, 754].

**T-SC3: control registers** (`hdl/sim/tb_control_regs.sv`, via `scripts/test_c64_expansion_infrastructure.py` [V :28, 56]).
- The existing all-ones write check derives its expectation from `MT_C64_CPU_CONFIG_MASK` with the RELEASE caps [V tb :86, 682-684]; with the new mask and RELEASE CAPS2, bit 20 reads 1 and bits 31:21 read 0.
- Add a bench parameter for CAPS2 (default: the RELEASE constant) and one instance with CAPS2 bit 15 clear: bit 20 reads 0. Reset value still 0x151.

**Existing suites, unchanged inputs** (the plan, "Regression gates every milestone keeps"):
- G-LOCK and G-TURBO run with `test_config` bit 20 = 0, so they are unchanged by construction; re-run all.
- G-STK: `tests/c64_ua/stock_bus_manifest.json` pins sources that exclude the turbo block, the adapter and the control registers [V manifest `source_sha256`], and the trace reads no `$D0BC` [V grep of tests/c64_ua/stock_bus.csv]. Re-run in compare mode, which checks only the trace [V scripts/test_c64_ua_stock_bus.py:129-137: the manifest is written only under `--capture`]. The trace and the manifest are never edited; the source hashes of the run go into the build receipt.
- G-SPEC: `tests/test_interface_spec.py`, `make check-generated` with the new constants, and `tests/test_core_package_tool.py` with `DEFAULT_ABI_MINOR = 26` [V tests/test_core_package_tool.py:34].
- Package check `verify_c64_release_settings()`: CAPS2_RELEASE bit 14 still clear and `CONFIG_DEFAULT & DEBUG_MASK == 0` [V scripts/package_cpu1_video_test.py:213-224].

### 7.2 Host and Python tests

| Test | New checks |
|---|---|
| `tests/host/test_c64_features.c` [V :36-54] | probe: package or live minor 25 with CAPS2 bit 15 → `scpu_detect` false and bit 15 cleared; 26 → true. `mt_c64_turbo_config()`: flags bit 3 + cap → bit 20; no cap → 0; `enabled = false` still sets bit 20 (the HDL gates); a bit-3 change with `apply_change = true` leaves the counter unchanged |
| `tests/host/test_supervisor_settings.c` `test_c64_turbo_storage()` [V :983-1001] | flags = `detail & 0x0B`. XM2 has already assigned byte 26 bit 5 (`c64_title_cfg_off`), so at XM3 the unassigned bits are 4, 6 and 7. Hold `c64_title_cfg_off` at 0 and assert `stored_file[26] == (detail & 0x0F)`. The future-bit mask (today `0xF8` [V :996]) becomes **`0xD0`**; `0xF0` would set the title flag on load. A separate bit-3 round trip through the frozen schema-10 fixture zeroes bit 3 and keeps the file. Sequence across milestones: XM2 asserts bits 0-2 and 5 with mask `0xD8`; XM3 `0xD0`; XM5 (bit 4) `0xC0` |
| `tests/host/test_core_frontend.c` [V :162-176] | The existing release check sets `abi_minor = 24` with `caps2 = MT_C64_CPU_CAPS2_RELEASE` and asserts 9 rows; RELEASE now carries bit 15. New cases (row lists before XM2's title rows 17-19, which the title-config tests cover): ABI 26 → 10 rows `{0,1,9,4,5,14,6,2,3,8}`; ABI 24 with the same caps → 9 rows `{0,1,9,4,5,6,2,3,8}` (the ABI gate of section 3.5); ABI 26 without bit 15 → 9 rows. The four value strings from the four states; an overlay-owned row shows the effective value with ` (title)` (stored 0 + overlay 1 → `Enabled (title)`; stored 1 + overlay 0 → `Disabled (title)`) and the owned help; help is ASCII and fits two wrapped 94-column lines with a 63-character source name; values fit 57 columns |
| `tests/test_c64_turbo_runtime.py` | The mock hard-codes package `abi_minor = 24` [V :62] and `MT_REG_ABI_VERSION` = 24 [V :30]; CAPS2 = the generated RELEASE constant [V :33]. The probe uses `min(package, live)`, so the new scenario sets **both** to 26 (make the mock's ABI a variable; old scenarios keep 24). Extract `change_c64_scpu_detect()`. Not owned: one save, one `C64_CPU_CONFIG` write with only bit 20 changed and the counter unchanged; the debug override survives; a save failure restores settings and writes nothing; Turbo Off gives the "inactive" status. Overlay owns SCPU: `saves == 0`, `persistent_settings.c64_turbo_flags` (byte 26 bit 3) unchanged, one `C64_CPU_CONFIG` write with only bit 20 changed and the counter unchanged, status "for this title". Every status text of section 3.6 is ASCII and at most 63 characters |
| `tests/host/test_c64_debug.c` [V :44] | override active, incoming mode 0, bit 20 set → output bit 20 = 0, mode 1; incoming mode 2 → bit 20 kept; counter unchanged in both |
| `tests/test_menu_runtime.py` | argument 14 applies while paused |
| Title-config tests (owned by the title-config design) | registry: the item aliases and value labels of section 3.7. Runtime end events (T-RT4) with an overlay that owns SCPU = Enabled: Revert writes `C64_CPU_CONFIG` bit 20 = stored; Options → Reset with a D64 source and drive 8 holding a disk keeps bit 20 = 1 and sets `Core restarted; SuperCPU Detect stays on (title .cfg)`; with a PRG source it writes bit 20 = stored; UCI REBOOT leaves bit 20 unchanged. Status texts (T-RT11): the launch warning and its precedence over the skipped count and the Command Interface note |

### 7.3 Board tests (C64 PAL; NTSC when its image ships)

All in a `:debug on` session with `:turbostat` logged, plus one sessionless control run [D TD:2077-2084]. Third-party programs stay under `build/` after a licence check [D TD:2037].

| # | Test | Pass |
|---|---|---|
| B1 | Our own probe program: `PEEK(53436)`, `PEEK(53437)`, `PEEK(53692)` (`$D1BC`), `PEEK(53436)` after `POKE 53297,0`, in Off / Manual 1 and 64 MHz / Registers / TurboEnable × Detect off/on | matches sections 3.1-3.2 |
| B2 (blocking) | GNW64 (README: SuperCPU and U64 support), kept under `build/` after a licence check | In Off, Manual, Registers and TurboEnable × Detect off/on, GNW64's detection result (its `TURBO` variable, read with the `:debug on` RAM peek; address from the program's own build listing [O]) equals the T-SC2 idiom expectation (section 7.1), and the game runs at the intended speed. If no licence allows local testing, record that; G-SCPU then falls back to the T-SC2 idiom check, a change to the plan's G-SCPU minimum that the plan records |
| B3 | A cc65 program calling `detect_scpu()` | returns 0 with Detect off and on |
| B4 | An Ozmoo story with the REU | Detect off: REU used; Detect on: no REU, still playable |
| B5 | Ultima IV Remastered, Registers, index 15, Detect on | loads and plays; I/O brackets drop to 1 MHz |
| B6 | CCGMS | prints "SuperCPU Enabled!" only with Detect on |
| B7 | GDOS64 and a GEOS disk with the SuperCPU64-Patch, Detect **Disabled**, in Off and in each turbo mode | In each mode, the boot outcome of both disks equals the outcome on the previous release image (dev7, or the last image before XM3) with the same settings, and B1 shows `$D0BC` = `$FF` in that mode. "Off is off" is a relative claim: GEOS at turbo can fail for reasons unrelated to `$D0BC` (custom fast loaders, auto-slow Off) [D ISS:90; V core_frontend.c:173]. With Detect Enabled: record the expected failure for the docs (information) |
| B8 | G-REU0 (Expand disks 1 and 2, full plan definition) with Detect at its default | identical to dev7. One extra run with Detect on, Manual index 0, as information (shows whether Expand probes `$D0BC` [O]) |
| B9 | Registers mode, software sets `$D031 = $05`, then toggle Detect in the menu | live `$D031` and the `:turbostat` apply counter unchanged |
| B10 | After XM2, stored Detect Disabled. (a) `GAME.cfg` with `SuperCPU Detect (D0BC)=Enabled`. (b) The same, then toggle the row off. (c) A Registers-mode U64 file (`Turbo Control=U64 Turbo Registers`, `SuperCPU Detect (D0BC)=Enabled`). (d) Run twice, once with a D64 source (Mount & Run `GAME.d64` beside `GAME.cfg`) and once with a PRG source (`GAME.prg` beside `GAME.cfg`), each with Detect Enabled: mount a GEOS disk with the SuperCPU64-Patch on drive 8 and LOAD it; then **Revert title settings**, reset, and boot it again. D64 run only, before Revert: Options → Reset, then B1 | (a) on for that title, row `Enabled (title)`, status line reports it; next launch without a file: off. (b) row `Disabled (title)`, `$D0BC` = `$FF`, `settings.bin` byte 26 bit 3 unchanged; relaunch without a file: `$D0BC` still `$FF`. (c) under the Q-SC14 default, `$D0BC` = `$01` and the status line says so (R6); under the alternative, `$D0BC` = `$FF` and the key is reported as ignored. (d) record the first boot (expected crash); after Revert the row shows the stored value (`Disabled`), B1 shows `$FF`, and the boot is normal. D64 run: after Options → Reset, B1 still shows `$01` and the status reads `Core restarted; SuperCPU Detect stays on (title .cfg)` |

### 7.4 E7-C: U64 measurement (open item, [O]; plan experiment E-X9)

On a U64 or C64U, record the firmware version and:
1. `PEEK(53436)` in each Turbo Control mode × {index 0, index > 0} × {right after reset, after `POKE 53371,0` (`$D07B`), after `POKE 53297,15` (`$D031`)}, with Detect on and off.
2. The same for `$D0BD`, `$D0BF`, `$D1BC`, `$D03C`, `$D0B8`.
3. A write to `$D0BC`, then a read.
4. An REU stash (C64 → REU) of `$D0BC`, then a fetch.
5. Whether a Detect toggle in Registers mode changes the live `$D031`.

Fold into TD's E7 row [D TD:2233]. Results change this design only through Q-SC1, Q-SC6 and Q-SC7.

### 7.5 Gate G-SCPU (the plan's feature gate, made concrete)

G-SCPU passes when all of these hold:
- T-SC1, T-SC2 (including the detection-idiom probe, PAL and NTSC in simulation) and T-SC3 pass.
- The host and Python tests of section 7.2 pass; G-STK, G-LOCK, G-TURBO, G-SET and G-SPEC still pass.
- **PAL (blocking):** routed timing met on every group, and the plan's ceilings for XM3 met: fast-island WNS not below +0.100 ns and slices ≤ 95 %, or the floor that a recorded **USER** waiver names (Q-SC18). On the dev7 baseline (+0.030 ns, 94.13 % slices) this item cannot pass without XM0 or a waiver. The receipt records the utilization delta, the A0-A9 loads and net delays, and the worst half-period slack before and after (section 4.2).
- **NTSC (Q14):** the NTSC image is built and its timing delta is recorded against the dev7 NTSC baseline (+0.219 ns, 92.95 % slices; section 4.2). NTSC must meet the ceilings to ship with XM3. If it misses, a miss caused by the `$D0BC` logic goes down the TD ladder first; if NTSC still misses at the end, Q14 holds NTSC and PAL may ship alone [D TD:15, 103, 175]. A held NTSC image does not fail G-SCPU.
- Board tests B1, B2, B3, B7, B8, B9 and B10(a), (b) and (d) pass; for B10(d) the pass is the row after Revert, B1 and the normal boot, while the first boot is recorded. B4-B6 and B10(c) are recorded but do not block.

B2 and B10(d) are blocking, as the plan's G-SCPU minimum requires (GNW64, and the `.cfg` cases with a D64 and a PRG source each ended with Revert title settings); the idiom probe in T-SC2 adds a simulated check of the B2 decision. Hardware support is claimed only after the board runs.

---

## 8. Open questions and decisions

### 8.1 Decisions

The plan's decision numbers (section "User decisions") are given in brackets; rows without one are design choices of this document.

| # | Question | Recommended default | Reason |
|---|---|---|---|
| Q-SC1 (plan P26) **USER** | Read value | **`$01`**; keep it even if E7-C differs, unless a program needs the U64 value | Passes every known test including GNW64; matches the real-hardware low bits; `$00` and `$07` fail GNW64 |
| Q-SC2 (plan P27) **USER** | Visible in Manual? | **Yes**, in Manual, Registers and TurboEnable | U64 firmware parity; Manual + Detect is the U64 combination users and `.cfg` files have |
| Q-SC3 (plan P27) **USER** | Visible during forced exact (cartridge, loader, reset window, disk guard, pause) and at index 0 | **Yes**: configuration-only gate | Fixes #654; probes run at start, often inside the reset window |
| Q-SC4 (plan P28) **USER, decide now** | `$D0B8`/`$D0B9` | **(a) recommended:** read-only `$D0B8`/`$D0B9` in the register modes only (Registers, TurboEnable), **not** gated on Detect, reading `$C1` when the live index is 0 and `$01` otherwise (bit 7 = bit 6 = 1 exactly at 1x; low bits `%001`, consistent with `$D0BC` = `$01` and S4; VICE's formula with `optim & 7` = 1 [V scpu64mem.c:665-669]). Off and Manual unchanged, so G-STK and the Turbo-Off Expand runs are unaffected; whether Expand reads `$D0B8` in the G-REU0 register-mode runs is [O]. Writes ignored. One more term in the shared compare (C3 applies), so (a) is taken only if XM0 leaves margin for it (plan P28). A third deviation number. **(b):** a known limitation in TD §1.1 and the "U64 registers" help | Current consumers need no mouse and no `$D0BC`: MegaPatch ScrSaver64 and the SuperMouse64 poll both "restore" via `$D07A` when `$D0B8` reads `$FF`, which leaves register-mode GEOS at 1 MHz [V section 2.4]. The old trigger "if 1351 mouse emulation lands" is moot. Nuance of (a): a `$D07B` restore sets live := preference, not the previous live index [V turbo_regs.sv:59] [I]. Gating on Detect would push users toward the GDOS64 crash. Needs no config bit. The U64 does not emulate `$D0B8` (#666 open), so (a) is a fix-instead-of-copy deviation; keep the E7-C `$D0B8` measurement |
| Q-SC5 (plan P29) **USER** | Row label | **"SuperCPU Detect (D0BC)"** (U64) rather than ISS's "SuperCPU speed registers" | That ISS name described `$D0B8` too; the U64 label matches `.cfg` files and user knowledge; the help says what is missing |
| Q-SC6 | `$D0BD-$D0BF` mirrors | **No** | No program found; a real SuperCPU mirrors, the U64 is [O]; add only if E7-C or a program shows a need |
| Q-SC7 | Writes to `$D0BC` | **Ignored** (VIC no-op); the value stays `$01` | U64 docs say read-only; real dosext writes need `hwenable`, which we do not model |
| Q-SC8 | Does a Detect toggle end the debug `:speed` override? | **No** | It is not a speed row; the live speed must not move |
| Q-SC9 | Status hint "software probed `$D0BC`" | **No** | GEOS, Ozmoo and U4R all probe; the hint would push users toward R1 |
| Q-SC14 (plan P30) **USER** | A `.cfg` (or UCI `$50`) with `SuperCPU Detect (D0BC)=Enabled` whose effective Turbo Control is Registers/TurboEnable, or whose speed is 1 MHz | **Honour it** and always report it in the status line (section 3.6). Alternative: ignore `Enabled` from such a file and report `GAME.cfg: SuperCPU Detect ignored (U64 would hide it)` | Honouring matches the file's text and D20 (the #654 fix). Ignoring matches what the file did on a U64, where such lines are inert, and avoids carrying a globally-left-on U64 setting into every title (R6) |
| Q-SC15 (plan P12) **USER** (plan, "Stored, effective and the title overlay", rule 7) | A menu toggle of an overlay-owned Detect | **Edit the overlay, no save** (section 4.3) | The alternative "edit stored and drop the key" is supported by the same handler (section 4.3) |
| Q-SC17 (title-config U23) **USER** | Options → Reset with a D64-source overlay that has Detect on | **Keep the title-config rule** (the overlay, Detect included, stays while drive 8 holds a disk), warn with `Core restarted; SuperCPU Detect stays on (title .cfg)`, and advise Revert. Alternative: the title-config design drops the SCPU group from a kept D64 overlay at Options → Reset | The usual crash recovery after Options → Reset is `LOAD"*",8,1` of the same title, which needs its own Detect value; dropping it would break that title. The alternative protects a GEOS disk swapped in before the reset, which Revert and the warning already cover |
| Q-SC18 (plan P2) **USER** (owned by the plan) | Timing baseline for XM3 | **XM0 first**: restore PAL fast-island slack to at least +0.100 ns before XM3. Alternative: a per-milestone waiver policy that names a floor | dev7 PAL is at +0.030 ns under a one-build waiver; XM3 loads the limiting nets; a waiver at that level leaves no room to tell the change from placement noise (section 4.2) |

### 8.2 Items other documents own

| # | Item | Owner | What this design needs |
|---|---|---|---|
| X1 | Decision and deviation numbers | the plan | Q21 for this design; D20 (F1) and D21 (F2) from the SuperCPU range the plan assigns; one more if Q-SC4 (a) |
| X2 | Constant name | the plan (0.26 spec edit) | `C64_SCPU_DETECT_MIN_ABI_MINOR`; dev7 uses `C64_IEC_MIN_ABI` without `_MINOR` [V spec/interface_v0.json:828]. Rename either one in the 0.26 edit |
| X3 | Crossing/timing row C3 | the plan, "New clocks, resets, crossings and timing rules" | C3 is not a logic-depth item only: it adds fanout to T65 A0-A9, which feed the half-period BRAM path (section 4.2). It sits beside the multi-SID fast-read decode row |
| X4 | `controls_i` | the plan | No change from this design; bit 20 is inside the existing 32-bit `c64_cpu_config` field [V top.sv:1244-1251] |
| X5 | Launch status precedence and the Options → Reset warning | the title-config design, "Menu and status" | error > SuperCPU warning > skipped count > Command Interface note > clean; the D64 Reset case of section 3.6; every text ≤ 63 characters (main.c:190) |
| X6 | Overlay helpers | the title-config design | `mt_c64_overlay_owns()`, `mt_c64_overlay_edit()` for argument 14, `mt_c64_cfg_summary()` returning the SuperCPU warning, per-row help buffers, group `SCPU` |
| X7 | REBOOT | the UCI design | REBOOT keeps the overlay and registers, so Detect is unchanged (section 3.7 table) |
| X8 | Shared address-net baseline | the multi-SID design | Uses the XM3 receipt's A0-A9 record (section 4.2) |
| X9 | B2 licence fallback | the plan's G-SCPU minimum | If GNW64 cannot be tested under a licence, the T-SC2 idiom check replaces it |

### 8.3 Open items [O]

| # | Item | Affects |
|---|---|---|
| O1 | MegaPatch 3.3 detection idiom; SMB64's SuperCPU path (Q-SC10) | R2 wording only |
| O2 | Real SuperCPU `$D0BC` after boot (`$01` vs `$00`/`$07`) and with REU DMA (Q-SC11) | Q-SC1 evidence only |
| O3 | E7-C on a U64 or C64U (section 7.4) | Q-SC1, Q-SC6, Q-SC7 |
| O4 | GNW64's `TURBO` address in its build listing (B2) | B2 procedure |
| O5 | Whether Expand probes `$D0BC` (B8 extra run) or `$D0B8` (Q-SC4 (a)) | information |
| O6 | Pinned versions of the third-party sources of section 2.4 | citation quality |

All plan allocations used here work as given: CONFIG bit 20, CAPS2 bit 15, byte 26 bit 3, argument 14 (10 release rows and 12 on a diagnostic image before XM2's title rows, which add 1-3; within the limit of 20 that XM1 sets, and within today's limit of 12 [V firmware/include/multitini_menu.h:18; D TD:1917] even without the title rows).

---

## 9. Work steps in order

Prerequisites: XM1 (stored/effective split, option registry, bounds-checked append) and XM2 (`.cfg` reader) land first. The 0.26 spec edit lands after all feature designs are reviewed. The plan's timing decision (Q-SC18) is taken before step 9.

| Step | Content | Files | Size |
|---|---|---|---|
| 1 | 0.26 spec bits and constants (shared spec edit); package ABI default 26; regenerate; `test_interface_spec.py` and `test_core_package_tool.py` | `spec/interface_v0.json`, generated files, `spec/core_interface_v0.md`, `scripts/make_core_package.py` | 0.25 day of the shared edit |
| 2 | RTL: gated `$0BC` override; clamp line | `multitini_c64_turbo_regs.sv`, `multitini_control_regs.sv` | 0.25 day |
| 3 | T-SC1 and T-SC3 bench extensions; run | `tb_c64_island_regs.sv`, `tb_control_regs.sv` | 0.5 day |
| 4 | T-SC2 bench, runner and probe program source (with the detection idioms); licence-list rows; `scripts/check_repo.py` | `tb_c64_scpu_detect.sv`, `test_c64_scpu_detect.py`, `tests/c64_turbo/`, `licenses/files.csv` | 1-1.5 days |
| 5 | Re-run G-LOCK, G-TURBO, G-STK (no change expected) | existing runners | machine time, 0.25 day review |
| 6 | Firmware: flag constants, probe, `mt_c64_turbo_config()`, settings codec | `multitini_c64_features.h`, `c64_features.c`, `supervisor_settings.{c,h}` | 0.5 day |
| 7 | Firmware: row (ABI and caps gate, effective value, `(title)`), handler (overlay-aware), dispatch, status texts (≤ 63 characters), `:turbostat`, debug-override bit-20 rule, effective turbo flags in the menu view, registry row | `core_frontend.c`, `main.c`, `c64_debug.c`, `c64_option_registry.c` | 0.75-1 day |
| 8 | Host and Python tests | section 7.2 files | 0.75-1 day |
| 9 | Full PAL and NTSC builds; timing and CDC review (C1, C3); utilization delta, A0-A9 loads and net delays, and worst half-period slack in the build receipt. PAL timing blocks; NTSC per Q14 (section 7.5) | — | build time + 0.5 day |
| 10 | Board tests B1-B10, G-REU0 | — | 1 day |
| 11 | Doc edits (section 4.4); TD Q21, D20, D21, E7-C | `docs/*.md` | 0.5 day |
| 12 | E7-C when a U64/C64U is available; apply Q-SC1/6/7 outcomes | — | 0.5 day, unscheduled |

Total: about 6.25-7.75 working days plus build and board time; Q-SC4 (a) adds about 0.5 day (RTL term, T-SC1/T-SC2 cases, docs). Steps 2-5 and 6-8 can run in parallel. Step 9 needs steps 1-5 done and passing, and the timing decision taken.

---

## 10. Review

Two reviews changed this design. Each issue was checked against the source before it was applied.

First review (19 issues, all applied):
- The menu handler edits the title overlay without saving when the overlay owns the key; the row shows the effective value with ` (title)`, from four defined states.
- The row is gated on ABI 26 as well as the cap; the package ABI default moves to 26; the runtime mock carries both ABIs.
- C3 restated: the compare adds fanout on the half-period path; shared compare; before/after measurement; the TD R2 ladder; the registered fallback removed.
- PAL timing blocks and NTSC follows Q14.
- The overlay end events restated; a disk-swap board case added.
- Licence-list rows, the GNW64 blocking board test with original-code detection idioms in simulation, and B7 as a relative comparison with the previous image.
- Byte 26 test mask `0xD0` at XM3; spelling `(inactive: Turbo Off)`; `:turbostat` decode with CONFIG[10].
- U64 Registers-mode files that enable Detect act differently here (R6, Q-SC14).
- `$D0B8` has current non-mouse consumers, so Q-SC4 became a decision now.
- The debug `:speed` override clears bit 20 when Turbo Control is Off.
- TD:277 and TD:2284 need a Detect qualifier.

Second review (cross-document, 2026-10-24):
- The timing and fit baseline moved from dev6 to the committed and released dev7 image (`3df6d04`, `4D00C707`, ABI 0.25): PAL fast island +0.030 ns under a one-build waiver, 94.13 % slices; NTSC +0.219 ns. G-SCPU now depends on the plan's timing decision (Q-SC18).
- All safety advice now names "Revert title settings" instead of Options → Reset, because a D64-source overlay survives Options → Reset while drive 8 holds a disk. The end-event table copies the title-config list. B10(d) runs with a D64 and a PRG source. Q-SC17 records the alternative.
- The launch status line follows the title-config precedence. While writing this document, the 63-character status buffer (main.c:190) was found; every status text was shortened to fit, and the owned help was checked against the overlay's 63-character source-name limit.
- G-STK runs in compare mode; the manifest is never edited.
- Anchors re-read at `3df6d04`; corrected: U64 `effectuate_settings` (u64_config.cc:1079), the DMA data path (fpga64_sid_iec.vhd:473, 808, 1287), the VIC write cases (video_vicII_656x.vhd:1913-1915), the TOGGLE_CORE_OPTION branch (~4947-4958).
- Research-note citations were replaced by primary sources, or the claim is tagged [I].
