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
XILFFS_VERSION = "xilffs_v5_5"
XILFFS_READY_STAMP = ".multitini-xilffs-ready"


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


def enable_fatfs_lfn(domain_bsp: Path) -> None:
    """Enable FatFs long filenames in every generated xilffs config copy."""
    patches = {
        Path("include") / "xilffs_config.h": [
            ("/* #undef FILE_SYSTEM_USE_LFN */",
             "#define FILE_SYSTEM_USE_LFN 1"),
        ],
        Path("libsrc") / "build_configs" / "gen_bsp" / "include" /
        "xilffs_config.h": [
            ("/* #undef FILE_SYSTEM_USE_LFN */",
             "#define FILE_SYSTEM_USE_LFN 1"),
        ],
        Path("libsrc") / "build_configs" / "gen_bsp" / "CMakeCache.txt": [
            ("XILFFS_use_lfn:STRING=0", "XILFFS_use_lfn:STRING=1"),
        ],
        Path("libsrc") / "build_configs" / "gen_bsp" /
        "cmake_lib_configs.txt": [
            ("XILFFS_use_lfn:STRING=0", "XILFFS_use_lfn:STRING=1"),
        ],
    }

    for relative, replacements in patches.items():
        path = require(domain_bsp / relative, f"generated {relative}")
        original = path.read_text(encoding="utf-8")
        updated = original
        for old, new in replacements:
            updated = updated.replace(old, new)
        if "LFN" in path.name.upper() or path.name == "xilffs_config.h":
            if "#define FILE_SYSTEM_USE_LFN 1" not in updated:
                raise RuntimeError(f"Could not enable FatFs LFN in {path}")
        elif "XILFFS_use_lfn:STRING=1" not in updated:
            raise RuntimeError(f"Could not enable FatFs LFN in {path}")
        if updated != original:
            path.write_text(updated, encoding="utf-8")
            print(f"Patched FatFs LFN: {path}")

    bsp_yaml = require(domain_bsp / "bsp.yaml", "generated BSP yaml")
    lines = bsp_yaml.read_text(encoding="utf-8").splitlines(keepends=True)
    in_lfn_option = False
    lfn_enabled = False
    for index, line in enumerate(lines):
        if line.startswith("    XILFFS_use_lfn:"):
            in_lfn_option = True
            continue
        if in_lfn_option and line.startswith("    ") and not line.startswith(
                "      "):
            in_lfn_option = False
        if in_lfn_option and line.strip() == "value: '0'":
            lines[index] = line.replace("value: '0'", "value: '1'", 1)
            lfn_enabled = True
            break
        if in_lfn_option and line.strip() == "value: '1'":
            lfn_enabled = True
            break
    yaml_text = "".join(lines)
    if not lfn_enabled:
        raise RuntimeError(f"Could not enable XILFFS_use_lfn in {bsp_yaml}")
    if yaml_text != bsp_yaml.read_text(encoding="utf-8"):
        bsp_yaml.write_text(yaml_text, encoding="utf-8")
        print(f"Patched FatFs LFN: {bsp_yaml}")


