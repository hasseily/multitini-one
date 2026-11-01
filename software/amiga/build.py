#!/usr/bin/env python3
"""Build original guest-only files; never opens hardware or an HDF.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import hashlib
import io
import json
import zipfile
from pathlib import Path
from helper import build, job, MODES, screenmode
from adf import data_disk


INSTALLER = '''; Original Multitini installer. SPDX-License-Identifier: GPL-3.0-only
.KEY
FailAt 21
CD MultitiniScreen:
MakeDir SYS:Prefs/Presets/Multitini >NIL:
Copy MultitiniScreen C:MultitiniScreen
IF WARN
  Echo "Helper copy failed; installation stopped."
  Quit 20
ENDIF
Copy Presets/#? SYS:Prefs/Presets/Multitini ALL
IF WARN
  Echo "Preset copy failed; installation stopped."
  Quit 20
ENDIF
IF EXISTS S:User-Startup
  Search QUIET S:User-Startup "C:MultitiniScreen" >NIL:
  IF NOT WARN
    Skip Installed
  ENDIF
ENDIF
Echo "" >>S:User-Startup
Echo "; Multitini original desktop-resolution helper" >>S:User-Startup
Echo "Run >NIL: <NIL: C:MultitiniScreen" >>S:User-Startup
Lab Installed
Run >NIL: <NIL: C:MultitiniScreen
Echo "Multitini helper installed. The helper starts on the next boot too."
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise SystemExit("Select a new output directory to preserve existing evidence")
    out.mkdir(parents=True)
    r = build()
    (out / "MultitiniScreen").write_bytes(r["hunk"])
    presets = out / "Presets"
    presets.mkdir()
    for n, mode in enumerate(MODES):
        (presets / f"{n}.prefs").write_bytes(screenmode(*mode))
        for save, prefix in ((False, "apply"), (True, "save")):
            (presets / f"{prefix}{n}").write_bytes(job(n, save))
    (out / "Install").write_text(INSTALLER, encoding="ascii", newline="\n")
    readme = ("Original Multitini guest files; no AmigaOS or ROM bytes.\n"
              "Boot your installed Workbench first. Mount this data disk, open\n"
              "an Amiga shell, CD MultitiniScreen: then Execute Install.\n"
              "The installer copies original files and appends one startup line.\n"
              "After upgrading an existing helper, reboot Workbench to start\n"
              "the new version; the running daemon prevents duplicates.\n"
              "Physical acceptance is pending. Preserve a backup of your volume.\n")
    (out / "ReadMe").write_text(readme, encoding="ascii", newline="\n")
    source = Path(__file__).resolve().parent
    source_archive = io.BytesIO()
    with zipfile.ZipFile(source_archive, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in ("m68k.py", "helper.py", "adf.py", "build.py", "README.md", "COPYING"):
            info = zipfile.ZipInfo(name, (2026, 11, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, (source / name).read_bytes())
    (out / "Source.zip").write_bytes(source_archive.getvalue())
    disk_files = {p.relative_to(out).as_posix(): p.read_bytes()
                  for p in out.rglob("*") if p.is_file()}
    (out / "MultitiniScreen.adf").write_bytes(data_disk(disk_files))
    a = r["assembler"]
    (out / "helper.listing").write_text("".join(f"{pc:06x}  {text}\n" for pc, text in a.listing), encoding="ascii", newline="\n")
    source = Path(__file__).resolve().parent
    files = {p.relative_to(out).as_posix(): {"bytes": p.stat().st_size,
             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
             for p in sorted(out.rglob("*"), key=lambda p:p.relative_to(out).as_posix()) if p.is_file()}
    manifest = {"schema": "multitini-original-amiga-helper-v1", "cpu": "68000",
        "minimum_libraries": 37, "mailbox_version": 1, "board_evidence": False,
        "mode_capability_flag": 64,
        "modes": [{"width": w, "height": h, "mode_id": f"0x{mode_id:08X}", "depth": d}
                  for w, h, mode_id, d in MODES],
        "os_bytes_included": False, "files": files,
        "source_sha256": {n: hashlib.sha256((source / n).read_bytes()).hexdigest()
                          for n in ("m68k.py", "helper.py", "adf.py", "build.py", "README.md", "COPYING")},
        "instruction_bytes": r["instruction_end"], "code_data_bytes": len(r["code"]),
        "labels": a.labels}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"output": str(out), "files": len(files), "hunk_bytes": len(r["hunk"]),
                      "hardware_written": False}, indent=2))


if __name__ == "__main__":
    main()
