#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build and package only Golden ARM code on an explicit retained platform.

The selected multiboot bundle supplies the exact FSBL and hardware platform.
Appletini, Multitini, FPGA and CPU1 images are verified and never rebuilt.
This helper does not flash hardware. --check-only accepts the directory or ZIP.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))

import image_manifest
import package_cpu1_video_test as p
import package_firmware_release as release
import package_multiboot_bundle as bundle

ROOT = p.ROOT
SCHEMA = "multitini-golden-only-release-v1"
SUPPORT_SOURCES = (*bundle.SUPPORT_SOURCES, "scripts/build_golden_release.py",
                   "scripts/create_vitis_workspace.py", "scripts/capture_vectrex_build_inputs.py")


def source_records():
    paths = {ROOT / name for name in SUPPORT_SOURCES}
    paths.update((ROOT / "LICENSE", ROOT / "MULTITINI_ONE_PLAN.md"))
    paths.update(path for name in ("licenses", "provenance")
                 for path in (ROOT / name).rglob("*") if path.is_file())
    return {path.relative_to(ROOT).as_posix(): p.sha(path) for path in sorted(paths)}


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def version():
    text = (ROOT / "firmware/golden/image_versions.h").read_text(encoding="utf-8")
    match = re.search(r'#define\s+APPLETINI_BOOT_IMAGE_VERSION_SHORT\s+"(B\d+\.\d+\.\d+)"', text)
    p.require(match is not None, "Golden source lacks a release version")
    return match[1]


def retained_inputs(selected):
    """Record every retained firmware, FPGA, CPU1 and platform input by hash."""
    files = {"manifest.json": p.describe(selected / "manifest.json")}
    for product in ("appletini", "multitini"):
        _, _, firmware = bundle.verify_component(selected / product, product)
        files[firmware.relative_to(selected).as_posix()] = p.describe(firmware)
        for path in sorted((selected / product / "inputs").rglob("*")):
            if path.is_file():
                files[path.relative_to(selected).as_posix()] = p.describe(path)
    return files


def audit(directory):
    result = bundle.audit_golden(directory)
    result["fsbl"]["path"] = "inputs/fsbl.elf"
    result["golden"]["path"] = "inputs/golden.elf"
    return result


def build_in_vitis():
    # Vitis supplies its Python API only inside its own script launcher.
    import create_vitis_workspace as build
    import vitis
    workspace = Path(os.environ["MULTITINI_VITIS_WORKSPACE"]).resolve()
    xpfm = Path(os.environ["MULTITINI_PLATFORM_XPFM"]).resolve()
    build.ensure_lopper_preprocessor()
    build.validate_reused_xilffs(xpfm)
    build.validate_reused_cpu1(xpfm)
    record = build.capture_build_inputs(xpfm)
    # Golden links its own tree and only this shared version header. Keep the
    # corresponding source archive focused while retaining every build helper
    # and source/license record needed to reproduce the selected executable.
    record["files"] = {name: digest for name, digest in record["files"].items()
                       if name.startswith("firmware/golden/") or
                       name == "firmware/include/multitini_version.h"}
    record["files"].update(source_records())
    client = vitis.create_client()
    try:
        client.set_workspace(path=str(workspace))
        p.write_json(workspace / "source-before.json", record["files"])
        client.add_platform_repos(platform=str(xpfm.parent))
        app = client.create_app_component(name="golden", platform=str(xpfm),
                                         domain=build.DOMAIN_NAME,
                                         template="empty_application")
        app.import_files(from_loc=str(ROOT), files=["firmware/golden"],
                         is_skip_copy_sources=True)
        build.add_include_directory("golden", ROOT / "firmware/golden/lib")
        build.configure_amp_application("golden", secondary=False)
        build.require_success(app.build(), "Golden-only application build")
        elf = workspace / "golden/build/golden.elf"
        build.validate_elf_memory(elf, 0x100000, build.CPU1_ENTRY - 0x100000)
        record = build.verify_build_inputs(record, ("golden",))
        p.write_json(workspace / "source-after.json",
                     {name: p.sha(ROOT / name) for name in record["files"]})
        record["golden_memory"] = build.validate_golden_stack(elf)
        p.write_json(workspace / "golden_build_inputs.json", record)
    finally:
        vitis.dispose()