def patch_fatfs_no_card_hang(domain_bsp: Path) -> None:
    """Fail fast without a card and bound the card-stable wait to one second."""
    path = require(domain_bsp / "libsrc" / "xilffs" / "src" / "diskio.c",
                   "generated FatFs diskio.c")
    text = path.read_text(encoding="utf-8")
    if "Multitini no-card fail-fast" in text:
        return

    replacements = [
        (
            "\tu32 StatusReg;\n"
            "\tu32 DelayCount = 0;\n",
            "\tu32 StatusReg;\n",
        ),
        (
            "\t\t\tif (CardDetect[pdrv]) {\n"
            "\t\t\t\twhile ((StatusReg & XSDPS_PSR_CARD_INSRT_MASK) == 0U) {\n"
            "\t\t\t\t\tif (DelayCount == 500U) {\n"
            "\t\t\t\t\t\ts = STA_NODISK | STA_NOINIT;\n"
            "\t\t\t\t\t\tgoto Label;\n"
            "\t\t\t\t\t}\n"
            "\t\t\t\t\telse {\n"
            "\t\t\t\t\t\t/* Wait for 10 msec */\n"
            "\t\t\t\t\t\tusleep(SD_CD_DELAY);\n"
            "\t\t\t\t\t\tDelayCount++;\n"
            "\t\t\t\t\t\tStatusReg = XSdPs_GetPresentStatusReg(BaseAddress[pdrv]);\n"
            "\t\t\t\t\t}\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n",
            "\t\t\tif (CardDetect[pdrv]) {\n"
            "\t\t\t\t/* Multitini no-card fail-fast: a missing card\n"
            "\t\t\t\t * must report STA_NODISK immediately, not after\n"
            "\t\t\t\t * a 5 s insertion poll that stalls startup. */\n"
            "\t\t\t\tif ((StatusReg & XSDPS_PSR_CARD_INSRT_MASK) == 0U) {\n"
            "\t\t\t\t\ts = STA_NODISK | STA_NOINIT;\n"
            "\t\t\t\t\tgoto Label;\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n",
        ),
        (
            "\t\tif (CardDetect[pdrv]) {\n"
            "\t\t\t/*\n"
            "\t\t\t * Card detection check\n"
            "\t\t\t * If the HC detects the No Card State, power will be cleared\n"
            "\t\t\t */\n"
            "\t\t\twhile (!((XSDPS_PSR_CARD_DPL_MASK |\n"
            "\t\t\t\t  XSDPS_PSR_CARD_STABLE_MASK |\n"
            "\t\t\t\t  XSDPS_PSR_CARD_INSRT_MASK) ==\n"
            "\t\t\t\t (XSdPs_GetPresentStatusReg(BaseAddress[pdrv]) &\n"
            "\t\t\t\t  (XSDPS_PSR_CARD_DPL_MASK |\n"
            "\t\t\t\t   XSDPS_PSR_CARD_STABLE_MASK |\n"
            "\t\t\t\t   XSDPS_PSR_CARD_INSRT_MASK))));\n"
            "\t\t}\n",
            "\t\tif (CardDetect[pdrv]) {\n"
            "\t\t\t/*\n"
            "\t\t\t * Card detection check\n"
            "\t\t\t * If the HC detects the No Card State, power will be cleared\n"
            "\t\t\t * Multitini: bounded wait (max 1 s) instead of the\n"
            "\t\t\t * stock unbounded loop; report no-disk on timeout.\n"
            "\t\t\t */\n"
            "\t\t\tu32 StableCount = 0U;\n"
            "\t\t\twhile (!((XSDPS_PSR_CARD_DPL_MASK |\n"
            "\t\t\t\t  XSDPS_PSR_CARD_STABLE_MASK |\n"
            "\t\t\t\t  XSDPS_PSR_CARD_INSRT_MASK) ==\n"
            "\t\t\t\t (XSdPs_GetPresentStatusReg(BaseAddress[pdrv]) &\n"
            "\t\t\t\t  (XSDPS_PSR_CARD_DPL_MASK |\n"
            "\t\t\t\t   XSDPS_PSR_CARD_STABLE_MASK |\n"
            "\t\t\t\t   XSDPS_PSR_CARD_INSRT_MASK)))) {\n"
            "\t\t\t\tif (StableCount == 100U) {\n"
            "\t\t\t\t\ts |= STA_NODISK | STA_NOINIT;\n"
            "\t\t\t\t\treturn s;\n"
            "\t\t\t\t}\n"
            "\t\t\t\tusleep(SD_CD_DELAY);\n"
            "\t\t\t\tStableCount++;\n"
            "\t\t\t}\n"
            "\t\t}\n",
        ),
    ]

    for old, new in replacements:
        if old not in text:
            raise RuntimeError(
                f"Could not find expected no-card code in {path}: {old!r}")
        text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8")
    print(f"Patched FatFs no-card handling: {path}")


def configure_xilffs(platform: object) -> None:
    vitis_root = os.environ.get("XILINX_VITIS")
    if not vitis_root:
        raise EnvironmentError("XILINX_VITIS is not set")
    xilffs = require(
        Path(vitis_root) / "data" / "embeddedsw" / "lib" / "sw_services" /
        XILFFS_VERSION,
        "xilffs BSP library",
    )
    domain = platform.get_domain(name=DOMAIN_NAME)
    domain.set_lib(lib_name="xilffs", path=str(xilffs))
    domain.regenerate()
    domain_bsp = (WORKSPACE / PLATFORM_NAME / "ps7_cortexa9_0" /
                  DOMAIN_NAME / "bsp")
    enable_fatfs_lfn(domain_bsp)
    patch_fatfs_no_card_hang(domain_bsp)


def mark_xilffs_ready(xpfm: Path) -> None:
    (xpfm.parent / XILFFS_READY_STAMP).write_text(
        "xilffs_v5_5 lfn=1 no-card=bounded\n", encoding="utf-8")


def validate_reused_xilffs(xpfm: Path) -> None:
    require(xpfm.parent / XILFFS_READY_STAMP,
            "Multitini xilffs-ready platform stamp; rebuild the platform "
            "without MULTITINI_PLATFORM_XPFM")
    include = xpfm.parent / "sw" / DOMAIN_NAME / "include"
    require(include / "ff.h", "reused platform FatFs header")
    config = require(include / "xilffs_config.h",
                     "reused platform xilffs config")
    if "#define FILE_SYSTEM_USE_LFN 1" not in config.read_text(
            encoding="utf-8"):
        raise RuntimeError(
            f"Reused platform lacks FatFs LFN support: {config}")


def main() -> int:
    require(XSA, "Vivado hardware platform")
    client = vitis.create_client()
    try:
        client.set_workspace(path=str(WORKSPACE))
        if REUSE_XPFM:
            xpfm = require(Path(REUSE_XPFM), "reused exported platform")
            validate_reused_xilffs(xpfm)
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
            configure_xilffs(platform)
            require_success(platform.build(), "platform build")
            xpfm = (WORKSPACE / PLATFORM_NAME / "export" / PLATFORM_NAME /
                    f"{PLATFORM_NAME}.xpfm")
            require(xpfm, "exported platform")
            mark_xilffs_ready(xpfm)

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
