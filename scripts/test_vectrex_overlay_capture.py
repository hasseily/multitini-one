#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare actual compositor RTL against a reference and render local artwork.

External OVR and native 540x720 intensity captures are explicit local inputs.
Generated captures are simulator evidence, never physical display evidence.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RTL = ROOT / 'hdl/core/vectrex/multitini_vectrex_compositor.sv'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected(intensity, rgba, gain, overlay_brightness):
    beam = min(255, intensity * gain)
    white = max(0, beam * 2 - 255)
    channels = [(rgba >> shift & 15) * 17 for shift in (0, 4, 8)]
    alpha = rgba >> 12
    if channels == [0, 0, 0]:
        channels, alpha = [255] * 3, 0
    result = [min(255, (c * alpha * (overlay_brightness // 25) >> 6) +
                  (255 if beam == 255 else (beam * c + white * (255 - c)) >> 8))
              for c in channels]
    return (result[0] >> 3 << 11) | (result[1] >> 2 << 5) | (result[2] >> 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--intensity', required=True, type=Path)
    parser.add_argument('--overlay', required=True, type=Path)
    parser.add_argument('--overlay-brightness', type=int, choices=(25, 50, 75, 100), default=50)
    parser.add_argument('--gain', type=int, choices=(1, 2, 3, 4), action='append',
                        help='Beam multiplier; repeat for several captures (default: 1, 2, 4)')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/vectrex-overlay-captures')
    args = parser.parse_args()
    directory = args.output.resolve()
    if not directory.is_relative_to(ROOT / 'build'):
        parser.error('Generated captures must stay under build/')
    directory.mkdir(parents=True, exist_ok=True)
    native = Image.open(args.intensity).convert('L')
    if native.size != (540, 720):
        parser.error('Intensity image must be 540x720')
    raw = args.overlay.read_bytes()
    if not raw or len(raw) > 777600 or len(raw) % 2:
        parser.error('Invalid OVR size')
    raw += bytes(777600 - len(raw))
    levels = list(native.getdata())
    artwork = [raw[i] | raw[i + 1] << 8 for i in range(0, len(raw), 2)]
    (directory / 'intensity.mem').write_text(''.join(f'{v:02x}\n' for v in levels))
    (directory / 'artwork.mem').write_text(''.join(f'{v:04x}\n' for v in artwork))
    bench = directory / 'capture.sv'
    bench.write_text('''`timescale 1ns/1ps
module capture;
  reg clk=0,resetn=0,visible=0;
  reg [7:0] intensity=0; reg [15:0] rgba=0;
  reg [4:0] config_word=0; wire [15:0] rgb;
  reg [7:0] beam[0:388799]; reg [15:0] art[0:388799];
  integer i,gain,opacity,file; reg [1023:0] filename;
  always #3.367 clk=!clk;
  multitini_vectrex_compositor dut(.clk(clk),.resetn(resetn),.visible(visible),
    .intensity(intensity),.rgba(rgba),.artwork_valid(1'b1),
    .video_config(config_word),.rgb565(rgb));
  initial begin
    $readmemh("intensity.mem",beam); $readmemh("artwork.mem",art);
    if (!$value$plusargs("GAIN=%d",gain)) $fatal(1,"Missing gain");
    if (!$value$plusargs("OPACITY=%d",opacity)) $fatal(1,"Missing opacity");
    config_word=1+((gain-1)<<1)+((opacity-1)<<3);
    $sformat(filename,"gain-%0d.rgb565",gain); file=$fopen(filename,"w");
    repeat(4) @(negedge clk); resetn=1;
    for(i=0;i<388803;i=i+1) begin
      @(negedge clk); visible=i<388800;
      intensity=i<388800 ? beam[i] : 0; rgba=i<388800 ? art[i] : 0;
      @(posedge clk); #1;
      if(i>=3) $fdisplay(file,"%04x",rgb);
    end
    $fclose(file); $finish;
  end
endmodule
''')
    runtime = Path(os.environ.get('MULTITINI_UCRT_BIN', 'C:/msys64/ucrt64/bin'))
    compiler = str(runtime / 'iverilog.exe') if os.name == 'nt' else 'iverilog'
    runner = str(runtime / 'vvp.exe') if os.name == 'nt' else 'vvp'
    guard = [sys.executable, str(ROOT / 'scripts/run_native_tool.py'), '--']
    binary = directory / 'capture.vvp'
    subprocess.run(guard + [compiler, '-g2012', '-s', 'capture', '-o', str(binary),
                           str(RTL), str(bench)], cwd=directory, check=True)
    captures = []
    for gain in args.gain or (1, 2, 4):
        subprocess.run(guard + [runner, str(binary), f'+GAIN={gain}',
                               f'+OPACITY={args.overlay_brightness // 25}'], cwd=directory, check=True)
        values = [int(line, 16) for line in (directory / f'gain-{gain}.rgb565').read_text().splitlines()]
        if len(values) != 388800:
            raise AssertionError('Incomplete compositor capture')
        pixels = []
        for index, (level, art, actual) in enumerate(zip(levels, artwork, values)):
            wanted = expected(level, art, gain, args.overlay_brightness)
            if actual != wanted:
                raise AssertionError(f'Gain {gain}, pixel {index}: {actual:04x} != {wanted:04x}')
            pixels.append(((actual >> 11) * 255 // 31,
                           (actual >> 5 & 63) * 255 // 63, (actual & 31) * 255 // 31))
        output = directory / f'gain-{gain}.png'
        picture = Image.new('RGB', (540, 720)); picture.putdata(pixels); picture.save(output)
        captures.append({'gain': gain, 'overlay_brightness': args.overlay_brightness,
                         'path': output.name, 'sha256': digest(output), 'checked_pixels': len(values)})
    record = {'evidence': 'RTL compositor with explicitly supplied local intensity/artwork; not a board capture',
              'inputs': {str(path.resolve()): digest(path) for path in (RTL, args.intensity, args.overlay)},
              'captures': captures}
    (directory / 'report.json').write_text(json.dumps(record, indent=2) + '\n')
    print(f'PASS: {sum(c["checked_pixels"] for c in captures):,} actual RTL compositor pixels; '
          f'overlay {args.overlay_brightness}%, beam ' + ', '.join(f'{c["gain"]}x' for c in captures))


if __name__ == '__main__':
    main()
