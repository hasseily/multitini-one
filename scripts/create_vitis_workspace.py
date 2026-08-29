#!/usr/bin/env python3
"""Create and build the minimal Vitis 2025.2 diagnostic application."""

from __future__ import annotations

import re
import sys
from pathlib import Path

import vitis


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT / "build" / "vitis_workspace"
XSA = ROOT / "build" / "vivado" / "multitini_one.xsa"
PLATFORM_NAME = "multitini_platform"
DOMAIN_NAME = "standalone_ps7_cortexa9_0"
APP_NAME = "diagnostic"


def require(path: Path, description: str) -> Path:
    if not path.exists():
        raise FileNotFoundError(f"{description} not found: {path}")
    return path


def require_success(status: object, description: str) -> None:
    if status != 0:
        raise RuntimeError(f"{description} returned status {status}")


def add_include_directory(component: str, include_directory: Path) -> None:
    config = WORKSPACE / component / "src" / "UserConfig.cmake"
    text = require(config, "Vitis UserConfig.cmake").read_text(encoding="utf-8")
    pattern = re.compile(r"set\(\s*USER_INCLUDE_DIRECTORIES\b(?P<body>[^)]*)\)",
                         re.DOTALL)
    match = pattern.search(text)
    if match is None:
        raise RuntimeError(f"USER_INCLUDE_DIRECTORIES is absent from {config}")
    # CMake treats backslashes in quoted strings as escape introducers.  Vitis
    # runs this script on Windows too, so always emit a portable slash path.
    entry = f'"{include_directory.as_posix()}"'
    if entry in match.group("body"):
        return
    replacement = f"set(USER_INCLUDE_DIRECTORIES{match.group('body').rstrip()}\n{entry}\n)"
    config.write_text(text[:match.start()] + replacement + text[match.end():],
                      encoding="utf-8")


def main() -> int:
    require(XSA, "Vivado hardware platform")
    client = vitis.create_client()
    try:
        client.set_workspace(path=str(WORKSPACE))
        platform = client.create_platform_component(
            name=PLATFORM_NAME,
            hw_design=str(XSA),
            os="standalone",
            cpu="ps7_cortexa9_0",
            domain_name=DOMAIN_NAME,
            compiler="gcc",
        )
        require_success(platform.build(), "platform build")
        xpfm = (WORKSPACE / PLATFORM_NAME / "export" / PLATFORM_NAME /
                f"{PLATFORM_NAME}.xpfm")
        require(xpfm, "exported platform")

        application = client.create_app_component(
            name=APP_NAME,
            platform=str(xpfm),
            domain=DOMAIN_NAME,
            template="empty_application",
        )
        application.import_files(
            from_loc=str(ROOT),
            files=[
                "firmware/common",
                "firmware/include",
                "firmware/platform/xilinx",
            ],
            is_skip_copy_sources=True,
        )
        add_include_directory(APP_NAME, ROOT / "firmware" / "include")
        require_success(application.build(), "diagnostic application build")
        require(WORKSPACE / APP_NAME / "build" / f"{APP_NAME}.elf",
                "diagnostic ELF")
        print(f"Vitis diagnostic build completed: {WORKSPACE}")
        return 0
    finally:
        vitis.dispose()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"ERROR: {type(error).__name__}: {error}", file=sys.stderr)
        try:
            vitis.dispose()
        except Exception:
            pass
        raise SystemExit(1)
