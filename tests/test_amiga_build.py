# SPDX-License-Identifier: GPL-3.0-only
"""Exercise Amiga source capture, clock selection and ROM-free SD packaging."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_core_amiga
import build_phos1
import capture_vectrex_build_inputs as capture
import check_repo
import package_phos1 as package


class AmigaBuildTests(unittest.TestCase):
    def test_git_keeps_cpu_tables_and_excludes_user_images(self):
        result = subprocess.run(["git", "check-ignore", "--no-index", "--stdin", "-z"],
                                input="\0".join(check_repo.AMIGA_CPU_TABLES) + "\0",
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual((result.returncode, result.stdout), (1, ""))
        private = "private/kick.rom\0private/Workbench.adf\0private/System.hdf\0"
        result = subprocess.run(["git", "check-ignore", "--no-index", "--stdin", "-z"],
                                input=private, cwd=ROOT, text=True, capture_output=True)
        self.assertEqual((result.returncode, result.stdout), (0, private))

    def test_cpu_table_policy_rejects_missing_or_changed_data(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            for relative in check_repo.AMIGA_CPU_TABLES:
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((ROOT / relative).read_bytes())
            with mock.patch.object(check_repo, "ROOT", root):
                errors = []
                check_repo.check_amiga_cpu_tables(errors)
                self.assertEqual(errors, [])
                target = root / "hdl/core/amiga/ports/m68k_cyc_idx.hex"
                original = target.read_bytes()
                target.write_bytes(original.replace(b"\r\n", b"\n"))
                check_repo.check_amiga_cpu_tables(errors)
                self.assertEqual(errors, [])
                target.write_bytes(b"000\n" + original)
                check_repo.check_amiga_cpu_tables(errors)
                self.assertTrue(any("differs" in error for error in errors))
                target.unlink()
                errors = []
                check_repo.check_amiga_cpu_tables(errors)
                self.assertTrue(any("absent" in error for error in errors))

    def test_standalone_wrapper_plans_only_amiga_by_default(self):
        result = subprocess.run([sys.executable, ROOT / "scripts/build_core_amiga.py", "--plan"],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PLAN BUILD amiga ", result.stdout)
        self.assertIn("PLAN TOTAL build=1 reuse=0", result.stdout)
        self.assertNotIn("PLAN BUILD supervisor", result.stdout)

    def test_aga_variant_plans_as_distinct_image(self):
        result = subprocess.run([sys.executable, ROOT / "scripts/build_core_amiga.py", "--plan",
                                 "--image", "amiga-aga", "--output", "build/amiga-aga-plan-test"],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PLAN BUILD amiga-aga ", result.stdout)
        self.assertIn("PLAN TOTAL build=1 reuse=0", result.stdout)
        self.assertEqual(package.AMIGA_AGA_VARIANT, ("aga-pal", "Amiga 1200 AGA PAL", "amiga-aga"))

    def test_explicit_amiga_reuse_is_validated_as_its_own_machine(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            candidate = root / "old"
            candidate.mkdir()
            with mock.patch("core_artifacts.inspect_artifact", return_value={
                    "current_input_match": True, "build_id": "0x4D000801", "abi": "0.24"}) as inspect:
                plan = build_phos1.selection_plan(["amiga"], {"amiga": candidate},
                                                 "4D000802", root / "new")
            inspect.assert_called_once_with(candidate, "AMIGA", gbc_psram=False, genesis_pal=False)
            self.assertEqual(plan["images"]["amiga"]["action"], "reuse")
            self.assertEqual(plan["images"]["amiga"]["artifact"]["build_id"], "0x4D000801")

    def test_source_capture_retains_upstream_and_port_inputs(self):
        paths = {path.relative_to(ROOT).as_posix() for path in capture.inputs("AMIGA")}
        self.assertIn("scripts/build_core_amiga.py", paths)
        for source in (ROOT / "third_party/amiga").rglob("*"):
            if source.is_file() and "__pycache__" not in source.parts:
                self.assertIn(source.relative_to(ROOT).as_posix(), paths)
        for source in (ROOT / "software/amiga").rglob("*"):
            if source.is_file() and "__pycache__" not in source.parts:
                self.assertIn(source.relative_to(ROOT).as_posix(), paths)
        self.assertFalse(any(path.startswith("build/") or path.endswith("kick.rom") for path in paths))


class AmigaContentTests(unittest.TestCase):
    def test_rom_free_package_preserves_user_directories(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            package.prepare_amiga_content(root)
            core = root / "sd/multitini/cores/amiga"
            self.assertTrue((core / "media/README.txt").is_file())
            readme = core / "roms/readme.txt"
            self.assertEqual(readme.read_bytes(), (ROOT / package.AMIGA_ROM_README).read_bytes())
            self.assertEqual(list((core / "roms").iterdir()), [readme])

    def test_amiga_names_preserve_core_and_variant_ids(self):
        self.assertEqual(package.AMIGA_CORE_SPEC,
                         ("amiga", "Amiga", "0x414D4947",
                          [("pal", "Amiga 600 ECS PAL", "amiga")]))

    def test_canonical_sd_data_rejects_a_rom_without_deleting_it(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            readme = root / package.AMIGA_ROM_README
            readme.parent.mkdir(parents=True)
            readme.write_bytes((ROOT / package.AMIGA_ROM_README).read_bytes())
            rom = readme.parent / "kick.rom"
            rom.write_bytes(b"synthetic test input")
            output = root / "output"
            with mock.patch.object(package, "ROOT", root):
                with self.assertRaisesRegex(ValueError, "Canonical Amiga ROM data"):
                    package.prepare_amiga_content(output)
            self.assertEqual(rom.read_bytes(), b"synthetic test input")
            self.assertFalse(output.exists())

    def test_package_rejects_a_preexisting_rom_without_deleting_it(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            rom = root / "sd/multitini/cores/amiga/roms/kick.rom"
            rom.parent.mkdir(parents=True)
            rom.write_bytes(b"synthetic test input")
            with self.assertRaisesRegex(ValueError, "package ROM directory"):
                package.prepare_amiga_content(root)
            self.assertEqual(rom.read_bytes(), b"synthetic test input")

    def test_removed_rom_embedding_argument_is_rejected_before_packaging(self):
        result = subprocess.run([sys.executable, ROOT / "scripts/package_core_gs.py",
                                 "--hardware", "unused", "--workspace", "unused",
                                 "--include-amiga", "--amiga-kickstart", "unused.rom"],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments: --amiga-kickstart", result.stderr)


class AmigaTimingGateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            import tkinter
            cls.interpreter = staticmethod(tkinter.Tcl)
            cls.interpreter().eval("expr {1 + 1}")
        except (ImportError, RuntimeError) as error:
            raise unittest.SkipTest("Tcl interpreter unavailable") from error
        source = (ROOT / "scripts/build_and_export_xsa.tcl").read_text()
        start = source.index("foreach delay_type {max min} {", source.index(
            "check_multitini_timing_coverage $coverage_checks $ethernet_checked"))
        cls.gate = source[start:source.index("\nputs [exec {*}$python", start)]

    def run_gate(self, machine, setup, hold="0.060"):
        tcl = self.interpreter()
        tcl.eval("proc puts {args} {}; proc get_timing_paths {args} {return path}")
        tcl.eval("proc get_property {property path} {global delay_type setup hold; "
                 "if {$delay_type eq {max}} {return $setup}; return $hold}")
        tcl.setvar("image_kind", machine)
        tcl.setvar("setup", setup)
        tcl.setvar("hold", hold)
        try:
            tcl.eval(self.gate)
        except Exception as error:
            return str(error)
        return None

    def test_final_amiga_setup_requires_more_than_ten_picoseconds(self):
        for slack in ("0", "0.009", "0.010"):
            with self.subTest(slack=slack):
                self.assertIn("must exceed +0.010 ns", self.run_gate("AMIGA", slack))
        for slack in ("0.010001", "0.017"):
            with self.subTest(slack=slack):
                self.assertIsNone(self.run_gate("AMIGA", slack))

    def test_other_machines_and_hold_keep_existing_policy(self):
        self.assertIsNone(self.run_gate("GENESIS", "0.009"))
        self.assertIsNone(self.run_gate("AMIGA", "0.017", "0"))
        self.assertIn("Negative min slack", self.run_gate("AMIGA", "0.017", "-0.001"))


@unittest.skipUnless(shutil.which("tclsh"), "native Tcl unavailable")
class AmigaProjectTests(unittest.TestCase):
    def test_amiga_has_distinct_machine_id_and_pal_master_clock(self):
        source = (ROOT / "scripts/create_project.tcl").read_text().split("proc require_file", 1)[0]
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith("MULTITINI_")}
        environment["MULTITINI_MACHINE"] = "AMIGA"
        script = "if {[catch {\n" + source + "\n} detail]} {puts stderr $detail; exit 1}\n"
        script += 'puts "$active_machine $c64_clock_mhz $c64_video_standard"\n'
        result = subprocess.run([shutil.which("tclsh")], input=script, text=True,
                                capture_output=True, env=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        machine, clock, standard = result.stdout.strip().split()
        self.assertEqual((machine, standard), ("7", "N/A"))
        self.assertAlmostEqual(float(clock), 28.375160)


if __name__ == "__main__":
    unittest.main()
