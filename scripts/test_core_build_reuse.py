#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise selective build orchestration without launching Vivado."""
from contextlib import redirect_stdout, redirect_stderr
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import build_phos1 as build
import core_artifacts


class CoreBuildReuseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=build.ROOT / "build")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "selected"
        self.candidates = self.root / "original"
        self.matrix = dict(build.MATRIX, **{"apple-iigs": ("APPLE_IIGS", False, False)})
        for name in self.matrix:
            (self.candidates / name).mkdir(parents=True)
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(build, "MATRIX", self.matrix).start()
        self.inspect = mock.patch.object(core_artifacts, "inspect_artifact", side_effect=self.artifact).start()
        self.native = mock.patch.object(build.subprocess, "run").start()

    def artifact(self, path, machine, **variants):
        return {"build_id": "4D000300", "abi": "0.21", "current_input_match": True,
                "changed_ancillary": ["MULTITINI_ONE_PLAN.md"], "changed_required": [],
                "bit": {"sha256": "a" * 64}, "xsa": {"sha256": "b" * 64}}

    def run_cli(self, *arguments):
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(output):
            result = build.main(["--output", str(self.output), "--build-id", "4D000301", *arguments])
        return result, output.getvalue()

    def test_all_seven_reused_across_release_id_without_tools_or_copies(self):
        result, text = self.run_cli("--reuse-from", str(self.candidates))
        self.assertEqual(result, 0)
        self.assertIn("PLAN TOTAL build=0 reuse=7", text)
        self.native.assert_not_called()
        record = json.loads((self.output / "core-selection.json").read_text())
        self.assertEqual(record["status"], "complete")
        self.assertEqual(record["requested_build_id"], "4D000301")
        self.assertEqual(len(record["images"]), 7)
        for name, selection in record["images"].items():
            self.assertEqual(selection["status"], "reused")
            self.assertEqual(selection["artifact"]["build_id"], "4D000300")
            self.assertEqual(selection["directory"], str((self.candidates / name).resolve()))
            self.assertFalse((self.output / name).exists())

    def test_plan_only_selected_gs_has_no_filesystem_or_native_side_effects(self):
        result, text = self.run_cli("--image", "apple-iigs", "--reuse-from", str(self.candidates), "--plan")
        self.assertEqual(result, 0)
        self.assertIn("PLAN TOTAL build=0 reuse=1", text)
        self.assertNotIn("PLAN REUSE supervisor", text)
        self.assertFalse(self.output.exists())
        self.native.assert_not_called()
        self.inspect.assert_called_once_with(self.candidates / "apple-iigs", "APPLE_IIGS",
                                             gbc_psram=False, genesis_pal=False)

    def test_check_only_can_save_explicit_selection_manifest(self):
        manifest = self.root / "plan.json"
        result, _ = self.run_cli("--image", "apple-iigs", "--check-only", "--selection-manifest", str(manifest))
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(manifest.read_text())["images"]["apple-iigs"]["action"], "build")
        self.native.assert_not_called()

    def test_automatic_incompatible_candidate_schedules_only_that_rebuild(self):
        def inspect(path, machine, **variants):
            value = self.artifact(path, machine)
            if machine == "APPLE_IIGS":
                value.update(current_input_match=False, changed_required=["hdl/core/apple_iigs/ports/iigs.sv"])
            return value
        self.inspect.side_effect = inspect
        result, text = self.run_cli("--reuse-from", str(self.candidates), "--plan")
        self.assertEqual(result, 0)
        self.assertIn("PLAN TOTAL build=1 reuse=6", text)
        self.assertIn("PLAN BUILD apple-iigs", text)
        self.assertIn("hdl/core/apple_iigs/ports/iigs.sv", text)
        self.native.assert_not_called()

    def test_automatic_tampered_candidate_is_reported_for_rebuild(self):
        self.inspect.side_effect = ValueError("Source ZIP hash mismatch")
        result, text = self.run_cli("--image", "apple-iigs", "--reuse-from", str(self.candidates), "--plan")
        self.assertEqual(result, 0)
        self.assertIn("PLAN TOTAL build=1 reuse=0", text)
        self.assertIn("Source ZIP hash mismatch", text)
        self.native.assert_not_called()

    def test_explicit_bad_candidate_fails_before_manifest_or_vivado(self):
        for problem in (ValueError("Original build setting/report mismatch"),
                        ValueError("Wrong source-capture machine")):
            with self.subTest(problem=str(problem)):
                self.inspect.side_effect = problem
                with self.assertRaises(SystemExit) as failure:
                    self.run_cli("--reuse-image", "apple-iigs=" + str(self.candidates / "apple-iigs"))
                self.assertEqual(failure.exception.code, 2)
                self.assertFalse(self.output.exists())
                self.native.assert_not_called()

    def test_explicit_effective_input_change_fails_closed(self):
        value = self.artifact(None, None)
        value.update(current_input_match=False, changed_required=["scripts/create_project.tcl"])
        self.inspect.side_effect = None
        self.inspect.return_value = value
        with self.assertRaises(SystemExit):
            self.run_cli("--image", "apple-iigs", "--reuse-image", "apple-iigs=" + str(self.candidates / "apple-iigs"))
        self.native.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_explicit_override_takes_priority_and_variant_is_checked(self):
        override = self.root / "pal-special"
        override.mkdir()
        result, _ = self.run_cli("--image", "genesis-pal", "--reuse-from", str(self.candidates),
                                 "--reuse-image", "genesis-pal=" + str(override), "--plan")
        self.assertEqual(result, 0)
        self.inspect.assert_called_once_with(override, "GENESIS", gbc_psram=False, genesis_pal=True)

    def test_invalid_duplicate_or_unselected_reuse_does_not_launch_tools(self):
        valid = "apple-iigs=" + str(self.candidates / "apple-iigs")
        for arguments in (("--reuse-image", "unknown=" + str(self.candidates)),
                          ("--reuse-image", valid, "--reuse-image", valid),
                          ("--image", "gbc", "--reuse-image", valid)):
            with self.subTest(arguments=arguments), self.assertRaises(SystemExit):
                self.run_cli(*arguments)
        self.native.assert_not_called()

    def test_only_non_reused_target_gets_capture_generate_and_build_commands(self):
        commands = []
        def inspect(path, machine, **variants):
            value = self.artifact(path, machine)
            if self.output in path.parents:
                value["build_id"] = "4D000301"
            return value
        self.inspect.side_effect = inspect
        def native(command, **kwargs):
            command = list(map(str, command))
            commands.append(command)
            if "capture_vectrex_build_inputs.py" in command[1] and "--verify" not in command:
                path = Path(command[command.index("--output") + 1])
                path.write_text(json.dumps({"files": {}}))
        self.native.side_effect = native
        result, text = self.run_cli("--image", "supervisor", "--image", "apple-iigs",
            "--reuse-image", "supervisor=" + str(self.candidates / "supervisor"), "--jobs", "1")
        self.assertEqual(result, 0)
        self.assertIn("PLAN TOTAL build=1 reuse=1", text)
        self.assertEqual(len(commands), 4)
        vivado = [command for command in commands if "-source" in command]
        self.assertEqual(len(vivado), 2)
        self.assertEqual({Path(command[command.index("-source") + 1]).name for command in vivado}, {"generate.tcl", "build.tcl"})
        self.assertTrue(all("apple-iigs" in command[command.index("-source") + 1] for command in vivado))
        record = json.loads((self.output / "core-selection.json").read_text())
        self.assertEqual(record["images"]["supervisor"]["status"], "reused")
        self.assertEqual(record["images"]["apple-iigs"]["status"], "built")
        self.assertEqual(record["images"]["apple-iigs"]["artifact"]["build_id"], "4D000301")

    def test_failed_new_build_is_recorded_without_falling_back_to_reuse(self):
        self.native.side_effect = RuntimeError("Captured build tool failed")
        result, text = self.run_cli("--image", "apple-iigs")
        self.assertEqual(result, 1)
        self.assertIn("FAIL apple-iigs", text)
        record = json.loads((self.output / "core-selection.json").read_text())
        self.assertEqual(record["status"], "failed")
        self.assertEqual(record["images"]["apple-iigs"]["status"], "failed")
        self.assertNotIn("artifact", record["images"]["apple-iigs"])


if __name__ == "__main__":
    unittest.main()
