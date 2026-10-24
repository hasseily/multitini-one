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

    def test_c64_combined_features_have_independent_abi_gates(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 24)
        constants = self.spec["constants"]
        for name in ("C64_TURBO_MIN_ABI_MINOR", "C64_ULTIMATE_AUDIO_MIN_ABI_MINOR",
                     "C64_REU_HOST_MIN_ABI_MINOR"):
            self.assertEqual(constants[name], 24)
        self.assertEqual(constants["C64_EXPANSION_MIN_ABI_MINOR"], 23)
        for name, expected in (("C64_EXPANSION_CAPS_ULTIMATE_AUDIO", 8),
                               ("C64_EXPANSION_CAPS_REU_HOST_LOAD", 16),
                               ("C64_EXPANSION_CONFIG_MASK", 31)):
            self.assertEqual(int(constants[name], 0), expected)
        offsets = {item["name"]: int(item["offset"], 0) for item in self.spec["registers"]}
        for name, expected in (("C64_CPU_CONFIG", 0xD8), ("C64_CPU_CAPS2", 0xE4),
                               ("C64_UA_STATUS", 0xF0), ("C64_UA_FETCHES", 0xF4),
                               ("C64_REU_MISSES", 0xF8), ("C64_RAM_PEEK", 0x10C),
                               ("C64_REU_HOST", 0x110)):
            self.assertEqual(offsets[name], expected)
        self.assertNotIn(0xFC, offsets.values())
        self.assertEqual(int(constants["C64_CPU_CAPS2_RELEASE"], 0) & (1 << 14), 0)
        self.assertEqual(int(constants["C64_EXPANSION_CONFIG_DEFAULT"], 0) & 16, 0)

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

    def test_bidirectional_media_request_addition_is_versioned(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 5)
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
            "MEDIA_DATA",
            "MEDIA_STATE",
        }
        self.assertTrue(
            request_registers.issubset(
                {register["name"] for register in self.spec["registers"]}
            )
        )

    def test_double_buffered_ui_window_is_versioned(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 6)
        registers = {register["name"] for register in self.spec["registers"]}
        self.assertIn("UI_COMMIT", registers)
        self.assertIn("UI_STATUS", registers)
        self.assertIn("CORE_VARIANT_INDEX", registers)
        constants = self.spec["constants"]
        columns = int(constants["UI_COLUMNS"])
        rows = int(constants["UI_ROWS"])
        cell_bytes = int(constants["UI_CELL_BYTES"])
        bank_bytes = int(constants["UI_VRAM_BANK_BYTES"], 0)
        bank0 = int(constants["UI_VRAM_BANK0_OFFSET"], 0)
        bank1 = int(constants["UI_VRAM_BANK1_OFFSET"], 0)
        window = int(constants["UI_WINDOW_SPAN"], 0)
        self.assertEqual(bank_bytes, columns * rows * cell_bytes)
        self.assertGreaterEqual(bank1, bank0 + bank_bytes)
        self.assertLessEqual(bank1 + bank_bytes, window)

    def test_c64_interface_ids_and_required_rom_sizes(self):
        c64 = json.loads((ROOT / "spec/c64_interface_v0.json").read_text())
        self.assertEqual(c64["schema_version"], 1)
        self.assertGreaterEqual(c64["abi"]["minor"], 2)
        load_ids = [int(value, 0) for value in c64["load_kinds"].values()]
        input_ids = [int(value, 0)
                     for value in c64["input_namespaces"].values()]
        self.assertEqual(len(load_ids), len(set(load_ids)))
        self.assertEqual(len(input_ids), len(set(input_ids)))
        self.assertEqual(c64["rom_sizes"]["C64_BASIC"], 8192)
        self.assertEqual(c64["rom_sizes"]["C64_KERNAL"], 8192)
        self.assertEqual(c64["rom_sizes"]["C64_CHARACTER"], 4096)
        self.assertEqual(int(c64["load_kinds"]["C64_PRG_STAGE"], 0), 0x15)
        self.assertEqual(int(c64["load_kinds"]["C64_PRG_RUN"], 0), 0x16)

    def test_dense_ui_preserves_legacy_window_and_fits_screen(self):
        constants = self.spec["constants"]
        self.assertGreaterEqual(self.spec["abi"]["minor"], 11)
        self.assertEqual(constants["UI_COLUMNS"] * constants["UI_CELL_WIDTH"], 1920)
        self.assertEqual(constants["UI_ROWS"] * constants["UI_CELL_HEIGHT"], 1080)
        self.assertEqual(constants["UI_FONT_SCALE"], 2)
        self.assertLessEqual(constants["UI_GLYPH_X"] + 7 * constants["UI_FONT_SCALE"],
                             constants["UI_CELL_WIDTH"])
        self.assertLessEqual(constants["UI_GLYPH_Y"] + 8 * constants["UI_FONT_SCALE"],
                             constants["UI_CELL_HEIGHT"])
        self.assertEqual(int(constants["UI_LEGACY_VRAM_BANK0_OFFSET"], 0), 0x1000)
        self.assertEqual(int(constants["UI_LEGACY_VRAM_BANK1_OFFSET"], 0), 0x2800)
        self.assertEqual(int(constants["UI_LEGACY_VRAM_BANK_BYTES"], 0), 80 * 36 * 2)
        self.assertGreaterEqual(int(constants["UI_VRAM_BANK0_OFFSET"], 0), 0x3E80)
        self.assertEqual(self.spec["bitfields"]["UI_COMMIT"]["DENSE"], 2)
        self.assertEqual(self.spec["bitfields"]["UI_STATUS"]["DENSE_LAYOUT"], 3)
        self.assertEqual(self.spec["bitfields"]["UI_STATUS"]["DENSE"], 4)

    def test_staged_prg_launch_is_versioned(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 9)

    def test_drive_parallel_cable_is_versioned(self):
        self.assertGreaterEqual(self.spec["abi"]["minor"], 10)
        control = self.spec["bitfields"]["CONTROL"]
        self.assertEqual(control["C64_CPU_TURBO_ENABLE"], 5)
        self.assertEqual(control["C64_DRIVE_PARALLEL_ENABLE"], 6)


if __name__ == "__main__":
    unittest.main()
