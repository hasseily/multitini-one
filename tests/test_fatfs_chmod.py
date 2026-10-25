# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production FatFs BSP patch and reuse gate without Vitis."""
import ast
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ChmodTests(unittest.TestCase):
    def setUp(self):
        tree = ast.parse((ROOT / "scripts/create_vitis_workspace.py").read_text())
        functions = {"require", "enable_fatfs_chmod", "validate_reused_xilffs"}
        constants = {"DOMAIN_NAME", "XILFFS_READY_STAMP", "XILFFS_READY_DESCRIPTION"}
        selected = [node for node in tree.body if
                    (isinstance(node, ast.FunctionDef) and node.name in functions) or
                    (isinstance(node, ast.Assign) and any(
                        isinstance(target, ast.Name) and target.id in constants
                        for target in node.targets))]
        self.namespace = {"Path": Path}
        exec(compile(ast.Module(body=selected, type_ignores=[]),
                     "<production-FatFs-chmod>", "exec"), self.namespace)
        self.patch = self.namespace["enable_fatfs_chmod"]
        self.validate = self.namespace["validate_reused_xilffs"]
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.bsp = Path(self.temp.name) / "bsp"
        generated = self.bsp / "libsrc/build_configs/gen_bsp"
        self.configs = [self.bsp / "include/xilffs_config.h",
                        generated / "include/xilffs_config.h",
                        generated / "CMakeCache.txt", generated / "cmake_lib_configs.txt"]
        for path in self.configs:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("/* #undef FILE_SYSTEM_USE_CHMOD */\n" if path.suffix == ".h"
                            else "XILFFS_use_chmod:BOOL=OFF\n")
        self.xpfm = Path(self.temp.name) / "export/platform.xpfm"
        self.include = self.xpfm.parent / "sw" / self.namespace["DOMAIN_NAME"] / "include"
        self.include.mkdir(parents=True)
        self.stamp = self.xpfm.parent / self.namespace["XILFFS_READY_STAMP"]
        self.stamp.write_text(self.namespace["XILFFS_READY_DESCRIPTION"])
        (self.include / "ff.h").write_text("/* FatFs */\n")
        (self.include / "xilffs_config.h").write_text(
            "#define FILE_SYSTEM_USE_LFN 1\n#define FILE_SYSTEM_FS_EXFAT\n"
            "#define FILE_SYSTEM_USE_CHMOD\n")
        (self.include / "diskio.h").write_text("#define MT_SD_CTRL_QUIESCE 0x80U\n")

    def test_all_four_configs_are_synchronized_and_idempotent(self):
        self.configs[0].write_text("#define FILE_SYSTEM_USE_CHMOD\n#define KEEP_THIS 42\n")
        self.patch(self.bsp)
        first = [path.read_bytes() for path in self.configs]
        self.patch(self.bsp)
        self.assertEqual(first, [path.read_bytes() for path in self.configs])
        for path in self.configs:
            text = path.read_text()
            self.assertIn("#define FILE_SYSTEM_USE_CHMOD" if path.suffix == ".h"
                          else "XILFFS_use_chmod:BOOL=ON", text)
            self.assertNotIn("#undef", text)
            self.assertNotIn(":BOOL=OFF", text)
        self.assertIn("#define KEEP_THIS 42", self.configs[0].read_text())

    def test_missing_markers_are_rejected_in_each_config(self):
        for path in self.configs:
            with self.subTest(path=path.name):
                saved = path.read_text()
                path.write_text("/* changed vendor configuration */\n")
                with self.assertRaisesRegex(RuntimeError, "Could not enable FatFs CHMOD"):
                    self.patch(self.bsp)
                path.write_text(saved)

    def test_missing_generated_file_is_rejected(self):
        self.configs[-1].unlink()
        with self.assertRaises(FileNotFoundError):
            self.patch(self.bsp)

    def test_current_export_and_stamp_are_accepted(self):
        self.validate(self.xpfm)

    def test_old_ready_stamp_is_rejected(self):
        self.stamp.write_text(self.namespace["XILFFS_READY_DESCRIPTION"].replace("chmod=1 ", ""))
        with self.assertRaisesRegex(RuntimeError, "predates the required FatFs BSP features"):
            self.validate(self.xpfm)

    def test_missing_ready_stamp_is_rejected(self):
        self.stamp.unlink()
        with self.assertRaises(FileNotFoundError):
            self.validate(self.xpfm)

    def test_new_stamp_cannot_hide_missing_chmod_support(self):
        path = self.include / "xilffs_config.h"
        path.write_text(path.read_text().replace("#define FILE_SYSTEM_USE_CHMOD",
                                                 "/* #undef FILE_SYSTEM_USE_CHMOD */"))
        with self.assertRaisesRegex(RuntimeError, "lacks #define FILE_SYSTEM_USE_CHMOD"):
            self.validate(self.xpfm)


if __name__ == "__main__":
    unittest.main()