def verify_directory(directory, compare_zip=False):
    manifest = release.verify_directory(directory)
    p.require(manifest.get("schema") == SCHEMA and
              manifest["flash_performed"] is False and
              manifest["board_validation"] is False and
              manifest["firmware_rebuilt"] is False and
              manifest["fpga_rebuilt"] is False and
              manifest["cpu1_rebuilt"] is False,
              "Unsupported or overstated Golden-only release")
    update = image_manifest.verify_image(directory / "BOOT.BIN", expected_role=1,
                                         expected_recovery=True)
    p.require(asdict(update) == manifest["golden_identity"], "Golden identity differs")
    p.require((directory / "sd-root/BOOT.BIN").read_bytes() ==
              (directory / "BOOT.BIN").read_bytes(), "SD Golden alias differs")
    sources = load(directory / "source-manifest.json")
    bundle.verify_archive(directory / "source.zip", sources)
    record = load(directory / "build-records/build_inputs.json")
    p.require(load(directory / "build-records/source-before.json") == record["files"] ==
              load(directory / "build-records/source-after.json"),
              "Golden source changed during the build")
    selected = load(directory / "build-records/selected-inputs.json")
    p.require(all(sources.get(name) == digest for name, digest in record["files"].items()),
              "Source archive differs from compiler inputs")
    bundle.verify_compilation(record, directory / "build-records/compile_commands.json",
                              sources, selected["source_root"])
    memory = record["golden_memory"]
    p.require(memory["stack_bytes"] >= 0x10000 and
              memory["symbols"]["_stack"] - memory["symbols"]["_stack_end"] ==
              memory["symbols"]["_STACK_SIZE"] == memory["stack_bytes"],
              "Golden stack gate is missing or failed")
    p.require(load(directory / "platform-manifest.json") == record["platform_files"],
              "Platform archive differs from build inputs")
    bundle.verify_archive(directory / "platform.zip", record["platform_files"])
    for name, item in (("fsbl.elf", "fsbl"), ("golden.elf", "golden_elf")):
        p.require(p.describe(directory / "inputs" / name) == selected[item]["artifact"],
                  "Selected Golden input differs: " + name)
    p.require(selected["packaging_script_sha256"] == sources["scripts/make_boot_bin.bat"],
              "Selected packaging source differs")
    with zipfile.ZipFile(directory / "platform.zip") as archive:
        p.require(p.digest(archive.read("sw/boot/fsbl.elf")) ==
                  p.sha(directory / "inputs/fsbl.elf"), "Platform FSBL differs")
    result = audit(directory)
    p.require(result == load(directory / "payload-audit.json"), "Golden payload audit differs")
    images = bundle.parse_golden((directory / "BOOT.BIN").read_bytes()[:update.payload_size])
    label = ("Multiboot " + manifest["golden_version"]).encode("ascii")
    p.require(any(label in part["payload"] for part in images[1]), "Golden release version differs")
    p.require(load(directory / "retained-inputs.json") == manifest["retained_inputs"],
              "Retained-input receipt differs")
    if compare_zip:
        verify_zip(Path(str(directory) + ".zip"), directory)
    return {"status": "PASS", "golden_version": manifest["golden_version"],
            "golden": p.describe(directory / "BOOT.BIN"),
            "flash_performed": False, "board_validation": False}


def verify_zip(path, directory=None):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        p.require(len(names) == len(set(names)) and archive.testzip() is None,
                  "Corrupt or duplicated Golden ZIP")
        for name in names:
            release.safe(name)
            p.require(not name.endswith("/"), "Unexpected ZIP directory member")
        if directory is not None:
            files = release.inventory(directory)
            p.require(set(names) == set(files) and
                      all(p.digest(archive.read(name)) == item["sha256"]
                          for name, item in files.items()), "Golden ZIP differs from directory")
            return
        with tempfile.TemporaryDirectory(prefix="golden-release-check-") as temporary:
            target = Path(temporary)
            for name in names:
                dest = target / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(archive.read(name))
            return verify_directory(target)


