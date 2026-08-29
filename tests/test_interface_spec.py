# SPDX-License-Identifier: GPL-3.0-only
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class InterfaceSpecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.spec = json.loads((ROOT / "spec/interface_v0.json").read_text())

    def test_register_offsets_are_unique_aligned_and_in_span(self):
        span = int(self.spec["register_span"], 0)
        offsets = [int(register["offset"], 0) for register in self.spec["registers"]]
        self.assertEqual(len(offsets), len(set(offsets)))
        self.assertTrue(all(offset % 4 == 0 and 0 <= offset < span for offset in offsets))

    def test_major_zero_is_explicitly_pre_release(self):
        self.assertEqual(self.spec["abi"]["major"], 0)
        self.assertGreaterEqual(self.spec["abi"]["minor"], 1)

    def test_control_defaults_keep_diagnostics_visible_and_core_reset(self):
        control = next(reg for reg in self.spec["registers"] if reg["name"] == "CONTROL")
        reset = int(control["reset"], 0)
        fields = self.spec["bitfields"]["CONTROL"]
        for name in ("CORE_RESET_REQUEST", "VIDEO_TEST_ENABLE",
                     "AUDIO_TEST_ENABLE", "AUDIO_MUTE"):
            self.assertNotEqual(reset & (1 << fields[name]), 0)


if __name__ == "__main__":
    unittest.main()
