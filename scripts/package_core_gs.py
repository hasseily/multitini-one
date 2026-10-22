#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit IIgs firmware and independently versioned, compatible FPGA images."""
import sys
import package_phos1
package_phos1.BUILD_ID='4D000400'
package_phos1.ABI_MINOR=23
if __name__=='__main__':
    sys.argv += ['--include-apple-iigs']
    raise SystemExit(package_phos1.main())
