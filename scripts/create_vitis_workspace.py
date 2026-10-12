#!/usr/bin/env python3
"""Create and build the minimal Vitis 2025.2 diagnostic application."""

from __future__ import annotations

import os
from datetime import datetime, timezone
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import vitis


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = Path(os.environ.get(
    "MULTITINI_VITIS_WORKSPACE", ROOT / "build" / "vitis_workspace"))
XSA = Path(os.environ.get(
    "MULTITINI_XSA", ROOT / "build" / "vivado" / "multitini_one.xsa"))
REUSE_XPFM = os.environ.get("MULTITINI_PLATFORM_XPFM")
CPU1_BASELINE = os.environ.get("MULTITINI_CPU1_BASELINE")
PLATFORM_NAME = "multitini_platform"
DOMAIN_NAME = "standalone_ps7_cortexa9_0"
APP_NAME = os.environ.get("MULTITINI_VITIS_APP", "diagnostic")
CPU1_DOMAIN_NAME = "standalone_ps7_cortexa9_1"
CPU1_APP_NAME = "video_worker"
GOLDEN_APP_NAME = "golden"
CPU1_ENTRY = 0x20000000
CPU1_PRIVATE_BYTES = 0x01000000
CPU1_READY_STAMP = ".multitini-cpu1-ready"
CPU1_READY_DESCRIPTION = "video-worker-abi=1 USE_AMP=1 entry=0x20000000 bytes=0x01000000\n"
XILFFS_VERSION = "xilffs_v5_5"
XILFFS_READY_STAMP = ".multitini-xilffs-ready"
XILFFS_READY_DESCRIPTION = (
    "xilffs_v5_5 lfn=1 exfat=1 media-validation=r0.16-patch2 "
    "fat-time-hook=rtc-cache no-card=bounded\n"
)


def ensure_lopper_preprocessor() -> None:
    """Expose the host GCC that Vitis' Windows lopper subprocess expects."""
    if os.name != "nt" or shutil.which("gcc") is not None:
        return
    vitis_root = os.environ.get("XILINX_VITIS")
    if not vitis_root:
        raise EnvironmentError("XILINX_VITIS is not set")
    install_root = Path(vitis_root).parent
    candidates = sorted(
        (install_root / "tps" / "mingw").glob("*/win64.o/nt/bin"),
    )
    for candidate in candidates:
        if ((candidate / "gcc.exe").is_file() and
                (candidate / "cpp.exe").is_file()):
            os.environ["PATH"] = f"{candidate}{os.pathsep}{os.environ['PATH']}"
            print(f"Using Vitis host preprocessor: {candidate}")
            return
    raise FileNotFoundError("Vitis host GCC/cpp not found under tps/mingw")


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


def vectrex32_sources() -> list[str]:
    """Use the same audited GS BASIC source whitelist as the host build."""
    manifest = ROOT / "firmware/vectrex32/gsbasic_sources.txt"
    sources = []
    for entry in require(manifest, "GS BASIC source whitelist").read_text().splitlines():
        entry = entry.strip()
        if not entry or entry.startswith("#"):
            continue
        if not re.fullmatch(r"[A-Za-z0-9_/]+\.cpp", entry) or ".." in entry:
            raise ValueError(f"Invalid GS BASIC source path: {entry}")
        path = f"third_party/gsbasic/{entry}"
        require(ROOT / path, "GS BASIC source")
        sources.append(path)
    if not sources or len(sources) != len(set(sources)):
        raise ValueError("GS BASIC source whitelist is empty or contains duplicates")
    local_sources = ["firmware/vectrex32/runtime.cpp", "firmware/vectrex32/lowlevel.cpp"]
    for path in local_sources:
        require(ROOT / path, "Vectrex32 adapter source")
    return sources + local_sources


