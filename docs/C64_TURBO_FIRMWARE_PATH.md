# C64 turbo evidence: firmware, menu and ABI path

Investigation report written on 2026-10-23 for the design in [C64_TURBO_DESIGN.md](C64_TURBO_DESIGN.md), against commit 5341812 (branch c64-turbo). It is design-stage evidence: no repository file was changed and nothing here is board-tested. Paths to throwaway session files were removed; those files are not kept.

Original title: C64 CPU turbo: path from menu to hardware, and what a multi-level speed selector would need (investigation only)

I made no changes to the repository and wrote no scratch files. I ran two read-only checks:
- `python scripts/generate_registers.py --check` exited 0, so the generated headers match `spec/interface_v0.json` at ABI 0.23.
- `python -m unittest tests.test_interface_spec` ran 10 tests, all OK.

Answers in brief:
1. The setting is one bit end to end: menu row → CONTROL bit 5 → a 1-bit synchronizer → the turbo guard → FPGA64, where `turbo_speed` is wired to `2'b00`.
2. Put the speed in a new register pair (0x0D8 config, 0x0DC caps) with a coherent mailbox, copying the IIgs pattern. Do not use CONTROL bit 4 or pack more bits into CONTROL.
3. Reuse byte 243 as the speed code, with 1 still meaning 2x. Old records stay valid with no schema change.
4. The ABI minor goes 0.23 → 0.24. The full file list is in section 4.
5. Only `tests/host/test_core_frontend.c:117` asserts the text "Smart 2x".

---

**1. How the setting flows today (verified)**

Saved setting:
- `supervisor_settings.h:37`: the profile field is `bool c64_cpu_turbo`.
- `supervisor_settings.c:318-326`: `mt_supervisor_settings_remember_c64_cpu_turbo(settings, core_id, bool)`.
- `supervisor_settings.c:598`: the encoder writes `output[243] = turbo ? 1 : 0`.
- `supervisor_settings.c:763`: the decoder rejects the whole file if `input[243] > 1`, for every schema.
- `supervisor_settings.c:772`: `c64_cpu_turbo = input[243] != 0`.
- The schema is 10 (`supervisor_settings.c:15`). The profile is 290 bytes and the file is 10966 bytes (`supervisor_settings.h:20`). Byte 243 has kept its meaning since schema 1; `test_supervisor_settings.c:959-967` checks this.

Applying the setting at core start and after a core reset:
- `main.c:2311-2314` in `restore_selection_settings`: `configure_c64_expansion(...)`, then `set_c64_cpu_turbo(package.abi_minor >= 8 && profile && profile->c64_cpu_turbo)`.
- This reads the ABI from `core.cfg` only. It does not read the live ABI register or any capability register.
- `main.c:240-247`, `set_c64_cpu_turbo`: read-modify-write of CONTROL (0x01C), setting or clearing `MT_CONTROL_C64_CPU_TURBO_ENABLE` (bit 5), then marks the menu dirty.

Building the menu:
- `main.c:2481-2482`: `state.cpu_turbo_enabled` comes from the live CONTROL bit 5, not from the profile.
- `multitini_core_frontend.h:29` declares `bool cpu_turbo_enabled`.
- `core_frontend.c:111-127`: the row appears only if `abi_minor >= 8`. Label "CPU turbo"; value "Smart 2x" or "Off"; help text "Enable Smart 2x CPU acceleration; drive timing remains native."; action `MT_MENU_ACTION_TOGGLE_CORE_OPTION` with argument 1; checkbox control at lines 125-126.

Menu input:
- The Options page has no Left/Right handling. Only Enter activates a row: `menu.c:372-378` sends `file_index = argument`. Left/Right are handled only on the Cores, Video and Effects pages (`menu.c:780-822`).

