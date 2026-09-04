# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import argparse
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "make_core_package.py"
SPEC = importlib.util.spec_from_file_location("make_core_package", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class CorePackageToolTests(unittest.TestCase):
    def test_parser_defaults_to_current_abi(self) -> None:
        args = MODULE.parser().parse_args([
            "--id", "gbc", "--name", "Game Boy Color",
            "--core-id", "0x47424320",
            "--content-root", "0:/multitini/cores/gbc/media",
            "--variant", "default|GB/GBC|core.bit",
            "--output", "package",
        ])
        self.assertEqual(args.abi_major, 0)
        self.assertEqual(args.abi_minor, 10)

    def test_variant_and_paths_reject_unsafe_values(self) -> None:
        parsed = MODULE.parse_variant("pal|PAL 50 Hz|build/core.bit")
        self.assertEqual(parsed.variant_id, "pal")
        self.assertEqual(parsed.label, "PAL 50 Hz")
        with self.assertRaises(argparse.ArgumentTypeError):
            MODULE.parse_variant("PAL|PAL|core.bit")
        with self.assertRaises(ValueError):
            MODULE.validate_content_root("0:/multitini/cores/../escape")

    def test_create_package_writes_runtime_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temp_path = Path(temporary)
            pal = temp_path / "pal.bit"
            ntsc = temp_path / "ntsc.bit"
            pal.write_bytes(b"bit")
            ntsc.write_bytes(b"bit")
            payloads = {"pal": b"P" * (1024 * 1024),
                        "ntsc": b"N" * (1024 * 1024 + 4)}

            def fake_process(_: str, source: Path,
                             destination: Path) -> None:
                destination.write_bytes(payloads[source.stem])

            args = argparse.Namespace(
                id="c64",
                name="Commodore 64",
                core_id=0x43363420,
                content_root="0:/multitini/cores/c64",
                abi_major=0,
                abi_minor=5,
                variants=[MODULE.Variant("pal", "PAL 50 Hz", pal),
                          MODULE.Variant("ntsc", "NTSC 60 Hz", ntsc)],
                output=temp_path / "package",
                bootgen=None,
            )
            with mock.patch.object(MODULE, "process_bitstream", fake_process), \
                    mock.patch.object(MODULE, "locate_bootgen",
                                      return_value="bootgen"):
                manifest = MODULE.create_package(args)
            text = manifest.read_text(encoding="ascii")
            self.assertIn("schema=1", text)
            self.assertIn("core_id=0x43363420", text)
            self.assertIn(
                "variant=pal|PAL 50 Hz|c64-pal.bin|1048576|", text)
            self.assertIn(
                "variant=ntsc|NTSC 60 Hz|c64-ntsc.bin|1048580|", text)
            self.assertEqual(
                (manifest.parent / "c64-pal.bin").read_bytes(),
                payloads["pal"])


if __name__ == "__main__":
    unittest.main()
