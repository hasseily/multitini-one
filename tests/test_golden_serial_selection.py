# SPDX-License-Identifier: GPL-3.0-only
"""Execute Golden's product-selection command with request failures injected."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

PREAMBLE = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "serial_boot.h"
#include "multiboot_platform.h"
static mb_flash_t flash;
static int unavailable, request_result;
static unsigned request_calls, messages;
static uint32_t requested, default_product=MB_PRODUCT_MULTITINI;
static void serial_puts_both(const char *text) { assert(text); ++messages; }
static void serial_logf(const char *format, ...) { assert(format); ++messages; }
const mb_flash_t *mb_platform_flash(void) { return unavailable ? NULL : &flash; }
int mb_prepare_request(const mb_flash_t *f, uint32_t product, uint32_t *golden_offset) {
    assert(f==&flash && golden_offset);
    assert(product==MB_PRODUCT_APPLETINI || product==MB_PRODUCT_MULTITINI);
    ++request_calls;
    if (request_result==MB_OK) { requested=product; *golden_offset=MB_GOLDEN_OFFSET; }
    else *golden_offset=UINT32_MAX;
    return request_result;
}
const char *mb_product_name(uint32_t product) {
    assert(product==MB_PRODUCT_APPLETINI || product==MB_PRODUCT_MULTITINI);
    return product==MB_PRODUCT_APPLETINI ? "Appletini" : "Multitini";
}
'''

SCENARIO = r'''
int main(void) {
    char *normal[]={"boot"};
    char *appletini[]={"boot", "appletini"};
    char *multitini[]={"boot", "MuLtItInI"};
    char *bad[]={"boot", "unknown"};
    char *extra[]={"boot", "appletini", "extra", "extra", "extra"};
    const int failures[]={MB_E_UNAVAILABLE,MB_E_READ,MB_E_METADATA,MB_E_CRC,MB_E_PROGRAM};
    unsigned i,calls;
    assert(select_firmware_command(1,normal)==SERIAL_BOOT_ACTION_BOOT_FIRMWARE);
    assert(request_calls==0 && requested==MB_PRODUCT_NONE);
    assert(select_firmware_command(2,appletini)==SERIAL_BOOT_ACTION_BOOT_FIRMWARE);
    assert(request_calls==1 && requested==MB_PRODUCT_APPLETINI);
    assert(select_firmware_command(2,multitini)==SERIAL_BOOT_ACTION_BOOT_FIRMWARE);
    assert(request_calls==2 && requested==MB_PRODUCT_MULTITINI);
    calls=request_calls;
    assert(select_firmware_command(2,bad)==SERIAL_BOOT_ACTION_NONE);
    for (i=3;i<=5;++i) assert(select_firmware_command((int)i,extra)==SERIAL_BOOT_ACTION_NONE);
    assert(request_calls==calls && requested==MB_PRODUCT_MULTITINI);
    unavailable=1;
    assert(select_firmware_command(2,appletini)==SERIAL_BOOT_ACTION_NONE);
    assert(request_calls==calls && requested==MB_PRODUCT_MULTITINI);
    unavailable=0;
    for (i=0;i<sizeof failures/sizeof failures[0];++i) {
        request_result=failures[i];
        assert(select_firmware_command(2,appletini)==SERIAL_BOOT_ACTION_NONE);
        assert(request_calls==++calls && requested==MB_PRODUCT_MULTITINI);
    }
    assert(default_product==MB_PRODUCT_MULTITINI && messages>0);
    puts("PASS: Golden serial selection preserves default and rejects failed requests");
    return 0;
}
'''


class GoldenSerialSelectionTests(unittest.TestCase):
    def test_product_request_success_and_failure_do_not_change_startup_default(self):
        compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("A host C compiler is required")
        source = (ROOT / "firmware/golden/bootloader/serial_boot.c").read_text()
        functions = []
        for signature in ("static uint8_t ascii_tolower(uint8_t c)",
                          "static int str_ieq(const char *a, const char *b)",
                          "static serial_boot_action_t select_firmware_command(int argc, char **argv)"):
            start = source.index(signature + "\n{")
            functions.append(source[start:source.index("\n}", start) + 2])
        parser = source[source.index("static serial_boot_action_t process_command("):]
        self.assertIn("return select_firmware_command(argc, argv);", parser)
        with tempfile.TemporaryDirectory(prefix="golden-serial-selection-") as name:
            directory = Path(name)
            harness = directory / "selection.c"
            harness.write_text(PREAMBLE + "\n".join(functions) + SCENARIO)
            executable = directory / ("selection.exe" if os.name == "nt" else "selection")
            guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
            result = subprocess.run(guard + [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                            "-Wpedantic", "-I", str(ROOT / "firmware/golden/lib"),
                                            "-I", str(ROOT / "firmware/golden/bootloader"),
                                            str(harness), "-o", str(executable)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(guard + [str(executable)], capture_output=True,
                                    text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS: Golden serial selection", result.stdout)


if __name__ == "__main__":
    unittest.main()
