# C64 turbo evidence: prior art

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: C64 CPU acceleration: external research findings (Multitini One, branch c64-turbo)

No repository files were changed. Downloaded sources and notes are in (session scratch, not kept) (subfolders mega65, mister, scpu, doom, chameleon, cores, web).

Tags used below: [V] = verified in a primary source or in source code I read. [M] = measured by a third party on real hardware. [I] = my inference. Lines marked file:line refer to the upstream files downloaded into the scratch folder.

---

## A. Headline findings

1. **64x already exists in a shipping product.** The C64 Ultimate and U64 Elite-II run the 6510 at 64 "time slots" per µs. Each slot can read one byte of internal memory. Any access to the external bus or I/O is locked to 1 MHz.
   - Measured top setting delivers 63.03x on the C64 Ultimate and 47.00x at "48 MHz". The top two settings fall short by exactly one PHI2 multiple. [M]
   - The original U64 tops out at 48x. [V]
2. **I/O cost decides the real speedup.** Measured on a C64 Ultimate at 64 MHz [M]:
   - Plain RAM `lda abs,x`: 63.5 ns (63x).
   - Any I/O-space read: about 920 ns.
   - Any I/O-space write: about 1920 ns.
   - The badline setting does not change these numbers; the floor is I/O decode locked to φ2.
   - The Chameleon reports 10–14x for memory-only loops, 4–6x for I/O-heavy programs, and about 12x for BASIC. [V]
3. **No accelerator I found keeps cycle-exact behaviour in turbo.** All of them rely on:
   - a manual or software switch back to 1 MHz;
   - automatic slowdown on IEC activity (MiSTer, MEGA65);
   - keeping the machine chips (VIC, SID, CIA) on their native clock.

   Raster-timed code, fast loaders and cycle-timed tape or digi code are given up in turbo everywhere.
4. **A dual-port BRAM CPU island is the MEGA65 design.** The MEGA65 keeps 384 KB of chip RAM in a dual-port BRAM: CPU on port A at 40.5 MHz, VIC-IV on port B (gs4510.vhdl:1650-1660). [V]
   - This removes the write-through cost that limited the SuperCPU and TurboMaster.
5. **A clock observation for this board.** [I, arithmetic]
   - PAL φ2 = 31.527954/32 = 0.985249 MHz, so 2 × c64_clk = 63.056 MHz = exactly 64x φ2.
   - NTSC: 2 × 32.727266 = 65.4545 MHz = exactly 64x.
   - A CPU island at 2 × c64_clk from the same MMCM, doing one 6502 cycle per clock, gives 64 slots per native cycle with a fixed phase relationship and no CDC.
   - 96x would need 3 × c64_clk (94.6 / 98.2 MHz).