def configure_vectrex32_cpp(component: str) -> None:
    """Keep C compiled as C, use the C++ linker, and reserve interpreter memory."""
    directory = WORKSPACE / component / "src"
    config = require(directory / "UserConfig.cmake", "Vitis user configuration")
    text = config.read_text(encoding="utf-8")
    marker = "# Multitini Vectrex32 runtime settings"
    if marker not in text:
        paths = "\n".join(f'"{(ROOT / path).as_posix()}"' for path in vectrex32_sources())
        text += (f"\n{marker}\nset(CMAKE_CXX_STANDARD 17)\n"
                 "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
                 f"set_source_files_properties(\n{paths}\nPROPERTIES\n"
                 'COMPILE_OPTIONS "-O2;-fwrapv;-fno-strict-aliasing;-fexceptions;-frtti")\n')
        config.write_text(text, encoding="utf-8")
    cmake = require(directory / "CMakeLists.txt", "Vitis application CMake")
    text = cmake.read_text(encoding="utf-8")
    # The vendor template switches the C compiler to g++ for mixed projects,
    # which changes the meaning of existing .c firmware. CMake already selects
    # the C++ linker when the executable contains .cpp sources.
    text = text.replace("set(CMAKE_C_COMPILER ${CMAKE_CXX_COMPILER})", "")
    # Keep incremental command-line builds independent of Vitis's server PATH.
    # The vendor template otherwise emits an unqualified arm-none-eabi-size.
    size_marker = "# Resolve size beside the selected compiler"
    if size_marker not in text:
        size_command = "print_elf_size(CMAKE_SIZE ${APP_NAME})"
        if size_command not in text:
            raise RuntimeError(f"Missing ELF size command in {cmake}")
        text = text.replace(size_command,
            f"{size_marker}\n"
            'get_filename_component(_multitini_tool_dir "${CMAKE_C_COMPILER}" DIRECTORY)\n'
            'find_program(MULTITINI_SIZE NAMES arm-none-eabi-size\n'
            '    HINTS "${_multitini_tool_dir}" NO_DEFAULT_PATH REQUIRED)\n'
            'set(CMAKE_SIZE "${MULTITINI_SIZE}")\n' + size_command)
    cmake.write_text(text, encoding="utf-8")
    lscript = require(directory / "lscript.ld", "Vitis linker script")
    text = lscript.read_text(encoding="utf-8")
    for symbol, value in (("_STACK_SIZE", "0x200000"), ("_HEAP_SIZE", "0x4000000")):
        text, count = re.subn(
            rf"(?m)^{symbol} = DEFINED\({symbol}\) \? {symbol} : [^;]+;",
            f"{symbol} = DEFINED({symbol}) ? {symbol} : {value};", text)
        if count != 1:
            raise RuntimeError(f"Could not configure {symbol} in {lscript}")
    lscript.write_text(text, encoding="utf-8")


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


def enable_fatfs_exfat(domain_bsp: Path) -> None:
    """Keep Vitis 2025.2's exported and build-tree exFAT config in sync.

    domain.set_config() updates bsp.yaml, CMakeCache.txt, and the gen_bsp
    header, but this Vitis release leaves the exported include header and
    cmake_lib_configs.txt at their old defaults.
    """
    patches = {
        Path("include") / "xilffs_config.h": [
            ("/* #undef FILE_SYSTEM_FS_EXFAT */",
             "#define FILE_SYSTEM_FS_EXFAT"),
        ],
        Path("libsrc") / "build_configs" / "gen_bsp" / "include" /
        "xilffs_config.h": [
            ("/* #undef FILE_SYSTEM_FS_EXFAT */",
             "#define FILE_SYSTEM_FS_EXFAT"),
        ],
        Path("libsrc") / "build_configs" / "gen_bsp" / "CMakeCache.txt": [
            ("XILFFS_enable_exfat:BOOL=OFF",
             "XILFFS_enable_exfat:BOOL=ON"),
        ],
        Path("libsrc") / "build_configs" / "gen_bsp" /
        "cmake_lib_configs.txt": [
            ("XILFFS_enable_exfat:BOOL=OFF",
             "XILFFS_enable_exfat:BOOL=ON"),
        ],
    }

    for relative, replacements in patches.items():
        path = require(domain_bsp / relative, f"generated {relative}")
        original = path.read_text(encoding="utf-8")
        updated = original
        for old, new in replacements:
            updated = updated.replace(old, new)
        if "xilffs_config.h" == path.name:
            if "#define FILE_SYSTEM_FS_EXFAT" not in updated:
                raise RuntimeError(f"Could not enable FatFs exFAT in {path}")
        elif "XILFFS_enable_exfat:BOOL=ON" not in updated:
            raise RuntimeError(f"Could not enable FatFs exFAT in {path}")
        if updated != original:
            path.write_text(updated, encoding="utf-8")
            print(f"Patched FatFs exFAT: {path}")


