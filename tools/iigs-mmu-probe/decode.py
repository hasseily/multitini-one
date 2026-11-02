# SPDX-License-Identifier: GPL-3.0-only
"""Decode an MMU1 screenshot, photograph, raw packet, or 32,000-byte SHR buffer."""
from __future__ import annotations
import argparse
import csv
import json
from pathlib import Path
from protocol import *

def homography(from_points, to_points):
    """Return the eight coefficients of a projective map, using four point pairs."""
    matrix = []
    for (x,y),(u,v) in zip(from_points,to_points,strict=True):
        matrix.append([x,y,1,0,0,0,-u*x,-u*y,u])
        matrix.append([0,0,0,x,y,1,-v*x,-v*y,v])
    for col in range(8):
        pivot = max(range(col,8),key=lambda row:abs(matrix[row][col]))
        matrix[col],matrix[pivot] = matrix[pivot],matrix[col]
        scale = matrix[col][col]
        if abs(scale) < 1e-12:
            raise ValueError("photo corners do not define a usable quadrilateral")
        matrix[col] = [value/scale for value in matrix[col]]
        for row in range(8):
            if row != col:
                scale = matrix[row][col]
                matrix[row] = [a-scale*b for a,b in zip(matrix[row],matrix[col])]
    return tuple(matrix[row][8] for row in range(8))

def image_rows(image, crop=None, corners=None, code_crop=None):
    from PIL import Image
    image = image.convert("L")
    if code_crop:
        # Fit only the fixed sync bars and encoded row numbers, never MMU data.
        # Pixel centers avoid mixing adjacent cells at a fractional window scale.
        px = image.load()
        low, high = image.getextrema()
        threshold = (low+high)/2
        known = [(bit, bool(byte & (128 >> (bit % 8))))
                 for bit in range(16) for byte in [b"\xa5\x5a"[bit//8]]]
        known += [(312+i, bool(0xa5 & (128 >> i))) for i in range(8)]
        def sample(box, row, column):
            x,y,w,h = box
            xx = min(image.width-1,max(0,int(x+(column+.5)*w/320)))
            yy = min(image.height-1,max(0,int(y+(row+.5)*h/96)))
            return px[xx,yy] > threshold
        def score(box):
            errors = 0
            for row in range(96):
                errors += sum(sample(box,row,column) != expected for column,expected in known)
                errors += sum(sample(box,row,16+i) != bool(row & (128 >> i)) for i in range(8))
            return errors
        box = list(map(float,code_crop))
        for step, radius in ((.5,4),(.25,1.5)):
            for dimension in range(4):
                choices = []
                for i in range(-int(radius/step),int(radius/step)+1):
                    candidate = box.copy(); candidate[dimension] += i*step
                    choices.append((score(candidate),abs(i),candidate))
                box = min(choices,key=lambda v:(v[0],v[1]))[2]
        return [bytes(sum(sample(box,row,i*8+k) << (7-k) for k in range(8))
                      for i in range(40)) for row in range(96)]
    if crop:
        x, y, width, height = crop
        image = image.crop((x, y, x+width, y+height))
    if corners:
        points = [tuple(corners[i:i+2]) for i in range(0, 8, 2)]
        coefficients = homography(((0,0),(640,0),(640,200),(0,200)),points)
        image = image.transform((640, 200), Image.Transform.PERSPECTIVE,
                                coefficients, Image.Resampling.BILINEAR)
    else:
        image = image.resize((640, 200), Image.Resampling.BOX)
    pixels = image.load()
    samples = [[sum(pixels[x*2+dx, 8+y*2+dy] for dx in range(2) for dy in range(2))/4
                for x in range(320)] for y in range(96)]
    ordered = sorted(v for row in samples for v in row)
    threshold = (ordered[len(ordered)//10]+ordered[9*len(ordered)//10])/2
    return [bytes(sum((row[i*8+k] > threshold) << (7-k) for k in range(8))
                  for i in range(40)) for row in samples]

def shr_rows(data):
    if len(data) != 32000:
        raise ValueError("SHR buffer must be 32000 bytes")
    output = []
    for row in range(96):
        bits = []
        for offset in range(160):
            byte = data[(8+row*2)*160+offset]
            bits.extend(((byte >> 4) == 15, (byte & 15) == 15))
        output.append(bytes(sum(bits[i*8+k] << (7-k) for k in range(8)) for i in range(40)))
    return output

def preview_packet(packet, destination):
    from PIL import Image
    image = Image.new("L", (640, 200), 0)
    pixels = image.load()
    for y, row in enumerate(screen_rows(packet)):
        for x in range(320):
            colour = 255 if row[x//8] & (128 >> (x % 8)) else 0
            for dx in range(2):
                for dy in range(2):
                    pixels[x*2+dx, 8+y*2+dy] = colour
    image.resize((1280, 800), Image.Resampling.NEAREST).save(destination)

def write_outputs(packet, decoded, output):
    output.mkdir(parents=True, exist_ok=True)
    (output/"measurement.bin").write_bytes(packet[:decoded["length"]])
    (output/"measurement.json").write_text(json.dumps(decoded, indent=2)+"\n")
    with (output/"observations.csv").open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(("profile", "section", "parameters", "case", "observed"))
        for index, profile in enumerate(decoded["profiles"]):
            parameters = {k:v for k,v in profile.items() if k not in ("section", "planes", "cases", "observations")}
            for case, value in enumerate(profile["observations"]):
                writer.writerow((index, profile["section"], json.dumps(parameters, sort_keys=True), case, value))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--packet", action="store_true", help="input is a raw measurement packet")
    parser.add_argument("--shr", action="store_true", help="input is an emulator's 32000-byte E1:2000 framebuffer")
    parser.add_argument("--crop", nargs=4, type=int, metavar=("X", "Y", "WIDTH", "HEIGHT"), help="whole active picture, including heading")
    parser.add_argument("--code-crop", nargs=4, type=float, metavar=("X", "Y", "WIDTH", "HEIGHT"), help="encoded rectangle only, excluding heading; small alignment errors are fitted using sync bars")
    parser.add_argument("--corners", nargs=8, type=float, help="whole picture TL,TR,BR,BL x/y coordinates, for a photograph")
    parser.add_argument("--output", type=Path, default=Path("decoded-mmu"))
    args = parser.parse_args()
    if args.packet:
        packet = args.image.read_bytes()
        decoded = decode_packet(packet)
    else:
        if args.shr:
            rows = shr_rows(args.image.read_bytes())
        else:
            from PIL import Image
            rows = image_rows(Image.open(args.image), args.crop, args.corners, args.code_crop)
        packet, decoded = packet_from_rows(rows)
    write_outputs(packet, decoded, args.output)
    summary = {k:decoded[k] for k in ("format", "rom_revision", "flags", "complete", "measured_cases", "vbl_edges", "anomalies", "dictionary_count", "length", "crc32")}
    print(json.dumps(summary, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
