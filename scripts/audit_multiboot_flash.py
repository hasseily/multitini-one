#!/usr/bin/env python3
"""Audit real firmware/Golden packages with the production C parser in mock NOR."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

import image_manifest

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appletini", required=True, type=Path)
    parser.add_argument("--multitini", required=True, type=Path)
    parser.add_argument("--golden", required=True, type=Path)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/multiboot-flash-audit")
    args = parser.parse_args()
    inputs = {name: getattr(args, name).resolve() for name in ("appletini", "multitini", "golden")}
    for name in ("appletini", "multitini"):
        image_manifest.verify_image(inputs[name], expected_role=image_manifest.ROLE_FIRMWARE,
                                    expected_recovery=True, expected_product=name)
    golden = image_manifest.verify_image(inputs["golden"], expected_role=image_manifest.ROLE_GOLDEN,
                                         expected_recovery=True)
    if golden.version != 1 or inputs["golden"].stat().st_size > 0x100000:
        raise ValueError("Golden must use generic v1 manifest and fit one 1 MiB slot")
    compiler = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise RuntimeError("Set CC to a host C compiler executable")
    out = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    executable = out / ("audit_flash.exe" if os.name == "nt" else "audit_flash")
    sources = [ROOT / "scripts/fixtures/multiboot_image_audit.c",
               ROOT / "firmware/golden/lib/multiboot.c", ROOT / "firmware/golden/lib/crc32.c"]
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
               "-I", str(ROOT / "firmware/golden/lib"), *(str(path) for path in sources),
               "-o", str(executable)]
    subprocess.run(guard + command, check=True, timeout=60)
    result = subprocess.run(guard + [str(executable), *(str(inputs[name]) for name in inputs)],
                            check=True, capture_output=True, text=True, timeout=60)
    report = json.loads(result.stdout.strip().splitlines()[-1])
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    report["inputs"] = {name: {"path": str(path), "sha256": digest(path)} for name, path in inputs.items()}
    headers = [ROOT / "firmware/golden/lib/multiboot.h", ROOT / "firmware/golden/lib/crc32.h"]
    report["production_source_sha256"] = {path.relative_to(ROOT).as_posix(): digest(path)
                                           for path in sources + headers}
    report["compiler"] = compiler
    report["hardware_accessed"] = False
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {report['selection_cases']} actual-artifact boot selections, two transactional installs, live corruption/erasure fallback; {out / 'report.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
