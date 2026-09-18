#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render production menu cells using the source HDL font and palette.

Requires Pillow and either a GCC/Clang-compatible compiler passed with --cc,
or the CMake-built menu_preview executable passed with --helper. These are
software previews with synthetic media; they are not board captures.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import html
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = {
    "c64-effects": "Video effects and color adjustments",
    "c64-effects-frame": "CPU1 persistence, blur and bloom",
    "gbc-effects-legacy": "Effects unavailable on an older core image",
    "system": "System page (sample readings)",
    "clock": "Board clock editor",
    "text-reader": "Read-only text document",
    "standby": "Standby and core library",
    "c64-media": "Commodore 64 media",
    "c64-options": "Commodore 64 settings",
    "c64-video": "Commodore 64 video and borders",
    "gbc-video-off": "Game Boy Color video, borders disabled",
    "gbc-video-legacy": "Borders unavailable on an older core image",
    "gbc-media": "Game Boy Color: saved",
    "gbc-save-failure": "Game Boy Color: save failure",
    "gbc-input": "Game Boy Color controls",
    "browser-long": "144-file browser, scrolled, with long names",
    "catalog-16": "16-core catalog with synthetic preview fixtures",
    "options-16": "16 settings on one page with synthetic preview fixtures",
}
SOURCES = [
    "tools/menu_preview.c",
    "firmware/common/menu.c",
    "firmware/common/ui.c",
    "firmware/common/core_frontend.c",
    "firmware/common/system_frontend.c",
    "firmware/common/text_reader.c",
    "firmware/common/clock.c",
    "firmware/common/video_effects.c",
    "firmware/common/video_effects_frontend.c",
]
FONT = ROOT / "hdl/common/multitini_font7x8.mem"
OVERLAY = ROOT / "hdl/common/multitini_text_overlay.sv"
SPEC = ROOT / "spec/interface_v0.json"


@dataclass(frozen=True)
class Geometry:
    columns: int
    rows: int
    cell_width: int
    cell_height: int
    font_scale: int
    glyph_x: int
    glyph_y: int

    @property
    def width(self) -> int:
        return self.columns * self.cell_width

    @property
    def height(self) -> int:
        return self.rows * self.cell_height


def read_geometry() -> Geometry:
    constants = json.loads(SPEC.read_text(encoding="utf-8"))["constants"]
    values = {}
    for name in Geometry.__dataclass_fields__:
        value = constants[f"UI_{name.upper()}"]
        values[name] = int(value, 0) if isinstance(value, str) else int(value)
    geometry = Geometry(**values)
    if (min(geometry.columns, geometry.rows, geometry.cell_width,
            geometry.cell_height, geometry.font_scale) <= 0 or
            min(geometry.glyph_x, geometry.glyph_y) < 0 or
            geometry.glyph_x + 7 * geometry.font_scale > geometry.cell_width or
            geometry.glyph_y + 8 * geometry.font_scale > geometry.cell_height):
        raise ValueError("Shared UI geometry does not contain the 7x8 font")
    return geometry


def rgb565(value: int) -> tuple[int, int, int]:
    red, green, blue = (value >> 11) & 31, (value >> 5) & 63, value & 31
    return (red << 3) | (red >> 2), (green << 2) | (green >> 4), (blue << 3) | (blue >> 2)


def read_palette(source: str) -> list[tuple[int, int, int]]:
    entries = re.findall(r"4'h([0-9a-fA-F]):\s*palette\s*=\s*16'h([0-9a-fA-F]{4})", source)
    default = re.search(r"default:\s*palette\s*=\s*16'h([0-9a-fA-F]{4})", source)
    if len(entries) != 15 or default is None:
        raise ValueError("Expected the current 16-color HDL palette")
    values = {int(index, 16): rgb565(int(value, 16)) for index, value in entries}
    values[15] = rgb565(int(default.group(1), 16))
    return [values[index] for index in range(16)]


