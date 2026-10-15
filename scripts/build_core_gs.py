#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build or explicitly reuse the IIgs and shared shell/core matrix.

Uses the established matrix runner with explicit per-image settings and
sealed source captures. All generated output stays in a fresh build folder.
"""
import build_phos1
build_phos1.BUILD_ID='4D000400'
build_phos1.DEFAULT_OUTPUT=build_phos1.ROOT / 'build/core-gs3/hardware'
build_phos1.MATRIX['apple-iigs']=('APPLE_IIGS',False,False)
if __name__=='__main__':raise SystemExit(build_phos1.main())
