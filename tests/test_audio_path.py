# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AudioPathTests(unittest.TestCase):
    def test_c64_defaults_to_the_6581_sid_model(self):
        adapter = (
            ROOT / "hdl/core/c64/multitini_c64_adapter.sv"
        ).read_text()
        voice = (
            ROOT / "third_party/c64/upstream/rtl/sid/sid_voice.sv"
        ).read_text()

        self.assertIn("SID_VERSION = 2'b00", adapter)
        self.assertIn(".sid_ver(SID_VERSION)", adapter)
        self.assertNotIn(".sid_ver(2'b11)", adapter)
        self.assertIn("NOISE_TTL_6581 = 24'h8000", voice)
        self.assertIn("NOISE_TTL_8580 = 24'h950000", voice)

    def test_machine_pcm_is_normalized_to_signed_24_bit(self):
        c64 = (ROOT / "hdl/core/c64/multitini_c64_adapter.sv").read_text()
        gbc = (
            ROOT / "hdl/core/gameboy/multitini_gameboy_adapter.sv"
        ).read_text()
        self.assertIn("$signed({raw_audio_l, 6'b000000})", c64)
        self.assertIn("$signed({raw_audio_r, 6'b000000})", c64)
        self.assertIn("$signed({raw_audio_l, 8'h00})", gbc)
        self.assertIn("$signed({raw_audio_r, 8'h00})", gbc)

        self.assertEqual((0x1FFFF << 6) & 0xFFFFFF, 0x7FFFC0)
        self.assertEqual((0x20000 << 6) & 0xFFFFFF, 0x800000)
        self.assertEqual((0x7FFF << 8) & 0xFFFFFF, 0x7FFF00)
        self.assertEqual((0x8000 << 8) & 0xFFFFFF, 0x800000)

    def test_board_routes_coherent_machine_audio_to_physical_output(self):
        top = (ROOT / "hdl/board/multitini_one_top.sv").read_text()
        output = (ROOT / "hdl/board/multitini_audio_test.sv").read_text()
        self.assertIn(".machine_audio_l(c64_audio_l)", top)
        self.assertIn(".machine_audio_r(c64_audio_r)", top)
        self.assertIn(".machine_audio_valid(c64_audio_valid)", top)
        self.assertEqual(output.count("cdc_handshake_reg #(.WIDTH(49))"), 2)
        self.assertIn("machine_ready_launch <= 1'b0", output)
        self.assertIn("machine_ready_launch <= 1'b1", output)
        self.assertEqual(output.count(".din(machine_ready_launch)"), 2)
        self.assertIn("source_test_bclk <= test_tone_bclk", output)
        self.assertIn("mute_bclk || !live_enable_bclk", output)

    def test_spdif_rate_matches_control_clock(self):
        transmitter = (
            ROOT / "hdl/board/multitini_audio_spdif_pcm_tx.sv"
        ).read_text()
        self.assertIn("HALF_BIT_INC = 32'h0BCBE61D", transmitter)
        self.assertIn("sample24 = frame_right", transmitter)
        self.assertIn("sample24 = mute ? 24'd0 : sample_l", transmitter)
        self.assertIn("frame_right <= mute ? 24'sd0 : sample_r", transmitter)


if __name__ == "__main__":
    unittest.main()