def verify_geometry(source: str, geometry: Geometry) -> None:
    # Fail rather than quietly producing misleading images after shell changes.
    compact = re.sub(r"\s+", "", source)
    required = [
        "localparam int COLUMNS = MT_UI_COLUMNS;",
        "localparam int ROWS = MT_UI_ROWS;",
        "assign columns = active_dense ? MT_UI_COLUMNS : MT_UI_LEGACY_COLUMNS;",
        "assign cell_width = active_dense ? MT_UI_CELL_WIDTH : MT_UI_LEGACY_CELL_WIDTH;",
        "assign cell_height = active_dense ? MT_UI_CELL_HEIGHT : MT_UI_LEGACY_CELL_HEIGHT;",
        f"parameter int H_ACTIVE = {geometry.width},",
        f"parameter int V_ACTIVE = {geometry.height},",
        "if (fetch_local_x == cell_width - 1)",
        "if (fetch_local_y == cell_height - 1)",
        "if (active_dense) begin "
        "if (local_y_qq >= MT_UI_GLYPH_Y && "
        "local_y_qq < MT_UI_GLYPH_Y + 8 * MT_UI_FONT_SCALE) "
        "glyph_row = glyph_q[(((local_y_qq - MT_UI_GLYPH_Y) / MT_UI_FONT_SCALE) * 8) +: 8]; "
        "if (local_x_qq >= MT_UI_GLYPH_X && "
        "local_x_qq < MT_UI_GLYPH_X + 7 * MT_UI_FONT_SCALE && "
        "local_y_qq >= MT_UI_GLYPH_Y && "
        "local_y_qq < MT_UI_GLYPH_Y + 8 * MT_UI_FONT_SCALE) "
        "glyph_pixel = glyph_row[(local_x_qq - MT_UI_GLYPH_X) / MT_UI_FONT_SCALE];",
    ]
    if any(re.sub(r"\s+", "", expression) not in compact for expression in required):
        raise ValueError("Overlay geometry changed; update the preview rasterizer")


