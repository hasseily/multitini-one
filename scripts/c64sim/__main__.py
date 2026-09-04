# SPDX-License-Identifier: GPL-3.0-only
"""Command line runner: boot, type lines, run and dump the screen."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from c64sim.system import (DEFAULT_BASIC, DEFAULT_CHARGEN, DEFAULT_DRIVE_ROM,
                               DEFAULT_KERNAL, SimTimeout, System)
else:
    from .system import (DEFAULT_BASIC, DEFAULT_CHARGEN, DEFAULT_DRIVE_ROM,
                         DEFAULT_KERNAL, SimTimeout, System)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="c64sim", description=__doc__)
    p.add_argument("--kernal", default=str(DEFAULT_KERNAL))
    p.add_argument("--basic", default=str(DEFAULT_BASIC))
    p.add_argument("--chargen", default=str(DEFAULT_CHARGEN))
    p.add_argument("--drive-rom", default=str(DEFAULT_DRIVE_ROM))
    p.add_argument("--d64", help="disk image to mount in device 8")
    p.add_argument("--type", action="append", default=[], metavar="LINE",
                   help="type LINE plus RETURN once READY appears (repeatable)")
    p.add_argument("--boot-timeout", type=float, default=5.0, help="simulated seconds")
    p.add_argument("--ready-timeout", type=float, default=120.0, help="simulated seconds")
    p.add_argument("--run", type=float, default=0.0, help="extra simulated seconds to run")
    p.add_argument("--parallel", action="store_true", help="attach the parallel cable")
    p.add_argument("--extram", action="store_true", help="8 KiB drive RAM at $8000")
    p.add_argument("--no-badlines", action="store_true")
    p.add_argument("--ntsc", action="store_true")
    p.add_argument("--trace-bus", type=int, default=0, metavar="N",
                   help="print the last N IEC transitions")
    p.add_argument("--save-d64", help="write the drive's decoded disk to this path")
    p.add_argument("--quiet", action="store_true", help="do not print the screen")
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    system = System(kernal_path=args.kernal, basic_path=args.basic, chargen_path=args.chargen,
                    drive_rom_path=args.drive_rom, d64=args.d64, pal=not args.ntsc,
                    badlines=not args.no_badlines, parallel_cable=args.parallel,
                    extram=args.extram)
    if args.trace_bus:
        system.enable_bus_trace()
    status = 0
    try:
        seconds = system.boot_to_ready(args.boot_timeout)
        print("READY after %.3f simulated seconds" % seconds)
        for line in args.type:
            system.type_line(line)
            seconds = system.run_until_ready(args.ready_timeout)
            print("%-32s %.3f simulated seconds" % (line, seconds))
    except SimTimeout as exc:
        print("timeout: %s" % exc)
        status = 1
    if args.run:
        system.run_for(args.run)
    if not args.quiet:
        print("\n".join("|%s|" % line for line in system.screen_text()))
    if args.trace_bus:
        print(system.bus_trace(args.trace_bus))
    if args.save_d64:
        image = system.disk_image()
        image.save(args.save_d64)
        if image.decode_errors:
            print("decode errors: %d" % len(image.decode_errors))
    stats = system.stats()
    print("sim %.3f s, wall %.2f s, %.0f C64 cycles per wall second, drive track %.1f" % (
        stats["sim_seconds"], stats["wall_seconds"], stats["c64_cycles_per_wall_second"],
        system.drive.track))
    return status


if __name__ == "__main__":
    sys.exit(main())
