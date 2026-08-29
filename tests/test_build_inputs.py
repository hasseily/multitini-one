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
        manifest = ROOT / "hdl" / "hdl_sources.txt"
        for line in manifest.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                entries.append(line)
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


if __name__ == "__main__":
    unittest.main()
