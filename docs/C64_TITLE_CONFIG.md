# Multitini One C64: per-title `.cfg` files (design)

Design only. Nothing here is implemented or tested.

- Date: 2026-10-24.
- Branch `c64-turbo` at `c7c2fba` (M0.7.0dev6). The dev7 work this design was first written against is now commit `3df6d04` (M0.7.0dev7, C64 image `4D00C707`, ABI 0.25). Every repository anchor below was re-checked at `3df6d04`.
- This is one of five documents for the Ultimate 64 style extensions:
  - docs/C64_U64_EXTENSIONS_PLAN.md (called **XP** below): overview, the shared frame (register, settings and menu allocations, the stored/effective model, the option registry, gates) and the milestones XM0-XM8. XP sections are cited by number with their topic, for example "XP §8.1 (stored/effective model)";
  - docs/C64_SUPERCPU_DETECT.md (**SCPU**);
  - docs/C64_MULTI_SID.md (**MSID**);
  - docs/C64_UCI.md (**UCI**);
  - docs/C64_TITLE_CONFIG.md (this document).
- Other repository documents: TD = docs/C64_TURBO_DESIGN.md (cited by section), UAD = docs/C64_ULTIMATE_AUDIO_DESIGN.md, ISS = docs/C64_TURBO_U64_ISSUES.md. `main.c` means `firmware/platform/xilinx/main.c`.
- **Citation base.**
  - Repository paths are relative to the repository root, at `3df6d04`.
  - `1541ultimate <path>:<lines>` = GideonZ/1541ultimate at `d43546fe` (GPL-3.0). Facts only; no code is copied. Short names used below: `configio.cc` = `software/userinterface/configio.cc`; `config_menu.cc` = `software/userinterface/config_menu.cc`; `config.cc` = `software/components/config.cc`; `pattern.cc` = `software/components/pattern.cc`; `u64_config.cc` = `software/u64/u64_config.cc`; `c64.cc` = `software/io/c64/c64.cc`; `c64_subsys.cc` = `software/io/c64/c64_subsys.cc`; `control_target.cc` = `software/io/command_interface/control_target.cc`; `filetype_*.cc` = `software/filetypes/filetype_*.cc`.
  - `1541u-documentation <path>` = GideonZ/1541u-documentation at `2da134b31b54`.
  - Third-party `.cfg` files are cited by their published source (section 2.7). Their bytes were checked during design. They are not committed (section 7.1, U18).
  - Files under `build/` are generated and not under source control, so those figures cannot be checked from the repository.
  - Design-stage research notes are not cited. A claim that rests only on such a note is tagged [I].
- **Tags.** [V] verified in source (path:line). [D] documented (doc:section or line). [R] user report or third-party file. [I] inference. [O] open: needs a measurement or a decision. **USER** marks a decision for the user; the text gives the recommended default and the reason.

Nothing in this document claims hardware support.

---

## 0. Summary

### 0.1 What the user sees

- A file with the title's name and the extension `.cfg`, placed beside the title, sets options for that title only. It is never saved to `settings.bin`. The next title without such a file runs with the saved settings again.
- Files written by an Ultimate 64 or C64 Ultimate for the 3.15 "Automatic per-title configuration" work unchanged. Multitini options without a U64 equivalent go in a `[Multitini]` section, and from XM7 in `[Multitini SID]`.
- Triggers: the Program and Cartridge browsers, UART `:load`, the three Mount & Run actions, and the stored-cartridge restore. A plain drive 8 mount does not apply a file.
- New Options rows: "Title .cfg files" (argument 17, default On), "Revert title settings" (18), "Save settings for this title" (19, XM2d).
- Rows the file sets show ` (title)`. A change to such a row, also from a hotkey, lasts for the title and is not saved.
- The status line says what the file did. The UART prints one line per skipped line.
- With no `.cfg` anywhere, the MMIO writes and `settings.bin` are the same as today. One menu change is visible: with title files On, the Cartridge row stays enabled while the REU is on, and the refusal moves to the launch (section 3.12).

### 0.2 What this document owns

This document owns (XP §8.6, option registry): the `.cfg` reader, the dialect and allow-list, the title overlay and its lifetime, the launch-flow hooks, the "Title .cfg files", "Revert title settings" and "Save settings for this title" actions, the launch status line, and the parser reuse by UCI Control `$50`. Each feature document owns its own option rows (SCPU: SuperCPU Detect; UCI: Command Interface; MSID: multi-SID). This document gives the `.cfg` mapping those rows must register.

### 0.3 Firmware shape

- XM2 is firmware only: a portable parser and resolver, one RAM overlay between the stored settings and the debug layer, a hook in the content service, three Options rows and byte 26 bit 5 of `settings.bin`. No register, ABI, RTL or image change.
- Later milestones add keys: SuperCPU Detect (XM3), Command Interface and UCI `$50` LOAD_CONFIG (XM5), multi-SID (XM7).
- XP's XM0 (recovering the dev7 PAL fast-island slack, +0.030 ns under a one-build waiver [V build/C64-4D00C707-070dev7-SD/README.txt:8]) gates XM3, not XM2: XM2 builds no image.
- User decisions are in section 8.2. The main ones: U1 (default On), U5 (owned edits stay in the overlay), U6 (Options → Reset per source kind), U22 (C64 software may not write title sidecars), U23 (SuperCPU Detect in a kept overlay).

---

## 1. Goal and scope

### 1.1 Goal

When the user launches a title and a file with the same name and the extension `.cfg` lies beside it, Multitini applies the settings in that file for that title only. It never saves them to `settings.bin`. The next title without a file runs with the saved settings again.

Files made by an Ultimate 64 or C64 Ultimate for 3.15 "Automatic per-title configuration" work unchanged. Our own options without a U64 equivalent go in a `[Multitini]` section, which a U64 skips as an unknown store.

### 1.2 In scope

| Milestone (XP §12.1) | Content |
|---|---|
| XM1 (XP, prerequisite) | Stored/effective split, option registry, menu limit 20. Section 4.1 lists what this design needs from XM1 |
| **XM2** | Reader, dialect, overlay, launch hooks, rows 17-18, byte 26 bit 5, the sidecar write rule for drive 9 (U22, if adopted), and keys for options that exist today: turbo (Turbo Control, CPU Speed, Badline Timing), REU on/off, Ultimate Audio, legacy SID layouts, joystick port, disk auto-slow, cartridge turbo and expansion memory speed (the last two only where caps allow), System Mode and REU Size as checks |
| XM2d (inside XM2 or right after) | "Save settings for this title" (needs menu argument 19, section 8.1 F-e) |
| XM3 | `SuperCPU Detect (D0BC)` key, registered by SCPU |
| XM5 | `Command Interface` key; UCI Control `$50` LOAD_CONFIG through this parser; the sidecar write rule for the UCI DOS targets |
| XM7 | SID addressing, UltiSID, mixer and `[Multitini SID]` keys, registered by MSID; per-field SID overlay |

### 1.3 Out of scope

- Loading ROMs or cartridges named in a file (Kernal/Basic/Char ROM, Cartridge). Never.
- REU preload named in a file. Possible later option (U14).
- Switching between the PAL and NTSC images from a file. Never automatic (section 3.13).
- Network keys. Never applied from a title file.
- A browser "Load Settings" action for arbitrary `.cfg` files. A UART dry run exists for authors (section 3.17).
- Folder-wide default files (THEC64 style). Not requested (U16).

### 1.4 Repository facts this design starts from

- TD §0.6 Q10 rejected sidecar `.cfg` files; TD §6.2 and §6.3 repeat it [D TD §0.6 Q10, §6.2, §6.3]. The user has reversed it. XP §3.6 names the new decision record Q22.
- dev7 is committed as `3df6d04`: drive 9 (a PS-backed IEC device) and C64 media subfolders [V git log]. The released image is `4D00C707`, ABI 0.25 [V build/C64-4D00C707-070dev7-SD/README.txt:1; spec/interface_v0.json:5]. XM2 is firmware only, so it runs on that image. There, SuperCPU Detect (K4) and Command Interface (K10) report UNAVAILABLE.
- No `.cfg` reader exists. `.cfg` classifies as `MT_CONTENT_UNKNOWN` [V firmware/common/content_type.c:38-77], so `collect_menu_file()` never lists it [V main.c `collect_menu_file()`].
- The expansion handler starts from the live register and saves from it [V main.c:4190 `old_config = shell_read(MT_REG_C64_EXPANSION_CONFIG)` in `change_c64_expansion()`]. The menu shows the live register [V main.c:2609-2614 in `supervisor_menu_view()`]. XM1 fixes this (XP §8.2 rule 2).
- **Joystick leak paths.** The joystick port is saved from the live USB HID value in three places: `remember_c64_input_settings()` [V main.c:2300-2309], `remember_manual_selection()` [V main.c:2311-2326] and the auto-boot toggle [V main.c:5117-5121], all through `usb_hid_service_joystick_port()`. A title value set through `usb_hid_service_set_joystick_port()` would reach `settings.bin` at the next keyboard-map toggle or auto-boot change [I]. XP §8.2 rule 3 names the first; the other two are found here. This design fixes all three (section 4.3).
- Turbo already separates stored and effective values: `configure_c64_turbo()` computes from stored settings, then `mt_c64_debug_effective_overlay()` adds the debug layer [V main.c:2376-2390; firmware/common/c64_debug.c:60-72].
- The CRT-versus-REU refusal reads the live register in five places:
  - the service [V firmware/platform/xilinx/c64_content_service.c:328-331, `c64_content_service_reu_enabled()`];
  - `handle_c64_content_load()` [V main.c:1372-1373];
  - the menu `MT_MENU_ACTION_LOAD_CONTENT` C64 branch [V main.c:4774-4777];
  - the stored-CRT restore [V main.c:2446-2448];
  - the Cartridge browser guard `MT_MENU_ACTION_BROWSE_CARTRIDGE` ("Turn REU off before loading a cartridge") [V main.c:4378-4381]. The first frame draft's B1 list missed it; XP §6.4 B1 now includes it (section 8.1 F-g).