Action handler:
- `main.c:3551-3558`: C64 argument 1 does not need the core to run, so it applies while the menu keeps the machine paused. `test_menu_runtime.py:381-382` asserts this.
- `main.c:4505-4508`: arguments ≥ 2 go to `change_c64_expansion`.
- `main.c:4512-4519`: rejects the action if the core is not C64, `abi_minor < 8`, or `file_index > 1`.
- `main.c:4594-4608`: computes the new state from the live register (`enabled = !(CONTROL & bit5)`), saves settings, then calls `set_c64_cpu_turbo`. If the save fails it rolls back and shows "CPU turbo setting not saved".

Register file:
- `multitini_control_regs.sv:507-508`: CONTROL is a plain 32-bit byte-strobe merge. It does not clean reserved bits, so they read back whatever was written.
- Reset value 0x0000000F (lines 355-361).

Clock crossing:
- `multitini_one_top.sv:869-874`: `cdc_bit_sync c64_cpu_turbo_sync` crosses bit 5 alone into `c64_clk`.
- The XDC false-paths the first stage only (`multitini_timing.xdc:70-73`, the `sync_meta_reg` pattern).
- Line 1203 drives `machine_adapter.cpu_turbo_enable`.

Adapter and FPGA64:
- `multitini_c64_adapter.sv:556-568`: `multitini_c64_cpu_turbo_guard` produces `cpu_turbo_active`. It is gated by IEC and parallel-strobe activity plus a holdoff of `(CORE_CLOCK_HZ+1)/2` machine cycles, and it does not depend on the speed level.
- `multitini_c64_adapter.sv:513-515`: `.turbo_mode({cpu_turbo_active, 1'b0})` (Smart bit), `.turbo_speed(2'b00)` hard-wired, `.local_turbo_allowed(!cart_loaded)`.
- `fpga64_sid_iec.vhd:1076-1087`: `turbo_speed` is sampled at CYCLE_EXT1 and CYCLE_EXT5. "00" adds the 16 slot (`turbo_m "010"`, 2x), "01" gives `"110"` (3x), "10" gives `"111"` (4x), "11" sets `turbo_dense` (about 7 slots).
- So the existing hardware already has four levels behind a 2-bit port that is tied to 00. Exposing 2x/3x/4x/dense needs only plumbing, no core change.

**2. Where to carry a 3–4 bit speed field**

Free space (verified):
- CONTROL bits 4 and 7–31 are free. `spec/core_interface_v0.md:276` says "Control bit 4 is reserved and ignored". Git history shows a drive-warp option was tried and removed (commit 1b6423f). `test_build_inputs.py:230-233` asserts that `C64_WARP_ENABLE` and the word "warp" do not appear in the adapter, drive, top or spec.
- Register offsets 0x0D8–0x13C are unused. The last two used are 0x0D4 (`C64_EXPANSION_CAPS`) and 0x140 (`VIDEO_EFFECT_ADDRESS`).
- `C64_EXPANSION_CONFIG` (0x0D0) uses bits [3:0]. The hardware masks reserved bits to zero (`clamp_c64_expansion`, `control_regs.sv:220-225`). It crosses clocks through a coherent 4-bit `multitini_async_mailbox` built on `xpm_cdc_handshake` (`multitini_c64_expansion_config.sv`, `top.sv:1188-1193`).
- The generator only accepts single-bit entries under `bitfields` (`generate_registers.py:49-58`). Multi-bit fields go under `constants` as SHIFT/MASK, the way BORDER, VECTREX and C64_EXPANSION already do.
- Writing to or reading from an unknown offset on an older image sets `MT_ERROR_BAD_WRITE` or bumps the bad-read counter (`control_regs.sv:320-345`, `rd_known`). Every new access must therefore be gated.

