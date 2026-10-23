#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce the pinned C64 VIC port and documented VICE timing corrections."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/c64/upstream/rtl/video_vicII_656x.vhd"
TARGET = ROOT / "hdl/core/c64/ports/video_vicII_656x.vhd"
COMMIT = "62fc697e3c9b5982c9dc76aa8cd03db545d20803"


def validate_reference() -> None:
    manifest = json.loads((ROOT / "third_party/c64/import_manifest.json").read_text())
    if manifest["source"]["commit"] != COMMIT:
        raise ValueError("Unexpected C64 source pin")
    lock = json.loads((ROOT / "provenance/imports.lock.json").read_text())
    entries = [entry for entry in lock["imports"]
               if entry["path"] == SOURCE.relative_to(ROOT).as_posix()]
    if (len(entries) != 1 or entries[0]["source"] != "MiSTer C64"
            or entries[0]["source_path"] != "rtl/video_vicII_656x.vhd"
            or hashlib.sha256(SOURCE.read_bytes()).hexdigest() != entries[0]["sha256"]):
        raise ValueError("Pinned C64 VIC source does not match the import lock")


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError("Pinned VIC port anchor changed: " + old.splitlines()[0])
    return text.replace(old, new, 1)


def port(source: str) -> str:
    source = replace_once(source,
        "\t\tcs : in std_logic;",
        "\t\t-- REU final-verify peek uses an unclocked mapped read before the CPU slot.\n"
        "\t\tread_early_active : in std_logic := '0';\n"
        "\t\tread_early : in std_logic := '0';\n"
        "\t\tcs : in std_logic;")
    source = replace_once(source,
        "\tmyRd   <= '1' when cs = '1' and phi = '1' and we = '0' and enaPixel = '1' and rasterX(1 downto 0) = \"01\" else '0';",
        "\tmyRd <= cs and not we and read_early when read_early_active = '1' else\n"
        "\t        '1' when cs = '1' and phi = '1' and we = '0' and enaPixel = '1' and rasterX(1 downto 0) = \"01\" else '0';")
    source = replace_once(source,
        "\t\tba_dma : out std_logic;",
        "\t\tba_dma : out std_logic;\n"
        "\t\tba_reu : out std_logic; -- REU observes sprite-0 activation one cycle later than CPU")
    source = replace_once(source,
        "\tsignal baSpriteLast : std_logic;",
        "\tsignal baSpriteLast : std_logic;\n"
        "\tsignal reuSprite0Grace : std_logic := '0';")
    source = replace_once(source,
        "\tbaLoc <= baChars and baSprite04 and baSprite15 and baSprite26 and baSprite37;",
        "\t-- VICE vicii_cycle_reu: only the first sprite-0 activation escapes\n"
        "\t-- the REU BA check. Keep the sampled condition through phi2; MDMA\n"
        "\t-- itself changes at the phi1 data edge. CPU BA/AEC are unaffected.\n"
        "\tprocess(clk)\n\tbegin\n\t\tif rising_edge(clk) then\n"
        "\t\t\tif reset = '1' then\n\t\t\t\treuSprite0Grace <= '0';\n"
        "\t\t\telsif phi = '0' then\n"
        "\t\t\t\treuSprite0Grace <= '0';\n"
        "\t\t\t\tif mode6569 = '1' and vicCycle = cycleCalcSprites and not MDMA(0)\n"
        "\t\t\t\tand ME(0) = '1' and MY(0) = rasterY(7 downto 0) then\n"
        "\t\t\t\t\treuSprite0Grace <= '1';\n"
        "\t\t\t\tend if;\n\t\t\tend if;\n\t\tend if;\n\tend process;\n"
        "\tba_reu <= baLoc or reuSprite0Grace;\n"
        "\tbaLoc <= baChars and baSprite04 and baSprite15 and baSprite26 and baSprite37;")
    source = replace_once(source,
        "\t\t\t\t\twhen cycleIdle1 =>\n"
        "\t\t\t\t\t\tif mode6567R8  = '1' then\n"
        "\t\t\t\t\t\t\tvicCycle <= cycleIdle2; -- New NTSC - 2 extra idle cycles (65 cycles/line)\n"
        "\t\t\t\t\t\telse\n"
        "\t\t\t\t\t\t\tvicCycle <= cycleCalcSprites; -- Old NTSC - 1 extra idle cycle (64 cycles/line)\n"
        "\t\t\t\t\t\tend if;\n"
        "\t\t\t\t\twhen cycleIdle2 => vicCycle <= cycleCalcSprites;",
        "\t\t\t\t\t-- Both NTSC variants start sprite 0 at cycle 59.\n"
        "\t\t\t\t\t-- The 6567R8's second idle belongs after sprite 7, at cycle 10.\n"
        "\t\t\t\t\twhen cycleIdle1 => vicCycle <= cycleCalcSprites;\n"
        "\t\t\t\t\twhen cycleIdle2 => vicCycle <= cycleRefresh1;")
    source = replace_once(source,
        "\t\t\t\t\t\tif sprite = 7 then\n"
        "\t\t\t\t\t\t\tvicCycle <= cycleRefresh1;\n"
        "\t\t\t\t\t\tend if;",
        "\t\t\t\t\t\tif sprite = 7 then\n"
        "\t\t\t\t\t\t\tif mode6567R8 = '1' then\n"
        "\t\t\t\t\t\t\t\tvicCycle <= cycleIdle2;\n"
        "\t\t\t\t\t\t\telse\n"
        "\t\t\t\t\t\t\t\tvicCycle <= cycleRefresh1;\n"
        "\t\t\t\t\t\t\tend if;\n"
        "\t\t\t\t\t\tend if;")
    source = replace_once(source,
        "\t\t\t\tif vicCycle = cycleRefresh1 then\n"
        "\t\t\t\t\tbaSprite37 <= '1';",
        "\t\t\t\t-- Release sprite 7 immediately after its second fetch cycle,\n"
        "\t\t\t\t-- including the 6567R8 idle before the refresh window.\n"
        "\t\t\t\tif vicCycle = cycleRefresh1 or vicCycle = cycleIdle2 then\n"
        "\t\t\t\t\tbaSprite37 <= '1';")
    source = replace_once(source,
        "cycleLast <= (vicCycle = cycleSpriteB) and (sprite = 2);",
        "-- On the 6567R8 sprite 3 straddles cycles 65/1. Other variants\n"
        "-- finish their line after sprite 2. Keep the delayed frame-zero reset.\n"
        "cycleLast <= (vicCycle = cycleSpriteA and sprite = 3) when mode6567R8 = '1'\n"
        "             else (vicCycle = cycleSpriteB and sprite = 2);")
    source = replace_once(source,
        "\t\tvariant : in std_logic_vector(1 downto 0); -- 00 - NMOS, 01 - HMOS, 10 - old HMOS",
        "\t\t-- The board mapper enables character ROM in VIC banks 0/2, except Ultimax.\n"
        "\t\tchar_rom_bank : in std_logic := '1';\n"
        "\t\tvariant : in std_logic_vector(1 downto 0); -- 00 - NMOS, 01 - HMOS, 10 - old HMOS")
    source = replace_once(source,
        "\tsignal rasterIrqDone : std_logic; -- Only one interrupt each rasterLine",
        "\tsignal rasterIrqDone : std_logic; -- Suppress only a continuously matching comparator")
    source = replace_once(source,
        "\tsignal BMMDelay: std_logic;",
        "\tsignal BMMDelay: std_logic;\n"
        "\tsignal fetchBMM, fetchECM : std_logic := '0'; -- Previous native graphics-fetch mode")
    source = replace_once(source,
        "\t\t\tVM, CB, ECM, BMM, nextChar, colCounter, rowCounter, MPtr, MCnt,\n"
        "\t\t\tMDMA_next, refreshCounter)\n\tbegin",
        "\t\t\tVM, CB, ECM, BMM, nextChar, colCounter, rowCounter, MPtr, MCnt,\n"
        "\t\t\tMDMA_next, refreshCounter, fetchBMM, fetchECM, variant, char_rom_bank)\n"
        "\t\tvariable currentAddr, previousAddr, graphicsAddr : unsigned(13 downto 0);\n"
        "\tbegin")
    source = replace_once(source,
        "\t\t\tif BMM = '1' then\n"
        "\t\t\t\tvicAddrLoc <= CB(13) & colCounter & rowCounter;\n"
        "\t\t\telse\n"
        "\t\t\t\tvicAddrLoc <= CB & nextChar(7 downto 0) & rowCounter;\n"
        "\t\t\tend if;",
        "\t\t\tcurrentAddr := CB & nextChar(7 downto 0) & rowCounter;\n"
        "\t\t\tpreviousAddr := currentAddr;\n"
        "\t\t\tif BMM = '1' then\n"
        "\t\t\t\tcurrentAddr := CB(13) & colCounter & rowCounter;\n"
        "\t\t\tend if;\n"
        "\t\t\tif fetchBMM = '1' then\n"
        "\t\t\t\tpreviousAddr := CB(13) & colCounter & rowCounter;\n"
        "\t\t\tend if;\n"
        "\t\t\tif ECM = '1' then currentAddr(10 downto 9) := \"00\"; end if;\n"
        "\t\t\tif fetchECM = '1' then previousAddr(10 downto 9) := \"00\"; end if;\n"
        "\t\t\tgraphicsAddr := currentAddr;\n"
        "\t\t\tif variant = \"00\" then\n"
        "\t\t\t\t-- NMOS BMM remains effective for the first fetch after it clears.\n"
        "\t\t\t\t-- RAM -> character-ROM transitions mix old low and new high address bits.\n"
        "\t\t\t\t-- This is VICE 3.10 vicii_fetch_graphics(), including the bank qualification.\n"
        "\t\t\t\tif fetchBMM = '1' then\n"
        "\t\t\t\t\tgraphicsAddr := CB(13) & colCounter & rowCounter;\n"
        "\t\t\t\t\tif ECM = '1' then graphicsAddr(10 downto 9) := \"00\"; end if;\n"
        "\t\t\t\tend if;\n"
        "\t\t\t\tif BMM /= fetchBMM and char_rom_bank = '1'\n"
        "\t\t\t\tand previousAddr(13 downto 12) /= \"01\"\n"
        "\t\t\t\tand currentAddr(13 downto 12) = \"01\" then\n"
        "\t\t\t\t\tgraphicsAddr := currentAddr(13 downto 8) & previousAddr(7 downto 0);\n"
        "\t\t\t\tend if;\n"
        "\t\t\tend if;\n"
        "\t\t\tvicAddrLoc <= graphicsAddr;")
    source = replace_once(source,
        "\t-- Registered address\n",
        "\t-- Advance fetch mode only after the native phi1 address has been consumed.\n"
        "\tprocess(clk)\n\tbegin\n\t\tif rising_edge(clk) then\n"
        "\t\t\tif reset = '1' then\n"
        "\t\t\t\tfetchBMM <= '0'; fetchECM <= '0';\n"
        "\t\t\telsif phi = '0' and enaData = '1' and baSync = '0' then\n"
        "\t\t\t\tfetchBMM <= BMM; fetchECM <= ECM;\n"
        "\t\t\tend if;\n\t\tend if;\n\tend process;\n\n"
        "\t-- Registered address\n")
    source = replace_once(source,
        "\t\t\tif phi = '1'\n"
        "\t\t\tand enaData = '1'\n"
        "\t\t\tand baSync = '0'\n"
        "\t\t\tand ((lastLineFlag and cycleLastDelay) or (not lastLineFlag and cycleLast)) then\n"
        "\t\t\t\trasterIrqDone <= '0';\n"
        "\t\t\tend if;\n"
        "\t\t\tif resetRasterIrq = '1' then\n"
        "\t\t\t\tIRST <= '0';\n"
        "\t\t\tend if;\n"
        "\t\t\tif (rasterIrqDone = '0')\n"
        "\t\t\tand (rasterY = rasterCmp) then\n"
        "\t\t\t\trasterIrqDone <= '1';\n"
        "\t\t\t\tIRST <= '1';\n"
        "\t\t\tend if;",
        "\t\t\tif reset = '1' then\n"
        "\t\t\t\trasterIrqDone <= '0';\n"
        "\t\t\t\tIRST <= '0';\n"
        "\t\t\telse\n"
        "\t\t\t\tif resetRasterIrq = '1' then IRST <= '0'; end if;\n"
        "\t\t\t\t-- A nonmatching comparator rearms the edge, even within this line.\n"
        "\t\t\t\t-- Acknowledging a still-matching comparator must not retrigger.\n"
        "\t\t\t\tif rasterY /= rasterCmp then\n"
        "\t\t\t\t\trasterIrqDone <= '0';\n"
        "\t\t\t\telsif rasterIrqDone = '0' then\n"
        "\t\t\t\t\trasterIrqDone <= '1';\n"
        "\t\t\t\t\tIRST <= '1';\n"
        "\t\t\t\tend if;\n"
        "\t\t\tend if;")
    return ("-- Generated by scripts/port_c64_vic.py; do not edit directly.\n"
            "-- Source: MiSTer C64 " + COMMIT + ".\n"
            "-- Local changes: 6567R8 slots/wrap, comparator rearm, NMOS fetch, REU BA/peek.\n"
            "-- Upstream notices and all other logic are retained below.\n" + source)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    validate_reference()
    expected = port(SOURCE.read_text())
    if args.check:
        if not TARGET.is_file() or TARGET.read_text() != expected:
            raise SystemExit("C64 VIC port differs from its pinned deterministic generation")
        print("PASS: C64 VIC port is reproducible from pinned source")
    else:
        TARGET.write_text(expected, encoding="utf-8", newline="\n")
        print(TARGET.relative_to(ROOT).as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
