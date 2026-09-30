# SPDX-License-Identifier: GPL-3.0-only
"""Exercise AMP build transforms and the production mailbox protocol."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load_builder():
    spec = importlib.util.spec_from_file_location(
        "cpu1_build_under_test", ROOT / "scripts/create_vitis_workspace.py")
    module = importlib.util.module_from_spec(spec)
    with patch.dict(sys.modules, {"vitis": types.ModuleType("vitis")}):
        spec.loader.exec_module(module)
    return module


def elf_fixture(base, memory_size=256):
    data = bytearray(128)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<II", data, 24, base, 52)
    struct.pack_into("<HH", data, 42, 32, 1)
    struct.pack_into("<6I", data, 52, 1, 84, base, base, 44, memory_size)
    return data


class Cpu1BuildTests(unittest.TestCase):
    def setUp(self):
        self.builder = load_builder()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.builder.WORKSPACE = self.root

    def fixture(self):
        directory = self.root / "worker" / "src"
        directory.mkdir(parents=True)
        (directory / "UserConfig.cmake").write_text(
            "set(USER_COMPILE_OPTIMIZATION_LEVEL -O0)\n"
            "set(USER_COMPILE_OPTIONS \" -g\")\n")
        (directory / "lscript.ld").write_text(
            "_STACK_SIZE = DEFINED(_STACK_SIZE) ? _STACK_SIZE : 0x2000;\n"
            "_HEAP_SIZE = DEFINED(_HEAP_SIZE) ? _HEAP_SIZE : 0x2000;\n"
            "MEMORY { ps7_ddr_0_memory_0 : ORIGIN = 0x100000, LENGTH = 0x3ff00000 }\n"
            ".rodata : { *(.rodata) } > ps7_ddr_0_memory_0\n")
        return directory

    def test_cpu1_build_is_bounded_optimized_and_neon(self):
        directory = self.fixture()
        self.builder.configure_amp_application("worker", True)
        self.builder.configure_amp_application("worker", True)
        linker = (directory / "lscript.ld").read_text()
        config = (directory / "UserConfig.cmake").read_text()
        self.assertIn("ORIGIN = 0x20000000, LENGTH = 0x1000000", linker)
        self.assertIn("_STACK_SIZE : 0x40000", linker)
        self.assertIn("_HEAP_SIZE : 0x10000", linker)
        self.assertIn("USER_COMPILE_OPTIMIZATION_LEVEL -O3", config)
        self.assertEqual(config.count("-mfpu=neon"), 1)
        self.assertEqual(config.count('APPEND USER_LINK_LIBRARIES "m"'), 1)

    def test_cpu0_cannot_overlap_worker_or_shared_memory(self):
        directory = self.fixture()
        self.builder.configure_amp_application("worker", False)
        linker = (directory / "lscript.ld").read_text()
        self.assertIn("ORIGIN = 0x100000, LENGTH = 0x1ff00000", linker)
        self.assertIn("KEEP(*(.mt_video_core1))", linker)
        self.assertIn("USER_COMPILE_OPTIMIZATION_LEVEL -O2",
                      (directory / "UserConfig.cmake").read_text())

    def test_cpu1_bsp_configures_amp_before_regeneration(self):
        calls = []
        class Domain:
            def set_config(self, **kwargs): calls.append(kwargs)
            def regenerate(self): calls.append("regenerate")
        class Platform:
            def add_domain(self, **kwargs):
                calls.append(kwargs)
                return Domain()
        self.builder.configure_cpu1_domain(Platform())
        self.assertEqual(calls[0]["cpu"], "ps7_cortexa9_1")
        self.assertEqual(calls[1]["value"], "-DUSE_AMP=1")
        self.assertEqual(calls[-1], "regenerate")

    def test_reuse_rejects_legacy_cpu0_only_platform(self):
        xpfm = self.root / "platform.xpfm"
        with self.assertRaises(FileNotFoundError):
            self.builder.validate_reused_cpu1(xpfm)

    def test_elf_audit_checks_bss_and_entry_not_only_file_bytes(self):
        path = self.root / "worker.elf"
        path.write_bytes(elf_fixture(0x20000000))
        self.builder.validate_elf_memory(path, 0x20000000, 256)
        for base, size in ((0x100000, 256), (0x20000000, 257)):
            path.write_bytes(elf_fixture(base, size))
            with self.assertRaises(RuntimeError):
                self.builder.validate_elf_memory(path, 0x20000000, 256)
        data = elf_fixture(0x20000000)
        struct.pack_into("<I", data, 28, 120)
        path.write_bytes(data)
        with self.assertRaises(RuntimeError):
            self.builder.validate_elf_memory(path, 0x20000000, 256)

    def test_build_fingerprint_rejects_changed_headers_platform_and_uncaptured_sources(self):
        self.builder.ROOT = self.root
        self.builder.__file__ = str(self.root / "scripts/create_vitis_workspace.py")
        script = Path(self.builder.__file__)
        script.parent.mkdir()
        script.write_text("fixture build script")
        source = self.root / "firmware/module.c"
        source.parent.mkdir()
        source.write_text('#include "module.h"\n')
        header = source.with_suffix(".h")
        header.write_text("original header")
        platform = self.root / "platform"
        platform.mkdir()
        library = platform / "libxil.a"
        library.write_bytes(b"platform")
        self.builder.XSA = platform / "hardware.xsa"
        self.builder.XSA.write_bytes(b"xsa")
        for app in (self.builder.APP_NAME, self.builder.CPU1_APP_NAME):
            commands = self.root / app / "build/compile_commands.json"
            commands.parent.mkdir(parents=True)
            commands.write_text(json.dumps([{"file": str(source)}]))
        record = self.builder.capture_build_inputs(platform / "platform.xpfm")
        verified = self.builder.verify_build_inputs(record)
        self.assertEqual(set(verified["compiled"]), {"diagnostic", "video_worker"})
        self.assertIn("firmware/module.h", record["files"])
        header.write_text("modified header")
        with self.assertRaisesRegex(RuntimeError, "source changed"):
            self.builder.verify_build_inputs(record)
        header.write_text("original header")
        library.write_bytes(b"modified platform")
        with self.assertRaisesRegex(RuntimeError, "platform changed"):
            self.builder.verify_build_inputs(record)
        library.write_bytes(b"platform")
        unknown = self.root / "uncaptured.c"
        unknown.write_text("late source")
        commands.write_text(json.dumps([{"file": str(unknown)}]))
        with self.assertRaisesRegex(RuntimeError, "not captured"):
            self.builder.verify_build_inputs(record)

    def test_cpu1_mailbox_protocol(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = self.root / "worker_test.c"
        source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define MT_VIDEO_WORKER_HOST_TEST
#define MT_VIDEO_WORKER_NO_DISPATCH
#include "firmware/cpu1/main.c"
static int calls, failure, oversize;
int mt_video_worker_execute(const void *input, uint32_t size,
                            void *output, uint32_t *capacity) {
    assert(((uintptr_t)input & 31U) == 0U);
    assert(((uintptr_t)output & 31U) == 0U);
    assert(*capacity == 4096U);
    ++calls;
    memcpy(output, input, size);
    *capacity = oversize ? 4097U : size;
    return failure;
}
int main(void) {
    mt_video_worker_mailbox_t box = {0};
    assert(!worker_step(&box));
    box.magic = MT_VIDEO_WORKER_MAGIC;
    box.abi = MT_VIDEO_WORKER_ABI;
    box.request_epoch = 42U;
    box.input_size = 4096U;
    memset(box.input, 0x5a, sizeof(box.input));
    box.request_sequence = 1U;
    assert(worker_step(&box));
    assert(calls == 1 && box.complete_epoch == 42U && box.complete_sequence == 1U);
    assert(box.result == 0 && box.output_size == 4096U);
    assert(memcmp(box.input, box.output, 4096U) == 0);
    assert(!worker_step(&box) && calls == 1);
    box.input_size = 4097U;
    box.request_sequence = 2U;
    assert(worker_step(&box) && calls == 1 && box.result != 0 && !box.output_size);
    box.input_size = 1U;
    box.abi++;
    box.request_sequence = 3U;
    assert(worker_step(&box) && calls == 1 && box.result != 0);
    box.abi = MT_VIDEO_WORKER_ABI;
    box.request_sequence = 4U;
    oversize = 1;
    assert(worker_step(&box) && calls == 2 && box.result != 0 && !box.output_size);
    oversize = 0;
    failure = -7;
    box.request_sequence = UINT32_MAX;
    assert(worker_step(&box) && box.result == -7 && !box.output_size);
    failure = 0;
    box.request_epoch = 43U;
    box.request_sequence = 1U;
    assert(worker_step(&box) && box.result == 0 && box.complete_epoch == 43U);
    return 0;
}
''')
        executable = self.root / ("worker_test.exe" if os.name == "nt" else "worker_test")
        guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
        subprocess.run(guard + [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(ROOT), "-I", str(ROOT / "firmware/include"),
                       str(source), "-o", str(executable)], check=True,
                       capture_output=True, text=True)
        subprocess.run(guard + [str(executable)], check=True,
                       capture_output=True, text=True)

    def test_production_dispatcher_accepts_settings_and_rejects_frame_jobs(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = self.root / "production_dispatch_test.c"
        source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define MT_VIDEO_WORKER_HOST_TEST
/* Expanding either macro must fail compilation: production must not even
 * compile a path that touches FPGA-owned frame memory. No frame kernel is
 * linked; the separate explicit reference test retains that dispatcher. */
#define MT_VIDEO_FRAME_CAPTURE_POINTER(slot) forbidden_capture_pool_access
#define MT_VIDEO_FRAME_OUTPUT_POINTER(slot) forbidden_output_pool_access
#include "firmware/cpu1/main.c"
int main(void) {
    mt_video_worker_mailbox_t box = {0};
    mt_video_effects_settings_t settings;
    mt_video_frame_job_t old_job = {MT_VIDEO_FRAME_JOB_MAGIC, MT_VIDEO_FRAME_JOB_ABI,
        1, 10, 1000, 1, 2, 3, 2, 128, {1, 2, 1, 3}};
    uint8_t old_output[MT_VIDEO_WORKER_PAYLOAD_BYTES];
    mt_video_effects_defaults(&settings);
    box.magic = MT_VIDEO_WORKER_MAGIC;
    box.abi = MT_VIDEO_WORKER_ABI;
    box.request_epoch = 99U;
    box.request_sequence = 1U;
    box.input_size = sizeof(settings);
    memcpy(box.input, &settings, sizeof(settings));
    assert(worker_step(&box));
    assert(box.result == 0 && box.output_size == sizeof(mt_video_effects_tables_t));
    memcpy(old_output, box.output, sizeof(old_output));
    box.input_size = sizeof(old_job);
    memcpy(box.input, &old_job, sizeof(old_job));
    box.request_sequence++;
    assert(worker_step(&box));
    assert(box.result != 0 && box.output_size == 0);
    assert(box.complete_epoch == 99U && box.complete_sequence == box.request_sequence);
    assert(memcmp(old_output, box.output, sizeof(old_output)) == 0);
    for (unsigned size = 0; size <= 80; ++size) {
        if (size == sizeof(settings)) continue;
        box.input_size = size;
        box.request_sequence++;
        assert(worker_step(&box) && box.result != 0 && box.output_size == 0);
    }
    return 0;
}
''')
        executable = self.root / ("production_dispatch_test.exe" if os.name == "nt" else "production_dispatch_test")
        guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
        subprocess.run(guard + [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(ROOT), "-I", str(ROOT / "firmware/include"),
                       str(source), str(ROOT / "firmware/common/video_effects.c"),
                       "-lm", "-o", str(executable)], check=True,
                       capture_output=True, text=True)
        subprocess.run(guard + [str(executable)], check=True,
                       capture_output=True, text=True)

    def test_cpu0_bounded_service_and_epoch_ownership(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        (self.root / "xil_cache.h").write_text(
            "#include <stdint.h>\ntypedef intptr_t INTPTR; typedef uint32_t u32;\n"
            "static inline void Xil_DCacheFlushRange(INTPTR p, u32 n) {(void)p;(void)n;}\n")
        (self.root / "xil_io.h").write_text(
            "static inline void Xil_Out32(unsigned p,unsigned n){(void)p;(void)n;}\n")
        (self.root / "xil_mmu.h").write_text(
            "#define NORM_NONCACHE 1U\n#define STRONG_ORDERED 2U\n"
            "static inline void Xil_SetTlbAttributes(unsigned p,unsigned n){(void)p;(void)n;}\n")
        (self.root / "xiltimer.h").write_text(
            "#include <stdint.h>\ntypedef uint64_t XTime;\n"
            "#define COUNTS_PER_SECOND 1000U\nextern uint64_t fake_time;\n"
            "static inline void XTime_GetTime(XTime *t){*t=fake_time;}\n")
        source = self.root / "service_test.c"
        source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define MT_VIDEO_WORKER_HOST_TEST
#define MT_VIDEO_WORKER_NO_DISPATCH
#include "firmware/platform/xilinx/video_worker.c"
#include "firmware/cpu1/main.c"
const unsigned char mt_video_core1_blob_start[1] = {0};
const unsigned char mt_video_core1_blob_end[1] = {0};
uint64_t fake_time;
static int execute_error;
int mt_video_worker_execute(const void *input, uint32_t size,
                            void *output, uint32_t *capacity) {
    assert(*capacity >= size);
    memcpy(output, input, size);
    *capacity = size;
    return execute_error;
}
int main(void) {
    mt_video_worker_mailbox_t box = {0};
    uint8_t source[2] = {42, 23}, output[2] = {0};
    uint32_t size = 100;
    mailbox = &box;
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_STOPPED);
    assert(!mt_video_worker_submit(1, source, sizeof(source)));
    started = true;
    fake_time = 999;
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_STARTING);
    fake_time = 1000;
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_FAILED);
    timed_out = false;
    fake_time = 0;
    box.magic = box.ready = MT_VIDEO_WORKER_MAGIC;
    box.abi = MT_VIDEO_WORKER_ABI;
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_IDLE);
    assert(!mt_video_worker_submit(7, 0, 1));
    assert(!mt_video_worker_submit(7, source, 4097));
    assert(mt_video_worker_submit(7, source, sizeof(source)));
    assert(!mt_video_worker_quiesce());
    assert(!mt_video_worker_submit(8, source, sizeof(source)));
    source[0] = 99; /* publication owns a copy, not this caller buffer */
    assert(!mt_video_worker_submit(8, source, sizeof(source)));
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_BUSY);
    assert(worker_step(&box));
    assert(mt_video_worker_quiesce());
    assert(!mt_video_worker_submit(8, source, sizeof(source)));
    assert(mt_video_worker_resume());
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_COMPLETE);
    assert(!mt_video_worker_collect(8, output, sizeof(output), &size) && !size);
    assert(!mt_video_worker_collect(7, output, 1, &size) && !size);
    assert(mt_video_worker_collect(7, output, sizeof(output), &size));
    assert(size == 2 && output[0] == 42 && output[1] == 23);
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_IDLE);
    assert(!mt_video_worker_collect(7, output, sizeof(output), &size));
    box.request_sequence = box.complete_sequence = collected_sequence = UINT32_MAX;
    assert(mt_video_worker_submit(8, source, sizeof(source)) && box.request_sequence == 1);
    execute_error = -7;
    assert(worker_step(&box));
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_FAILED);
    execute_error = 0;
    assert(mt_video_worker_submit(9, source, sizeof(source)));
    fake_time = 1999;
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_BUSY);
    fake_time = 2000;
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_FAILED);
    assert(worker_step(&box));
    assert(mt_video_worker_status().state == MT_VIDEO_WORKER_FAILED);
    assert(!mt_video_worker_submit(10, source, sizeof(source)));
    return 0;
}
''')
        executable = self.root / ("service_test.exe" if os.name == "nt" else "service_test")
        guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
        subprocess.run(guard + [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(self.root), "-I", str(ROOT),
                       "-I", str(ROOT / "firmware/include"), str(source),
                       "-o", str(executable)], check=True, capture_output=True, text=True)
        subprocess.run(guard + [str(executable)], check=True,
                       capture_output=True, text=True)

    def test_reference_frame_slot_validation_history_and_elapsed_decay(self):
        compiler = os.environ.get("CC") or shutil.which("gcc")
        if not compiler and Path("C:/msys64/ucrt64/bin/gcc.exe").is_file():
            compiler = "C:/msys64/ucrt64/bin/gcc.exe"
        if not compiler:
            self.skipTest("host C compiler unavailable")
        source = self.root / "frames_test.c"
        source.write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "multitini_video_frame.h"
static uint8_t capture[MT_VIDEO_FRAME_SLOT_BYTES * MT_VIDEO_FRAME_SLOT_COUNT];
static uint8_t display[MT_VIDEO_FRAME_SLOT_BYTES * MT_VIDEO_FRAME_SLOT_COUNT];
#define MT_VIDEO_FRAME_CAPTURE_POINTER(slot) ((const uint16_t *)(capture + (slot) * MT_VIDEO_FRAME_SLOT_BYTES))
#define MT_VIDEO_FRAME_OUTPUT_POINTER(slot) ((uint16_t *)(display + (slot) * MT_VIDEO_FRAME_SLOT_BYTES))
#define MT_VIDEO_WORKER_HOST_TEST
#define MT_VIDEO_WORKER_FRAME_REFERENCE
#include "firmware/cpu1/main.c"
static unsigned calls, effects_calls, expected_steps;
static bool expected_reset, process_fail;
int mt_video_effects_worker_execute(const void *input, uint32_t size,
                                    void *output, uint32_t *capacity) {
    (void)input; (void)size; (void)output; *capacity = 0;
    ++effects_calls;
    return 0;
}
bool mt_video_frame_process(const uint16_t *input, uint16_t *output,
    uint32_t *history, uint32_t *base, uint32_t *scratch, uint32_t width,
    uint32_t height, const mt_video_frame_options_t *options, uint32_t steps, bool reset) {
    assert(input != output && history != base && base != scratch);
    assert(options->enabled == 1);
    assert(reset == expected_reset && steps == expected_steps);
    ++calls;
    for (uint32_t i = 0; i < width * height; ++i) output[i] = (uint16_t)~input[i];
    return !process_fail;
}
static void run(mt_video_frame_job_t *job, bool reset, uint32_t steps) {
    mt_video_frame_result_t result;
    uint32_t size = sizeof(result);
    expected_reset = reset; expected_steps = steps;
    assert(mt_video_worker_execute(job, sizeof(*job), &result, &size) == 0);
    assert(size == sizeof(result) && result.sequence == job->sequence);
    assert(result.epoch == job->epoch && result.output_slot == job->output_slot);
}
int main(void) {
    mt_video_frame_job_t job = {MT_VIDEO_FRAME_JOB_MAGIC, MT_VIDEO_FRAME_JOB_ABI,
        1, 10, 1000, 1, 2, 3, 2, 128, {1, 2, 1, 3}};
    mt_video_frame_job_t bad;
    mt_video_frame_result_t result;
    mt_video_worker_mailbox_t unused = {0};
    uint32_t size;
    uint16_t *src = (uint16_t *)(capture + MT_VIDEO_FRAME_SLOT_BYTES);
    uint16_t *dst = (uint16_t *)(display + 2U * MT_VIDEO_FRAME_SLOT_BYTES);
    src[0] = 0x1234; src[1] = 0xabcd; src[2] = 0;
    src[64] = 0xff; src[65] = 0x5555; src[66] = 0xffff;
    memset(display, 0xa5, sizeof(display));
    assert(!worker_step(&unused));
    run(&job, true, 1);
    assert(dst[0] == 0xedcb && dst[1] == 0x5432 && dst[2] == 0xffff);
    assert(dst[64] == 0xff00 && dst[65] == 0xaaaa && dst[66] == 0);
    assert(dst[3] == 0xa5a5 && dst[63] == 0xa5a5 && dst[67] == 0xa5a5);
    job.timestamp_ms = 1008; run(&job, false, 0);
    job.timestamp_ms = 1020; run(&job, false, 1);
    job.timestamp_ms = 1040; run(&job, false, 1);
    job.epoch++; run(&job, true, 1);
    job.options.blur++; run(&job, true, 1);
    job.timestamp_ms = 100000; run(&job, false, 64);
    job.epoch++; job.timestamp_ms = UINT32_MAX - 4U; run(&job, true, 1);
    job.timestamp_ms = 10; run(&job, false, 0);
    for (unsigned which = 0; which < 10; ++which) {
        const unsigned old_calls = calls;
        bad = job; size = sizeof(result);
        switch (which) {
        case 0: bad.magic++; break;
        case 1: bad.abi++; break;
        case 2: bad.source_slot = 4; break;
        case 3: bad.output_slot = 4; break;
        case 4: bad.width = 0; break;
        case 5: bad.width = MT_VIDEO_FRAME_MAX_WIDTH + 1; break;
        case 6: bad.height = MT_VIDEO_FRAME_MAX_HEIGHT + 1; break;
        case 7: bad.stride--; break;
        case 8: bad.options.persistence = 4; break;
        case 9: size = sizeof(result) - 1; break;
        }
        assert(mt_video_worker_execute(&bad, sizeof(bad), &result, &size) != 0);
        assert(calls == old_calls);
    }
    size = sizeof(result);
    assert(mt_video_worker_execute(&job, 1, &result, &size) == 0 && effects_calls == 1);
    process_fail = true; expected_reset = false; expected_steps = 0;
    size = sizeof(result);
    assert(mt_video_worker_execute(&job, sizeof(job), &result, &size) != 0);
    process_fail = false; run(&job, true, 1);
    /* All native transports plus the contract maximum fit one slot and
       preserve the following output slot. */
    {
        static const uint32_t geometry[][2] = {
            {382, 270}, {404, 250}, {160, 144}, {540, 720}, {640, 720}
        };
        for (unsigned i = 0; i < sizeof(geometry) / sizeof(geometry[0]); ++i) {
            job.width = geometry[i][0]; job.height = geometry[i][1];
            job.stride = ((job.width + 63U) / 64U) * 128U;
            job.epoch++;
            run(&job, true, 1);
            assert(display[3U * MT_VIDEO_FRAME_SLOT_BYTES] == 0xa5);
        }
    }
    return 0;
}
''')
        executable = self.root / ("frames_test.exe" if os.name == "nt" else "frames_test")
        guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
        subprocess.run(guard + [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(ROOT), "-I", str(ROOT / "firmware/include"),
                       str(source), "-o", str(executable)], check=True,
                       capture_output=True, text=True)
        subprocess.run(guard + [str(executable)], check=True,
                       capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