How the IIgs does multi-level speed (the pattern to copy):
- Capability register: `IIGS_ACCEL_CAPS` (0x0CC, read-only, set by a top-level parameter; `top.sv:735` gives `32'h8000037f`). Bit 31 = valid, bits 6:0 = mask of supported speed codes, bits 8 and 9 = other features.
- Firmware reads it only if the live ABI major matches and minor ≥ 22 (`iigs_content_service.c:638-646`). Otherwise it uses the legacy 5-speed mask 0x1F.
- The speed itself does not travel in a register. It is byte 1 of the 16-byte `MT_LOAD_IIGS_CONFIG` stream (`send_config`, `iigs_content_service.c:102-111`), applied live without a restart (`apply_configuration`, line 715).
- Menu: `core_frontend.c:249-252,271-274`. Absolute-MHz labels "2.8 MHz (stock)", "3.6 MHz", "4.8 MHz", "7.2 MHz", "14.3 MHz", "28.6 MHz", "57.3 MHz". The control is a plain value (no checkbox). The row is disabled unless the card is ZipGS. "Unavailable" shows if the saved code is not in the mask.
- Each Enter steps to the next supported code and wraps (`mt_iigs_next_speed`, `iigs_content.c:38-46`; tested in `test_iigs_content.c:119-124`).
- Storage: its own file `gs-settings.bin` with magic "MTGSSET2" and a CRC; the speed is at `settings_wire[9]`. A higher saved speed is clamped down to the mask at start-up (`iigs_content_service.c:662`). Choosing card None forces speed 0 (`normalize_config`, line 97).
- Another multi-level precedent: Vectrex beam gain and opacity, and the video multiplier, are index fields inside a config register with SHIFT/MASK constants and Left/Right handling (`menu.c:247-330`).

Recommendation (inference):
- Add `C64_CPU_CONFIG` at 0x0D8 (rw) and `C64_CPU_CAPS` at 0x0DC (ro, parameter), copying the IIgs and C64-expansion contract:
  - Caps: bit 31 = valid; bits [15:0] = mask of supported speed codes.
  - Config: speed code in bits [3:0]; reserved bits read zero; values outside the caps mask are clamped.
  - Cross into `c64_clk` with a second coherent mailbox, a copy or parameterised version of `multitini_c64_expansion_config`.
- Keep CONTROL bit 5 as the master enable. Effective speed = (bit 5 && guard active) ? code : 1x.
- Reset the code to the 2x value. Then firmware from ABI 0.8 to 0.23, which only sets bit 5, still gets 2x on a new image.
- New firmware writes the code first, then sets bit 5 to (level ≠ Off).
- Do not carry the field in CONTROL through several `cdc_bit_sync` instances. Each bit would cross on its own (for example 3→4 could show 7 or 0 for a cycle), which breaks the "review every crossing" rule.
- Folding the field into `C64_EXPANSION_CONFIG` bits [7:4] would reuse the existing mailbox, but it breaks:
  - `tb_control_regs.sv:174-181` (reserved bits must read zero);
  - `test_c64_expansion_runtime.py` (its mock MMIO and the "gates" counters);
  - `configure_c64_expansion` (`main.c:2271-2283`), which writes the register from scratch and returns early when SID/REU are missing.
- Suggested code table (open): log2 codes 0=Off/1x, 1=2x, 2=4x, 3=8x, 4=16x, 5=32x, 6=64x, 7=Max, 8–15 reserved. Code 1 then equals today's Smart 2x, which is what keeps byte 243 compatible. If 3x or dense ~7x should also be offered, use an explicit code table and a display-order list in firmware instead of log2.
- Menu: keep argument 1, so `test_menu_runtime.py` and the `file_index > 1` gate still hold. Use a plain value control instead of the checkbox (`core_frontend.c:125-126`). Show the row as multi-level only if `abi_minor >= 24` and the caps mask is valid; otherwise keep today's Off/2x behaviour.
- Add a portable `mt_c64_next_speed(level, mask)` helper, like the IIgs one.
- Optional generic Left/Right on the Options page, sending direction in `action.variant_index` the way Effects does. AGENTS rules say "no core-specific branch belongs in menu.c". It must emit only for rows marked as steppable: otherwise Left/Right on the IIgs "ROM version" or "Accelerator card" rows would trigger a restart. Today 8 levels are reached by Enter cycling only.