def patch_fatfs_media_validation(domain_bsp: Path) -> None:
    """Backport the FatFs R0.16 patch-2 media validation checks.

    Vitis 2025.2 supplies xilffs 5.5 with FatFs R0.15a. Keep the upstream
    bounds checks in this reproducible BSP build instead of modifying the
    machine-wide Xilinx installation.
    """
    path = require(domain_bsp / "libsrc" / "xilffs" / "src" / "ff.c",
                   "generated FatFs ff.c")
    text = path.read_text(encoding="utf-8")
    patched_checks = (
        "#define MIN_EXFAT\t0x00000100",
        "nclst < MIN_EXFAT || nclst > MAX_EXFAT",
        "fasize >= 0x200000U",
        "si < dj.dir[XDIR_NumLabel] && si < 11U",
    )
    if all(check in text for check in patched_checks):
        return

    replacements = [
        (
            "#define MAX_EXFAT\t0x7FFFFFFD\t\t/* Max exFAT clusters "
            "(differs from specs, implementation limit) */\n",
            "#define MAX_EXFAT\t0x7FFFFFFD\t\t/* Max exFAT clusters "
            "(differs from specs, implementation limit) */\n"
            "#define MIN_EXFAT\t0x00000100\t\t/* Min exFAT clusters "
            "(implementation limit) */\n",
        ),
        (
            "\t\tnclst = ld_dword(fs->win + BPB_NumClusEx);\t\t/* Number "
            "of clusters */\n"
            "\t\tif (nclst > MAX_EXFAT) {\n"
            "\t\t\treturn FR_NO_FILESYSTEM;        /* (Too many clusters) */\n"
            "\t\t}\n",
            "\t\tnclst = ld_dword(fs->win + BPB_NumClusEx);\t\t/* Number "
            "of clusters */\n"
            "\t\tif (nclst < MIN_EXFAT || nclst > MAX_EXFAT) {\n"
            "\t\t\treturn FR_NO_FILESYSTEM;        /* (Wrong cluster count) */\n"
            "\t\t}\n",
        ),
        (
            "\t\tif (fasize == 0) {\n"
            "\t\t\tfasize = ld_dword(fs->win + BPB_FATSz32);\n"
            "\t\t}\n"
            "\t\tfs->fsize = fasize;\n",
            "\t\tif (fasize == 0) {\n"
            "\t\t\tfasize = ld_dword(fs->win + BPB_FATSz32);\n"
            "\t\t}\n"
            "\t\tif (fasize >= 0x200000U) {\n"
            "\t\t\treturn FR_NO_FILESYSTEM;        /* (FAT is too large) */\n"
            "\t\t}\n"
            "\t\tfs->fsize = fasize;\n",
        ),
        (
            "\t\t\t\t\tfor (si = di = hs = 0; si < "
            "dj.dir[XDIR_NumLabel]; si++) {\t/* Extract volume label from "
            "83 entry */\n",
            "\t\t\t\t\tfor (si = di = hs = 0; si < "
            "dj.dir[XDIR_NumLabel] && si < 11U; si++) {\t/* Extract volume "
            "label from 83 entry */\n",
        ),
    ]
    for old, new in replacements:
        if old not in text:
            raise RuntimeError(
                f"Could not find expected FatFs media-validation code in "
                f"{path}: {old!r}")
        text = text.replace(old, new, 1)
    if not all(check in text for check in patched_checks):
        raise RuntimeError(f"FatFs media-validation patch incomplete: {path}")
    path.write_text(text, encoding="utf-8")
    print(f"Patched FatFs media validation: {path}")


def verify_fatfs_exfat_configuration(domain_bsp: Path) -> None:
    """Stop if Vitis did not apply exFAT and its BSP safety hooks."""
    config_paths = (
        domain_bsp / "include" / "xilffs_config.h",
        domain_bsp / "libsrc" / "build_configs" / "gen_bsp" / "include" /
        "xilffs_config.h",
    )
    for path in config_paths:
        text = require(path, "generated xilffs_config.h").read_text(
            encoding="utf-8")
        for marker in ("#define FILE_SYSTEM_FS_EXFAT",
                       "#define FILE_SYSTEM_USE_LFN 1"):
            if marker not in text:
                raise RuntimeError(f"Missing {marker!r} in {path}")

    generated = domain_bsp / "libsrc" / "build_configs" / "gen_bsp"
    for path in (generated / "CMakeCache.txt",
                 generated / "cmake_lib_configs.txt"):
        if "XILFFS_enable_exfat:BOOL=ON" not in require(
                path, "generated BSP exFAT config").read_text(
                    encoding="utf-8"):
            raise RuntimeError(f"exFAT is not enabled in {path}")

    yaml_path = require(domain_bsp / "bsp.yaml", "generated BSP yaml")
    yaml = yaml_path.read_text(encoding="utf-8")
    option_start = yaml.find("    XILFFS_enable_exfat:")
    option_end = yaml.find("\n    XILFFS_", option_start + 1)
    if option_start < 0:
        raise RuntimeError(f"Missing XILFFS_enable_exfat in {yaml_path}")
    if option_end < 0:
        option_end = len(yaml)
    if "value: 'true'" not in yaml[option_start:option_end]:
        raise RuntimeError(f"exFAT is not enabled in {yaml_path}")

    ff_path = require(domain_bsp / "libsrc" / "xilffs" / "src" / "ff.c",
                      "patched FatFs ff.c")
    ff_source = ff_path.read_text(encoding="utf-8")
    for marker in ("#define MIN_EXFAT\t0x00000100",
                   "nclst < MIN_EXFAT || nclst > MAX_EXFAT",
                   "fasize >= 0x200000U",
                   "si < dj.dir[XDIR_NumLabel] && si < 11U"):
        if marker not in ff_source:
            raise RuntimeError(f"Missing FatFs media guard {marker!r} in "
                               f"{ff_path}")

    diskio_path = require(domain_bsp / "libsrc" / "xilffs" / "src" /
                          "diskio.c", "patched FatFs diskio.c")
    diskio_source = diskio_path.read_text(encoding="utf-8")
    for marker in ("__attribute__((weak)) DWORD "
                   "multitini_fatfs_get_fattime (void)",
                   "return multitini_fatfs_get_fattime();"):
        if marker not in diskio_source:
            raise RuntimeError(f"Missing FatFs RTC timestamp hook "
                               f"{marker!r} in {diskio_path}")


