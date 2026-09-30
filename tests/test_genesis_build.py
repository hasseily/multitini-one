# SPDX-License-Identifier: GPL-3.0-only
"""Genesis interface, source-policy and real Tcl configuration gates."""

from __future__ import annotations

import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / f"{name}.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


INTERFACE = module("generate_genesis_interface")
AUDIT = module("audit_genesis_import")
CAPTURE = module("capture_vectrex_build_inputs")


class GenesisInterfaceTests(unittest.TestCase):
    def test_generated_contract_matches_disk(self):
        spec = INTERFACE.load_spec()
        self.assertEqual(INTERFACE.C_PATH.read_text(), INTERFACE.generate_c(spec))
        self.assertEqual(INTERFACE.SV_PATH.read_text(), INTERFACE.generate_sv(spec))
        self.assertEqual(INTERFACE.number(spec["config"]["MAGIC"]).to_bytes(4, "little"), b"MD01")
        self.assertEqual(spec["sizes"]["GENESIS_CONFIG"], 16)

    def test_rejects_ambiguous_or_unrepresentable_contracts(self):
        original = INTERFACE.load_spec()
        edits = [
            ("buttons", "Z", 10),
            ("load_kinds", "GENESIS_CONFIG", "0x30"),
            ("input_namespaces", "GENESIS_GAMEPAD", 256),
            ("sizes", "GENESIS_CART_MAX", 16 * 1024 * 1024),
            ("sizes", "GENESIS_SAVE", 32768),
            ("sizes", "GENESIS_CONFIG", 15),
            ("regions", "EU", 1),
            ("clocks_hz", "CORE_NTSC", True),
        ]
        for group, name, value in edits:
            with self.subTest(group=group, name=name):
                spec = copy.deepcopy(original)
                spec[group][name] = value
                with self.assertRaises(ValueError):
                    INTERFACE.validate_spec(spec)

    def test_namespace_does_not_collide_with_existing_cores(self):
        genesis = INTERFACE.load_spec()
        for name in ("c64", "gameboy"):
            other = json.loads((ROOT / f"spec/{name}_interface_v0.json").read_text())
            for group in ("load_kinds", "input_namespaces"):
                self.assertFalse({INTERFACE.number(value) for value in genesis[group].values()} &
                                 {INTERFACE.number(value) for value in other[group].values()})


class GenesisImportTests(unittest.TestCase):
    def manifest(self):
        return json.loads(AUDIT.MANIFEST_PATH.read_text())

    def test_exact_import_and_license_census(self):
        self.assertGreater(AUDIT.audit(self.manifest()), 20)

    def test_public_bundle_keeps_enclosed_license_and_port_origins(self):
        paths = {path.relative_to(ROOT).as_posix() for path in CAPTURE.inputs("GENESIS")}
        for relative in ("third_party/genesis/upstream/rtl/nuked-md/LICENSE",
                         "third_party/genesis/upstream/rtl/nuked-md/README.md",
                         "third_party/genesis/import_manifest.json",
                         "third_party/genesis/upstream/rtl/pad_io.sv",
                         "third_party/genesis/upstream/sys/iir_filter.v",
                         "scripts/port_genesis_core.py"):
            self.assertIn(relative, paths)
        self.assertNotIn("third_party/genesis/upstream/rtl/nuked-md/LICENSE",
                         {path.relative_to(ROOT).as_posix() for path in CAPTURE.inputs("SUPERVISOR")})

    def test_rejects_excluded_unknown_duplicate_or_unsafe_sources(self):
        for path in ("rtl/VM2413/opll.vhd", "../outside.v", "/outside.v", "a\\outside.v"):
            with self.subTest(path=path):
                manifest = self.manifest()
                manifest["resolved_groups"][0]["paths"].append(path)
                with self.assertRaises(ValueError):
                    AUDIT.validate_manifest(manifest)
        manifest = self.manifest()
        manifest["resolved_groups"][0]["paths"].append(manifest["resolved_groups"][0]["paths"][0])
        with self.assertRaises(ValueError):
            AUDIT.validate_manifest(manifest)
        manifest = self.manifest()
        manifest["unresolved"] = [{"path": "unknown.v", "reason": "no grant"}]
        with self.assertRaises(ValueError):
            AUDIT.validate_manifest(manifest)


@unittest.skipUnless(shutil.which("tclsh"), "native Tcl unavailable")
class GenesisProjectTests(unittest.TestCase):
    def configure(self, machine="GENESIS", pal=None):
        source = (ROOT / "scripts/create_project.tcl").read_text().split("proc require_file", 1)[0]
        # Execute the production machine/environment selection without requiring
        # Vivado's project creation commands or mocking the selection itself.
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("MULTITINI_")}
        environment["MULTITINI_MACHINE"] = machine
        if pal is not None:
            environment["MULTITINI_GENESIS_PAL"] = str(pal)
        script = "if {[catch {\n" + source + "\n} detail]} {puts stderr $detail; exit 1}\n"
        script += 'puts "$active_machine $c64_clock_mhz $genesis_pal_mode"\n'
        return subprocess.run([shutil.which("tclsh")], input=script, text=True,
                              capture_output=True, env=environment)

    def test_pal_ntsc_machine_clocks(self):
        for pal, frequency in ((0, 107.386), (1, 106.406)):
            with self.subTest(pal=pal):
                result = self.configure(pal=pal)
                self.assertEqual(result.returncode, 0, result.stderr)
                machine, clock, selected = result.stdout.strip().split()
                self.assertEqual((machine, int(selected)), ("4", pal))
                self.assertAlmostEqual(float(clock), frequency)

    def test_bad_or_unrelated_pal_settings_fail(self):
        for machine, pal in (("GENESIS", 2), ("C64", 1), ("SUPERVISOR", 0)):
            with self.subTest(machine=machine, pal=pal):
                self.assertNotEqual(self.configure(machine, pal).returncode, 0)


if __name__ == "__main__":
    unittest.main()
