# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed release gates for explicit CPU1 video inputs (no native tools)."""
from __future__ import annotations

import binascii
import importlib.util
import json
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_cpu1_video_test as package


class PackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.reports = self.root / "reports"
        self.reports.mkdir()
        for name in package.REQUIRED_REPORTS:
            (self.reports / name).write_text("fixture\n")
        self.summary()
        (self.reports / "bus_skew.rpt").write_text("Id: 1\nSlack (MET) : 0.1ns\n")
        (self.reports / "cdc.rpt").write_text("CDC Report\nCDC-1 Info 1\n")
        (self.reports / "timing_coverage_gate.rpt").write_text("\n".join("checking " + category for category in (
            "no_clock", "unconstrained_internal_endpoints", "no_input_delay", "no_output_delay",
            "partial_input_delay", "partial_output_delay")))
        (self.reports / "route_status.rpt").write_text(
            "# of routable nets....... : 123 :\n# of fully routed nets....... : 123 :\n"
            "# of nets with routing errors....... : 0 :\n")

    def summary(self, wns="0.01", whs="0.00", wpws="0.2", failures="0", endpoints="20"):
        (self.reports / "timing_summary.rpt").write_text(
            "WNS(ns) TNS(ns) TNS Failing Endpoints TNS Total Endpoints\n-----\n"
            f"{wns} 0 {failures} {endpoints} {whs} 0 0 20 {wpws} 0 0 20\n")

    def test_firmware_delivery_has_named_identical_root_and_sd_copies(self):
        original = self.root / "original.bin"
        original.write_bytes(b"selected tagged Multitini firmware")
        out = self.root / "delivery"
        firmware = package.stage_firmware(out, original)
        self.assertEqual(firmware, out / "FIRMWARE-MULTITINI.BIN")
        self.assertEqual({path.relative_to(out).as_posix() for path in out.rglob("*") if path.is_file()},
                         {"FIRMWARE-MULTITINI.BIN", "sd/FIRMWARE-MULTITINI.BIN"})
        self.assertEqual(firmware.read_bytes(), original.read_bytes())
        self.assertEqual((out / "sd/FIRMWARE-MULTITINI.BIN").read_bytes(), original.read_bytes())
        # Normal packaging generates the named root image before staging it.
        package.stage_firmware(out, firmware)
        self.assertEqual((out / "sd/FIRMWARE-MULTITINI.BIN").read_bytes(), original.read_bytes())

    def test_strict_positive_setup_hold_pulse_and_endpoint_gates(self):
        self.assertEqual(package.timing(self.root, "SUPERVISOR", False)["timing_ns"]["WNS"], .01)
        for values in ({"wns": "0"}, {"wns": "-0.01"}, {"wns": "nan"},
                       {"whs": "-0.01"}, {"wpws": "-0.1"}, {"failures": "1"}, {"endpoints": "0"}):
            self.summary(**values)
            with self.assertRaisesRegex(ValueError, "endpoint gate failed"):
                package.timing(self.root, "SUPERVISOR", False)

    def test_cdc_allows_only_eight_unique_external_psram_paths(self):
        rows = [f"{bank * 4 + bit + 1} CDC-13 Critical False Path psram_{'ab'[bank]}_io[{bit}] top/u_{'ab'[bank]}_iddr/D"
                for bank in range(2) for bit in range(4)]
        path = self.reports / "cdc.rpt"
        path.write_text("CDC Report\nCDC-13 Critical 8\n" + "\n".join(rows))
        self.assertEqual(package.timing(self.root, "C64", True)["external_psram_waivers"], 8)
        with self.assertRaisesRegex(ValueError, "critical CDC"):
            package.timing(self.root, "SUPERVISOR", False)
        path.write_text("CDC Report\nCDC-13 Critical 8\n" + "\n".join([rows[0]] * 8))
        with self.assertRaisesRegex(ValueError, "exactly the eight"):
            package.timing(self.root, "C64", True)
        path.write_text("CDC Report\nCDC-13 Critical 8\n" + "\n".join(rows[:-1]))
        with self.assertRaisesRegex(ValueError, "critical CDC"):
            package.timing(self.root, "C64", True)

    def test_missing_reports_route_coverage_and_skew_fail_closed(self):
        cases = (("route_status.rpt", "# of routable nets.. : 123 :\n# of fully routed nets.. : 122 :\n# of nets with routing errors.. : 0 :", "routing"),
                 ("bus_skew.rpt", "Id: 1\nSlack (VIOLATED) : -0.1ns", "Bus-skew"),
                 ("timing_coverage_gate.rpt", "checking no_clock", "coverage"),
                 ("drc.rpt", "UCIO-1#1 Error\n", "DRC"))
        for name, text, error in cases:
            path = self.reports / name
            saved = path.read_text()
            path.write_text(text)
            with self.assertRaisesRegex(ValueError, error):
                package.timing(self.root, "SUPERVISOR", False)
            path.write_text(saved)
        (self.reports / "clocks.rpt").unlink()
        with self.assertRaisesRegex(ValueError, "Missing timing"):
            package.timing(self.root, "SUPERVISOR", False)

    def test_source_paths_and_dirty_bytes_are_verified(self):
        (self.root / "module.c").write_text("before")
        with patch.object(package, "ROOT", self.root):
            record = {"module.c": package.sha(self.root / "module.c")}
            package.verify_sources(record)
            (self.root / "module.c").write_text("after")
            with self.assertRaisesRegex(ValueError, "source changed"):
                package.verify_sources(record)
            for name in ("../escape.c", "/absolute.c", "a\\b.c"):
                with self.assertRaisesRegex(ValueError, "Unsafe source"):
                    package.source_path(name)

    def test_gbc_package_rejects_psram_disabled_image(self):
        (self.root / "source-inputs.json").write_text(json.dumps({
            "machine": "GBC", "configuration": {"gbc_psram": False}, "files": {}}))
        with patch.object(package, "verify_sources") as sources:
            with self.assertRaisesRegex(ValueError, "Wrong GBC PSRAM selection"):
                package.verify_hardware(self.root, "GBC")
            sources.assert_not_called()

    def test_both_64bit_hp_ports_required_for_vid2_platform(self):
        path = self.root / "platform.xsa"
        cases = (("1", "64", "1", "64", True),
                 ("0", "64", "1", "64", False),
                 ("1", "32", "1", "64", False),
                 ("1", "64", "0", "64", False),
                 ("1", "64", "1", "32", False),
                 ("1", "64", None, None, False))
        for hp0, width0, hp1, width1, accepted in cases:
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("ps7_init.c", "generated platform init")
                description = f'<PARAMETER NAME="PCW_USE_S_AXI_HP0" VALUE="{hp0}"/>\n' + \
                              f'<PARAMETER NAME="C_S_AXI_HP0_DATA_WIDTH" VALUE="{width0}"/>'
                if hp1 is not None:
                    description += f'<PARAMETER NAME="PCW_USE_S_AXI_HP1" VALUE="{hp1}"/>\n' + \
                                   f'<PARAMETER NAME="C_S_AXI_HP1_DATA_WIDTH" VALUE="{width1}"/>'
                archive.writestr("design.hwh", description)
            if accepted:
                self.assertEqual(package.video_xsa(path), package.digest(b"generated platform init"))
            else:
                with self.assertRaisesRegex(ValueError, "64-bit video HP[01]"):
                    package.video_xsa(path)

    def test_core_payload_crc_hash_abi_and_membership_verified(self):
        payload = b"core payload"
        path = self.root / "core.bin"
        path.write_bytes(payload)
        original = (f"abi=0.15\n# native.sha256={package.digest(payload)}\n"
                    f"variant=native|Display|core.bin|{len(payload)}|0x{binascii.crc32(payload):08X}\n")
        config = self.root / "core.cfg"
        config.write_text(original)
        self.assertEqual(package.verify_core(self.root, {"native"})["native"]["bytes"], len(payload))
        for text, error in ((original.replace("0.15", "0.14"), "ABI"),
                            (original.replace("native|", "extra|"), "variants"),
                            (original.replace("core.bin|", "../core.bin|"), "Unsafe"),
                            (original.replace(package.digest(payload), "0" * 64), "SHA-256")):
            config.write_text(text)
            with self.assertRaisesRegex(ValueError, error):
                package.verify_core(self.root, {"native"})
        config.write_text(original)
        path.write_bytes(b"corruption!!")
        with self.assertRaisesRegex(ValueError, "size/CRC"):
            package.verify_core(self.root, {"native"})

    def test_cpu1_production_excludes_frame_processor_and_test_dispatchers(self):
        files = {"firmware/cpu1/main.c", "firmware/common/video_effects.c"}
        commands = [{"command": "arm-none-eabi-gcc -O3 -mfpu=neon source.c"}]
        package.verify_cpu1_control_program(files, commands)
        with self.assertRaisesRegex(ValueError, "source list"):
            package.verify_cpu1_control_program(files | {"firmware/common/video_frame.c"}, commands)
        with self.assertRaisesRegex(ValueError, "source list"):
            package.verify_cpu1_control_program(files - {"firmware/cpu1/main.c"}, commands)
        for macro in ("MT_VIDEO_WORKER_FRAME_REFERENCE", "MT_VIDEO_WORKER_HOST_TEST",
                      "MT_VIDEO_WORKER_NO_DISPATCH"):
            for key, value in (("command", "gcc -D" + macro), ("arguments", ["gcc", "-D", macro])):
                with self.assertRaisesRegex(ValueError, "reference/test dispatcher"):
                    package.verify_cpu1_control_program(files, [{key: value}])

    def test_archive_exact_bytes_and_no_overwrite(self):
        source = self.root / "source.c"
        source.write_text("source")
        archive = self.root / "source.zip"
        package.archive_files(archive, {"module/source.c": source})
        with zipfile.ZipFile(archive) as result:
            self.assertEqual(result.read("module/source.c"), b"source")
        with self.assertRaises(FileExistsError):
            package.archive_files(archive, {"module/source.c": source})

    def test_fpga_precapture_archive_and_configuration_verified(self):
        source = b"captured HDL"
        record = {"hdl/source.sv": package.digest(source)}
        wrapper = self.root / "build.tcl"
        wrapper.write_bytes(b"captured build configuration")
        archive = self.root / "source-inputs.zip"
        with zipfile.ZipFile(archive, "w") as result:
            result.writestr("hdl/source.sv", source)
            result.writestr("build-config/build.tcl", wrapper.read_bytes())
        package.verify_hardware_source_archive(self.root, record)
        wrapper.write_bytes(b"different configuration")
        with self.assertRaisesRegex(ValueError, "configuration changed"):
            package.verify_hardware_source_archive(self.root, record)
        with zipfile.ZipFile(archive, "w") as result:
            result.writestr("hdl/source.sv", b"wrong HDL")
        with self.assertRaisesRegex(ValueError, "archive mismatch"):
            package.verify_hardware_source_archive(self.root, record)


if __name__ == "__main__":
    unittest.main()