**3. Settings compatibility for byte 243 (verified rules, then options)**

Rules today:
- Records are versioned by schema and size. The CRC covers bytes 24 to the end (`supervisor_settings.c:639-642,683-685`).
- Older schemas are accepted only at their exact older lengths (lines 837-845). Any decode failure gives `MT_SETTINGS_CORRUPT`: defaults are used, the warning "Settings invalid; using safe defaults" appears (`main.c:4765-4782`), and the next save overwrites the file.
- No decoder accepts a newer schema, and the docs accept that downgrade is not preserved ("keep a copy of settings.bin", `CONTROLLERS_AND_SCREENSHOTS.md:60`).

Option A (recommended): keep schema 10 and store the level code in byte 243.
- 0 = Off, 1 = 2x (the old meaning), 2..7 = new levels.
- Decoder: change `input[243] > 1` to `> MAX` (it can stay ≤ 1 for schema < 10). Add a range check to `settings_valid`, change the profile field type to `uint8_t`, and change the remember function to take `uint8_t`.
- Every existing record (0 or 1) keeps its exact meaning.
- Older firmware still reads files where the level is 0 or 1. Only a level ≥ 2 makes older firmware treat the whole file as corrupt, losing all profiles and controller maps on its next save.
- No test churn from a schema bump.

Option B: schema 11 and one extra appended byte per profile, as V10 did for the expansion byte.
- Every new save becomes unreadable to older firmware, whatever level is chosen.
- It changes `MT_SETTINGS_WIRE_SIZE` and its `#if` size check, the migration loop in `test_sd_storage.c:684-751`, and the schema byte asserts (`source[8]==10`) in `test_video_settings_runtime.c:205`.

Option C (possible, hacky): use global bytes 26–27.
- No decoder validates them; they are CRC-covered and written as zero.
- They are file-global, not per profile.

At apply time: keep the saved requested level, clamp it down to the image's caps mask when applying, and do not rewrite the file (the IIgs approach). On images below ABI 0.24, any non-zero level means bit 5 only (2x).

**4. What must be bumped**

ABI 0.23 → 0.24. The template is commit 3fdea82, the 0.23 bump.

- `spec/interface_v0.json`:
  - `abi.minor` = 24.
  - Two new register entries with descriptions.
  - Constants such as `C64_CPU_MIN_ABI_MINOR`, `C64_CPU_CAPS_VALID`, `C64_CPU_CAPS_SPEED_MASK`, `C64_CPU_SPEED_MASK`, `C64_CPU_SPEED_DEFAULT`, `C64_CPU_SPEED_MAX`.
  - Keep `"C64_CPU_TURBO_ENABLE": 5` (`test_interface_spec.py:121` asserts it).
- Regenerate with `python scripts/generate_registers.py` (`--check` verifies), which rewrites `firmware/include/multitini_regs.h` and `hdl/generated/multitini_regs_pkg.sv`.
  - `core_artifacts.py:22,131-138` reads the image ABI from the archived `regs_pkg`.
  - `package_cpu1_video_test.py:368-371` requires `--abi-minor` to equal the header value. So bump `scripts/package_core_gs.py` `ABI_MINOR=23` → 24 (as in 3fdea82).
  - `require_runtime_abi` (`core_artifacts.py:184-189`) requires image ABI ≤ firmware ABI.
  - Both C64 variants must have the same ABI (`package_phos1.py`, near line 313), so rebuild PAL and NTSC together.
  - `make_core_package.py:19` defaults to 22; this default is stale, but release scripts pass the value explicitly.
