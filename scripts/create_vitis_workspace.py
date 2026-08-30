#!/usr/bin/env python3
"""Create and build the minimal Vitis 2025.2 diagnostic application."""

from __future__ import annotations

import re
import os
import sys
from pathlib import Path

import vitis


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = Path(os.environ.get(
    "MULTITINI_VITIS_WORKSPACE", ROOT / "build" / "vitis_workspace"))
XSA = Path(os.environ.get(
    "MULTITINI_XSA", ROOT / "build" / "vivado" / "multitini_one.xsa"))
REUSE_XPFM = os.environ.get("MULTITINI_PLATFORM_XPFM")
PLATFORM_NAME = "multitini_platform"
DOMAIN_NAME = "standalone_ps7_cortexa9_0"
APP_NAME = os.environ.get("MULTITINI_VITIS_APP", "diagnostic")


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


def patch_usbh_class_info_section(component: str) -> None:
    """Retain CherryUSB class records and expose their linker boundaries."""
    lscript = WORKSPACE / component / "src" / "lscript.ld"
    text = require(lscript, "Vitis linker script").read_text(encoding="utf-8")
    if "__usbh_class_info_start__" in text:
        return
    section = (
        "\n.usbh_class_info : {\n"
        "   . = ALIGN(4);\n"
        "   __usbh_class_info_start__ = .;\n"
        "   KEEP(*(.usbh_class_info))\n"
        "   __usbh_class_info_end__ = .;\n"
        "} > ps7_ddr_0_memory_0\n"
    )
    pattern = re.compile(
        r"(\.rodata\s*:\s*\{.*?\}\s*>\s*ps7_ddr_0_memory_0\s*)",
        re.DOTALL,
    )
    updated, count = pattern.subn(r"\1" + section, text, count=1)
    if count == 0:
        raise RuntimeError(f"Could not find .rodata section in {lscript}")
    lscript.write_text(updated, encoding="utf-8")


def main() -> int:
    require(XSA, "Vivado hardware platform")
    client = vitis.create_client()
    try:
        client.set_workspace(path=str(WORKSPACE))
        if REUSE_XPFM:
            xpfm = require(Path(REUSE_XPFM), "reused exported platform")
            print(f"Reusing exported platform: {xpfm}")
        else:
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
                "third_party/CherryUSB/core/usbh_core.c",
                "third_party/CherryUSB/class/hid/usbh_hid.c",
                "third_party/CherryUSB/class/vendor/xbox/usbh_xbox.c",
                "third_party/CherryUSB/port/ehci/usb_hc_ehci.c",
            ],
            is_skip_copy_sources=True,
        )
        add_include_directory(APP_NAME, ROOT / "firmware" / "include")
        add_include_directory(APP_NAME, ROOT / "firmware" / "platform" /
                              "xilinx")
        add_include_directory(APP_NAME, ROOT / "third_party" / "CherryUSB" /
                              "common")
        add_include_directory(APP_NAME, ROOT / "third_party" / "CherryUSB" /
                              "core")
        add_include_directory(APP_NAME, ROOT / "third_party" / "CherryUSB" /
                              "class" / "hub")
        add_include_directory(APP_NAME, ROOT / "third_party" / "CherryUSB" /
                              "class" / "hid")
        add_include_directory(APP_NAME, ROOT / "third_party" / "CherryUSB" /
                              "class" / "vendor" / "xbox")
        add_include_directory(APP_NAME, ROOT / "third_party" / "CherryUSB" /
                              "port" / "ehci")
        patch_usbh_class_info_section(APP_NAME)
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
