#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual Tcl coverage gate, including its narrow I/O exception."""
from pathlib import Path
import tkinter
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Coverage(unittest.TestCase):
    def setUp(self):
        self.tcl = tkinter.Tcl()
        self.tcl.eval((ROOT / "scripts/check_timing_coverage.tcl").read_text())

    def check(self, report, enabled=True):
        self.tcl.call("check_multitini_timing_coverage", report, enabled)

    def partial(self, names):
        return (f"There are {len(names)} input ports with partial input delay specified. (HIGH)\n\n"
                + "\n".join(names) + "\n\n")

    def test_clean(self):
        self.check(self.partial([]), False)

    def test_bounded_bidirectional_pads(self):
        self.check(self.partial([f"eth_d[{i}]" for i in range(8)]))

    def test_reject_unproved_or_other_ports(self):
        valid = self.partial([f"eth_d[{i}]" for i in range(8)])
        for report, enabled in [(valid, False), (valid.replace("eth_d[3]", "psram_a_io[3]"), True),
                                (valid.replace("eth_d[3]", "eth_d[4]"), True),
                                (self.partial(["eth_d[0]"]), True), ("", True), (valid + valid, True)]:
            with self.subTest(report=report, enabled=enabled), self.assertRaises(tkinter.TclError):
                self.check(report, enabled)

    def test_other_failures_remain_fatal(self):
        for failure in ["1 register/latch pins with no clock", "1 pins that are not constrained for maximum delay",
                        "1 input ports with no input delay specified", "1 ports with no output delay specified. (HIGH)",
                        "1 ports with partial output delay specified"]:
            with self.subTest(failure=failure), self.assertRaises(tkinter.TclError):
                self.check(self.partial([]) + "There are " + failure)


if __name__ == "__main__":
    unittest.main()
