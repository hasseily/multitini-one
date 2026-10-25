# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production C64 expansion settings transactions with mocked MMIO/SD."""
from __future__ import annotations

import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def production_function(source: str, signature: str) -> str:
    """Extract a definition, skipping forward declarations and quoted braces."""
    import re
    for match in re.finditer(re.escape(signature), source):
        start = match.start()
        opening = source.index("{", start)
        if ";" in source[start:opening]:
            continue
        tokens = re.compile(r'''/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]''', re.S)
        depth = 0
        for token in tokens.finditer(source, opening):
            if token.group() == "{": depth += 1
            elif token.group() == "}":
                depth -= 1
                if depth == 0: return source[start:token.end()]
    raise AssertionError(f"Definition not found: {signature}")


class C64ExpansionRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
        if compiler is None:
            raise unittest.SkipTest("A host C compiler is required")
        (ROOT / "build").mkdir(exist_ok=True)
        cls.temporary = tempfile.TemporaryDirectory(prefix="c64-expansion-runtime-", dir=ROOT / "build")
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        source = (ROOT / "firmware/platform/xilinx/main.c").read_text()
        signatures = (
            "static bool ensure_active_settings_profile(void)",
            "static mt_c64_capabilities_t selection_c64_features(",
            "static mt_c64_title_values_t c64_stored_values(",
            "static void refresh_c64_title(",
            "static mt_c64_title_values_t c64_requested_values(",
            "static int apply_c64_title(",
            "static int uci_apply_config(",
            "static void configure_c64_expansion(",
            "static bool commit_c64_edit(",
            "static void change_c64_expansion(",
            "static void change_c64_turbo(",
            "static unsigned cycle_setting(",
            "static void change_c64_sid_setup(",
            "static void c64_title_launched(",
            "static mt_c64_content_service_result_t c64_title_load(",
            "static mt_c64_content_service_result_t c64_title_disk(",
            "static const char *c64_content_failure_message(",
        )
        harness = folder / "runtime.c"
        fixture = (ROOT / "tests/host/c64_title_runtime.inc").read_text()
        # Preserve the actual Media failure call as well as the helper: a
        # working helper alone does not prove the menu uses the refusal reason.
        media_failure = re.findall(
            r"set_supervisor_error\(c64_content_failure_message\(result\.status, remounted\)\);",
            source)
        if len(media_failure) != 1:
            raise AssertionError("Expected exactly one production Media failure call")
        content = (ROOT / "firmware/common/content_type.c").read_text()
        classifier = "\n".join(production_function(content, s) for s in (
            "static char ascii_lower(", "static bool extension_is(",
            "mt_content_type_t mt_content_classify(const char *path)"))
        harness.write_text(fixture.replace("/* PRODUCTION_FUNCTIONS */",
            classifier + "\n" + "\n".join(production_function(source, s) for s in signatures)).replace(
                "/* PRODUCTION_MENU_FAILURE */", media_failure[0]))
        cls.executable = folder / ("runtime.exe" if os.name == "nt" else "runtime")
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", compiler,
                   "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                   "-I", str(ROOT / "firmware/include"), "-I", str(ROOT / "firmware/platform/xilinx"),
                   str(harness), str(ROOT / "firmware/platform/xilinx/supervisor_settings.c"),
                   str(ROOT / "firmware/platform/xilinx/c64_title_service.c"),
                   *[str(ROOT / "firmware/common" / name) for name in
                     ("gamepad.c", "controls.c", "video_effects.c", "c64_features.c",
                      "c64_sid.c", "c64_title.c", "media_browser.c")], "-lm", "-o", str(cls.executable)]
        result = subprocess.run(command, text=True, capture_output=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_scenario(self, name):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--",
                                 str(self.executable), name], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS: C64 expansion {name}", result.stdout)

    def test_default_and_saved_configuration(self): self.run_scenario("defaults")
    def test_older_abi_and_missing_capabilities(self): self.run_scenario("gates")
    def test_mode_cycle_and_reu_toggle(self): self.run_scenario("cycle")
    def test_cartridge_conflict_precedes_writes(self): self.run_scenario("conflict")
    def test_save_failure_preserves_hardware_and_settings(self): self.run_scenario("save-failure")
    def test_partial_capability_preserves_other_saved_preferences(self): self.run_scenario("partial")
    def test_ua_atomic_apply_and_refusals(self): self.run_scenario("ua")

    def test_title_owned_edits_remain_transient(self): self.run_scenario("title")
    def test_first_sid_table_commit_from_invalid_reset(self): self.run_scenario("sid-first")
    def test_failed_hold_does_not_apply(self): self.run_scenario("hold-failure")
    def test_rejected_sid_table_leaves_old_settings(self): self.run_scenario("sid-reject")
    def test_ambiguous_sid_ack_preserves_hold(self): self.run_scenario("sid-timeout")
    def test_sid_io_ownership_excludes_live_devices(self): self.run_scenario("io-owner")
    def test_default_reu_cartridge_refusal_reaches_uart_and_menu(self): self.run_scenario("launch-refusal")
    def test_disk_launch_refusal_reaches_uart_and_menu(self): self.run_scenario("disk-refusal")
    def test_allowed_title_launches_still_reach_loader_and_set_home(self): self.run_scenario("launch-allowed")
    def test_uci_load_config_preserves_software_unlock(self): self.run_scenario("uci-unlock-config")


if __name__ == "__main__":
    unittest.main()
