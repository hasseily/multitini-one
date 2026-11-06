#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute a C-generated XEX boot disk with py65 and an SIOV disk hook."""

import argparse
import hashlib
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from py65.devices.mpu6502 import MPU


SECTOR_SIZE = 128
DCB = 0x0300
SIOV = 0xE459
PORTB = 0xD301
TEST_RETURN = 0x4000
STOCK_XL_DISK_BOOT = 0xC58B
STOCK_XL_MD5 = "06daac977823773a3eea3422fd26a703"


class AtariMemory(list):
    def __setitem__(self, address, value):
        if isinstance(address, int) and 0xA000 <= address < 0xC000 and (self[PORTB] & 2) == 0:
            return  # BASIC occupies this window until PORTB disables it.
        super().__setitem__(address, value)


def u16(data, offset):
    return data[offset] | data[offset + 1] << 8


def boot(atr, rom=None, fail_read=False):
    disk = atr[16:]
    first = disk[:SECTOR_SIZE]
    load_address = u16(first, 2)
    boot_sectors = first[1]
    assert first[0] == 0 and load_address == 0x0800

    memory = AtariMemory([0] * 65536)
    if rom is None:
        program = disk[:boot_sectors * SECTOR_SIZE]
        memory[load_address:load_address + len(program)] = program
        pc = load_address + 6
    else:
        memory[0xC000:] = rom
        memory[0x02D5] = SECTOR_SIZE  # Stock single-density disk size.
        pc = STOCK_XL_DISK_BOOT
    memory[PORTB] = 0xFD  # Built-in BASIC enabled at startup.
    cpu = MPU(memory=memory, pc=pc)
    cpu.stPushWord(TEST_RETURN - 1)

    requests = []
    for _ in range(200000):
        if cpu.pc == SIOV:
            device, unit, command, status = memory[DCB:DCB + 4]
            buffer = u16(memory, DCB + 4)
            length = u16(memory, DCB + 8)
            sector = u16(memory, DCB + 10)
            assert (device, unit, status) == (0x31, 1, 0x40)
            requests.append((command, sector))
            result = 1
            if command == 0x53:
                assert length == 4
                memory[buffer:buffer + length] = (0x08, 0xFF, 0xE0, 0)
            else:
                assert command == 0x52 and length == SECTOR_SIZE
                if fail_read:
                    result = 0x8A
                else:
                    offset = (sector - 1) * SECTOR_SIZE
                    assert sector > 0 and offset + length <= len(disk)
                    memory[buffer:buffer + length] = disk[offset:offset + length]
            memory[DCB + 3] = result
            cpu.y = result
            cpu.FlagsNZ(result)
            cpu.pc = (cpu.stPopWord() + 1) & 0xFFFF
        elif cpu.pc == TEST_RETURN:
            assert fail_read and cpu.p & cpu.CARRY and cpu.sp == 0xFF
            assert memory[0x6000:0x6002] == [0, 0]
            return
        elif memory[0x6001] != 0:
            break
        else:
            cpu.step()
    else:
        raise AssertionError(f"boot did not finish (PC={cpu.pc:04X}, SIO={requests})")

    assert memory[0x6000:0x6002] == [0x12, 0x12]  # Both INITs once, then the final RUN.
    assert memory[0x1FFF:0x2100] == [(index & 0xFF) ^ 0x5A for index in range(257)]
    assert memory[PORTB] == 0xFF and memory[0x03F8] == 1
    first_read = boot_sectors + 1 if rom is None else 1
    assert [sector for command, sector in requests if command == 0x52] == list(
        range(first_read, len(disk) // SECTOR_SIZE + 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native_test", type=Path, help="built test_atari_disk executable")
    parser.add_argument("--xl-rom", type=Path, help="optional user-supplied 800XL OS Rev 2 ROM")
    args = parser.parse_args()
    with TemporaryDirectory() as directory:
        fixture = Path(directory) / "xex-test.atr"
        subprocess.run([str(args.native_test.resolve()), str(fixture)], check=True)
        atr = fixture.read_bytes()
    boot(atr)
    boot(atr, fail_read=True)
    print("PASS: 6502 bootstrap relocation, both INITAD callbacks, final RUNAD and SIO read failure")
    if args.xl_rom:
        rom = args.xl_rom.read_bytes()
        if hashlib.md5(rom).hexdigest() != STOCK_XL_MD5:
            parser.error("--xl-rom must be the stock 800XL OS Rev 2 ROM")
        boot(atr, rom)
        print("PASS: stock XL Rev 2 disk bootstrap launched the generated XEX disk")


if __name__ == "__main__":
    main()