def enable_fatfs_timestamp_hook(domain_bsp: Path) -> None:
    """Patch generated FatFs get_fattime() to allow an RTC-backed override."""
    path = require(domain_bsp / "libsrc" / "xilffs" / "src" / "diskio.c",
                   "generated FatFs diskio.c")
    text = path.read_text(encoding="utf-8")
    if "multitini_fatfs_get_fattime" in text:
        return

    old = (
        "DWORD get_fattime (void)\n"
        "{\n"
        "\treturn\t((DWORD)(2010U - 1980U) << 25U)\t/* Fixed to Jan. 1, 2010 */\n"
        "\t\t| ((DWORD)1 << 21)\n"
        "\t\t| ((DWORD)1 << 16)\n"
        "\t\t| ((DWORD)0 << 11)\n"
        "\t\t| ((DWORD)0 << 5)\n"
        "\t\t| ((DWORD)0 >> 1);\n"
        "}\n"
    )
    new = (
        "__attribute__((weak)) DWORD multitini_fatfs_get_fattime (void)\n"
        "{\n"
        "\treturn\t((DWORD)(2010U - 1980U) << 25U)\t/* Fixed to Jan. 1, 2010 */\n"
        "\t\t| ((DWORD)1 << 21)\n"
        "\t\t| ((DWORD)1 << 16)\n"
        "\t\t| ((DWORD)0 << 11)\n"
        "\t\t| ((DWORD)0 << 5)\n"
        "\t\t| ((DWORD)0 >> 1);\n"
        "}\n\n"
        "DWORD get_fattime (void)\n"
        "{\n"
        "\treturn multitini_fatfs_get_fattime();\n"
        "}\n"
    )
    if old not in text:
        raise RuntimeError(f"Could not find generated get_fattime() in "
                           f"{path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"Patched FatFs timestamp hook: {path}")


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
    domain.set_config(option="lib", param="XILFFS_enable_exfat",
                      value="true", lib_name="xilffs")
    domain.regenerate()
    domain_bsp = (WORKSPACE / PLATFORM_NAME / "ps7_cortexa9_0" /
                  DOMAIN_NAME / "bsp")
    enable_fatfs_exfat(domain_bsp)
    enable_fatfs_lfn(domain_bsp)
    patch_fatfs_media_validation(domain_bsp)
    enable_fatfs_timestamp_hook(domain_bsp)
    patch_fatfs_no_card_hang(domain_bsp)
    verify_fatfs_exfat_configuration(domain_bsp)


def mark_xilffs_ready(xpfm: Path) -> None:
    (xpfm.parent / XILFFS_READY_STAMP).write_text(
        XILFFS_READY_DESCRIPTION, encoding="utf-8")


def validate_reused_xilffs(xpfm: Path) -> None:
    stamp = require(xpfm.parent / XILFFS_READY_STAMP,
                    "Multitini xilffs-ready platform stamp; rebuild the "
                    "platform without MULTITINI_PLATFORM_XPFM")
    if stamp.read_text(encoding="utf-8") != XILFFS_READY_DESCRIPTION:
        raise RuntimeError(
            f"Reused platform predates the required FatFs BSP features: "
            f"{stamp}")
    include = xpfm.parent / "sw" / DOMAIN_NAME / "include"
    require(include / "ff.h", "reused platform FatFs header")
    config = require(include / "xilffs_config.h",
                     "reused platform xilffs config")
    config_text = config.read_text(encoding="utf-8")
    for marker in ("#define FILE_SYSTEM_USE_LFN 1",
                   "#define FILE_SYSTEM_FS_EXFAT"):
        if marker not in config_text:
            raise RuntimeError(
                f"Reused platform lacks {marker}: {config}")


def configure_cpu1_domain(platform: object) -> None:
    """AMP startup pattern from Appletini One at
    1fd4f371e8f4ef08bc63875550564590609fcfeb. No PS hardware change is needed.
    CPU1 must not initialize the SCU or shared L2 already owned by CPU0.
    """
    domain = platform.add_domain(cpu="ps7_cortexa9_1", os="standalone",
                                 name=CPU1_DOMAIN_NAME,
                                 display_name=CPU1_DOMAIN_NAME, compiler="gcc")
    domain.set_config(option="proc", param="proc_extra_compiler_flags",
                      value="-DUSE_AMP=1")
    domain.set_config(option="os", param="standalone_stdout", value="ps7_uart_1")
    domain.regenerate()


def validate_reused_cpu1(xpfm: Path) -> None:
    stamp = require(xpfm.parent / CPU1_READY_STAMP,
                    "CPU1 AMP platform stamp; rebuild without MULTITINI_PLATFORM_XPFM")
    if stamp.read_text(encoding="utf-8") != CPU1_READY_DESCRIPTION:
        raise RuntimeError(f"Reused CPU1 platform configuration differs: {stamp}")
    include = xpfm.parent / "sw" / CPU1_DOMAIN_NAME / "include"
    require(include / "xil_mmu.h", "CPU1 standalone BSP")
    require(xpfm.parent / "sw" / CPU1_DOMAIN_NAME / "lib" / "libxil.a",
            "CPU1 standalone library")


def configure_amp_application(component: str, secondary: bool) -> None:
    """Give each image a bounded linker region and explicit optimization."""
    directory = WORKSPACE / component / "src"
    config = require(directory / "UserConfig.cmake", "Vitis user configuration")
    text = config.read_text(encoding="utf-8")
    optimization = "-O3" if secondary else "-O2"
    text, count = re.subn(r"set\(\s*USER_COMPILE_OPTIMIZATION_LEVEL\b[^)]*\)",
                         f"set(USER_COMPILE_OPTIMIZATION_LEVEL {optimization})", text)
    if count != 1:
        raise RuntimeError(f"Missing optimization setting: {config}")
    # Preserve the vendor's ARM ABI; only select NEON in its existing FPU flag.
    text = re.sub(r"-mfpu=[A-Za-z0-9_-]+", "-mfpu=neon", text)
    if "-mfpu=neon" not in text:
        text += '\nlist(APPEND USER_COMPILE_OPTIONS " -mfpu=neon")\n'
    if 'list(APPEND USER_LINK_LIBRARIES "m")' not in text:
        text += '\nlist(APPEND USER_LINK_LIBRARIES "m")\n'
    config.write_text(text, encoding="utf-8")
    lscript = require(directory / "lscript.ld", "Vitis linker script")
    text = lscript.read_text(encoding="utf-8")
    origin = CPU1_ENTRY if secondary else 0x00100000
    length = CPU1_PRIVATE_BYTES if secondary else CPU1_ENTRY - origin
    text, count = re.subn(
        r"(ps7_ddr_0_memory_0\s*:\s*ORIGIN\s*=\s*)0x[0-9a-fA-F]+"
        r"(\s*,\s*LENGTH\s*=\s*)0x[0-9a-fA-F]+",
        rf"\g<1>0x{origin:x}\g<2>0x{length:x}", text)
    if count != 1:
        raise RuntimeError(f"Could not bound DDR region: {lscript}")
    if secondary:
        for symbol, value in (("_STACK_SIZE", "0x40000"), ("_HEAP_SIZE", "0x10000")):
            text, count = re.subn(
                rf"(?m)^{symbol} = DEFINED\({symbol}\) \? {symbol} : [^;]+;",
                f"{symbol} = DEFINED({symbol}) ? {symbol} : {value};", text)
            if count != 1:
                raise RuntimeError(f"Could not configure {symbol}: {lscript}")
    elif ".mt_video_core1 :" not in text:
        text, count = re.subn(r"(?m)^\.rodata\s*:",
            ".mt_video_core1 : { KEEP(*(.mt_video_core1)) } > ps7_ddr_0_memory_0\n\n.rodata :",
            text, count=1)
        if count != 1:
            raise RuntimeError(f"Could not add embedded CPU1 section: {lscript}")
    lscript.write_text(text, encoding="utf-8")


def validate_elf_memory(path: Path, base: int, length: int) -> None:
    """Reject misplaced entry points or LOAD/BSS ranges before packaging."""
    data = require(path, "ARM ELF").read_bytes()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01":
        raise RuntimeError(f"Expected little-endian ELF32: {path}")
    entry, phoff = struct.unpack_from("<II", data, 24)
    phentsize, phnum = struct.unpack_from("<HH", data, 42)
    if entry != base or phentsize < 32 or phnum == 0:
        raise RuntimeError(f"Unexpected ARM entry/program headers: {path}")
    if phoff + phnum * phentsize > len(data):
        raise RuntimeError(f"Truncated ARM program headers: {path}")
    loads = 0
    for index in range(phnum):
        ptype, offset, vaddr, paddr, filesz, memsz = struct.unpack_from(
            "<6I", data, phoff + index * phentsize)
        if ptype != 1:
            continue
        loads += 1
        if (vaddr != paddr or paddr < base or paddr + memsz > base + length or
                filesz > memsz or offset + filesz > len(data)):
            raise RuntimeError(f"ARM LOAD segment exceeds reserved memory: {path}")
    if loads == 0:
        raise RuntimeError(f"ARM ELF has no loadable segments: {path}")


def arm_objcopy() -> Path:
    return require(Path(os.environ["XILINX_VITIS"]) / "gnu" / "aarch32" / "nt" /
                   "gcc-arm-none-eabi" / "bin" / "arm-none-eabi-objcopy.exe",
                   "ARM objcopy")


def embed_cpu1(application: object) -> Path:
    elf = WORKSPACE / CPU1_APP_NAME / "build" / f"{CPU1_APP_NAME}.elf"
    validate_elf_memory(elf, CPU1_ENTRY, CPU1_PRIVATE_BYTES)
    directory = WORKSPACE / APP_NAME / "src"
    binary = directory / "video_core1_blob.bin"
    subprocess.run([str(arm_objcopy()), "-O", "binary", str(elf), str(binary)], check=True)
    if not 0 < binary.stat().st_size <= CPU1_PRIVATE_BYTES:
        raise RuntimeError("CPU1 binary exceeds its private region")
    assembly = directory / "video_core1_blob.S"
    assembly.write_text(
        '/* Generated CPU1 image; do not edit. */\n'
        '    .section .mt_video_core1, "a", %progbits\n'
        '    .global mt_video_core1_blob_start\n'
        '    .global mt_video_core1_blob_end\n'
        '    .balign 32\n'
        'mt_video_core1_blob_start:\n'
        f'    .incbin "{binary.as_posix()}"\n'
        'mt_video_core1_blob_end:\n', encoding="utf-8")
    application.import_files(from_loc=str(directory), files=[assembly.name],
                             is_skip_copy_sources=True)
    return binary


def capture_build_inputs(xpfm: Path) -> dict:
    """Freeze rebuildable application inputs before either CPU is compiled."""
    suffixes = {".c", ".h", ".cpp", ".hpp", ".S", ".s", ".ld", ".cmake", ".inc", ".i", ".txt"}
    paths = {Path(__file__).resolve()}
    if CPU1_BASELINE:
        paths.update(ROOT / "scripts" / name for name in
                     ("package_cpu1_video_test.py", "package_firmware_release.py"))
    for directory in ("firmware", "third_party/CherryUSB", "third_party/gsbasic"):
        paths.update(path.resolve() for path in (ROOT / directory).rglob("*")
                     if path.is_file() and path.suffix in suffixes and
                     not any(part in {".git", "build", "__pycache__"}
                             for part in path.relative_to(ROOT).parts))
    files = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in sorted(paths)}
    platform = xpfm.resolve().parent
    platform_files = {
        path.relative_to(platform).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(platform.rglob("*")) if path.is_file()}
    return {"schema": 1, "captured_utc": datetime.now(timezone.utc).isoformat(),
            "files": files, "platform_path": str(platform),
            "platform_files": platform_files,
            "xsa": {"path": str(XSA.resolve()),
                    "sha256": hashlib.sha256(XSA.read_bytes()).hexdigest()}}


