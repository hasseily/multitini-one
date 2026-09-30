# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import os
import re
import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class BuildInputTests(unittest.TestCase):
    def test_hdl_manifest_is_unique_and_complete(self):
        entries = []
        sections = {"common": [], "c64": [], "gbc": [], "vectrex": [], "genesis": []}
        languages = {}
        active_section = "common"
        manifest = ROOT / "hdl" / "hdl_sources.txt"
        for line in manifest.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            section = re.fullmatch(r"\[([A-Za-z0-9_]+)\]", line)
            if section:
                active_section = section.group(1).lower()
                self.assertIn(active_section, sections)
                continue
            fields = [field.strip() for field in line.split("|")]
            self.assertLessEqual(len(fields), 2)
            entry = fields[0]
            language = fields[1].lower() if len(fields) == 2 else ""
            self.assertIn(language, ("", "systemverilog"))
            entries.append(entry)
            sections[active_section].append(entry)
            languages[entry] = language
        self.assertEqual(len(entries), len(set(entries)))
        # Machine electrical overrides follow the shared pin defaults, and
        # generated clocks exist before the common asynchronous grouping.
        self.assertTrue(entries[-5].endswith("multitini_pins.xdc"))
        self.assertEqual(entries[-4], "board/multitini_psram_iob.xdc")
        self.assertEqual(entries[-3], "board/multitini_vectrex_psram_timing.xdc")
        self.assertIn(entries[-3], sections["vectrex"])
        self.assertEqual(entries[-2], "board/multitini_timing.xdc")
        self.assertEqual(entries[-1], "board/multitini_psram_timing.xdc")
        for entry in entries:
            self.assertTrue((ROOT / "hdl" / entry).is_file(), entry)
        joined = "\n".join(entries)
        self.assertIn("core/c64/ports/fpga64_sid_iec.vhd", joined)
        self.assertNotIn(
            "third_party/c64/upstream/rtl/fpga64_sid_iec.vhd", joined
        )
        self.assertIn("core/c64/multitini_c64_adapter.sv", joined)
        self.assertNotIn("third_party/c64/upstream/c64.sv", joined)
        self.assertNotIn("third_party/c64/upstream/rtl/fpga64_buslogic.vhd",
                         joined)
        self.assertIn(
            "third_party/c64/upstream/rtl/iec_drive/iecdrv_via6522.vhd",
            joined,
        )
        self.assertIn(
            "core/c64/ports/iecdrv_misc.sv",
            joined,
        )
        self.assertNotIn(
            "third_party/c64/upstream/rtl/iec_drive/iecdrv_misc.sv",
            joined,
        )
        self.assertIn("board/multitini_psram_driver.sv", joined)
        self.assertNotIn("third_party/appletini/hdl/psram_driver.sv", joined)
        self.assertIn("core/c64/ports/c1541_logic.sv", joined)
        self.assertIn("core/c64/ports/c1541_gcr.sv", joined)
        self.assertIn(
            "third_party/c64/upstream/rtl/iec_drive/lfsr.v", joined
        )
        self.assertIn(
            "third_party/c64/upstream/rtl/iec_drive/c1541_direct_gcr.sv",
            joined,
        )
        self.assertIn("core/c64/multitini_c64_d64_track.sv", joined)
        self.assertIn("core/c64/multitini_c64_g64_track.sv", joined)
        self.assertIn("core/c64/multitini_c64_1541.sv", joined)
        for excluded_drive_source in (
            "c1541_multi.sv",
            "c1541_drv.sv",
            "c1541_track.sv",
            "iec_drive.sv",
        ):
            self.assertNotIn(excluded_drive_source, joined)
        self.assertNotIn(".mif", joined)
        self.assertIn("core/gameboy/multitini_gameboy_adapter.sv", joined)
        self.assertIn("core/gameboy/ports/gb.v", joined)
        self.assertIn("core/gameboy/ports/cart.v", joined)
        self.assertIn("core/gameboy/ports/cheatcodes.sv", joined)
        self.assertIn("core/gameboy/ports/gbc_snd.vhd", joined)
        self.assertIn("core/gameboy/ports/megaswizzle.sv", joined)
        self.assertIn("core/gameboy/boot/cgb_boot.mem", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/gb.v", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/cart.v", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/cheatcodes.sv", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/gbc_snd.vhd", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/megaswizzle.sv", joined)
        self.assertIn("core/c64/multitini_c64_adapter.sv", sections["c64"])
        self.assertIn("core/c64/ports/video_sync.vhd", sections["c64"])
        self.assertIn("board/multitini_c64_video_bridge.sv",
                      sections["c64"])
        self.assertNotIn("../third_party/c64/upstream/rtl/video_sync.vhd",
                         joined)
        self.assertNotIn("core/gameboy/multitini_gameboy_adapter.sv",
                         sections["c64"])
        self.assertIn("core/gameboy/multitini_gameboy_adapter.sv",
                      sections["gbc"])
        self.assertIn("board/multitini_gameboy_video_bridge.sv",
                      sections["gbc"])
        adapter = (ROOT / "hdl" / "core" / "gameboy" /
                   "multitini_gameboy_adapter.sv").read_text(encoding="utf-8")
        self.assertIn("ready && ce_cpu && lcd_clkena", adapter)
        self.assertIn("assign video_vsync = ready && lcd_vsync;", adapter)
        self.assertNotIn("core/c64/multitini_c64_adapter.sv", sections["gbc"])
        for entry in entries:
            if entry.startswith(("core/c64/", "../third_party/c64/")):
                self.assertIn(entry, sections["c64"])
            if entry.startswith(("core/gameboy/", "../third_party/gameboy/")):
                self.assertIn(entry, sections["gbc"])
            if entry.startswith(("core/genesis/", "../third_party/genesis/")):
                self.assertIn(entry, sections["genesis"])
        for entry in ("core/genesis/multitini_genesis_adapter.sv",
                      "core/genesis/multitini_genesis_machine.sv",
                      "board/multitini_genesis_video_bridge.sv",
                      "../third_party/genesis/upstream/rtl/nuked-md/68k_ucode.txt",
                      "../third_party/genesis/upstream/rtl/nuked-md/68k_ncode.txt"):
            self.assertIn(entry, sections["genesis"])
        self.assertNotIn("../third_party/genesis/upstream/rtl/cartridge.sv", joined)
        self.assertIn("board/multitini_gameboy_psram_path.sv",
                      sections["common"])
        self.assertFalse(set(sections["common"]) & set(sections["c64"]))
        self.assertFalse(set(sections["common"]) & set(sections["gbc"]))
        self.assertNotIn("board/multitini_c64_video_bridge.sv",
                         sections["gbc"] + sections["common"])
        self.assertNotIn("board/multitini_gameboy_video_bridge.sv",
                         sections["c64"] + sections["common"])
        self.assertNotIn("../third_party/gameboy/upstream/rtl/cheatcodes.sv",
                         sections["common"] + sections["c64"])
        self.assertEqual(
            languages["../third_party/c64/upstream/rtl/mos6526.v"],
            "systemverilog",
        )

    def test_c64_fpga64_port_stretches_external_memory_wait(self):
        source = (
            ROOT / "hdl" / "core" / "c64" / "ports" /
            "fpga64_sid_iec.vhd"
        ).read_text(encoding="utf-8")
        self.assertIn("memory_wait : in  std_logic", source)
        self.assertIn("memory_wait_q <= memory_wait", source)
        self.assertRegex(
            source,
            r"if memory_wait_q = '0' then\s+"
            r"preCycle <= sysCycleDef'succ\(preCycle\)",
        )
        self.assertIn(
            "enable => enableCpu and not dma_active and not memory_wait_q",
            source,
        )
        self.assertIn(
            "enaData => enableVic and not memory_wait_q", source
        )
        self.assertIn(
            "phi2_n => enableCia_n and not memory_wait_q", source
        )
        self.assertIn("ce_1m => enableSid and not memory_wait_q", source)

    def test_c64_media_density_and_dirty_eject_wiring(self):
        drive = (
            ROOT / "hdl" / "core" / "c64" / "multitini_c64_1541.sv"
        ).read_text(encoding="utf-8")
        top = (
            ROOT / "hdl" / "board" / "multitini_one_top.sv"
        ).read_text(encoding="utf-8")
        self.assertRegex(
            drive,
            r"c1541_gcr\s+disk_gcr\s*\([\s\S]*?\.freq\(motor_frequency\)",
        )
        self.assertRegex(
            drive,
            r"c1541_direct_gcr\s+raw_disk_gcr\s*\([\s\S]*?"
            r"\.freq\(g64_track_speed\)",
        )
        self.assertIn(
            "(media_eject && !media_request_write_control)", top
        )

    def test_c64_cpu_turbo_uses_smart_mode_without_drive_warp_path(self):
        adapter = (
            ROOT / "hdl" / "core" / "c64" /
            "multitini_c64_adapter.sv"
        ).read_text(encoding="utf-8")
        drive = (
            ROOT / "hdl" / "core" / "c64" /
            "multitini_c64_1541.sv"
        ).read_text(encoding="utf-8")
        top = (
            ROOT / "hdl" / "board" / "multitini_one_top.sv"
        ).read_text(encoding="utf-8")
        interface = (
            ROOT / "spec" / "interface_v0.json"
        ).read_text(encoding="utf-8")
        fpga64 = (
            ROOT / "hdl" / "core" / "c64" / "ports" /
            "fpga64_sid_iec.vhd"
        ).read_text(encoding="utf-8")
        self.assertIn(
            ".turbo_mode({1'b0, cpu_turbo_active})", adapter
        )
        self.assertNotIn(
            ".turbo_mode({cpu_turbo_active, 1'b0})", adapter
        )
        self.assertIn("turbo_en <= turbo_mode(0);", fpga64)
        self.assertIn(
            "(turbo_mode(0) and turbo_state) = '1' or "
            "turbo_mode(1) = '1'",
            fpga64,
        )
        self.assertIn(".turbo_request(cpu_turbo_enable)", adapter)
        self.assertNotIn("warp", adapter.lower())
        self.assertNotIn("warp", drive.lower())
        self.assertNotIn("C64_WARP_ENABLE", top)
        self.assertNotIn("C64_WARP_ENABLE", interface)
        self.assertFalse(
            (ROOT / "hdl" / "core" / "c64" /
             "multitini_c64_drive_clock.sv").exists()
        )

    def test_c64_drive_parallel_cable_follows_the_fast_rom_profile(self):
        adapter = (
            ROOT / "hdl" / "core" / "c64" /
            "multitini_c64_adapter.sv"
        ).read_text(encoding="utf-8")
        drive = (
            ROOT / "hdl" / "core" / "c64" /
            "multitini_c64_1541.sv"
        ).read_text(encoding="utf-8")
        top = (
            ROOT / "hdl" / "board" / "multitini_one_top.sv"
        ).read_text(encoding="utf-8")
        fpga64 = (
            ROOT / "hdl" / "core" / "c64" / "ports" /
            "fpga64_sid_iec.vhd"
        ).read_text(encoding="utf-8")
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text(
            encoding="utf-8")
        self.assertIn("pb_oe_o <= cia2_pbe;", fpga64)
        self.assertIn(
            ".pb_i(drive_par_data_out), .pb_o(c64_pb_o), "
            ".pb_oe_o(c64_pb_oe),",
            adapter,
        )
        self.assertIn(".pc2_n_o(c64_pc2_n)", adapter)
        self.assertIn(".flag2_n_i(drive_par_stb_out)", adapter)
        self.assertIn(".pa2_i(drive_par_stb_out)", adapter)
        self.assertIn(
            "pa_in => cia2_pai and (cia2_pao or not cia2_pae),", fpga64
        )
        self.assertIn("pa_oe => cia2_pae,", fpga64)
        self.assertIn(
            ".par_data_in(c64_pb_o | ~c64_pb_oe), .par_stb_in(c64_pc2_n),",
            adapter,
        )
        self.assertIn(
            ".c64_par_stb(c64_pc2_n || !drive_parallel_enable)", adapter
        )
        self.assertIn(".ext_en(parallel_enable)", drive)
        self.assertIn(
            "assign parallel_active = parallel_enable && !drive_reset;",
            drive,
        )
        self.assertIn("MT_CONTROL_C64_DRIVE_PARALLEL_ENABLE", top)
        self.assertIn(
            ".drive_parallel_enable(c64_drive_parallel_enable)", top
        )
        # Firmware decides the cable beside the ROM streams and nowhere else,
        # so it can never disagree with the installed pair.
        stream = main.split(
            "static bool stream_c64_rom_profile(", 1
        )[1].split("\n}\n", 1)[0]
        self.assertIn(
            "set_c64_drive_parallel_cable(fast && drive_available);", stream
        )
        self.assertEqual(main.count("set_c64_drive_parallel_cable("), 2)
        self.assertEqual(
            main.count("MT_CONTROL_C64_DRIVE_PARALLEL_ENABLE"), 2
        )

    def test_every_pl_port_has_one_pin_assignment(self):
        top = (ROOT / "hdl" / "board" / "multitini_one_top.sv").read_text()
        declarations = re.findall(
            r"^\s*(?:input|output|inout)\s+(?:logic|wire)\s+"
            r"(?:\[(\d+):0\]\s+)?([A-Za-z_]\w*)\s*,?",
            top,
            flags=re.MULTILINE,
        )
        ps_ports = {
            "MIO", "PS_CLK", "PS_PORB", "PS_SRSTB", "DDR_VRP", "DDR_VRN",
            "DDR_Addr", "DDR_BankAddr", "DDR_CAS_n", "DDR_Clk", "DDR_Clk_n",
            "DDR_CKE", "DDR_CS_n", "DDR_DM", "DDR_DQ", "DDR_DQS",
            "DDR_DQS_n", "DDR_ODT", "DDR_RAS_n", "DDR_DRSTB", "DDR_WEB",
        }
        pl_widths = {name: (int(msb) + 1 if msb else 1)
                     for msb, name in declarations if name not in ps_ports}

        xdc = (ROOT / "third_party" / "appletini" / "constraints" /
               "multitini_pins.xdc").read_text(encoding="utf-8")
        active_xdc = "\n".join(line for line in xdc.splitlines()
                                 if not line.lstrip().startswith("#"))
        xdc_ports = re.findall(r"\[get_ports\s+\{?([A-Za-z_]\w*)", active_xdc)
        package_pins = re.findall(r"\bPACKAGE_PIN\s+([A-Z]+\d+)\b", active_xdc,
                                  flags=re.IGNORECASE)

        self.assertEqual(set(xdc_ports), set(pl_widths))
        self.assertEqual(len(package_pins), sum(pl_widths.values()))
        self.assertEqual(len(package_pins), len(set(package_pins)))

    def test_tcl_files_have_complete_commands(self):
        tclsh = shutil.which("tclsh")
        if tclsh is None:
            self.skipTest("tclsh is unavailable")
        checker = (
            "set h [open $::env(MT_TCL_CHECK) r]\n"
            "set d [read $h]\nclose $h\n"
            "if {![info complete $d]} {exit 1}\n"
        )
        for relative in ("scripts/create_project.tcl",
                         "scripts/build_and_export_xsa.tcl"):
            environment = os.environ.copy()
            environment["MT_TCL_CHECK"] = str(ROOT / relative)
            subprocess.run([tclsh], input=checker, text=True, check=True,
                           env=environment)

    def test_project_script_keeps_critical_board_settings(self):
        script = (ROOT / "scripts" / "create_project.tcl").read_text()
        required = (
            "xc7z020clg484-2",
            "PCW_CRYSTAL_PERIPHERAL_FREQMHZ {33.333333}",
            "PCW_SD1_SD1_IO {MIO 10 .. 15}",
            "PCW_I2C0_I2C0_IO {MIO 22 .. 23}",
            "PCW_UART0_UART0_IO {MIO 26 .. 27}",
            "PCW_UART0_BAUD_RATE {921600}",
            "PCW_UART1_UART1_IO {MIO 24 .. 25}",
            "PCW_UART1_BAUD_RATE {115200}",
            "PCW_USB0_USB0_IO {MIO 28 .. 39}",
            "PCW_USB1_USB1_IO {MIO 40 .. 51}",
            "PCW_USB0_RESET_ENABLE {0}",
            "PCW_USB1_RESET_ENABLE {0}",
            "set c64_clock_mhz 31.527954",
            "set c64_clock_mhz 32.727266",
            "set c64_clock_mhz 33.554432",
            'set machine "SUPERVISOR"',
            "set active_machine 2",
            "MULTITINI_C64_VIDEO_STANDARD must be PAL or NTSC",
            "MULTITINI_MACHINE must be SUPERVISOR, C64, GBC, VECTREX, or GENESIS",
            "MULTITINI_GENESIS_PAL must be 0 or 1",
            "GENESIS_PAL_MODE=1'b${genesis_pal_mode}",
            "CONFIG.PCW_USE_S_AXI_HP2 {1}",
            "S_AXI_HP0_0:S_AXI_HP1_0:S_AXI_HP2_0",
            "MULTITINI_ENABLE_GBC_PSRAM",
            "ACTIVE_MACHINE",
            "proc add_manifest_sources {repo_root machine}",
            "COMMON C64 GBC",
            'active_section ne "COMMON" && $active_section ne $machine',
            "add_manifest_sources $repo_root $machine",
            "USED_IN_SYNTHESIS false",
            "-vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_c64",
            "assign_bd_address -offset 0x40000000 -range 0x00010000",
        )
        for value in required:
            self.assertIn(value, script)

    def test_boot_shell_has_no_packaged_machine_core(self):
        project = (ROOT / "scripts" / "create_project.tcl").read_text()
        top = (ROOT / "hdl" / "board" / "multitini_one_top.sv").read_text()
        export = (ROOT / "scripts" / "build_and_export_xsa.tcl").read_text()
        packager = (ROOT / "scripts" / "make_firmware_bin.bat").read_text()

        self.assertIn('set machine "SUPERVISOR"', project)
        self.assertIn("ACTIVE_MACHINE=${active_machine}", project)
        self.assertIn("parameter integer ACTIVE_MACHINE = 2", top)
        supervisor = top.split(
            "end else begin : g_supervisor_shell", 1
        )[1].split("end\n    endgenerate", 1)[0]
        for marker in (
            "assign c64_core_id = 32'd0",
            "assign c64_ready = 1'b0",
            "assign c64_load_ready = 1'b0",
            "assign c64_input_ready = 1'b0",
            "assign machine_video_active = 1'b0",
            "assign c64_audio_valid = 1'b0",
            "assign psram_ce_n = 1'b1",
        ):
            self.assertIn(marker, supervisor)
        self.assertIn('set image_kind "SUPERVISOR"', export)
        self.assertIn('puts $image_kind_stream $image_kind', export)
        self.assertIn('not "%BIT_KIND%"=="SUPERVISOR"', packager)

    def test_gp0_axi_clock_survives_full_pl_reconfiguration(self):
        script = (ROOT / "scripts" / "create_project.tcl").read_text()
        fclk_net = script.split(
            "connect_bd_net [get_bd_pins $ps/FCLK_CLK0]", 1
        )[1].split("\n\n", 1)[0]
        self.assertIn("$control_clk", fclk_net)
        self.assertIn("$control_reset/slowest_sync_clk", fclk_net)
        self.assertIn("$ps/M_AXI_GP0_ACLK", fclk_net)
        self.assertNotIn("$idelay_wiz/clk_out1", fclk_net)

        self.assertIn(
            "[get_bd_pins $constant_one/dout] \\\n    [get_bd_pins $control_reset/dcm_locked]",
            script,
        )
        self.assertIn(
            "[get_bd_pins $idelay_wiz/locked] \\\n    [get_bd_pins $idelay_reset/dcm_locked]",
            script,
        )
        self.assertIn(
            "[get_bd_pins $idelay_reset/peripheral_aresetn] $idelay_aresetn",
            script,
        )

        top = (ROOT / "hdl/board/multitini_one_top.sv").read_text()
        psram = (ROOT / "hdl/board/multitini_gameboy_psram_path.sv").read_text()
        self.assertIn(".idelay_aresetn(idelay_aresetn)", top)
        self.assertIn(".idelay_resetn(idelay_aresetn)", top)
        self.assertIn(".RST(!idelay_resetn)", psram)

        timing = (ROOT / "hdl" / "board" /
                  "multitini_timing.xdc").read_text()
        self.assertIn(
            "set control_root_clock [get_clocks -quiet clk_fpga_0]",
            timing,
        )

    def test_gameboy_cartridge_load_selects_core_video(self):
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        load_action = main.split('xil_printf("[GBC] load', 1)[1].split(
            "} else {", 1
        )[0]
        detach_action = main.split(
            "gameboy_content_service_detach();", 1
        )[1].split("} else {", 1)[0]
        self.assertIn("set_video_test(false);", load_action)
        self.assertIn("set_video_test(true);", detach_action)

    def test_c64_input_path_is_not_tied_off(self):
        top = (ROOT / "hdl/board/multitini_one_top.sv").read_text()
        self.assertIn("c64_input_mailbox", top)
        self.assertIn(".input_valid(c64_input_valid)", top)
        self.assertNotIn(".input_valid(1'b0)", top)

    def test_psram_driver_port_diff_is_limited_to_registered_tristate_polarity(self):
        upstream = (
            ROOT / "third_party/appletini/hdl/psram_driver.sv"
        ).read_text()
        expected = upstream.replace(
            "module psram_driver (", "module multitini_psram_driver (", 1
        )
        expected = expected.replace(
            "output logic [3:0] psram_oe,", "output logic [3:0] psram_t,", 1
        )
        expected = expected.replace(
            "psram_oe / psram_a_o / psram_b_o",
            "psram_t / psram_a_o / psram_b_o",
            1,
        )
        expected = expected.replace(
            "psram_oe <= {4{sh_oe_tape[31]}};",
            "psram_t <= {4{!sh_oe_tape[31]}};",
            1,
        )
        expected = expected.replace(
            "psram_oe <= {3'b0, sh_oe_tape[31]};",
            "psram_t <= {3'b111, !sh_oe_tape[31]};",
            1,
        )
        local = (ROOT / "hdl/board/multitini_psram_driver.sv").read_text()
        self.assertEqual(
            local,
            "// SPDX-License-Identifier: GPL-3.0-only\n" + expected,
        )

    def test_timing_xdc_uses_only_supported_constraint_commands(self):
        xdc = (ROOT / "hdl/board/multitini_timing.xdc").read_text()
        psram_xdc = (
            ROOT / "hdl/board/multitini_psram_timing.xdc"
        ).read_text()
        psram_iob_xdc = (
            ROOT / "hdl/board/multitini_psram_iob.xdc"
        ).read_text()
        self.assertNotRegex(xdc, r"(?m)^\s*if\s*\{")
        self.assertIn("-include_generated_clocks audio_mclk", xdc)
        self.assertIn("set_false_path -to [get_ports audio_spdif_out]", xdc)
        self.assertNotRegex(psram_xdc, r"(?m)^\s*if\s*\{")
        self.assertIn("create_generated_clock -name psram_clk_out", psram_xdc)
        self.assertIn("set_false_path -from $psram_data_ports", psram_xdc)
        self.assertEqual(psram_iob_xdc.count("set_property IOB TRUE"), 4)

    def test_vivado_script_gates_critical_cdc_findings(self):
        script = (ROOT / "scripts/build_and_export_xsa.tcl").read_text()
        self.assertIn("require_packed_iob_registers", script)
        self.assertIn('{.*psram_t_reg\\[[0-3]\\](_rep)?$} 8', script)
        self.assertIn("set cdc_report", script)
        self.assertIn("cdc_unexpected_critical", script)
        self.assertIn("Expected $expected_psram_waivers external PSRAM CDC waivers", script)
        self.assertIn("CDC gate found unexpected critical crossings", script)
        self.assertIn("Fabric LUTs drive clock pins", script)

    def test_gameboy_ram_port_preserves_registered_address_reads(self):
        source = (ROOT / "hdl/core/gameboy/ports/dpram.sv").read_text()
        machine = (ROOT / "hdl/core/gameboy/ports/gb.v").read_text()
        self.assertEqual(source.count("xpm_memory_tdpram"), 2)
        self.assertEqual(source.count('.MEMORY_PRIMITIVE("block")'), 2)
        self.assertEqual(source.count(".READ_LATENCY_A(1)"), 2)
        self.assertEqual(source.count(".READ_LATENCY_B(1)"), 2)
        self.assertIn("module dpram_ce", source)
        self.assertIn(".ena(clken_a)", source)
        self.assertEqual(machine.count("dpram_ce #("), 4)
        self.assertNotIn("clk_sys & ce", machine)

    def test_mailbox_acknowledgement_is_destination_registered(self):
        mailbox = (ROOT / "hdl/common/multitini_async_mailbox.sv").read_text()
        self.assertIn("always_ff @(posedge dst_clk)", mailbox)
        self.assertIn("destination_request && !destination_ack", mailbox)
        self.assertNotIn(
            "assign destination_ack = dst_valid && dst_ready", mailbox
        )

    def test_vitis_script_checks_every_build_status(self):
        script = (ROOT / "scripts/create_vitis_workspace.py").read_text()
        self.assertIn("include_directory.as_posix()", script)
        self.assertIn(
            'require_success(platform.build(), "platform build")', script
        )
        self.assertIn(
            'require_success(application.build(), '
            '"diagnostic application build")',
            script,
        )

    def test_vitis_platform_enables_safe_fatfs(self):
        script = (ROOT / "scripts/create_vitis_workspace.py").read_text()
        self.assertIn('domain.set_lib(lib_name="xilffs"', script)
        self.assertIn("domain.regenerate()", script)
        self.assertIn("enable_fatfs_lfn(domain_bsp)", script)
        self.assertIn("patch_fatfs_no_card_hang(domain_bsp)", script)
        self.assertIn("Multitini no-card fail-fast", script)
        self.assertIn("StableCount == 100U", script)
        self.assertIn("validate_reused_xilffs(xpfm)", script)

    def test_c64_sd_paths_stay_under_canonical_core_root(self):
        header = (ROOT / "firmware/include/multitini_c64_rom_files.h").read_text()
        self.assertIn('#define MT_SD_CORE_ROOT "0:/multitini/cores"', header)
        self.assertIn('MT_C64_SD_ROM_ROOT "/basic.bin"', header)
        self.assertIn('MT_C64_SD_ROM_ROOT "/kernal.bin"', header)
        self.assertIn('MT_C64_SD_ROM_ROOT "/characters.bin"', header)
        self.assertIn('MT_C64_SD_ROM_ROOT "/1541.bin"', header)
        self.assertNotIn("Multitini/cores", header)

    def test_c64_sd_workflow_documents_source_contract(self):
        readme = (ROOT / "README.md").read_text()
        workflow = (ROOT / "docs/C64_SD_STORAGE.md").read_text()
        d64 = (ROOT / "firmware/common/c64_d64.c").read_text()
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text()

        self.assertIn("docs/C64_SD_STORAGE.md", readme)
        for path in (
            "0:/multitini/cores/c64/roms/basic.bin",
            "0:/multitini/cores/c64/roms/kernal.bin",
            "0:/multitini/cores/c64/roms/characters.bin",
            "0:/multitini/cores/c64/roms/1541.bin",
            "0:/multitini/cores/c64/media/drive8.d64",
        ):
            self.assertIn(path, workflow)
        for command in (":dir", ":mount FILE.d64", ":disk", ":eject"):
            self.assertIn(command, workflow)
            self.assertIn(command, main)
        for size in (174848, 175531, 196608, 197376, 205312, 206114):
            self.assertIn(f"{size}U", d64)
            self.assertIn(f"{size:,} bytes", workflow)
        self.assertIn("read-only", workflow)
        self.assertIn(":c64rx 20480 0xCRC32", workflow)
        # Acceptance changes as board tests pass; keep the evidence discoverable
        # without requiring wording from a superseded bring-up milestone.
        self.assertIn("HARDWARE_RESULTS.md", workflow)

    def test_c64_settings_restore_validates_live_fpga_mount(self):
        header = (
            ROOT / "firmware/platform/xilinx/c64_d64_service.h"
        ).read_text()
        service = (
            ROOT / "firmware/platform/xilinx/c64_d64_service.c"
        ).read_text()
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        restore = main.split(
            "static bool restore_selection_settings(", 1
        )[1].split("static mt_menu_view_t supervisor_menu_view", 1)[0]
        restart = main.split(
            "} else if (action.kind == MT_MENU_ACTION_RESET_CORE)", 1
        )[1].split(
            "} else if (action.kind == MT_MENU_ACTION_TOGGLE_AUTO_BOOT)", 1
        )[0]

        self.assertIn("c64_d64_service_ensure_mounted", header)
        self.assertIn("read_stable_media_state(&media_state)", service)
        self.assertIn("MT_MEDIA_STATE_EJECT_PENDING", service)
        self.assertIn("c64_d64_service_ensure_mounted", restore)
        self.assertNotIn("already_mounted", restore)
        self.assertIn("restore_selection_settings(", restart)
        self.assertIn("&supervisor.active", restart)

    def test_c64_fast_rom_switch_has_complete_rollback_paths(self):
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        boot = main.split(
            "static bool attempt_c64_sd_boot(", 1
        )[1].split("static c64_rom_stage_result_t stage_c64_rom_profile", 1)[0]
        stage = main.split(
            "static c64_rom_stage_result_t stage_c64_rom_profile", 1
        )[1].split("static void handle_c64_rx_command", 1)[0]
        toggle = main.split(
            "} else if (action.kind == MT_MENU_ACTION_TOGGLE_CORE_OPTION)", 1
        )[1].split(
            "} else if (action.kind == MT_MENU_ACTION_RESET_CORE)", 1
        )[0]

        # After a candidate drive may have streamed, stock fallback must be a
        # complete pair rather than leaving the candidate drive ROM installed.
        self.assertIn(
            "read_c64_rom_profile(false, true, &drive_available)", boot
        )
        # A system-only stock boot is valid, but not as a transactional
        # rollback source for a live pair switch.
        self.assertIn("!c64_active_drive_rom_available", stage)
        # Media restore and rollback failures must prevent settings commit and
        # reach distinct user-visible outcomes.
        self.assertIn("C64_ROM_STAGE_ROLLBACK_FAILED", stage)
        self.assertIn("C64_ROM_STAGE_ROLLBACK_MEDIA_PENDING", stage)
        self.assertNotIn("(void)restore_active_c64_rom_profile", stage)
        self.assertIn("stage_result != C64_ROM_STAGE_OK", toggle)
        self.assertIn("ROM rollback failed; restart the core", toggle)
        self.assertIn("ROM restored; drive remount pending", toggle)

    def test_c64_mount_and_run_stages_remounts_then_runs(self):
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        flow = main.split(
            "} else if (action.kind == MT_MENU_ACTION_MOUNT_AND_RUN", 1
        )[1].split(
            "} else if (action.kind == MT_MENU_ACTION_LOAD_CONTENT)", 1
        )[0]
        stage = flow.index("c64_content_service_stage_prg_image")
        remount = flow.index("finish_c64_content_change")
        run = flow.index("c64_content_service_run_staged_prg")

        self.assertLess(stage, remount)
        self.assertLess(remount, run)
        self.assertIn("if (!remounted)", flow)
        self.assertIn("mt_menu_close_browser(&supervisor_menu)", flow)
        self.assertRegex(main, r"#define\s+MT_MENU_MAX_FILES\s+144U")

    def test_menu_run_actions_wait_for_hidden_overlay(self):
        main = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        policy = main.split(
            "static bool menu_action_needs_core_progress(", 1
        )[1].split("static int native_load_key", 1)[0]
        for action in (
            "SWITCH_CORE", "MOUNT_DRIVE8", "MOUNT_AND_RUN",
            "LOAD_DISK_PROGRAM", "DIRECT_DISK_PROGRAM", "EJECT_DRIVE8",
            "LOAD_CONTENT", "DETACH_CARTRIDGE", "RESET_CORE", "REBOOT_GOLDEN",
        ):
            self.assertIn(f"case MT_MENU_ACTION_{action}:", policy)
        self.assertNotIn("case MT_MENU_ACTION_BROWSE_", policy)
        self.assertNotIn("case MT_MENU_ACTION_TOGGLE_AUTO_BOOT:", policy)
        self.assertIn("action.file_index == 0U", policy)  # ROM-pair restart

        hide = main.split(
            "static bool hide_menu_for_core_action(void)\n{", 1
        )[1].split("static bool menu_action_needs_core_progress", 1)[0]
        # The temporary hardware hide must not close the user's menu. The
        # pending redraw restores the requested visibility after the action.
        self.assertNotRegex(hide, r"supervisor_menu\.visible\s*=")
        self.assertIn("supervisor_menu_dirty = true", hide)
        self.assertNotIn("supervisor_menu_dirty = false", hide)
        # Queuing a commit is insufficient: wait for its acknowledgement and
        # the actual video visibility before a paused CPU must make progress.
        self.assertRegex(
            hide, r"result == MT_UI_PRESENT_OK && !supervisor_presenter.pending &&\s+"
                  r"\(shell_read\(MT_REG_UI_STATUS\) & MT_UI_STATUS_VISIBLE\) == 0U"
        )
        self.assertIn("MT_MENU_HIDE_TIMEOUT_MS", hide)
        self.assertIn("service_runtime();", hide)
        self.assertNotIn("service_supervisor_menu();", hide)
        self.assertIn("action cancelled", hide)

        dispatch = main.split("static void service_supervisor_menu(void)", 1)[1]
        dispatch = dispatch.split("static void initialize_supervisor_menu", 1)[0]
        self.assertLess(dispatch.index("hide_menu_for_core_action()"),
                        dispatch.index("handle_menu_action(action)"))
        self.assertNotRegex(dispatch, r"supervisor_menu\.visible\s*=")
        self.assertNotIn("supervisor_status_color ==", dispatch)
        runtime = main.split("static void service_runtime(void)", 1)[1]
        runtime = runtime.split("static void print_status", 1)[0]
        self.assertIn("gameboy_content_service_poll();", runtime)
        self.assertNotIn("supervisor_menu.visible", runtime)

        prepare = main.split("static bool prepare_c64_content_change(void)", 1)[1]
        prepare = prepare.split("static bool finish_c64_content_change", 1)[0]
        self.assertLess(prepare.index("hide_menu_for_core_action()"),
                        prepare.index("c64_d64_service_prepare_reset()"))

        uart_loop = main.split("int main(void)", 1)[1]
        self.assertLess(uart_loop.index("supervisor_menu_action_active = true"),
                        uart_loop.index("switch (input)"))
        self.assertLess(uart_loop.index("case ':': handle_extended_command(false)"),
                        uart_loop.index("supervisor_menu_action_active = false"))
        self.assertIn("supervisor_menu.visible = !supervisor_menu.visible", uart_loop)

    def test_vitis_build_includes_wired_xbox_360_host_support(self):
        script = (ROOT / "scripts/create_vitis_workspace.py").read_text()
        config = (ROOT / "firmware/platform/xilinx/usb_config.h").read_text()
        xbox = (ROOT /
                "third_party/CherryUSB/class/vendor/xbox/usbh_xbox.c").read_text()

        endpoint_capacity = re.search(
            r"#define\s+CONFIG_USBHOST_MAX_ENDPOINTS\s+(\d+)", config
        )
        self.assertIsNotNone(endpoint_capacity)
        self.assertGreater(int(endpoint_capacity.group(1)), 4)
        self.assertRegex(
            config, r"#define\s+CONFIG_USBHOST_MAX_XBOX_CLASS\s+1\b"
        )
        self.assertIn(
            '"third_party/CherryUSB/class/vendor/xbox/usbh_xbox.c"',
            script,
        )
        self.assertIn('"class" / "vendor" / "xbox"', script)
        self.assertIn("{ 0x045e, 0x028e }", xbox)

    def test_vitis_platform_hook_is_owned_source(self):
        platform_directory = ROOT / "firmware" / "platform" / "xilinx"
        header = (platform_directory / "platform.h").read_text()
        implementation = (platform_directory / "platform.c").read_text()
        self.assertIn("void init_platform(void);", header)
        self.assertIn("void init_platform(void)", implementation)
        self.assertIn("Zynq FSBL", implementation)
        self.assertIn("STDIN_BASEADDRESS != XPAR_XUARTPS_0_BASEADDR",
                      implementation)
        self.assertIn("STDOUT_BASEADDRESS != XPAR_XUARTPS_0_BASEADDR",
                      implementation)


if __name__ == "__main__":
    unittest.main()
