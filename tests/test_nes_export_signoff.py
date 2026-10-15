# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import revalidate_nes_export as signoff


class NesExportSignoffTests(unittest.TestCase):
    def pair(self):
        current = (ROOT / signoff.CHECKER).read_bytes()
        text = current.decode().replace("\r\n", "\n")
        start = text.index(signoff.START)
        end = text.index(signoff.END, start)
        original = (text[:start] + signoff.OLD_GUARD + text[end:]).encode()
        return original, current

    def test_only_exact_reviewed_guard_delta_is_accepted(self):
        original, current = self.pair()
        record = signoff.checker_delta(original, current)
        self.assertEqual(record["corrected_guard_sha256"], signoff.NEW_GUARD_SHA256)

    def test_design_command_change_outside_guard_is_rejected(self):
        original, current = self.pair()
        changed = current.replace(b"reset_run synth_1", b"reset_run different_run", 1)
        with self.assertRaisesRegex(ValueError, "outside the reviewed clock guard"):
            signoff.checker_delta(original, changed)

    def test_unreviewed_clock_ratio_is_rejected(self):
        original, current = self.pair()
        changed = current.replace(b"CLKFBOUT_MULT_F 31.5", b"CLKFBOUT_MULT_F 32.0", 1)
        with self.assertRaisesRegex(ValueError, "not the reviewed structural check"):
            signoff.checker_delta(original, changed)

    def test_cdc_census_adds_nes_without_changing_any_timing_bound(self):
        current = (ROOT / signoff.CDC_CHECKER).read_bytes()
        old = current.replace(b"([0-6])", b"([0-5])", 1).replace(
            b"Genesis, IIgs and NES capture", b"Genesis and IIgs capture", 1).replace(
            b"$active_machine in {4 5 6}", b"$active_machine in {4 5}", 1)
        signoff.cdc_checker_delta(old, current)
        with self.assertRaisesRegex(ValueError, "beyond the reviewed"):
            signoff.cdc_checker_delta(old, current.replace(b"> 0.0005", b"> 0.5", 1))

    def test_receipt_rejects_binary_tampering_and_external_paths(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            path = directory / "image.bit"
            path.write_bytes(b"selected exact bitstream")
            pinned = {"image.bit": signoff.describe(path)}
            signoff.pinned_files(directory, pinned)
            path.write_bytes(b"different bitstream")
            with self.assertRaisesRegex(ValueError, "receipt file changed"):
                signoff.pinned_files(directory, pinned)
            with self.assertRaisesRegex(ValueError, "Unsafe signoff receipt path"):
                signoff.pinned_files(directory, {"../outside.bit": pinned["image.bit"]})


if __name__ == "__main__":
    unittest.main()
