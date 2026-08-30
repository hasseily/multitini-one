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
        sections = {"common": [], "c64": [], "gbc": []}
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
        self.assertTrue(entries[-2].endswith("multitini_pins.xdc"))
        self.assertEqual(entries[-1], "board/multitini_timing.xdc")
        for entry in entries:
            self.assertTrue((ROOT / "hdl" / entry).is_file(), entry)
        joined = "\n".join(entries)
        self.assertIn("third_party/c64/upstream/rtl/fpga64_sid_iec.vhd", joined)
        self.assertIn("core/c64/multitini_c64_adapter.sv", joined)
        self.assertNotIn("third_party/c64/upstream/c64.sv", joined)
        self.assertNotIn("third_party/c64/upstream/rtl/fpga64_buslogic.vhd",
                         joined)
        self.assertNotIn("third_party/c64/upstream/rtl/iec_drive/", joined)
        self.assertNotIn(".mif", joined)
        self.assertIn("core/gameboy/multitini_gameboy_adapter.sv", joined)
        self.assertIn("core/gameboy/ports/gb.v", joined)
        self.assertIn("core/gameboy/ports/cart.v", joined)
        self.assertIn("third_party/gameboy/upstream/rtl/megaswizzle.sv", joined)
        self.assertIn("core/gameboy/boot/cgb_boot.hex", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/gb.v", joined)
        self.assertNotIn("third_party/gameboy/upstream/rtl/cart.v", joined)
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
        self.assertNotIn("core/c64/multitini_c64_adapter.sv", sections["gbc"])
        for entry in entries:
            if entry.startswith(("core/c64/", "../third_party/c64/")):
                self.assertIn(entry, sections["c64"])
            if entry.startswith(("core/gameboy/", "../third_party/gameboy/")):
                self.assertIn(entry, sections["gbc"])
        self.assertIn("board/multitini_gameboy_psram_path.sv",
                      sections["gbc"])
        self.assertFalse(set(sections["common"]) & set(sections["c64"]))
        self.assertFalse(set(sections["common"]) & set(sections["gbc"]))
        self.assertNotIn("board/multitini_c64_video_bridge.sv",
                         sections["gbc"] + sections["common"])
        self.assertNotIn("../third_party/gameboy/upstream/rtl/cheatcodes.sv",
                         sections["common"] + sections["c64"])
        self.assertEqual(
            languages["../third_party/c64/upstream/rtl/mos6526.v"],
            "systemverilog",
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
            "PCW_UART1_UART1_IO {MIO 24 .. 25}",
            "PCW_USB0_USB0_IO {MIO 28 .. 39}",
            "PCW_USB1_USB1_IO {MIO 40 .. 51}",
            "PCW_USB0_RESET_ENABLE {0}",
            "PCW_USB1_RESET_ENABLE {0}",
            "set c64_clock_mhz 31.527954",
            "set c64_clock_mhz 32.727266",
            "set c64_clock_mhz 33.554432",
            "MULTITINI_C64_VIDEO_STANDARD must be PAL or NTSC",
            "MULTITINI_MACHINE must be C64 or GBC",
            "MULTITINI_ENABLE_GBC_PSRAM",
            "ACTIVE_MACHINE",
            "proc add_manifest_sources {repo_root machine}",
            "COMMON C64 GBC",
            'active_section ne "COMMON" && $active_section ne $machine',
            "add_manifest_sources $repo_root $machine",
            "USED_IN_SYNTHESIS false",
            "-vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_c64",
            "assign_bd_address -offset 0x40000000 -range 0x00001000",
        )
        for value in required:
            self.assertIn(value, script)

    def test_c64_input_path_is_not_tied_off(self):
        top = (ROOT / "hdl/board/multitini_one_top.sv").read_text()
        self.assertIn("c64_input_mailbox", top)
        self.assertIn(".input_valid(c64_input_valid)", top)
        self.assertNotIn(".input_valid(1'b0)", top)

    def test_timing_xdc_uses_only_supported_constraint_commands(self):
        xdc = (ROOT / "hdl/board/multitini_timing.xdc").read_text()
        self.assertNotRegex(xdc, r"(?m)^\s*if\s*\{")
        self.assertIn("-include_generated_clocks audio_mclk", xdc)
        self.assertIn("set_false_path -to [get_ports audio_spdif_out]", xdc)

    def test_vivado_script_gates_critical_cdc_findings(self):
        script = (ROOT / "scripts/build_and_export_xsa.tcl").read_text()
        self.assertIn("set cdc_report", script)
        self.assertIn("CDC gate found one or more critical crossings", script)

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