6. **More than 64x needs a faster clock or fewer cycles per instruction.** Known FPGA 6502 data points:
   - Arlet-derived core: 80 MHz on Spartan-6 -2 with BRAM, "just meets timing" [V].
   - 65F02: 100 MHz on Spartan-6, then lost reliable 100 MHz when more memory-map logic was added [V].
   - Cores that remove dead cycles save roughly 10–30% of cycles [I, from Arlet's cycle tables]. They drop NMOS illegal opcodes and/or dummy cycles that C64 software depends on.
   - I found no published T65 fmax.

---

## B. Per-system findings

### B1. Ultimate 64 / U64 Elite-II / C64 Ultimate (Gideon Zweijtzer)

The U64 FPGA source is not public. Firmware and config code are public (GideonZ/1541ultimate).

**Mechanism** [V] (1541u-documentation config/turbo_mode.rst, "Notes on Bad Lines in Turbo Mode", near the end of the file):
- "the CPU and memory subsystem run at 48 MHz (U64) or 64 MHz (U64E2), and ... this turbo setting determines how many of the available time slots are assigned to the CPU. When the VIC is enabled, it will still steal cycles from these available time slots."
- "On the U64, there are 48 time slots per MHz, and on the C64U even 64. In all of these time slots a memory byte can be read, as long as the memory is internal. As soon as an access needs to be done to the external bus, it will request the cartridge bus bridge ... This always works at 1 MHz", and only while BA = 1.
- The badline setting "allows the CPU to finish its cycle ... regardless of the BA signal. When badlines are enabled, the CPU is halted during BA=0 ... When badlines are disabled, the CPU can continue even when BA=0, given that the access is to an internal address."
- BA on the cartridge port is never changed. External devices and SID sockets still hit badline limits.

**Speeds** [V] (u64_config.cc:321-322):
- U64: 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48.
- U64E2 / C64U: 1, 2, 3, 4, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64.

**Software-visible registers** [V] (turbo_mode.rst; u64_config.cc:325-326, 387-394, 1603-1637):
- $D031: bits 0–3 = speed index; bit 7 = badline timing (0 = enabled). Live only when Turbo Control is "U64/C64U Turbo Registers" or "TurboEnable Bit"; otherwise reads $FF.
- $D030 bit 0: write 0 = 1 MHz with badlines, 1 = menu settings. Read gives turbo on/off.
- $D07A / $D07B: SuperCPU-style normal/turbo, write-only.
- $D0BC: SuperCPU detect, read-only. It needs a separate option, which defaults to Disabled.
- Badline Timing defaults to Enabled (config default 1).

**Measured behaviour (Doom C64U project, slesinger/doom, C64 Ultimate fw 1.1.0 / core 1.49)** [M]:
- I/O timing (docs/georam-vs-reu.md §11, lines ~440-470):

  | Access | 1 MHz | 64 MHz |
  |---|---:|---:|
  | RAM `lda` | 3997 ns | 63.5 ns |
  | RAM `sta` | 4997 ns | 79.6 ns |
  | I/O `lda $de00,x` | 3997 ns | 920 ns |
  | I/O `sta` | 4997 ns | 1920 ns |

  Their summary: "every I/O access is locked to one 1 MHz bus cycle, and a write to two."
- CIA timer A counts 1 µs regardless of turbo.
- Frame rate (pipeline.md:1518-1530):

  | $D031 | Speed | fps |
  |---|---|---:|
  | $00 | 1 MHz | 0.83 |
  | $06 | 10 MHz | 7.15 |
  | $0B | 24 MHz | 16.78 |
  | $0F | 64 MHz | 50.1 (vsync-locked) |

- SID writes at 64 MHz were tested (src/sidtest.asm). IRQs at 64 MHz are fine when the handler saves and restores $01 (src/irqtest.asm, docs/reu-format.md §6).
- Firmware quirk (src/main.asm:241-264): writing the target speed to $D031 alone did not take effect. They write $00 and then $0F. Issue #874 also found $D031 writes ignored for about 2 s after reset.

**REU under turbo (important for Multitini's DDR-backed REU)** [V/M]:
- 1541ultimate tests/e2e/io/c64/reu_turbo_test.py:1-30: "Starting an REU transfer has to stop the 6510 in the same cycle as the write to $DF01. If the core inserts any delay before it asserts DMA, a machine running faster than 1 MHz executes the instructions that follow while the transfer is already in progress."
  - Real REU code puts the next transfer's register setup right after the command, so a delayed DMA corrupts data.
  - Fixed in the U64 cores (`reu.vhd` asserts `reu_dma_n` on the command write when `g_no_dma_delay` is set). The 3.15 release notes say "REU transfers started while the CPU is running in turbo mode no longer risk returning stale data".
  - Doom C64U failed its REU check before the fix.
- REU DMA rate does not scale with turbo:
  - Doom docs: 1.00 µs/byte flat on the C64U.
  - Issue #839 (U64E fw 3.15): stash 5.14 B/µs and fetch 3.52 B/µs at 48 MHz. A plain `lda/sta` copy loop managed 5.28 B/µs. Above 3 MHz the CPU out-copies the REU.

**Cartridges** [V] (ultimate64.com/OlderFirmware):
- v1.37: "Since the introduction of the turbo mode, the cartridge compatibility of the U64 was flawed." With bus mode "Dynamic" the U64 probes cartridge bus cycles: "By cleverly caching these results, turbo mode is applied automatically in memory regions that do not need to run at 1 MHz."
- 3.15: a compatibility mode where "the C64 runs continuously at 1 MHz and every CPU cycle is exposed on the cartridge bus".
- 3.15: per-file configuration (turbo settings can travel with a disk image or PRG).

**Failure modes and known issues** [V]:
- v1.34: "Corrupted disk access with some loaders, when bus bridge is not on 'Quiet' mode, and badlines are disabled."
- 3.14d: "Turn off turbo mode temporarily on reset to have PAL/NTSC detected properly. This allows JiffyDOS to work correctly with Turbo mode enabled."
- Issue #737: virtual IEC printer with JiffyDOS at 8 MHz loses bytes with badline timing Enabled; correct with badline timing Disabled. The stock KERNAL was fine.
- Issue #153: in TurboEnable mode, a program that writes $D030 = 1 (C128 2 MHz idiom) gets the full menu turbo; the reporter says "only some parts of the software uses $d030 control, and if its enabled by default it causes crashes".
- An itch.io user reports a game needed a fix before joystick detection worked at higher clock speeds.
- Open: I found no documentation of an automatic IEC slowdown on the U64. How JiffyDOS works at turbo there is unknown to me.

### B2. MEGA65 (45GS02 at 40.5 MHz) — mega65-core gs4510.vhdl, cia6526.vhdl

**Speed selection** [V] (gs4510.vhdl:212-219, 5254-5300):

| C128 2 MHz ($D030 bit 0) | C65 FAST ($D031) | MEGA65 FAST ($D054) | Speed |
|---|---|---|---|
| 0 | 0 | X | ~1 MHz |
| 1 | 0 | 0 | ~2 MHz |
| 0 | 1 | 0 | ~3.5 MHz |
| 1 | X | 1 | 40.5 MHz |
| X | 1 | 1 | 40.5 MHz |

- `POKE 0,65` forces 40 MHz; `POKE 0,64` restores normal speed control. These two values written to the CPU port DDR set `force_fast` instead of the DDR (gs4510.vhdl:2141, 3300-3306).
- A CAPS LOCK key input also forces 40 MHz (`fast_key`).

**Slow modes are not bus-cycle exact** [V] (45GS10-Processor-Notes.md; gs4510.vhdl:4744-4790):
- At 1 / 2 / 3.5 MHz it "counts the cycles that would have been taken on a 6502 ... and introduces pauses after each instruction". The CPU runs fast, then waits off its cycle debt.
- Badlines are approximated as a lump stall of 40–43 cycles. The code comments say it only approximates, because "we allow whatever instruction was running to complete".
- Register $D710: bit 0 = badline emulation enable, bit 1 = 6502-style 7-cycle interrupts, bit 3 = charge for taken branches, bits 4–5 = badline cost 40..43 (gs4510.vhdl:3410-3419).

**Memory** [V]:
- Chip RAM is a dual-port BRAM `shadowram`: CPU on clkA, VIC-IV on clkB (gs4510.vhdl:1650-1660).
- Wait states: shadow RAM 0, I/O read 1, colour RAM 2 (gs4510.vhdl:530-544).
- There is also a zero-page cache.

**I/O** [V]:
- CIA, VIC and SID registers are "fastio" at the CPU clock with wait states. They are not locked to φ2.
- Chip internals still step on `phi0_1mhz`.
- In slow mode, CIA port A writes (IEC lines) are delayed by 4 φ2 cycles "so that it is as though it were written at the end of an STA $nnnn instruction" (cia6526.vhdl:20-23, 389-401, 736-742).

**IEC slowdown** [V] (gs4510.vhdl:4307-4318, 3600-3601):
- When `iec_bus_active` and $D7F1 bit 1 (default 0), the CPU slows for 40000 cycles (about 1 ms at 40.5 MHz).
- The slowdown cancels POKE 0,65 and CAPS LOCK force-fast. It does not override the $D054 select: "the programmer is assumed to know what they are doing."

**Cycle counts** [V, old notes]:
- 45GS10 notes give reduced counts: implied 1, zero page 2, absolute 3, plus 1 for reads and 1 for RMW.
- RMW to $D019 "incur a one cycle penalty to emulate the dummy write of the NMOS 6502". The DEC/LSR $D019 IRQ-ack idiom must keep working.
- The notes are marked "may not be completely accurate".
- At 40.5 MHz the machine runs about 2,600 cycles per raster line (Racing the Beam article).

**Note** [V]: the MEGA65 also ships a separate MiSTer-based C64 core (MJoergen/C64MEGA65) for native C64 use.

### B3. CMD SuperCPU (WDC 65C816 at 20 MHz)

Sources: VICE scpu64cpu.c / scpu64mem.c; C64-Wiki; CMD spec text.

**Mechanism** [V, VICE model]:
- Fast SRAM holds a copy of C64 RAM and the OS. Writes into "mirrored" ranges also go to motherboard RAM through a one-deep write buffer:
  - `scpu64_clock_write_stretch` sets `buffer_finish` to the next C64 cycle.
  - `wait_buffer()` stalls only when the next mirrored write or I/O access comes before the buffer drains (scpu64cpu.c:138-149, 244-255).
- I/O reads stall to the C64 bus, about 1–2 C64 cycles (`scpu64_clock_read_stretch_io`, :178-191, with "measured" sub-cycle constants).
- Writes to $FF00 trigger REU DMA through a special long-stretch path.

**Optimization modes** set which range is mirrored [V] (scpu64mem.c:81-86, 705-712; C64-Wiki):

| Register | Mode | Mirrored range |
|---|---|---|
| $D074 | VIC bank 2 / GEOS | $8000–$BFFF |
| $D075 | VIC bank 1 | $4000–$7FFF |
| $D076 | BASIC | $0400–$07FF |
| $D077 | No optimization | all memory |

Mirroring cost is why these modes exist.

**Speed registers** [V] (VICE scpu64mem.c:690-760):

| Register | Function |
|---|---|
| $D072 / $D073 | System 1 MHz enable / disable |
| $D07A | Software 1 MHz enable (normal) |
| $D07B (and $D079) | Turbo |
| $D07E / $D07F | Hardware registers enable / disable |
| $D0B0–$D0BF | Status; $D0B5 bit 6 = speed switch, $D0B8 = software/master speed flags |

- Issue #654 on 1541ultimate describes the real switch: "normal" locks the machine at 1 MHz. "Turbo" only allows 20 MHz; software can still change speed.

**Compatibility** [V, CMD spec]:
- "performs disk access functions properly in any speed mode."
- "Most ROM cartridges (generally games) will either force the SuperCPU to 1 MHz mode" or need Turbo turned off by hand.
- The REU "does not have to slow down to 1MHz" for DMA.
- Documented 6510 opcodes only (C64-Wiki). Illegal-opcode software fails.
- [I] The "System 1 MHz" register pair is how the SuperCPU ROM likely holds 1 MHz during serial routines. I could not reach a primary document to confirm this.

### B4. Turbo Chameleon 64 (FPGA-64 6510 core) — chameleon_progmanual.pdf §8

**Mechanism** [V]:
- "Only CPU cycles where memory is accessed can be speed-up. When any registers are accessed the CPU needs to slow down and resynchronize to the C64 clock. This is true for all register accesses in the D000h-DFFFh area", colour RAM included.
- Two speeds: double (two accesses per system cycle, "almost identical to the C128 in 2 MHz mode" when VIC accesses are off) or maximum. Maximum "uses all remaining free SDRAM cycles"; actual speed depends on cache hits and other SDRAM users.

**Register $D0F3 CFGTUR** [V]:

| Bit | Function |
|---|---|
| 7 | Turbo enable |
| 6 | Turbo switchable with button |
| 5 | Mirror the enable at $D030 bit 0 |
| 0 | Limit to 2 MHz |

**Other facts** [V]:
- REU: 1 MB/s, stops on badlines.
- FAQ speeds: 10–14x tight memory loops, 4–6x I/O-heavy, about 12x BASIC. The VIC-II screen stays active in turbo. Tape is inoperable while the Chameleon is used. No 65816, so SuperCPU and Flash-8 software do not run.
- The designer notes reliable 20 MHz cycle-exact operation with SDRAM is hard.

### B5. Commodore 128, 2 MHz mode ($D030 bit 0)

[V] from C128 I/O map and forum sources:
- $D030 bit 0 = 2 MHz, bit 1 = test bit.
- In 2 MHz mode the VIC-IIe cannot fetch display data. The 40-column screen shows garbage while borders stay correct, so programs switch to 2 MHz only in the borders.
- Most I/O accesses are clock-stretched to 1 MHz.
- On a C64, $D030 reads $FF. Programs use a non-$FF read to detect a C128.

### B6. MiSTer C64 upstream turbo (MiSTer-devel/C64_MiSTer, rtl/fpga64_sid_iec.vhd)

[V]
- Extra CPU slots are at sysCycle CPU0 / CPU4 / CPU8, gated by `turbo_m(0..2)` and `cs_ram`. The native slot at CPUC also allows I/O (fpga64_sid_iec.vhd:830-836).
- `turbo_m` is decided at EXT1 / EXT5 only when `cs_io = '0'` and `dma_req = '0'`: "00" → 2x, "01" → 3x, "10" → 4x (fpga64_sid_iec.vhd:848-861).
- Hard ceiling: 4 memory slots in the φ2 half of the 32-clock frame. Multitini inherited this structure.
- C128 mode: the VIC register $D030 bit 0 sets `turbo_reg`. Reads return "1111111" & (`turbo_reg` or not `turbo_en`) (video_vicII_656x.vhd:1685, 1750).
- Smart mode: `turbo_mode = {status[47] & ~disk_access, status[46]}` (c64.sv:933). `disk_access` is set on any edge of the C64 or drive IEC CLK, or on parallel-cable strobes. It holds for `to <= 16000000; // 0.5s` (c64.sv:1170-1188).
- README: "C128/Smart Turbo mode up to 4x".

### B7. Historical cards

**TurboMaster CPU** (Schnedler; C64-Wiki) [V]:
- R65C02 at 4.09 MHz (DOTCLK/2), 64 KB SRAM. The 6510 is disabled through DMA.
- The clock is halted or synchronized to the C64 bus for I/O ($D000–$DFFF), badlines and sprite DMA, all RAM writes (write-through, no buffer) and external DMA.
- Speed switch is bit 7 of $00 (0 = 4 MHz) plus a hardware switch.
- Failures: undocumented opcodes do not work; graphics stored under $D000 do not work; `POKE 0,0` crashes in turbo; timing-sensitive programs need 1 MHz.

**Flash 8** (Roßmöller; C64-Wiki) [V]:
- 65816 at 8 MHz. The first 64K replaces C64 RAM.
- Some areas are not visible to the VIC (zero page and stack hidden "for performance"), causing corrupted screens.
- Even in 1 MHz mode, chip timing differs: IFLI fails, and a per-machine potentiometer adjustment is needed.

**TurboProcess** [V, sparse]: a 4 MHz 65816 cartridge. cc65's accelerator header lists it as 4x. No technical documentation was reachable.

### B8. 65F02 (Jürgen Müller, e-basteln.de) and fast FPGA 6502 cores

**65F02** [V]:
- Spartan-6, Arlet core with 65C02 extensions, 100 MHz internally, 64 KB on-chip RAM.
- At power-on it copies host RAM and ROM in. Afterwards only I/O goes out, as a host-clock bus cycle. It detects "time-critical peripheral devices" and slows temporarily.
- July 2024: adding memory maps slowed the logic enough that 100 MHz was no longer reliable.
- The C64 is untested.

**hoglet67/verilog-6502** (Arlet's core plus 65C02) [V]:
- "running at 80MHz using 64KB of internal block RAM. It just meets timing at 80MHz" on XC6SLX9-2.
- Synchronous memory: data is expected the cycle after the address, so it connects directly to BRAM.

**Arlet's newer cores** [V] (READMEs):
- verilog-65C02-fsm: synchronous memory, all dead cycles removed (implied 1, branch always 2, RTS 4, JSR 5, PHx/PLx 2, no page-cross or index penalty). Passes Dormann with BCD off. No IRQ / NMI / RDY. Proof of concept only.
- verilog-65C02-microcode: complete, handles IRQ / NMI / RDY, BCD costs an extra cycle. Supports asynchronous memory through a combinational next-address output `AD`.
- Neither implements NMOS illegal opcodes; both state "(except for NOPs as undefined ...)".

**Unverified figures from search snippets of 6502.org threads** (the forum was unreachable):
- One core at 1.01 cycles per instruction versus 3.14 for T65 on a benchmark.
- A "Pipelining the 6502" design study (micro-op queue) reaching CPI around 1.x. Its author calls the ideal one-instruction-per-cycle pipeline "somewhat of a chimera".

**Other data points:**
- All About Circuits lists cpu65c02_tc at "110 MHz up to 180 MHz" with no device named [unverified].
- PiTubeDirect emulates a 6502 at 274–292 MHz in software on a Pi [V, project README / RPi magazine].
- No published T65 fmax was found.

**In-repo pointer** [V] (docs/APPLE_IIGS_ACCELERATION.md:1-30): this board's IIgs build at 57.27 MHz had setup failures whose paths "start at the CPU microcode ROM and cross address generation and MMU/IO decode". About 75% of each path was routing; closure needed reroute passes.

---

## C. Speed-sensitive software and how accelerators handle it

**KERNAL serial IEC**
- Serial IEC has no receiver back channel during bit transmission: "The protocol therefore is based on timing", with about 60 µs holds (pagetable.com part 4) [V].
- [I] When the C64 is talker (SAVE, OPEN, commands), its bit-hold times come from instruction delays and shrink in turbo. When the C64 listens (LOAD), the drive sets the pace, but the KERNAL's own handshake delays still shorten.
- Handling:
  - MiSTer: drop to 1x on any IEC CLK edge, then 0.5 s holdoff.
  - MEGA65: opt-in roughly 1 ms cooldown; it does not override an explicit software speed select.
  - SuperCPU: disk works in any speed mode, probably through system 1 MHz in its ROM [I].
  - U64: no documented mechanism.
  - Multitini today: guard with 0.5 s holdoff (as the lead stated).

**JiffyDOS and fast loaders** (cycle-timed on both sides)
- They must run at 1x. Locking I/O to φ2 (U64) keeps each access aligned but not the spacing between accesses [I].
- U64 #737: JiffyDOS printing at 8 MHz fails unless badline timing is disabled. Fast loaders also avoid badlines by watching raster position [I].
- Rule everywhere: IEC-activity slowdown, a user switch, or per-title profiles (U64 3.15).

**Tape**
- Chameleon: tape unsupported. Other accelerators do not document it.
- [I] Loaders that time pulses with the CIA FLAG/timers work at any CPU speed. Loaders that count cycles in loops break.
- Open for Multitini: does the turbo guard also cover cassette motor or sense activity?

**Raster-timed code** (stable raster, FLD/FLI/VSP, open borders, digis)
- Breaks in any turbo. U64's "badline timing enabled" keeps line-level CPU stalls but not exact cycle position.
- C128 2 MHz users switch only in borders. Mapping $D030 = 1 to full turbo crashes such code (U64 #153).

**Delay loops and timing-based PAL/NTSC detection**
- Games run too fast or detect the wrong system. U64 forces 1x across reset (3.14d).
- Most games pace on raster or CIA and therefore gain nothing from turbo; they also lose nothing.

**REU / DMA**
- The CPU must halt in the same cycle as the $DF01 (or $FF00) trigger write (U64 regression test).
- DMA rate stays near 1–5 B/µs. At 48–64x the CPU copies faster than DMA.

**Cartridges**
- Bank-switching and EXROM/GAME-toggling cartridges need per-access correctness.
- U64 probes and caches which regions can run fast. SuperCPU: most ROM cartridges force 1 MHz. Multitini currently excludes cartridge accesses from turbo.

**Illegal opcodes**
- TurboMaster, SuperCPU, Flash 8, 65F02 and Arlet cores lack NMOS illegal opcodes, and that is a known failure class. T65 keeps them.

**Benefits in practice**
- BASIC (about 12x on Chameleon), decrunchers, GEOS, compilers and 3D engines.
- Doom C64U: 0.83 fps at 1x → 50 fps at 64x. It needs a 16 MB REU and U64 $D031 support.

---

## D. Design lessons for a dual-port BRAM CPU island targeting 64x+

1. **Clock.** Run the CPU island at 2 × c64_clk from the same MMCM, one 6502 cycle per clock. That is exactly 64 slots per native φ2 with a fixed phase, and I/O sync becomes a counted wait for the native phase. Budget about 63x delivered (U64 lost one slot per φ2). [I]
2. **Memory.** CPU on BRAM port A, VIC on port B, as on the MEGA65. Use a registered next-address so the path is BRAM read only.
   - Colour RAM also dual-ported, with a 1–2 wait-state fast path as on the MEGA65. Character ROM readable at speed.
   - Precompute the PLA plus cartridge map into a per-page table updated on $00/$01, EXROM/GAME and bank writes. The 65F02 lost 100 MHz to memory-map logic; Multitini's current worst path is cartridge mapping plus cache hit; the IIgs critical path crosses MMU/IO decode. [I]
3. **I/O.**
   - Run every $D000–$DFFF access to VIC, CIA, SID, I/O1 or I/O2 as a native-phase access, as U64, Chameleon and SuperCPU do. Measured U64 cost: read about 0.92 µs, write about 1.92 µs.
   - Avoid MEGA65-style fast I/O for the real chips. It changes CIA/VIC semantics, such as several accesses inside one φ2 or the timing of writes against timer ticks.
4. **Interrupt sampling after I/O (risk).** [I] After an IRQ ack (a write to $D019, or a read of $DC0D/$DD0D), the chip's IRQ/NMI line updates on a later φ2 edge. At 64x the CPU can run RTI/CLI and sample a stale asserted IRQ, giving spurious re-entry.
   - Hold IRQ/NMI sampling until the post-access line state is valid, for example until the next native edge after an I/O access.
   - The MEGA65 has a "6502-style slow interrupts" option and IRQ deferral, which suggests it met interrupt-timing issues too.
   - Do not post (buffer) I/O writes.
5. **NMOS bus semantics.** Keep the RMW double write, the indexed page-cross dummy read and the stack/BRK dummy cycles for I/O addresses at least. Example: `LDA $DC0D-$xx,X` with a page cross clears the ICR on the dummy read. Keep all illegal opcodes.
   - A dead-cycle-removing core (fsm / MEGA65 style) gains maybe 1.1–1.3x and must special-case these behaviours, as the MEGA65 does for $D019 RMW. Keeping T65 semantics is lower risk. [I]
6. **Self-modifying code** is pervasive on the C64. Any prefetch or pipeline beyond one cycle per clock must snoop stores against the fetch stream. [I]
7. **BA / badlines.** With dual-port BRAM the CPU never needs to wait for the VIC on internal RAM. Offer a U64-style "badline timing" option: halt on BA = 0 for C64-like line proportions (U64 default Enabled), or run through for speed (JiffyDOS-at-turbo worked only this way on U64 #737).
8. **DMA.** Assert the REU halt on the triggering write with no added latency (U64 reu_turbo_test.py). Consider an accelerated REU transfer rate in turbo; demand and numbers are in U64 issue #839.
9. **IEC, tape, reset.** Keep the MiSTer-style IEC guard. Consider a narrower "KERNAL serial entry PC-range" 1x hold like the SuperCPU's system 1 MHz [I]. Force 1x across reset and KERNAL init (U64 3.14d). Decide whether tape activity triggers the guard.
10. **Software interface.**
    - Implement the U64 $D031 speed index (and optionally $D030 TurboEnable) so C64 Ultimate turbo software (Doom C64U, the c64-https crypto) runs. Use Doom C64U and the U64 e2e test ideas as acceptance tests.
    - Keep C128 $D030 bit 0 as a true 2x, not full turbo.
    - Do not expose SuperCPU detect ($D0BC). SuperCPU software expects 65816 opcodes.
    - Consider per-title speed profiles.
11. **Measurement method.** CIA timers and video frames count native φ2 regardless of CPU speed. Copy the #874 method: three instruction mixes with equal cycle counts, null-run subtraction, and a frame-count timebase check to verify the delivered multiple.

---

## E. Open questions and risks

- **T65 fmax on xc7z020-2** with a one-cycle BRAM loop and no clock enable is unknown. Synthesize it standalone at 63 / 65.5 MHz before committing. If it fails, the options are an Arlet-style core extended with NMOS illegals and dummy cycles (large validation effort) or pipelining the T65 address path. [I]
- **How the U64 keeps KERNAL and JiffyDOS IEC working at 64x** is unknown (no documented slowdown). The FPGA source is closed.
- **SuperCPU ROM behaviour** for serial routines (system 1 MHz) is unconfirmed. The forum.6502.org, commodore.ca and web.archive.org fetches failed, so the "fastest 6502 implementations" and "Pipelining the 6502" figures above come from search snippets only.
- **Turbo with cartridges** (EasyFlash, freezers in Ultimax) needs per-access mapping at 64x. The U64 needed several releases (1.37, 3.14d, 3.15) to get this right.
- **Turbo with a DDR-backed REU:** the halt-on-write latency and the DDR cache behaviour under 64x-rate CPU traffic between transfers have not been checked.
- **Timing closure:** the IIgs experience on this board (CPU microcode → address → MMU/IO decode, about 75% routing) predicts the same closure pressure at 63–65 MHz.

---

## F. Sources

- 1541u-documentation turbo page: https://1541u-documentation.readthedocs.io/en/latest/config/turbo_mode.html (raw: github.com/GideonZ/1541u-documentation config/turbo_mode.rst)
- Ultimate firmware notes: https://ultimate64.com/OlderFirmware , https://ultimate64.com/Firmware ; howto/release_3.15.md in the documentation repo
- 1541ultimate source and tests: https://github.com/GideonZ/1541ultimate (software/u64/u64_config.cc, software/system/u64.h, tests/e2e/io/c64/reu_turbo_test.py, doom_release_test.py); issues #874, #839, #737, #654, #153
- Doom C64U: https://github.com/slesinger/doom (docs/georam-vs-reu.md, pipeline.md, src/main.asm, src/defs.asm, src/sidtest.asm, src/irqtest.asm, src/reubench.asm)
- MEGA65 core: https://github.com/MEGA65/mega65-core (src/vhdl/gs4510.vhdl, src/vhdl/cia6526.vhdl, docs/45GS10-Processor-Notes.md); https://dansanderson.com/mega65/racing-the-beam/
- C64 for MEGA65: https://github.com/MJoergen/C64MEGA65
- MiSTer C64: https://github.com/MiSTer-devel/C64_MiSTer (rtl/fpga64_sid_iec.vhd, rtl/video_vicII_656x.vhd, c64.sv, README)
- VICE SuperCPU model: https://github.com/VICE-Team/svn-mirror (vice/src/scpu64/scpu64cpu.c, scpu64mem.c)
- SuperCPU: https://www.c64-wiki.com/wiki/SuperCPU ; http://headgap.com/Library/CMD/SuperCPU-Specs.txt ; https://en.wikipedia.org/wiki/SuperCPU
- Chameleon: https://www.syntiac.com/pdf/chameleon_progmanual.pdf ; https://syntiac.com/chameleon_faq.html
- TurboMaster: https://www.c64-wiki.com/wiki/Turbo_Master_CPU ; Flash 8: https://c64-wiki.com/wiki/Flash_8
- C128 $D030: https://www.commodore.ca/manuals/funet/cbm/maps/C128io.doc ; https://www.commodore-128.org/index.php?msg=14237
- 65F02: https://e-basteln.de/computing/65f02/65f02/
- FPGA 6502 cores: https://github.com/hoglet67/verilog-6502 ; https://github.com/Arlet/verilog-65C02-fsm ; https://github.com/Arlet/verilog-65C02-microcode ; https://opencores.org/projects/t65
- 6502.org threads (search snippets only): http://forum.6502.org/viewtopic.php?f=1&t=6049 ; http://forum.6502.org/viewtopic.php?f=4&t=5820
- IEC timing: https://www.pagetable.com/?p=1135 ; https://en.wikipedia.org/wiki/Commodore_bus
- PiTubeDirect: https://github.com/hoglet67/PiTubeDirect ; https://magazine.raspberrypi.com/articles/pitubedirect