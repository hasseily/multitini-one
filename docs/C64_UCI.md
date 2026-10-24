# Multitini One C64: Ultimate Command Interface and W5100S network (design)

Design only. Nothing here is implemented or tested.

- Date: 2026-10-24.
- Repository: `multitini-one`, branch `c64-turbo` at `c7c2fba` (M0.7.0dev6, the base of the turbo and Ultimate Audio designs). The branch head is now `3df6d04` (M0.7.0dev7: drive 9, ABI 0.25, C64 image `4D00C707`). Every repository anchor below was re-read at `3df6d04`. Anchors in `firmware/platform/xilinx/main.c` name the function; other anchors give lines.
- Scope: the Ultimate Command Interface (UCI) at `$DF18-$DF1F` (milestones XM5 and XM5b), its Network target on the board's WIZnet W5100S, and the network-service details the target relies on (milestone XM4).
- Place in the plan: docs/C64_U64_EXTENSIONS_PLAN.md (called **plan** below, cited as "plan §n" with the topic) is the overview and the shared frame. On a shared allocation or rule the plan is binding. Where this design proposes a change, section 8.1 lists it as an open frame item. Sibling designs: docs/C64_SUPERCPU_DETECT.md, docs/C64_MULTI_SID.md, docs/C64_TITLE_CONFIG.md. TD = docs/C64_TURBO_DESIGN.md, UAD = docs/C64_ULTIMATE_AUDIO_DESIGN.md, SPEC = docs/C64_TURBO_U64_SPEC.md.
- Decision records: **Q23** (UCI) and **Q25** (network on the W5100S) (plan §3.6). Deviation numbers: D40-D59 for the UCI, D70-D79 for the network (plan §3.6). User decisions use the plan's P numbers; experiments use its E-X numbers.

**Citation base.**
- `1541ultimate <path>:<lines>` = GideonZ/1541ultimate at `d43546fe` (GPL-3.0, root `LICENSE.txt`). Bare names such as `command_protocol.vhd`, `command_intf.cc`, `dos.cc`, `control_target.cc`, `network_target.cc` mean files of that tree.
- `1541u-documentation <path>` = GideonZ/1541u-documentation at `2da134b31b54` (the UCI chapters under `uci/`).
- `appletini-one <path>:<lines>` = the sibling repository at `73de6fe` (GPL-3.0). Read-only: Multitini copies or ports from it and never edits it.
- Client software is cited by project and file, as read on 2026-10-24: xlar54 ultimateii-dos-lib (`ultimate_lib.c`, `ultimate_lib.h`), xahmol ultimate-uci-oscar64 (`oscar_common.c`, `oscar_common.h`; "oscar64" below), xahmol UBoot64 (`README.md`), GUI64 (its UCI library source, "GUI64 lib"), barryw Ultimate SDK (`README.md`), the SID Detector II UCI notes (`UCI.md`, "SIDD"), the DolphinDOS 2 KERNAL source ("DD2"), the GEOS ultimateRTC driver source, a SuperREU player source and a C64U COPY_FILE bug report. Their commits are pinned when the files are fetched for the board tests (7.3). They are evidence of behaviour only; no client code is copied.
- `#nnn` = an issue or pull request in GideonZ/1541ultimate.
- W5100S chip facts come from the WIZnet W5100S datasheet [D]. This design did not re-read the datasheet; the facts it depends on are plan experiment E-X3 (8.3).
- `build/` is not under source control. Build figures cited from it are the local Vivado reports of the released images.

**Tags.** [V] verified in a primary file (path:line). [D] documented (document and place). [R] user or issue report. [I] inference. [O] open: needs a measurement, a datasheet check or a decision. **USER** marks a decision for the user; the text gives the recommendation and the reason.

**Test results.** None. Nothing was built, simulated or run on the board for this design. No UCI target and no network function is claimed to work on the board (AGENTS.md).

---

## 0. Summary

### 0.1 What C64 software sees

- One new Options row, **Command Interface**, default **Off**, argument 15, after RAM expansion and before Ultimate Audio (plan §5.1, Options arguments). Off: the C64 bus is bit-identical to today.
- On: the U64 register block at `$DF18-$DF1F`. `$DF18-$DF1A` read `$FF`, `$DF1B` the SoftIEC device number, `$DF1C` status (read) and control (write), `$DF1D` identification (read) and command bytes (write), `$DF1E` response bytes, `$DF1F` status bytes. The block is active with no cartridge or with a generic 8K/16K/Ultimax cartridge.
- Targets: Ultimate DOS `$01`/`$02` on the C64 media folder; Control `$04` (a subset, including LOAD_CONFIG through the title overlay); Network `$03` on the W5100S; Software IEC `$05`, which answers "not loaded" until XM5b; HTTP `$06`, an empty target until XM8.
- U64 clients work unchanged where the board has the function. Where U64 users reported a real defect, this design fixes it, unless a known client depends on the defect (section 5).
- The C64 never reaches the W5100S. Every network access goes through the PS service.

### 0.2 What the board gains: the W5100S

- The Appletini ONE card carries a WIZnet W5100S 10/100 Ethernet controller with hardware TCP/IP, on an 8-bit parallel MCU bus to the PL (`eth_d[7:0]`, `eth_a[1:0]`, `eth_rd_n`, `eth_wr_n`, `eth_cs_n`, `eth_rst_n`, `eth_int_n`), bank 35, LVCMOS33 [V hdl/board/multitini_one_top.sv:88-94; third_party/appletini/constraints/multitini_pins.xdc:362-392].
- Multitini holds it in reset in every image today: `eth_rst_n = 0`, strobes and chip select high, data bus released [V multitini_one_top.sv:2329-2334].
- The Appletini firmware drives the same chip as an Uthernet II [V appletini-one hdl/apple/uthernet2_card.sv:1-54; R user].
- XM4 (plan §9) activates it in the C64 image as a shared PS service: a PL bus bridge ported from Appletini, a portable driver, DHCP with renew, a DNS resolver, `0:/multitini/network.ini`, two System page rows and the UART command `:net`. The UCI Network target is its first client. Section 3.10.7 gives the details this design relies on.
- Only the Network (`$03`) and HTTP (`$06`) targets need the network. DOS, Control and Software IEC do not.

### 0.3 Decisions at a glance

UCI (Q23):

| # | Topic | Decision | Who |
|---|---|---|---|
| U1 | Slot | Full U64 slot `$DF18-$DF1F`, no mirrors. Owned only when enabled, with no cartridge or a generic 8K/16K/Ultimax cartridge (plan §6.3) | **USER** P41 |
| U2 | Register semantics | U64 bit-compatible status and control bytes. Protocol fixes that no known client can observe as a regression (3.3) | design |
| U3 | Queue sizes | HDL carries the full 896/896/256 bytes. Firmware sends at most 895 data bytes per block and 255 status bytes, as the U64 does, except DOS ECHO | design |
| U4 | Abort completion | An ABORT with no open transaction (Idle, or Data Last ended by DATA_ACC) completes in the block on that edge. ABORT_P is never visible in Idle, so a PUSH never meets it. Aborts of an open transaction are answered by the service (op 3) | design |
| U5 | Buffer | 2 KB dual-clock BRAM, 8-bit C64 port, 32-bit PS port: one RAMB36 tile, not one RAMB18 (4.7) | design |
| U6 | Service | CPU0, polled in `service_runtime()`. Every handler is a bounded state machine. ABORT and C64 reset are served first in every pass (plan §7.1) | plan |
| U7 | First release (XM5) | DOS 1/2, Control subset, Network on the W5100S (needs XM4), SoftIEC answers "not loaded", HTTP an empty target | **USER** P40 |
| U8 | XM5b | C64 RAM write without reset (load kind 0x17), the FREEZE-bit hold, RAM read for SAVE, the SoftIEC target on drive 9, `$DF1B` = 9 | **USER** (8.2) |
| U9 | XM8 | HTTP target; UCI C64 IRQ; the TCP listener if its reply format is not confirmed earlier | **USER** P6, P8, P48 |
| U10 | DOS root | The package `content_root` (default `0:/multitini/cores/c64/media`), the same as drives 8 and 9, and below the active package directory | **USER** P42 |
| U11 | Unlock | The U64 `$AB`→`$D038`, `$CD`→`$D036` unlock is not implemented: Off means off | **USER** P9 |
| U12 | REU loads | DOS `LOAD_REU`/`SAVE_REU` and Control `$08/$09` copy without the menu pause and without the UA hold. A LOAD holds the machine in HDL (`C64_UCI_CONTROL` bit 10 `HOLD`, no menu) only around drain and invalidate (3.8.6; plan §7.4) | design |
| U13 | Idle socket read | Keep the U64 `FF FF` + `02,NO DATA: 11` reply for a live socket with no data | **USER** P52 |
| U14 | New register | `C64_UCI_STATUS2` at 0x134, read as a pair with STATUS through a shadow (plan §3.4) | plan |
| U15 | Op tags | Every op carries the push sequence and the C64 reset epoch[2:0] (`C64_UCI_HANDSHAKE[31:29]`); no sticky stale mode | plan §3.4 |
| U16 | REBOOT | Control `$06` is a C64 reset with no settings restore: a cartridge restarts without its `.cfg` being read again; overlay, title session and registers stay; drive 8 keeps its current image (3.8.3; plan §7.3) | plan |
| U17 | GET_HWINFO | Sub `$01` keeps the U64 shape (SID 1, SID 2, then emulated sockets, at most 4 frames); the full SID list is a Multitini sub `$02` from XM7 (3.8.3) | **USER** P37 |
| U18 | LOAD_CONFIG live apply | REU, UA and (XM7) SID-map changes from `$50` are written under `HOLD` with the REU idle; REU or UA `Enabled` while a cartridge is attached is BLOCKED (3.8.4; plan §6.4 B9, §8.5) | plan |

Network (Q25). The plan's §9 is the XM4 design; these rows give the parts this design relies on or refines.

| # | Topic | Decision | Who |
|---|---|---|---|
| N1 | Bridge | Shell module `hdl/board/multitini_w5100s_bridge.sv` on `control_clk`, ported from the host half of Appletini's `uthernet2_card.sv`; only in the C64 images (top parameter `NET_BRIDGE`) | **USER** P5 |
| N2 | Bridge details | IOB registers on every pin (proposed: RTL attributes plus a build check, 8.1 FI-2); write data held one cycle past WR# (proposed, 8.1 FI-1); `eth_int_n` through `cdc_bit_sync` | plan §9.2 + design |
| N3 | Socket memory | 2 KB RX and 2 KB TX per socket (RMSR/TMSR `$55`), not Appletini's 4/2/1/1 KB | plan §9.5 |
| N4 | Socket 0 | MACRAW only while no address is held; UDP on port 68 otherwise (DHCP renew/rebind and DNS, demultiplexed by peer port and transaction id) | plan §9.5 |
| N5 | DHCP | Lease client: renew at T1, rebind at T2, INIT at expiry or NAK, confirm on link return; the chip is reset only at a network start | plan §9.6 |
| N6 | DNS | A records, two servers, 2 s per try with one retry, CNAME up to 4 steps, 8-entry TTL cache, no TCP fallback | plan §9.6 |
| N7 | MAC | `mac=` wins; otherwise generated once from the SD card CID, the timer and XADC readings, and stored. Proposed: regenerate when the card's CID changes (8.1 FI-3) | **USER** P54 |
| N8 | Settings | `0:/multitini/network.ini`, strict key=value, journaled replace; default Off until G-NET passes | **USER** P53 |
| N9 | Status | System rows "Network" and "Network status"; UART `:net` | plan §5.3, §9.8 |
| N10 | Lifetime | UCI sockets close at every C64 reset; the lease and a runtime address survive; the chip resets at a core switch and its cached lease is confirmed on return | plan §9.6 |
| N11 | HTTP | Later milestone XM8 (3.10.8) | **USER** P6 |
| N12 | FTP SD service | Not requested; a possible later option, default Off (3.10.9) | **USER** P55 |
| N13 | Debug | Raw W5100S register access and packet traces: off by default, UART only | standing rule |
| N14 | Pin timing | IOB flops plus `set_false_path` on `eth_*` in `multitini_timing.xdc`, justified by the bus sequence (plan §9.3); comment corrected (8.1 FI-1) | plan §9.3 |

### 0.4 Release split

| Milestone (plan §12.1) | Content here | Depends on |
|---|---|---|
| XM0 | Fast-island timing recovery (plan); no UCI content | — |
| XM4 | W5100S bridge, driver, DHCP, DNS, `network.ini`, System rows, `:net`. No C64-visible change | XM3 (0.26 registers) |
| XM5 | UCI block, buffer, engine; DOS 1/2; Control subset with `$50` and GET_HWINFO `$01` for legacy modes; Network target on XM4; SoftIEC "not loaded"; HTTP empty; HOLD; row 15; byte 26 bit 4 | XM3; XM2 for `$50`; XM4 for `$03` |
| XM5b | Load kind 0x17, FREEZE hold, C64 RAM read, SoftIEC target on drive 9, `$DF1B` = 9 | XM5 board pass |
| XM7a | GET_HWINFO `$01` in map mode and sub `$02` (every SID) | multi-SID (docs/C64_MULTI_SID.md) |
| XM8 | HTTP target; listener if not earlier; UCI C64 IRQ if wanted | XM4, XM5 |

### 0.5 Regression gates

- Stock REU behaviour must not change. Expand by Bonzai disks 1 and 2 (G-REU0) is a hard gate, run with the Command Interface Off and again On (REU on, UA off).
- G-STK: `tests/c64_ua/stock_bus.csv` reads `$FF` at `$DF18-$DF1F` with the REU on [V tests/c64_ua/stock_bus.csv:26-33, 4589-4596]. Every default here keeps that trace bit-identical: Command Interface Off; `C64_UCI_CONTROL` = 0x1F, which has no effect while bit 5 is 0; `NET_CONTROL` = 0, so the W5100S stays in reset (plan §6.5).
- With the Command Interface On, only `$DF1B-$DF1F` change: `1f 00 c9 00 00` instead of `ff ff ff ff ff` (T-U5). The trace has no CPU write in `$DF18-$DF1F`; it reads each address once per standard [V grep of stock_bus.csv].
- The network service has no C64-visible effect.

### 0.6 Budget and timing baseline (dev7)

The plan §2.2 table gives the full dev7 figures. Those this design depends on:

| Measure | PAL `4D00C707` | NTSC `4D00C707` | Source |
|---|---|---|---|
| LUT / FF | 36,689 / 30,334 | 36,769 / 30,150 | [V build/Multitini-070dev7/evidence/c64-{pal,ntsc}/reports/utilization.rpt:25] |
| Slices | 12,519 of 13,300 (94.13 %) | 12,362 (92.95 %) | [V build/c707p3/.../impl_1/multitini_one_top_utilization_placed.rpt:76; build/c707n2/... same line] |
| Slices left below 95 % | 116 | 273 | computed |
| BRAM tiles, DSP | 128, 33 | 128, 33 | placed.rpt:106; utilization.rpt:25 |
| Fast island (`clk_out2` c64) WNS | +0.030 ns | +0.219 ns | [V .../timing_summary.rpt:166, 196-210] |
| `c64_clk` (`clk_out1` c64) WNS | +3.483 ns | +1.914 ns | same |
| `control_clk` (`clk_fpga_0`) WNS | +0.288 ns | +0.496 ns | same |

- The PAL worst path is the fast-island half-period path: `main_ram_i/character_reg` (RAMB36, falling edge) to `cpu_island_i/cpu_i/cpu/BAL_reg[8]`, 7.929 ns requirement at a 15.859 ns period [V build/Multitini-070dev7/evidence/c64-pal/reports/timing_summary.rpt:65745-65752].
- The user accepted +0.030 ns for that build only; the repository floor stays +0.100 ns [V build/C64-4D00C707-070dev7-SD/README.txt; scripts/build_and_export_xsa.tcl:207]. The plan schedules XM0 to recover it before XM3, or a waiver policy (**USER** P2).
- 116 slices of headroom on PAL is about 340 LUT at today's 2.93 LUT per slice [I]. XM4 and XM5 need about 170-325 slices at raw estimates and up to about 640 with the ×2 margin (4.7). So these milestones depend on XM0 or P2, and on Vivado packing denser than today.
- The UCI and the bridge add no logic to the fast island or its half-period path (3.7). They do add logic to `control_clk`, which has +0.288 ns on PAL (4.7, R16).

---

## 1. Goal and scope

**Goal.** C64 software written for the Ultimate 64 Command Interface runs on the Multitini C64 without changes, where the board has the matching function. Where the U64 has a defect that users reported, we fix it instead of copying it, unless the fix would break a client that depends on the defect.

**In scope.**
- The C64-visible register block at `$DF18-$DF1F` and its protocol.
- The PL block, the buffer, the PS crossing and the CPU0 service.
- Targets: Ultimate DOS (`$01`, `$02`), Network (`$03`), Control (`$04`), Software IEC (`$05`, XM5b), HTTP (`$06`, XM8).
- Commands that move data into REU memory, and (XM5b) into and out of C64 RAM.
- The menu row, the setting, the `.cfg` key, and UCI LOAD_CONFIG through the title overlay.
- The network-service details the Network target needs (3.10.7), on top of plan §9.
- Tests and board gates for both.

**Out of scope.**
- The U64 cartridge-requested UCI slots `$DE18-$DE1F` and `$DFF8-$DFFF` (EasyFlash, SID player cartridge) [V 1541ultimate software/io/c64/c64_crt.cc:501-509; software/filetypes/filetype_sid.cc:73, 85]. Our UCI stays at `$DF1C` with a generic cartridge and is suspended with every other cartridge type (3.1). UCI clients that need another cartridge type are not supported.
- The `$D038`/`$D036` unlock (3.12).
- The Appletini FTP SD service: a possible later option, default Off (3.10.9).
- C64-side direct access to the W5100S (an Uthernet-style register window for the C64). Not requested.
- TLS, IPv6, mDNS.
- Any change to stock REU behaviour. With the Command Interface Off, the C64 bus is bit-identical to today (G-STK, G-REU0).

---

## 2. U64 behaviour (facts)

### 2.1 Registers [V 1541ultimate fpga/io/command_interface/vhdl_source/command_protocol.vhd]

| Address | Read | Write | Source |
|---|---|---|---|
| `$DF18-$DF1A` | `$FF` | ignored | :97-103 (`X"FF" when others`) |
| `$DF1B` | `bus_id` (SoftIEC device; bits 7-5 = 0) | ignored | :102, :204-208 |
| `$DF1C` | status byte | control byte | :98, :148-170 |
| `$DF1D` | `irq_n & "1001001"` = `$C9`, `$49` while the UCI IRQ is active | command byte | :99, :121, :144-147 |
| `$DF1E` | response byte, `$00` when none is valid; every read pops | ignored | :100, :105, :176-180 |
| `$DF1F` | status byte, `$00` when none is valid; every read pops | ignored | :101, :106, :181-185 |

- The slot is 8 bytes, decoded on address bits 8..3, with no mirrors [V :108, :141].
- A disabled block drives nothing and ignores writes [V :108, :114, :141].
- Status bits: 7 DATA_AV, 6 STAT_AV, 5-4 state (00 Idle, 01 Busy, 10 Data Last, 11 Data More), 3 ERROR, 2 ABORT_P, 1 DATA_ACC, 0 CMD_BUSY ("should NOT be used by C64") [V :54-61, :84-89].
- DATA_AV and STAT_AV are registered and forced 0 while ABORT_P is set [V :131-140].

### 2.2 Control write [V command_protocol.vhd:148-170]

| Bit | Effect |
|---|---|
| 7 DMA, 6 TRIGGER, 5 IRQ | Latched on any write with bit 0 set, **even when the PUSH is refused** [V :152-161] |
| 3 CLR_ERR | Clears ERROR, evaluated before PUSH [V :149-151] |
| 2 ABORT | Sets ABORT_P in any state; only the Ultimate clears it [V :168-170] |
| 1 DATA_ACC | Only in a data state: Data Last → Idle; Data More → Busy with DATA_ACC set [V :163-167] |
| 0 PUSH | Idle → Busy and CMD_BUSY; in any other state sets ERROR [V :152-160] |

### 2.3 Buffers and limits

