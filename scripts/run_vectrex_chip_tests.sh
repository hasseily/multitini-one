#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$repo_root/scripts/run_vectrex_chip_tests.py"
