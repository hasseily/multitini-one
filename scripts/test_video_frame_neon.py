#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# ARM/Unicorn harness setup adapted from Appletini One
# scripts/test_video_mono_neon.py at 1fd4f371e8f4ef08bc63875550564590609fcfeb.
"""Execute production ARM NEON and scalar video kernels in Unicorn.

Use --unicorn-path for an existing install, or install Unicorn separately.
The generated binaries remain under build; no board or network is accessed.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/video_frame_neon_test"
MAX_PIXELS = 460800


def run(command):
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    return subprocess.run(guard + [str(part) for part in command], check=True,
                          text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT).stdout


def build_arm():
    compiler = os.environ.get("ARM_CC") or shutil.which("arm-none-eabi-gcc") or (
        "C:/Xilinx/2025.2/Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe")
    compiler = Path(compiler)
    if not compiler.is_file():
        raise RuntimeError("Set ARM_CC to the Cortex-A9 GCC compiler")
    suffix = compiler.suffix
    prefix = compiler.stem.removesuffix("gcc")
    tool = lambda name: compiler.with_name(prefix + name + suffix)
    BUILD.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c11", "-O3", "-mcpu=cortex-a9", "-mfpu=neon", "-mfloat-abi=hard",
             "-marm", "-ffreestanding", "-fno-builtin", "-ffunction-sections",
             "-fdata-sections", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
             "-Wall", "-Wextra", "-Werror", "-I", ROOT / "firmware/include"]
    objects = []
    for variant in ("neon", "scalar"):
        obj = BUILD / f"{variant}.o"
        extra = [f"-Dmt_video_frame_process=mt_video_frame_process_{variant}",
                 f"-Dmt_video_frame_process_scaled=mt_video_frame_process_scaled_{variant}"]
        if variant == "scalar":
            extra += ["-DMT_VIDEO_FRAME_SCALAR=1", "-fno-tree-vectorize"]
        run([compiler] + flags + extra + ["-c", ROOT / "firmware/common/video_frame.c", "-o", obj])
        objects.append(obj)
    wrapper = BUILD / "harness.c"
    wrapper.write_text('''#include <stdint.h>
#include <stddef.h>
#include "multitini_video_frame.h"
uint32_t params[8];
uint16_t pixels[460800] __attribute__((aligned(32)));
uint16_t output[2][460800] __attribute__((aligned(32)));
uint32_t history[2][460800] __attribute__((aligned(32)));
uint32_t base[2][460800] __attribute__((aligned(32)));
uint32_t scratch[2][460800] __attribute__((aligned(32)));
uint32_t success;
void *memcpy(void *destination,const void *source,size_t size) {
    uint8_t *d=destination;const uint8_t *s=source;
    while(size--) *d++=*s++;
    return destination;
}
void *memmove(void *destination,const void *source,size_t size) {
    uint8_t *d=destination;const uint8_t *s=source;
    if((uintptr_t)d>(uintptr_t)s){while(size){--size;d[size]=s[size];}}
    else while(size--) *d++=*s++;
    return destination;
}
#define DECLARE(name) bool name(const uint16_t*,uint16_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t,uint32_t,uint32_t,uint32_t,const mt_video_frame_options_t*,uint32_t,bool)
DECLARE(mt_video_frame_process_scaled_neon);
DECLARE(mt_video_frame_process_scaled_scalar);
#define RUN(name,index) void run_##name(void){ \\
    mt_video_frame_options_t options={(uint8_t)params[2],(uint8_t)(params[2]>>8), \\
        (uint8_t)(params[2]>>16),(uint8_t)(params[2]>>24),(uint8_t)params[7]}; \\
    success=mt_video_frame_process_scaled_##name(pixels,output[index],history[index],base[index], \\
        scratch[index],params[0],params[1],params[5],params[6],&options,params[3],params[4]!=0);}
RUN(neon,0)
RUN(scalar,1)
''')
    elf = BUILD / "video_frame.elf"
    run([compiler] + flags + ["-nostdlib", wrapper] + objects + [
        "-Wl,--build-id=none,-Ttext=0x10000,-e,run_neon", "-lgcc", "-o", elf])
    binary = BUILD / "video_frame.bin"
    run([tool("objcopy"), "-O", "binary", elf, binary])
    assembly = run([tool("objdump"), "-d", elf])
    (BUILD / "video_frame.asm").write_text(assembly)
    for instruction in ("vld4.8", "vst4.8", "vqadd.u8", "vmax.u8"):
        if instruction not in assembly:
            raise RuntimeError(f"NEON instruction absent: {instruction}")
    symbols = {}
    for line in run([tool("nm"), "-n", elf]).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    return binary.read_bytes(), symbols


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--unicorn-path", type=Path)
    args = parser.parse_args()
    if args.unicorn_path:
        sys.path.insert(0, str(args.unicorn_path))
    try:
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
        from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
                                       UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
                                       UC_CPU_ARM_CORTEX_A9)
    except ImportError as error:
        raise RuntimeError("Unicorn unavailable; supply an existing --unicorn-path") from error
    binary, symbols = build_arm()
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    cpu.mem_map(0x10000, 0x2000000)
    cpu.mem_write(0x10000, binary)
    cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    cpu.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
    stop = 0x1FF0000
    rng = random.Random(0xC0FFEE)
    cases = [(w, h, (1, rng.randrange(4), rng.randrange(4), rng.randrange(4)),
              rng.choice((0, 1, 2, 5, 32, 64)), bool(rng.randrange(2)))
             for w in (1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 33, 63, 64, 65, 127, 160, 382, 404, 540, 640, 724)
             for h in (1, 3, 7)]
    cases += [(17, 9, (1, persistence, blur, bloom), 2, False)
              for persistence in range(4) for blur in range(4) for bloom in range(4)]
    cases += [(256, 256, (1, 0, 0, 0), 1, True),
              (31, 7, (0, 3, 3, 3), 64, False),
              (540, 720, (1, 3, 3, 3), 1, False),
              (724, 496, (1, 3, 3, 3), 1, False)]
    cases += [(256, 1, (1, strength, 0, 0), steps, False)
              for strength, bound in ((1, 18), (2, 24), (3, 33))
              for steps in (bound-1, bound, 64)]
    cases = [(*case, 3, 2) for case in cases]
    # Every mode runs through vector bodies and scalar tails at both production
    # integer scales, equivalent baseline ratios, and the coefficient bounds.
    scales = ((4, 1), (6, 1), (6, 4), (1, 64), (64, 1))
    cases += [(17, 9, (1, persistence, blur, bloom), 2, False, num, den)
              for num, den in scales for persistence in range(4)
              for blur in range(4) for bloom in range(4)]
    cases += [(w, h, (1, rng.randrange(4), rng.randrange(1, 4), rng.randrange(4)),
               1, False, num, den)
              for num, den in scales
              for w in (1, 2, 3, 7, 8, 9, 15, 16, 31, 33, 63, 65)
              for h in (1, 3)]
    cases += [(w, h, (1, 3, 3, 3), 1, False, num, den)
              for w, h, num, den in ((160, 144, 6, 1), (240, 160, 6, 1),
                                      (382, 270, 4, 1), (404, 250, 4, 1))]
    cases = [(*case, 0) for case in cases]
    # Cross every native horizontal level with every vertical/bloom level.
    # Width 17 exercises vector bodies, clamped edges and scalar tails.
    cases += [(17, 9, (1, 3, blur, bloom), 1, False, num, den, softness)
              for num, den in ((3, 2), (4, 1), (6, 1))
              for softness in (1, 2, 3) for blur in range(4) for bloom in range(4)]
    count = 0
    for width, height, options, steps, reset, scale_num, scale_den, softness in cases:
        size = width * height
        pixels = list(range(65536)) if size == 65536 else [rng.randrange(65536) for _ in range(size)]
        prior = [rng.randrange(1 << 24) for _ in range(size)]
        for index, color in enumerate((0, 0xffffff, 0x202020, 0x404040, 0x808080, 0xff0040, 0x000303)):
            if index < size:
                prior[index] = color
        cpu.mem_write(symbols["pixels"], struct.pack(f"<{size}H", *pixels))
        history = struct.pack(f"<{size}I", *prior)
        for index in range(2):
            cpu.mem_write(symbols["history"] + index * MAX_PIXELS * 4, history)
            for name, item in (("output", 2), ("history", 4), ("base", 4), ("scratch", 4)):
                if size < MAX_PIXELS:
                    cpu.mem_write(symbols[name] + (index * MAX_PIXELS + size) * item, b"\xa5" * item * 8)
        packed_options = sum(value << (8 * index) for index, value in enumerate(options))
        cpu.mem_write(symbols["params"], struct.pack("<8I", width, height, packed_options,
                                                    steps, reset, scale_num, scale_den, softness))
        for variant in ("neon", "scalar"):
            cpu.reg_write(UC_ARM_REG_SP, 0x1F00000)
            cpu.reg_write(UC_ARM_REG_LR, stop)
            cpu.emu_start(symbols[f"run_{variant}"], stop, count=1000000000)
            if cpu.reg_read(UC_ARM_REG_PC) != stop:
                raise AssertionError(f"Instruction budget exceeded: {variant} {width}x{height}")
            if cpu.mem_read(symbols["success"], 4) != b"\x01\0\0\0":
                raise AssertionError("Kernel rejected valid input")
        for name, item in (("output", 2), ("history", 4)):
            neon = cpu.mem_read(symbols[name], size * item)
            scalar = cpu.mem_read(symbols[name] + MAX_PIXELS * item, size * item)
            if neon != scalar:
                raise AssertionError(f"NEON/scalar {name} differs: {width}x{height} scale={scale_num}/{scale_den} {options} steps={steps} reset={reset}")
        for name, item in (("output", 2), ("history", 4), ("base", 4), ("scratch", 4)):
            if size < MAX_PIXELS:
                for index in range(2):
                    guard = cpu.mem_read(symbols[name] + (index * MAX_PIXELS + size) * item, item * 8)
                    if guard != b"\xa5" * item * 8:
                        raise AssertionError(f"Buffer overrun in {name}")
        count += 1
    print(f"Cortex-A9 NEON/scalar matched: {count} cases; six scale ratios; all 65536 RGB565 inputs; tail guards intact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