- The firmware clamps only UA for a cartridge [V firmware/common/c64_features.c:119]. The HDL clamps both: `reu_active = reu_enable && !cart_loaded && !cart_loading`, `ua_active = ua_enable && reu_active && ...` [V hdl/core/c64/multitini_c64_adapter.sv:608, 611].
- `mt_c64_turbo_detail()` replaces a byte 27 whose bits 7:6 are 0 with `0x41` (Manual 2 MHz) [V c64_features.c:85-88], and `mt_c64_turbo_config()` applies it first [V c64_features.c:93]. Zero is the default of byte 27 [V firmware/platform/xilinx/supervisor_settings.h:65; supervisor_settings.c:234 memsets the defaults].
- Neither CRT detach path [V main.c:1391-1400 `handle_c64_cart_detach()`; main.c:4845-4870] nor the fast-ROM toggle (argument 0) [V main.c:4970-5041; `stage_c64_rom_profile()` main.c:1126-1173] writes the expansion or turbo registers. Only `MT_MENU_ACTION_RESET_CORE` calls `restore_selection_settings()` after its reset [V main.c:5084-5093].
- Options → Reset asserts the core reset through `set_core_reset_request()`, waits 1 ms, releases it, then calls `restore_selection_settings()` [V main.c:5084-5093]. `set_core_reset_request()` flushes and ejects drive 8 at assert [V main.c:767-786; c64_d64_service.c:790-800], and `service_runtime()` remounts the same image after the release [V main.c:490-505; c64_d64_service.c:825-841].
- A C64 reset does not detach a cartridge. The content loader clears `cart_loaded` only on its own reset (the adapter's hard `reset`, not the C64 reset request), a memory fault, a new CRT stream, a detach or a load error [V hdl/core/c64/multitini_c64_adapter.sv:427; hdl/core/c64/multitini_c64_content_loader.sv:398-421, 461-465, 555-560, 593-598, 772-796].
- A same-name sidecar exists for Vectrex: the last extension is replaced, the file is optional, and a bad file gives a warning without failing the game [V firmware/common/vectrex_overlay.c:31-57; firmware/platform/xilinx/vectrex_content_service.c `load_sidecar()`].
- C64 media browsers use folders, under the package `content_root` [V firmware/common/core_frontend.c:476-479; c64_content_service.c:144-165 `content_path()`, `c64_content_service_set_media_root()`].
- The status line holds 63 characters: `supervisor_status_text[64]`, and the setters truncate [V main.c:190, 1976-1987].
- Drive 9 asks its protected-path callback before every open, read or write, and before remove, rename, mkdir and commit [V firmware/platform/xilinx/c64_iec_service.c:194-200, 253-272, 274-282]. Today the callback means "no access at all" [V main.c:445-449 → `c64_d64_service_path_protected()`].

---

## 2. U64 behaviour (facts)

### 2.1 File format

| Fact | Tag |
|---|---|
| `[Store Name]` line, then `Item=value` lines, a blank line after each store | [V 1541ultimate configio.cc:198-228] |
| Writer uses LF only, no header, no BOM, the primary store name, and the enum label text without the display format | same |
| Enum labels keep their padding: `CPU Speed= 1`, `Vol Master= 0 dB` | [V 1541ultimate u64_config.cc:321-322; R Heartbeat file, section 2.7] |
| Store aliases accepted on read: `C64U Specific Settings` for `U64 Specific Settings`; `Cartridge and ROM Settings` for `C64 and Cartridge Settings`; matching is case-insensitive | [V 1541ultimate u64_config.cc:906, 943; c64.cc:132-133; config.cc:196-208] |
| The SuperCPU item is spelled `SuperCPU Detect (D0BC)`; the docs write `($D0BC)` | [V 1541ultimate u64_config.cc:394; D 1541u-documentation config/turbo_mode.rst:63] |
| `DMA Load Mimics ID:` has a colon in its item name | [V 1541ultimate c64.cc:96] |

### 2.2 Reader rules (`ConfigIO::S_read_from_file`, `S_read_store_element`)

| Rule | Tag |
|---|---|
| CR is dropped anywhere; LF ends a line | [V configio.cc:247-254] |
| Line buffer 320 bytes (319 characters) at d43546fe. v3.15a used 128 bytes | [V configio.cc:233]; v3.15a [I] |
| An over-long line fails the load; if it starts with `[`, the following items are skipped | [V configio.cc:255-259, 267-275] |
| **A last line without LF is never parsed** (EOF returns before the pending line is handled) | [V configio.cc:260-265] |
| Comment only when the **first** character is `#` or `;` | [V configio.cc:276-278] |
| Header: first character `[`, name up to the first `]`; an unknown store is a warning and its section is skipped; a `[` without `]` leaves `store = NULL`, and every later item then fails with "Not inside valid store" | [V configio.cc:279-298, 310-313] |
| A UTF-8 BOM makes the first header an item outside a store | [I from configio.cc:279, 310-313] |
| `=` must fall in the first 40 characters; the name is trimmed; an empty name (also a line of spaces) is MALFORMED | [V configio.cc:359-375] |
| Unknown item: logged, UNKNOWN, not an error | [V configio.cc:381-390] |
| VALUE: `sscanf("%d")`, **no range check** | [V configio.cc:395-403] |
| STRING: taken verbatim, truncated to the item's maximum | [V configio.cc:404-414] |
| ENUM: case-insensitive, outer spaces ignored, only labels in `min..max` | [V configio.cc:324-350, 415-431] |
| The value goes straight into `item->value`; the change hook is not called; a changed value sets `staleEffect` and `staleFlash` | [V configio.cc:399-402, 409-413, 419-423] |
| Duplicates: last one wins | [I: lines apply in order] |
| Read-only (disabled) items can be written from a file | [V no `isEnabled()` check in configio.cc:352-434] |

### 2.3 Applying a loaded file

| Fact | Tag |
|---|---|
| `S_load_and_effectuate` walks **every** store and effectuates each one that is stale, whatever the parse result | [V configio.cc:455-467] |
| Interactive load shows a popup; `mode = 1` (auto-load) shows nothing, not even errors | [V configio.cc:447-454, 469-475] |
| Flash is never written by the load; `staleFlash` stays set | [V configio.cc:436-478] |
| Leaving the config menu with any flash-stale store asks "Save changes to Flash?" (or saves silently) | [V 1541ultimate config_menu.cc:169-201] |
| The U64 reset task re-applies the RAM values of the U64, socket, mixer, UltiSID and addressing stores on every C64 reset, so a loaded file stays in force across resets until reboot, "Reset from Flash" or another file | [V u64_config.cc:1035-1050] |
| History: #765 effectuated only the stores the file loaded; commit 03284765 removed that again; upstream's own test records 19 stores effectuated on a C64 Ultimate 1.2.0 for a one-store file and is kept manual. #787 and #789 report and change the stale marking | [V 1541ultimate tests/e2e/filemanager/cfg_partial_effectuate_test.py:1-22]; #787, #789 [R upstream issue tracker, not re-read here] |

### 2.4 Same-name auto-load (3.15 and d43546fe)

Name derivation: `set_extension(filename, ".cfg")` replaces the text after the last `.`, in the same directory, with `mode = 1` [V configio.cc:480-492; pattern.cc:276-306]. The buffer is `4 + strlen(filename)`, so a name without an extension loses its last character (`GAME` → `GAM.cfg`) [V pattern.cc:296-301 with configio.cc:482-485].

| U64 action | `.cfg` applied | Order | Tag |
|---|---|---|---|
| D64/D71/D81/DNP "Run Disk" | yes | mount drive A → cfg → `LOAD"*",8,1` and RUN | [V 1541ultimate filetype_d64.cc:112-127] |
| D64 "Mount Disk" variants | no | | [V filetype_d64.cc:73-81] |
| G64/G71 "Run Disk" | yes | mount → cfg → load and run | [V filetype_g64.cc:87] |
| CRT "Run Cart" | yes | `load_crt` into cart memory → cfg → `C64_START_CART` | [V filetype_crt.cc:64-81] |
| PRG "Run", "Load" (file system) | yes: `<name>.cfg`, else `<name>.usr` | cfg → DMA load (resets) | [V filetype_prg.cc:164-169] |
| PRG inside a D64: "Mount & Run", "Real Run" | no (mount-bit branch) | | [V filetype_prg.cc:154-163] |
| PRG "DMA" (raw) | no | | [V filetype_prg.cc:170-171] |
| TAP "Run Tape" | yes | | [V filetype_tap.cc:235] |
| REST `run_prg`/`load_prg` by file | yes (`.cfg`, then `.usr`) | | [V filetype_prg.cc:184-191] |

- The `.usr` fallback lets a disk carry a config as a USR file named like the program. "Run" on a PRG listed inside a D64 browses the image as a directory, so `GAME.USR` in the image is found [I from filetype_prg.cc:164-169 and the CBM file-type extensions].
- The release note says "When loading a D64, ... CRT or PRG, the firmware looks for a matching `.cfg` or `.usr` file and automatically applies the settings it contains" [D 1541u-documentation howto/release_3.15.md:64-72]. In code it means the Run actions only (table above).
- No public issue asked for the feature before it was written, and upstream has no end-to-end test of auto-load [I].

### 2.5 C64 Ultimate (Commodore firmware)

- Writes `C64U Turbo Registers` where the U64 writes `U64 Turbo Registers` [R slesinger/doom v1.0.0 `release/doom.cfg`; V 1541ultimate tests/e2e/io/c64/doom_release_test.py:35-41].
- Public U64 firmware rejects `C64U Turbo Registers` ("not a valid choice") and applies the other lines [V u64_config.cc:325; configio.cc:428-431].
- C64U hardware is U64-II class, so `CPU Speed` uses the U64-II list (`64`, no ` 5`) [I; V u64_config.cc:321-322].
- Whether C64U firmware auto-loads same-name files is not confirmed by any public source [O].

### 2.6 UCI Control `$04 $50` LOAD_CONFIG

- Default file `/temp/uci_config.cfg`; bytes 2 and later of the command give a path [V control_target.cc:565-568].
- Same reader. Effectuates stale stores **only if** the parse was fully OK [V control_target.cc:578-595].
- But the reader has already written the item values into RAM [V configio.cc:399-423]. The reset task re-applies RAM values on every C64 reset [V u64_config.cc:1035-1050]. So after a failed `$50` the bad file's values can still take effect at the next reset [I from the cited code].
- Reply: the parse log, at most 511 bytes. Status `00,OK`, `88,CANNOT OPEN CONFIG FILE`, `89,CONFIG FILE HAD ERRORS` [V control_target.cc:34-35, 597-611].
- REBOOT (`$04 $06`) runs `start_cartridge(NULL)`: a restart with the configured cartridge that keeps the RAM settings [V control_target.cc:134-137; c64_subsys.cc:279-285].

### 2.7 Real files examined (bytes checked during design)

| File | Size | Facts | Tag |
|---|---|---|---|
| The Tuneful Eight `tt8-ultimate.cfg`, in the U64 firmware 1.24 package (CSDb release 189578, `u64_124.zip`, beside `tt8-ultimate.prg`, 27,536 bytes) | 763 bytes | LF only (33 LF, no CR), no BOM, ends with LF; sections Audio Mixer, UltiSID Configuration, SID Addressing; `UltiSID Range Split=1/4 (A7,A8)`, UltiSID 1 `$D400`, UltiSID 2 `$D420`, sockets Unmapped, `Ext DualSID Range Split=A7` | [R, bytes checked] |
| Heartbeat Soundtracker `Heartbeat_UE2_64MHz.cfg` and `..._16MHz.cfg`, attached to 1541ultimate issue #922 | 1,593 bytes each | LF only (77 LF, no CR), no BOM, double blank lines in places; adds Speaker Mixer, SID Sockets Configuration, `Turbo Control=TurboEnable Bit`, `CPU Speed=64` or `16`, `Badline Timing=Enabled`, `SuperCPU Detect (D0BC)=Enabled`, REU 16 MB, UA Enabled, `Command Interface=Enabled`; same SID block as TT8 | [R, bytes checked] |
| Doom `release/doom.cfg` (slesinger/doom v1.0.0, commit 53430ced; written for a C64U) | 396 bytes | no blank lines between sections; `System Mode=PAL`, `Turbo Control=C64U Turbo Registers`, `Cartridge=`, `Kernal ROM=`, REU 16 MB, UA, Command Interface, and a `[Network Settings]` section that enables DMA, FTP and Web services. Not named after the program, so 3.15 does not auto-load it | [R; no local copy was kept] |

---

## 3. Design (software-visible behaviour)

### 3.1 Model

The layers of XP §8.1 (stored/effective model):

```
effective = debug_overlay( title_overlay( stored ) )
```

- `stored` = `settings.bin` (+ `c64_sid.bin` from XM7). Only `stored` is ever serialised.
- `title_overlay` = one RAM object: a mask of owned **groups** (section 3.8), a value per owned group, the source file name, and the title session it belongs to.
  - The SID group is the one exception. At XM2 it holds one field, the legacy SID mode. From XM7 it is **field-level**: MSID's record `mt_c64_sid_overlay_t`, with the model values, a 60-bit field mask (25 U64-layer, 32 per-SID and 3 output fields) and three class bits ADDR, MIX and MODEL that MSID's overlay rule reads [D MSID §3.2, §3.10]. The SID group counts as owned when any SID field is owned.
- `debug_overlay` = today's `:speed` and debug bits, unchanged.

No new register, bit or ABI step. XM2 is firmware only. It writes existing registers through the existing functions `mt_c64_turbo_config()` [V firmware/common/c64_features.c:89-111], `mt_c64_expansion_restore()` [V c64_features.c:112-125], `set_c64_cpu_turbo()` [V main.c] and `usb_hid_service_set_joystick_port()`.

A **title session** starts with a successful trigger launch (section 3.3) and records the medium's relative path and kind (PRG, CRT, D64). It exists with or without a `.cfg`, and it is what "Save settings for this title" writes beside.

### 3.2 The feature switch

- Options row, argument 17, "Title .cfg files", values `On` / `Off` [XP §5.1, Options arguments].
- Stored in `settings.bin` byte 26 bit 5, **1 = Off**, so 0 (the default and what older firmware writes) means On [XP §4.1, settings allocation; XP P10].
- Off means off: no lookup, no SD access for `.cfg`, no overlay. Switching to Off ends an active overlay at once (section 3.10). Switching to On does not touch the running title; it acts from the next launch.
- **USER** (XP P10, U1): default On. Reason: with no file present nothing changes, and the U64 has no switch at all.

### 3.3 Triggers

| Our action | Code path today | U64 counterpart | `.cfg` looked up | Decision |
|---|---|---|---|---|
| Program browser Enter; UART `:load X.prg` | `handle_menu_action()` `MT_MENU_ACTION_LOAD_CONTENT` C64 branch; `handle_c64_content_load()` | PRG Run | `<stem>.cfg`, else `<stem>.usr`, beside the PRG | yes |
| Cartridge browser; UART `:load X.crt` | same | Run Cart | `<stem>.cfg` beside the CRT | yes |
| Mount & Run Enter (drive LOAD launcher) | `MT_MENU_ACTION_MOUNT_AND_RUN` | Run Disk | `<d64 stem>.cfg` beside the mounted D64 (`c64_d64_service_mounted_name()`) | yes |
| Mount & Run Right (typed LOAD) | `MT_MENU_ACTION_LOAD_DISK_PROGRAM` → `launch_disk_program_native()` | Run Disk | same | yes |
| Mount & Run Left (direct stage) | `MT_MENU_ACTION_DIRECT_DISK_PROGRAM` | Run Disk (closest) | same | yes |
| Drive 8 browser mount; UART `:mount` | `MT_MENU_ACTION_MOUNT_DRIVE8` → `mount_c64_disk()` | Mount Disk | no; status hint if a `.cfg` exists (section 3.12) | **USER** U2, recommend no |
| Boot auto-mount of `drive8.d64`; restored D64 at activation or Reset | `restore_selection_settings()` | none | no | **USER** U3, recommend no |
| Restored CRT at activation or Options → Reset | `restore_selection_settings()` CRT branch | none (a U64 reboot reads flash) | yes, as a relaunch with `apply = false` | **USER** U4, recommend yes (section 8.1 F-b) |
| Load into REU; UART `:reuload` | `MT_MENU_ACTION_LOAD_REU` | none | no | no |
| C64 software loads (drive 8, drive 9, UCI DOS) | C64-initiated | none | no | no |
| UCI Control `$06` REBOOT (XM5) | UCI posted action (UCI §4.5) | REBOOT | no (section 3.10) | no |
| UCI Control `$50` LOAD_CONFIG (XM5) | UCI service | `$50` | the file the command names | yes, merged (section 3.15) |

Reasons:
- PRG, CRT and Mount & Run all reset the C64 and start a title, like the U64 Run actions [V main.c launch branches].
- A plain mount does not reset [D docs/C64_SD_STORAGE.md:114-115: "At a game's disk-change prompt, mount the replacement through `Drive 8` without restarting the game"]. Applying turbo or REU changes under a running program at a disk-swap prompt would break it. U64 parity.
- The stored-CRT restore is a relaunch of the same title. Without its file, a CRT whose `.cfg` turned the REU off cannot be restored at all (section 8.1 F-b).
- G64 has no run action today: Mount & Run lists the programs of a mounted D64 only [V firmware/common/core_frontend.c:85]. So a G64 title gets no `.cfg`. U15 proposes a "Run disk" action.

### 3.4 File lookup

1. Take the title's path relative to the package media root (the `content_root` set by `c64_content_service_set_media_root()` in `supervisor_activate()`) [V c64_content_service.c:158-165; main.c `supervisor_activate()`].
2. Derive the sidecar in the last path component: replace the text from the last `.` with `.cfg`. A name without an extension gains `.cfg` (this fixes the U64 truncation, section 2.4). A leading dot belongs to the stem. A component that ends in `.` or a space is rejected. Same rule as `mt_vectrex_overlay_sidecar_path()` [V vectrex_overlay.c:31-57].
3. Join with the media root through `mt_media_browser_join()`, which rejects absolute paths, `.`/`..` and empty components [V firmware/include/multitini_media_browser.h:23-26]. Lookups therefore never leave the root.
4. Read with `mt_sd_storage_read_file_optional_max()` into a static 16 KiB buffer. A missing file means no `.cfg` and no message. `MT_SD_TOO_LARGE` means the file is refused with a notice [V firmware/platform/xilinx/sd_storage.h:15, 69-70; sd_storage.c `mt_sd_storage_read_file_max()` returns TOO_LARGE].
5. PRG titles only: if `<stem>.cfg` is missing, try `<stem>.usr` (U64 parity [V filetype_prg.cc:164-168]). For `.usr` only, every refusal (over 16 KB, not text, SD error) is one UART line and no status text, and the file is used only if it holds at least one section header the dialect knows (section 3.6.1). Otherwise it is treated as absent. Reason: real CBM USR files are binary (GEOS) or PETSCII text with CR line ends; an ordinary PRG launch must not get a warning for them [I].
6. Case: FAT and exFAT match names case-insensitively, so `DRILLER.CFG` serves `Driller.prg` [I FatFs behaviour].
7. One file per launch. No folder default, no `<name>.<ext>.cfg` (U21).
8. Shared stems: `Game.d64`, `Game.prg` and `Game.crt` in one folder share `Game.cfg`, as on the U64 [I from `set_extension`]. Documented, not changed.
9. Length: a derived path longer than the content path buffer (`MT_C64_CONTENT_PATH_CAPACITY` = 2 × 255 + 2 = 512 [V c64_content_service.c:13; multitini_media_browser.h:8]) or a component over 255 bytes means no lookup and one UART line.
10. If "Save settings for this title" exists (XM2d), the lookup first calls `mt_sd_storage_journal_recover(<stem>.cfg, <stem>.cfg.mtj, ...)` [V sd_storage.h:75-78], so an interrupted save is completed or rolled back before reading.
11. Busy check. The journal functions use raw `f_open` without the managed-file busy check [V sd_storage.c:355-470, f_open at :379 and :464], and FatFs locking is off [V generated BSP build/p070d7r2/.../include/ffconf.h:335 `FF_FS_LOCK 0`]. Drive 9 can hold `<stem>.cfg` open through the managed API, because its path protection covers only the mounted D64 [V main.c:445-449; c64_d64_service.c:162-176], and it lists every file, so an existing `GAME.CFG` can be opened by name [V firmware/common/c64_iec_dos.c:186, 587-598]. Drive 9 itself probes exclusivity before each journal operation [V c64_iec_service.c:114-143, 274-302]. The lookup does the same: before the journal recover and the read, it opens `<stem>.cfg` and `<stem>.cfg.mtj` with `mt_sd_file_open(path, "r+")` and closes them, as `exclusive_target()` does. `MT_SD_FILE_BUSY` → the file is not used; status `... not used (open on drive 9)` (section 3.12). No drive 9 quiesce: a launch refused at step 4 of section 3.9 must not close the running program's files.

**Browser visibility.** `.cfg`, `.usr` and `.cfg.mtj` stay hidden in the Program, Cartridge and Drive 8 browsers (they classify as unknown, as today). **USER** (U19), recommend hidden: they are not launchable, and the U64-style "Load Settings" browser action is out of scope.

### 3.5 Parser

Portable C in `firmware/common/c64_title_cfg.c`, no heap, no recursion, host-tested. Input is a byte buffer; output is a candidate (section 3.7) and a report.

| # | Rule | U64 | Ours | Reason |
|---|---|---|---|---|
| P1 | Size | no limit | at most 16,384 bytes, else the whole file is refused | bounded buffer; a full U64 dump is about 5-9 KB [I: the 319 item definitions at d43546fe total about 8.9 KB across all build variants]; the first frame draft said 4 KB; XP §7.1 now says 16 KiB (section 8.1 F-d) |
| P2 | Text check | none | refuse the file if it holds a NUL, any byte below 0x20 other than TAB, CR and LF, or 0x7F | a binary `.usr` must never be parsed |
| P3 | BOM | breaks the first section | a leading EF BB BF is skipped | fix |
| P4 | Line ends | CR dropped, LF ends | LF ends a line; CR LF is one end; a CR not followed by LF also ends a line | fix: PETSCII and classic-Mac writers use CR only, including a C64 program writing a `$50` file; the U64 reads such a file as one line and applies nothing [V configio.cc:247-254] |
| P5 | Last line | dropped without LF | parsed | fix |
| P6 | Line length | 319 (d43546fe) / 128 (v3.15a) | 319 characters; a longer line is reported and skipped, and parsing goes on; a long line starting with `[` skips its section | d43546fe parity, without failing the file |
| P7 | Blank or whitespace-only line | whitespace-only is MALFORMED | skipped | fix |
| P8 | Comment | first character `#` or `;` | first non-blank character `#` or `;` | superset, harmless |
| P9 | Inline comment | none | none: `Enabled ; x` is a bad value | parity |
| P10 | Header | `[` first, name to `]` | first non-blank `[`, name to `]`, trimmed, case-insensitive; text after `]` ignored, and non-blank text there is one INFO entry "text after section name ignored" | parity plus trim; the INFO keeps a damaged file visible |
| P11 | Header without `]` | every later item fails | one report line; items up to the next good header are skipped | fix |
| P12 | Item before any header | "Not inside valid store" per line | one report line for the run of such items | fix (less noise) |
| P13 | Name | before the first `=`, within 40 characters, trimmed, case-insensitive | same | parity |
| P14 | Missing `=` or empty name | MALFORMED | MALFORMED, reported | parity |
| P15 | Enum value | case-insensitive, outer space ignored | same | parity |
| P16 | Numeric value | `%d`, no range check | decimal only, the full string must parse, range-checked against the key's table | fix |
| P17 | Duplicates | last wins | last wins, reported at INFO level | parity |
| P18 | Unknown section | warning, section skipped, not an error | INFO, section skipped | parity |
| P19 | Unknown item in a section we read | UNKNOWN, not an error | warning, counted as skipped (likely a typo or newer firmware) | visibility |
| P20 | Non-ASCII bytes 0x80-0xFF | stored as given | allowed in comments and in ignored items; never match an honoured key or value | the FAT code page is 932 [V generated BSP build/p070d7r2/.../include/ffconf.h:124]; honoured labels are ASCII |
| P21 | Strings | verbatim | XM2 honours no string-valued key | no file names are acted on (section 3.16) |

Report classes (one entry per line that matters):

| Class | Meaning | Counted as |
|---|---|---|
| APPLIED | honoured key, valid value | applied |
| ADJUSTED | applied with a change: speed 5 → 4 MHz, image speed limit, mode downgraded for missing caps, "cartridge runs at 1 MHz" (section 3.7) | applied (note) |
| CHECKED | check-only key that matches (System Mode equals the image, REU Size 16 MB, any REU Size while the REU is off), or an off-state request for a feature this image lacks (section 3.7) | not counted |
| NOT_APPLICABLE | known U64 item or section with no meaning here (list in section 3.6.3) | not counted; UART only |
| NEVER | section never applied from a title file (network, UI) | not counted; the UART line says so |
| UNAVAILABLE | honoured key, but this image or milestone lacks the feature | skipped |
| BLOCKED | value breaks a combination rule (XP §6.4 B8) | skipped |
| BAD_VALUE | value not in the key's table, or out of range | skipped |
| UNKNOWN_ITEM | item not known in a section we read | skipped |
| MALFORMED | no `=`, empty name, over-long line, bad header | skipped |
| INFO | duplicate, unknown section, item outside a section | not counted |

The report keeps counts and the first 16 non-applied entries `{line, class, item ≤ 40 chars, value ≤ 40 chars, reason code}`. Entries are ASCII only; other bytes print as `?`.

### 3.6 Dialect

#### 3.6.1 Sections

| Section as written | Accepted aliases | Content |
|---|---|---|
| `U64 Specific Settings` | `C64U Specific Settings` | turbo, SuperCPU Detect, System Mode, Joystick Swapper |
| `C64 and Cartridge Settings` | `Cartridge and ROM Settings` | REU, REU Size, UA, Command Interface |
| `SID Addressing` | — | SID group |
| `UltiSID Configuration`, `Audio Mixer` | — | SID group from XM7 (MSID §4.5); NOT_APPLICABLE before |
| `Multitini` | — | our options without a U64 equivalent |
| `Multitini SID` | — | XM7: MSID's per-SID and output keys (MSID §4.5). Before XM7: one UNAVAILABLE entry "needs multi-SID" for the section, counted once as skipped; its items are not parsed |

#### 3.6.2 Honoured keys

Group names are defined in section 3.8. "View" means the field of the stored settings that the group overrides; the effective value then goes through the existing register writers.

| # | Section | Item (aliases) | Values accepted | Group | Effect | From |
|---|---|---|---|---|---|---|
| K1 | U64 Specific | `Turbo Control` | `Off`, `Manual`, `U64 Turbo Registers`, `C64U Turbo Registers`, `TurboEnable Bit` | TURBO_CONTROL | Off: turbo enabled = false (profile byte 243 view); the mode field keeps the normalised stored mode (section 3.8), never 0. Others: enabled = true, mode 1 / 2 / 2 / 3 (byte 27 bits 7:6 view) → `C64_CPU_CONFIG[5:4]` and `CONTROL` bit 5 `C64_CPU_TURBO_ENABLE`. Mode 2 or 3 without `C64_CPU_CAPS` REGISTERS / TURBO_ENABLE → Manual, ADJUSTED (today `mt_c64_turbo_config()` downgrades silently [V c64_features.c:97-98]). Needs `caps.turbo` [V c64_features.c:34]: without it every value but `Off` is UNAVAILABLE and `Off` is CHECKED, because on such an image (ABI ≥ 8) `CONTROL` bit 5 is the Smart 2x switch and argument 1 saves `!CONTROL bit 5` [V main.c:4947-4951, 5044-5057] | XM2 |
| K2 | U64 Specific | `CPU Speed` | integers 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64 (U64 MK1 and U64-II labels), outer spaces ignored, optional trailing ` MHz` | SPEED | index into {1,2,3,4,6,8,10,12,14,16,20,24,32,40,48,64} [V c64_features.c:5] → `C64_CPU_CONFIG[3:0]`, clamped to `C64_CPU_CAPS[15:0]` by `mt_c64_clamp_speed()`. `5` → index 3 (4 MHz), ADJUSTED. Matched by MHz, never by index (MK1 and U64-II indices differ [V u64_config.cc:321-322]). UNAVAILABLE without `caps.turbo` | XM2 |
| K3 | U64 Specific | `Badline Timing` | `Disabled`, `Enabled` | BADLINE | byte 27 bit 5 view → `C64_CPU_CONFIG` bit 6. UNAVAILABLE without `caps.turbo` (`Enabled` is CHECKED there: the stock path has badlines) | XM2 |
| K4 | U64 Specific | `SuperCPU Detect (D0BC)` (`SuperCPU Detect ($D0BC)`) | `Disabled`, `Enabled` | SCPU | `c64_turbo_flags` bit 3 view (byte 26 bit 3 on the wire); `mt_c64_turbo_config()` alone turns it into `C64_CPU_CONFIG` bit 20, gated by `C64_CPU_CAPS2` bit 15 [D SCPU §4.3; XP §3.3, new bits]; never an apply event; UNAVAILABLE without CAPS2 bit 15 | XM3 |
| K5 | U64 Specific | `System Mode` | `PAL`, `NTSC` | — (check) | compared with the active variant id (`pal`/`ntsc`); match = CHECKED; mismatch = UNAVAILABLE "needs the C64 NTSC image" (or PAL); `PAL-60`, `NTSC-50`, `PAL-60/L`, `NTSC-50/L` = UNAVAILABLE. Never switches the image (section 3.13) | XM2 |
| K6 | U64 Specific | `Joystick Swapper` | `Normal`, `Swapped` | JOYSTICK | relative to the **stored** port: Normal = stored, Swapped = 3 − stored → `usb_hid_service_set_joystick_port()`. `WASD ...` values = UNAVAILABLE | XM2 |
| K7 | C64 and Cartridge | `RAM Expansion Unit` | `Disabled`, `Enabled`; `GeoRAM Mode` = UNAVAILABLE | EXPANSION_MEM | expansion byte bit 3 view → `C64_EXPANSION_CONFIG` bit 3. `Enabled` with a cartridge = BLOCKED (section 3.7 step 3) | XM2 |
| K8 | C64 and Cartridge | `REU Size` | `16 MB` = CHECKED; `128 KB` … `8 MB`: CHECKED "only 16 MB" when the resolved REU is off, ADJUSTED "only 16 MB" when it is on (REU state unchanged by this line). Reason: every full U64 dump carries the default `REU Size`, also beside `RAM Expansion Unit=Disabled`, and it must not count as a setting | — (check) | none: the REU is fixed at 16 MB [V firmware/include/multitini_regs.h:286 `MT_C64_EXPANSION_CAPS_REU_16MB`; no size register] | XM2 |
| K9 | C64 and Cartridge | `Map Ultimate Audio $DF20-DFFF` | `Disabled`, `Enabled` | EXPANSION_MEM | byte 26 bit 2 view → `C64_EXPANSION_CONFIG` bit 4; `Enabled` also sets the REU (B3); UNAVAILABLE without UA caps; `Enabled` with a cartridge = BLOCKED (section 3.7 step 3) | XM2 |
| K10 | C64 and Cartridge | `Command Interface` | `Disabled`, `Enabled` | UCI | byte 26 bit 4 view → `C64_EXPANSION_CONFIG` bit 5 [XP §3.3, new bits]; UNAVAILABLE without `C64_EXPANSION_CAPS` bit 5; ignored (INFO) when the file comes through UCI `$50` itself (section 3.15) | XM5 |
| K11 | SID Addressing | `SID Socket 1 Address`, `SID Socket 2 Address`, `UltiSID 1 Address`, `UltiSID 2 Address`, `UltiSID Range Split`, `Ext DualSID Range Split`, `Auto Address Mirroring`, `Paddle Override` | U64 labels [V u64_config.cc:218-226, 265-266, 412-422] | SID | XM2: legacy layouts only, resolved as one value after the whole file is read (section 3.7 step 2) → `C64_EXPANSION_CONFIG[2:0]`; the K11 position for "later line wins" against K17 is the line number of its last present member. XM7: each item sets its field of MSID's U64 layer, in file order (section 3.7 step 2), and every item except `Paddle Override` sets the ADDR class bit [D MSID §3.2] | XM2 (legacy), XM7 |
| K12 | UltiSID Configuration, Audio Mixer | items per MSID §4.5 (`UltiSID n Combined Waveforms`, `UltiSID n Digis Level`, `UltiSID n Filter Curve`, `UltiSID n Filter Resonance`, `Vol Master`, `Vol UltiSid n`, `Pan UltiSID n`, and the socket items if MSID U4 adopts them) | U64 labels | SID (field-level) | each item sets its field of MSID's U64 layer; `Vol UltiSid n` and `Pan UltiSID n` set the MIX class bit, `UltiSID n Combined Waveforms` the MODEL class bit; a `Pan UltiSID n` key clears that group's `pan_from_preset` in the overlay [D MSID §3.2]. Items MSID ignores stay NOT_APPLICABLE | XM7; NOT_APPLICABLE before |
| K13 | Multitini | `Disk auto-slow` | `On`, `Off` | AUTOSLOW | byte 26 bit 0 view (1 = Off) → `C64_CPU_CONFIG` bit 8; UNAVAILABLE without `caps.turbo` (`On` CHECKED) | XM2 |
| K14 | Multitini | `Cartridge turbo` | `Allowed`, `Forces 1 MHz` | CART_TURBO | byte 26 bit 1 view → bit 9; UNAVAILABLE without `C64_CPU_CAPS` bit 21 | XM2 |
| K15 | Multitini | `Expansion memory speed` | `C64U (1 byte per cycle)`, `Follow CPU` | FOLLOW | byte 27 bit 4 view → bit 18; UNAVAILABLE without `C64_CPU_CAPS2` bit 12 | XM2 |
| K16 | Multitini | `Joystick port` | `1`, `2` | JOYSTICK | absolute port | XM2 |
| K17 | Multitini | `SID audio` | XM2: `Single`, `Dual $D420`, `Dual $D500` (our menu labels). XM7 adds MSID's presets `Triple`, `Quad`, `Eight` (alias `Tuneful Eight`); `Custom` is BAD_VALUE [D MSID §3.4, §3.10] | SID | XM2: `C64_EXPANSION_CONFIG[2:0]` = 0 / 2 / 3. XM7: sets every field a preset sets (U64 layer, both `pan_from_preset` flags, every per-SID field), in file order (section 3.7 step 2); a preset that needs more SIDs than the image has follows MSID's missing-parts rule [D MSID §3.3 step 4a] | XM2, XM7 |
| K18 | Multitini SID | MSID's keys: `SID n Address`, `SID n Model`, `SID n Volume`, `SID n Pan` (n = 1-8, value `Follow` allowed), `SID Headroom`, `SID DC Removal`, `SID Resampling` | MSID labels [D MSID §4.5] | SID (field-level) | each key sets its per-SID or output field; such a field keeps its value even when a key-class bit makes the other stored per-SID fields read as Follow [D MSID §3.2] | XM7 |

- Multitini item names stay under 40 characters, so a U64 that ever reads the section would parse them [V configio.cc:359].
- When a U64 key and a Multitini key set the same value, the later line wins (P17). At XM2 this is per group, and the SID group's K11 position is its last present member (K11 row). At XM7 it is per field in file order.
- K14 and K15 need `caps.turbo` as well, because `mt_c64_turbo_config()` returns 0 without it [V c64_features.c:92].
- XP §8.2 rule 9 holds: no debug knob (`c64u_top`, IEC holdoff, static release, diagnostics, capture, peek) is a key.
- "Fast serial ROMs" is not a key (XP §8.6 registry: overlay not allowed, it restarts the C64). U13 keeps it as a possible later key.

#### 3.6.3 Known items that are NOT_APPLICABLE or NEVER

Kept as a table in `c64_title_cfg.c`, so that typos stay visible (P19) while full U64 dumps stay quiet. Names come from 1541ultimate d43546fe and must be re-checked against the pinned upstream when the table is written [O].

| Section | Items or rule | Class |
|---|---|---|
| U64 Specific Settings | `Mouse Mode`, `Mouse Sensitivity`, `Mouse Acceleration`, `Mouse Wheel Sensitivity`, `Mouse Wheel Direction`, `Menu Mouse Navigation`, `UserPort Power Enable`, `Palette Definition`, `Adjust Color Clock`, `Analog Video Mode`, `Digital Video Mode`, `HDMI Scan lines`, `HDMI Scan Resolution`, `Serial Bus Mode`, `SpeedDOS Parallel Cable`, `Burst Mode Patch`, `LED Select Top`, `LED Select Bot`, `Speaker Enable`, `Speaker Volume (SpkDat)`, `SID Player Autoconfig`, `Allow Autoconfig uses UltiSid`, `Power On After Power Loss`, `Wake On Wi-Fi`, `VIC Test Colors`; legacy names commented out at d43546fe but present in older dumps: `Cartridge Preference` (moved to the C64 store) and `Chroma Delay` [V u64_config.cc:366, 370] | NOT_APPLICABLE |
| C64 and Cartridge Settings | `Cartridge`, `Cartridge Preference`, `Bus Operation Mode`, `Bus Sharing - ROMs`, `Bus Sharing - I/O1`, `Bus Sharing - I/O2`, `Bus Sharing - Interrupts`, `Fast Reset`, `Kernal ROM`, `Basic ROM`, `Char ROM`, `Alternate Kernal`, `REU Preload`, `REU Preload Image`, `REU Preload Offset`, `DMA Load Mimics ID:`, `Button order`, `CPU Addr valid after PHI2`, `CPU Addr valid after PHI1` (Ultimate-II/II+ builds [V c64.cc:100-114]), `Force 2 MHz support`, `Kernal Shadow RAM`, `PHI2 edge recovery`, `Perform VIC sync at DMA RUN` | NOT_APPLICABLE |
| Whole sections | `Speaker Mixer`, any `SID Socket N: ...`, `Drive A Settings`, `Drive B Settings`, `SoftIEC Drive Settings`, `Tape Settings`, `Printer Settings`, `LED Strip Settings`, `Machine Monitor Bookmarks`, `Clock Settings`; `UltiSID Configuration` and `Audio Mixer` until XM7; `SID Sockets Configuration` until XM7, and after XM7 too unless MSID decision U4 adopts socket enables [D MSID §4.5] | NOT_APPLICABLE |
| Whole sections | `Network Settings`, `Ethernet Settings`, `WiFi settings`, `Data Streams`, `Modem Settings`, `User Interface Settings` | NEVER: a title file never changes network, streaming or UI state, even after XM4 adds the network (fixes doom.cfg's service switches [R]) |

### 3.7 From parse to candidate

Two passes over the parsed lines:

1. **Collect.** Each honoured line stores its value in the candidate slot for its key (last wins). Check keys and SID members are kept raw, with their line numbers.
2. **Resolve**, in this order:
   1. TURBO_CONTROL, SPEED, BADLINE, SCPU, AUTOSLOW, CART_TURBO, FOLLOW, UCI, JOYSTICK: caps checks (UNAVAILABLE or ADJUSTED).
      - A key for a missing feature whose value is that feature's off state (`SuperCPU Detect (D0BC)=Disabled`, `Command Interface=Disabled` before XM5, `Turbo Control=Off` without `caps.turbo`) is CHECKED, not UNAVAILABLE. The request is already met, and full U64 dumps stay quiet.
      - Cartridge rule: on a CRT launch or CRT restore, or for a `$50` file while a cartridge is attached, on an image without `C64_CPU_CAPS` bit 21, `Turbo Control` (other than `Off`), `CPU Speed` and `Badline Timing` are ADJUSTED "cartridge runs at 1 MHz". Until M7 a loaded cartridge forces exact mode [D TD §1.4], and the release caps have bit 21 clear [V firmware/include/multitini_regs.h:336 `MT_C64_CPU_CAPS_RELEASE 0xF2DFFFFF`]. The values still enter the candidate, so hotkeys, Revert and Save behave the same.
   2. SID group.
      - XM2 rule: if `UltiSID Range Split` is present and not `Off` → UNAVAILABLE "needs multi-SID" (group not set). Otherwise collect the addresses of the present address items that are not `Unmapped`, sockets included (we have no sockets; a socket address states where the title expects a SID [I]). `{$D400}` → Single (0); `{$D400, $D420}` → Dual `$D420` (2); `{$D400, $D500}` → Dual `$D500` (3); any other set → UNAVAILABLE "SID layout not available". `Auto Address Mirroring`, `Paddle Override` and `Ext DualSID Range Split` do not change the legacy result. Then K11 against K17 by position (K11 row).
      - XM7 rule: K11, K12, K17 and K18 lines are applied in file order, each to the fields it sets; a later line overwrites only the fields it touches. The candidate's SID field mask and key-class bits record what the file set. MSID's caps checks apply (8580, IO slots [D MSID §4.5]). The split key is never skipped: on an image with fewer SIDs, MSID drops the missing parts after mirroring and reports them [D MSID §3.3 step 4a].
   3. **Cartridge rule for the expansion keys, first.** On a CRT launch or CRT restore, and for a `$50` file while a cartridge is attached, `RAM Expansion Unit=Enabled` and `Map Ultimate Audio $DF20-DFFF=Enabled` are each BLOCKED (B8). `Disabled` lines stay.
      - For a launch, the BLOCKED lines are removed and the launch goes on. So a full dump beside a CRT with `RAM Expansion Unit=Disabled` and UA `Enabled` gives REU off, as the file asks, and the CRT loads.
      - For `$50`, BLOCKED is an error, so nothing is applied and the reply is `89,CONFIG FILE HAD ERRORS` (section 3.15). Reason: the UCI stays active under a generic 8K/16K/Ultimax cartridge [D UCI §3.1], and the menu refuses the same change while a cartridge is attached ("Eject the cartridge before enabling ..." [V main.c:4199-4203]). Without this block a `$50` file could create the "REU requested with a cartridge attached" state that the menu forbids.
   4. EXPANSION_MEM from the lines left, so that the group always holds UA ⇒ REU (B3 [XP §6.4, blocks]; today's handlers: `reu = !reu; if (!reu) ua = false;` and `ua = !ua; if (ua) reu = true;` [V main.c:4197-4198]). The U64 sets the two enables independently [V c64.cc:312-330]; here the named key decides the unnamed one. "Base" below is the stored value for a launch, and the current effective value for a `$50` file, so that a `$50` file naming one key does not undo the other key of an active title overlay [I]:

      | Lines left | Group value |
      |---|---|
      | both | UA `Enabled` wins: REU on, and a `RAM Expansion Unit=Disabled` line is ADJUSTED "REU on because Ultimate Audio is on". Otherwise as written |
      | REU `Disabled` only | REU off, UA off. When the base UA is on, the REU line carries the note "Ultimate Audio off because the REU is off" |
      | REU `Enabled` only | REU on, UA = base UA |
      | UA `Enabled` only | UA on, REU on |
      | UA `Disabled` only | UA off, REU = base REU |
      | none | group not owned |

3. The candidate owns exactly the groups (and, from XM7, the SID fields) whose resolved value is set. A key whose value equals the stored value is still owned: the title states it, so a later change of the saved setting does not change the title.

The same function serves the launch paths, the UART dry run and UCI `$50`.

### 3.8 Overlay groups and the effective computation

| Group | Stored view it replaces | Coupled with |
|---|---|---|
| TURBO_CONTROL | profile byte 243 (turbo on) and byte 27 bits 7:6 (mode) | — |
| SPEED | byte 27 bits 3:0 | — |
| BADLINE | byte 27 bit 5 | — |
| FOLLOW | byte 27 bit 4 | — |
| AUTOSLOW | byte 26 bit 0 | — |
| CART_TURBO | byte 26 bit 1 | — |
| SCPU (XM3) | `c64_turbo_flags` bit 3 (byte 26 bit 3) | — |
| EXPANSION_MEM | expansion byte bit 3 (REU) and byte 26 bit 2 (UA) | one group, because B3 couples them |
| SID | XM2: expansion byte bits 2:0, one field. XM7: field-level over `mt_c64_sid_model_t` (the `c64_sid.bin` record fields [D MSID §3.2, §4.4]), plus three key-class bits; the legacy expansion byte follows MSID's "nearest legacy mode" rule [D MSID §4.4] | — |
| UCI (XM5) | byte 26 bit 4 | — |
| JOYSTICK | profile byte 241 | — |

There is no separate group for the UltiSID and mixer keys (K12): they are fields of the SID group.

The byte 27 groups (TURBO_CONTROL mode, BADLINE, FOLLOW, SPEED) replace bit fields of the **normalised** stored byte. Stored byte 27 is 0 for every user who never touched a turbo row, and `mt_c64_turbo_detail()` turns any byte with bits 7:6 = 0 into `0x41` [V c64_features.c:85-88, 93; supervisor_settings.c:234]. Laying a SPEED field over a raw 0 would give `0x09`, which normalises back to Manual 2 MHz and loses the file's speed. When TURBO_CONTROL is Off, its mode field is the normalised stored mode, never 0. `_edit()`, the writer (section 3.14) and the menu view use the same rule.

Effective values (portable function, host-tested). It returns two values: **requested** (what the stored settings and the title ask for) and **applied** (what the registers get):

```
P = stored
P.detail = mt_c64_turbo_detail(stored.detail)            (normalise first)
for each group g owned by the overlay:
    replace g's fields in P by overlay.g                 (field-wise; other byte 27 bits stay)
for each SID field f owned by the overlay (XM7):
    P.sid.f = overlay.sid.f                              (key-class bits passed to MSID)
if P.ua: P.reu = true                                    (B3)
requested = P
applied   = P
if cartridge_after:                                      (B1/B2; same as the HDL clamp
    applied.reu = false; applied.ua = false               reu_active = reu_enable && !cart_loaded && !cart_loading,
                                                          V hdl/core/c64/multitini_c64_adapter.sv:608, 611)
C64_CPU_CONFIG       = debug_overlay( mt_c64_turbo_config(applied.detail, applied.flags,
                                                          applied.enabled, caps, previous, apply) )
                       (flags bit 3 = SCPU; mt_c64_turbo_config() alone sets bit 20, SCPU §4.3)
CONTROL bit 5        = applied.enabled                   (set_c64_cpu_turbo); from the overlay only when caps.turbo
C64_EXPANSION_CONFIG = mt_c64_expansion_restore(applied.sid, applied.reu, applied.ua, cartridge_after, caps)
                       | (applied.uci ? bit 5 : 0)       (XM7: bit 6 and the SID table per MSID)
joystick port        = applied.joystick                  (usb_hid_service_set_joystick_port)
```

`cartridge_after` says whether a cartridge is attached once the action completes. The caller sets it:

| Caller | `cartridge_after` |
|---|---|
| CRT launch; CRT restore relaunch | true |
| PRG load, `stage_prg_image`, `prepare_disk_load` (each detaches any CRT [V c64_content_service.c:200-203, 350-363]) | false |
| CRT detach end event (section 3.10) | false |
| Recomputes without a launch: menu edit, Revert, files Off, UCI `$50`, the menu view | `c64_content_service_crt_attached()` |

- `requested` feeds every CRT-versus-REU decision: step 4 (section 3.9), `hook->reu_requested`, the CRT restore check, the Cartridge browser guard, and the RAM expansion and UA rows while a cartridge is attached (section 3.12). Only the register writes use `applied`. Without this split, the clamp would make the REU false for every CRT launch, so the refusal could never fire, and a CRT with no `.cfg` would load while the stored REU was on (B1 lost).
- A PRG-type launch while a CRT is attached commits REU and UA as requested, before `load_image()` detaches the CRT [V c64_content_service.c:194, 200-203]. Between the commit and the detach, the HDL clamp keeps both inactive [V adapter.sv:608, 611]. Committing after the detach instead would not keep the reset held: `c64_content_service_detach_crt()` starts with `release_stale_core_reset()` [V c64_content_service.c:288]. The window (REU enable set while a cartridge is loaded) is new on hardware; R14 and section 7.6 add a bench case [O].
- Stored values obey B3, because the handlers keep UA ⇒ REU [V main.c:4197-4198]. Today's restore drops the global UA before B3 when a CRT is pending [V main.c:2399-2403 `cart_pending` in `configure_c64_expansion()`; c64_features.c:119-121]. The two orders differ only for a profile with REU off while the global UA flag is on, which needs two C64 packages sharing that flag [I]. There the CRT restore is now refused with the usual message; T-RT1 records that case.
- In every state reachable today the stored REU is off whenever a CRT is attached (B1 and the enable refusals [V main.c:1372-1373, 4199-4203]), so `applied` equals the live registers [I]. Section 3.9 uses this for trace identity.
- Apply events: the existing counter rule raises `C64_CPU_CONFIG[15:12]` only when bits 6:0 change and `apply` is true [V c64_features.c:109; c64_debug.c:69-70]. A `.cfg` is an apply source for user launches and Revert, never for the CRT restore relaunch (TD: no apply happens for boot restore [D TD §1.5]). SCPU is never an apply event [XP §3.3, new bits]. The #787 lesson (apply only on a real change) holds by construction.

### 3.9 Apply sequence per launch

The commit runs **after the content file is read and validated and before the load's reset**, with the C64 held in reset while registers change.

New content-service entry points (section 4.4) take a hook:

```c
typedef struct {
    bool reu_requested;             /* requested REU (section 3.8); replaces the live CRT check */
    bool hold_reset;                /* the commit changes a C64-visible register */
    int (*commit)(void *context);   /* writes the changed registers, installs the overlay */
    void *context;
} mt_c64_launch_hook_t;
```

**No hook when nothing changes.** `title_candidate_for()` returns no hook (NULL) when there is no candidate, no active overlay, and every computed register value equals the current one. The old entry points then run unchanged. With title files Off there is never a candidate or an overlay; a hook appears only when a computed value differs from the live one. That needs a state today's firmware cannot reach: a CRT attached with the stored REU on, left behind when files Off or Revert ends an REU-off overlay (section 3.8, R4).

Sequence (PRG, CRT, Mount & Run alike):

| Step | Action | On failure |
|---|---|---|
| 1 | Resolve the title path (existing code) | as today |
| 2 | If title files are On: derive, read, parse, resolve → candidate C and report R (sections 3.4-3.7). No register or settings write | a refused file (too large, not text, busy, SD error) gives C = none plus a notice; the launch goes on |
| 3 | Compute E' = `mt_c64_effective(stored, C, caps, cartridge_after)` (section 3.8). C replaces the current overlay; it is not added to it | — |
| 4 | CRT launch with `E'.requested.reu` → refuse. Files On: `Turn REU off, or disable the REU in Game.cfg`. Files Off: today's `Turn REU off before loading a cartridge`. This replaces the live checks at main.c:1372-1373 and 4774-4777; with files Off, `requested` = stored, which equals the live register in every state today's firmware reaches [I, section 3.8] | old overlay and machine unchanged |
| 5 | `prepare_c64_content_change()` (hide menu, quiesce drive 9, flush drive 8) [V main.c:1332-1347] | as today; overlay unchanged |
| 6 | Hooked load/stage. The service reads and validates the content as today [V c64_content_service.c:185-192] | read, validation or REU failure: return before the hook; overlay and registers unchanged |
| 7 | If `hold_reset`: the service asserts the C64 reset (its own `set_core_reset(true)` [V c64_content_service.c:52-59]). Then `commit()`: the joystick first, then only the registers whose computed value differs from the current one, among `C64_EXPANSION_CONFIG`, `C64_CPU_CONFIG` (apply = true) and `CONTROL` bit 5. The joystick takes part only when the old or the new overlay owns JOYSTICK. Then install C as the overlay and record the title session | the resolver yields only port 1 or 2, and the setter fails only on other values [V firmware/platform/xilinx/usb_hid_service.c:1908-1914]; MMIO writes cannot fail. So the one failure left is the reset command. On any commit error act as in step 8: write effective(stored), clear the overlay and the session, release the reset, return `MT_C64_CONTENT_SERVICE_CONFIG_FAILED`, status `Settings could not be applied; saved settings restored`; nothing streamed |
| 8 | The service continues exactly as today: `release_stale_core_reset()` [V c64_content_service.c:194] releases the hold, a PRG detaches a CRT [V :200-203], then the content streams with the loader's own reset | transfer failure after the commit: clear the overlay and the session, write the stored effective values, status `Load failed; saved settings restored` |
| 9 | `finish_c64_content_change()`; status line with the report summary (section 3.12) | as today |

Why this order:
- The REU, UA, SID layout and UCI are present when the title boots, so start-up probes see them.
- No old code runs before the reset. The hold covers the read of a 2 MB CRT and the commit. After `release_stale_core_reset()` the C64 boots with the new values. If a CRT was attached, that old cartridge can boot with them for a short time: until the detach on a PRG launch [V c64_content_service.c:194, 200-203], or until the first CRT byte on a CRT launch, which detaches it in the HDL [V c64_content_service.c:218-232]. REU and UA stay clamped while a cartridge is loaded [V adapter.sv:608, 611], and a loaded cartridge forces exact speed until M7 [D TD §1.4]. T-RT2 records the gap.
- Trace identity: with no `.cfg` and no previous overlay the hook is NULL (above), so the MMIO write sequence is identical to today's (G-CFG trace test, section 7.4).
- Turbo writes made before the reset land at the end of the 2.0 s reset window, as TD specifies for any write before a reset [D TD §1.5].
- U64 differences: the U64 applies after mounting for Run Disk and after loading the cart image for Run Cart [V filetype_d64.cc:115-118; filetype_crt.cc:69-72]. The visible result is the same; ours runs no old code with the new values before the reset.

CRT restore relaunch (`restore_selection_settings()`, section 8.1 F-b): steps 2-8 with `apply = false` and `cartridge_after = true`. `requested.reu` replaces the existing `c64_content_service_reu_enabled()` check [V main.c:2446-2448]. If title files are Off, or the file is gone, `requested` = stored and the stored REU decides as today ("cartridge restore skipped; turn REU off first").

Mount & Run: the title medium is the mounted D64, and `cartridge_after = false`. Steps 2-4 run immediately before each `prepare_c64_content_change()` call: main.c:4632 (drive LOAD launcher and direct stage) and main.c:3997 (`launch_disk_program_native()`). Not at the top of the `MT_MENU_ACTION_MOUNT_AND_RUN` branch, because that branch calls `launch_disk_program_native()` for a typed LOAD or a PRG without a BASIC start [V main.c:4595-4602], which would read the `.cfg` twice. Step 6 uses the hooked `stage_prg_image` and `prepare_disk_load`.

### 3.10 When the overlay ends

**Rule: every overlay end writes effective(stored).** It runs the commit of section 3.9 step 7 (only changed registers, apply = true), unless a launch commits right after (next trigger launch), or the core stops or reloads (core switch, standby, USB export), where the next activation writes stored values through `restore_selection_settings()` anyway [V main.c `restore_selection_settings()` call sites]. Clearing the overlay without this write would leave the title's turbo, REU, UA and SID values in the registers while the menu shows stored values.

This table is the one definition of the overlay's lifetime. SCPU, UCI and MSID cite it rather than restate it.

| Event | Overlay | Title session | Note |
|---|---|---|---|
| Next trigger launch | replaced by the new candidate, or cleared if none | replaced | XP §8.4 |
| Launch refused or failed before the reset (section 3.9 steps 4-6) | unchanged | unchanged | nothing changed on the machine |
| Launch failed after the commit (step 7 error or step 8) | cleared, stored values written | cleared | the machine holds no title |
| Options → Reset (`MT_MENU_ACTION_RESET_CORE`) | PRG source: cleared; `restore_selection_settings()` then writes stored values [V main.c:5084-5093]. D64 source: **kept** while drive 8 holds a disk (the source or a later swap), cleared when drive 8 is empty. CRT source: cleared, then re-applied by the CRT restore relaunch (keys merged by `$50` are lost) | PRG: cleared; D64: kept while the overlay is; CRT: kept | **USER** U6 (changed after review). `restore_selection_settings()` keeps `profile->drive8_name` mounted [V main.c:2462-2470], so the usual recovery `LOAD"*",8,1` runs the same title, which must keep its file. Status `Core restarted; Game.cfg stays active`, or SCPU's `Core restarted; SuperCPU Detect stays on (title .cfg)` when the kept overlay turns SuperCPU Detect on (section 3.12; U23). CRT per section 8.1 F-b |
| "Fast serial ROMs" toggle (argument 0, restarts the C64) | CRT source: kept; argument 0 never detaches the cartridge, so the same title reboots [V main.c:4970-5041]. D64 source: as Options → Reset (`stage_c64_rom_profile()` remounts drive 8 [V main.c:1152]). PRG source: cleared; after `stage_c64_rom_profile()` succeeds, `configure_c64_expansion()` and `configure_c64_turbo(..., true)` write stored values. Today argument 0 writes neither [V main.c:1126-1173, 4970-5041] | same as the overlay | the restart ends a PRG title |
| Reset by C64 software (`JMP $FCE2` and the like) | unchanged | unchanged | not a firmware event; U64 parity |
| UCI Control `$06` REBOOT (XM5) | unchanged | unchanged | **One definition, shared with UCI §3.8.3 and §4.5** (section 8.1 F-j). REBOOT is a C64 reset through `set_core_reset_request()` (assert, then release), run as a posted supervisor action. It does not call `restore_selection_settings()`, re-reads no `.cfg`, and does not relaunch a cartridge. An attached cartridge restarts with the C64, because a C64 reset does not detach it (section 1.4). `C64_EXPANSION_CONFIG`, `C64_CPU_CONFIG`, `CONTROL` bit 5 and the joystick port keep their values. Drive 8 keeps its image: the reset flushes and remounts that same image, as every C64 reset does [V main.c:767-786, 490-505]; the stored drive 8 name is not restored. Keys merged by `$50` survive. U64: `start_cartridge(NULL)` [V control_target.cc:134-137; c64_subsys.cc:279-285] |
| Cartridge detach (`MT_MENU_ACTION_DETACH_CARTRIDGE`, `:cartoff`) | cleared; the hooked detach (section 4.4) commits effective(stored) with `cartridge_after = false` after a successful detach, inside a reset hold | cleared | today neither detach path writes the expansion or turbo registers [V main.c:1391-1400, 4845-4870]. Whether the C64 restarts at detach is not visible in the loader's detach branch [V hdl/core/c64/multitini_c64_content_loader.sv:555-561] [O]; the reset hold makes the result the same either way |
| Drive 8 eject or mount (disk swap) | **unchanged** | unchanged | keeps multi-disk titles working (section 8.1 F-a) |
| Core switch, `supervisor_activate()`, variant change | cleared | cleared | XP §8.4 |
| Standby (`supervisor_enter_standby()`), power-off | cleared (RAM) | cleared | XP §8.4 |
| USB SD export (`mount_sd_over_usb()`) | cleared | cleared | the core is stopped; files may change |
| "Revert title settings" (argument 18), `:cfg revert` | cleared, stored values written with apply = true (`cartridge_after` = a CRT is attached) | kept | ends **every** overlay, whatever its source and whatever drive 8 holds. It is the documented way to end a kept D64 overlay before booting another disk (SCPU §3.5-3.7 advise it before GEOS or GDOS64). Save stays possible |
| "Title .cfg files" → Off | cleared, stored values written (same) | kept | |
| UCI `$50` LOAD_CONFIG | merged (section 3.15) | unchanged | XP §8.2 rule 5 (section 8.1 F-c) |
| Load into REU, `:speed`, debug commands | unchanged | unchanged | the debug layer sits above the overlay |

### 3.11 Editing while a title overrides a setting

Rule (XP §8.2 rule 7, **USER** U5, recommended): **a menu row or controller action that changes a group (or, from XM7, a SID field) the overlay owns edits the overlay, not `stored`, and makes no settings save.** Groups the overlay does not own edit `stored` and save, as today.

| Action | Owned group | Result |
|---|---|---|
| CPU turbo (arg 1), Turbo on/off (arg 9) | TURBO_CONTROL | overlay enabled/mode change; status `Turbo on for this title (Driller.cfg)` |
| CPU speed (arg 4); controller CPU slower (arg 13) | SPEED | overlay index steps; status `CPU turbo: 40 MHz for this title, now 40 MHz (running)` |
| Controller CPU faster (arg 12) | TURBO_CONTROL and SPEED: with turbo off it sets enabled and Manual, then steps the speed [V main.c:4266-4269] | see the coupling rule below |
| Badline timing (5), Disk auto-slow (6), Cartridge turbo (7), Expansion memory speed (10) | BADLINE, AUTOSLOW, CART_TURBO, FOLLOW | overlay toggles |
| SID audio (2) | SID | overlay cycles. From XM7 the preset sets the fields it defines; those fields become owned |
| SID setup sub-page items (XM7, MSID §4.5) | SID fields | `change_c64_sid_setup(item, delta)` checks ownership per item. An item whose field the overlay owns edits the overlay, saves nothing and shows "for this title"; other items edit `stored` and save `c64_sid.bin`, as MSID §4.5 says. A per-SID field counts as owned when the overlay sets it, or when the overlay's key-class bit for its kind is set, because that bit makes the stored value read as Follow [D MSID §3.2]. Layout (item 0) and Reset SID setup (item 12) touch many fields and follow the coupling rule. Select SID (item 3) and the summary (item 1) are not settings |
| RAM expansion (3), Ultimate Audio (8) | EXPANSION_MEM | overlay changes both fields, B3 applied inside the overlay. Enabling either while a CRT is attached keeps today's refusal "Eject the cartridge before enabling ..." [V main.c:4199-4203] |
| SuperCPU Detect (14), Command Interface (15) | SCPU, UCI | overlay toggles |
| Keyboard joystick (Controls page), UART single key `j` | JOYSTICK | overlay port swaps; `remember_c64_input_settings()` saves the stored port only (section 4.3). `j` swaps the port and saves the same way today [V main.c:5673-5687], so it takes the same owned check |
| Any of these, group not owned | — | stored edit and save, as today |

Coupling rule for actions that touch more than one group or SID field (arg 12 when turbo is off; RAM expansion and UA inside EXPANSION_MEM; SID presets and Reset SID setup):
- If the action touches **any** owned group or field, it goes wholly to the overlay. The other groups or fields it touches become owned, copied from `effective` first, and nothing is saved. Example: the overlay owns SPEED only, the stored turbo is Off, CPU faster → the overlay gains TURBO_CONTROL (enabled, Manual) and the stepped SPEED; `settings.bin` is unchanged.
- If it touches only unowned groups, it edits `stored` field by field, starting from `persistent_settings.c64_turbo_detail` (normalised) and `c64_turbo_flags`, never from the effective byte. Byte 27 packs mode, badline, follow and speed, and today's handler saves the whole byte [V main.c:4252, 4283-4284]. Built from stored fields, it can never carry an overlay field into `settings.bin`.
- Reason: a hotkey during a title must not change saved settings for every title (the rule above). R5 depends on this coupling rule.

- `mt_c64_debug_clear_override()` still runs first on every turbo change [V main.c:4248 in `change_c64_turbo()`].
- The edit lasts for the title session. "Save settings for this title" makes it permanent in the file.
- Alternative (**USER**): "edit `stored` and drop the group from the overlay". Not recommended: slowing one game with a hotkey would change the saved speed for every title.

### 3.12 Menu and status

Rows (XP §5.1 arguments 17-19; argument 19 was added at this design's request, section 8.1 F-e). All are Enter-only and apply while paused [XP §5.1].

| Arg | Row | Shown | Value | Enter |
|---|---|---|---|---|
| 17 | Title .cfg files | always, C64 core | `Off`; `On`; `On: Driller.cfg`; `On: Driller.cfg, 2 skipped` | toggle; saves byte 26 bit 5; Off ends the overlay |
| 18 | Revert title settings | overlay active | source file name, truncated | clear the overlay, write stored values, status `Saved settings restored` |
| 19 | Save settings for this title | title session active (XM2d) | `Driller.cfg` or `Create Driller.cfg` | write the file (section 3.14) |

Help texts (ASCII [D TD §6.2]):
- 17: "Apply a .cfg with the same name as the program, cartridge or disk when it starts. Never saved to your settings."
- 18: "Use your saved settings now. Driller.cfg set: CPU speed, turbo mode, REU." (lists the owned groups)
- 19: "Write the current settings to Driller.cfg beside the title."

Row markers while overridden:
- Every row whose group the overlay owns shows the effective value with the suffix ` (title)`, for example `48 MHz (title)`. The CPU turbo status row becomes `Manual (title): 48 MHz, live $00, now 48 MHz (running)`.
- The help line of such a row starts with "Set by Driller.cfg for this title."
- The menu takes all values from `effective` and the owned mask, never from registers (XP §8.2 rule 4). Fault states still come from status registers as today (`C64_UA_STATUS` fault).
- RAM expansion and UA rows while a cartridge is attached: the row shows the **requested** value (section 3.8), with the suffix ` (cartridge)` when the applied value differs, for example `16 MB REU (cartridge)`. Enter that would enable keeps today's refusal "Eject the cartridge before enabling REU" [V main.c:4199-4203]. Enter that disables edits `stored` (or the overlay, if it owns EXPANSION_MEM) as today. Without this, the clamped `Off` would show, and Enter, which toggles from stored under XM1 rule 2, would save the REU off while the user meant to turn it on [V core_frontend.c:192-202; main.c:4190-4203].

Cartridge row: today it is disabled with "Turn REU off first" when the REU is on [V firmware/common/core_frontend.c:93-99], and its action `MT_MENU_ACTION_BROWSE_CARTRIDGE` has its own guard that refuses to open the browser while the live REU is on [V main.c:4378-4381]. With title files On, the row and the browser stay enabled whatever the REU state, and the refusal moves to the launch (section 3.9 step 4, `requested.reu`). Otherwise a CRT whose `.cfg` turns the REU off could never be chosen from the menu, and the REU defaults to on [V supervisor_settings.h:25 `MT_SETTINGS_C64_REU_DEFAULT true`]. While `requested.reu` is on, the row's help reads "REU is on: a cartridge loads only if its .cfg sets RAM Expansion Unit=Disabled." With title files Off, the row and the guard behave as today, with the guard reading `requested.reu` (equal to the live register in every state today's firmware reaches). This is the one default-visible UI change; G-CFG names it (sections 7.8 and 8.1 F-f).

**Status line after a launch.** One line, set by `set_supervisor_status()`, `set_supervisor_warning()` or `set_supervisor_error()`. The buffer holds 63 characters and the setters truncate [V main.c:190, 1976-1987].
- Name: the shown name is the sidecar's last path component. A name longer than 20 characters shows its first 15 characters, `~` and its 4-character extension. Every template below then fits 63 characters with counts up to 999. In the table, `Game.cfg` stands for the shown name.
- Precedence, highest first. One text is shown; the others go to the UART summary line and to `:cfg`:
  1. **Error**: the commit error, the transfer failure, the CRT refusal, and any error today's launch code sets (`Content loaded; settings write failed`, `Content loaded; drive remount failed` [V main.c:1385, 4794-4806]). If such an error hides a SuperCPU warning, the SuperCPU row still shows `Enabled (title)` and the UART line reports it.
  2. **SuperCPU warning** (XM3): the file turned SuperCPU Detect on. SCPU §3.6 owns the wording and requires that it is always shown when no error is [D SCPU §3.6, §3.7].
  3. **Skipped, refused or a launch warning**: some lines skipped; the file refused; turbo keys adjusted for a cartridge; a warning today's launch code sets (`Cartridge loaded; long path cannot be remembered` [V main.c:1387, 4798]).
  4. **Command Interface note** (XM5): the file turned the Command Interface on (R8). Under the U22 alternative it ranks above 3.
  5. **Clean**.

| Rank | Case | Text | Colour | Longest (20-character name) |
|---|---|---|---|---|
| 5 | file applied cleanly | `Content loaded; Game.cfg: 3 settings` | normal | 50 |
| 3 | some lines skipped | `Content loaded; Game.cfg: 3 settings, 2 skipped` | warning | 63 |
| 3 | file refused | `Content loaded; Game.cfg not used (not text)`, `(over 16 KB)`, `(SD read failed)`, `(open on drive 9)` | warning | 63 |
| 3 | CRT title with turbo keys, no CAPS bit 21 | `Game.cfg: 3 settings; cartridge runs at 1 MHz` | warning | 59 |
| 2 | file turned SuperCPU Detect on (XM3) | SCPU's text: `Title .cfg turned SuperCPU Detect on; Revert before GEOS`; from `$50`: `UCI config turned SuperCPU Detect on; Revert before GEOS` [D SCPU §3.6] | warning | 56 |
| 4 | file turned the Command Interface on (XM5) | `Game.cfg: 3 settings; Command Interface on` | normal | 56 |
| 1 | CRT refused (section 3.9 step 4) | files On: `Turn REU off, or disable the REU in Game.cfg`; files Off: `Turn REU off before loading a cartridge` (today's text) | warning | 56 |
| 1 | commit error (section 3.9 step 7) | `Settings could not be applied; saved settings restored` | error | 54 |
| 1 | transfer failure after the commit | `Load failed; saved settings restored` | error | 36 |
| — | Options → Reset with a kept D64 overlay | `Core restarted; Game.cfg stays active`; when the overlay turns Detect on, SCPU's `Core restarted; SuperCPU Detect stays on (title .cfg)` instead [D SCPU §3.6] | normal; warning for the SuperCPU text | 53 |
| — | plain mount, `.cfg` beside the D64, no overlay | `Drive 8 mounted; Game.cfg applies with Mount & Run` | normal | 62 |
| — | plain mount while an overlay is active | `Drive 8 mounted; Game.cfg stays active` | normal | 50 |
| — | Revert | `Saved settings restored` | normal | 23 |
| — | files switched Off / On | `Title .cfg files off; saved settings restored` / `Title .cfg files on` | normal | 45 |

The exact key the user needs for a CRT refusal (`RAM Expansion Unit=Disabled`) is in the Cartridge row help and in the UART line, not in the status text, which has no room for it.

Each non-applied report entry also prints one UART line, with no length limit:
`[C64] Driller.cfg line 7 "REU Size=2 MB": only 16 MB; REU is 16 MB`.

### 3.13 Options that need a reset or a reload

| Key | Needs | Sequencing |
|---|---|---|
| Turbo keys, SCPU | none (live registers) | committed in the reset hold; land after the 2.0 s window |
| REU, UA, SID layout, Command Interface | none (live `C64_EXPANSION_CONFIG`) | committed in the reset hold, so the title boots with them; the CRT check uses `E'.requested` |
| Joystick | none | committed in the hold for one consistent switch |
| REU Size | a size register, which does not exist | check only (K8). A size register would change the REU path and G-REU0; not proposed |
| System Mode | another bitstream (core reload, seconds; NTSC is held until it closes at 64x) | never automatic. Mismatch notice only (K5). **USER** U10 |
| Fast serial ROMs (not a key) | ROM restage and C64 restart | out of XM2; a later key would stage ROMs before step 5 and keep the overlay across that restart (U13) |
| REU preload (not a key) | a 16 MB copy with the C64 paused | later option: after the commit, before the stream; REU content survives the reset (U14) |

### 3.14 Save settings for this title (XM2d)

- Writes `<stem>.cfg` beside the title medium (PRG, CRT or D64), never a `.usr` file and never inside a disk image.
- New file content: every honoured group available in this image, with its effective value (byte 27 fields from the normalised byte, section 3.8). Sections and labels a U64 reads:
  - `[U64 Specific Settings]`: Turbo Control with `U64 Turbo Registers`, CPU Speed with the U64 padded labels ` 1` … ` 4`, ` 6`, ` 8`, `10` … `64`, Badline Timing, SuperCPU Detect from XM3;
  - `[C64 and Cartridge Settings]`: RAM Expansion Unit, Map Ultimate Audio $DF20-DFFF, Command Interface from XM5;
  - `[SID Addressing]` for the legacy layout: `SID Socket 1 Address=Unmapped`, `SID Socket 2 Address=Unmapped`, UltiSID 1 `$D400`, UltiSID 2 `$D400`/`$D420`/`$D500`, `UltiSID Range Split=Off`;
  - `[Multitini]` for K13-K17;
  - LF line ends, no BOM, a blank line after each section.
  - Sockets are written `Unmapped`, because the resolver counts socket addresses (section 3.7 step 2); the TT8 and Heartbeat files do the same (section 2.7). `Ext DualSID Range Split` is not written: it splits socket SIDs, which are unmapped, and both real files keep it at `A7` [I].
  - From XM7 the writer also covers the SID model (UltiSID, mixer and `[Multitini SID]` keys) per MSID (W12).
- Merge into an existing file that parsed without MALFORMED lines. The writer keeps every other line byte for byte (comments, unknown sections, U64 items we ignore). Per group:
  - **Rewritten** only if the active overlay owns the group, or the group resolved APPLIED or ADJUSTED in the last parse of that file. Rewriting replaces the value of the last occurrence of each key; missing keys of that group go at the end of their section.
  - **Added** if the file does not mention the group at all: its keys go at the end of their section, missing sections at the end.
  - **Left alone** if any of its lines resolved UNAVAILABLE, BLOCKED or CHECKED: those lines and the group's other keys stay byte for byte. Example: a TT8-like file saved on an XM2 image keeps `UltiSID Range Split=1/4 (A7,A8)` and `UltiSID 2 Address=$D420`, so XM7 can still use them.
  - When the SID group is rewritten, every `SID Socket 1/2 Address` line becomes `Unmapped` (added if missing) and `UltiSID Range Split` becomes `Off`. Otherwise a dump with `SID Socket 2 Address=$D420` merged with Dual `$D500` would re-parse as {`$D400`, `$D420`, `$D500`}, which is UNAVAILABLE.
  - Equivalent spellings stay as written: `C64U Turbo Registers` while the mode is still 2, the headers `[C64U Specific Settings]` and `[Cartridge and ROM Settings]`, and an unpadded `CPU Speed=4` while the MHz is unchanged.
  - A file with MALFORMED lines or bad headers is not touched: `Game.cfg has errors; fix or delete it first`.
- Size: the writer refuses when the merged output would exceed `MT_C64_TITLE_CFG_MAX_BYTES` (16,384): `Game.cfg would exceed 16 KB`. The reader refuses larger files (P1), so such a file would never apply.
- Busy: before the journal replace, the writer probes `<stem>.cfg` and `<stem>.cfg.mtj` as in section 3.4 step 11. `MT_SD_FILE_BUSY` → `Game.cfg is open on drive 9`.
- Written through `mt_sd_storage_journal_replace(path, path + ".mtj", ...)` [V sd_storage.h:79-82]; the lookup recovers the journal first (section 3.4 step 10).
- After the write, the saved file is parsed again and becomes the active overlay, so the rows of its groups show `(title)`.
- `CPU Speed=64` is invalid on a U64 MK1 and `5` cannot occur; `U64 Turbo Registers` in a new file may be rejected by C64U firmware [O]. Our reader accepts all of them.
- Reason for adding groups the file does not mention, rather than owned groups only: a user who tunes settings in the menu (stored) and then saves gets a file that pins the title against later global changes; an owned-only file would be empty in that flow [I]. Reason for leaving held-back groups alone: a request this image cannot meet (multi-SID before XM7) belongs to the file, not to our image.

### 3.15 UCI Control `$50` LOAD_CONFIG (XM5)

- **Path.** With a command of 2 bytes or less, the file is `/temp/uci_config.cfg` as the UCI DOS target resolves it: `Temp` → `<package directory>/uci_temp` (default `0:/multitini/cores/c64/uci_temp`), outside the media root, created on first use, and emptied when the C64 core starts [D UCI §3.9 rule 5]. So the default `$50` file is deleted at every C64 core start. No trigger launches a title from that folder, so its files never auto-apply. U64 clients write the file through DOS `OPEN_FILE`/`WRITE_DATA`/`CLOSE_FILE` and then send a bare `$04 $50` [V control_target.cc:553-569]. A path argument (bytes 2 and later) is resolved by the same DOS rules, confined to the UCI root [D UCI §3.9; XP §7.4, files and paths].
- **Buffer.** `$50` has its own static 16 KiB buffer, not the launch buffer. UCI file I/O runs across service passes (at most 4 KB per pass [XP §7.1]), and a menu launch reads the next title's `.cfg` at section 3.9 step 2, before `prepare_c64_content_change()` aborts the UCI command [XP §7.2]. If that launch is refused at step 4, prepare never runs, and a shared buffer would let an in-flight `$50` resume with another file's bytes. Cost: 16 KiB of static RAM [O: confirm in the linker map]. CPU0 only; FatFs has one owner [XP §7.1].
- Same parser and resolver (sections 3.5-3.7), source "UCI".
- **All or nothing.** If any line is MALFORMED, BAD_VALUE or BLOCKED, nothing is applied and the status is `89,CONFIG FILE HAD ERRORS`. UNKNOWN_ITEM, UNAVAILABLE and NOT_APPLICABLE are not errors (U64 parity for unknown items [V configio.cc:381-390]). Because the candidate is separate from the overlay, a failed load leaves nothing behind (this fixes section 2.6).
- **Cartridge.** While a cartridge is attached (the UCI stays active under a generic cartridge [D UCI §3.1]), `RAM Expansion Unit=Enabled` and `Map Ultimate Audio $DF20-DFFF=Enabled` are BLOCKED (section 3.7 step 3), so such a file gives `89`.
- **Merge on success.** The candidate is merged into the active overlay. Groups in the file replace the overlay's values for those groups; other groups stay. For the SID group (XM7) the merge is per field: fields the file sets replace the overlay's; other SID fields stay; the key-class bits are ORed. With no overlay, a new one is created with the title session's medium, or with no medium if no session exists. Section 8.1 F-c gives the reason. UCI §3.8.4 states the same rule.
- **Live apply.** Applied without a reset, apply = true, with `cartridge_after` = a CRT is attached (section 3.8).
  - `C64_CPU_CONFIG`, `CONTROL` bit 5 and the joystick port are written directly, as menu edits are.
  - A change of `C64_EXPANSION_CONFIG` bits 3-4 (REU, UA) or, from XM7, of the SID map (bit 6 and the table commit) is written under `C64_UCI_CONTROL.HOLD` (bit 10), with the steps UCI §3.8.6 uses around its REU invalidate: set HOLD; wait for paused, then for REU IDLE (release and retry, 1 s in total); write; release HOLD; then validate the reply [D UCI §3.8.6 steps 4-5 and 7, §3.13].
  - If the hold or REU idle is not reached, nothing is applied, the overlay is unchanged, and the status is UCI's failed-hold status, `87,INTERNAL ERROR` as in UCI §3.8.6. For the SID map MSID §3.10 gives the order inside the hold: table words, COMMIT, wait for the accepted sequence, then one `C64_EXPANSION_CONFIG` write.
  - No menu pause and no UA hold: the menu pause shows a "please wait" screen [V main.c `c64_debug_pause()`], and the UA hold removes UA from the I/O map [V adapter.sv:611]. The program keeps its screen; audio stops only for the hold (R9).
- `Command Interface=Disabled` in a `$50` file is INFO "ignored when loaded through the Command Interface", so a program cannot remove the interface that must deliver its reply.
- Reply data: report lines, ASCII, at most 511 bytes (U64 parity [V control_target.cc:600-610]). Status `00,OK`, `88,CANNOT OPEN CONFIG FILE` (missing, too large or not text), `89,CONFIG FILE HAD ERRORS`, or `87,INTERNAL ERROR` (hold failed).
- The overlay from `$50` ends with the title session, like any overlay. UCI REBOOT keeps it (section 3.10).

### 3.16 Safety

- Bounded: 16 KiB file, 319-character lines, 16 report entries, fixed-size candidate; no heap; O(n) in the file size.
- No file loading from a `.cfg`: no honoured key takes a string. ROM, cartridge, palette, REU image and partition names are NOT_APPLICABLE. Any later key that names a file (REU preload) must resolve only relative to the `.cfg`'s own folder through `mt_media_browser_join()`, only for its own content type, and never for ROMs.
- Network, streaming and UI sections are NEVER applied (section 3.6.3).
- Debug features stay UART-only and off by default; no `.cfg` key reaches them [XP §8.2 rule 9; D TD §6.5].
- A title file can enable the Command Interface (K10), and with it UCI file access inside the UCI root and, once XM4/XM5 exist, sockets. The board network mode itself is not a key, so a title cannot turn the network on (R8).
- Writes by this feature: only "Save settings for this title", only beside the title, journaled.
- **Writes by C64 software (U22).**
  - Facts. From XM5, any C64 program with the Command Interface on can write files in the UCI root through the UCI DOS targets. Drive 9 (dev7) cannot create a `.cfg`, because it appends `.prg`/`.seq` to new names [V firmware/common/c64_iec_dos.c:593-598], but it lists every file and can replace or append to an existing `GAME.CFG` by name with `@` or append mode [V c64_iec_dos.c:186, 587-592]. A replaced sidecar auto-applies at later launches of that title, including `Command Interface=Enabled` and turbo or REU changes. The UCI protected list covers the mounted image, open drive 9 files, every `*.mtj` and package files, not sidecars [D UCI §3.9 rule 8].
  - Real clients write `.cfg` files that are not sidecars: UBoot64 and DMBoot save `DMBCFG.CFG` and `DMBSLT.CFG` in the root of "the first USB drive found (using /usb*/ as path)" [R xahmol/UBoot64 README, "First run"], which UCI maps to its root [D UCI §3.9 rules 5 and 11]. A rule that refused every `*.cfg` would fail their first run with `98,FUNCTION PROHIBITED`.
  - **Recommended rule:** protect only real sidecars. A path is a protected sidecar when its last component ends in `.cfg` or `.usr` (case-insensitive), it lies below the media root, and a `<stem>.prg`, `<stem>.crt` or `<stem>.d64` exists in the same folder (FatFs matches names case-insensitively). Protected sidecars refuse open-for-write, create, append, rename (as source or as target), delete and copy-to. Reads stay allowed.
  - Placement. The predicate is portable (`mt_c64_cfg_sidecar_write_protected()`, section 4.2) and is consulted by the shared file-system layer of drive 9 and the UCI DOS targets [XP §7.4]. Drive 9 asks its callback before every open, also for reads [V c64_iec_service.c:194-200], so the sidecar rule needs the access kind: the callback gains a read/write argument (section 4.3). Existing protections answer for both kinds, as today. Drive 9 can replace an existing sidecar from XM2 on, so the predicate lands with XM2; the UCI DOS targets use it from XM5.
  - Cost: up to three `f_stat` calls per checked `.cfg`/`.usr` write [I].
  - Residual: a C64 program can still write a sidecar for a title it creates itself, or a `.cfg` before its title exists. Such a file affects only titles that program wrote, or a title the user later adds under that stem [I].
  - `uci_temp` is outside the media root, so `$50` files there are never protected and never auto-apply.
  - Alternative (**USER** U22): allow all writes, and rank the Command Interface note of section 3.12 above the skipped count.
- Stock defaults: with no `.cfg` file, the register writes are the same as today (G-CFG). Expand by Bonzai stays the hard gate (G-REU0).

### 3.17 UART commands (normal commands, not debug)

| Command | Effect |
|---|---|
| `:cfg` | switch state, title session, overlay source, owned groups and values (SID fields from XM7), last report and its full summary line |
| `:cfg test NAME` | parse `NAME` (a path relative to the media root, any extension) and print the report and the resolved candidate; applies nothing |
| `:cfg revert` | same as row 18 |
| `:cfg save` | same as row 19 (XM2d) |

These print parse results only. They read no C64 RAM and record nothing, so the debug rule does not apply.

---

## 4. Implementation plan by file

### 4.1 What XM2 needs from XM1 (XP milestone)

| Item | Where | Note |
|---|---|---|
| Option registry `c64_option_registry.c` with the XP §8.6 fields | `firmware/common/c64_option_registry.c`, `firmware/include/multitini_c64_option_registry.h` (new) | this design adds the `.cfg` columns: section id, item name, aliases, value table, group (or SID field id from XM7), check-only flag |
| Stored snapshot and effective function | `firmware/common/c64_effective.c`, `firmware/include/multitini_c64_effective.h` (new) | `mt_c64_stored_t`, `mt_c64_effective_t mt_c64_effective(const mt_c64_stored_t *, const mt_c64_title_overlay_t *, const mt_c64_capabilities_t *, bool cartridge_after)` implementing section 3.8; `mt_c64_effective_t` holds `requested` and `applied`, and the caller sets `cartridge_after` per the section 3.8 table |
| `change_c64_expansion()` starts from stored values, not `shell_read(MT_REG_C64_EXPANSION_CONFIG)` | main.c:4190 | XP §8.2 rule 2 |
| `supervisor_menu_view()` fills SID/REU/UA and the turbo detail and flags from `effective` (requested REU/UA while a cartridge is attached, section 3.12) | main.c:2601-2614 | XP §8.2 rule 4; SCPU §3.5 asks for the turbo flags too |
| `configure_c64_turbo()` and `configure_c64_expansion()` write `effective` | main.c:2376-2404 | XP §8.2 rule 1 |
| `MT_MENU_MAX_CORE_OPTIONS` 12 → 20, bounds-checked append in `build_c64()` | `firmware/include/multitini_menu.h:18`, `firmware/common/core_frontend.c:64-213` | XP §5.1 |

### 4.2 New portable code (XM2)

| File | Content |
|---|---|
| `firmware/include/multitini_c64_title_cfg.h` | limits (`MT_C64_TITLE_CFG_MAX_BYTES` 16384, `..._LINE_MAX` 319, `..._NAME_MAX` 40, `..._REPORT_ENTRIES` 16, `..._SHOWN_NAME_MAX` 20); `mt_c64_cfg_class_t`; `mt_c64_cfg_report_t`; `mt_c64_cfg_candidate_t`; `mt_c64_title_overlay_t` (owned group mask, group values, source name ≤ 63, origin PRG/CRT/D64/UCI, medium path ≤ 255; from XM7 an `mt_c64_sid_overlay_t` [D MSID §3.10]: `value`, the 60-bit `field_mask` and `class_bits` ADDR, MIX, MODEL) |
| `firmware/common/c64_title_cfg.c` | `mt_c64_cfg_sidecar_path()` (section 3.4 steps 2-3), `mt_c64_cfg_is_text()` (P2), `mt_c64_cfg_parse()` (P1-P21), `mt_c64_cfg_resolve()` (section 3.7; takes caps, source kind (launch kind or UCI), cartridge state, base snapshot, variant), the NOT_APPLICABLE/NEVER tables, `mt_c64_cfg_report_line()` (ASCII formatter), `mt_c64_cfg_summary()` (status text with the name rule, the 63-character limit and the precedence of section 3.12), `mt_c64_cfg_sidecar_write_protected(path, root, exists_fn, context)` (section 3.16) |
| `firmware/common/c64_title_overlay.c` | `mt_c64_overlay_install()`, `_merge()` (UCI; per group, per field for SID), `_clear()`, `_owns()`, `_owns_sid_field()` (field bit or key-class bit), `_edit()` (per menu argument, controller action and, from XM7, SID sub-page item, section 3.11), `_describe()` (owned group list for help text) |
| `firmware/common/c64_title_cfg_writer.c` (XM2d) | `mt_c64_cfg_write()`: merge writer of section 3.14 into a caller buffer of `MT_C64_TITLE_CFG_MAX_BYTES`; takes the last parse report of the file so it can tell rewritten, added and held-back groups apart; fails when the output would not fit |
| `CMakeLists.txt` | add the files to `multitini_common` [V CMakeLists.txt:13-62] and the new host tests |

### 4.3 `firmware/platform/xilinx/main.c` (function by function)

| Function or branch | Change |
|---|---|
| new `static mt_c64_title_overlay_t c64_title_overlay; static c64_title_session_t c64_title_session;` | RAM state; cleared at initialisation and in `supervisor_activate()` |
| new `title_candidate_for(kind, relative, apply, cartridge_after)` | section 3.9 steps 2-3: busy probe, lookup, read into the static 16 KiB launch buffer, parse, resolve, compute E'. Returns **no hook (NULL)** when there is no candidate, no active overlay and every computed register equals the current one, so the old path runs unchanged (T-RT1). Otherwise it builds the hook: `reu_requested` = `E'.requested.reu`, `hold_reset` = some computed register differs from the current one |
| new `commit_c64_title(void *ctx)` | section 3.9 step 7: joystick first (only when the old or new overlay owns JOYSTICK), then only the changed registers among `C64_EXPANSION_CONFIG`, `C64_CPU_CONFIG`, `set_c64_cpu_turbo()` (overlay value only when `caps.turbo`); install overlay and session. Also used by every end event of section 3.10 |
| `handle_c64_content_load()` and the `MT_MENU_ACTION_LOAD_CONTENT` C64 branch | replace `c64_content_service_reu_enabled()` [V main.c:1372-1373, 4774-4777] with `E'.requested.reu` (step 4); call `c64_content_service_load_hooked()`; failure handling of steps 7-8; status of section 3.12. `cartridge_after` = the file is a CRT |
| `MT_MENU_ACTION_BROWSE_CARTRIDGE` [V main.c:4378-4381] | files On: open the browser whatever the REU state; the refusal happens at step 4. Files Off: the guard as today, reading `requested.reu` |
| `MT_MENU_ACTION_MOUNT_AND_RUN` / `MT_MENU_ACTION_DIRECT_DISK_PROGRAM` branch | candidate from `c64_d64_service_mounted_name()` immediately before `prepare_c64_content_change()` at main.c:4632, not at the top of the branch; `c64_content_service_stage_prg_image_hooked()`; `cartridge_after = false` |
| `launch_disk_program_native()` | same, immediately before main.c:3997, with `c64_content_service_prepare_disk_load_hooked()` |
| `MT_MENU_ACTION_MOUNT_DRIVE8` branch | no overlay change; the two status hints of section 3.12 (one optional open of `<stem>.cfg`) |
| `restore_selection_settings()` CRT branch | relaunch through the hook with `apply = false`, `cartridge_after = true`; `E'.requested.reu` replaces the live check [V main.c:2446-2448] |
| `MT_MENU_ACTION_RESET_CORE` | end the overlay per section 3.10 (PRG: clear; D64: keep while drive 8 holds a disk; CRT: clear, the relaunch re-applies) before `restore_selection_settings()`, which then writes effective values [V main.c:5084-5093]; the Reset status of section 3.12 |
| UCI REBOOT supervisor action (XM5, UCI §4.5) | `set_core_reset_request(true)`, then `(false)`; no `restore_selection_settings()`, no overlay or session change, no `.cfg` read (section 3.10) |
| argument 0 fast-ROM path [V main.c:4970-5041] | CRT source: keep the overlay. D64 source: as Options → Reset. PRG source: clear it, and after `stage_c64_rom_profile()` succeeds call `configure_c64_expansion()` and `configure_c64_turbo(..., true)`. Today this path calls neither |
| `MT_MENU_ACTION_DETACH_CARTRIDGE` C64 branch [V main.c:4845-4870], `handle_c64_cart_detach()` for `:cartoff` [V main.c:1391-1400] | call `c64_content_service_detach_crt_hooked()` with a hook from effective(stored), `cartridge_after = false`; clear overlay and session on success |
| `supervisor_activate()`, `supervisor_enter_standby()`, `mount_sd_over_usb()` | clear overlay and session |
| `change_c64_turbo()` | if the action touches a group the overlay owns (args 1, 4, 5, 6, 7, 9, 10, 12, 13): `mt_c64_overlay_edit()` with the coupling rule of section 3.11 (arg 12 can add TURBO_CONTROL), no `save_persistent_settings()`, `configure_c64_turbo(apply = true)`, status "for this title". Unowned: the stored edit starts from `persistent_settings` fields as today [V main.c:4248-4285] |
| `change_c64_expansion()` | same for args 2, 3, 8 (EXPANSION_MEM as one group); starts from stored, not `old_config` (XM1); the cartridge refusal [V main.c:4199-4203] stays |
| `change_c64_sid_setup(item, delta)` (XM7, MSID §4.3) | per-item ownership check of section 3.11; owned: overlay edit, no `c64_sid.bin` save |
| `MT_MENU_ACTION_SWAP_JOYSTICK_PORT` [V main.c:4901-4908] and the UART single key `j` [V main.c:5673-5687] | one shared helper; owned: overlay edit, no save |
| `remember_c64_input_settings()` [V main.c:2300-2309], `remember_manual_selection()` [V main.c:2311-2326], the auto-boot toggle [V main.c:5117-5121] | pass the **stored** joystick port (profile byte 241) instead of `usb_hid_service_joystick_port()` when the overlay owns JOYSTICK; fixes the leak of section 1.4. XP §12.1 lists the saver fix under XM1; whichever milestone carries it covers all three savers |
| `c64_iec_path_protected()` [V main.c:445-449] | gains the access kind; for writes it also returns `mt_c64_cfg_sidecar_write_protected()` (section 3.16, U22) |
| `handle_menu_action()` `MT_MENU_ACTION_TOGGLE_CORE_OPTION` | new branch for arguments 17, 18, 19 before the `>= 2U` catch-all [XP §5.1] → new `change_c64_title_option()` |
| `supervisor_menu_view()` | fill the new state fields (section 4.6) |
| `handle_extended_command()` | `:cfg`, `:cfg test NAME`, `:cfg revert`, `:cfg save` |
| controller action routing (`MT_CONTROL_TURBO_TOGGLE` … `MT_CONTROL_CPU_SLOWER` → args 9/12/13) | unchanged; the owned check lives in `change_c64_turbo()` |

Drive 9 and the shared file-system layer (U22): `firmware/include/multitini_c64_iec_dos.h:32` (`protected_path`) and `firmware/platform/xilinx/c64_iec_service.c` (`protected_path`, `fs_open`, `fs_remove`, `fs_rename`, `fs_mkdir`, commit [V :87-90, 194-200, 253-282]) pass the access kind: write for any open mode other than `r`/`rb`, and for remove, rename, mkdir and commit. The UCI DOS targets use the same signature from XM5 [D UCI §3.9 rule 8].

### 4.4 `firmware/platform/xilinx/c64_content_service.c` and `.h`

| Item | Change |
|---|---|
| `mt_c64_launch_hook_t` (section 3.9) | new type in the header |
| `MT_C64_CONTENT_SERVICE_CONFIG_FAILED` | new status; `c64_content_service_status_name()` gains its name |
| `c64_content_service_load_hooked(name, hook)` | as `c64_content_service_load()` [V :312-348], with the REU check at :328-331 using `hook->reu_requested` when a hook is given; with NULL it reads the live register as today (a backstop: main.c's step 4 has already decided) |
| `c64_content_service_stage_prg_image_hooked()`, `c64_content_service_prepare_disk_load_hooked()` | hooked twins of :350-363 |
| `c64_content_service_detach_crt_hooked(hook)` | as `c64_content_service_detach_crt()` [V :278-310]; after a successful detach, if `hook->hold_reset`: `set_core_reset(true)`, `hook->commit()`, `set_core_reset(false)` |
| `load_image()` [V :167-259] | after validation (:185-192) and before `release_stale_core_reset()` (:194): if `hook->hold_reset` then `set_core_reset(true)` (:52-59); call `hook->commit()`; on error release and return CONFIG_FAILED. The commit stays before the PRG path's CRT detach (:200-203); section 3.8 explains why |
| old entry points | call the hooked ones with `NULL`, so behaviour is unchanged |

### 4.5 `firmware/platform/xilinx/supervisor_settings.c` and `.h`

| Item | Change |
|---|---|
| `mt_supervisor_settings_t` | new `bool c64_title_cfg_off` (byte 26 bit 5) |
| encode [V supervisor_settings.c:588-589] | OR in `(settings->c64_title_cfg_off ? 0x20U : 0U)` (bits 3-4 per SCPU and UCI) |
| decode [V :710-711] | `c64_title_cfg_off = (settings_wire[26U] & 0x20U) != 0U` |
| no schema change | byte 26 bits are ignored by the frozen schema-10 decoder [XP §4.1, §4.3, settings] |

### 4.6 Menu and front end

| File | Change |
|---|---|
| `firmware/include/multitini_core_frontend.h` | state: `bool c64_title_cfg_on`, `bool c64_title_session`, `uint32_t c64_title_owned`, `const char *c64_title_source`, `uint16_t c64_title_applied, c64_title_skipped`, `bool c64_reu_requested, c64_ua_requested`; texts in `mt_core_frontend_t`: `char c64_title_text[64], c64_title_help[192]`, plus per-row buffers `char c64_option_text[MT_MENU_MAX_CORE_OPTIONS][48]` and `char c64_option_help[MT_MENU_MAX_CORE_OPTIONS][192]` (20 × 240 B = 4.8 KB after XM1), and `char c64_joystick_text[48], c64_joystick_help[192]` for the Controls page. Rows use string literals today, and the front end has only `c64_turbo_text`, `c64_turbo_help` and `c64_speed_text` [V firmware/include/multitini_core_frontend.h:50; core_frontend.c:186-211] |
| `firmware/common/core_frontend.c` `build_c64()` | rows 17, 18, 19 in the last group; ` (title)` suffix and help prefix on owned rows (args 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 14, 15, 16) written into the per-row buffers; ` (cartridge)` on the RAM expansion and UA rows (section 3.12); Controls page joystick row suffix; the Cartridge-row rule of section 3.12 |
| SID sub-page (XM7, MSID §4.5) | ` (title)` per item from `_owns_sid_field()` |
| `firmware/common/menu.c` | none (Enter-only rows exist) |

### 4.7 Spec, ABI, RTL

- `spec/interface_v0.json`, `hdl/generated/multitini_regs_pkg.sv`, `firmware/include/multitini_regs.h`: no change for XM2. Keys K4 and K10 use the bits that XM3 and XM5 add [XP §3.3, new bits].
- RTL: none. No new clock, reset, crossing or timing rule. XM2 builds no image, so the resource and timing ceilings of XP §10.3 do not apply to it; G-STK, G-LOCK and G-TURBO hold by construction [XP §13]. One new case in an existing bench checks the HDL clamp that the PRG-after-CRT commit relies on (section 7.6, R14).
- `scripts/make_core_package.py`: none.

### 4.8 Docs (when XM2 lands)

Line numbers are at `3df6d04`.

| Doc | Change |
|---|---|
| TD §0.6 Q10 (line 171), §6.2 (line 1920), §6.3 (line 1989), §11 (line 2530), §15 (line 2897) | Q10 reversed in part by Q22; describe the overlay; the "never persisted" list gains the title overlay |
| TD §6.2 row table and routing note | arguments 17-19 |
| TD §6.3 byte 26 table | bit 5 |
| TD §1.5 apply-event list | add "title `.cfg` launch and Revert" as sources; the CRT restore relaunch is not |
| ISS §3(g) item 11 (lines 365-376) | mark the sweep proposal as adopted with the changes in section 5 |
| `docs/C64_SD_STORAGE.md` | `.cfg` and `.usr` beside media; hidden in browsers; the journal file; sidecar write protection for drive 9 |
| this document | add a user guide section when XM2 lands: triggers, dialect table, Multitini section, report texts, Revert, Save, examples written from scratch (no third-party files) |

---

## 5. Fix-instead-of-copy list

Each item is a U64 behaviour that hurts users or could confuse our model, with the evidence and our behaviour.

| # | U64 behaviour | Evidence | Multitini |
|---|---|---|---|
| X1 | A per-title file is never reverted; it survives C64 resets and the next title runs with it | [V configio.cc:436-478; u64_config.cc:1035-1050] | session overlay with defined end events (section 3.10) |
| X2 | Loaded values are flash-stale; leaving the menu asks "Save changes to Flash?" and Yes makes them global | [V configio.cc:399-423; config_menu.cc:169-201]; shared flash pages may also persist them on unrelated writes [I] | the overlay is never serialised; savers write `stored` only; no prompt |
| X3 | Every stale store is effectuated, including stores the file did not name (#765, #787, #789; C64U 1.2.0) | [V configio.cc:455-467; tests/e2e/filemanager/cfg_partial_effectuate_test.py:1-22] | one commit of the effective values; apply events only on real change |
| X4 | Last line without LF dropped | [V configio.cc:260-265] | parsed (P5) |
| X5 | UTF-8 BOM breaks the first section | [I configio.cc:279, 310-313] | skipped (P3) |
| X6 | Whitespace-only line is an error; a header without `]` fails every later line | [V configio.cc:279-298, 310-313, 370-375] | skipped; one report line (P7, P11) |
| X7 | No range check on numbers | [V configio.cc:395-403] | range-checked (P16) |
| X8 | Change hook bypassed; read-only items writable | [V configio.cc:399-401; no `isEnabled()` in :352-434] | allow-list through the normal effective path with caps checks |
| X9 | Auto-load errors invisible (`mode = 1`) | [V configio.cc:447-454, 487] | status line with counts, UART lines, `:cfg` (sections 3.12, 3.17) |
| X10 | A full dump or a game file switches network services | [R doom.cfg; V configio.cc:198-228 writes every item of a store] | NEVER sections (section 3.6.3) |
| X11 | `C64U Turbo Registers` rejected by U64 firmware | [V u64_config.cc:325; doom_release_test.py:35-41] | both labels accepted (K1) |
| X12 | `CPU Speed` labels differ between MK1 and U64-II; the same index means different speeds | [V u64_config.cc:321-322] | matched by MHz; 5 → 4 MHz reported (K2) |
| X13 | Extensionless names lose a character | [V pattern.cc:296-301; configio.cc:482-485] | correct derivation (section 3.4) |
| X14 | A `.usr` fallback can feed a binary file to the parser | [I] | text check (P2) |
| X15 | A failed UCI `$50` still writes values that take effect at the next reset | [V control_target.cc:578-595; configio.cc:399-423; u64_config.cc:1035-1050] [I for the consequence] | separate candidate; all or nothing (section 3.15) |
| X16 | Release notes say "when loading"; code applies on Run only | [D 1541u-documentation howto/release_3.15.md:64-72; V section 2.4 table] | triggers documented exactly; mount hint in the status line |
| X17 | UA and REU are separate items in one store ("C64 and Cartridge Settings"), set independently; a file can enable UA with the REU off | [V c64.cc:90, 95, 132, 312-330] | EXPANSION_MEM is one group; the named key decides the other under B3 (section 3.7 step 4) |

Kept as on the U64, on purpose: last line wins for duplicates; one `.cfg` per stem in a folder; no `.cfg` on a plain mount; no folder default; enum matching rules; `.usr` fallback for PRG files; REBOOT keeps the loaded settings (section 3.10).

---

## 6. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | A path still saves an overlay value (a missed saver, a future option) | medium / high (silent global change) | every saver takes `stored`; the three joystick savers fixed; property test over random action sequences (T-RT8); the registry is the only path to add an option |
| R2 | Commit in the wrong place: the title boots without its REU or turbo, or the old program runs with the new values | low / medium | hook between validation and `release_stale_core_reset()`; runtime test asserts the MMIO order (T-RT2) |
| R3 | Full U64 dumps beside titles change many settings (REU off, turbo off, single SID from the default `$D400`/`$D400` layout) | medium / low | faithful to the file; counts in the status line; Revert row; `:cfg` shows each value |
| R4 | New state "stored REU on + CRT attached" (a CRT loaded under an REU-off overlay; stays after Revert or files Off) | certain once used / medium | `requested` vs `applied` (section 3.8): the CRT refusal and the menu use `requested`, the registers `applied`; the RAM expansion row shows `16 MB REU (cartridge)`; the CRT restore relaunch applies the file (F-b); with files Off the existing warning appears; the next PRG launch or the detach commits the stored REU; tests T-RT5, T-RT5b |
| R5 | Coupled options edited with mixed ownership | low / medium | EXPANSION_MEM is one group; TURBO_CONTROL holds both turbo on and mode; the coupling rule of section 3.11 (an action touching an owned group or field goes wholly to the overlay) |
| R6 | U64 or C64U renames items or labels in later firmware | medium / low | unknown items are warnings, not silent; tables pinned to d43546fe and easy to extend |
| R7 | Extra SD opens per launch in big folders (`.cfg`, `.usr`, journal) | low / low | at most three failed opens; measure on a 144-file folder [O] |
| R8 | A title file turns on the Command Interface and gets SD (and later socket) access; a C64 program then writes or replaces `<title>.cfg`/`.usr` sidecars through UCI DOS or drive 9, which auto-apply at later launches | medium / medium | UCI confined to its root [XP §7.4]; network mode is not a key; Command Interface note in the status line; **USER** U17; sidecar write protection for real sidecars, **USER** U22 (section 3.16) |
| R9 | UCI `$50` changes REU, UA or the SID map while the C64 runs and UA or REU DMA may be active | low / medium | written under `C64_UCI_CONTROL.HOLD` after paused and REU IDLE (section 3.15); a T-U4 case in the UCI bench toggles `C64_EXPANSION_CONFIG` bits 3-4 under HOLD with an REU loop running [O] |
| R10 | Users expect a hotkey change during a title to be saved | medium / low | status says "for this title"; Save row |
| R11 | Third-party test files have unclear licences | low / low | hand-written fixtures with the same setting lines; originals only in an uncommitted local check (section 7.1) |
| R12 | Menu rows exceed 20 | low / low | worst case: 9 release rows + arguments 14-19 (6) + diagnostic arguments 7 and 10 (2) + the UA level row (argument 11) = 18 ≤ 20; XP's capacity test (XP §5.1) |
| R13 | Docs drift (Q10 text, ISS proposal) | medium / low | section 4.8 list |
| R14 | A PRG-type launch while a CRT is attached sets the REU (and UA) enable before the CRT detach; this state never occurs today | low / medium | the HDL clamp `reu_active = reu_enable && !cart_loaded && !cart_loading` [V adapter.sv:608, 611]; a bench case in the existing expansion-config bench (section 7.6) [O]; fallback if it fails: commit with REU off and write `C64_EXPANSION_CONFIG` again after the detach, before the PRG stream resets the C64 |
| R15 | `.cfg` or `.cfg.mtj` held open by drive 9 while the lookup recovers the journal or Save replaces the file (FatFs locking off) | low / medium | busy probe through the managed-file API before every journal operation (sections 3.4 step 11, 3.14) |
| R16 | A kept D64 overlay that turns SuperCPU Detect on survives Options → Reset; a GEOS or GDOS64 disk swapped in and booted then crashes on its SuperCPU patch | low / high for that user | Revert ends every overlay (section 3.10); the Reset status names SuperCPU Detect; SCPU's help and docs advise Revert; **USER** U23 |
| R17 | A status text overflows the 63-character buffer and loses its end | medium / low | fixed templates, name shortening to 20 characters, precedence (section 3.12); T-RT11 checks every template at the limits |

---

## 7. Tests and gates

### 7.1 Host parser tests: `tests/host/test_c64_title_cfg.c` (new)

Fixtures: `tests/host/fixtures/title_cfg/*.h` as C string literals, written by hand. They reproduce the setting lines of the real files (byte layout from section 2.7): `tt8_like` (763-byte layout, sections Audio Mixer, UltiSID Configuration, SID Addressing), `heartbeat_ue2_16_like`, `heartbeat_ue2_64_like`, `doom_like` (no blank lines between sections, `C64U Turbo Registers`, `[Network Settings]`). An optional local check `scripts/check_title_cfg_originals.py` parses the downloaded originals from `build/third_party_cfg/` if present and is never committed with them. **USER** U18.

| Case | Expected |
|---|---|
| Heartbeat 16 MHz on an XM2 image | APPLIED: Turbo Control TurboEnable (mode 3), CPU Speed 16 (index 9), Badline Enabled, REU Enabled, UA Enabled; UNAVAILABLE: SuperCPU Detect, Command Interface, SID layout `1/4 (A7,A8)`; NOT_APPLICABLE: Audio Mixer, Speaker Mixer, SID Sockets, UltiSID Configuration, SID Player Autoconfig, Allow Autoconfig, REU Preload; summary "5 settings, 3 skipped" |
| Heartbeat 64 MHz | CPU Speed 64 → index 15; on an image without index 15 → ADJUSTED to the image limit |
| Same files on an XM3/XM5/XM7 build of the tables | SCPU, UCI, SID become APPLIED in turn |
| TT8 on XM2 | SID group UNAVAILABLE "needs multi-SID"; nothing else honoured; summary "0 settings, 1 skipped" |
| doom_like | System Mode PAL = CHECKED on PAL, UNAVAILABLE on NTSC; Turbo Control `C64U Turbo Registers` → mode 2; Badline APPLIED; `SuperCPU Detect (D0BC)=Disabled` CHECKED (XM2); `Cartridge=`, `Kernal ROM=` NOT_APPLICABLE; REU, UA APPLIED; `Command Interface=Enabled` UNAVAILABLE (XM2); `[Network Settings]` three NEVER entries; `REU Size=16 MB` CHECKED; summary "4 settings, 1 skipped" on PAL |
| BOM + CRLF + no final LF | same result as the clean file |
| CR-only line ends | parsed like LF: same result as the clean file |
| `[U64 Specific Settings]junk` | header accepted; one INFO entry "text after section name ignored" |
| Line of 319 characters / 320 | accepted / MALFORMED, parsing continues; a 320-character `[...]` header skips its section |
| Whitespace-only line, `   # comment`, `;comment` | skipped |
| `[U64 Specific Settings` (no `]`) followed by items | one MALFORMED entry, items skipped until the next header |
| Items before any header | one INFO entry |
| `Turbo Control = manual` (spaces, case) | APPLIED mode 1 |
| `CPU Speed= 5`, `CPU Speed=48 MHz`, `CPU Speed=7`, `CPU Speed=0` | 4 MHz ADJUSTED; 48; BAD_VALUE; BAD_VALUE |
| `SuperCPU Detect ($D0BC)=Enabled` | alias accepted (XM3 tables) |
| `RAM Expansion Unit=GeoRAM Mode`; `REU Size=2 MB` | UNAVAILABLE; `REU Size` CHECKED "only 16 MB" when the resolved REU is off, ADJUSTED when it is on |
| `Map Ultimate Audio $DF20-DFFF=Enabled` with `RAM Expansion Unit=Disabled` | UA on, REU on, REU line ADJUSTED |
| One-key EXPANSION_MEM files, each with base UA on and with base UA off | `RAM Expansion Unit=Disabled` only → REU off, UA off (note when the base UA was on); `RAM Expansion Unit=Enabled` only → REU on, UA = base; UA `Enabled` only → both on; UA `Disabled` only → UA off, REU = base |
| CRT launch kind with `RAM Expansion Unit=Enabled` | BLOCKED, group not in the candidate |
| CRT launch kind with `RAM Expansion Unit=Disabled` and UA `Enabled` | UA line BLOCKED; group {REU off, UA off}; the CRT is not refused |
| UCI source kind, cartridge attached, `RAM Expansion Unit=Enabled` (and separately UA `Enabled`) | BLOCKED; the file result is "had errors", nothing in the candidate |
| CRT launch kind, `CPU Speed=48`, image without CAPS bit 21 | ADJUSTED "cartridge runs at 1 MHz"; SPEED still in the candidate |
| Image without `caps.turbo`: `Turbo Control=Off`, `Turbo Control=Manual`, `CPU Speed=4` | CHECKED; UNAVAILABLE; UNAVAILABLE |
| SID group and K17 in one file (XM2 tables): K11 lines on lines 10-14 and `SID audio` on line 20 | K17 wins; with `SID audio` on line 12 instead, the K11 group (last member on line 14) wins |
| XM7 tables: `SID audio=Dual $D500` on line 5, then `UltiSID 2 Address=$D420` on line 9; and the reverse order | the preset's fields, then `ultisid_addr[1]` = `$D420`, field mask and ADDR class bit set; in the reverse order the plain preset |
| `[Multitini SID]` fixture (`SID 3 Address=$D600`, `SID 5 Pan=Right 5`, `SID Headroom=-6 dB`) | XM2 tables: one UNAVAILABLE "needs multi-SID" for the section, counted once; XM7 tables: three fields APPLIED, not skipped as an unknown section |
| `[SID Sockets Configuration]` | NOT_APPLICABLE before XM7; after XM7 per MSID U4 |
| `Enabled ; note` | BAD_VALUE |
| `DMA Load Mimics ID=8` (no colon) | UNKNOWN_ITEM in a read section |
| `[Some Future Store]` | INFO, section skipped |
| Duplicate `CPU Speed` lines | last wins, INFO |
| File with a NUL byte; GEOS-like binary `.usr` | `.cfg`: refused as not text, no entries applied; `.usr`: treated as absent, UART line only |
| PETSCII text `.usr` with no known section header | treated as absent; no status text |
| 16,384 bytes / 16,385 bytes | parsed / refused before parsing (platform test) |
| Non-ASCII bytes in a comment and in an ignored item | no effect |
| 17 problems | 16 entries kept, counts exact |
| Sidecar path: `a/b/Game.prg`, `Game`, `.hidden`, `x.tar.d64`, `Game.`, 255-byte component, absolute, `..` | `a/b/Game.cfg`, `Game.cfg`, `.hidden.cfg`, `x.tar.cfg`, rejected, rejected, rejected, rejected (same edge set as `tests/host/test_vectrex_overlay.c:129-168` [V]) |
| Sidecar write predicate | `DMBCFG.CFG` at the root with no `DMBCFG.PRG`/`.CRT`/`.D64` → not protected; `GAME.CFG` beside `GAME.PRG` → protected; `game.usr` beside `GAME.D64` → protected (case); `GAME.CFG` in `uci_temp` → not protected (outside the media root); a rename of `X.CFG` to `GAME.CFG` beside `GAME.PRG` → protected as a target |
| Summary formatter | every template of section 3.12 fits 63 characters with a 20-character shown name and counts of 999; a 29-character name is shortened to 15 + `~` + `.cfg`; precedence: error over SuperCPU, SuperCPU over skipped, skipped over the Command Interface note, the note over clean |
| Writer (XM2d): merge into the heartbeat_like file | only rewritten or added groups change; every other byte identical; the SID group (UNAVAILABLE on XM2) keeps every `[SID Addressing]` line byte-identical; re-parse gives the written values; a U64-reader replica (P-rules in U64 mode) accepts every written line except `CPU Speed=64` on MK1 tables |
| Writer: tt8_like saved on an XM2 image | every SID Addressing line byte-identical; only added groups appended |
| Writer: doom_like with `C64U Turbo Registers`, mode unchanged | the label stays `C64U Turbo Registers` |
| Writer: fixture with `SID Socket 2 Address=$D420`, effective Dual `$D500`, SID group owned | both socket lines `Unmapped`, `UltiSID Range Split=Off`; re-parse gives Dual `$D500` |
| Writer: stored byte 27 = 0, SPEED owned at 16 MHz | `Turbo Control=Manual`, `CPU Speed=16` |
| Writer: existing file of 16,380 bytes that needs new keys | refused "Game.cfg would exceed 16 KB"; file untouched |

### 7.2 Overlay and effective tests: `tests/host/test_c64_title_overlay.c` (new)

- Effective = stored for an empty overlay, for every caps combination of `tests/host/test_c64_features.c`.
- Each group alone, all groups together; B3 inside the group; `cartridge_after = true` forces `applied` REU and UA off and leaves `requested` unchanged.
- Stored byte 27 = `0x00`, enabled true: a SPEED-only overlay `CPU Speed= 1` gives index 0; a BADLINE-only overlay `Badline Timing=Disabled` gives CONFIG bit 6 = 0; a FOLLOW-only overlay gives bit 18 (with caps); a TURBO_CONTROL `Off` overlay plus a SPEED overlay keeps the file's speed when turbo is switched back on.
- `cartridge_after` per caller: CRT attached + PRG launch whose `.cfg` says `RAM Expansion Unit=Enabled` → the commit writes `C64_EXPANSION_CONFIG` bit 3 = 1.
- Mode 2/3 downgrade reported when caps are missing; speed clamp reported.
- Apply counter unchanged when the effective bits 6:0 do not change (#787).
- SCPU group sets `flags` bit 3 only; `mt_c64_turbo_config()` sets bit 20 (shared with SCPU's tests).
- `_edit()` for every argument and controller action of section 3.11, including the coupling rule (arg 12 with SPEED owned and turbo off adds TURBO_CONTROL); unowned groups untouched.
- `_merge()` keeps unnamed groups; `_clear()` returns to stored.
- `_merge()` per field for SID (XM7): file 1 sets `UltiSID 1 Address`, `UltiSID 2 Address` and `UltiSID Range Split`; file 2 sets only `Pan UltiSID 1` → the addresses stay, group 1 pan is replaced, `pan_from_preset[0]` cleared, class bits = ADDR and MIX.
- `_merge()` base: overlay owns EXPANSION_MEM {REU off, UA off}; a `$50` file with only `Map Ultimate Audio $DF20-DFFF=Disabled` → REU stays off.
- `_owns_sid_field()`: a per-SID `addr` with no field bit but the ADDR class bit set counts as owned.
- Owned SID sub-page edit (XM7): the overlay changes; the `c64_sid.bin` image is byte-identical.

### 7.3 Content service tests: `tests/host/test_c64_content_service.c` (extended)

- Hook called once, after the read and validation, before the reset release and the stream; register log order checked on the fake register file.
- Not called on READ_FAILED, REJECTED, INVALID_ARGUMENT, REU_CONFLICT.
- `hold_reset` asserts reset before `commit()`; `release_stale_core_reset()` releases it.
- `commit()` error → CONFIG_FAILED, nothing streamed.
- CRT REU check uses `hook->reu_requested`.
- NULL hook → register log identical to today's for PRG, CRT, staged PRG, disk-load prepare and detach.
- Hooked detach: commit after a successful detach, inside a reset hold; not called when the detach fails.
- PRG load with a CRT attached: the commit precedes the detach; the log shows `C64_EXPANSION_CONFIG` written, then the detach stream, then the PRG stream.

### 7.4 Runtime tests (production `main.c` functions with mocked MMIO and SD, as `tests/test_c64_expansion_runtime.py` does [V])

New `tests/test_c64_title_cfg_runtime.py`:

| Test | Checks |
|---|---|
| T-RT1 trace identity | PRG, CRT and all three Mount & Run launches with files On and no file, with files Off, and against the pre-XM1 golden trace: identical MMIO write sequences (the hook is NULL); files Off makes no `.cfg` open. One recorded exception: a profile with REU off while the global UA flag is on now refuses its CRT restore (section 3.8) |
| T-RT1c CRT refusal kept | no `.cfg`, stored REU on, CRT launch through the menu and through `:load`, files On and Off → refused (REU_CONFLICT path), no MMIO write; files On gives the new text |
| T-RT2 apply order | with a file: reset held, registers written, reset released, stream started, in that order; with a CRT attached, the gap between the release and the detach or first CRT byte is recorded |
| T-RT3 replace and revert | title A with file, title B without → registers = stored; Revert row → stored; files Off → stored |
| T-RT4 end events | each row of section 3.10 on a mocked machine: overlay and session state afterwards **and the register values** (`C64_EXPANSION_CONFIG`, `C64_CPU_CONFIG`, `CONTROL` bit 5, joystick port) equal effective(stored) or the kept overlay. Covers CRT detach, the fast-ROM toggle for each source kind, Options → Reset with a D64 overlay (kept while drive 8 holds a disk, cleared when it is empty), Revert of a kept D64 overlay, and UCI REBOOT (XM5): overlay, session and registers unchanged; `restore_selection_settings()` not called; no `.cfg` opened; no CRT stream with a CRT attached; a key merged by `$50` still present; drive 8 holds the same image |
| T-RT5 CRT + REU | stored REU on, files On, Cartridge row → the browser opens; CRT with `RAM Expansion Unit=Disabled` loads; RAM expansion row shows `16 MB REU (cartridge)`; Enter on it is refused "Eject the cartridge before enabling REU" and saves nothing; Options → Reset restores the CRT with the file applied and no apply event; files Off then Reset → "cartridge restore skipped" as today |
| T-RT5b REU back after the CRT | stored REU on, CRT with an REU-off `.cfg` loaded, then a PRG without a `.cfg` → `C64_EXPANSION_CONFIG` bit 3 = 1 after the launch. Same after Revert, then CRT detach |
| T-RT6 disk swap | Mount & Run disk 1 with file, mount disk 2 → overlay unchanged, status hint |
| T-RT7 menu edits | owned group edit → `settings.bin` image byte-identical, register changes, row shows `(title)`; unowned edit → saved as today; overlay owns SPEED only, stored turbo Off, CPU faster → `settings.bin` unchanged, overlay now owns TURBO_CONTROL and SPEED |
| T-RT8 no-leak property | 2,000 seeded random sequences of {launch with/without file, menu edit, controller action, keyboard toggle, auto-boot toggle, joystick swap (menu and UART `j`), Revert, Reset, fast-ROM toggle, detach, mount, files On/Off}: after every step the decoded `settings.bin` equals a model of `stored` that never sees overlay values, and the registers equal the model's effective values |
| T-RT9 joystick leak | overlay owns JOYSTICK, toggle keyboard map and toggle auto-boot → saved port = stored port; UART `j` → overlay port swaps, nothing saved, live port = overlay port |
| T-RT10 failure paths | read failure before commit (overlay unchanged); reset-hold failure at commit (stored written, overlay and session cleared, "Settings could not be applied; saved settings restored"); transfer failure after commit (overlay cleared, stored written) |
| T-RT11 status texts | every text of section 3.12 at its longest (20-character name, counts 999) is at most 63 characters and ASCII only; precedence cases: SuperCPU + skipped → SuperCPU text; launch error + SuperCPU → error; Command Interface + clean → note; skipped + Command Interface → skipped; Reset with a kept overlay owning SCPU Enabled → `Core restarted; SuperCPU Detect stays on (title .cfg)` (XM3) |
| T-RT12 busy files | drive 9 holds `Game.cfg` open → the launch goes on without it, "open on drive 9"; Save refused with the same text |
| T-RT13 images without turbo caps | overlay with `Turbo Control=Manual` on an ABI ≥ 8 image without `caps.turbo` → `CONTROL` bit 5 unchanged; CPU turbo row Enter saves `!stored`, never an overlay value |
| T-RT14 sidecar writes through drive 9 | with `GAME.PRG` and `GAME.CFG` in a folder: drive 9 `@0:GAME.CFG` replace and append → refused (write protect); `LOAD"GAME.CFG",9` (read) → allowed; with no `GAME.PRG` → replace allowed |

`tests/test_menu_runtime.py` (extended): rows 17-19 visibility, Enter actions, the paused-apply rule [V tests/test_menu_runtime.py].

### 7.5 Settings and front-end tests

- `tests/host/test_supervisor_settings.c` `test_c64_turbo_storage()` [V :983-1002]: at XM2, set `c64_title_cfg_off = (detail & 0x20U) != 0` and assert `stored_file[26] == (detail & 0x27U)` (bits 0-2 and 5) at :994; the future-bit mask at :996 becomes **`0xD8`** (bits 3, 4, 6, 7, which have no firmware field yet), so reserved bits 3-4 keep their downgrade coverage; a separate bit-5 round trip through the frozen schema-10 fixture keeps the file and zeroes bit 5 on its save (downgrade = files On, the default). SCPU narrows the mask to `0xD0` at XM3 and UCI to `0xC0` at XM5 [D SCPU §7.2; UCI §4.6].
- `tests/host/test_core_frontend.c`: release argument order gains 17 (and 18, 19 when active); `(title)` suffix on owned rows with every row's text in its own buffer (two owned rows at once do not share text); `(cartridge)` on the RAM expansion and UA rows; Cartridge row enabled with stored REU on and files On (help names the `.cfg` rule), disabled with files Off; largest row set ≤ 20.

### 7.6 Simulation benches and gates of other features

- No new bench: XM2 changes no RTL.
- One new case in the existing `hdl/sim/tb_c64_expansion_config.sv` for R14: REU and UA enables set while `cart_loaded` = 1, then the detach; expect no REU or UA I/O decode and no DMA while the cartridge is loaded, and a clean REU start after the detach [O].
- Existing gates still run unchanged: G-STK, G-TURBO, G-AUD, G-SPEC. G-STK runs `scripts/test_c64_ua_stock_bus.py` in compare mode, which checks only the trace [V scripts/test_c64_ua_stock_bus.py:129-137]. The manifest `tests/c64_ua/stock_bus_manifest.json` is the record of the one-time capture and is never edited: the checker writes it only under `--capture`, which refuses once the golden file exists [V scripts/test_c64_ua_stock_bus.py:64, 129-134]. Source hashes go into the receipt `build/c64-ua-stock-golden/validation.json` [V :144] and into the milestone build receipt.
- G-IEC9 (`scripts/test_c64_iec9.py`, `tests/host/test_c64_iec_dos.c`) runs again at XM2, because drive 9's protected-path callback gains the access kind and the sidecar rule.
- XM5 cases owned by UCI (listed here because this design defines the behaviour):
  - UCI host tests `test_c64_uci_control.c`: DOS write of `/temp/uci_config.cfg`, then `$50` with no path bytes → `00,OK`; `$50` with `RAM Expansion Unit=Enabled` while a generic cartridge is attached → `89,CONFIG FILE HAD ERRORS`, nothing applied;
  - `test_c64_uci_dos.c`: create `DMBCFG.CFG` at the root through `/usb*/` → allowed; write `GAME.CFG` beside `GAME.PRG` → `98,FUNCTION PROHIBITED`; read `GAME.CFG` → allowed;
  - `tests/test_c64_uci_runtime.py`: REBOOT keeps the overlay, session and registers and calls no `restore_selection_settings()`;
  - T-U4 (`hdl/sim/tb_c64_uci_wiring.sv`): a `$50` REU/UA change written under `HOLD` while a program runs an REU STASH/FETCH loop; the write happens only when paused and REU IDLE; no lost DMA [O].

### 7.7 Board tests (named software)

Run inside `:debug on` with `:turbostat` logged where turbo matters, plus one sessionless control run [D TD §6.5].

| # | Test | Pass |
|---|---|---|
| B1 | G-REU0: Expand by Bonzai disks 1 and 2 with files On and no `.cfg` anywhere; reudiag `reutiming` | identical to the baseline run on image 4D00C707 with dev7 firmware [XP §13] |
| B2 | G-REU0 with a hand-written `Expand by Bonzai (Disk 1).cfg` (`Turbo Control=Off`, `RAM Expansion Unit=Enabled`, `Map Ultimate Audio $DF20-DFFF=Disabled`) beside disk 1, global turbo 48 MHz, Mount & Run; swap to disk 2 at the prompt. Run once with stored UA Off and once with stored UA On | demo identical to stock in both runs (Expand needs UA off [V core_frontend.c:208]); the overlay stays across the swap; the next launch without a file returns to 48 MHz |
| B3 | Driller, Stunt Car Racer, Hard Drivin' (reported unplayable at a fixed speed [R ISS §2]) with `Turbo Control=Off` or `CPU Speed= 1` while the saved speed is 48 MHz | play at stock speed; `settings.bin` unchanged (hash over USB SD export before and after) |
| B4 | Hotkey CPU faster/slower during B3 | status "for this title"; `settings.bin` unchanged; Save row writes the file; relaunch uses it |
| B5 | Heartbeat Soundtracker with its UE2 16 MHz `.cfg` renamed to the program's stem | XM2: turbo, REU, UA applied, "3 skipped"; XM3: SCPU applied; XM5: UCI applied; XM7: 8 SIDs |
| B6 | The Tuneful Eight: `tt8-ultimate.prg` with `tt8-ultimate.cfg` as shipped in the U64 1.24 package (same stem, no rename [R, zip listing: `tt8-ultimate.cfg` 763 bytes, `tt8-ultimate.prg` 27,536 bytes]); a direct real-world check of the same-name rule | XM2: notice "needs multi-SID"; XM7: 8 SIDs at `$D400`-`$D5A0` |
| B7 | Doom (C64U) with `doom.cfg` copied as `launcher.cfg`, REU image loaded by hand | XM5: turbo registers, REU, UA, Command Interface applied; Network Settings logged as never applied |
| B8 | A CRT the user has rights to, with stored REU on and `RAM Expansion Unit=Disabled` in its `.cfg`, chosen from the Cartridge row | browser opens; CRT loads; Options → Reset restores it; the next PRG launch has the REU back on (the commit precedes the detach, section 3.8) |
| B9 | GNW64 detection with `SuperCPU Detect (D0BC)=Enabled` (XM3) | result as G-SCPU expects [XP §14]; SCPU B10(d) covers the GEOS swap with a D64 and a PRG source, both ended with Revert |
| B10 | Power cycle after B3-B8 | no overlay; `settings.bin` hash equals the pre-test hash except for rows the user changed unowned |

### 7.8 Gate G-CFG (XP §14, expanded)

G-CFG passes when sections 7.1-7.5 pass, T-RT1 shows identical traces, T-RT8 finds no leak, G-IEC9 passes again, and B1-B4, B8 and B10 pass on the board. B5-B7 and B9 belong to the milestones that add their keys.

Wording of the gate's first clause. The first frame draft said "No `.cfg` → byte-identical behaviour". This design meets that for MMIO writes and `settings.bin`, but not for the menu: with files On (the default) and the REU on, the Cartridge row and its browser are enabled, and the refusal moves to the launch with a new text (section 3.12). XP §14 now uses the wording this design proposed: "No `.cfg` gives byte-identical MMIO writes and `settings.bin`; the one listed UI change is the Cartridge row and its browser guard" (section 8.1 F-f).

### 7.9 Test results

None. Nothing in this document has been built, simulated or run on the board.

---

## 8. Open questions and user decisions

### 8.1 Changes this design made to the shared frame

The first draft of the shared frame had rules that do not work for title files. F-a to F-m changed them. XP now carries all thirteen (XP §5.1, §5.4, §6.4, §7.1, §7.3, §7.4, §8.1, §8.2, §8.4, §8.5, §14, P56); the table keeps the reasons. The decision record for this feature is Q22; it claims no deviation number. None of these items changes a register, bit or settings byte.

| # | First frame text | Problem | Change | Now in XP |
|---|---|---|---|---|
| F-a | "eject or detach of the medium that supplied it" ends the overlay | Multi-disk titles: the game asks for disk 2, the user mounts it (the documented flow [D docs/C64_SD_STORAGE.md:114-115]), and turbo or REU would change under the running game | Only a CRT detach ends the overlay; the hooked detach then commits the stored values inside a reset hold (section 3.10). A drive 8 eject or mount never does | §8.4 |
| F-b | boot restore of the stored CRT or D64 does not re-apply its `.cfg` (marked USER) | A CRT loaded because its `.cfg` turned the REU off is stored with the stored REU on. Without the file, every boot and every Options → Reset skips it with "turn REU off first" | Re-apply on the **CRT** restore (a relaunch of the same title, `apply = false`). Keep "no re-apply" for the D64, which is only mounted | §8.4, P15 |
| F-c | "A new source replaces the old overlay" | UCI `$50` runs inside a title session. Replacing would drop the title's REU, UA or turbo under the running program | Title launches replace; UCI `$50` merges (per field for SID). UCI §3.8.4 states the same | §8.2 rule 5, §8.5 |
| F-d | ".cfg parse ≤ 4 KB file" | A full U64 "Save to File" dump is about 5-9 KB [I], so U64 full dumps would be refused | 16 KiB cap; parse time stays far under 1 ms [I] | §7.1, §8.6 |
| F-e | arguments 17-18 only | "Save settings for this title" (now allowed) needs a row | Argument 19, shown while a title session exists | §5.1 |
| F-f | G-CFG: "No `.cfg` → byte-identical behaviour" | With files On (the default) and the REU on, the Cartridge row and its browser must stay enabled, or a CRT whose `.cfg` turns the REU off can never be chosen; the refusal moves to the launch with a new text (section 3.12). The alternative, keeping the row disabled until a CRT folder holds a `.cfg`, needs a folder scan per menu build | G-CFG: "No `.cfg` → byte-identical MMIO writes and `settings.bin`; the listed UI change is the Cartridge row and its browser guard" | §14 |
| F-g | the B1 location list | It named `handle_c64_content_load()`, the LOAD_CONTENT branch and core_frontend.c:93-99, but not the `MT_MENU_ACTION_BROWSE_CARTRIDGE` guard [V main.c:4378-4381], which also refuses while the live REU is on | Add the guard to B1; this design changes it (section 4.3) | §6.4 B1 |
| F-h | Options → Reset ends the overlay | A D64 title stays mounted across the reset [V main.c:2462-2470], so `LOAD"*",8,1` after a crash would run the same title at the global settings (for example 48 MHz for a game whose `.cfg` sets 1 MHz). The U64 keeps loaded values across resets [V u64_config.cc:1035-1050] | Keep a D64-origin overlay across Options → Reset and the fast-ROM restart while drive 8 holds a disk (U6). PRG-origin overlays still end; CRT ones are re-applied by the relaunch | §8.4, P16 |
| F-i | one effective value for every use | One effective value cannot serve both the CRT refusal and the register write: clamped, it hides a requested REU from the refusal; unclamped, it writes REU with a cartridge | `mt_c64_effective()` returns `requested` and `applied` (section 3.8). No allocation changes | §8.1 |
| F-j | Options → Reset is an end event; the UCI draft made REBOOT "the same reset and stored-media restore as Options → Reset" | Options → Reset clears a PRG overlay and relaunches a CRT, which re-reads `Game.cfg` and drops keys merged by `$50`, the case a `$50`-then-REBOOT client needs. Upstream REBOOT is `start_cartridge(NULL)`, which keeps the RAM settings [V control_target.cc:134-137; c64_subsys.cc:279-285] | One definition in section 3.10 (REBOOT row), used by UCI §3.8.3 and §4.5: a C64 reset through `set_core_reset_request()`; no restore, no `.cfg` re-read, no cartridge relaunch; overlay, session and registers unchanged | §7.3, P17 |
| F-k | XP §5.4: the launch status line is "one line, 122 columns", names up to 40 characters, lower cases appended while they fit | The drawn field is 122 columns [V firmware/common/menu.c:1543 `MT_UI_COLUMNS - 6U`], but the buffer holds 63 characters and the setters truncate [V main.c:190, 1976-1987]. XP's example texts (for example the SuperCPU text with `($D0BC reads $01)`) would be cut. SCPU §3.6 already sizes its texts to 63 | Keep the buffer; one text per launch, 63 characters, names shortened to 20 (section 3.12). Alternative: raise `supervisor_status_text` to 123 bytes in XM2 (W5), then XP's longer texts and appended cases fit | §5.4, P56 |
| F-l | XP §7.4: the shared callback refuses "write, create, rename, delete and open-for-write" of real sidecars | Drive 9 asks the callback before every open, also for reads, and answers 70 for a protected read [V c64_iec_service.c:194-200]. Adding sidecars to today's callback would also block `LOAD"GAME.CFG",9` | The callback gains an access kind (read or write); the sidecar rule answers for writes only (sections 3.16, 4.3) | §7.4 |
| F-m | XP §7.3: REBOOT "does not remount drive 8"; XP §8.5 lists `$50` statuses `00`, `88`, `89` | Every C64 reset through `set_core_reset_request()` flushes, ejects and remounts the same drive 8 image [V main.c:767-786, 490-505]; what REBOOT skips is the restore of the stored name. A `$50` whose HOLD cannot reach REU idle needs a status too | Say "keeps the drive 8 image (the reset's own flush and remount); no restore of the stored name". Add `87,INTERNAL ERROR` for a failed hold (section 3.15) | §7.3, §8.5 |

### 8.2 User decisions

| # | XP | Question | Recommended default | Reason |
|---|---|---|---|---|
| U1 | P10 | Default of "Title .cfg files" (XP P10) | On | no file, no effect; the U64 has no switch |
| U2 | P13 | `.cfg` on a plain drive 8 mount | No, with a status hint | a mount does not reset; U64 parity |
| U3 | P14 | `.cfg` on the boot auto-mount and D64 restore | No | mount only |
| U4 | P15 | `.cfg` on the stored CRT restore (boot, Reset) | Yes (F-b) | the CRT restarts, and REU-off CRTs need it |
| U5 | P12 | Editing a group the overlay owns (XP §8.2 rule 7) | Edits the overlay, no save | one game's tweak must not change every game |
| U6 | P16 | Options → Reset (and the fast-ROM restart) ends the overlay | PRG source: yes. D64 source: no, while drive 8 holds a disk (changed after review, F-h). CRT source: re-applied by the relaunch | a PRG is gone after the reset; a D64 stays mounted and the usual crash recovery reloads the same title. Trade-off: a different game loaded by hand from a disk mounted later runs with the old overlay, the same exposure as F-a; the status line names the active file. The worst case is a kept overlay with SuperCPU Detect on and a GEOS disk swapped in (R16, U23); "Revert title settings" ends it |
| U7 | P17 | Reset by C64 software ends the overlay | No | no firmware event; U64 parity. UCI REBOOT follows the same rule (F-j) |
| U8 | P19 | `.usr` fallback | PRG on SD: yes, with the text check. USR inside the mounted D64 for Mount & Run: later; if adopted, the USR file wins over the D64's `.cfg` | parity for PRG; an in-image lookup needs the D64 directory reader and has no known users |
| U9 | P18 | "Save settings for this title" | Yes, XM2d, merge writer (rewrite owned or applied groups, add missing ones, leave held-back groups alone, section 3.14), argument 19 | allowed now; the merge keeps U64 lines and requests this image cannot meet |
| U10 | P22 | System Mode mismatch | Notice only, never switch images | a reload takes seconds; NTSC is held |
| U11 | P23 | `REU Size` other than 16 MB | REU at 16 MB with a notice | no size register; most software works with more |
| U12 | P23 | `GeoRAM Mode` | Skip the REU line with a notice | no GeoRAM |
| U13 | P25 | ROM keys; a Multitini "Fast serial ROMs" key | ROM keys never. Fast ROMs key: later, after XM2 | content and ROM rules; the restage has its own rollback paths |
| U14 | P25 | REU preload from a `.cfg` (Doom needs a preload) | Later option, file beside the `.cfg` only | a 16 MB copy and the launch ordering need their own tests |
| U15 | P25 | A "Run disk" action (reset, `LOAD"*",8,1`, RUN) for D64 and G64 | Yes, as a follow-up | U64 Run Disk parity; gives G64 titles a trigger |
| U16 | P25 | Folder default file | No | not U64; extra lookups |
| U17 | P20 | Title files may enable the Command Interface | Yes, named in the status line (rank 4, section 3.12) | Heartbeat and Doom need it; the network stays a board setting |
| U18 | — | Test fixtures | Hand-written equivalents; originals only in an uncommitted local check | the licence of third-party files is unclear |
| U19 | P24 | `.cfg` files in media browsers | Hidden | not launchable; `:cfg test` covers authors |
| U20 | — | `Joystick Swapper` meaning | Relative: Swapped = the other port from the user's stored port | Normal/Swapped describe a swap on the U64 |
| U21 | P25 | `<name>.<ext>.cfg` to separate `Game.d64` from `Game.prg` | No | not U64; one more open per launch |
| U22 | P21 | May C64 software write title sidecars (`*.cfg`, `*.usr`) through drive 9 or UCI DOS? | No for real sidecars only: a `.cfg` or `.usr` whose stem matches a `.prg`, `.crt` or `.d64` in the same folder refuses write, create, append, rename and delete; reads and all other `.cfg` files stay allowed (section 3.16). Lands with XM2 for drive 9 and XM5 for UCI DOS | sidecars auto-apply, including `Command Interface=Enabled`; UBoot64 and DMBoot write `DMBCFG.CFG` and `DMBSLT.CFG` at the root and must keep working. Alternative: allow all writes and rank the Command Interface note above the skipped count |
| U23 | SCPU Q-SC17 | A D64 overlay kept at Options → Reset (or the fast-ROM restart) that turns SuperCPU Detect on | Keep it, as every other group; the Reset status says `Core restarted; SuperCPU Detect stays on (title .cfg)`; SCPU advises "Revert title settings" before GEOS or GDOS64 | one end rule for all groups; a title that needs Detect keeps it after a crash reload. Alternative: drop the SCPU group from a kept D64 overlay at those two events (safer for a swapped-in GEOS disk; the title loses Detect after a Reset) |

### 8.3 Open items needing measurement or checks [O]

1. SD open cost of two or three missing files in a large folder on the board (R7).
2. REU, UA and SID-map changes under `C64_UCI_CONTROL.HOLD` with an REU loop running (R9; UCI's T-U4 case; XP E-X5, E-X6).
3. Exact U64 item names for the NOT_APPLICABLE tables against the pinned upstream at implementation time.
4. Whether C64U firmware accepts `U64 Turbo Registers` and `[U64 Specific Settings]` (files we write).
5. Size of a real full U64 dump (to confirm F-d; XP E-X12); none was available during design.
6. REU and UA enables set while `cart_loaded` = 1, then the detach (R14; XP E-X7): bench case in `tb_c64_expansion_config.sv`.
7. Whether the C64 restarts on a CRT detach; the loader's detach branch shows no reset [V multitini_c64_content_loader.sv:555-561]. The hooked detach holds the reset either way.
8. RAM for the separate 16 KiB `$50` buffer (section 3.15): check the linker map.
9. Cost of the sidecar write predicate (up to three `f_stat` calls) on a large folder through drive 9.

---

## 9. Work steps in order with size estimates

Sizes: S ≤ 1 day, M 2-3 days, L 4-5 days of focused work, including tests. Line counts are rough [I].

| Step | Content | Depends on | Size |
|---|---|---|---|
| W0 | XM1 (XP): registry, stored snapshot, effective function, `change_c64_expansion()` and `supervisor_menu_view()` from stored/effective, menu limit 20 with capacity test; golden MMIO traces recorded before the change for T-RT1 | — | M (XP) |
| W1 | Parser, sidecar path, text check, report, NOT_APPLICABLE/NEVER tables, resolver for K1-K3, K5-K9, K11 (legacy), K13-K17; status formatter with the 63-character rule and precedence; sidecar write predicate; fixtures; `test_c64_title_cfg.c` (section 7.1 without the writer) | W0 registry | M (~950 lines C, ~750 test) |
| W2 | Overlay object, groups, `_edit/_merge/_clear`; `test_c64_title_overlay.c` | W0 | S-M (~350 C, ~400 test) |
| W3 | Content-service hook and the four hooked entry points (load, stage, disk prepare, detach); `test_c64_content_service.c` additions; the R14 bench case | — | S (~170 C, ~280 test) |
| W4 | `main.c` integration: candidate and commit (changed registers only, NULL hook), all trigger paths, CRT restore relaunch, end events with their register writes, Cartridge browser guard, owned edits with the coupling rule, the three joystick saver fixes and UART `j`, busy probe, `:cfg` commands; drive 9 callback access kind and the sidecar rule; G-IEC9 again | W1-W3 | L (~700 lines) |
| W5 | Settings bit 5; rows 17-18; `(title)` and `(cartridge)` markers with per-row text buffers (~4.8 KB static); Cartridge-row rule; status texts; settings and front-end tests | W4 | M (~280 C, ~320 test) |
| W6 | Runtime tests T-RT1 … T-RT14 and menu runtime additions | W4-W5 | M-L (~1,050 test) |
| W7 | Board run: B1-B4, B8, B10; G-REU0 with the new firmware [XP §13] | W6 | M (2 board days) |
| W8 | Docs of section 4.8, TD Q22 record | W7 | S |
| W9 (XM2d) | Merge writer (group rules, socket lines, label keeping, 16 KB limit, busy probe), argument 19 (after XP allocates it), `:cfg save`, journal recovery in the lookup, writer tests, board B4 save part | W5 | M (~400 C, ~400 test) |
| W10 (XM3) | Register K4 in the SuperCPU row; parser tests flip to APPLIED; SCPU's Reset and launch texts and the SuperCPU status rank; board B9 | XM3 | S |
| W11 (XM5) | Register K10; UCI `$50` through `mt_c64_cfg_parse()`/`_resolve()`/`_merge()` with the effective base; all-or-nothing; the cartridge block; live apply under `HOLD`; reply formatter; REBOOT action without restore; board B5 (UCI part), B7 | XM5 | M |
| W11a (XM5) | `$50` default path through the UCI `Temp` alias (`<package directory>/uci_temp`), own 16 KiB buffer; the sidecar write predicate in the UCI DOS targets with UCI's DOS host cases | XM5, U22 | S |
| W12 (XM7) | MSID registers K11 (full), K12 and K18; per-field SID overlay with key-class bits; resolver step 2 XM7 rule; `[Multitini SID]` section; K17 presets; per-item ownership in `change_c64_sid_setup()`; per-field `_merge()`; the save writer covers the SID model; board B5, B6 | XM7 | M (MSID owns most) |
| Later | U15 "Run disk" action (M), U8 USR in D64 (S-M), U14 REU preload key (M-L), U13 fast-ROM key (M) | user decisions | — |

Order rationale: W1-W3 are independent and host-testable before any `main.c` change. W4 lands only after the hook and the overlay are proven on the host. W7 must pass G-REU0 before W9 adds file writes.

---

## 10. Review

Three review rounds checked this design against the source before this write-up. Each issue was verified before it was applied. No register bit, settings byte or milestone changed. Rules where this design departs from XP are in section 8.1.

The first two rounds changed:
- Effective values split into `requested` and `applied`, so the CRT refusal still fires while the registers get the clamped values; `cartridge_after` set per caller (F-i).
- Every overlay end writes effective(stored); a hooked cartridge detach; fast-ROM handling per source kind.
- The EXPANSION_MEM resolution table; launch rules run before it; byte 27 fields laid over the normalised byte.
- A merge writer for Save that keeps U64 lines, label spellings and requests this image cannot meet; a 16 KB limit for written files.
- A NULL hook and changed-registers-only commit, so the no-file trace stays identical.
- The coupling rule for actions that touch several groups; per-row text buffers; the UART `j` path; turbo keys gated on `caps.turbo`.
- A busy probe before journal operations; a separate `$50` buffer; quiet `.usr` handling; CR-only line ends; turbo keys ADJUSTED for cartridges.
- Options → Reset keeps a D64 overlay while drive 8 holds a disk (U6 changed, F-h).
- The Cartridge row and browser guard (F-f, F-g).
- Rejected: calling the hook after the detach inside `load_image()` (it would not hold the reset [V c64_content_service.c:288]); writing `Ext DualSID Range Split=Off` (the real files keep `A7`).

The cross-document review changed:
- Baseline: dev7 committed as `3df6d04` (image `4D00C707`, ABI 0.25); anchors re-checked; XM2 builds no image, so the image ceilings do not gate it.
- U22 protects only real sidecars, for writes only; UBoot64 and DMBoot configuration files stay writable; drive 9's callback gains the access kind, and the rule lands with XM2.
- The `$50` default file follows UCI's `/Temp` mapping (`<package directory>/uci_temp`, emptied at C64 core start).
- UCI REBOOT is defined once, in section 3.10 (F-j).
- The SID overlay is per field from XM7, with key-class bits; `[Multitini SID]` is a known section; K12 is part of the SID group; K17 gains the multi-SID presets; SID sub-page edits respect ownership.
- `$50`: REU, UA and SID-map changes under UCI's `HOLD`; REU and UA enables BLOCKED while a cartridge is attached; the resolver base is the effective value.
- Status line: the 63-character limit, name shortening, a precedence, and the SuperCPU and Command Interface cases.
- Byte-26 test step at XM2 (bits 0-2 and 5, mask `0xD8`).
- G-STK wording: compare mode, manifest never edited.
- A kept D64 overlay with SuperCPU Detect on: Revert is the documented way out, the Reset status names it (SCPU's text), and U23 records the alternative.
- A third joystick saver (the auto-boot toggle) found and fixed.
- Cross-checked against XP, SCPU and MSID as written: XP carries F-a to F-m, including the 63-character status line (§5.4, P56), the callback access kind (§7.4), the REBOOT drive 8 wording (§7.3) and the `87` status (§8.5). SuperCPU texts and the SID overlay record follow SCPU §3.6 and MSID §3.10.
