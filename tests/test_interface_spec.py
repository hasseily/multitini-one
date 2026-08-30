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

    def test_input_event_addition_is_versioned(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 3)
        self.assertEqual(self.spec["commands"]["INPUT_EVENT"], 5)
        self.assertEqual(self.spec["bitfields"]["STATUS"]["INPUT_READY"], 8)

    def test_read_only_media_request_addition_is_versioned(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 4)
        self.assertEqual(self.spec["commands"]["MEDIA_MOUNT"], 32)
        self.assertEqual(self.spec["commands"]["MEDIA_EJECT"], 33)
        self.assertEqual(self.spec["commands"]["MEDIA_COMPLETE"], 34)
        self.assertEqual(self.spec["bitfields"]["STATUS"]["MEDIA_REQUEST"], 9)
        self.assertEqual(self.spec["constants"]["MEDIA_BLOCK_BYTES"], 256)
        self.assertEqual(
            int(self.spec["constants"]["MEDIA_MOUNT_READ_ONLY"], 0),
            1 << 16,
        )
        request_registers = {
            "MEDIA_REQUEST",
            "MEDIA_REQUEST_ID",
            "MEDIA_REQUEST_LBA",
            "MEDIA_REQUEST_BLOCK_COUNT",
            "MEDIA_REQUEST_GENERATION",
        }
        self.assertTrue(
            request_registers.issubset(
                {register["name"] for register in self.spec["registers"]}
            )
        )

    def test_c64_interface_ids_and_required_rom_sizes(self):
        c64 = json.loads((ROOT / "spec/c64_interface_v0.json").read_text())
        self.assertEqual(c64["schema_version"], 1)
        load_ids = [int(value, 0) for value in c64["load_kinds"].values()]
        input_ids = [int(value, 0)
                     for value in c64["input_namespaces"].values()]
        self.assertEqual(len(load_ids), len(set(load_ids)))
        self.assertEqual(len(input_ids), len(set(input_ids)))
        self.assertEqual(c64["rom_sizes"]["C64_BASIC"], 8192)
        self.assertEqual(c64["rom_sizes"]["C64_KERNAL"], 8192)
        self.assertEqual(c64["rom_sizes"]["C64_CHARACTER"], 4096)


if __name__ == "__main__":
    unittest.main()
