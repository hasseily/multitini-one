#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit and package explicit PHO1 firmware and all machine cores; never flash."""
from pathlib import Path
from datetime import datetime, timezone
from dataclasses import asdict
import argparse
import json
import re
import tempfile
import zipfile

import package_cpu1_video_test as package
import audit_vectrex_package as payload
import image_manifest
from build_phos1 import MATRIX, BUILD_ID

ROOT = package.ROOT
ABI_MINOR = 19


def verify_source_tar(directory, record):
    archive = record.get("archive", {})
    package.require(archive.get("name") == "source-inputs.sources.tar.gz",
                    "Unexpected FPGA source TAR archive name")
    path = directory / archive["name"]
    package.require(path.is_file() and package.sha(path) == archive.get("sha256"),
                    "FPGA source TAR archive missing or changed: " + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hardware", type=Path, required=True)
    parser.add_argument("--supervisor-dir", type=Path, help="Explicit separately rebuilt supervisor")
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--output", type=Path, help="Fresh output directory; default build/firmware-<suffix>")
    parser.add_argument("--build-id", default=BUILD_ID, help="Eight hexadecimal digits identifying all CPU/FPGA images")
    parser.add_argument("--abi-minor", type=int, default=ABI_MINOR)
    parser.add_argument("--suffix", default="phos1", help="Lowercase filename suffix, without a leading hyphen")
    parser.add_argument("--vitis", type=Path, default=Path("C:/Xilinx/2025.2/Vitis"))
    parser.add_argument("--evidence", action="append", type=Path, default=[])
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", args.build_id):
        parser.error("--build-id must contain exactly eight hexadecimal digits")
    if not 0 <= args.abi_minor <= 65535:
        parser.error("--abi-minor must be in the range 0..65535")
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", args.suffix) or len(args.suffix) > 64:
        parser.error("--suffix must be a safe lowercase alphanumeric slug, optionally separated by hyphens")
    args.build_id = args.build_id.upper()
    package.ABI = f"0.{args.abi_minor}"
    output = args.output or ROOT / ("build/firmware-" + args.suffix)
    workspace, out, vitis = args.workspace.resolve(), output.resolve(), args.vitis.resolve()
    package.require(out.is_relative_to(ROOT / "build") and out != ROOT / "build", "Output must stay under build/")
    if not args.check_only:
        package.require(not out.exists(), "Output directory already exists: " + str(out))
        package.require(not out.with_suffix(".zip").exists(), "Output ZIP already exists: " + str(out.with_suffix(".zip")))
    firmware_name = f"FIRMWARE-{args.suffix}.BIN"
    identity = bytes.fromhex(args.build_id).decode("ascii", errors="replace")
    if not re.fullmatch(r"[A-Z0-9]+", identity):
        identity = "0x" + args.build_id
    cpu = package.verify_cpu_images(workspace, build_id=args.build_id, abi_minor=args.abi_minor)
    builds = {}
    for name, (machine, _, pal) in MATRIX.items():
        directory = args.supervisor_dir if name == "supervisor" and args.supervisor_dir else args.hardware / name
        builds[name] = package.verify_hardware(directory.resolve(), machine, build_id=args.build_id, genesis_pal=pal)
        builds[name]["source_tar"] = verify_source_tar(directory.resolve(), builds[name]["record"])
        package.require(builds[name]["ps7_init_sha256"] == cpu["ps7_init_sha256"], "PS7/FSBL configuration mismatch: " + name)
        with zipfile.ZipFile(builds[name]["xsa"]) as archive:
            bits = [entry for entry in archive.namelist() if entry.endswith(".bit")]
            package.require(len(bits) == 1 and package.digest(archive.read(bits[0])) == package.sha(builds[name]["bit"]),
                            "XSA bitstream differs from selected image: " + name)
    required = dict(cpu["record"]["files"])
    for build in builds.values():
        for name, digest in build["record"]["files"].items():
            package.require(name not in required or required[name] == digest, "Source conflict: " + name)
            required[name] = digest
    source = package.source_inventory(required)
    for path in args.evidence:
        package.require(path.is_file(), "Missing evidence: " + str(path))
    if args.check_only:
        print(json.dumps({"status": "PASS", "abi": package.ABI, "matrix": {n: b["timing"] for n, b in builds.items()}}, indent=2))
        return 0
    out.mkdir(parents=True, exist_ok=False)
    for name, path in cpu["paths"].items():
        package.copy(path, out / "inputs" / {"cpu0_elf": "diagnostic.elf", "cpu1_elf": "video_worker.elf",
                                             "cpu1_binary": "video_core1_blob.bin"}[name])
    package.copy(cpu["fsbl"], out / "inputs/fsbl.elf")
    package.copy(cpu["xsa"], out / "inputs/platform.xsa")
    package.copy(workspace / "cpu1_manifest.json", out / "evidence/vitis/cpu1_manifest.json")
    for app in ("diagnostic", "video_worker"):
        for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
            package.copy(workspace / app / "src" / name, out / "evidence/vitis" / app / name)
        package.copy(workspace / app / "build/compile_commands.json", out / "evidence/vitis" / app / "compile_commands.json")
    for name, build in builds.items():
        package.copy(build["bit"], out / "inputs" / (name + ".bit"))
        package.copy(Path(str(build["bit"]) + ".kind"), out / "inputs" / (name + ".bit.kind"))
        package.copy(build["xsa"], out / "inputs" / (name + ".xsa"))
        directory = build["bit"].parents[2]
        package.copy(directory / "source-inputs.json", out / "evidence" / name / "source-inputs.json")
        source_tar = out / "evidence" / name / build["source_tar"].name
        package.copy(build["source_tar"], source_tar)
        package.require(package.sha(source_tar) == build["record"]["archive"]["sha256"],
                        "Packaged FPGA source TAR archive changed: " + name)
        for path in build["artifacts"]:
            if path not in (build["bit"], Path(str(build["bit"]) + ".kind"), build["xsa"]):
                package.copy(path, out / "evidence" / name / path.relative_to(directory))
    for i, path in enumerate(args.evidence):
        package.copy(path, out / "evidence/validation" / f"{i:02d}-{path.name}")
    package.archive_files(out / "source.zip", {name: ROOT / name for name in source})
    package.write_json(out / "source-manifest.json", source)
    package.archive_files(out / "inputs/used-vitis-platform.zip",
        {"multitini_platform/" + name: cpu["platform"] / name for name in cpu["record"]["platform_files"]})
    package.write_json(out / "evidence/vitis/platform-manifest.json", cpu["record"]["platform_files"])
    bootgen = vitis / "bin/bootgen.bat"
    (out / "sd").mkdir()
    package.run([ROOT / "scripts/make_firmware_bin.bat", out / firmware_name, out / "inputs/fsbl.elf",
                 out / "inputs/supervisor.bit", out / "inputs/diagnostic.elf"], vitis=vitis)
    package.copy(out / firmware_name, out / "sd/FIRMWARE.BIN")
    core_specs = (("c64", "Commodore 64", "0x43363420", [("pal", "PAL", "c64-pal")]),
                  ("gbc", "Game Boy Color", "0x47424320", [("native", "Game Boy / Game Boy Color", "gbc")]),
                  ("vectrex", "Vectrex", "0x56454358", [("native", "Vectrex / Vectrex32", "vectrex")]),
                  ("genesis", "Genesis / Mega Drive", "0x4D442020", [("ntsc", "NTSC", "genesis-ntsc"), ("pal", "PAL", "genesis-pal")]))
    cores = {}
    with tempfile.TemporaryDirectory(prefix=args.suffix + "-audit-", dir=ROOT / "build") as temporary:
        scratch = Path(temporary)
        objcopy = vitis / "gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy.exe"
        package.run([objcopy, "-O", "binary", out / "inputs/video_worker.elf", scratch / "worker.bin"])
        package.require((scratch / "worker.bin").read_bytes() == (out / "inputs/video_core1_blob.bin").read_bytes(), "CPU1 raw image mismatch")
        for ident, label, core_id, variants in core_specs:
            directory = out / "sd/multitini/cores" / ident
            content_root = "0:/multitini/cores/" + ident + "/media"
            command = [package.sys.executable, ROOT / "scripts/make_core_package.py", "--id", ident, "--name", label,
                       "--core-id", core_id, "--abi-major", "0", "--abi-minor", str(args.abi_minor),
                       "--content-root", content_root, "--output", directory, "--bootgen", bootgen]
            for variant, display, build_name in variants:
                command += ["--variant", f"{variant}|{display}|{out / 'inputs' / (build_name + '.bit')}"]
            package.run(command, vitis=vitis)
            cores[ident] = package.verify_core(directory, {v[0] for v in variants})
            for variant, _, build_name in variants:
                processed, _ = payload.process_bit(bootgen, out / "inputs" / (build_name + ".bit"), scratch, build_name)
                package.require(package.digest(processed) == cores[ident][variant]["sha256"], "SD payload differs from selected bitstream")
        firmware = out / firmware_name
        update_manifest = image_manifest.verify_image(firmware,
            expected_role=image_manifest.ROLE_FIRMWARE, expected_recovery=True)
        package.require(firmware.stat().st_size <= 0x00DF0000, "Firmware exceeds the active updater slot")
        raw_firmware = scratch / "firmware.raw.bin"
        raw_firmware.write_bytes(firmware.read_bytes()[:update_manifest.payload_size])
        images = payload.parse_firmware(raw_firmware.read_bytes())
        audit = {"status": "PASS", "fsbl": payload.verify_elf(images[0], out / "inputs/fsbl.elf"),
                 "cpu0": payload.verify_elf(images[2], out / "inputs/diagnostic.elf"), "cores": cores,
                 "cpu1_embedded_sha256": package.sha(out / "inputs/video_core1_blob.bin"),
                 "update_manifest": asdict(update_manifest), "raw_bootgen_sha256": package.sha(raw_firmware)}
        processed, log = payload.process_bit(bootgen, out / "inputs/supervisor.bit", scratch, "supervisor")
        package.require(images[1][0]["payload"] == processed and images[1][0]["attributes"] == 0x20 and
                        images[1][0]["load_address"] == images[1][0]["entry_address"] == 0, "Supervisor partition mismatch")
        audit.update(supervisor_processed_sha256=package.digest(processed), bootgen_process_output=log,
                     bootgen_readback=payload.bootgen(bootgen, ["-read", raw_firmware], scratch))
    package.write_json(out / "evidence/payload-audit.json", audit)
    package.write_json(out / "evidence/timing-audit.json", {n: b["timing"] for n, b in builds.items()})
    (out / "README.txt").write_text(
        f"Multitini {identity} / -{args.suffix} / ABI {package.ABI}\n\n"
        "Copy sd/ contents to the SD card, preserving media and settings.\n"
        f"{firmware_name} is identical to sd/FIRMWARE.BIN, the updater's required filename.\n"
        "Use the existing golden updater, then reload cores. No flash was performed here.\n"
        "All cores are rebuilt: C64 PAL, PSRAM GBC, Vectrex, Genesis NTSC/PAL.\n"
        "Appletini F1.2.5-derived Dot bleed, Blur and Glow replace independent axis smoothing.\n"
        + ("Blur uses a 12.5%, 25%, 50% kernel mix and preserves flat colours exactly. Glow is unchanged.\n"
           if args.build_id in ("50484F32", "50484F33", "50484F34", "50484F35") else "") +
        ("Per-core Video size: 1x/2x/3x/4x/Auto; centered oversize images clip.\n"
         "Auto retains current sizing; native source frames remain complete.\n"
           if args.build_id in ("50484F33", "50484F34", "50484F35") else "") +
        ("PHO4 centers the standby artwork in all six output modes.\n"
         "Opaque menus cover every output pixel, including partial cell strips.\n"
         "Screen screenshots use the same edge-cell background/transparency policy.\n"
           if args.build_id == "50484F34" else "") +
        ("PHO5 centers the visible compact MULTITINI/ONE/F12 block; 1080p is unchanged.\n"
         "Video size commits geometry without reprogramming an unchanged pixel clock.\n"
         "Effects-Off status and Genesis transfer phases are explicit.\n"
         "Genesis runtime diagnosis remains open; this package does not establish a cartridge-load fix.\n"
           if args.build_id == "50484F35" else "") +
        "Controls retain Off/Light/Medium/Strong; other picture controls are preserved.\n"
        "The FPGA applies frames in order; CPU1 prepares color tables.\n"
        "source.zip and evidence/ contain exact source inputs, tool settings, timing/CDC,\n"
        "compile records and independent firmware/core payload audits. No user media is supplied.\n"
        "Vivado/Vitis 2025.2; board performance and gameplay acceptance remain pending.\n", encoding="utf-8")
    package.verify_sources(source)
    package.verify_cpu_images(workspace, build_id=args.build_id, abi_minor=args.abi_minor)
    for name, build in builds.items():
        package.require(package.sha(build["bit"]) == package.sha(out / "inputs" / (name + ".bit")), "Bitstream changed during packaging")
    files = {p.relative_to(out).as_posix(): package.describe(p) for p in sorted(out.rglob("*")) if p.is_file()}
    package.write_json(out / "manifest.json", {"classification": "Local test; board acceptance pending",
        "created_utc": datetime.now(timezone.utc).isoformat(), "firmware_id": "0x" + args.build_id, "suffix": "-" + args.suffix,
        "abi": package.ABI, "git_head": package.git("rev-parse", "HEAD").strip(), "dirty_source_snapshot": True,
        "source_files": len(source), "files": files})
    files["manifest.json"] = package.describe(out / "manifest.json")
    (out / "SHA256SUMS.txt").write_text("".join(f"{v['sha256']}  {n}\n" for n, v in sorted(files.items())), encoding="ascii")
    package.archive_files(out.with_suffix(".zip"), {p.relative_to(out).as_posix(): p for p in out.rglob("*") if p.is_file()})
    print(json.dumps({"status": "PASS", "firmware": package.describe(out / firmware_name),
                      "bundle": package.describe(out.with_suffix(".zip")), "output": str(out)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
