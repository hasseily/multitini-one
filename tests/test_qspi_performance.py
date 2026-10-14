"""Execute the production QSPI library with a vendor-faithful native model."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QspiPerformanceTests(unittest.TestCase):
    def test_quad_spi_and_unsupported_board_variants(self):
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not compiler:
            candidate = Path("C:/msys64/ucrt64/bin/gcc.exe")
            if candidate.exists():
                compiler = str(candidate)
        self.assertIsNotNone(compiler, "A native compiler is required")
        variants = (
            ("quad", 200000000, 2, 0, 1),
            ("one-wire", 200000000, 0, 0, 0),
            ("parallel", 200000000, 2, 2, 0),
            ("clock-above-fsbl", 240000000, 2, 0, 0),
            ("unknown-bsp", None, None, None, 0),
        )
        with tempfile.TemporaryDirectory(prefix="qspi-nor-native-") as name:
            folder = Path(name)
            for label, clock, width, connection, expected in variants:
                with self.subTest(label=label):
                    binary = folder / (label + (".exe" if os.name == "nt" else ""))
                    command = [str(compiler), "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Wpedantic",
                               "-Werror", "-Wconversion", "-Wshadow", "-Wstrict-prototypes",
                               "-I", str(ROOT / "tests/xilinx_stubs"), "-I", str(ROOT / "firmware/golden/lib"),
                               f"-DEXPECT_QUAD={expected}"]
                    if clock is not None:
                        command += [f"-DXPAR_XQSPIPS_0_CLOCK_FREQ={clock}U",
                                    f"-DXPAR_XQSPIPS_0_QSPI_BUS_WIDTH={width}U",
                                    f"-DXPAR_XQSPIPS_0_CONNECTION_MODE={connection}U"]
                    command += [str(ROOT / "tests/host/test_qspi_nor.c"),
                                str(ROOT / "firmware/golden/lib/qspi_nor.c"), "-o", str(binary)]
                    launcher = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
                    compiled = subprocess.run(launcher + command, capture_output=True, text=True, timeout=60)
                    self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                    executed = subprocess.run(launcher + [str(binary)], capture_output=True, text=True, timeout=60)
                    self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)
                    self.assertIn("PASS: QSPI", executed.stdout)

    def test_shared_source_and_no_linear_or_status_write_path(self):
        source = ROOT / "firmware/golden/lib/qspi_nor.c"
        appletini = ROOT / "build/appletini-multiboot/ps_sources/lib/qspi_nor.c"
        if appletini.exists():
            self.assertEqual(source.read_bytes(), appletini.read_bytes())
        code = source.read_text()
        self.assertNotIn("XQspiPs_LqspiRead(", code)
        self.assertNotIn("0xFC000000", code)
        self.assertNotIn("CMD_WRSR", code)
        self.assertIn("QSPI_READ_CHUNK  4096U", code)
        self.assertIn("QSPI_QUAD_IO_CFG 0x0000016BU", code)


if __name__ == "__main__":
    unittest.main()
