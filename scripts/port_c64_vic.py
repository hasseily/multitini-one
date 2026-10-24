#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce the pinned C64 VIC port and documented VICE timing corrections."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
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


def fast_port(s):
    s = replace_once(s, '\t\tregisteredAddress : boolean;', '\t\tFAST_PORT : boolean := false;\n\t\tregisteredAddress : boolean;')
    s = replace_once(s, '\t\tclk: in std_logic;', '''\t\tfast_phase : in unsigned(4 downto 0) := (others => '0');
\t\tfast_rd_addr : in unsigned(5 downto 0) := (others => '0');
\t\tfast_rd_data : out unsigned(7 downto 0);
\t\tfast_wr : in std_logic := '0';
\t\tfast_wa : in unsigned(5 downto 0) := (others => '0');
\t\tfast_wd : in unsigned(7 downto 0) := (others => '0');
\t\tfast_clr_m2m : in std_logic := '0';
\t\tfast_clr_m2d : in std_logic := '0';
\t\tfast_hold_d011 : out std_logic;
\t\tfast_hold_spr : out std_logic;
\t\tfast_native_busy : out std_logic;
\t\tclk: in std_logic;''')
    wr_start = s.index('writeRegisters: process(clk)')
    rd_start = s.index('readRegisters: process(clk)')
    wr = s[wr_start:rd_start]
    cases = re.findall(r'case (?:aRegisters|addr_r) is\n(.*?)end case;', wr, re.S)
    if len(cases) != 5:
        raise ValueError("Pinned VIC write-register case structure changed")
    mapping = {}
    for body in cases[1:]: # Omit native HMOS transient FF writes.
        for addr, statements in re.findall(r'when "([01]{6})" =>(.*?)(?=\s*when |\Z)', body, re.S):
            lines = [x.strip().replace('diRegisters', 'fast_wd').replace('di_r', 'fast_wd')
                     for x in statements.strip().splitlines() if not x.strip().startswith('--')]
            mapping.setdefault(addr, []).extend(lines)
    fast_wr = '\t\t\t\tif FAST_PORT and fast_wr = \'1\' then\n\t\t\t\t\tcase fast_wa is\n'
    for a, lines in sorted(mapping.items()):
        fast_wr += f'\t\t\t\t\twhen "{a}" =>\n' + ''.join('\t\t\t\t\t\t' + line + '\n' for line in lines)
    fast_wr += '\t\t\t\t\twhen others => null;\n\t\t\t\t\tend case;\n\t\t\t\tend if;\n'
    anchor = '\t\t\tend if;\n\t\tend if;\n\tend process;'
    wr_new = replace_once(wr, anchor, fast_wr + anchor)
    s = s[:wr_start] + wr_new + s[rd_start:]
    readcase = s.split('readRegisters: process(clk)', 1)[1].split('case aRegisters is\n', 1)[1].split('\t\t\tend case;', 1)[0]
    readcase = readcase.replace('do <=', 'fast_rd_data <=').replace('fast_rd_data <= MC;', 'fast_rd_data <= MCDelay;')
    fast_read = '''
-- Acceptance-time hazards include the registered pop-to-landing edge.
fast_hold_d011 <= '1' when FAST_PORT and fast_phase >= 10 and fast_phase <= 13 else '0';
fast_hold_spr <= '1' when FAST_PORT and
    ((vicCycle = cycleSpriteA and sprite = 0 and fast_phase >= 14 and fast_phase <= 29)
     or (vicCycle = cycleCalcSprites and fast_phase = 13)) else '0';
fast_native_busy <= we_r when FAST_PORT else '0';
fastReadRegisters: process(all)
begin
    fast_rd_data <= (others => '1');
    if FAST_PORT then
        case fast_rd_addr is
''' + readcase + '''        end case;
    end if;
end process;
'''
    s = replace_once(s, 'end architecture;', fast_read + 'end architecture;')
    # Append masked clears after stock updates, retaining stock set/clear priority.
    a = s.index('spriteSpriteCollision: process(clk)')
    b = s.index('spriteBackgroundCollision: process(clk)')
    part = s[a:b]
    part = replace_once(part, 'process(clk)\n', 'process(clk)\n\tvariable fast_next : unsigned(7 downto 0);\n')
    clear = '''\t\t\tif FAST_PORT and fast_clr_m2m = '1' then
\t\t\t\tfast_next := M2M;
\t\t\t\tif enaPixel = '1' then
\t\t\t\t\tif collision /= 0 and (collision and (collision - 1)) /= 0 then
\t\t\t\t\t\tfast_next := fast_next or collision;
\t\t\t\t\tend if;
\t\t\t\t\tif M2MClr = '1' then fast_next := (others => '0'); end if;
\t\t\t\tend if;
\t\t\t\tM2M <= fast_next and not fast_wd;
\t\t\t\tM2Mhit <= '0';
\t\t\tend if;
'''
    part = replace_once(part, '\t\tend if;\n\tend process;', clear + '\t\tend if;\n\tend process;')
    s = s[:a] + part + s[b:]
    a = s.index('spriteBackgroundCollision: process(clk)')
    b = s.index('-- Generate IRQ signal', a)
    part = s[a:b]
    part = replace_once(part, 'process(clk)\n', 'process(clk)\n\tvariable fast_next, fast_delay : unsigned(7 downto 0);\n')
    clear = '''\t\t\tif FAST_PORT and fast_clr_m2d = '1' then
\t\t\t\tfast_next := M2D;
\t\t\t\tfast_delay := M2DDelay;
\t\t\t\tif enaPixel = '1' then
\t\t\t\t\tfast_next := M2DDelay;
\t\t\t\t\tfor i in 0 to 7 loop
\t\t\t\t\t\tif ((MC(i) = '0' and MCurrentPixel(i)(1) = '1') or
\t\t\t\t\t\t    (MC(i) = '1' and MCurrentPixel(i) /= "00"))
\t\t\t\t\t\tand pixelBgFlag = '1' and TBBorder = '0' then fast_delay(i) := '1'; end if;
\t\t\t\t\tend loop;
\t\t\t\tend if;
\t\t\t\tif myRd = '1' and aRegisters = "011111" then
\t\t\t\t\tfast_next := (others => '0'); fast_delay := (others => '0');
\t\t\t\tend if;
\t\t\t\tM2D <= fast_next and not fast_wd;
\t\t\t\tM2DDelay <= fast_delay and not fast_wd;
\t\t\t\tM2Dhit <= '0';
\t\t\tend if;
'''
    part = replace_once(part, '\t\tend if;\n\tend process;', clear + '\t\tend if;\n\tend process;')
    return s[:a] + part + s[b:]


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
    source = fast_port(source)
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