def create(selected, workspace, out, vitis):
    bundle.validate_output(out, (selected, workspace))
    build_root = (ROOT / "build").resolve()
    p.require(workspace.is_relative_to(build_root) and workspace != build_root and
              not workspace.exists() and not workspace.is_relative_to(selected) and
              not selected.is_relative_to(workspace), "Golden workspace must be fresh and isolated")
    bundle.verify_directory(selected)
    retained = retained_inputs(selected)
    previous = load(selected / "golden/build-records/build_inputs.json")
    platform = Path(previous["platform_path"]).resolve()
    xpfm = platform / "multitini_platform.xpfm"
    p.require(platform.is_relative_to(build_root) and xpfm.is_file(),
              "Selected retained platform is absent or outside this checkout")
    p.require({path.relative_to(platform).as_posix(): p.sha(path)
               for path in platform.rglob("*") if path.is_file()} == previous["platform_files"],
              "Retained exported platform changed")
    xsa = selected / "multitini/inputs/platform.xsa"
    p.require(p.sha(xsa) == previous["xsa"]["sha256"], "Selected retained XSA differs")
    fsbl = selected / "multitini/inputs/fsbl.elf"
    p.require(p.sha(fsbl) == p.sha(platform / "sw/boot/fsbl.elf"), "Selected retained FSBL differs")
    p.require((vitis / "bin/vitis.bat").is_file() and (vitis / "bin/bootgen.bat").is_file(),
              "Explicit Vitis tools are missing")
    environment = dict(os.environ)
    environment.update({"XILINX_VITIS": str(vitis), "MULTITINI_GOLDEN_BUILD_ONLY": "1",
                        "MULTITINI_VITIS_WORKSPACE": str(workspace),
                        "MULTITINI_PLATFORM_XPFM": str(xpfm), "MULTITINI_XSA": str(xsa)})
    environment.pop("MULTITINI_CPU1_BASELINE", None)
    environment.pop("MULTITINI_SKIP_GOLDEN", None)
    temp = workspace.parent / (workspace.name + "-temp")
    temp.mkdir(parents=True, exist_ok=True)
    environment.update({"TEMP": str(temp), "TMP": str(temp)})
    log = Path(str(workspace) + ".log")
    with log.open("w", encoding="utf-8") as stream:
        subprocess.run(p.GUARD + [str(vitis / "bin/vitis.bat"), "-s", str(Path(__file__).resolve())],
                       cwd=ROOT, env=environment, stdout=stream, stderr=subprocess.STDOUT, check=True)
    record, platform, elf = bundle.verify_golden_build(workspace, selected / "multitini")
    out.mkdir(parents=True)
    p.copy(fsbl, out / "inputs/fsbl.elf")
    p.copy(elf, out / "inputs/golden.elf")
    p.copy(log, out / "build-records/vitis.log")
    p.copy(workspace / "golden_build_inputs.json", out / "build-records/build_inputs.json")
    p.copy(workspace / "source-before.json", out / "build-records/source-before.json")
    p.copy(workspace / "source-after.json", out / "build-records/source-after.json")
    p.copy(workspace / "golden/build/compile_commands.json", out / "build-records/compile_commands.json")
    for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
        p.copy(workspace / "golden/src" / name, out / "build-records" / name)
    sources = {**record["files"], **{name: p.sha(ROOT / name) for name in SUPPORT_SOURCES}}
    p.archive_files(out / "source.zip", {name: ROOT / name for name in sources})
    p.write_json(out / "source-manifest.json", sources)
    p.archive_files(out / "platform.zip", {name: platform / name for name in record["platform_files"]})
    p.write_json(out / "platform-manifest.json", record["platform_files"])
    p.write_json(out / "build-records/selected-inputs.json", {
        "source_root": str(ROOT), "workspace": str(workspace), "vitis": str(vitis),
        "fsbl": {"selected_path": str(fsbl), "artifact": p.describe(fsbl)},
        "golden_elf": {"selected_path": str(elf), "artifact": p.describe(elf)},
        "packaging_script_sha256": p.sha(ROOT / "scripts/make_boot_bin.bat"),
    })
    p.run([ROOT / "scripts/make_boot_bin.bat", out / "BOOT.BIN",
           out / "inputs/fsbl.elf", out / "inputs/golden.elf"], vitis=vitis)
    p.copy(out / "BOOT.BIN", out / "sd-root/BOOT.BIN")
    p.write_json(out / "payload-audit.json", audit(out))
    p.write_json(out / "retained-inputs.json", retained)
    golden_version = version()
    (out / "README.txt").write_text(
        f"Golden-only {golden_version}; board validation is pending.\n\n"
        "Copy sd-root/BOOT.BIN to the SD root and use Update Golden Boot...\n"
        "from Appletini's initial About menu or Multitini's System menu.\n"
        "After primary repair, the matching BOOT.BIN is acknowledged as BOOT.OK.\n\n"
        "Only Golden ARM code was built. The exact selected FSBL is retained.\n"
        "Appletini, Multitini, FPGA and CPU1 images were verified and retained.\n"
        "No flash operation or board test was performed.\n\n"
        "Install this Golden using the current firmware menu before restoring\n"
        "an older Appletini. Original Appletini firmware archives are not included.\n"
        "Restore an original Appletini F>=1.1.0\n"
        "image by placing it at SD root as FIRMWARE.BIN or FIRMWARE-APPLETINI.BIN.\n"
        "Golden verifies and installs it in the Appletini slot, then requests\n"
        "Appletini for that boot. The saved startup choice, Multitini and user\n"
        "SD data are preserved.\n"
        "Keep only the intended Appletini install file on SD: successful files\n"
        "become .OK and multiple Appletini files are processed in scan order.\n"
        "Older Appletini menus cannot switch products. Enter Golden's UART\n"
        "recovery window at boot and use boot appletini or boot multitini to\n"
        "select either installed firmware for that boot, or install a\n"
        "multiboot-capable Appletini again.\n\n"
        "Validate directory or ZIP with build_golden_release.py --check-only --output PATH.\n",
        encoding="utf-8")
    bundle.verify_directory(selected)
    p.require(retained_inputs(selected) == retained, "Retained F/M/hardware inputs changed")
    bundle.verify_golden_build(workspace, selected / "multitini")
    p.require(p.describe(elf) == p.describe(out / "inputs/golden.elf"), "Golden ELF changed")
    p.write_json(out / "manifest.json", {
        "schema": SCHEMA, "created_utc": datetime.now(timezone.utc).isoformat(),
        "golden_version": golden_version, "selected_bundle": str(selected),
        "retained_inputs": retained, "classification": "Board validation pending",
        "golden_identity": asdict(image_manifest.verify_image(out / "BOOT.BIN")),
        "flash_performed": False, "board_validation": False, "firmware_rebuilt": False,
        "fpga_rebuilt": False, "cpu1_rebuilt": False, "files": release.inventory(out),
    })
    files = release.inventory(out)
    (out / "SHA256SUMS.txt").write_text(
        "".join(f"{item['sha256']}  {name}\n" for name, item in files.items()), encoding="ascii")
    verify_directory(out)
    p.archive_files(Path(str(out) + ".zip"), {name: out / name for name in release.inventory(out)})
    return verify_directory(out, compare_zip=True)


def main():
    if os.environ.get("MULTITINI_GOLDEN_BUILD_ONLY") == "1":
        build_in_vitis()
        return
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retained-bundle", type=Path)
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vitis", type=Path, default=Path("C:/Xilinx/2025.2/Vitis"))
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    if args.check_only:
        result = verify_zip(out) if out.is_file() else verify_directory(out)
    else:
        p.require(args.retained_bundle is not None and args.workspace is not None,
                  "Building requires an explicit retained bundle and fresh workspace")
        result = create(args.retained_bundle.resolve(), args.workspace.resolve(), out, args.vitis.resolve())
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