def render(cells: list[int], font: list[int], palette: list[tuple[int, int, int]],
           geometry: Geometry) -> Image.Image:
    result = Image.new("RGB", (geometry.width, geometry.height))
    draw = ImageDraw.Draw(result)
    for index, cell in enumerate(cells):
        x = index % geometry.columns * geometry.cell_width
        y = index // geometry.columns * geometry.cell_height
        if cell & 0x8000:
            raise ValueError("Opaque menu preview unexpectedly contains transparent cells")
        foreground, background = palette[(cell >> 7) & 15], palette[(cell >> 11) & 15]
        draw.rectangle((x, y, x + geometry.cell_width - 1,
                        y + geometry.cell_height - 1), fill=background)
        character = cell & 127
        glyph = font[character - 32] if 32 <= character <= 127 else 0
        for row in range(8):
            bits = (glyph >> (row * 8)) & 255
            for column in range(7):
                if bits & (1 << column):
                    gx = x + geometry.glyph_x + column * geometry.font_scale
                    gy = y + geometry.glyph_y + row * geometry.font_scale
                    draw.rectangle((gx, gy, gx + geometry.font_scale - 1,
                                    gy + geometry.font_scale - 1), fill=foreground)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    compiler = parser.add_mutually_exclusive_group(required=True)
    compiler.add_argument("--cc", type=Path, help="GCC/Clang-compatible C compiler executable")
    compiler.add_argument("--helper", type=Path, help="Already-built menu_preview executable")
    parser.add_argument("--output", type=Path, default=ROOT / "build/ui-preview-dense")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    provenance = SOURCES + [
        "firmware/include/multitini_menu.h", "firmware/include/multitini_ui.h",
          "firmware/include/multitini_core_frontend.h", "scripts/preview_menu.py",
          "firmware/include/multitini_video_effects.h", "firmware/include/multitini_video_effects_frontend.h",
        "firmware/include/multitini_regs.h", "hdl/generated/multitini_regs_pkg.sv",
        str(FONT.relative_to(ROOT)), str(OVERLAY.relative_to(ROOT)),
        str(SPEC.relative_to(ROOT)),
    ]
    source_hashes = {source: hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
                     for source in provenance}
    if args.cc:
        helper = output / ("menu_preview.exe" if os.name == "nt" else "menu_preview")
        compiler_path = args.cc.resolve()
        compiler_env = os.environ.copy()
        compiler_env["PATH"] = str(compiler_path.parent) + os.pathsep + compiler_env.get("PATH", "")
        compile_temp = output / "tmp"
        compile_temp.mkdir(exist_ok=True)
        compiler_env.update(TEMP=str(compile_temp), TMP=str(compile_temp), TMPDIR=str(compile_temp))
        subprocess.run([
            str(compiler_path), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "firmware/include"),
            *(str(ROOT / source) for source in SOURCES), "-lm", "-o", str(helper),
        ], check=True, cwd=ROOT, env=compiler_env, timeout=60)
    else:
        helper = args.helper.resolve()
    overlay = OVERLAY.read_text(encoding="utf-8")
    geometry = read_geometry()
    verify_geometry(overlay, geometry)
    palette = read_palette(overlay)
    font = [int(line, 16) for line in FONT.read_text(encoding="ascii").splitlines() if line.strip()]
    if len(font) != 96:
        raise ValueError("Expected 96 ASCII font glyphs")
    cards = []
    images = {}
    for scenario, title in SCENARIOS.items():
        data = subprocess.check_output([str(helper), scenario], text=True, cwd=ROOT)
        header, payload = data.split("\n", 1)
        if header != f"MTUI1 {geometry.columns} {geometry.rows}":
            raise ValueError(f"Unexpected cell format: {header!r}")
        cells = [int(value, 16) for value in payload.split()]
        if (len(cells) != geometry.columns * geometry.rows or
                any(cell < 0 or cell > 65535 for cell in cells)):
            raise ValueError(f"Invalid cell data for {scenario}")
        (output / f"{scenario}.cells").write_text(data, encoding="ascii")
        text = "\n".join("".join(chr(cell & 127) for cell in
                                  cells[row * geometry.columns:(row + 1) * geometry.columns])
                         for row in range(geometry.rows)) + "\n"
        (output / f"{scenario}.txt").write_text(text, encoding="ascii")
        path = output / f"{scenario}.png"
        render(cells, font, palette, geometry).save(path)
        images[scenario] = hashlib.sha256(path.read_bytes()).hexdigest()
        cards.append(f'<article><h2>{html.escape(title)}</h2><a href="{scenario}.png">'
                     f'<img src="{scenario}.png" alt="{html.escape(title)}" '
                     f'width="{geometry.width}" height="{geometry.height}"></a>'
                     f'<p><a href="{scenario}.txt">Cell text</a> · <a href="{scenario}.cells">Raw cells</a></p></article>')
    if any(hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != digest
           for source, digest in source_hashes.items()):
        raise ValueError("Preview sources changed during rendering; rerun for consistent evidence")
    manifest = {
        "evidence": "Software render of production menu cells; not a board capture.",
        "fixtures": "Media names and extra catalog entries are synthetic preview fixtures.",
        "geometry": dict(asdict(geometry), width=geometry.width,
                         height=geometry.height, glyph_width=7, glyph_height=8),
        "palette_rgb": palette,
        "helper_build": ("Compiled from these unchanged workspace sources during this run."
                         if args.cc else "Provided executable; its build freshness is caller-verified."),
        "workspace_source_sha256": source_hashes,
        "helper_sha256": hashlib.sha256(helper.read_bytes()).hexdigest(),
        "image_sha256": images,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    page = """<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Multitini shared interface previews</title><style>
body{margin:0;background:#080a0d;color:#e8eef2;font:16px system-ui,sans-serif}
main{max-width:1500px;margin:auto;padding:32px}h1{font-size:30px;margin:0 0 12px}
p{color:#a6b3bd;line-height:1.5}a{color:#6b9ede}article{margin:32px 0 48px}
h2{font-size:20px}img{display:block;width:100%;height:auto;border:1px solid #313a43}
</style><main><h1>Multitini shared interface</h1>
<p>Software previews from the production menu renderer and current FPGA font and palette.
These are not board captures. Media names and extra catalog entries are synthetic preview fixtures.</p>
<p>Open an image for its native resolution. <a href="manifest.json">Source and image hashes</a></p>
""" + f"<p>{geometry.columns} columns × {geometry.rows} rows; " \
        f"{geometry.cell_width} × {geometry.cell_height} pixels per cell.</p>\n" \
        + "\n".join(cards) + "</main></html>\n"
    (output / "index.html").write_text(page, encoding="utf-8")
    print(f"Rendered {len(SCENARIOS)} production-menu previews: {output / 'index.html'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Preview failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