def select_retained_cpu1(build_inputs: dict) -> None:
    """Select an exact baseline ELF only after qualifying a normal CPU1 rebuild."""
    if not CPU1_BASELINE:
        return
    sys.path.insert(0, str(ROOT / "scripts"))
    import package_firmware_release as release
    package = release.p
    baseline = Path(CPU1_BASELINE).resolve()
    release.verify_directory(baseline)
    release.verify_source_archive(baseline)
    original = json.loads((baseline / "evidence/vitis/cpu1_manifest.json").read_text())
    previous = original["build_inputs"]
    package.require(build_inputs["platform_files"] == previous["platform_files"],
                    "Retained CPU1 platform differs from fresh application export")
    for name, digest in previous["compiled"][CPU1_APP_NAME]["files"].items():
        package.require(build_inputs["files"].get(name) == digest, "Retained CPU1 source changed: " + name)
    selected_elf = baseline / "inputs/video_worker.elf"
    selected_binary = baseline / "inputs/video_core1_blob.bin"
    validate_elf_memory(selected_elf, CPU1_ENTRY, CPU1_PRIVATE_BYTES)
    directory = WORKSPACE / CPU1_APP_NAME / "build"
    elf = directory / f"{CPU1_APP_NAME}.elf"
    rebuilt_elf, rebuilt_binary = directory / "rebuilt_video_worker.elf", directory / "rebuilt_video_worker.bin"
    package.require(not rebuilt_elf.exists() and not rebuilt_binary.exists(), "CPU1 rebuild evidence already exists")
    package.copy(elf, rebuilt_elf)
    subprocess.run([str(arm_objcopy()), "-O", "binary", str(rebuilt_elf), str(rebuilt_binary)], check=True)
    selected_check = directory / "verified_selected_video_worker.bin"
    subprocess.run([str(arm_objcopy()), "-O", "binary", str(selected_elf), str(selected_check)], check=True)
    package.require(selected_check.read_bytes() == selected_binary.read_bytes(), "Retained CPU1 ELF/raw mismatch")
    qualification = package.compare_cpu1_build_id(selected_elf, selected_binary, rebuilt_elf, rebuilt_binary)
    package.copy(selected_elf, elf)
    build_inputs["retained_cpu1"] = {
        "baseline_path": str(baseline), "baseline_manifest_sha256": package.sha(baseline / "manifest.json"),
        "selected_elf": package.describe(selected_elf), "selected_binary": package.describe(selected_binary),
        "rebuilt_elf": {"path": str(rebuilt_elf), **package.describe(rebuilt_elf)},
        "rebuilt_binary": {"path": str(rebuilt_binary), **package.describe(rebuilt_binary)},
        "qualification": qualification,
    }
    print("Selected exact retained CPU1 ELF after qualification: " + qualification["comparison"])


