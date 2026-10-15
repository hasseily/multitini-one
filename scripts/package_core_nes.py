#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit NES firmware and the explicitly selected compatible core images."""
import sys
import package_phos1

package_phos1.BUILD_ID = "4D000501"
package_phos1.ABI_MINOR = 22

if __name__ == "__main__":
    sys.argv += ["--include-apple-iigs", "--include-nes"]
    if not any(value == "--suffix" or value.startswith("--suffix=") for value in sys.argv[1:]):
        sys.argv += ["--suffix", "core-nes-m051"]
    raise SystemExit(package_phos1.main())