- One 2 KB RAM: command 0-895, response 896-1791, status 1792-2047 [V 1541ultimate fpga/io/command_interface/vhdl_source/command_if_pkg.vhd:33-41].
- The command pointer stops at 895, so the longest command is 895 bytes [V command_protocol.vhd:95, 145-147].
- The response and status pointers saturate at the last buffer byte and pop even with no valid data [V :174-188]. An 896-byte reply therefore leaves DATA_AV set forever (#957) [R #957; V 1541ultimate software/io/command_interface/command_target.h:60-62].
- The status length register is 8 bits ("FIXME"), so 256 status bytes cannot be expressed [V command_protocol.vhd:233-235].
- The command pointer resets only when the Ultimate writes handshake bit 0: ACCEPT (`$01`) or RESET (`$87`) [V :209-213; 1541ultimate software/io/command_interface/command_intf.h:45-47]. ACCEPT_NEXTDATA (`$02`) does not reset it. So bytes written while a multi-block reply is read survive into the next command [I]. HyperSpeed does not rely on this: `uci_setup_cmd` calls `uci_abort`, which waits for Idle, before it writes [V 1541ultimate roms/c64rom/kernal/uci.s:438-452, 497-526].
- The server reads the command length when it processes the command [V command_intf.cc:157]. Bytes written in Busy before that read join the command [I].
- Handlers read the command straight from the BRAM [V command_intf.cc:55], including bytes beyond the command length left by earlier commands. Several write a terminator at `message[length]` [V 1541ultimate software/filemanager/dos.cc:55, 112, 210, 224, 240, 323, 434; software/io/command_interface/softiec_target.cc:149, 210, 236; software/io/network/network_target.cc:196]. READ_DATA takes its length from bytes 2-3 with no length check [V dos.cc:467-476].
- Every path component is matched as a pattern: `*` any run, `?` one character, case-insensitive; the first match in directory order wins [V 1541ultimate software/filesystem/file_system.cc:79-108; software/components/pattern.cc:84-128].
- `/Temp` is a RAM disk, formatted at every boot [V 1541ultimate software/filesystem/ramdisk.cc:30-41]. LOAD_CONFIG and U64_SAVEMEM default to files in it [V control_target.cc:523, 566].

### 2.4 Server [V command_intf.cc:111-206]

- One FreeRTOS task, fed with events from an ISR queue. Per event: reset notice, then ABORT (`target->abort(consumed)`, `HANDSHAKE_RESET` = `$87`), then DATA_ACC (`get_more_data`, validate, `ACCEPT_NEXTDATA`), then NEW_COMMAND [V :128-185].
- Target = byte 0 & `$0F`; bit 7 = no reply (`HANDSHAKE_RESET` after parsing) [V :165-172; command_target.h:8-9].
- A zero-length PUSH gets an empty Data Last ("Null command") [V :175-182].
- `parse_command()` runs inline. A handler that hangs wedges the interface, and ABORT cannot release it [V :167; R #740].
- An ABORT in any state, Idle included, is answered with `HANDSHAKE_RESET` in the next server pass [V command_intf.cc:136-144].
- `HANDSHAKE_RESET` also resets the command pointer [V command_intf.h:45; command_protocol.vhd:210-213]. A PUSH accepted while ABORT_P is pending is therefore lost, and the server then answers a zero-length "null command" [V command_intf.cc:136-182]. Between the reset and the null-command validate the state is briefly Idle; the server prints a debug line in that gap [V command_intf.cc:142, 176-181]. The oscar64 library saw the lost command in 2 of 6 starts [R oscar64 oscar_common.c:430-441].
- Empty targets answer IDENTIFY with `NO TARGET` / `00,OK` and anything else with `21,UNKNOWN COMMAND` [V command_target.h:30-40; command_intf.cc:238-240].

### 2.5 Enabling

- Item `Command Interface`, `Disabled`/`Enabled`, default Disabled, store `C64 and Cartridge Settings` (alias `Cartridge and ROM Settings`), menu group "Memory Configuration" after REU, before Ultimate Audio [V 1541ultimate software/io/c64/c64.cc:115, 130-133, 1946].
- `.cfg` form: `[C64 and Cartridge Settings]` / `Command Interface=Enabled` (docs/C64_TITLE_CONFIG.md §2).
- U64/C64U unlock since 2026-09: `$AB`→`$D038`, then `$CD`→`$D036`, maps the UCI regardless of the setting [V 1541ultimate software/u64/u64_config.cc:1012-1019; R oscar64 oscar_common.h:53-60].

### 2.6 Turbo

On the U64E and the C64U, I/O1/I/O2 accesses (REU, GeoRAM, UCI) run at one per φ2 whatever the CPU speed [D SPEC:18-21, 119]. An 895-byte command costs at least 895 µs at any speed.

### 2.7 What clients depend on

| Client | Dependency | Source |
|---|---|---|
| xlar54 `ultimate_lib.c` (UltimateTerm, u-chat, u-shell, u-echoserver, …) | Waits for Idle, writes the bytes, then read-modify-write of `$DF1C` (`*controlreg \|= 0x01`); tests "ERROR" at bit 2 (that is ABORT_P); if set, writes `\|= 0x08` and loops back to "wait for Idle", never sending DATA_ACC in that loop | [V xlar54 ultimate_lib.c:472, 484, 489-492, 499] |
| same | Socket read result `0` = EOF, `-1` (`FF FF`) = no data, retry; `uii_tcp_nextchar()` retries forever on −1 | [V xlar54 ultimate_lib.c:825-838] |
| same | `uii_read_file()` sends only 2 bytes (target, `$04`); the length comes from stale bytes 2-3 | [V xlar54 ultimate_lib.c:240-248; dos.cc:467-476] |
| same | `uii_status[256]`, then `uii_status[count] = 0` after the read loop: a 256-byte status writes one byte past the buffer | [V xlar54 ultimate_lib.c:23, 567-577] |
| same | `uii_data[1792]`; reads while DATA_AV | [V xlar54 ultimate_lib.c:24, 551-560] |
| c64-wireguard | Treats `FF FF` as "nothing pending" | [R #847] |
| HyperSpeed KERNAL | Never writes ABORT in Idle; after ABORT polls bit 2; after DATA_ACC polls bit 1; zero-length PUSH to flush | [V 1541ultimate roms/c64rom/kernal/uci.s:478-526] |
| oscar64 library | `uii_detect()` writes ABORT (`$04`) on every call when `$DF1D` = `$C9`; `uii_wait_for_uci()` calls it at program start; each PUSH first waits (bounded, 65,536 status reads) for ABORT_P to clear | [V oscar64 oscar_common.c:95-107, 126-155, 415-441] |
| SID Detector II | `$04 $28 $01`; buffer for 4 frames (22 bytes); reads frames 1-2 at fixed offsets; more than 4 frames is untested; type `$83`/`$85` only as a hint | [D SIDD UCI.md:333-345, 369-371] |
| UBoot64, DMBoot | Save `DMBCFG.CFG` and `DMBSLT.CFG` to `/usb*/` (a pattern path); UBoot64 ships only as a `.crt`; NTP sync through SET_TIME; demo mode switches off every drive but 8 | [D UBoot64 README:64-66, 88, 111, 207-220] |
| GUI64 | GET_TIME; takes characters 11, 12, 14, 15 of the reply without checking the status | [V GUI64 lib:179-192] |
| barryw SDK tests | Write only to `/Temp`, a RAM disk, "so running them cannot fill a medium" | [D barryw README:329-331] |
| Heartbeat Soundtracker, Doom C64U | Their `.cfg` files enable the REU and the Command Interface together | [D docs/C64_TITLE_CONFIG.md §2.7] |

---

## 3. Our design (software-visible behaviour)

### 3.1 Enable, ownership and address map

- Option "Command Interface", Disabled/Enabled, default **Disabled**, `settings.bin` byte 26 bit 4, global (plan §4.1).
- Effective enable = `C64_EXPANSION_CONFIG` bit 5 `UCI_ENABLE`, clamped to 0 unless `C64_EXPANSION_CAPS` bit 5 (plan §3.3).
- **Active** = enabled, no cartridge loading, and either no cartridge or a generic one (`cartridge.sv` type 0: 8K, 16K or Ultimax by EXROM/GAME). Type 0 never enables an I/O1 or I/O2 decode [V hdl/core/c64/ports/cartridge.sv:161-168, 185-190], so it cannot collide with the UCI. The U64 also keeps the UCI at `$DF1C` for these types, because they prohibit nothing [V 1541ultimate software/io/c64/c64_crt.cc:472-479; c64.cc:1420-1428]. Reason: UBoot64 is a UCI client that ships only as a `.crt` [D UBoot64 README:64-66]; whether its CRT is type 0 is open (E-X10). **USER** P41.
- With every other cartridge type, and while any cartridge is loading, the block is held in reset and the slot is not decoded. The CRT load is not refused; the menu row shows "On (suspended: cartridge)" (plan §6.4 B4). A further type may join only after its I/O2 decode in `cartridge.sv` is checked and the T-U4 sweep covers it.
- The REU stays suspended under every cartridge, as today [V hdl/core/c64/multitini_c64_adapter.sv:608]. With a generic cartridge the UCI therefore owns `$DF18-$DF1F` and the rest of `$DF00-$DFFF` is open bus.
- Decode priority in `$DF00-$DFFF` is the plan's table (plan §6.3). For the UCI:
  - `$DF18-$DF1F` with the UCI active and no REU DMA: the UCI owns reads and writes.
  - The REU never sees an access to `$DF18-$DF1F` while the UCI is active. The REU reads `$FF` there today [V hdl/core/c64/multitini_c64_reu.sv:106-124], so with the REU on only `$DF1B-$DF1F` change.
  - No mirrors. With UA off, the REU still answers `$DF38-$DF3F`, `$DF58-$DF5F`, and so on.
  - REU DMA releases the UCI exactly as it releases REU and UA [V multitini_c64_adapter.sv:612-614]. DMA reads see the open bus and never pop.
- The UCI does not need the REU.
- A title `.cfg` may enable the Command Interface (**USER** P20). The launch status line then shows the Command Interface note at its rank in plan §5.4 (error, then SuperCPU warning, then skipped or adjusted, then the Command Interface note, then clean). docs/C64_TITLE_CONFIG.md §3.12 owns the texts.

### 3.2 C64 registers (UCI active)

| Address | Read | Write |
|---|---|---|
| `$DF18`, `$DF19`, `$DF1A` | `$FF` | ignored |
| `$DF1B` | `C64_UCI_CONTROL[4:0]`, bits 7-5 = 0. 31 = no SoftIEC device (XM5). 9 when the SoftIEC target serves drive 9 (XM5b) | ignored |
| `$DF1C` | status byte (3.4); no side effect | control byte (3.3) |
| `$DF1D` | `$C9` (always; `$49` only after the IRQ milestone) | command byte (3.5) |
| `$DF1E` | head response byte if DATA_AV, else `$00` | ignored |
| `$DF1F` | head status byte if STAT_AV, else `$00` | ignored |

Side effects:
- Every side effect happens on an accepted CPU transaction (`cpu_bus_valid`) [V hdl/core/c64/ports/fpga64_sid_iec.vhd:1225-1229], on the same edge the REU uses for its read-to-clear status [V multitini_c64_reu.sv:158-160]. So the CPU gets the value from before the side effect.
- 6502 dummy reads are bus transactions and pop, as on a real C64 with a U2 cartridge [I from command_protocol.vhd:174-188]. Example: `INC $DF1E,X` reads `$DF1E` twice and pops twice.
- REU DMA, VIC DMA, the debug RAM peek and firmware reads never pop. The peek reads main RAM, never I/O [V multitini_c64_adapter.sv:497-498]. This fixes #983 by construction.
- A read of `$DF1E`/`$DF1F` with no valid byte returns `$00` and **does not pop**. The U64 advances its pointer anyway [V command_protocol.vhd:176-185]. Not popping keeps the consumed count exact (3.6).

### 3.3 Control write (`$DF1C`)

The bits keep the U64 meaning (2.2). Several bits may be set. They act in this order, all against the state S before the write:

1. Bit 3 CLR_ERR: ERROR = 0.
2. Bit 2 ABORT. The command fill count returns to 0, as the U64's `HANDSHAKE_RESET` resets its command pointer [V command_protocol.vhd:209-213]. If the same write carries a PUSH that step 4 accepts, the PUSH latches its length from the fill count before this write.
   - S = Idle: no transaction is open, so the abort completes in the block on this edge. ABORT_P stays 0 and the idle-abort count (`C64_UCI_STATUS2[30:28]`) increases by 1 (3.6).
   - S ≠ Idle: ABORT_P = 1. DATA_AV and STAT_AV drop on the next clock.
3. Bit 1 DATA_ACC, only if S is a data state: Data Last → Idle; Data More → Busy with DATA_ACC = 1. It clears the IRQ latch. If ABORT_P is set (before this write or by step 2) and the move is Data Last → Idle, the abort completes in the block on this edge: ABORT_P = 0, idle-abort count +1.
4. Bit 0 PUSH:
   - S = Idle: state Busy, CMD_BUSY = 1, push sequence +1, command length latched (3.5). Bits 7, 6, 5 are latched as FREEZE, TRIGGER and IRQ-enable. ABORT_P is always 0 in Idle (steps 2-3, 3.6), so an accepted PUSH never meets a pending abort.
   - S ≠ Idle: ERROR = 1. **Nothing is latched.** Fix: the U64 latches bits 7-5 of a refused PUSH [V :152-161], so a read-modify-write client in a data state could freeze the C64.

Consequences:
- Writing `$03` in Data Last gives ERROR and Idle, as on the U64 (the PUSH is evaluated against Data Last).
- Writing `$06` in Data Last completes the abort in the block and goes to Idle.
- The xlar54 read-modify-write push reads `$00` (or `$08` with ERROR set) in Idle and writes `$01` (or `$09`, clear and push). It never reads ABORT_P in Idle, so it never writes `$05`, and its bit-2 test after the push fires only if the program itself wrote ABORT after the push [V xlar54 ultimate_lib.c:484-492].
- An earlier draft ran a PUSH accepted while ABORT was pending. That hung this client: ABORT_P stayed visible in Busy after a `$05` push, xlar54 wrote `$1D` and waited for Idle, and the command was served into Data Last, which that loop never leaves [V xlar54 ultimate_lib.c:472-499]. With a service pickup of 1 ms or more, ABORT_P was always still pending at the push. On the U64 the command is lost and a short Idle window lets the loop go on (2.4). The in-block completion removes the case.
- A single write with both ABORT and PUSH from Idle runs the pushed command with the bytes written before it: the abort can only refer to the closed transaction. The U64 answers such a write with a null command [V command_intf.cc:136-182]. Deliberate deviation D40 (section 5); no known client writes it on purpose.

### 3.4 Status read (`$DF1C`)

| Bit | Name | Value |
|---|---|---|
| 7 | DATA_AV | data state and not ABORT_P and response head byte valid and bytes remain |
| 6 | STAT_AV | the same rule for status |
| 5-4 | STATE | 00 Idle, 01 Busy, 10 Data Last, 11 Data More |
| 3 | ERROR | set by a refused PUSH; cleared by CLR_ERR and by C64 reset |
| 2 | ABORT_P | set by ABORT in Busy, Data Last or Data More; cleared by the service (op 3, op 15), by DATA_ACC from Data Last, or by C64 reset. Never 1 in Idle |
| 1 | DATA_ACC | set by DATA_ACC in Data More; cleared when the next block is validated |
| 0 | CMD_BUSY | set by an accepted PUSH; cleared when the reply is validated or reset |

Typical values: Idle `$00`; Busy `$11`; Data Last with data and status `$E0`; Data More `$F0`; waiting for the next block `$12`; Busy with abort pending `$15` [I, derived from the table].

CMD_BUSY stays 1 until the reply is validated, so it clears on the same edge the data state appears. This matches the U64 after its race fix that sends ACCEPT just before the validate [V command_intf.cc:167-174].

### 3.5 Queues and lengths

| Queue | HDL capacity | Firmware policy | U64 |
|---|---|---|---|
| Command | 896 bytes. Byte 897 and later are dropped and set an overflow flag | Overflow → reply `21,UNKNOWN COMMAND`, no action | 895; byte 896 and later overwrite byte 895 [V :145-147] |
| Response block | 0-896 | ≤ 895, except DOS ECHO, which mirrors the command (≤ 896) | 895 [V command_target.h:60-62] |
| Status | 0-256 | ≤ 255 always | 255 [V :235] |

Why the firmware keeps U64 sizes: xlar54's `uii_readstatus()` writes a terminator at index `count`, so a 256-byte status overruns `uii_status[256]` [V xlar54 ultimate_lib.c:23, 577]. The HDL fix still matters: a full block can never leave DATA_AV stuck (#957), because the block counts bytes read instead of saturating a pointer.

Command bytes:
- A write to `$DF1D` while CMD_BUSY = 0 (Idle, the data states, Busy waiting for the next block) stores the byte at the command fill count and increments it.
- A write to `$DF1D` while CMD_BUSY = 1 (from an accepted PUSH to the first validate or op 15) is **not** written to the BRAM and does not move the fill count; `C64_UCI_STATUS2` bit 22 records it. Reason: the PS reads the command as 32-bit words in that window, and its last word covers bytes len..len+3. A C64 write into that physical word on the other clock would make the read unreliable (4.2, C4). On the U64 such bytes are lost too, because its ACCEPT resets the pointer [V command_protocol.vhd:209-213].
- At an accepted PUSH the fill count is latched as the command length (reported in `C64_UCI_STATUS[17:8]`). The firmware reads exactly that many bytes. Bytes written after the PUSH cannot join the command (fix of the race in 2.3).
- The fill count returns to 0 at the first validate of a transaction (op 4/5 while CMD_BUSY = 1, the U64 ACCEPT), at op 1 and op 15, on every ABORT write (3.3 step 2), and at block reset. Later validates of a multi-block reply leave it alone, so bytes written while a long reply is read survive into the next command, as on the U64 (2.3). Op 3 never touches it.

Reply bytes:
- The service writes the reply into the buffer while Busy, then validates with both lengths in one write (op 4/5, 3.13).
- On op 4/5 the block latches both lengths and fetches both head bytes while it still reports Busy with CMD_BUSY = 1. Then, on one `c64_clk` edge, it switches the state, clears CMD_BUSY and raises DATA_AV/STAT_AV (for non-zero lengths). No status read can show a data state while a head is still being fetched. C64 writes take priority on BRAM port A; the switch waits until both heads are valid, at most a few `c64_clk` cycles [I].
- The block counts response and status bytes read, 0-896 and 0-256, and fetches the next head byte within three `c64_clk` cycles after each pop. The next C64 access comes at least one φ2 (32 `c64_clk`) later, so the C64 never sees that gap [I; T-U1 asserts it].

### 3.6 Abort, reset, no-reply, null and short commands

**Abort.** ABORT can be written in any state.
- **No open transaction.** In Idle the abort completes in the block on the write edge (3.3 step 2). In Data Last, ABORT sets ABORT_P; a DATA_ACC in the same or a later write completes it in the block as the state goes to Idle (3.3 step 3). Either way the idle-abort count (`C64_UCI_STATUS2[30:28]`) increases. Example: oscar64's `uii_detect()` writes ABORT in Idle at every program start [V oscar64 oscar_common.c:95-107, 144-154]; the following PUSH waits for nothing.
- The service consumes an idle-abort count change before it dispatches the next PUSH. It calls the abort hook of the last target with the bytes the C64 consumed from the last block (`C64_UCI_STATUS2[9:0]`), as the U64 calls `abort()` for every abort [V command_intf.cc:136-144]. Abort hooks are idempotent: with no open target state they do nothing. The STATUS2 shadow (3.13) gives the count and the push sequence from one snapshot, so the service never sees a new PUSH without an abort written before it.
- **Open transaction** (Busy, Data More, or Data Last before DATA_ACC). The block sets ABORT_P and masks DATA_AV/STAT_AV at once. The state does not change until the service answers. The service checks ABORT first in every pass. It cancels the running handler (every cancel is bounded), calls the target's abort hook with the consumed bytes, and writes op 3.
- Op 3 (applied when epoch and push sequence match and ABORT_P = 1): ABORT_P = 0, DATA_ACC = 0, state Idle, CMD_BUSY = 0, reply counts 0, FREEZE/TRIGGER/IRQ latches 0. The fill count is not touched: the ABORT write already cleared it, and bytes the C64 wrote after the ABORT belong to its next command. This is the U64 `HANDSHAKE_RESET` without its pointer reset [V command_protocol.vhd:209-232]. An op 3 that arrives after the C64 already completed the abort with DATA_ACC finds ABORT_P = 0 and is dropped; one that arrives after a new PUSH carries the old sequence and is dropped.
- ABORT always completes within one service pass, even if a handler is mid-way. This fixes #740.
- A cancelled UCI REU load always finishes with drain and invalidate (3.8.6).
- There is no "abort before push" case: ABORT_P is 0 in Idle, so a PUSH never meets it. HyperSpeed writes ABORT only outside Idle and polls bit 2 [V uci.s:506-526]; op 3 serves it.

**C64 reset.**
- Any C64 reset (menu Reset, PRG load, CRT load or detach, Mount & Run, core reload, UCI REBOOT) resets the block. The adapter's `core_reset_n` already combines these sources [V multitini_c64_adapter.sv:404-407].
- After a block reset: state Idle, all flags 0 including ERROR, counts 0. The C64 reset epoch (`C64_UCI_STATUS[25:22]`) increases by 1.
- Every op carries the epoch[2:0] it was built for (`C64_UCI_HANDSHAKE[31:29]`). The block drops any op whose tag differs from its current epoch[2:0], whatever the op code. An op built before the reset is dropped, and the first op the service builds after reading the new epoch applies at once. There is no stale mode and no op is needed to leave one. Three bits suffice: an op crosses in microseconds and a C64 reset lasts milliseconds, so eight resets cannot fall between the build and the arrival of one op [I].
- Example: every PRG load is a C64 reset. After it, oscar64's ABORT in Idle completes in the block, and its first PUSH runs with no wait. An ABORT in Data Last after the reset is cleared by an op 3 tagged with the new epoch, with no PUSH needed.
- The service sees the epoch change at its next pass, or is told directly from the reset paths (4.5). It cancels the handler, finishes a UCI REU load with drain and invalidate (3.8.6), closes UCI files and directory snapshots, closes UCI sockets and frees HTTP slots. A runtime SET_IPADDR address is kept (3.10.5). The DOS current directories are kept, as on the U64.
- After it copies a command, the engine reads STATUS again; if the epoch changed, it discards the copy (4.2, C4).
- `$DF1B` comes from `C64_UCI_CONTROL`, which a C64 reset does not change.

**No-reply bit.** Target byte bit 7: the service handles the command and then writes op 15, so the state returns to Idle with no data (U64 parity [V command_intf.cc:166-172]).

**Null command.** A PUSH with length 0 gives an empty Data Last; no target is called (U64 parity [V command_intf.cc:175-182]).

**Short commands and stale parameters.**
- The service keeps an 896-byte PS shadow of the U64 command buffer, zero at service start and kept across commands and C64 resets (the U64 BRAM is not cleared either). Each dispatched command overwrites shadow bytes 0 to length − 1. Where a U64 handler writes a terminator at `message[length]` (2.3), our handler writes it into the shadow too.
- Handlers that read fixed parameters beyond the command length read them from the shadow, as the U64 reads its BRAM [V command_intf.cc:55]: READ_DATA bytes 2-3 [V dos.cc:467-476], FILE_SEEK 2-5 [V dos.cc:141-144], DOS LOAD_REU/SAVE_REU 2-9 [V dos.cc:261-263, 294-296], MOUNT/UMOUNT 2 [V dos.cc:325, 379]. Example: xlar54's `uii_read_file()` sends 2 bytes and gets its length from the previous command's bytes; after `uii_open_file(1, "README")` that is `$5201` [V xlar54 ultimate_lib.c:240-248]. A zero-filled copy would make every such read return nothing.
- Names are always cut at the command length; an empty name gives the handler's own "no name" error.
- A one-byte command (target only) answers `21,UNKNOWN COMMAND`. The U64 dispatches a stale byte 1 in that case (class of #836).
- Length checks stay only where the U64 behaviour is harmful or already checked: WRITE_SOCKET length < 3 → `81,INVALID PARAMS` (#836); Control `$08/$09` length < 5 → `81,INVALID PARAMS` (U64 parity, 3.8.3).

### 3.7 Behaviour at turbo

- `$DC00-$DFFF` accesses are φ2 forwards at every speed; the fast port never serves `$DF00-$DFFF` [V hdl/core/c64/multitini_c64_island_map.sv:69-75; D TD §2.13]. The UCI needs no island change.
- The forward retires on the same native edge as `cpu_bus_valid`, so pops happen exactly once per T65 bus cycle at every speed, dummy cycles included [I; bench T-U6 checks it].
- `$DF1C-$DF1F` are outside the island acknowledge class [V island_map.sv:72-75]. Without a UCI IRQ they need not join it.
- Throughput: about 1 byte per µs at any turbo speed; about 65 KB/s for a 1 MHz `LDA $DF1E / STA (zp),Y / INY / BNE` loop [I]. The same pacing as the U64 (2.6).
- The service latency does not depend on the CPU speed. A 64 MHz program spins faster on the status byte; each spin still costs one φ2.
- The UCI read data enters fpga64's `io_data` on the native `c64_clk` path [V multitini_c64_adapter.sv:751-752], not the fast island's half-period path.

### 3.8 Targets and commands

Status text conventions follow the U64: `NN,TEXT` for DOS, Network and Control; one binary byte for SoftIEC; `NNN TEXT` for HTTP [V dos.cc:18; softiec_target.cc:11-21; 1541ultimate software/io/network/http_target.cc]. Identify strings are identical to the U64, because clients compare them.

Two support codes are used for commands we do not offer:
- `21,UNKNOWN COMMAND` for commands the U64 does not implement either.
- `99,FUNCTION NOT IMPLEMENTED` for U64 commands we do not offer. The text already exists in the U64 DOS vocabulary [V dos.cc:18].

#### 3.8.1 Target dispatch

| Target byte & `$0F` | Target | XM5 | Later |
|---|---|---|---|
| `$00`, `$07-$0F` | empty | `$01` → `NO TARGET` / `00,OK`; else `21,UNKNOWN COMMAND` | — |
| `$01`, `$02` | Ultimate DOS, two instances | yes | — |
| `$03` | Network | yes (needs XM4; else 0 interfaces) | listener (3.10.6) |
| `$04` | Control | subset (3.8.3) | GET_HWINFO `$02` (XM7a) |
| `$05` | Software IEC | every command answers binary `$05` (not loaded), as a U64 with IEC Drive Disabled [V softiec_target.cc:55-58] | XM5b |
| `$06` | HTTP | empty target | XM8 |

Bits 6..4 of the target byte are ignored (`$11` = DOS 1) [V command_intf.cc:165].

#### 3.8.2 Ultimate DOS (`$01`, `$02`)

Identify `ULTIMATE-II DOS V1.2`. Each instance has its own current directory, one open file, one directory snapshot and one stream state [V dos.cc:12-15, 32-42].

| Cmd | Name | Ours | Behaviour and fixes |
|---|---|---|---|
| `$01` | IDENTIFY | yes | parity |
| `$02` | OPEN_FILE | yes | Attribute bits as FatFs `FA_*` [V dos.cc:111-124]: `$01` read, `$02` write, `$04` create new, `$08` create always, `$10` open always. An open file is closed first (fix: the U64 opens into the same `file` pointer and leaks the old handle [V dos.cc:113]). Protected path → `98,FUNCTION PROHIBITED` |
| `$03` | CLOSE_FILE | yes | `84,NO FILE TO CLOSE` when none |
| `$04` | READ_DATA | yes | 16-bit length from bytes 2-3, from the shadow when the command is shorter (3.6); 512-byte Data More blocks as on the U64 [V dos.cc:784-803]; empty status while streaming; a read error ends the stream and reports the file-system text in the status (fix: the U64 builds the text and drops it [V dos.cc:795-800]) |
| `$05` | WRITE_DATA | yes | 2 ignored bytes then data, ≤ 892 bytes per command (U64 891) |
| `$06` | FILE_SEEK | yes | 32-bit little-endian |
| `$07` | FILE_INFO | yes | `size32 date16 time16 ext[3] attrib name`, 12 + name length [V dos.cc:154-182]; FAT date and time from FatFs |
| `$08` | FILE_STAT | yes | same structure; the name is NUL-terminated in our copy (fix [V dos.cc:183-187]) |
| `$09` | DELETE_FILE | yes | protected → `98,FUNCTION PROHIBITED` |
| `$0A` | RENAME_FILE | yes | both names checked for protection |
| `$0B` | COPY_FILE | yes | Fix: the destination may be a file name or an existing directory (then `dir/<source name>`). Never overwrites (`FILE EXISTS`). The U64 always treats it as a directory and the copy fails [V dos.cc:238-253; R C64U COPY_FILE report]. Runs as a bounded state machine, 4 KB per pass |
| `$11` | CHANGE_DIR | yes | `..`, `/`, absolute and relative paths (3.9). Into a disk image: `83,NO SUCH DIRECTORY` (the U64 can enter images) |
| `$12` | GET_PATH | yes | `/` for the root, `/a/b/` below it |
| `$13` | OPEN_DIR | yes | Snapshot of the current directory into a per-instance arena (64 KB, ≤ 4096 entries). Empty → `01,DIRECTORY EMPTY`; unreadable → `86,CAN'T READ DIRECTORY` |
| `$14` | READ_DIR | yes | One entry per Data More block: attribute byte + long name [V dos.cc:804-822]. Attribute = `$10` directory, `$20` file. `*.mtj` journals are not listed. Empty listing → empty Data Last with `01,DIRECTORY EMPTY` (fix: the U64 returns `87,INTERNAL ERROR` [V dos.cc:804-809]). More than 4096 entries → last block `02,REQUEST TRUNCATED` |
| `$15` | COPY_UI_PATH | `99` | parity (deprecated on the U64 [V dos.cc:413-416]) |
| `$16` | CREATE_DIR | yes | |
| `$17` | COPY_HOME_PATH | yes | Home = the folder of the title launched last from the menu in this session, else the root (**USER** P44). Reports the real CD result (fix #725: the U64 always says `00,OK`) |
| `$21` | LOAD_REU | yes | Reads from the open file into REU memory: address masked to 24 bits, length to 25 bits, truncated at 16 MB with `02,REQUEST TRUNCATED` [V dos.cc:254-286]. Reply text `$%6x BYTES LOADED TO REU $%6x` (35 bytes) as on the U64. Coherency in 3.8.6 |
| `$22` | SAVE_REU | yes | Same, `... BYTES SAVED FROM REU ...` (36 bytes) |
| `$23` | MOUNT_DISK | yes | Device 8 or 0 → drive 8, exactly like the menu mount (no reset, name remembered). `.d64`, `.g64` only; `.d71`, `.d81`, `.g71` → `91,INCOMPATIBLE IMAGE`; other names → `89,NOT A DISK IMAGE`; missing → `82,FILE NOT FOUND`; any other device → `90,DRIVE NOT PRESENT` [V dos.cc:321-366]. Runs as a posted supervisor action (plan §7.3); the reply is validated when it completes (4.5) |
| `$24` | UMOUNT_DISK | yes | Drive 8 eject, like the menu; posted supervisor action (4.5) |
| `$25` | SWAP_DISK | `99` | no swap list |
| `$26` | GET_TIME | yes | Byte 2 absent or 0: `yyyy/mm/dd hh:mm:ss` (19); 1: `WWW yyyy/mm/dd hh:mm:ss` (23); other → `21,UNKNOWN COMMAND` (parity [V dos.cc:499-525]). From the board RTC, refreshed each second [V main.c `service_fatfs_time()`, `refresh_fatfs_time_from_rtc()`] (fix #363: stale time). The string is always returned, as on the U64. With the RTC invalid it carries the FatFs fallback time, 2010-01-01 00:00:00 [V firmware/include/multitini_fatfs_time.h:15; firmware/common/fatfs_time.c:18, 81], and the status is `88,NO INFORMATION AVAILABLE`; with a valid RTC `00,OK`. GUI64 uses the string without reading the status [V GUI64 lib:179-192]; the GEOS driver checks the status first and keeps its clock on `88` [V GEOS ultimateRTC:230-262] |
| `$27` | SET_TIME | yes | Exactly 8 bytes, else the 2-digit length as data and `21,UNKNOWN COMMAND` (parity [V dos.cc:526-537]). Bytes 2-7: year − 1900, month, day, hour, minute, second [V dos.cc:539-545]. Accepted: year byte 100-199 (2000-2099, the range our PCF8563 encoder takes [V firmware/common/clock.c:19, 65]), month 1-12, a valid day for the month, hour 0-23, minute and second 0-59. Otherwise `98,FUNCTION PROHIBITED`, clock unchanged (the U64 writes any value [V dos.cc:539-562]). Writes through `set_board_clock()` [V main.c]. Reply: the 23-byte `WWW yyyy/mm/dd hh:mm:ss` read back from the clock, `00,OK` (parity [V dos.cc:563-570]). UBoot64's NTP sync uses it [D UBoot64 README:207-220]. **USER** P45 |
| `$41`, `$42` | GEOS RAM disk | `99` | GEOS MegaPatch helpers; later if asked |
| `$F0` | ECHO | yes | Returns the whole command, up to 896 bytes |
| other | | `21,UNKNOWN COMMAND` | |

Fixed parameters beyond a short command come from the shadow (3.6). Paths follow 3.9, wildcards included.

Raw file-system errors are the U64 texts, mapped from the FatFs result [V 1541ultimate software/filesystem/file_system.cc:28-68]: `FILE DOESN'T EXIST`, `PATH DOESN'T EXIST`, `INVALID NAME`, `ACCESS DENIED`, `FILE EXISTS`, `WRITE PROTECTED`, `DISK IS FULL`, `DIRECTORY NOT EMPTY`, `DEVICE NOT READY` (also while the SD card is exported over USB). The global managed-file limit gives `87,INTERNAL ERROR`.

#### 3.8.3 Control (`$04`)

Identify `CONTROL TARGET V1.1` [V 1541ultimate software/io/command_interface/control_target.cc:26].

| Cmd | Name | Ours | Behaviour |
|---|---|---|---|
| `$01` | IDENTIFY | yes | parity |
| `$02` | READ_RTC | `21` | parity (defined, not implemented upstream) |
| `$03` | FINISH_CAPTURE | `99` | no tape |
| `$05` | FREEZE | yes | Posts a supervisor action that opens the Multitini menu, which pauses the C64 (4.5). The reply (empty, empty status) is validated when the menu closes (**USER** P51). DolphinDOS 2 uses it on F7 [V DD2:2570-2589] |
| `$06` | REBOOT | yes | Posted supervisor action; definition below |
| `$08` | LOAD_REU | yes | Length < 5 → `81,INVALID PARAMS`, empty reply (parity [V control_target.cc:344-370]). Loads the REU image last loaded from the menu in this session; overlay preload keys come later (**USER** P50). No source or unreadable → `85,REU FILE CANNOT BE OPENED`; REU off → `84,REU NOT ENABLED`. Reply = 4-byte little-endian result + upper-case log text (parity [V control_target.cc:344-378]). Coherency in 3.8.6 |
| `$09` | SAVE_REU | yes | Length < 5 → `81,INVALID PARAMS`, empty reply (parity). Writes the REU back to that same image; the old file is renamed `.bu` first (U64 behaviour); offset beyond size → `86,REU OFFSET > SIZE. NOT SAVED` |
| `$0F` | U64_SAVEMEM | `99` | A 64 KB RAM dump to a file. Not offered: dumps stay UART-only debug features (standing rule) |
| `$11` | DECODE_TRACK | `99` | later if a client needs it |
| `$12` | ENCODE_TRACK | `21` | parity |
| `$20` | EASYFLASH | `99` | cartridge slots are out of scope (an EasyFlash cartridge suspends the UCI, 3.1) |
| `$28` | GET_HWINFO | yes | Sub-commands below |
| `$29` | GET_DRVINFO | yes | Count + `{type, address, power}`: drive 8 `{$00, 8, 1}`; drive 9 when enabled `{$0F, 9, 1}` (the software IEC type [D 1541u-documentation uci/control_target.rst:194-223]); no printer |
| `$30` | ENABLE_DRIVE_A | yes | `00,OK` (drive 8 is always on) |
| `$31` | DISABLE_DRIVE_A | `99` | we have no drive 8 power control |
| `$32`, `$33` | drive B on/off | yes | `00,OK`, no action: there is no drive B (U64 parity: it answers OK when drive B is absent [V control_target.cc:162-177]). UBoot64's demo mode switches off every drive but 8 [D UBoot64 README:111] |
| `$34` | DRIVE_A_POWER | yes | `on ` |
| `$35` | DRIVE_B_POWER | yes | `off` |
| `$40` | GET_RAMDISKINFO | `99` | GEOS |
| `$50` | LOAD_CONFIG | yes | 3.8.4 |
| `$51-$54` | palette | `99` | U64 only |
| other | | `21,UNKNOWN COMMAND` | |

**REBOOT (`$04 $06`), one definition (U16).** The plan states it once (plan §7.3); docs/C64_TITLE_CONFIG.md §3.10 uses the same text.
- It is a posted supervisor action (4.5). The action pulses the C64 reset through `set_core_reset_request(true)`, then `set_core_reset_request(false)`, as Options → Reset does [V main.c `handle_menu_action()`, `MT_MENU_ACTION_RESET_CORE` branch]. Unlike Options → Reset, it does **not** call `restore_selection_settings()`.
- A loaded cartridge stays loaded: the content loader resets only on a PL reset [V multitini_c64_adapter.sv:426-427; hdl/core/c64/multitini_c64_content_loader.sv:420]. The cartridge restarts with the C64 reset, which clears its bank state [V cartridge.sv:161-168]. Its `.cfg` is not read again, so keys merged by `$50` stay.
- The title overlay, the title session and the expansion and turbo registers are unchanged. No apply event is raised.
- Drive 8 keeps the image it holds now (the stored one or a later swap) through the reset-resume path of `set_core_reset_request()` [V main.c `set_core_reset_request()`; firmware/platform/xilinx/c64_d64_service.c:790-800]. The stored drive 8 name is not re-mounted.
- This is the U64's REBOOT: `start_cartridge(NULL)` restarts the C64 with the configured cartridge and leaves the settings alone [V control_target.cc:133-137; 1541ultimate software/io/c64/c64_subsys.cc:279-285].
- No reply can arrive (the reset clears the block), as on the U64.

**GET_HWINFO (`$04 $28`) (U17, P37).** The SID content comes from `mt_c64_sid_hwinfo()` of docs/C64_MULTI_SID.md §4.6.
- No sub-command, or sub `$00`: the product name, `MULTITINI ONE` (**USER** P47).
- Sub `$01`, U64 shape:
  - count = 2 + emulated SID sockets (0 unless P34 adopts socket emulation), at most 4. Frame 1 = SID 1, frame 2 = SID 2, frames 3-4 = emulated sockets in U64 order. The U64 always sends its two internal SIDs first [V control_target.cc:418-439].
  - Frame = address low, address high, secondary low (0), secondary high (0), type.
  - Address = the SID's configured base. The U64 reports the mirror-cleared base, which is wrong when every SID shares a set address line [V control_target.cc:425-439]; this fix is kept. With the SID map active, an unmapped SID 1 or 2 reports `$0000`, type `$80`.
  - Type = `$80 | 2` when the SID decodes exactly one 32-byte slot, `$80 | 3` when it decodes more (mirrors) [V control_target.cc:431, 439]; emulated sockets `$80 | 4` / `$80 | 5` [V :452, 467].
  - Legacy modes (XM5): mode 0 → `$D400`/`$83`, `$D400`/`$83`; mode 2 → `$D400`/`$83`, `$D420`/`$83`; mode 3 (default) → `$D400`/`$83`, `$D500`/`$83`. Always 2 frames, 11 bytes. XM5 ships this legacy part of `mt_c64_sid_hwinfo()` (step 12 in section 9).
- Sub `$02`, Multitini (XM7a): every mapped SID and emulated socket, in SID index order, same frame format, at most 1 + 10 × 5 = 51 bytes. Before XM7a it answers `81,INVALID PARAMS`, as the U64 does for any sub other than 0 and 1 [V control_target.cc:380-394 and the final `else`].
- Any other sub → `81,INVALID PARAMS`.
- Reason: SID Detector II, the only known client, sizes its buffer for 4 frames, reads frames 1-2 at fixed offsets and lists more than 4 frames as untested [D SIDD UCI.md:333-345, 369-371]. A `$01` reply with every SID, or with one frame in mode 0, would match no U64 reply. `$02` gives the full list without that risk; existing clients never send it, and a U64 rejects it.

#### 3.8.4 LOAD_CONFIG (`$04 $50`) and the title overlay

docs/C64_TITLE_CONFIG.md §3.15 owns the parser, the allow-list, the candidate and the overlay; the plan states the shared rules (plan §8.5). This section gives what the UCI side adds or relies on.

- **Path.** Bytes 2 and later give the path. With none, the file is `/temp/uci_config.cfg` [V control_target.cc:561-566], which 3.9 rule 5 maps to `<package directory>/uci_temp/uci_config.cfg`. Every entry of that folder, this file included, is deleted when the C64 core starts. A path argument is resolved by the DOS rules of 3.9: absolute from the UCI root, relative from DOS 1's current directory.
- **Size.** At most 16 KiB, read into the separate `$50` buffer of docs/C64_TITLE_CONFIG.md §3.15, across service passes (≤ 4 KB per pass).
- **All or nothing.** If any line is MALFORMED, BAD_VALUE or BLOCKED, nothing is applied and the status is `89,CONFIG FILE HAD ERRORS`. UNKNOWN_ITEM, UNAVAILABLE and NOT_APPLICABLE are listed in the log but are not errors.
- **Merge on success.** The file's groups replace the active overlay's values for those groups; every other overlay key stays. For the SID group (XM7) the merge is per field (plan §8.3; docs/C64_MULTI_SID.md §3.10), so a second file that sets only "Pan UltiSID 1" keeps the addresses of the first. With no overlay, a new one is created, attached to the title session if one exists. Replacing would drop the title's REU, UA or turbo under the running program. The U64 also touches only the items the file contains [V control_target.cc:553-561].
- **Cartridge rule (plan §6.4 B9).** While a cartridge is attached (the UCI is then active only with a generic one, 3.1), `RAM Expansion Unit=Enabled` and `Map Ultimate Audio $DF20-DFFF=Enabled` are BLOCKED. So such a file applies nothing and answers `89,CONFIG FILE HAD ERRORS`. The menu refuses the same change ("Eject the cartridge before enabling ...") [V main.c `change_c64_expansion()`]. Turbo keys under a cartridge are ADJUSTED, not errors (docs/C64_TITLE_CONFIG.md §3.7).
- **Live apply under HOLD.** A `$50` change of the REU or UA enable (`C64_EXPANSION_CONFIG` bits 3-4), and from XM7 a SID-map change, is written under `C64_UCI_CONTROL.HOLD` (3.13), with the same steps as 3.8.6 steps 4-5 and 7:
  1. Set HOLD.
  2. Wait for `C64_CPU_STATUS` PAUSE [V main.c `c64_debug_paused()`].
  3. Check `C64_REU_HOST` IDLE [V firmware/include/multitini_regs.h:211]. If it is not idle, release, wait for idle and try again; at most 1 s in total. If no try succeeds, nothing is applied, the overlay stays as it was, and the status is `87,INTERNAL ERROR`, as for the REU invalidate (3.8.6).
  4. Commit the merged overlay and write `C64_EXPANSION_CONFIG`. For the SID map, docs/C64_MULTI_SID.md §3.10 gives the order inside the hold (table words, COMMIT, accepted sequence, then the one `C64_EXPANSION_CONFIG` write).
  5. Release HOLD, then validate the reply.
  Turbo and SuperCPU changes write their registers directly and need no hold. The hold replaces "a short core pause, as menu changes do today": the only firmware pause is the dense menu with its "please wait" screen [V main.c `c64_debug_pause()`], which must not appear under a running program.
- Fix over the U64: there the reader writes the good lines into its stores even when the parse fails, and they take effect at the next reset [V control_target.cc:579-596]. Our candidate is separate from the overlay, so a failed load leaves nothing behind.
- `Command Interface=Disabled` from this source is INFO "ignored when loaded through the Command Interface". Turning the UCI off mid-transaction would cut the reply.
- Reply: the log, at most 511 bytes (U64 size [V control_target.cc:600-610]). Status `00,OK`, `88,CANNOT OPEN CONFIG FILE` (missing, over 16 KiB or not text), `89,CONFIG FILE HAD ERRORS`, or `87,INTERNAL ERROR` (hold not reached).
- An overlay change of Turbo Control, CPU Speed or Badline Timing is an apply event only if the value changes (plan §8.2 rule 10).
- The overlay from `$50` ends with the title session, like any overlay. UCI REBOOT does not end it (3.8.3; plan §8.4).

#### 3.8.5 Software IEC (`$05`, XM5b)

Drive 9 (dev7) is a PS-backed DOS device with a channel API [V firmware/include/multitini_c64_iec_dos.h:65-80]. It is the natural model for the U64 SoftIEC target. The target serves the same `mt_iec_dos_t` instance, serialised on CPU0.

- Until XM5b, and whenever drive 9 is unavailable: every command returns binary `$05`, and `$DF1B` = 31.
- With XM5b: `$DF1B` = 9 (plan §4.1: only when drive 9 is enabled **and** the SoftIEC target exists). Identify `SOFTWARE IEC TARGET V1.0` with `00,OK`; other statuses are binary [V softiec_target.cc:11-21].
- Names go to `mt_iec_dos` unchanged, in CBM DOS syntax (`0:NAME`, `@0:NAME`, `$`, `*`), as the U64 hands them to its IEC drive [V uci.s:467-477; softiec_target.cc:149-153, 236-239]. The path rules of 3.9 do not apply to them; drive 9's own name rules do. Only the GET_FATNAME output follows 3.9.

| Cmd | Name | Mapping |
|---|---|---|
| `$01` | IDENTIFY | string |
| `$10` | LOAD_SU | `mt_iec_dos_open(channel 0, name)`; reply = 2-byte start address; name at offset 8 (per source, #689) [V softiec_target.cc:135-161] |
| `$11` | LOAD_EX | Streams the rest of the file into C64 RAM through load kind 0x17 (4.1.5) under an HDL machine pause; status `$00 endL endH`, or `$00`/`$80` for verify (verify reads RAM, 4.1.5) |
| `$12` | SAVE | Reads C64 RAM start..end through the UCI RAM read path and writes channel 1 |
| `$13` | OPEN | `mt_iec_dos_open(SA, name)`; empty status |
| `$14` | CLOSE | `mt_iec_dos_close(SA)`; `00,OK` |
| `$15` | CHKIN | Streams the channel: 32 bytes, then 256 per Data More block (U64 sizes) via `mt_iec_dos_peek/consume`. On ABORT the target keeps the bytes the C64 did not consume (`C64_UCI_STATUS2[9:0]`) in a 256-byte per-channel push-back buffer, served first next time |
| `$16` | CHKOUT | `mt_iec_dos_write(SA, data)`; `00,OK` |
| `$20`, `$21` | partitions | `$08` invalid partition (drive 9 has one root) |
| `$22` | GET_FATNAME | path of the channel's file relative to the UCI root |
| `$23` | GET_IECNAME | drive 9's alias for a host name (`mt_iec_entry_t.alias`) [V multitini_c64_iec_dos.h:33-40] |

The FREEZE (DMA) bit is honoured from XM5b: an accepted PUSH with bit 7 holds the machine until the reply is validated (4.1.5). TRIGGER (`$FF00`) is latched and reported but never acts; no known client uses it [V uci.s:478-481 uses `$01`; I].

#### 3.8.6 REU memory commands and cache coherency

Facts:
- REU memory is DDR at `0x25000000`, 16 MiB [V firmware/include/multitini_regs.h:351-352].
- The REU store has four read lines and four merging posted writes; an invalidate waits for posted writes and then drops the lines [V hdl/core/c64/multitini_c64_reu_cache.sv:3-10, 170-171].
- While an invalidate is pending, the store refuses every request (`request_ready` needs `!invalidate_pending`) and starts no fill; the invalidate completes only at IDLE with no posted write and **no request valid** [V multitini_c64_reu_cache.sv:117-118, 170-172].
- The REU FSM's `run` has no pause term, and it holds `memory_valid` until the request is accepted [V hdl/core/c64/multitini_c64_reu.sv:77, 98-101].
- So an REU DMA that requests memory while an invalidate waits (for a line fill or a posted-write drain) blocks both forever: the C64 freezes in DMA, the firmware's two 100 ms tries expire, and `reload_required` stays set until the core is reloaded [V firmware/common/c64_reu_host.c:59-69] [I from the cited RTL].
- Only a hard PL reset clears the store; a C64 reset does not [V multitini_c64_reu_cache.sv:9-10].
- The menu load path pauses the C64, holds UA, waits for REU idle, copies 4 KB chunks, drains, and invalidates (two tries at most) [V c64_reu_host.c:31-73]. It is safe because the paused CPU cannot start a DMA. Its pause is the dense menu: `show_handoff_progress("C64 memory operation; please wait")` [V main.c `c64_debug_pause()`]. The UA hold removes UA from the C64's I/O map while it is set [V multitini_c64_adapter.sv:611]. `:reudump` also pauses first [V firmware/common/c64_debug.c:171-173].

Design (U12, plan §7.4):
- The copy does **not** use the menu pause and does **not** hold UA. A pause would show a "please wait" screen in the middle of a running program, and the UA hold would make `$DF20-$DFFF` vanish under it. Loading the next part into the REU while the current part plays is the main LOAD_REU use.
- LOAD sequence (DOS `$21`, Control `$08`):
  1. Wait until `C64_REU_HOST` IDLE (≤ 1 s, else `87,INTERNAL ERROR`).
  2. Copy in 4 KB steps, one or more per pass.
  3. Drain PS writes (DSB, PL310 sync, DSB, as `drain_writes` [V firmware/include/multitini_c64_reu_host.h:18]).
  4. Set `C64_UCI_CONTROL.HOLD` (bit 10, in XM5): a whole-machine HDL pause through the existing pause path, with no menu [V multitini_c64_adapter.sv:731-737]. The CPU, VIC timing, UA and the drive stop.
  5. Wait for paused, then check REU IDLE. If the REU is not idle, release the hold, wait for idle and try again (bounded, 1 s in total). If no try succeeds, the reply is `87,INTERNAL ERROR` and `reload_required` is set, as for a failed invalidate, because stale lines may remain.
  6. Write the invalidate counter and wait for done (two tries of 100 ms, as the menu path).
  7. Release the hold; then validate the reply.
- With the CPU held and the REU idle, no REU request can arrive during the invalidate: a DMA starts only from a CPU write to the REU command register or to `$FF00` [I]. The hold covers one invalidate after at most four posted writes, expected well under 1 ms [I; O measure, 8.3]. Audio stops for that time.
- Alternative, if the hold cannot reach REU idle under a running program (E-X5): change `multitini_c64_reu_cache.sv` so a pending invalidate completes at IDLE once `count == 0`, even with a request waiting. That request has not been served, so dropping the lines first stays coherent. It changes the REU store, so it needs a REU-cache bench case (invalidate during an active DMA) and G-STK and G-REU0 again. Not chosen first, because the hold leaves the stock REU RTL unchanged.
- SAVE (DOS `$22`, Control `$09`): wait for REU idle, read, no invalidate, no hold. A program that writes the REU during the save gets a mixed image, as on the U64, which reads REU memory with no lock [V dos.cc:254-286]. `:reudump` is a paused debug dump, not a precedent here.
- During the copy the C64 program is polling `$DF1C`. If its own IRQ code starts an REU DMA or plays UA samples from the region being loaded, it sees old or mixed data until the transfer ends. That is the program's race, as on the U64 [V dos.cc:254-286]. It cannot deadlock: no invalidate is pending during the copy.
- Cancel: an ABORT, a C64 reset or a quiesce that stops a LOAD after any byte was written still runs steps 3-7, through the step API's `finish()` (4.4). A guest reset does not clear cache lines [V multitini_c64_reu_cache.sv:9-10], and the menu path also invalidates after a partial load [V c64_reu_host.c:59-60].
- If the invalidate does not complete, the existing `reload_required` state is set and the reply is `87,INTERNAL ERROR` [V c64_reu_host.c:67-69].
- This needs a step-wise variant of `mt_c64_reu_load()` with pause, UA hold and invalidate hold as options (4.4).

### 3.9 File root and path rules (DOS targets, LOAD_CONFIG, GET_FATNAME output)

SoftIEC names are not paths; they follow 3.8.5. The plan states the shared root, alias and protection rules (plan §7.4); this section gives them in full.

Visible root (U10, P42): the active C64 package's `content_root`, default `0:/multitini/cores/c64/media`, the root drives 8 and 9 already use (plan §2.1).
- A package root must lie below `0:/multitini/cores/` [V firmware/common/core_package.c:74-87]. So `settings.bin`, BOOT images and `0:/multitini/*.bin` are out of reach by construction.
- Confinement to the C64 folder depends on the package `core.cfg`: the check also accepts another core's folder [V core_package.c:74-87]. The UCI service therefore also requires the root to lie below the active package directory; otherwise every file command answers `DEVICE NOT READY` [I].
- Alternatives: the whole SD card (exposes `0:/multitini/*.bin` and BOOT images; rejected); a dedicated `c64uci` folder (files the user sees in the Program and Drive 8 browsers would be invisible to UCI launchers such as UBoot64; rejected).

Rules:
1. A name is the command bytes up to the command end or the first `$00`.
2. Bytes `$20-$7E` pass. PETSCII shifted letters `$C1-$DA` become `A-Z`. Any other byte → `INVALID NAME`. Only ASCII reaches FatFs [I].
3. `/` is the only separator. `\` and `:` → `INVALID NAME`.
4. A leading `/` starts at the UCI root; otherwise at the instance's current directory.
5. First-component aliases, case-insensitive (**USER** P43): `SD`, `Usb0`, `Usb1`, `Usb2` → the root; `Temp` → `<package directory>/uci_temp` (default `0:/multitini/cores/c64/uci_temp`), outside the media listing, created on first use. Every entry in it is deleted when the C64 core starts (bounded, across service passes) [I]. It is documented as volatile. Reason: U64 software hard-codes volume names such as `/Usb0/` and `/temp/` [V control_target.cc:523, 566]. On the U64 `/Temp` is a RAM disk that is empty at every boot (2.3), and clients use it as scratch [D barryw README:329-331]. A folder inside the media root would keep scratch files and show them in the Program and Drive 8 browsers. The `$50` default file lives here (3.8.4).
6. `.` is ignored. `..` goes up one level and stops at the root.
7. At most 255 bytes below the root; components at most 255 bytes.
8. Protected for write, create, append, rename, delete and open-for-write, through the one shared callback `c64_iec_path_protected()` that drive 9 and both UCI DOS targets use [V main.c `c64_iec_path_protected()`]. The answer is `98,FUNCTION PROHIBITED`. Protected:
   - the mounted drive 8 image and its journal (`c64_d64_service_path_protected()`);
   - files drive 9 holds open for writing; every `*.mtj`;
   - if the root is the package directory itself, `core.cfg`, `roms/` and bitstream files;
   - real title sidecars (**USER** P21): a `.cfg` or `.usr` whose stem matches, case-insensitively, a `.prg`, `.crt` or `.d64` in the same folder, checked at each of those operations. Other `.cfg` files stay writable: UBoot64 and DMBoot create `DMBCFG.CFG` and `DMBSLT.CFG` in the root through `/usb*/` [D UBoot64 README:88], and `$50` clients write `/temp/uci_config.cfg`. The sidecar rule lands with XM2 for drive 9 and serves the UCI DOS targets from XM5. Drive 9 asks the callback on every open, so the callback gains an access-kind argument (plan §7.4; docs/C64_TITLE_CONFIG.md §3.16). A change of the callback re-runs G-IEC9.
9. The UCI uses at most 4 of the 16 global managed files [V firmware/platform/xilinx/sd_storage.h:93-97]: one open file per DOS instance, plus two during COPY_FILE. Directory listings are snapshots, so no directory handle stays open.
10. USB SD export: the service closes its files first; while exported, file commands answer `DEVICE NOT READY`.
11. Wildcards. Every path component is a U64 pattern: `*` matches any run, `?` one character, case-insensitive (2.3) [V pattern.cc:84-128]. A component with a wildcard is matched against the directory entries in FatFs order (`f_readdir`), and the first match wins, as on the U64 [V file_system.cc:79-108]. At the root, the alias names `SD`, `Usb0`, `Usb1`, `Usb2` and `Temp` are tried first, in that order, then the real entries. So `/usb*/DMBCFG.CFG` reaches the root, as UBoot64 and DMBoot expect [D UBoot64 README:88]. A component without wildcards goes straight to FatFs. A create or rename target whose last component has no match is used literally; a wildcard in it gives `INVALID NAME`, because FatFs rejects `*` and `?` [I]. A scan of a large directory runs across passes, like a directory snapshot, within the 4096-entry limit.

### 3.10 Network (`$03`) on the board's W5100S

The plan makes the network shared infrastructure (plan §9, XM4): the PL bridge (`NET_*` registers 0x02C-0x038, plan §3.4), the portable driver `firmware/common/net_w5100s.c` ported from Appletini's `uthernet2_control.c`, and the platform service `net_service.c`. The UCI Network target is a client of it. The C64 never reaches the W5100S directly.

#### 3.10.1 What the target uses from the network service (plan §9.4)

| Function | Use |
|---|---|
| `mt_net_interface(&info)` | present, mode (Off/DHCP/Static), link (PHYSR bit 0 [V appletini-one ps_sources/frontend/uthernet2_control.c:16-20, 314-325]), address valid, MAC, IP, mask, gateway |
| `mt_net_resolve_start(name)`, `mt_net_resolve_poll(&ip)` | non-blocking DNS through socket 0 (3.10.7 (f)) |
| `mt_net_open(owner, type, ip, port, &h)`, `mt_net_poll(h)` | non-blocking TCP connect / UDP open on S1-S3 |
| `mt_net_listen(owner, port, &h)` | TCP LISTEN on S1-S3 |
| `mt_net_send(h, data, n, &queued)`, `mt_net_recv(h, buf, cap, &n, &info)` | ring copies; `info` carries the UDP peer, datagram size, truncation, and the close cause (peer FIN, RST, TIMEOUT, service close) |
| `mt_net_close(h)`, `mt_net_close_owner(owner)` | graceful close; close every socket of one owner |
| `mt_net_set_runtime_address(ip, mask, gw)`, `mt_net_clear_runtime_address()` | SET_IPADDR |

Owner `MT_NET_OWNER_UCI` lets a C64 reset close only the UCI's sockets.

#### 3.10.2 Socket budget and ids (plan §9.5)

- Hardware: 4 sockets, 16 KB buffer memory, TCP/UDP/IPRAW/MACRAW, no DNS, no TLS [D datasheet].
- S0 is the system socket (3.10.7 (d)). S1-S3 serve the UCI, the listener included.
- Buffers: 2 KB RX and 2 KB TX per socket (N3). The largest UCI read (1472 bytes, 3.10.3) fits.
- UCI socket ids are logical, 1-15, handed out in rotation and never 0. Reason: xlar54's `uii_connect()` returns `uii_data[0]`, which is 0 after a failed open with an empty reply [V xlar54 ultimate_lib.c:551, 630-659]; a real socket 0 would look like a failure [I]. The U64 tracks up to 16 [V 1541ultimate software/io/network/network_target.h:28-31].
- A fourth concurrent open answers `85,ERROR OPENING SOCKET` [V network_target.cc:23]. A live socket is never evicted. A socket the C64 already closed but that is still lingering in FIN/TIME-WAIT is reclaimed at once (CLOSE) when an open needs it. A socket that closed on its own (3.10.3) frees its hardware socket at once; only its logical id waits for the one closed report.

#### 3.10.3 Commands and their W5100S mapping

Identify `ULTIMATE-II NETWORK INTERFACE V1.0` [V network_target.cc:18]. W5100S socket offsets, command, interrupt and status codes as used below are verified in Appletini [V appletini-one ps_sources/frontend/uthernet2_control.h:10-44]. `CONNECT` `$04`, UDP mode `$02`, `SOCK_UDP` `$22`, `Sn_DPORT` `$10`, `SOCK_SYNSENT` `$15`, RTR `$0017` and RCR `$0019` are datasheet values [D; O E-X3].

| Cmd | Name | Parameters | Ours | W5100S mapping and replies |
|---|---|---|---|---|
| `$01` | IDENTIFY | — | yes | string |
| `$02` | GET_INTERFACE_COUNT | — | yes | 1 byte: 1 if `NET_CAPS` valid and mode ≠ Off, else 0 |
| `$03` | SET_INTERFACE | — | `21` | parity (commented out upstream [V network_target.cc:67-79]) |
| `$04` | GET_NETADDR | if (length 3) | yes | 6-byte MAC (SHAR `$0009`). Length ≠ 3 → `81,INVALID PARAMS`; if ≥ count → `82,PARAMETER(S) OUT OF RANGE` [V network_target.cc:101-122] |
| `$05` | GET_IPADDR | if | yes | 12 bytes IP, mask, gateway (SIPR `$000F`, SUBR `$0005`, GAR `$0001` [V uthernet2_control.c:11-17]); zeros while DHCP has no address |
| `$06` | SET_IPADDR | if + 12 bytes (length 15) | yes | Runtime only: writes SIPR/SUBR/GAR and suspends DHCP renew. The address stays until the Network setting changes, a core switch or a board reset; a C64 reset keeps it (**USER** P46). The U64 also changes the address at run time only and keeps it until the Ultimate reboots [V 1541ultimate software/io/network/network_interface.cc:463-472]. Every PRG load is a C64 reset, so a utility that sets the address and then starts a network program keeps it |
| `$07` | OPEN_TCP | port LE, host | yes | Host = dotted IPv4 or a DNS name. No interface, no link or no address → `83,INTERFACE NOT AVAILABLE`. DNS failure → `84,UNRESOLVED HOST`. No free socket → `85,ERROR OPENING SOCKET`. Then: Sn_MR = TCP (`$01`), Sn_PORT = ephemeral 49152-65535, OPEN (`$01`) → SR INIT (`$13`), Sn_DIPR/Sn_DPORT, CONNECT, poll each pass: ESTABLISHED (`$17`) → reply 1-byte id, `00,OK`; SR CLOSED with IR TIMEOUT (`$08`) → `11,ERROR ON CONNECT: 116` (ETIMEDOUT); refused (RST) → `11,ERROR ON CONNECT: 104` (lwIP maps ERR_RST to ECONNRESET) [O: check on a U64; how the W5100S reports a RST is E-X3]; firmware deadline 10 s → close, `11,ERROR ON CONNECT: 116`. A W5100S command that does not complete → `86,INTERNAL ERROR`. Errno numbers are newlib's, because the U64 builds lwIP without `LWIP_PROVIDE_ERRNO` [V network_target.cc:222; 1541ultimate software/network/config/arch/cc.h:41; the same numbering in software/FreeRTOS/Source/include/projdefs.h:101] |
| `$08` | OPEN_UDP | port LE, host | yes | Sn_MR = UDP, ephemeral local port, OPEN → SOCK_UDP; peer IP and port stored. "Connected" UDP as on the U64 [V network_target.cc:213-219]: datagrams from another peer are dropped. Reply id at once |
| `$09` | CLOSE_SOCKET | s (length 3) | yes | Unknown id → `12,ERROR ON CLOSE: 9` (EBADF, parity). TCP: DISCON (`$08`), background CLOSE (`$10`) on SR CLOSED or after 1 s. UDP: CLOSE. Reply `00,OK` at once |
| `$10` | READ_SOCKET | s, len LE (length 5) | yes | len > 1472 → `82,PARAMETER(S) OUT OF RANGE` [V network_target.cc:260-264; network_target.h:34-38]. Waits up to 40 ms for data **as a deferred reply** (the loop keeps running) [V network_target.cc:247-250]. TCP: copy min(len, Sn_RX_RSR) from the RX ring at Sn_RX_RD, advance, RECV (`$40`). Sn_RX_RSR is read until two reads agree [V appletini-one uthernet2_control.c:248-267]. UDP: one datagram; the 8-byte W5100S header (peer IP, port, size) is parsed [D; O E-X3]; a longer datagram is cut and the rest discarded with `04,DATAGRAM TRUNCATED: n` [V network_target.cc:311-322]. Reply: 2-byte total + payload; first block ≤ 893 payload, then ≤ 895 (U64 sizes [V network_target.h:40-44]). **Live socket, no data after 40 ms → `FF FF` + `02,NO DATA: 11`** (U13). Closed TCP socket: when a UCI TCP socket has been closed for any reason (peer FIN, RST, retry TIMEOUT, or a close by the net service, 3.10.5) and its RX data is used up, the next READ answers `00 00` + `01,CONNECTION CLOSED BY HOST` once and frees the id. Id never issued or already reported closed → `FF FF` + `02,NO DATA: 9`. Fix: on the U64 a peer FIN gives that report [V network_target.cc:305-310], but a reset or timed-out lwIP socket keeps answering −1 [V network_target.cc:284-328], and xlar54's `uii_tcp_nextchar()` retries forever, so UltimateTerm and u-chat hang when a BBS drops the line [V xlar54 ultimate_lib.c:825-838] |
| `$11` | WRITE_SOCKET | s, data | yes | Length < 3 → `81,INVALID PARAMS` (fix #836). Payload ≤ 893 (U64 892, #807). TCP: wait for Sn_TX_FSR ≥ n (≤ 2 s, deferred), copy at Sn_TX_WR, SEND (`$20`), wait SEND_OK (`$10`); reply 2-byte count, `00,OK`. UDP: Sn_DIPR/Sn_DPORT from the peer, SEND, wait SEND_OK or TIMEOUT (ARP failure → `12,SEND ERROR: 118`, EHOSTUNREACH in newlib numbering; the U64 value is not known, because lwIP queues the packet while ARP resolves [I; O]). Unknown id → `FF FF` + `12,SEND ERROR: 9` |
| `$12-$15` | TCP listener | port / — | 3.10.6 | xlar54 extension, not in upstream firmware |
| other | | | `21,UNKNOWN COMMAND` | `$0A` OPEN_UDP_LOCAL (#1016), `$16` WRITE_SOCKET_CHUNK (#835), GET_RANDOM (#953) are candidates for later [R] |

#### 3.10.4 Timeouts (all deferred; ABORT cancels any of them in the next pass)

| Wait | Limit | Basis |
|---|---|---|
| DNS | 2 s per try, one retry per server, two servers: at most 8 s | plan §9.6 |
| TCP connect | 10 s firmware deadline; W5100S retry registers stay at their defaults [I; O E-X3] | short enough for a C64 user, long enough for a slow Internet host |
| READ_SOCKET | 40 ms | U64 parity [V network_target.cc:247-250] |
| WRITE_SOCKET space | 2 s, then the bytes queued so far are reported | [I] |
| Close linger | 1 s, then CLOSE | [I] |

#### 3.10.5 Lifetime

- C64 reset: `mt_net_close_owner(MT_NET_OWNER_UCI)` (fix #808). The ids are discarded with no closed report: the program that owned them is gone. A runtime SET_IPADDR address and the DHCP lease survive (plan §7.2).
- The net service closes UCI sockets only on lease loss (NAK or expiry), an address change, Network Off, a chip error or a core switch. Each such TCP socket answers its next READ with `00 00` + `01,CONNECTION CLOSED BY HOST` once (3.10.3). A lookup pending at expiry ends with `84,UNRESOLVED HOST` (plan §9.5).
- Link loss: sockets and lease stay. Open TCP sessions are left to the W5100S retransmission timeout and to our 10 s connect and 40 ms read deadlines. A short cable pull that retransmission survives keeps the session, as lwIP does on the U64 [I].
- Network Off in the settings, or an image without the bridge (`NET_CAPS` = 0): GET_INTERFACE_COUNT = 0, GET_IPADDR with if 0 → `82,PARAMETER(S) OUT OF RANGE`, every open → `83,INTERFACE NOT AVAILABLE`. UltimateTerm and similar clients then print the status and stay usable [I; board test B10].

#### 3.10.6 TCP listener (`$12-$15`)

- xlar54's library sends `$12` START port, `$13` STOP, `$14` GET_LISTENER_STATE, `$15` GET_LISTENER_SOCKET, with states 0 not listening, 1 listening, 2 connected, 3 bind error, 4 port in use [V xlar54 ultimate_lib.c:707-770; ultimate_lib.h:76-85]. u-echoserver needs it [R #98]. The server side was PR #98, closed unmerged in 2026-08 [R #98].
- Cheap on the W5100S: one socket in LISTEN (`$02`) → SR LISTEN (`$14`) becomes ESTABLISHED on the first connection; one connection per listener, which is what the PR intended [R #98].
- The exact reply bytes of PR #98 are not in our copies (E-X11). Until confirmed: `$12` → 2-byte LE state; `$13` → 2-byte LE 0; `$14` → 2-byte LE state; `$15` → 1-byte socket id once state = 2.
- Closed on C64 reset. Uses one of S1-S3.
- Recommendation: ship in XM5 if the PR's reply format is confirmed before the XM5 code freeze, else XM8 (**USER** P48).

#### 3.10.7 The XM4 network service: details this design relies on

Plan §9 is the binding XM4 design (registers, pins, socket plan, DHCP and DNS rules, MAC, settings, tests T-NET1-T-NET8, board tests N-B1-N-B7, work steps N1-N7). This subsection adds the details the UCI target relies on and marks this design's proposals, which 8.1 lists.

**(a) Facts.**
- Pins and ports as in 0.2. Every image holds the chip in reset [V multitini_one_top.sv:2329-2334]. `eth_int_n` is false-pathed as a static input [V hdl/board/multitini_timing.xdc:93-96].
- The timing-coverage gate fails on input or output ports with no delay constraint ("(HIGH)") and passes ports covered by a false path ("(MEDIUM)") [V scripts/build_and_export_xsa.tcl:611-632; build/Multitini-070dev7/evidence/c64-pal/reports/timing_coverage_gate.rpt:40-58, the PSRAM inputs and `audio_spdif_out`].
- IOB precedents: `(* IOB = "TRUE" *)` attributes on output ports [V hdl/board/multitini_video_test.sv:53-57] and `hdl/board/multitini_psram_iob.xdc` [V :1-14].
- Appletini `uthernet2_card.sv` at 133 MHz: reset hold 10 ms, ready wait 61 ms, bus setup 2 cycles, strobes 16 cycles, recover 5 cycles [V appletini-one hdl/apple/uthernet2_card.sv:22-28, 70-71]. It uses the W5100S indirect bus mode and reloads the indirect address only when its tracked pointer differs [V :1-20, 101-104, 157-160]; physical address = address & `$7FFF` [V :228-232].
- Bus sequence: address and write data are driven from the first cycle of an operation; CS# and RD#/WR# fall together after the 2 setup cycles; in the last strobe cycle CS#, RD#/WR# and the data output enable all go inactive on the same edge, and read data is sampled on that edge; `eth_a` stays through recovery [V uthernet2_card.sv:729-815].
- Driver: one byte per command, polling ready, then done with a sequence change, with `usleep()` waits [V uthernet2_control.c:120-173]; socket commands poll up to 1000 reads [V :329-350]; DHCP start resets the chip (MR = `$80`) and closes all sockets [V :352-364, 885-916]; the xid seed is a constant [V :79]; 3 attempts, 2 s reply wait [V :32, 43]; default MAC `02:41:50:50:4C:01` [V :93-101].

**(b) Bridge (plan §9.2) and its two proposed refinements.**
- Registers as plan §3.4. `NET_ACCESS[15]` is reserved and must be 0; `NET_STATUS[5]` INT comes through a `cdc_bit_sync` (plan C10).
- IOB enforcement (proposal FI-2): the launch flops of `eth_a`, `eth_rd_n`, `eth_wr_n`, `eth_cs_n`, `eth_rst_n` and the 8 data bits, 8 replicated tristate-enable flops and the 8 input capture flops carry `(* IOB = "TRUE" *)` in the RTL, and the C64 build fails if any `eth_*` port is not driven or captured by an IOB-packed cell. The plan's false paths are sound only with that packing.
- Write-data hold (proposal FI-1): the output enable stays on for the first recover cycle, so write data is held 7.5 ns past the WR# rising edge. Appletini drops the enable on the same edge as WR# [V uthernet2_card.sv:739-744]. The W5100S write hold requirement is E-X3.
- Access cost: 2 + 16 + 5 = 23 cycles = 172.5 ns per bus operation; a random byte adds an address reload (two more operations), about 520 ns. With one AXI write and one or two AXI status reads per byte, a sequential byte costs about 0.4-0.6 µs [I; O measure]. A 1472-byte read is therefore split across passes (≤ 1 KB per pass, plan §7.1).

**(c) Pin constraints (plan §9.3).** The plan's text goes into `hdl/board/multitini_timing.xdc`. Its comment describes the sequence of (a): only `eth_a` is held through recovery; CS# rises with the strobe, and write data is released at the strobe end. With FI-1 write data is held one cycle longer. Proposed comment with FI-1 and FI-2:

```
# W5100S parallel bus, driven by multitini_w5100s_bridge on control_clk (133 MHz).
# No clock leaves the FPGA. Every eth_* pin is launched or captured by an IOB
# flop (checked by the build). eth_a is set 2 cycles (15 ns) before CS# and the
# 16-cycle (120 ns) RD#/WR# strobe fall together, and holds through the 5
# recovery cycles. Write data is driven from the first cycle (135 ns setup to
# WR# rising) and held 1 cycle (7.5 ns) after it. eth_d is captured 15 cycles
# (112.5 ns) into the read strobe. tb_multitini_w5100s_bridge asserts the shape.
set_false_path -to   [get_ports {eth_d[*] eth_a[*] eth_rd_n eth_wr_n eth_cs_n eth_rst_n}]
set_false_path -from [get_ports {eth_d[*]}]
```

The read data must be valid within about 110 ns of RD# falling (E-X3). Appletini runs the same counts on this board [R user].

**(d) Driver and socket 0 (plan §9.4, §9.5).**
- Ops struct `access(addr, write, data, &rdata)`, `now_us()`, `barrier()`; the platform binds it to `NET_ACCESS`/`NET_STATUS`, host tests bind it to a C model of the W5100S. One access waits ≤ 2 µs for READY and ≤ 2 µs for DONE; any timeout is a chip error ((i)). Socket commands are polled across passes with a 10 ms deadline, not a busy loop. 16-bit counters are read until two reads agree, at most 8 tries.
- Chip init, only at a network start: MR = `$80`, wait, ping answering on, RMSR = TMSR = `$55`, SHAR = MAC, RTR/RCR at defaults, all sockets CLOSED, all Sn_IR cleared. `$55` instead of Appletini's `$06` [V uthernet2_control.h:10-14], because three equal UCI sockets must each hold a 1472-byte datagram, which a 1 KB socket cannot [I].
- S0 follows the plan's socket plan: MACRAW (MAC filter, Sn_MR `$44` [V uthernet2_control.h:28]) only while no address is held; UDP on port 68 whenever an address is held, in DHCP and static mode. Received datagrams are demultiplexed by the peer port in the 8-byte header (67 → DHCP with xid and MAC check; 53 → DNS with ID, server and question check); others are consumed and dropped. While S0 is MACRAW every UCI open answers `83` at once, so no lookup waits on MACRAW. During the MACRAW-to-UDP switch after an ACK (a few ms), a lookup waits at most 200 ms, then fails with `84` [I].
- Open item: WIZnet's own DHCP library runs DHCP on a UDP socket with source 0.0.0.0 and the broadcast flag [I, not checked here]. If the board confirms that (E-X3), S0 can stay UDP:68 in every state.

**(e) DHCP (plan §9.6).**

| State | Action | Next |
|---|---|---|
| INIT | S0 MACRAW. Random xid. DISCOVER with options 53, 61 (client id = 1 + MAC), 12 (hostname), 55 (1, 3, 6, 51, 58, 59), broadcast flag | SELECTING |
| SELECTING | Wait for an OFFER with our xid and MAC. Retransmit after 4, 8, 16, 32 s, then every 64 s, each ±1 s random [D RFC 2131 §4.1] | REQUESTING on the first OFFER |
| REQUESTING | REQUEST with 50 (offered IP) and 54 (server id), broadcast flag; 3 tries of 2 s | BOUND on ACK; INIT on NAK or no reply |
| BOUND | Store IP, mask (1), router (3), up to two DNS servers (6), lease (51), T1 (58), T2 (59); defaults T1 = 0.5 × lease, T2 = 0.875 × lease; a lease under 60 s counts as 60 s; `$FFFFFFFF` = infinite. Write SIPR/SUBR/GAR; S0 → UDP:68 | RENEWING at T1 |
| RENEWING | Unicast REQUEST to the server (ciaddr set, no 50/54, flags 0). Retransmit after max(60 s, (T2 − now) / 2) | BOUND on ACK; INIT on NAK; REBINDING at T2 |
| REBINDING | Broadcast REQUEST (ciaddr set) over the UDP S0; if UDP broadcast with SIPR set fails on the W5100S, the plan's fallback is MACRAW after closing the UCI sockets (E-X3). Retransmit after max(60 s, (expiry − now) / 2) | BOUND on ACK; INIT on NAK or at expiry |

- Lease loss (NAK or expiry): close the clients' sockets with cause "service close", cancel a pending lookup with `84`, clear SIPR/SUBR/GAR, S0 → MACRAW, INIT. No chip reset.
- Link loss keeps lease, sockets and timers. Link return with a valid lease: one unicast renew; else INIT.
- Return to the C64 core after a core switch: the chip was reset, so firmware writes the cached address if the lease is still valid and confirms it with a renew; else INIT (plan §9.6).
- SET_IPADDR suspends DHCP (renew and rebind stop); S0 stays UDP:68 for DNS.
- Parser: bounded TLV walk; pad and end; an option that runs past the packet rejects the packet; option 52 (overload) ignored; unknown options skipped.
- Timers use the PS global timer (64-bit µs), not the RTC.

**(f) DNS (plan §9.6).**
- Servers: DHCP option 6 (first two) or `dns1`/`dns2` from `network.ini`; none → `84` at once.
- Query: random 16-bit ID, RD = 1, one question, type A, class IN. Names 1-253 bytes, labels 1-63; letters, digits, `-`, and `_` tolerated. A dotted IPv4 literal skips DNS; 0.0.0.0 and 255.255.255.255 are refused for TCP.
- A response is accepted only from the queried server, port 53, with our ID, QR = 1, opcode 0 and the echoed question (case-insensitive). RCODE 3 → `84` at once; RCODE 2 or 5 → next server; TC = 1 → use the A records present, else next server. Compression pointers are followed with a loop guard (at most 16 jumps, offsets inside the message); CNAME chains up to 4 steps; the first A record of the final name wins. Messages up to 512 bytes.
- Cache: 8 entries, least recently used out, TTL clamped to 30 s - 1 day, flushed on an address change, a mode change and a core switch. One query in flight; a queue of 2.

**(g) MAC and randomness (plan §9.7, P54).**
- The plan's rule: a `mac=` key wins; otherwise firmware generates a MAC once, at the first switch from Off, from a hash over the SD card CID, the 64-bit timer at that key press and 16 XADC readings (first octet `(x & 0xFC) | 0x02`), and stores it; it is never regenerated while the key exists.
- Concern: a whole SD card copied to a second board carries `mac=` with it, so two boards on one LAN would share a MAC. Proposal FI-3: store a generated MAC as `mac_auto=` together with `mac_card=` (CRC32 of the CID); at each network start, if the card's CID no longer matches, generate a new `mac_auto`. A user-written `mac=` always wins and is never changed. The plan's alternative (device DNA through a `DNA_PORT`) needs a clock below the primitive's limit, which is a new clock rule (P54).
- Random numbers: xorshift64*, seeded with the same CID hash, the timer and the XADC readings, and mixed with the timer at every DHCP and DNS event. Used for the DHCP xid, DNS IDs, ephemeral ports (random start in 49152-65535, +1 per open, skipping ports in use) and retry jitter. Not cryptographic; the service holds no secret [I].
- Hostname default: `multitini-` plus the last 3 MAC bytes in hex.

**(h) Settings (plan §9.8, P53).** `0:/multitini/network.ini`, strict `key=value` in the core-package parser style [V firmware/common/core_package.c:203-253], rewritten whole through `mt_sd_storage_journal_replace()` and read after `mt_sd_storage_journal_recover()` [V firmware/platform/xilinx/sd_storage.h:75-82], as `turbo_controls.bin` is [V supervisor_settings.c:900-937]. Keys: `mode=off|dhcp|static`, `ip`, `netmask`, `gateway`, `dns1`, `dns2`, `hostname`, `mac` (plus `mac_auto` and `mac_card` if FI-3 is adopted). A malformed value rejects the file: mode Off and a warning; an unknown key is a warning only. Default when absent: Off, until G-NET passes. Board scope; not in `settings.bin`. While the SD card is exported over USB, writes are refused and the change is not applied.

**(i) Menu, status, UART and lifetime (plan §5.3, §9.8).**
- System rows "Network" (Off/DHCP/Static) and "Network status" (`Off`, `DHCP: waiting for lease`, `DHCP 192.168.1.37, link up`, `Static 192.168.1.50, link down`). Proposal FI-5: one more text, `192.168.1.90, set by C64 program`, while a SET_IPADDR address is active; Enter on that row clears it and restarts DHCP. `MT_MENU_MAX_SYSTEM_ITEMS` 21 → 23 [V firmware/include/multitini_menu.h:20]; `system_frontend.c` fills a fixed array and then copies ranges into the About list and compacts the System list [V firmware/common/system_frontend.c:99-140], so the new rows shift those ranges.
- UART: `:net` (mode, link, MAC and its source, IP, mask, gateway, DNS, lease left, T1/T2, socket table with owner, mode, state and ports), `:net set KEY VALUE`, `:net resolve NAME`. Proposal: `:net renew`, the same action as Enter on the status row.
- Lifetime:

| Event | Network action |
|---|---|
| Firmware start, C64 core active, mode ≠ Off | `RUN` = 1; after READY: chip init, then DHCP from INIT (or the cached lease) or the static address |
| C64 reset | UCI sockets closed (3.10.5); lease, runtime address and S0 untouched |
| Mode or address change | close every socket (UCI gets closed reports), `RUN` = 0, then a fresh start |
| Core switch away, standby | close every socket, `RUN` = 0; other images keep the chip in reset |
| Return to the C64 core | chip init; cached lease confirmed by a renew, else INIT |
| USB SD export | network keeps running; settings writes refused; UCI file commands answer `DEVICE NOT READY` |
| Chip error (an access or a socket command times out) | close every socket with cause "service close", `RUN` = 0, retry the start after 1 s; after 3 failed starts the status row shows the error until a mode change |
| PL reconfiguration | the bridge runs the full reset hold (plan §9.2; pull-ups E-X14) |

**(j) Debug.** Raw W5100S register access and packet traces are debug features: off at power-on, refused without `:debug on`, never in the menu (standing rule; plan §9.8).

#### 3.10.8 HTTP (`$06`), XM8

- Commands per the U64 3.15 target [V 1541ultimate software/io/network/http_target.cc; http_target.h:13-41; D 1541u-documentation uci/http_target.rst:121-161]. Until XM8, `$06` is an empty target, so `$06 $01` answers `NO TARGET`.
- Mapping: header and body slots in PS RAM (16 + 16, bounded sizes); DO_EXCHANGE = DNS → one TCP socket (S1-S3) → send → receive the header (≤ 4 KB) → body as raw 895-byte Data More blocks or a parsed JSON object → close.
- Fixes: `https://` is refused with `501 HTTPS NOT SUPPORTED` (the U64 silently connects plain TCP to port 443 [V http_target.h:57-101; 1541ultimate software/network/http_request.cc:6-50]); slots are freed on C64 reset (the U64 keeps them [V http_target.cc:6-41, 304-419]); connect, header and idle-body timeouts of 10 s each.
- Why later (**USER** P6): the W5100S has no TLS and we have no TLS stack; the target is large (slots, JSON builder) for few clients; it needs the DNS, TCP and timeouts that XM5 proves first; the U64 itself added it only in 3.15.
- Implementation: original code written from the documented interface, with upstream `http_target.cc` (GPL-3.0) as a test-only oracle.

#### 3.10.9 FTP SD service (not requested)

Appletini's FTP server for the SD card [V appletini-one ps_sources/frontend/ftp_sd_service.c] is anonymous, passive-only and needs exclusive SD ownership [V appletini-one ps_sources/frontend/ftp_sd_service.h:7-12]. It could be a later option, default Off (**USER** P55). While it owns the SD card, UCI file commands would answer `DEVICE NOT READY`, and it would need two of S1-S3.

### 3.11 Service latency

- Pickup: one superloop pass of `service_runtime()` (plan §7.5). Target ≤ 1 ms with no blocking action [O, E-X8].
- Worst case: the longest synchronous action in the loop (content load, journaled save, D64 commit, menu render, screenshot), up to seconds. The C64 sees Busy.
- oscar64 waits up to 65,536 status reads for ABORT_P to clear before each PUSH [V oscar64 oscar_common.c:430-441], and its `uii_detect()` writes ABORT at every program start (2.7). An ABORT in Idle completes in the block (3.6), so that wait ends at once on our board, also right after a C64 reset.
- The wait matters only after an ABORT of an open transaction while the loop is blocked. oscar64 then waits out its bound: about 65-100 ms at turbo, where each status read is one φ2 and the rest of the loop is fast; about 0.5-2 s at 1 MHz, at 8-30 cycles per loop pass depending on the compiled code [I; the library comment says "well under a second at 1 MHz", V oscar64 oscar_common.c:436-438]. It then waits for Idle without a bound, which comes when the service answers. No PUSH meets ABORT_P (3.3).
- The PL-to-PS interrupt (IRQ_F2P) exists but is unused [V scripts/create_project.tcl:208-210; hdl/common/multitini_control_regs.sv:349]. An ISR could only set a flag, because FatFs has one cooperative, non-reentrant owner [V sd_storage.h:44-50]. The superloop would still read that flag, so latency would not improve. Not used (plan §7.1).

### 3.12 The `$AB`→`$D038`, `$CD`→`$D036` unlock

Decision: not implemented (U11, P9). Reasons:
1. "Command Interface" is the user's consent for C64 software to delete files on the SD card and to open network connections. With the unlock, any program bypasses it. On the U64 the setting is no longer a boundary [V u64_config.cc:1012-1019].
2. Detecting the sequence needs write snoops on `$D036/$D038` in the VIC-page write path, which at turbo is the posted fast-I/O path [V island_map.sv:66-68]. That is turbo-critical decode for a bypass.
3. On the U64 the unlock leaves ABORT_P set, which causes the empty-reply race [R oscar64 oscar_common.c:138-143].
4. The client that uses it probes first and falls back: "simply ignored and uii_detect() keeps failing exactly as it does today" [V oscar64 oscar_common.c:118-120]. A title `.cfg` with `Command Interface=Enabled` gives the same per-title result through the user's own file.

The writes reach the VIC as today; `$D036`/`$D038` are unused VIC registers [I].

### 3.13 PS-side registers (exact)

The plan allocates and lays out these registers (plan §3.2, §3.4). This section adds their behaviour.

**`C64_UCI_STATUS` @0x128 (ro).**
- Bit 28 `HANDSHAKE_BUSY`: the op mailbox has not accepted the last op. Driven in `control_clk` from the mailbox source, not from the snapshot.
- `[17:8]` while CMD_BUSY = 1: the latched pushed length; otherwise the current fill count.
- Bits 27 effectively enabled, 26 suspended, 31 valid.

**`C64_UCI_HANDSHAKE` @0x12C (wo).** `[31:29]` = epoch[2:0] the op was built for (3.6); the op mailbox is 32 bits. The block drops an op whose epoch tag or push sequence `[28:25]` differs from its own. Op meanings, applied in `c64_clk`:

| Op | Name | Applied when (epoch and seq always match) | Effect |
|---|---|---|---|
| 1 | ACCEPT | CMD_BUSY = 1 | CMD_BUSY = 0, fill count 0. Defined; the service does not use it (the first validate implies it) |
| 2 | NEXT | — | DATA_ACC = 0 |
| 3 | ABORT_DONE | ABORT_P = 1 | ABORT_P = 0, DATA_ACC = 0, state Idle, CMD_BUSY = 0, reply counts 0, latches 0. Fill count unchanged (3.6) |
| 4 | VALIDATE_LAST | state Busy | Lengths latched; counts 0; both heads fetched while still Busy; then on one edge: state Data Last, CMD_BUSY = 0, DATA_ACC = 0, DATA_AV/STAT_AV per the lengths, FREEZE/TRIGGER = 0. If CMD_BUSY was 1 (first block, the U64 ACCEPT): fill count 0 (3.5) |
| 5 | VALIDATE_MORE | state Busy | same, state Data More |
| 15 | RESET | — | U64 `HANDSHAKE_RESET`: state Idle; CMD_BUSY, DATA_ACC, ABORT_P = 0; counts and fill count 0; latches 0. ERROR unchanged [V command_protocol.vhd:209-232]. Used for the no-reply bit, and by the engine to force Idle after a firmware restart (it reads seq and epoch first) |

Every op, applied or dropped, advances `STATUS2.op_ack`. Bad writes (set `MT_ERROR_BAD_WRITE`, op not sent): op not in {1, 2, 3, 4, 5, 15}; response length > 896; status length > 256; bit 15 set; `HANDSHAKE_BUSY`; caps bit 5 = 0.

**`C64_UCI_CONTROL` @0x130 (rw, reset `0x0000001F`).** Bit 10 `HOLD`: the service holds the C64 machine through the HDL pause path, with no menu. XM5 uses it around the REU invalidate (3.8.6) and the `$50` expansion write (3.8.4); it is forced 0 unless caps bit 5. XM5b adds the latched FREEZE bit as a second hold source (caps bit 13). `[13:12]` ≠ 0 is a bad write. The `controls_i` `RESET_VALUE` carries `14'h001F` in the UCI field (plan §3.5), so `$DF1B` reads 31 before the first PS write.

**`C64_UCI_STATUS2` @0x134 (ro)**, from `c64_clk` in the same `status_i` snapshot as STATUS. Firmware reads STATUS first. That read latches STATUS2 into a shadow register in `multitini_control_regs.sv` on the same edge, and a STATUS2 read returns the shadow. So the pair always comes from one snapshot. Without the shadow, two single-beat reads [V hdl/common/multitini_axi3_regs.sv:4-7] could see different snapshots, because `status_i` sends a new one whenever any of its bits changes [V hdl/common/multitini_word_mailbox.sv:18], including the UA fetch counters and CPU status [V multitini_one_top.sv:1252-1257]. Read side effects already exist in that bank [V multitini_control_regs.sv:267-270]. Fields as plan §3.4: [9:0] response bytes read, [18:10] status bytes read, [20:19] `op_ack`, 21 last op dropped, 22 `$DF1D` bytes written while CMD_BUSY, 23 command overflow, [27:24] XM5b RAM-read done sequence, [30:28] idle-abort count (`hard_reset` domain), 31 valid.

**`C64_UCI_RAM_READ` @0x138 (wo, XM5b, caps bit 13)**: [15:0] C64 address, [26:16] length − 1 (≤ 1,152 bytes), [27] start; done in `STATUS2[27:24]` (4.1.5).

**`C64_UCI_BUFFER` @0xC000-0xC7FF**: command 0x000-0x37F, response 0x380-0x6FF, status 0x700-0x7FF; 32-bit words, byte `b` at 0xC000 + `b`.
- PS access rule: volatile 32-bit single accesses only (`Xil_In32`/`Xil_Out32`), with explicit little-endian byte packing in software; never `memcpy`. The register slave answers SLVERR to bursts and to any transfer that is not 32 bits [V multitini_axi3_regs.sv:4-7, 164-195], so a byte or halfword store or an LDM/STM burst would be an external abort.

---

## 4. Implementation plan by file

### 4.1 RTL

#### 4.1.1 New `hdl/core/c64/multitini_c64_uci.sv` (`c64_clk` only)

- Ports: `clk`, `hard_reset` (= `!c64_aresetn`), `block_reset` (= `!core_reset_n || !active`), `bus_valid`, `bus_write`, `bus_addr[2:0]`, `bus_wdata`, `select`, `rdata[7:0]`, `bus_id[4:0]`, `op_data[31:0]`, `op_valid`, `op_ready`, `status[31:0]`, `status2[31:0]`, BRAM port A (`ram_addr[10:0]`, `ram_we`, `ram_wdata`, `ram_rdata`), `freeze` (0 until XM5b), `irq` (0 until the IRQ milestone).
- State: 2-bit state, ERROR, ABORT_P, DATA_ACC, CMD_BUSY, busy-write flag, overflow, FREEZE/TRIGGER/IRQ latches, 10-bit fill count, 10-bit latched length, 10-bit response length and count, 9-bit status length and count, two head registers with valid bits, and a validate-pending flag for the head fetch before the state switch.
- The epoch, push sequence, idle-abort count, `op_ack` and dropped flag are in the `hard_reset` domain, so they survive a block reset.
- Op filter: an op applies only if its `[28:25]` equals the push sequence and its `[31:29]` equals epoch[2:0], plus the per-op condition of 3.13.
- Read mux (combinational from registers only): `$FF`, bus id, status byte, `$C9`, response head or `$00`, status head or `$00`, by `bus_addr[2:0]`.
- Every C64-side side effect is captured into registers on the `bus_valid` edge. The BRAM write happens one cycle later, which keeps the BRAM enable off the `cpu_bus_valid` path [I]. The write enable for a `$DF1D` byte is gated by CMD_BUSY = 0 at that edge (3.5). The next C64 access comes at least 32 cycles later.
- Head fetch: on op 4/5, read both heads while still Busy, then switch the state (3.5). After each pop, read the next head; the data returns one cycle later. C64 writes take priority on port A.
- No cartridge, REU or turbo logic inside; the adapter gives `select`.

#### 4.1.2 New `hdl/core/c64/multitini_c64_uci_transport.sv` (two clocks, top level)

- `xpm_memory_tdpram`, independent clocks: port A `c64_clk` 8-bit, port B `control_clk` 32-bit, 2 KB used. Precedent with asymmetric widths: `multitini_text_bank` [V hdl/common/multitini_text_overlay.sv:24-50]. A behavioural model under `ifndef SYNTHESIS`, as there.
- `multitini_async_mailbox #(.WIDTH(32), .DRAIN_ON_RESET(1))` for ops (op, lengths, seq, epoch tag), `control_clk` → `c64_clk` [V pattern hdl/core/c64/multitini_c64_iec_transport.sv:62-71].
- Port B: write from `reg_wr_en/addr/data/strb` in the window; read address from the new early-read outputs of `multitini_axi3_regs` (4.1.4). Only 32-bit single beats reach it; anything else gets SLVERR (3.13).
- The same split as the dev7 IEC transport: two-clock module in the top, `c64_clk` endpoint in the adapter [V multitini_one_top.sv:1215-1226]. The UCI's static control and status words ride the shared `controls_i`/`status_i` mailboxes, so each remote reset keeps a single synchroniser [V multitini_one_top.sv:1241-1257]. The transport holds only the BRAM and the op mailbox.

#### 4.1.3 `hdl/core/c64/multitini_c64_adapter.sv`

- New ports: `uci_enable`, `uci_bus_id[4:0]`, `uci_op_*`, `uci_status`, `uci_status2`, `uci_ram_*`, `uci_hold`.
- Selects, beside the REU/UA selects [V multitini_c64_adapter.sv:608-614]:
  - `uci_active = uci_enable && !cart_loading && (!cart_loaded || loader_cart_id == 8'd0)` (3.1)
  - `uci_io_select = uci_active && !reu_dma_req && cart_iof && cpu_bus_addr[7:3] == 5'b00011`
  - `reu_io_select` gains `&& !uci_io_select`.
- fpga64 ports [V multitini_c64_adapter.sv:751-752]: `io_ext` gains `|| uci_io_select`; `io_data` = `uci_io_select ? uci_rdata : ua_io_select ? … : …` (UCI first; its range never overlaps UA).
- Instance `multitini_c64_uci` with `.bus_valid(cpu_bus_valid && uci_io_select)` and `.block_reset(reset || !core_reset_n || !uci_active)`.
- `irq_ext_n` [V multitini_c64_adapter.sv:766] unchanged in XM5.
- XM5: the pause input of fpga64 and of the island gains `|| uci_hold` (`C64_UCI_CONTROL` bit 10) [V multitini_c64_adapter.sv:511, 542, 736]. XM5b adds the latched FREEZE bit as a second source of `uci_hold`.

#### 4.1.4 `hdl/common/multitini_axi3_regs.sv` and `hdl/board/multitini_one_top.sv`

- AXI slave: the read address is registered at `ar_take`, and the register read data is sampled at the end of the next cycle [V multitini_axi3_regs.sv:99-104, 181-195]. A BRAM needs its address one cycle earlier. Add two outputs: `reg_rd_early_en` (= `ar_take` for a single-word read) and `reg_rd_early_addr` (= `s_axi_araddr[15:0]`). No behaviour change for existing users.
- Fallback, if the `control_clk` path through the window read mux fails timing (+0.288 ns PAL today, 0.6): give window reads one wait state (hold `s_axi_rvalid` one more cycle for 0xC000-0xC7FF) and register the BRAM output, instead of the early read. It costs one `control_clk` cycle per 32-bit buffer read [I].
- Top, `g_c64_machine`:
  - Instantiate the transport.
  - Window decode 0xC000-0xC7FF, like the framebuffer window [V multitini_one_top.sv:217-220, 829-843]: excluded from `control_wr_en`, `ui_vram_wr_en` and bad writes; the read mux gains `uci_buffer_rd_select ? uci_buffer_rd_data`.
  - `controls_i` 120 → 136 bits and `status_i` 228 → 292 bits (301 with multi-SID), in the layout and with the single `RESET_VALUE` of plan §3.5. dev7 already carries its IEC words in them [V multitini_one_top.sv:1244-1257].
  - `C64_EXPANSION_CAPS` `32'h8000001F` → `32'h8000003F` in the XM5 image [V multitini_one_top.sv:749]; XM5b adds bit 13.
- Other images: the window reads 0; caps bit 5 = 0.

#### 4.1.5 XM5b additions (RAM write, FREEZE hold, RAM read)

- `hdl/core/c64/multitini_c64_content_loader.sv`: load kind 0x17 (plan §3.6) = "C64 RAM write, no reset". The same framing as `MT_LOAD_C64_PRG` without `STATE_PRG_RESET` and `STATE_PRG_POINTERS`; offsets 0-1 carry the load address. `prg_install_pause` is asserted while streaming [V multitini_c64_content_loader.sv:374-381, 571-588]. This is an HDL pause, not the menu.
- Hold: the latched FREEZE bit joins `C64_UCI_CONTROL.HOLD` as a source of `uci_hold`. Time stops for the C64; no menu appears.
- RAM read for SAVE and VERIFY: the UCI block reads main RAM through the island memory peek port while held, muxed with the debug peek [V multitini_c64_adapter.sv:485-498, 657-660]. Results go into the reply area of the UCI buffer, up to 1152 bytes per request, requested through `C64_UCI_RAM_READ` (3.13).
- The debug peek keeps its own gate (`debug_enable && paused`) [V hdl/core/c64/multitini_c64_host.sv:39-56]. The UCI read is gated by the UCI hold; it is not a debug feature.

#### 4.1.6 Other UCI HDL files

- `hdl/common/multitini_control_regs.sv`: registers 0x128-0x134 (0x138 in XM5b); the bad-write rules (3.13); clamps: `clamp_c64_expansion()` mask `0x7F`, `if (!C64_EXPANSION_CAPS[5]) result[5] = 0;` [V multitini_control_regs.sv:235-243]; CONTROL bit 8 forced 0 unless caps 13, bit 9 unless caps 14, bit 10 unless caps 5; the writable-offset list [V :404-420]; the STATUS2 shadow, latched on `rd_en` of a STATUS read (3.13).
- `hdl/hdl_sources.txt`: the two new modules.
- No change to `multitini_c64_island_map.sv`, `multitini_c64_cpu_island.sv`, `multitini_c64_reu.sv`, `multitini_c64_reu_cache.sv` (unless the alternative of 3.8.6 is taken), `multitini_c64_ua.sv`, `cartridge.sv` or `fpga64_sid_iec.vhd`.

#### 4.1.7 Network RTL (XM4, plan §9.2, §9.10)

- New `hdl/board/multitini_w5100s_bridge.sv`, instance `net_bridge_i` in the top under `NET_BRIDGE`; today's `eth_*` idle assignments [V multitini_one_top.sv:2329-2334] stay in the `else` branch. The top drives `eth_d` per pin from the replicated enables (`eth_d[i] = eth_d_oe[i] ? eth_d_o[i] : 1'bz`), as Appletini does with one enable [V appletini-one hdl/appletini_yarz_top.sv:958-963].
- IOB attributes and the write-data hold of 3.10.7 (b) (proposals FI-1, FI-2).
- `hdl/common/multitini_control_regs.sv`: `NET_*` at 0x02C-0x038; `NET_ACCESS` is a write pulse to the bridge in the same `control_clk` domain.
- `hdl/board/multitini_timing.xdc`: the false paths of 3.10.7 (c). `scripts/build_and_export_xsa.tcl`: the IOB check for C64 images (FI-2).
- `provenance/sources.lock.json` and `licenses/files.csv`: the Appletini source pinned at `73de6fe`.

### 4.2 Clocks, resets, crossings and timing rules (review list)

Numbers follow plan §11.

| # | Item | Kind | Primitive | Review point |
|---|---|---|---|---|
| C2 | `UCI_ENABLE` | existing word crossing, wider | `controls_i` | coherent with the REU/UA/SID bits on one `c64_clk` edge |
| C4 | UCI buffer | dual-clock BRAM | `xpm_memory_tdpram` | No same-word conflict by protocol. The C64 writes the command area only while CMD_BUSY = 0 (3.5). The PS reads the command area only while CMD_BUSY = 1 (from the PUSH to the first validate), so its last 32-bit word (bytes len..len+3) never meets a C64 write. A block reset in that window lets the C64 write again, but it also changes the epoch, and the engine discards the copy (3.6). The PS writes the reply area only in Busy, before the op; C64-side head reads run only after the op arrives. XM5b RAM-read results are written on port A while the PS waits for done |
| C5 | ops PS → C64 | event crossing | `multitini_async_mailbox`, 32 bits | stale-op drop by push sequence and epoch tag (`[31:29]`); `HANDSHAKE_BUSY` from the source side |
| C6 | STATUS, STATUS2 | wider word crossing | `status_i` +64 | both words from one `c64_clk` cycle; firmware reads them as a pair through the STATUS2 shadow |
| C7 | block reset | C64 reset, suspension, disable | `core_reset_n`, `uci_active` | epoch and sequence counters outside the block reset |
| C9 | W5100S bridge pins | I/O timing exception | IOB flops; `set_false_path` (3.10.7 (c)) | the bus sequence of 3.10.7 (a)-(c); the IOB check (FI-2); the corrected comment (FI-1); the datasheet numbers (E-X3) |
| C10 | `eth_int_n` | asynchronous input | `cdc_bit_sync`; existing static false path kept [V multitini_timing.xdc:93-96] | |
| C11 | load kind 0x17 (XM5b) | extends the load path | `c64_load_mailbox` | must not change REU DMA timing (G-REU0) |
| C12 | UCI hold (XM5: `CONTROL` bit 10 for the REU invalidate and the `$50` expansion write; XM5b: FREEZE) | existing pause path, via `controls_i` | — | shared with the menu pause and `prg_install_pause`; behaviour of an REU DMA under the pause (E-X5) |
| C13 | `C64_UCI_CONTROL` | word crossing | `controls_i` +14 | static value; `$DF1B` may lag one snapshot; reset value 31 in `RESET_VALUE` |
| C14 | write ordering PS buffer → op | same AXI slave, in order [V multitini_axi3_regs.sv:164-179] | firmware DSB + read of `C64_UCI_STATUS` before the op write | the AXI path is in order; the read-back guards the GP0 memory type (E-X15) |
| C15 | early read address | `control_clk` only | AXI AR channel | BRAM output valid on the `reg_rd_en` edge (bench T-U3); the one-wait-state fallback of 4.1.4 if `control_clk` slack runs out |
| C16 | STATUS2 shadow | `control_clk` only | register | latched on the `rd_en` edge of a STATUS read, from the same snapshot (T-U3) |

No new clock and no new MMCM. No new `set_false_path` or `set_max_delay` inside the FPGA: the BRAM and the mailbox are XPM primitives with their own constraints, as in the text overlay and the IEC transport [I]. The only new port exceptions are the `eth_*` false paths of plan §9.3.

### 4.3 Spec and ABI

- `spec/interface_v0.json`, in the plan's single 0.26 edit (plan §3.1, P1): UCI registers 0x128-0x134 (0x138 for XM5b); `C64_EXPANSION_CONFIG` bit 5 and mask; caps bits 5, 13, 14; `C64_UCI_BUFFER_OFFSET` = 0xC000, `C64_UCI_BUFFER_BYTES` = 0x800; op codes; `C64_UCI` and `NET` `*_MIN_ABI_MINOR` = 26; `NET_*` at 0x02C-0x038.
- Regenerate `hdl/generated/multitini_regs_pkg.sv` and `firmware/include/multitini_regs.h` with `scripts/generate_registers.py`; `make check-generated`; `tests/test_interface_spec.py` (plan §3.7 rule 4).
- `spec/core_interface_v0.md`: the 0.26 section (plan §18).

### 4.4 Firmware: portable modules (host-tested, `multitini_common` in `CMakeLists.txt`)

| File | Content |
|---|---|
| `firmware/include/multitini_c64_uci.h`, `firmware/common/c64_uci.c` | Protocol engine: reads STATUS, then STATUS2 (the shadow, 3.13); detects the reset epoch, an idle-abort count change (consumed before the next dispatch), ABORT_P, a new PUSH (CMD_BUSY with a new push sequence) and DATA_ACC; copies the command into the 896-byte shadow (3.6) and reads STATUS again to confirm the epoch; dispatches; writes replies; builds every op with the current push sequence and epoch[2:0]; sends ops one at a time and waits for `op_ack`. Ops struct: `read_reg`, `write_reg`, `read_buffer`, `write_buffer` (loops of volatile 32-bit single accesses with explicit little-endian packing; never `memcpy`), `barrier`, `now_us`. Target interface: `begin(cmd, len, shadow, reply)` → DONE or PENDING, `step(reply, budget_us)`, `more(reply)`, `abort(consumed)` (idempotent), `c64_reset()`, `shutdown()` |
| `firmware/common/c64_uci_dos.c` | DOS targets over `mt_uci_fs_t`: the same storage primitives and protection callback as drive 9 [V multitini_c64_iec_dos.h:17-31], with the access kind of 3.9 rule 8, plus seek, stat (size, FAT date/time, attributes), directory snapshot and FatFs result pass-through for the U64 texts |
| `firmware/common/c64_uci_control.c` | Control target; hooks for product name, SID list (`mt_c64_sid_hwinfo()`; its legacy part in XM5), drive info, REU transfer, menu freeze and reboot (posted as supervisor actions, 4.5), overlay load with the HOLD sequence of 3.8.4 |
| `firmware/common/c64_uci_net.c` | Network target over the `mt_net_*` API (3.10.1) |
| `firmware/common/c64_uci_softiec.c` | XM5b, over `mt_iec_dos_t` [V multitini_c64_iec_dos.h:65-80] |
| `firmware/common/c64_uci_http.c` | XM8 |
| `firmware/common/net_w5100s.c` (plan §9.4) | XM4 driver, socket table, S0 schedule, DHCP, DNS, random numbers, MAC generation (3.10.7) |
| `firmware/common/c64_reu_host.c`, `firmware/include/multitini_c64_reu_host.h` | Add `mt_c64_reu_transfer_begin/step/finish` with `pause`, `ua_hold` and `invalidate_hold` (the HOLD steps of 3.8.6) options. `finish()` always drains and invalidates once any byte was written, also when ABORT, a C64 reset or a quiesce cancels the transfer. `mt_c64_reu_load()` becomes a loop over them, unchanged for the menu [V c64_reu_host.c:15-74] |
| `firmware/common/c64_features.c`, `firmware/include/multitini_c64_features.h` | `mt_c64_capabilities_t` gains `uci`, `uci_ram_dma`; `mt_c64_probe_capabilities()` gates on `MT_C64_UCI_MIN_ABI_MINOR` and caps; `mt_c64_expansion_restore(sid, reu, ua, uci, cartridge, caps)` sets bit 5 [V firmware/include/multitini_c64_features.h:8-26] |
| `firmware/common/c64_option_registry.c` (XM1) | Row: id `UCI_ENABLE`, argument 15, byte 26 bit 4, global, caps EXPANSION bit 5, `.cfg` `[C64 and Cartridge Settings]` (alias `[Cartridge and ROM Settings]`), item `Command Interface`, `Disabled`/`Enabled`, apply class expansion write, overlay allowed (not from UCI LOAD_CONFIG) |
| `firmware/common/core_frontend.c`, `firmware/include/multitini_core_frontend.h` | Row "Command Interface" (argument 15) after RAM expansion, before Ultimate Audio (plan §5.1), through the bounds-checked append (XM1). Values "Off", "On", "On (suspended: cartridge)". Help (ASCII): "Lets C64 programs use the Ultimate Command Interface at $DF1C: files in the media folder, clock, network. Off by default." State field `c64_command_interface` |

### 4.5 Firmware: platform glue

- New `firmware/platform/xilinx/c64_uci_service.c/.h`:
  - `c64_uci_service_init(mt_core_link_t *link, const char *root, c64_iec_protected_fn protected_path, void *context)` (the same shape as `c64_iec_service_init()` [V firmware/platform/xilinx/c64_iec_service.h]).
  - `c64_uci_service_poll(void)`: budget 1 ms, file I/O ≤ 4 KB (plan §7.1).
  - `c64_uci_service_notify_core_reset(void)`, `c64_uci_service_quiesce(void)`, `c64_uci_service_shutdown(void)`, `c64_uci_service_status(void)`.
  - MMIO ops, binding to `sd_storage`, REU transfer, RTC, menu freeze, reboot, overlay, net.
  - Posted supervisor actions (plan §7.3). Control `$05` FREEZE, `$06` REBOOT and DOS `$23` MOUNT_DISK, `$24` UMOUNT_DISK are not run inside `c64_uci_service_poll()`. The handler posts a pending supervisor action, in the `pending_menu_action` pattern [V main.c `pending_menu_action` and its main-loop consumer], and returns PENDING. The main loop runs the action after the poll returns; the handler completes on a later pass. Reason: `set_core_reset_request()` quiesces the C64 services [V main.c `set_core_reset_request()`], and this design adds `c64_uci_service_quiesce()` there, so a direct call would re-enter the UCI service in the middle of a handler.
  - The REBOOT action: `set_core_reset_request(true)`, 1 ms, `set_core_reset_request(false)`, and nothing else (3.8.3). It never calls `restore_selection_settings()`.
  - `c64_uci_service_quiesce()` and `c64_uci_service_notify_core_reset()` accept being called while such an action is pending. REBOOT's reply is dropped by the reset, as on the U64; the MOUNT, UMOUNT and FREEZE replies are validated when the action completes, or the transaction is cancelled if a reset intervened.
- `firmware/platform/xilinx/net_service.c/.h` (XM4, plan §9.4): binds `net_w5100s` to `NET_*`; `net_service_poll()`; settings; System actions; `:net`; the lifecycle hooks of 3.10.7 (i).
- `firmware/platform/xilinx/main.c`:
  - `supervisor_activate()`: UCI init after `c64_iec_service_init()`, with `selection->package.content_root` and `c64_iec_path_protected`; net start when the C64 core is active and `NET_CAPS` is valid.
  - `service_runtime()`: `net_service_poll()` before the core-specific block; `c64_uci_service_poll()` after `c64_iec_service_poll(32U)`; in the `MT_CONTROL_CORE_RESET_REQUEST` branch, `c64_uci_service_notify_core_reset()`.
  - Quiesce at the drive 9 points (plan §7.2): `set_core_reset_request()`, `prepare_c64_content_change()`, `mount_c64_disk()` (UCI mount excluded), `supervisor_activate()`, USB SD export, core switch, standby. Net shutdown at core switch and standby.
  - `handle_menu_action()`: argument 15 branch before the `>= 2U` expansion catch-all → new `change_c64_uci()`, which edits `stored`, saves, then writes the effective `C64_EXPANSION_CONFIG` (plan §8.2 rules 1-3). New System actions for the two network rows.
  - `configure_c64_expansion()` and `supervisor_menu_view()` carry bit 5; `C64_UCI_CONTROL[4:0]` is written at restore (31; 9 with XM5b).
- `firmware/platform/xilinx/sd_storage.c/.h`: `mt_sd_file_stat()` (FILINFO size, date, time, attributes) and an "open always" mode for OPEN_FILE `$10` if the stdio-style modes cannot express it [I; V sd_storage.h:93-100].

### 4.6 Settings, menu and UART

- `firmware/platform/xilinx/supervisor_settings.c/.h`: `c64_command_interface` in byte 26 bit 4 (plan §4.1); encode and decode next to the existing byte 26 fields [V supervisor_settings.c:588-589, 710-711].
- `tests/host/test_supervisor_settings.c:994-996`: at XM5 every byte 26 bit 0-5 has a field, so the assertion covers bits 0-5 and the future-bit mask is `0xC0` (plan §4.3). XM2 (`0xD8`) and XM3 (`0xD0`) are the steps of the title-config and SuperCPU designs.
- Options row (4.4). The row applies while paused (plan §5.1). No System row for the UCI; the network rows are XM4's (3.10.7 (i)).
- UART `:uci`: enabled, suspended, state, push sequence, epoch, idle-abort count, fill count, busy-write and overflow flags, last target and command, open UCI files, UCI sockets. Not a debug feature (no C64 or REU data).
- UART `:uci trace on|off`: prints command and reply bytes; refuses without `:debug on` (standing rule; plan §13 G-DBG).

### 4.7 Budget

**Resources.**

| Item | LUT | FF | Slices | BRAM | DSP | Basis |
|---|---|---|---|---|---|---|
| UCI block + transport + window + STATUS2 shadow + HOLD | 300-600 (×2 margin: ≤ 1,200) | 280-450 | 100-410 | 1 RAMB36 tile | 0 | [I] |
| `controls_i` +16, `status_i` +64 | ~50 | ~160 | 20-60 | 0 | 0 | word mailbox flops on both sides [I] |
| XM5b loader kind, FREEZE hold, RAM read | 150-300 | 100-150 | 50-100 | 0 | 0 | [I] |
| W5100S bridge + NET registers (XM4) | 150-300 (×2: ≤ 600) | 100-150 | 50-205 | 0 | 0 | host half of `uthernet2_card.sv` [I]; plan §10.1 |

- Baseline: dev7 PAL, 36,689 LUT and 12,519 slices (94.13 %), 2.93 LUT per slice. Headroom to 95 %: 116 slices (0.6).
- XM4 + XM5 at raw estimates: about 500-950 LUT, about 170-325 slices at today's packing; with the ×2 margin up to about 640 slices [I]. Both exceed the headroom at today's packing. Vivado packs denser as the device fills, so the routed build is the only evidence (plan §10.2). XM5 can miss the slice ceiling while its LUT and BRAM figures look comfortable.
- The only lever that frees LUTs is the serial UA mixer, about −1,000 LUT and −6 DSP (plan §10.3 lever 2).
- Each milestone receipt records LUT, FF, slices, BRAM, DSP and the worst path per clock group (plan §13 G-IMG).
- BRAM: a RAMB18 port is at most 18 bits wide in true dual-port mode, so a 32-bit PS port needs a RAMB36, or two RAMB18 in simple dual-port mode [I, from the 7-series block RAM port-width table; O E-X4]. Either way +1 tile: 128 + 1 = 129, within the plan's 136.
- DSP: 0.

**Timing.**
- The UCI adds one input to the `io_data` mux on the `cpu_bus_addr` → `cpuDi` path in `c64_clk` (3.7). That clock has +3.483 ns on PAL and +1.914 ns on NTSC at dev7 (0.6) [O measure at XM5].
- `control_clk` gains the UCI buffer port B and its read mux, the STATUS2 shadow, the NET registers and the bridge. It has +0.288 ns on PAL (0.6). If the window read path fails, 4.1.4 gives the one-wait-state fallback (R16).
- Neither the UCI nor the bridge adds logic to the fast-island half-period path (+0.030 ns at dev7 PAL). Placement pressure may still move it [I]; every build records it.

**Firmware memory.** UCI: 896-byte command shadow, two 64 KB directory arenas, the 16 KiB `$50` buffer (docs/C64_TITLE_CONFIG.md §3.15), 1472-byte socket staging. Network: DNS cache about 2.1 KB, DHCP frame and packet buffers about 1.2 KB (Appletini sizes [V uthernet2_control.c:24-25]), a 512-byte DNS message buffer, the socket table. About 200 KB in all [I; O linker map].

---

## 5. Fix instead of copy

| # | U64 behaviour | Evidence | Ours |
|---|---|---|---|
| X1 | An 896-byte reply leaves DATA_AV stuck; 256 status bytes cannot be expressed (#957) | [V command_protocol.vhd:131, 178, 235; R #957] | Byte counters, 10-bit and 9-bit lengths. Firmware still sends ≤ 895 / ≤ 255, because xlar54 overruns a 256-byte status buffer [V xlar54 ultimate_lib.c:577] |
| X2 | Command limit 895; byte 896 and later overwrite byte 895 (#807) | [V :145-147; R #807] | 896 bytes; overflow is reported, never silently merged |
| X3 | A refused PUSH still latches DMA/TRIGGER/IRQ | [V :152-161] | Latched only on an accepted PUSH |
| X4 | PUSH while ABORT_P: command lost, empty reply | [V command_intf.cc:136-182; R oscar64 oscar_common.c:430-441] | An ABORT with no open transaction completes in the block; ABORT_P is never visible in Idle, so no PUSH meets it (3.3, 3.6) |
| X5 | Command length read late; bytes written in Busy can join | [V command_intf.cc:157] | Length latched at PUSH |
| X6 | A handler hang wedges the interface; ABORT never completes (#740, #741) | [R #740, #741] | Bounded state machines; ABORT and reset served first, always complete |
| X7 | Debug/REST reads pop (#983) | [R #983] | Only CPU transactions pop; the peek reads RAM only |
| X8 | Pops with no data advance the pointer | [V :176-185] | No pop without a valid byte; exact consumed count |
| X9 | One-byte commands dispatch on a stale byte; WRITE_SOCKET length −1 (#836) | [V network_target.cc:367; R #836] | One-byte commands → `21`; WRITE_SOCKET length checked. Parameters beyond a short command still come from earlier commands, through the PS shadow, because xlar54's READ_DATA needs them (3.6) |
| X10 | Sockets survived C64 reset (#808, fixed upstream); DOS files and HTTP slots still survive | [R #808; V dos.cc has no `c64_reset`; http_target.cc:6-41] | All closed on C64 reset. A runtime SET_IPADDR address is kept, as on the U64 |
| X11 | COPY_HOME_PATH always `00,OK` (#725) | [R #725] | Real result |
| X12 | COPY_FILE treats the destination as a directory and fails | [V dos.cc:238-253; R C64U COPY_FILE report] | File or directory destination |
| X13 | READ_DIR on an empty listing → `87,INTERNAL ERROR` | [V dos.cc:804-809] | `01,DIRECTORY EMPTY` |
| X14 | FILE_STAT name not terminated; READ_DATA error text dropped; OPEN_FILE leaks the old handle | [V dos.cc:183-187, 795-800, 113] | Fixed |
| X15 | GET_TIME may repeat (#363) | [R #363] | Clock refreshed each second; an invalid clock is reported in the status while the string is still returned |
| X16 | GET_HWINFO reports the mirror-cleared base; an unmapped SID shows `$D010`; no way to list more than 2 SIDs | [V control_target.cc:418-468] | `$01` keeps the U64 shape with the configured base and `$0000`/`$80` for an unmapped SID; the full list on Multitini sub `$02` from XM7a (3.8.3) |
| X17 | `https://` connects in plain text | [V http_target.h:57-101] | Refused with a clear status (XM8) |
| X18 | The unlock bypasses the setting | [V u64_config.cc:1012-1019] | Not implemented |
| X19 | The server serves DATA_ACC after an abort in the same event and validates new data after the reset | [V command_intf.cc:136-154] | An abort ends the transaction; no `get_more` after it |
| X20 | A late op after a C64 reset can revive a stale reply | [I from the server structure] | Ops tagged with the push sequence and the reset epoch (3.6) |
| X21 | A reset or timed-out TCP socket answers −1 forever; xlar54 clients hang when a BBS drops the line | [V network_target.cc:284-328; xlar54 ultimate_lib.c:825-838] | `00 00` + `01,CONNECTION CLOSED BY HOST` once for any close cause, then the id is freed (3.10.3) |
| X22 | A failed LOAD_CONFIG leaves the good lines in RAM; they apply at the next reset | [V control_target.cc:579-596] | All or nothing; merged on success (3.8.4) |
| X23 | SET_TIME writes any value | [V dos.cc:539-562] | Range-checked; invalid values refused (3.8.2) |

Port changes over the Appletini driver and bridge (not U64 issues):

| # | Appletini behaviour | Evidence | Ours |
|---|---|---|---|
| P-a | Fixed MAC `02:41:50:50:4C:01`; two boards collide | [V uthernet2_control.c:93-101] | Generated per board (plan §9.7; FI-3 for copied cards) |
| P-b | DHCP keeps no lease time, DNS or renew; the start resets the chip and closes every socket | [V uthernet2_control.c:44-54, 352-364, 885-916] | Lease client with renew, rebind, expiry and link-return confirm; no chip reset with sockets open (3.10.7 (e)) |
| P-c | Constant xid seed | [V uthernet2_control.c:79] | Seeded random numbers (3.10.7 (g)) |
| P-d | Write data released on the WR# rising edge | [V uthernet2_card.sv:739-744] | Held one more cycle (FI-1) |
| P-e | `usleep()` busy waits | [V uthernet2_control.c:120-173] | Deadlines across passes (3.10.7 (d)) |
| P-f | Unconstrained `eth_*` pins | [V appletini-one hdl/constraints/appletini_yarz.xdc:368-398, 661-673] | IOB flops, reviewed false paths and a packing check (3.10.7 (c), FI-2) |

**Not changed on purpose** (U64 parity kept; a change would break clients or gain nothing):
- Parameters beyond a short command come from earlier commands (3.6). xlar54's `uii_read_file()` depends on it.
- The command fill count resets only at the first validate of a transaction (the U64 ACCEPT), so bytes written during a multi-block reply survive into the next command (3.5).
- Idle READ_SOCKET keeps `FF FF` + `02,NO DATA: 11` (#847, P52). xlar54 treats 0 as EOF and −1 as retry [V xlar54 ultimate_lib.c:833-834]; c64-wireguard tests for `$FFFF` [R #847]. We document `FF FF` as the "no data" value instead. No real read is ever announced as `FF FF` (maximum 1472).
- Block sizes stay at the U64 values (512 for DOS reads, 893/895 for sockets, 32/256 for CHKIN).
- Identify strings and status texts are unchanged.
- `$DF18-$DF1A` stay `$FF`. #975/#985 ask for RAM bytes there; that would change REU-visible reads with the UCI on (P49).
- GET_HWINFO `$01` keeps the U64 frame order and its 4-frame limit (3.8.3).

**Deliberate deviations** (neither a U64 fix nor parity; numbered in the plan's UCI range):
- D40: a write with both ABORT and PUSH from Idle runs the pushed command; the U64 answers it as a null command (3.3).
- D41: `/Temp` is an SD folder emptied at core start, not a RAM disk (3.9 rule 5).
- D42: the UCI is suspended under every cartridge type except the generic one (3.1); the U64 moves it or keeps it per type.
- D43: `$DF1D` bytes written while CMD_BUSY = 1 are not stored (3.5); on the U64 they can join the command (X5) or are dropped by its ACCEPT.
- D44: GET_HWINFO sub `$02` is a Multitini extension; a U64 rejects it (3.8.3).

---

## 6. Risks

| # | Risk | Likelihood | Mitigation |
|---|---|---|---|
| R1 | REU software touches `$DF1B-$DF1F` when the UCI is On. Reads see UCI values; a `$DF1D` write appends a command byte; a `$DF1C` write can PUSH or ABORT. The SuperREU player writes `$DF19` and `$DF1D` [V SuperREU player:28-38] | low: U64 users run REU titles with the UCI on (2.7) | Default Off; G-REU0 with the UCI On; the UCI-on trace differs only at `$DF1B-$DF1F`; B1 also logs the push sequence, idle-abort count, fill count and busy-write flag (`:uci`), which reveal any `$DF1C`/`$DF1D` write by a title, and checks that the titles run the same. A stray PUSH runs whatever bytes were written (zeros give target 0 → `21`); a C64 reset clears the block [I] |
| R2 | `c64_clk` timing on the `io_data` mux | low (+1.9 ns NTSC, +3.5 ns PAL) | Read data from registers only; BRAM off the bus path; measure at XM5 |
| R3 | Superloop blocked for seconds | high during menu and content actions | Clients see Busy; an ABORT with no open transaction never waits for the service (3.6); measure (E-X8) |
| R4 | Client buffer overruns from full-size blocks | real (xlar54 status) | Firmware U64 sizes (X1); ECHO is the only exception and mirrors the client's own length |
| R5 | UCI DOS deletes or overwrites user files | by design, with consent | Off by default; root confinement; protected paths, real sidecars included (P21); no unlock |
| R6 | LOAD_REU without the menu pause races with the program's own REU/UA use during the copy; an REU DMA during an unprotected invalidate would deadlock the REU store | race: low, as on the U64; deadlock: real whenever a DMA request meets the invalidate, so it is designed out | Documented race; HDL hold around drain and invalidate (3.8.6); the cache RTL alternative; bench T-U4 and board B7 with an REU loop running |
| R7 | W5100S socket shortage (3 for the UCI) | medium for multi-socket clients | Clear `85`; reclaim closed sockets; a fourth would need S0 sharing (later) |
| R8 | W5100S details (RST reporting, UDP header, broadcast send, MACRAW on S0, retry registers, bus timing) differ from our assumptions | medium | E-X3 before driver code; T-NET1-T-NET4; board N-B1-N-B7 |
| R9 | The listener reply format is guessed | medium | E-X11; else ship the listener in XM8 |
| R10 | The BRAM costs a full tile | certain | Within the plan's 136-tile ceiling |
| R11 | Third-party test software licences | — | Kept out of git under `build/` after a licence check [D TD §10] |
| R12 | Slice headroom: 116 slices to 95 % at dev7, shared with XM3, XM4 and XM5 | high | XM0 or P2; slices in every receipt; slice ceiling in G-IMG; plan §10.3 lever 2 |
| R13 | UBoot64's CRT is not the generic type | unknown | E-X10; if not generic, UBoot64 is not supported and B2 uses DMBoot, GUI64 and u-shell |
| R14 | Wildcard scans of large directories cost time | low | Scans run across passes within the 4096-entry limit (3.9 rule 11) |
| R15 | The `$50` HOLD cannot reach REU idle under a running program | low | Bounded retry; nothing applied, `87`; E-X5 decides the cache alternative |
| R16 | `control_clk` (+0.288 ns PAL) fails after the UCI buffer port, the STATUS2 shadow, the NET registers and the bridge are added | medium | Registered buffer output and the one-wait-state fallback for window reads (4.1.4); the existing repair flow for the shell clock |
| R17 | DNS from the fixed source port 68 is easier to spoof | low on a home LAN | Random 16-bit ID, server and question match (plan §9.5) |
| R18 | A copied SD card makes two boards share a MAC | low to medium (users clone cards) | FI-3; `:net` shows the MAC and its source |
| R19 | The W5100S RST# floats during a PL reload | unknown | Full reset hold after every configuration; E-X14 |

---

## 7. Tests and gates

Small tests run before any full Vivado build (AGENTS.md).

### 7.1 RTL benches (XSim, one PASS line each, `scripts/run_xsim_tests.sh`)

| Bench | Content |
|---|---|
| T-U1 `hdl/sim/tb_c64_uci.sv` | Block against a SystemVerilog reference model: every control byte (256) × state (4) × ABORT_P × DATA_ACC × CMD_BUSY; PUSH latch rules; command fill 0, 1, 895, 896, 897 (overflow); `$DF1D` writes while CMD_BUSY = 1 not stored and fill count unchanged; fill count kept across later validates of a multi-block reply; reply lengths 0, 1, 512, 895, 896 and status 0, 1, 255, 256; one pop per read, none without data; no cycle shows a data state while a head is still being fetched. Abort cases: ABORT in Idle completes in the block (ABORT_P never 1 in Idle, fill count 0, idle-abort count +1); ABORT then DATA_ACC in Data Last, and `$06` in one write, complete in the block; ABORT in Data Last, DATA_ACC, command bytes written, the late op 3 arriving mid-bytes, then PUSH: the full command is latched and the op 3 is dropped; ABORT+PUSH in one write from Idle. Op tags: seq and epoch; an op built before a reset is dropped; reset, then ABORT in Idle, then PUSH runs with no op needed; reset, PUSH, validate, ABORT in Data Last, then op 3 with the new epoch clears ABORT_P. Client scripts as C64 write/read sequences: xlar54 `uii_abort(); uii_sendcommand()`, oscar64 `uii_detect()` then a PUSH, HyperSpeed abort in Data Last. `op_ack`, dropped flag; epoch across resets |
| T-U2 oracle | Upstream `command_protocol.vhd` with `command_if_pkg.vhd`, `io_bus_pkg.vhd`, `slot_bus_pkg.vhd` as a test-only import pinned to `d43546fe` (`third_party/ultimate_reference`, new manifest entries, `licenses/files.csv` rows). These files carry no per-file notice, and the repository licence is GPL-3.0 [V 1541ultimate LICENSE.txt; the four files have no header]. Lockstep on the common subset. Deviations X1-X5, X8, the in-block abort and the busy-write rule (3.5) asserted explicitly |
| T-U3 `hdl/sim/tb_c64_uci_transport.sv` | BRAM with independent clocks at random phase ratios; C64 `$DF1D` writes during CMD_BUSY while the PS reads the command words: copy intact; the early-read path (or the wait-state fallback) delivers data on the `reg_rd_en` edge; op mailbox busy → bad write; STATUS then STATUS2 read as one snapshot while `status_i` changes every cycle (UA counters toggling); sub-word and burst accesses to the window get SLVERR |
| T-U4 `hdl/sim/tb_c64_uci_wiring.sv` | Adapter decode sweep `$DE00-$DFFF` × UCI on/off × REU on/off × UA on/off × CRT none/generic/other type/loading × REU DMA: one owner per address (plan §6.3); the generic CRT keeps the UCI active, every other type suspends it; the REU never sees `$DF18-$DF1F` with the UCI on; DMA reads never pop; open bus with REU and UCI off. HOLD: set while a program runs an REU STASH/FETCH loop; the invalidate is posted only when paused and REU IDLE; no deadlock. `$50` case: `C64_EXPANSION_CONFIG` bits 3-4 changed while HOLD is set and paused, with the REU loop idle: no DMA starts mid-change, UA appears only after the release (also covers E-X6 for the `$50` path) |
| T-U5 G-STK | `tests/c64_ua/stock_bus.csv` bit-exact with the UCI off, in compare mode: the checker receipt records the source hashes, and the manifest is never edited [V scripts/test_c64_ua_stock_bus.py:126-137]. A new variant with the UCI on: identical except the rows at `$DF1B-$DF1F`, `1f 00 c9 00 00` expected in the PAL rows and in the NTSC rows [V tests/c64_ua/stock_bus.csv:29-33, 4592-4596]. `$DF1B` reads `C64_UCI_CONTROL[4:0]`, reset 31 (3.2, 3.13) |
| T-U6 turbo | T65 programs at indices 0, 1 and 15: `LDA $DF1E` loops, `INC $DF1E,X` (two pops), `LDA $DEFF,X` page-cross dummy into I/O1 (no pop), `STA $DF1D,X` (dummy read of `$DF1D`, no effect); X-T1 pacing includes `$DF18-$DF1F`; X-DET UA detection with the UCI on |
| T-U7 (XM5b) | Load kind 0x17 writes without reset; FREEZE hold; RAM read into the buffer; REU DMA timing unchanged (`reutiming` bench) |
| T-U8 (only with the 3.8.6 alternative) | REU-cache bench: an invalidate posted during an active DMA completes once posted writes drain; the waiting request is served from fresh lines; then G-STK and G-REU0 again |
| T-NET1 `hdl/sim/tb_multitini_w5100s_bridge.sv` (XM4, plan §9.9) | As the plan lists, plus: write data held one cycle past WR# (FI-1); the cycle shape stated in the XDC comment of 3.10.7 (c); `RUN` = 0 mid-access ends it with strobes high and the enable off |
| T-NET-top (XM4, proposal) | Top elaborated with `NET_BRIDGE` = 0: every `eth_*` output is today's idle constant [V multitini_one_top.sv:2329-2334]; with `NET_BRIDGE` = 1: every `eth_*` pin is driven or captured by an IOB-attributed flop (FI-2) |

Runner: `scripts/test_c64_uci.py`, mirroring dev7's `scripts/test_c64_iec9.py`.

### 7.2 Host tests (CMake, `tests/host/`, and Python runtime tests)

| Test | Content |
|---|---|
| `test_c64_uci.c` | Engine against a C model of the block: ordering; ABORT in Busy, Data Last, Data More and Idle; idle-abort count consumed (abort hook) before the next dispatch; every op carries seq and epoch; reset, then ABORT in Idle, then PUSH: no stall; reset, PUSH, ABORT in Data Last: op 3 clears ABORT_P with no further PUSH; reset epoch mid-handler; an epoch change during the command copy discards the copy; torn STATUS/STATUS2 read (the model changes STATUS2 between the reads; the engine reads STATUS first and gets the shadow); op wait; null command; no-reply bit; overflow; short commands from the shadow (OPEN_FILE attr 1 `README`, then a 2-byte READ_DATA returns data); buffer ops issue only aligned 32-bit single accesses (a mock MMIO asserts width and alignment); per-pass budget; deferred replies |
| `test_c64_uci_dos.c` | Every DOS command on a temporary directory: path rules, aliases, `..` at the root, PETSCII mapping, wildcards (`/usb*/DMBCFG.CFG`, `GAME*.PRG`, `?AME.D64`, first match in FatFs order, alias order at the root, literal create target), `/Temp` outside the media root and emptied at core start, protected paths, root below the package directory, COPY fix, empty and over-4096-entry listings, handle limit, USB export, GET_TIME with a valid and an invalid RTC (string and `88`), SET_TIME ranges and the 23-byte reply, LOAD/SAVE_REU through a fake REU host. Sidecar cases (P21): create `DMBCFG.CFG` in the root through `/usb*/` → allowed; write `GAME.CFG` beside `GAME.PRG` → `98`; rename `X.TXT` to `GAME.CFG` beside `GAME.CRT` → `98`; delete `GAME.USR` beside `GAME.PRG` → `98`; read `GAME.CFG` → allowed; write `/temp/uci_config.cfg` → allowed. G-IEC9 runs again with the changed callback |
| `test_c64_uci_control.c` | IDENTIFY; GET_HWINFO: sub 0 product name; `$01` in legacy modes 0/2/3 gives two frames of type `$83` at the addresses of 3.8.3; `$02` → `81` before XM7a; at XM7a with a SID-map fixture: `$01` in U64 shape (configured base, `$0000`/`$80` for an unmapped SID 1 or 2, at most 4 frames), `$02` lists every SID (≤ 51 bytes); other subs → `81`. DRVINFO with and without drive 9. LOAD_CONFIG through an overlay stub: a title-overlay REU key survives a `$50` file that does not name it; a file with one BAD_VALUE line applies nothing; `Command Interface` ignored; with a generic CRT attached, `RAM Expansion Unit=Enabled` or UA `Enabled` → `89`, nothing applied; the MMIO order HOLD → PAUSE seen → REU IDLE → expansion write → release; REU not idle within 1 s → `87`, nothing applied; at XM7 two `$50` files, the second with only a pan key, keep the first file's addresses. REU `$08/$09` sources and errors, length < 5 → `81`; `$32/$33` → `00,OK`; FREEZE and REBOOT post supervisor actions; unsupported codes |
| `test_c64_uci_net.c` | Fake `mt_net_*`: open/close/read/write, `FF FF` idle on a live socket, 40 ms deferral, fourth open `85`, ids never 0, close on reset, a socket closed by FIN, RST or TIMEOUT answers `00 00` + `01` once and then `FF FF` + `02,NO DATA: 9`, link loss keeps sockets, lease loss closes them, a pending lookup at expiry → `84`, SET_IPADDR survives a C64 reset, errno values 116, 104 and 118, DNS `84`, no interface `83`, length checks, truncated datagram, listener |
| T-NET2-T-NET6 (XM4, plan §9.9) | As the plan lists, plus: chip init writes RMSR/TMSR `$55` and the MAC; DHCP renew and rebind timing with clamped short leases; DNS loop guard, TC handling and the 200 ms switch wait; ephemeral port selection skips ports in use; `network.ini` journal recovery; MAC regeneration on a changed CID (if FI-3 is adopted) |
| T-NET7 (XM4) | The System page tests live in `tests/host/test_system_frontend.c` [V exists], because the page is built in `system_frontend.c` (FI-4): the two network rows at their indices; every existing System and About item keeps its action after the re-index (a table of expected actions by index); rows disabled without `NET_CAPS`; count ≤ `MT_MENU_MAX_SYSTEM_ITEMS`; status texts fit |
| T-NET8 (XM4) | As the plan lists, plus: SD export refuses settings writes; the chip-error retry and its bound |
| `test_c64_reu_host.c` (extend) | Step API with and without pause, UA hold and invalidate hold; `finish()` after a partial copy always drains and invalidates (ABORT, reset, quiesce); hold retry when the REU is not idle under the hold; invalidate failure → reload required |
| `test_supervisor_settings.c` | Byte 26 bit 4 round trip; at XM5 the assertion covers bits 0-5 and the future-bit mask is `0xC0` (4.6); schema-10 fixture (G-SET) |
| `test_core_frontend.c` | Row 15 placement and caps gating; the largest row set fits 20 (plan §5.1) |
| `test_c64_features.c` | UCI caps at ABI 0.26; package and live minimum |
| `tests/test_c64_uci_runtime.py` | `main.c` functions with mocked MMIO (pattern `tests/test_c64_expansion_runtime.py`): `change_c64_uci()` saves stored and writes effective; quiesce points; reset branch; REBOOT, FREEZE, MOUNT and UMOUNT run as posted supervisor actions after `c64_uci_service_poll()` returns, never inside it; quiesce and notify while such an action is pending. REBOOT: `set_core_reset_request(true)` then `(false)`, never `restore_selection_settings()`; with a CRT attached, no content load and the overlay unchanged; the stored drive 8 name is not re-mounted. docs/C64_TITLE_CONFIG.md T-RT4 checks the overlay side of the same event |
| `tests/test_menu_runtime.py` | Row 15 applies while paused |
| `tests/test_interface_spec.py` | New UCI and NET constants (G-SPEC) |

Optional: a register-accurate UCI device model in `scripts/c64sim` to run 6502 client routines (xlar54 `uii_*`, HyperSpeed-style abort) before the board [V scripts/c64sim exists].

### 7.3 Board tests (C64 PAL first; NTSC after it closes at 64x)

Third-party programs stay under `build/`, out of git, after a licence check. Every UCI run is inside `:debug on` with `:uci` logged, plus one run without a session (plan §13 G-DBG).

| # | Test | Software | Pass |
|---|---|---|---|
| B0 | Register suite at 1 MHz and 64x | `ucitest.prg` (ours; source in the repo, assembled at test time): `$DF18-$DF1F` values, ECHO 896, abort in every state, `\|=` patterns, `INC $DF1E,X`, the xlar54 sequence `uii_abort(); uii_sendcommand()`, an oscar64-style cold start after a C64 reset (`uii_detect()` then identify) | all checks; no ABORT_P wait over 2 ms in the two client sequences |
| B1 | G-REU0 with the UCI Off and On (REU on, UA off) | Expand by Bonzai disks 1 and 2; reudiag `reutiming` | identical to UCI Off; `:uci` push sequence, idle-abort count and fill count logged before and after, so any `$DF1C`/`$DF1D` write by the title is reported |
| B2 | DOS | xlar54 u-shell; GUI64; UBoot64 (only if its CRT is generic, E-X10); DMBoot | browse, cd, load, mount drive 8; `/usb*/` configuration files saved and read back |
| B3 | Clock | GEOS with the ultimateRTC driver (user-supplied GEOS) | time set at boot |
| B4 | SID list | SID Detector II in legacy modes 0/2/3; at XM7a the SID map at 4 (and later 8) SIDs with SID Detector II and the barryw SDK | addresses and types as in 3.8.3; at XM7a SID Detector II still reads its 2-4 frames from `$01`, and `$02` lists every SID |
| B5 | Freeze | DolphinDOS 2 F7 (user-supplied KERNAL) | menu opens; C64 resumes |
| B6 | Abort race | oscar64 library demo, cold starts ×20 | identify always answered; no start waits for ABORT_P |
| B7 | REU load under DMA | a test PRG (ours) that runs an REU STASH/FETCH loop in its IRQ while UCI LOAD_REU loads 1 MB, ×20 | no freeze, no `reload_required`, data verified after each load |
| B8 | `$50` live apply | a test PRG (ours) that writes a `.cfg` to `/temp` and sends `$04 $50` while UA plays: REU on → off → on; then with a generic CRT attached and REU Enabled | no glitch beyond the hold; `89` under the CRT and nothing changed |
| B9 | REBOOT | a test PRG that sends `$04 $06` with a title overlay active and with a generic CRT attached | C64 restarts; overlay and cartridge stay; drive 8 keeps its image |
| B10 | Network Off | UltimateTerm, u-chat | clean `83`, no hang |
| B11 | Network On (XM4 passed) | UltimateTerm to a telnet BBS; u-chat; c64-wireguard if a server is available; a test server that sends RST | connect, idle poll, disconnect, reset closes sockets; on RST and on server close the client sees EOF and does not hang; a short cable pull keeps the session |
| B12 | Exhaustion | `ucitest.prg` network section | fourth open `85` |
| B13 | Listener | u-echoserver | echo works; closed on reset |
| B14 | 64x robustness (#242) | u-chat at 64 MHz | address shown, chat works |
| S1 (XM5b) | SoftIEC | HyperSpeed KERNAL (user-supplied) with drive 9 | LOAD, SAVE, OPEN/CHKIN/CHKOUT, abort mid-stream |
| C1 | Cartridge | with the UCI On, load a generic 8K CRT, then a type with I/O (for example Action Replay) | generic: the UCI answers identify; other type: the row shows suspended and `$DF1C` is cartridge or open bus |

Network service board tests are the plan's N-B1-N-B7 (plan §9.9). This design proposes four more:

| # | Test | Pass |
|---|---|---|
| N-B8 | Pins in an image without the bridge, and in the C64 image with Network Off (scope or logic analyser) | RST# low; CS#, RD#, WR# high; data released, as today |
| N-B9 | Lease expiry with UCI sockets open: stop the DHCP server | at expiry the address drops, UCI sockets report closed once, a pending lookup gives `84`; a new lease when the server returns |
| N-B10 | Throughput: UCI TCP read at 64x | bytes per second recorded |
| N-B11 | A copy of the SD card in a second board (if FI-3 is adopted) | the second board generates its own MAC |

### 7.4 Gates

- **XM4 (G-NET, plan §9.9)** ships only when T-NET1-T-NET8, the build checks and N-B1-N-B7 (with N-B8-N-B11 if adopted) pass, together with G-SPEC, G-DBG and G-IMG (the plan's ceilings or the P2 policy, the coverage gate, the IOB check, the crossing review of 4.2). G-STK, G-REU0 and G-LOCK run too, because the C64 image is rebuilt.
- **XM5 (G-UCI, plan §14)** ships only when T-U1 to T-U6, the UCI host tests, G-STK, G-LOCK, G-TURBO (as the plan defines it; T-S1c is only planned), G-AUD, G-SET, G-DBG, G-SPEC, G-IEC9 (shared protection callback) and G-IMG pass, and B0-B9, B10 and C1 pass on the board. With the 3.8.6 alternative, T-U8 and a second G-REU0 run join the list. B11-B14 gate the Network target only; if XM4 is not done, `$03` ships reporting 0 interfaces.
- **XM5b** adds T-U7 and S1, and re-runs G-REU0 and G-IEC9.
- **XM7a** adds B4 in map mode and the `$02` host cases.
- Hardware support is claimed per target and per network function only after its board tests pass (AGENTS.md).

---

## 8. Open questions and decisions

### 8.1 The plan and this design

Already in the plan (no change needed): `C64_UCI_STATUS2` at 0x134 and `C64_UCI_RAM_READ` at 0x138; the epoch tag in HANDSHAKE `[31:29]`; `HOLD` in XM5; `HANDSHAKE_BUSY`; `RESET_VALUE` with `C64_UCI_CONTROL` = 0x1F and the 120 → 136 / 228 → 292 → 301 mailbox layouts (plan §3.5); the UCI with a generic cartridge (P41); `$DF1B` = 9 only once the SoftIEC target exists; `/Temp` → `<package directory>/uci_temp`; real-sidecar protection (P21); the LOAD_REU path without menu pause or UA hold (plan §7.4); the one REBOOT definition (plan §7.3); GET_HWINFO `$01`/`$02` (P37); `$50` under HOLD and B9; the U64 `FF FF` idle read (P52); the buffer at one RAMB36 tile; the stock-trace compare mode with the manifest never edited; the dev7 baseline, XM0 and P2; decision records Q23 and Q25 and the D-ranges.

Open frame items this design raises:

| # | Plan text | Problem | Proposal |
|---|---|---|---|
| FI-1 | Plan §9.3: the ported sequence, as its XDC comment now states (corrected 2026-10-24 from "holds [eth_a, eth_cs_n and write data] through [the strobe] and for 5 recovery cycles") | In the Appletini sequence that the bridge ports, CS# rises with the strobe and the data output enable drops on the same edge as WR# [V uthernet2_card.sv:729-815]; only `eth_a` is held through recovery. Write data then has no designed hold at WR# rising | Hold the data enable one recover cycle (7.5 ns); use the comment of 3.10.7 (c); T-NET1 asserts both |
| FI-2 | Plan §9.2: "Every output, the data output enable and the data input capture are IOB registers" | No enforcement is named, and the false paths of §9.3 hide any skew if a flop is not packed | `(* IOB = "TRUE" *)` on the flops (precedent `multitini_video_test.sv:53-57`) and a C64-image build check that fails when an `eth_*` port is not driven or captured by an IOB-packed cell |
| FI-3 | Plan §9.7, P54: the generated MAC is stored and "never regenerated while the key exists" | A copied SD card carries the MAC to a second board | Store a generated MAC as `mac_auto=` with `mac_card=` (CRC32 of the CID); regenerate when the CID differs; `mac=` written by the user always wins. **USER**, with P54 |
| FI-4 | Plan §9.9 T-NET7 named `tests/host/test_core_frontend.c` | The System page is built in `firmware/common/system_frontend.c` and tested by `tests/host/test_system_frontend.c` [V both exist] | Put T-NET7 in `test_system_frontend.c`. Applied to plan §9.9 on 2026-10-24 |
| FI-5 | Plan §9.8 status texts | No text for a runtime SET_IPADDR address; no way to clear it from the menu | `192.168.1.90, set by C64 program`; Enter clears it and restarts DHCP |

### 8.2 User decisions

The plan numbers them (plan §16); its recommended default is this design's recommendation unless the row says otherwise.

| P | Question | Recommendation | Note for this design |
|---|---|---|---|
| P2 | Timing policy | XM0 before XM3 | XM4 and XM5 need it as much as XM3 does (0.6) |
| P5 | W5100S bridge images | C64 image only | — |
| P6 | HTTP | XM8 | 3.10.8 |
| P8 | UCI C64 IRQ | deferred | needs the island acknowledge class [V island_map.sv:72-75] |
| P9 | `$D038/$D036` unlock | left out | 3.12 |
| P20 | Title files may enable the Command Interface | yes, named in the status line | 3.1 |
| P21 | C64 writes to title sidecars | refused for real sidecars only | 3.9 rule 8; host cases in 7.2 |
| P34 | Emulated U64 sockets | as docs/C64_MULTI_SID.md decides | changes the `$01` frame count only |
| P37 | GET_HWINFO | `$01` U64 shape; `$02` every SID from XM7a | 3.8.3 |
| P40 | XM5 content | DOS 1/2, Control subset, Network on XM4, SoftIEC "not loaded", HTTP empty | 0.4 |
| P41 | UCI with a cartridge | active with a generic CRT only | E-X10 decides whether UBoot64 benefits |
| P42 | UCI DOS root | `content_root`, below the active package directory | 3.9 |
| P43 | Aliases and wildcards | `/SD`, `/Usb0-2`, `/Temp`, `*`, `?`; `/Temp` volatile | 3.9 rules 5 and 11 |
| P44 | Home directory | folder of the title launched last this session, else the root | answers #724 |
| P45 | SET_TIME | allowed, validated (2000-2099) | 3.8.2 |
| P46 | SET_IPADDR | runtime only; kept across C64 resets | 3.10.3; FI-5 adds the status text |
| P47 | Product name | `MULTITINI ONE` | 3.8.3 |
| P48 | TCP listener | XM5 if PR #98 confirms the replies (E-X11), else XM8 | 3.10.6 |
| P49 | RAM bytes at `$DF18-$DF1A` | no | REU-visible reads |
| P50 | Control `$08/$09` source | the REU image last loaded from the menu this session | overlay preload keys later |
| P51 | FREEZE | opens our menu | DolphinDOS 2 |
| P52 | Idle READ_SOCKET | keep `FF FF` + `02,NO DATA: 11` | 3.10.3 |
| P53 | `network.ini` name and default | as named; Off until G-NET passes, then DHCP | 3.10.7 (h) |
| P54 | MAC source | generated once from SD CID, timer and XADC, stored | add FI-3, so a copied card does not copy the MAC |
| P55 | FTP SD service | not planned; later option, default Off | 3.10.9 |
| — | XM5b (RAM DMA, FREEZE hold, SoftIEC) | yes, after XM5 passes on the board | HyperSpeed needs LOAD and SAVE together; not numbered in the plan |

### 8.3 Open items [O]

| # | Item | Plan experiment |
|---|---|---|
| 1 | PR #98 reply bytes for `$12-$15` | E-X11 |
| 2 | Superloop pickup latency, typical and worst case | E-X8 |
| 3 | W5100S datasheet: MACRAW on socket 0 only; the UDP and MACRAW RX headers; UDP broadcast with SIPR set; DHCP on a UDP socket with source 0.0.0.0; how a refused connect (RST) shows in Sn_IR/Sn_SR; RMSR/TMSR encoding for `$55`; RTR/RCR defaults and the resulting connect timeout; RD#-to-data time against 112.5 ns; write hold after WR#; reset pulse and ready time against 10 ms and 61 ms | E-X3 |
| 4 | Board pull-ups on RST# and INT# during PL configuration | E-X14 |
| 5 | RAMB mapping of the asymmetric dual-clock buffer | E-X4 |
| 6 | `c64_clk` and `control_clk` slack after the UCI and bridge logic | XM4 and XM5 builds |
| 7 | GP0 memory attributes: is DSB plus a read-back enough to order buffer writes before the op write? | E-X15 |
| 8 | T65 at turbo forwards every dummy I/O cycle | T-U6 |
| 9 | Whether Expand by Bonzai or reudiag read or write `$DF1B-$DF1F` | B1 |
| 10 | UBoot64's CRT type (EXROM/GAME and hardware type); only type 0 keeps the UCI | E-X10 |
| 11 | Does the whole-core pause stop an REU DMA mid-transfer, and does REU IDLE arrive under the hold? If not, take the cache RTL alternative of 3.8.6 | E-X5 |
| 12 | The lwIP errno for a refused connect on a U64 (104 expected) and for a UDP send to an unreachable host | plan §17 item 10 |
| 13 | Length of the invalidate hold on the board, and whether the short audio stop is audible | B7, B8 |
| 14 | SD driver access to the card CID, and PS-XADC reads (MAC seed, FI-3) | E-X13 |
| 15 | GP0 cost of one `NET_ACCESS` byte (estimate 0.4-0.6 µs) | XM4 bench on the board |
| 16 | Firmware RAM for the UCI and network buffers | linker map |
| 17 | Whether the P21 stem rule needs the reverse check: a program that creates `GAME.CFG` first and `GAME.PRG` later | docs/C64_TITLE_CONFIG.md |

---

## 9. Work steps in order

Sizes: S ≤ 1 day, M 2-5 days, L 1-2 weeks [I]. Milestones per plan §12.1. Every HDL step waits for XM0 or the P2 policy.

| Step | Milestone | Content | Depends on | Size |
|---|---|---|---|---|
| 1 | XM1 | Option registry row, bounds-checked append, overlay plumbing for bit 5 (plan work; the UCI row only here) | — | S |
| 2 | XM3 | 0.26 spec content for the UCI and the network (plan §3) | plan review | S |
| 3 | XM4 | Plan steps N1-N7 (plan §9.10, about 19-27 days), with the proposals FI-1, FI-2, FI-4 and FI-5 inside N2, N3 and N6, and FI-3 inside N6 if adopted | 2, XM0 or P2 | — |
| 4 | XM4 | N-B8-N-B11 with the plan's N-B1-N-B7 | step 3 | S |
| 5 | XM5 | `multitini_c64_uci.sv` (in-block abort, epoch-tagged ops, head fetch before the state switch, busy-write rule) + T-U1 reference-model bench | 2 | L |
| 6 | XM5 | T-U2 oracle import (pinned, test-only) | 5 | S |
| 7 | XM5 | Transport, AXI early read (or the wait-state fallback), top window, control regs (STATUS2 shadow, CONTROL bit 10 HOLD), mailbox widths and `RESET_VALUE` + T-U3 | 5 | M |
| 8 | XM5 | Adapter wiring (generic-CRT rule, HOLD into the pause path) + T-U4, T-U5 (both traces), T-U6 turbo | 7 | M |
| 9 | XM5 | Firmware engine `c64_uci.c` (command shadow, 32-bit buffer ops) + `test_c64_uci.c` | 2 | M |
| 10 | XM5 | DOS targets, `mt_uci_fs_t`, wildcard walk, `/Temp` folder, sidecar cases, `sd_storage` additions + host tests; G-IEC9 again | 9 | L |
| 11 | XM5 | REU step API with the invalidate hold + host tests; DOS `$21/$22`, Control `$08/$09`; E-X5 on the first build, else the cache RTL alternative + T-U8 | 9 | M |
| 12 | XM5 | Control target: LOAD_CONFIG with the HOLD sequence and the cartridge rule (needs XM2), REBOOT, FREEZE, and the legacy part of `mt_c64_sid_hwinfo()` (modes 0/2/3) + host tests | 9, XM2 | M |
| 13 | XM5 | Platform service, posted supervisor actions, `main.c` glue, settings bit and mask step, menu row, `:uci` + runtime tests | 9-12 | M |
| 14 | XM5 | C64 PAL and NTSC builds; utilization (LUT, FF, slices, BRAM, DSP) and timing receipt per clock group; crossing review (4.2) | 8, 13 | M |
| 15 | XM5 | Board: B0-B10, C1 | 14 | M |
| 16 | XM5 | Network target on XM4 + `test_c64_uci_net.c`; listener if E-X11 is resolved | XM4, 9 | M |
| 17 | XM5 | Board: B11-B14 | 16, step 4 | M |
| 18 | XM5 | Docs: this document's support tables updated from the board results; plan §18 rows; docs/C64_EXPANSIONS.md; docs/C64_SD_STORAGE.md | 15 | S |
| 19 | XM5b | Load kind 0x17, FREEZE hold, RAM read register + T-U7 | 15 | M |
| 20 | XM5b | SoftIEC target on drive 9 + host tests; `$DF1B` = 9 | 19 | L |
| 21 | XM5b | Board: S1; G-REU0 and G-IEC9 again | 20 | M |
| 22 | XM7a | GET_HWINFO `$01` in map mode and `$02` from the committed SID table; B4 in map mode | multi-SID | S |
| 23 | XM8 | HTTP target; listener if not in XM5; IRQ if wanted | XM5 | L |

XM4 can run in parallel with the XM5 PL work (steps 5-8). Each HDL step records its utilization, slices included, and its timing delta (plan §13 G-IMG). Small benches pass before any full build (AGENTS.md).

---

## 10. Review

Two reviews changed this design before it entered the repository.

First review (33 issues, each checked against the cited source):
- Abort handling: in-block completion of aborts with no open transaction, the idle-abort count, the fill count cleared on the ABORT write, and epoch-tagged ops replaced a rule that hung xlar54 clients and stalled oscar64 after every reset.
- REU loads: the HDL hold around drain and invalidate, after the REU cache deadlock with the C64 running was found; cancel paths always invalidate.
- Protocol details: the STATUS2 shadow, the head fetch before the state switch, `$DF1D` bytes not stored while CMD_BUSY, the 32-bit PS access rule, `$DF1B` = 31 in the UCI-on trace.
- Firmware: posted supervisor actions for REBOOT, FREEZE, MOUNT and UMOUNT; the command shadow for short commands; U64 wildcards and the root aliases; `/Temp` outside the media root.
- Targets: closed-socket reporting for every close cause; SET_IPADDR kept across C64 resets; GET_TIME always returns a string; SET_TIME ranges; `$32/$33` answer OK; newlib errno numbers; the UCI active with a generic cartridge.

Second review (across the four designs and the plan):
- Budget and timing rebased on the released dev7 image (`3df6d04`, `4D00C707`): 116 slices of headroom, fast-island +0.030 ns under a one-build waiver; per-clock slack added, including `control_clk` at +0.288 ns and the window-read fallback.
- GET_HWINFO: `$01` in the U64 shape, the full list on `$02` from XM7a, the legacy part delivered in XM5.
- REBOOT: one definition, shared with the plan and docs/C64_TITLE_CONFIG.md.
- LOAD_CONFIG: REU, UA and SID-map changes under HOLD, `87` when the hold fails; REU or UA `Enabled` blocked under a cartridge; per-field SID merge.
- Sidecar protection limited to real sidecars, with host cases for `DMBCFG.CFG` and `GAME.CFG`.
- Network: the XM4 design now lives in plan §9; this design aligned its socket-0 schedule, DHCP, DNS, MAC, settings, status rows and test names with it, kept the details the UCI needs (3.10.7), and raised five open frame items (8.1).
- The stock-trace wording (compare mode, manifest never edited), the mailbox widths and `RESET_VALUE`, and the byte-26 mask step at XM5 (`0xC0`).