def verify_build_inputs(record: dict, apps=None) -> dict:
    """Reject sources changed during a build; audit actual compiler inputs."""
    for name, expected in record["files"].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Application source changed during build: {name}")
    for name, expected in record["platform_files"].items():
        path = Path(record["platform_path"]) / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Exported platform changed during build: {name}")
    if hashlib.sha256(Path(record["xsa"]["path"]).read_bytes()).hexdigest() != record["xsa"]["sha256"]:
        raise RuntimeError("Selected hardware XSA changed during application build")
    compiled = {}
    for app in (apps if apps is not None else (APP_NAME, CPU1_APP_NAME)):
        commands = WORKSPACE / app / "build" / "compile_commands.json"
        entries = json.loads(commands.read_text(encoding="utf-8"))
        if not entries:
            raise RuntimeError(f"Empty compiler source list: {commands}")
        listing = {}
        for entry in entries:
            path = Path(entry["file"]).resolve()
            if path == (WORKSPACE / APP_NAME / "src/video_core1_blob.S").resolve():
                name = "generated/video_core1_blob.S"
            else:
                name = path.relative_to(ROOT).as_posix()
                if name not in record["files"]:
                    raise RuntimeError(f"Compiled source was not captured before build: {name}")
            listing[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        compiled[app] = {"files": listing,
                         "compile_commands_sha256": hashlib.sha256(commands.read_bytes()).hexdigest()}
    return {**record, "compiled": compiled}


def verify_embedded_cpu1(binary: Path, build_inputs: dict) -> None:
    elf = WORKSPACE / APP_NAME / "build" / f"{APP_NAME}.elf"
    validate_elf_memory(elf, 0x00100000, CPU1_ENTRY - 0x00100000)
    extracted = WORKSPACE / APP_NAME / "build" / "verified_video_core1.bin"
    subprocess.run([str(arm_objcopy()), "--dump-section",
                    f".mt_video_core1={extracted}", str(elf)], check=True)
    if extracted.read_bytes() != binary.read_bytes():
        raise RuntimeError("CPU0 embedded video worker differs from selected CPU1 ELF")
    paths = {"cpu0_elf": elf,
             "cpu1_elf": WORKSPACE / CPU1_APP_NAME / "build" / f"{CPU1_APP_NAME}.elf",
             "cpu1_binary": binary}
    manifest = {name: {"path": str(path), "bytes": path.stat().st_size,
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                for name, path in paths.items()}
    manifest["build_inputs"] = verify_build_inputs(build_inputs)
    (WORKSPACE / "cpu1_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    require(XSA, "Vivado hardware platform")
    ensure_lopper_preprocessor()
    client = vitis.create_client()
    try:
        client.set_workspace(path=str(WORKSPACE))
        if REUSE_XPFM:
            xpfm = require(Path(REUSE_XPFM), "reused exported platform")
            validate_reused_xilffs(xpfm)
            validate_reused_cpu1(xpfm)
            client.add_platform_repos(platform=str(xpfm.parent))
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
            configure_cpu1_domain(platform)
            require_success(platform.build(), "platform build")
            xpfm = (WORKSPACE / PLATFORM_NAME / "export" / PLATFORM_NAME /
                    f"{PLATFORM_NAME}.xpfm")
            require(xpfm, "exported platform")
            mark_xilffs_ready(xpfm)
            (xpfm.parent / CPU1_READY_STAMP).write_text(
                CPU1_READY_DESCRIPTION, encoding="utf-8")

        if os.environ.get("MULTITINI_PLATFORM_ONLY") == "1":
            print(f"Vitis CPU0/CPU1 platform build completed: {xpfm}")
            return 0

        build_inputs = capture_build_inputs(xpfm)
        if os.environ.get("MULTITINI_SKIP_GOLDEN") == "1":
            print("Skipping golden recovery application (MULTITINI_SKIP_GOLDEN=1)")
        else:
            golden = client.create_app_component(
                name=GOLDEN_APP_NAME, platform=str(xpfm), domain=DOMAIN_NAME,
                template="empty_application")
            golden.import_files(from_loc=str(ROOT), files=["firmware/golden"],
                                is_skip_copy_sources=True)
            add_include_directory(GOLDEN_APP_NAME, ROOT / "firmware/golden/lib")
            configure_amp_application(GOLDEN_APP_NAME, secondary=False)
            require_success(golden.build(), "golden recovery application build")
            validate_elf_memory(WORKSPACE / GOLDEN_APP_NAME / "build" / "golden.elf",
                                0x00100000, CPU1_ENTRY - 0x00100000)
            (WORKSPACE / "golden_build_inputs.json").write_text(
                json.dumps(verify_build_inputs(build_inputs, (GOLDEN_APP_NAME,)), indent=2) + "\n",
                encoding="utf-8")

        secondary = client.create_app_component(
            name=CPU1_APP_NAME, platform=str(xpfm), domain=CPU1_DOMAIN_NAME,
            template="empty_application")
        secondary.import_files(from_loc=str(ROOT), files=[
            "firmware/cpu1/main.c", "firmware/common/video_effects.c"],
            is_skip_copy_sources=True)
        add_include_directory(CPU1_APP_NAME, ROOT / "firmware" / "include")
        configure_amp_application(CPU1_APP_NAME, secondary=True)
        require_success(secondary.build(), "CPU1 video worker build")
        select_retained_cpu1(build_inputs)

        application = client.create_app_component(
            name=APP_NAME,
            platform=str(xpfm),
            domain=DOMAIN_NAME,
            template="empty_application",
        )
        application.import_files(
            from_loc=str(ROOT),
            files=[
                "firmware/golden/lib",
                "firmware/common",
                "firmware/include",
                "firmware/platform/xilinx",
                "third_party/CherryUSB/core/usbh_core.c",
                "third_party/CherryUSB/class/hid/usbh_hid.c",
                "third_party/CherryUSB/class/vendor/xbox/usbh_xbox.c",
                "third_party/CherryUSB/port/ehci/usb_hc_ehci.c",
            ] + vectrex32_sources(),
            is_skip_copy_sources=True,
        )
        add_include_directory(APP_NAME, ROOT / "firmware/golden/lib")
        add_include_directory(APP_NAME, ROOT / "firmware" / "include")
        add_include_directory(APP_NAME, ROOT / "firmware/vectrex32")
        add_include_directory(APP_NAME, ROOT / "firmware/vectrex32/compat")
        add_include_directory(APP_NAME, ROOT / "third_party/gsbasic")
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
        configure_vectrex32_cpp(APP_NAME)
        configure_amp_application(APP_NAME, secondary=False)
        binary = embed_cpu1(application)
        require_success(application.build(), "diagnostic application build")
        require(WORKSPACE / APP_NAME / "build" / f"{APP_NAME}.elf",
                "diagnostic ELF")
        verify_embedded_cpu1(binary, build_inputs)
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