- At runtime there is no upper limit on the package minor (`supervisor.c:42,115`; `main.c:2950-2953` requires live ≥ package). Older firmware will therefore load a 0.24 image and drive only bit 5.
- Gate every new read and write on all three: `package.abi_minor >= 24`, live ABI ≥ 24, and the caps valid bit — the way `c64_content_service_expansion_caps` does (`c64_content_service.c:33-41`).
- HDL:
  - `multitini_control_regs.sv`: new parameter and output, read decode at 227-241, the accepted-write list at 335-338 (otherwise BAD_WRITE), reset value, clamp.
  - `multitini_one_top.sv`: parameter at 734-736, mailbox in `g_c64_machine`, adapter port.
  - Adapter: `turbo_speed` or the new model.
  - `hdl/hdl_sources.txt`, `licenses/files.csv`, `scripts/run_hdl_tests.sh`, `tb_control_regs.sv`, and a new tb for the mailbox.
  - `test_build_inputs.py:217-229` asserts exact text: `.turbo_mode({cpu_turbo_active, 1'b0})`, `.turbo_request(cpu_turbo_enable)`, and the FPGA64 condition string. Do not use the word "warp" in the adapter or drive.
- Firmware:
  - `multitini_core_frontend.h:29`, `core_frontend.c:111-127`.
  - `main.c:240-247`, `2311-2314`, `2481-2482`, `4594-4608`.
  - `supervisor_settings.h/.c`; `multitini_version.h` (the version string has been bumped with each ABI change).
  - `test_video_settings_runtime.c` includes all of `main.c`, builds with `-Wconversion -Werror`, and asserts on any unexpected MMIO (162-185).
- Tests:
  - `test_core_frontend.c:103,117`, plus new cases (legacy ABI, mask clamp).
  - `test_supervisor_settings.c:413,922-967` and `test_sd_storage.c:669` call the remember function with `true`. That still compiles as code 1 with a `uint8_t` parameter, but the field name changes if the field is renamed.
  - `tools/menu_preview.c:72` sets `cpu_turbo_enabled`.
  - `test_interface_spec.py`: add a versioned test for the new registers.
  - `test_menu_runtime.py:381-382` stays valid only if argument 1 still applies while paused. If the new model needs a reset or state handoff to change speed, argument 1 must move into the progress list at `main.c:3551-3558`, and this test changes.
- Docs and spec: `spec/core_interface_v0.md:271-276` plus a new 0.24 section; `docs/ARCHITECTURE.md:58,226,371`; `C64_RAM_CPU.md:27,63,78`; `C64_SD_STORAGE.md:262-266`; `MULTICORE_RUNTIME.md:11,185-191,207`; `SETTINGS_HARDWARE_TEST.md:57`.

**5. Tests that check the "Smart 2x" text (verified)**

- In code, only `tests/host/test_core_frontend.c:117`: `strcmp(view.core_options[1].value, "Smart 2x") == 0`.
- Other code that depends on the current shape but not the text:
  - `test_core_frontend.c:86-91`: two options, with arguments 0 and 1.
  - `tools/menu_preview.c:72`.
  - `scripts/preview_menu.py` only renders and hashes images; it compares against no stored images.
- The string itself is in `core_frontend.c:120-121` and in the docs listed in section 4.

**Risks and open questions**

- Downgrade: with Option A, a level ≥ 2 makes older firmware discard the whole `settings.bin`. Decide whether that is acceptable, as the controller-map precedent did.
- The menu shows the requested level, not the effective one. Turbo is silently off while a cartridge is loaded (`local_turbo_allowed = !cart_loaded`) and during the IEC holdoff. An optional status bit (active, cartridge-blocked) would help the UI, especially for "Max".
- Labels: PAL and NTSC clocks differ, so multipliers ("16x") are clearer than MHz. The IIgs uses MHz.
- If "Max" or 64x needs a different machine clock or bitstream, the existing variant mechanism (up to 8 variants per package, saved per profile) could carry it. Switching variant reloads the FPGA and restarts the core.
- There is no UART command for the turbo or speed setting. A `:speed N` command would make board benchmarking easier.
- None of the above is board-tested. The section-3 options and the section-2 register design are inference.